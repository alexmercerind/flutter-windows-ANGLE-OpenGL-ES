from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import sys
from pathlib import Path
from typing import Any

from .common import (
    angle_source,
    checkout_root,
    command_environment,
    depot_tools_dir,
    find_executable,
    git_safe_environment,
    load_lock,
    load_toolchain_lock,
    output_dir,
    run,
    run_logged,
    validate_architecture,
    work_root,
    write_json,
)


GN_BOOLEAN = {True: "true", False: "false"}
DEPENDENCY_PATTERNS = {
    "chromium": r"'chromium_revision'\s*:\s*'([0-9a-f]{40})'",
    "swiftshader": r"/SwiftShader@([0-9a-f]{40})",
    "vulkan_loader": r"/Vulkan-Loader@([0-9a-f]{40})",
    "vulkan_headers": r"/Vulkan-Headers@([0-9a-f]{40})",
    "vulkan_tools": r"/Vulkan-Tools@([0-9a-f]{40})",
    "vulkan_validation_layers": r"/Vulkan-ValidationLayers@([0-9a-f]{40})",
    "vulkan_memory_allocator": r"/VulkanMemoryAllocator@([0-9a-f]{40})",
    "spirv_headers": r"/SPIRV-Headers@([0-9a-f]{40})",
    "spirv_tools": r"/SPIRV-Tools@([0-9a-f]{40})",
    "glslang": r"/glslang@([0-9a-f]{40})",
    "zlib": r"/third_party/zlib@([0-9a-f]{40})",
    "libcxx": r"/libcxx\.git@([0-9a-f]{40})",
    "libcxxabi": r"/libcxxabi\.git@([0-9a-f]{40})",
    "abseil": r"/third_party/abseil-cpp@([0-9a-f]{40})",
}


def gn_literal(value: Any) -> str:
    if isinstance(value, bool):
        return GN_BOOLEAN[value]
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        return json.dumps(value)
    raise TypeError(f"Unsupported GN value: {value!r}")


def resolved_gn_args(lock: dict[str, Any], architecture: str) -> dict[str, Any]:
    result = dict(lock["gn_args"])
    result["target_cpu"] = lock["architectures"][architecture]["gn_target_cpu"]
    return result


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def windows_toolchain_metadata(out: Path, architecture: str) -> dict[str, str]:
    environment_path = out / f"environment.{architecture}"
    if not environment_path.is_file():
        raise RuntimeError(f"GN toolchain environment is missing: {environment_path}")
    entries = environment_path.read_bytes().decode(
        "utf-8", errors="replace"
    ).split("\0")
    variables = {
        name.upper(): value
        for entry in entries
        if "=" in entry
        for name, value in (entry.split("=", 1),)
    }
    include = variables.get("INCLUDE", "")
    msvc_match = re.search(
        r"VC[\\/]Tools[\\/]MSVC[\\/]([^\\/;]+)", include, re.IGNORECASE
    )
    sdk_match = re.search(
        r"Windows Kits[\\/]10[\\/]include[\\/]([^\\/;]+)",
        include,
        re.IGNORECASE,
    )
    if not msvc_match or not sdk_match:
        raise RuntimeError(
            f"Could not resolve MSVC and Windows SDK versions from {environment_path}"
        )
    return {
        "msvc_tools_version": msvc_match.group(1),
        "windows_sdk_version": sdk_match.group(1),
    }


def verify_deps(lock: dict[str, Any]) -> dict[str, str]:
    deps_path = angle_source() / "DEPS"
    content = deps_path.read_text(encoding="utf-8")
    actual: dict[str, str] = {}
    for name, pattern in DEPENDENCY_PATTERNS.items():
        match = re.search(pattern, content)
        if not match:
            raise RuntimeError(f"Could not resolve {name} from {deps_path}")
        actual[name] = match.group(1)
        expected = lock["dependencies"][name]
        if actual[name] != expected:
            raise RuntimeError(
                f"DEPS mismatch for {name}: expected {expected}, got {actual[name]}"
            )
    return actual


def setup(_: argparse.Namespace) -> None:
    lock = load_lock()
    toolchain_lock = load_toolchain_lock()
    root = work_root()
    depot = depot_tools_dir()
    checkout = checkout_root()
    source = angle_source()
    depot_config = toolchain_lock["depot_tools"]
    root.mkdir(parents=True, exist_ok=True)

    if not depot.exists():
        run(
            [
                "git",
                "clone",
                "--depth",
                "1",
                depot_config["repository"],
                depot,
            ],
            env=dict(os.environ),
        )
    elif not (depot / ".git").exists():
        raise RuntimeError(f"Existing depot_tools path is not a Git checkout: {depot}")

    host_env = git_safe_environment([depot], env=dict(os.environ))
    host_git = find_executable(["git.exe", "git"], env=host_env)
    actual_remote = run(
        [host_git, "remote", "get-url", "origin"],
        cwd=depot,
        capture=True,
        env=host_env,
    ).stdout.strip()
    if actual_remote.rstrip("/") != depot_config["repository"].rstrip("/"):
        raise RuntimeError(
            f"depot_tools origin mismatch: expected {depot_config['repository']}, "
            f"got {actual_remote}"
        )
    print(
        "[depot_tools 1/4] Fetch exact locked revision "
        f"{depot_config['commit']}",
        flush=True,
    )
    run(
        [
            host_git,
            "fetch",
            "--depth",
            "1",
            "origin",
            depot_config["commit"],
        ],
        cwd=depot,
        env=host_env,
    )
    print("[depot_tools 2/4] Check out locked revision in detached mode", flush=True)
    run(
        [host_git, "checkout", "--detach", depot_config["commit"]],
        cwd=depot,
        env=host_env,
    )
    depot_head = run(
        [host_git, "rev-parse", "HEAD"],
        cwd=depot,
        capture=True,
        env=host_env,
    ).stdout.strip()
    if depot_head != depot_config["commit"]:
        raise RuntimeError(
            f"depot_tools mismatch: expected {depot_config['commit']}, got {depot_head}"
        )

    print("[depot_tools 3/4] Bootstrap pinned Windows helper wrappers", flush=True)
    bootstrap = depot / "bootstrap" / "win_tools.bat"
    if not bootstrap.is_file():
        raise RuntimeError(f"depot_tools bootstrap is missing: {bootstrap}")
    run([bootstrap], cwd=depot, env=host_env)
    missing_wrappers = [
        name for name in ("git.bat", "python3.bat") if not (depot / name).is_file()
    ]
    if missing_wrappers:
        raise RuntimeError(
            "depot_tools bootstrap did not create required wrappers: "
            + ", ".join(missing_wrappers)
        )
    print(
        f"[depot_tools 4/4] Verified revision {depot_head} and Windows wrappers",
        flush=True,
    )

    checkout.mkdir(parents=True, exist_ok=True)
    env = git_safe_environment([depot, source], env=command_environment())
    gclient = find_executable(["gclient.bat", "gclient"], env=env)
    if not (checkout / ".gclient").exists():
        run(
            [
                gclient,
                "config",
                "--name=angle",
                lock["angle"]["repository"],
            ],
            cwd=checkout,
            env=env,
        )

    revision = f"angle@{lock['angle']['commit']}"
    run(
        [gclient, "sync", "--no-history", "-D", "--revision", revision],
        cwd=checkout,
        env=env,
    )
    if not source.exists():
        raise RuntimeError(f"gclient did not create expected source directory: {source}")

    git = find_executable(["git.exe", "git"], env=env)
    head = run([git, "rev-parse", "HEAD"], cwd=source, capture=True, env=env).stdout.strip()
    if head != lock["angle"]["commit"]:
        raise RuntimeError(f"ANGLE checkout mismatch: expected {lock['angle']['commit']}, got {head}")

    actual_dependencies = verify_deps(lock)
    write_json(
        root / "setup-report.json",
        {
            "angle_commit": head,
            "dependencies": actual_dependencies,
            "depot_tools_commit": depot_head,
        },
    )
    print(f"ANGLE checkout ready: {source}")


def build(args: argparse.Namespace) -> None:
    lock = load_lock()
    toolchain_lock = load_toolchain_lock()
    architecture = validate_architecture(args.architecture, lock)
    log_path = work_root() / "logs" / f"build-{architecture}.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(
        f"ANGLE build log\narchitecture={architecture}\n",
        encoding="utf-8",
        newline="\n",
    )

    def announce(step: int, message: str) -> None:
        line = f"[{step}/6] {message}"
        print(line, flush=True)
        with log_path.open("a", encoding="utf-8", newline="\n") as log:
            log.write(line + "\n")

    announce(1, f"Validate checkout and pinned dependency revisions; log={log_path}")
    source = angle_source()
    if not source.exists():
        raise RuntimeError(
            "ANGLE checkout is missing; run python scripts/windows/angle.py setup first"
        )
    expected_depot = toolchain_lock["depot_tools"]["commit"]
    depot = depot_tools_dir()
    host_env = git_safe_environment([depot], env=dict(os.environ))
    host_git = find_executable(["git.exe", "git"], env=host_env)
    actual_depot = run(
        [host_git, "rev-parse", "HEAD"],
        cwd=depot,
        capture=True,
        env=host_env,
    ).stdout.strip()
    if actual_depot != expected_depot:
        raise RuntimeError(
            f"depot_tools mismatch: expected {expected_depot}, got {actual_depot}; "
            "run python scripts/windows/angle.py setup"
        )
    actual_dependencies = verify_deps(lock)

    announce(2, "Write the complete pinned GN argument set")
    out = output_dir(architecture)
    out.mkdir(parents=True, exist_ok=True)
    gn_args = resolved_gn_args(lock, architecture)
    args_text = "\n".join(
        f"{name} = {gn_literal(value)}" for name, value in sorted(gn_args.items())
    )
    (out / "args.gn").write_text(args_text + "\n", encoding="utf-8", newline="\n")

    env = git_safe_environment(
        [depot_tools_dir(), source], env=command_environment()
    )
    env["NINJA_STATUS"] = "[%f/%t %p %e] "
    gn = find_executable(["gn.exe", "gn"], env=env)
    announce(3, "Generate Ninja files and reject every unused GN argument")
    run_logged(
        [gn, "gen", out, "--fail-on-unused-args"],
        cwd=source,
        env=env,
        log_path=log_path,
    )
    resolved_windows_toolchain = windows_toolchain_metadata(out, architecture)

    announce(4, "Query every required value through gn args --list")
    listed: dict[str, str] = {}
    for name in sorted(gn_args):
        completed = run(
            [gn, "args", out, f"--list={name}", "--short"],
            cwd=source,
            capture=True,
            env=env,
        )
        output = completed.stdout.strip()
        if not output or name not in output:
            raise RuntimeError(f"GN did not report required argument {name}")
        listed[name] = output
    listed_text = "\n\n".join(listed[name] for name in sorted(listed)) + "\n"
    (out / "gn-args-list.txt").write_text(
        listed_text,
        encoding="utf-8",
        newline="\n",
    )
    with log_path.open("a", encoding="utf-8", newline="\n") as log:
        log.write("[resolved GN args]\n")
        log.write(listed_text)

    autoninja = find_executable(["autoninja.bat", "autoninja"], env=env)
    targets = [
        "libEGL",
        "libGLESv2",
        "third_party/SwiftShader:swiftshader",
        "third_party/vulkan-loader/src:libvulkan",
        "copy_compiler_dll",
    ]
    announce(
        5,
        "Compile ANGLE, SwiftShader, Vulkan Loader, and D3D compiler "
        f"({len(targets)} Ninja targets)",
    )
    run_logged(
        [autoninja, "-C", out, *targets],
        cwd=source,
        env=env,
        log_path=log_path,
    )

    announce(6, "Record resolved build/toolchain metadata")
    git = find_executable(["git.exe", "git"], env=env)
    clang = (
        source
        / "third_party"
        / "llvm-build"
        / "Release+Asserts"
        / "bin"
        / "clang-cl.exe"
    )
    if not clang.is_file():
        raise RuntimeError(f"Pinned checkout clang-cl is missing: {clang}")
    depot_python = depot_tools_dir() / "python3.bat"
    if not depot_python.is_file():
        raise RuntimeError(f"depot_tools Python wrapper is missing: {depot_python}")
    toolchain = {
        # depot_tools/gn.bat locates the checkout GN binary from the current
        # source tree. Keep this probe in the same cwd as every other GN call.
        "gn": run(
            [gn, "--version"], cwd=source, capture=True, env=env
        ).stdout.strip(),
        "ninja": run(
            [find_executable(["ninja.exe", "ninja"], env=env), "--version"],
            capture=True,
            env=env,
        ).stdout.strip(),
        "python": sys.version,
        "depot_tools_python": run(
            [depot_python, "--version"], capture=True, env=env
        ).stdout.strip(),
        "git": run(
            [git, "--version"], capture=True, env=env
        ).stdout.strip(),
        "depot_tools_commit": actual_depot,
        "clang": {
            "version": run(
                [clang, "--version"], cwd=source, capture=True, env=env
            ).stdout.strip(),
            "sha256": file_sha256(clang),
            "size": clang.stat().st_size,
        },
        **resolved_windows_toolchain,
    }
    report_path = out / "build-report.json"
    write_json(
        report_path,
        {
            "architecture": architecture,
            "build_host": {
                "os": platform.platform(),
                "architecture": platform.machine(),
                "runner_image": {
                    "label": toolchain_lock["runner_labels"]["build"],
                    "image_os": os.environ.get("ImageOS"),
                    "image_version": os.environ.get("ImageVersion"),
                    "runner_name": os.environ.get("RUNNER_NAME"),
                    "runner_architecture": os.environ.get("RUNNER_ARCH"),
                },
            },
            "angle_commit": run(
                [git, "rev-parse", "HEAD"], cwd=source, capture=True, env=env
            ).stdout.strip(),
            "dependencies": actual_dependencies,
            "gn_args": gn_args,
            "targets": targets,
            "toolchain": toolchain,
        },
    )
    with log_path.open("a", encoding="utf-8", newline="\n") as log:
        log.write(f"[build report] {report_path}\n")
    print(f"Build completed: {out}")


def clean(args: argparse.Namespace) -> None:
    lock = load_lock()
    architecture = validate_architecture(args.architecture, lock)
    out = output_dir(architecture)
    if out.exists():
        resolved = out.resolve()
        managed = (work_root() / "out").resolve()
        if managed not in resolved.parents:
            raise RuntimeError(f"Refusing to remove unmanaged path: {resolved}")
        shutil.rmtree(resolved)
    print(f"Removed build output: {out}")
