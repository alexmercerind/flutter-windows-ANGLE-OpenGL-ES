from __future__ import annotations

import argparse
import json
import os
import platform
import sys
from pathlib import Path
from typing import Any

from .common import (
    REPO_ROOT,
    command_environment,
    find_executable,
    load_lock,
    load_toolchain_lock,
    run,
    smoke_build_dir,
    staging_dir,
    validate_architecture,
    write_json,
)


def find_cmake(env: dict[str, str]) -> str:
    override = env.get("ANGLE_CMAKE")
    if override:
        candidate = Path(override).resolve()
        if not candidate.is_file():
            raise FileNotFoundError(f"ANGLE_CMAKE does not exist: {candidate}")
        return str(candidate)
    try:
        return find_executable(["cmake.exe", "cmake"], env=env)
    except FileNotFoundError:
        pass

    candidates: list[Path] = []
    vs_install = env.get("VSINSTALLDIR")
    if vs_install:
        candidates.append(
            Path(vs_install)
            / "Common7"
            / "IDE"
            / "CommonExtensions"
            / "Microsoft"
            / "CMake"
            / "CMake"
            / "bin"
            / "cmake.exe"
        )
    for visual_studio_root in (
        Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
        / "Microsoft Visual Studio",
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Microsoft Visual Studio",
    ):
        if visual_studio_root.is_dir():
            candidates.extend(
                visual_studio_root.glob(
                    "*/*/Common7/IDE/CommonExtensions/Microsoft/"
                    "CMake/CMake/bin/cmake.exe"
                )
            )
    for candidate in sorted(candidates, reverse=True):
        if candidate.is_file():
            return str(candidate)
    raise FileNotFoundError("CMake was not found in PATH or Visual Studio")


def executable_path(build_dir: Path) -> Path:
    candidates = (
        build_dir / "Release" / "angle_smoke_test.exe",
        build_dir / "angle_smoke_test.exe",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"Smoke-test executable was not produced in {build_dir}")


def smoke_build_report_path(architecture: str) -> Path:
    return smoke_build_dir(architecture) / "smoke-build-report.json"


def build_smoke(args: argparse.Namespace) -> None:
    lock = load_lock()
    toolchain_lock = load_toolchain_lock()
    architecture = validate_architecture(args.architecture, lock)
    staging = Path(args.staging).resolve() if args.staging else staging_dir(architecture)
    if not (staging / "verify-report.json").is_file():
        raise RuntimeError(f"Verified staging directory is required: {staging}")
    build_dir = smoke_build_dir(architecture)
    build_dir.mkdir(parents=True, exist_ok=True)
    env = command_environment()
    cmake = find_cmake(env)
    cmake_version = run(
        [cmake, "--version"], capture=True, env=env
    ).stdout.strip()
    print(f"[1/2] Configure native {architecture} smoke test with {cmake}", flush=True)
    run(
        [
            cmake,
            "-S",
            REPO_ROOT / "tests" / "native",
            "-B",
            build_dir,
            "-A",
            lock["architectures"][architecture]["cmake_platform"],
            f"-DANGLE_ROOT={staging}",
        ],
        env=env,
    )
    print(f"[2/2] Compile native {architecture} smoke test", flush=True)
    run([cmake, "--build", build_dir, "--config", "Release"], env=env)
    executable = executable_path(build_dir)
    write_json(
        smoke_build_report_path(architecture),
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
            "cmake": {
                "executable": cmake,
                "version": cmake_version,
            },
            "python": sys.version,
            "executable": str(executable),
        },
    )
    print(f"Smoke test built: {executable}")


def run_smoke(args: argparse.Namespace) -> None:
    lock = load_lock()
    toolchain_lock = load_toolchain_lock()
    architecture = validate_architecture(args.architecture, lock)
    staging = Path(args.staging).resolve() if args.staging else staging_dir(architecture)
    executable = (
        Path(args.executable).resolve()
        if args.executable
        else executable_path(smoke_build_dir(architecture))
    )
    if not executable.is_file():
        raise RuntimeError(f"Smoke-test executable is missing: {executable}")

    host = platform.machine().lower()
    if architecture == "arm64" and host not in ("arm64", "aarch64"):
        raise RuntimeError(
            f"ARM64 smoke test must run on a native ARM64 host; detected {platform.machine()}"
        )

    env = command_environment()
    env["PATH"] = str(staging) + os.pathsep + env.get("PATH", "")
    results: dict[str, Any] = {}
    errors: list[str] = []
    backends = (
        ("d3d11", "--d3d11"),
        ("swiftshader", "--swiftshader"),
        ("interop_d3d11", "--interop-d3d11"),
        ("interop_warp", "--interop-warp"),
    )
    for index, (backend, option) in enumerate(backends, start=1):
        print(
            f"[{index}/{len(backends)}] Run native {architecture} {backend} "
            "rendering and readback",
            flush=True,
        )
        completed = run(
            [executable, option],
            cwd=staging,
            capture=True,
            check=False,
            env=env,
        )
        results[backend] = {
            "passed": completed.returncode == 0,
            "exit_code": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
        }
        if completed.returncode != 0:
            errors.append(f"{backend} smoke test failed with exit code {completed.returncode}")
        print(completed.stdout, end="")
        if completed.stderr:
            print(completed.stderr, file=sys.stderr, end="")

    report = {
        "passed": not errors,
        "architecture": architecture,
        "host_architecture": platform.machine(),
        "native_test_host": {
            "os": platform.platform(),
            "architecture": platform.machine(),
            "runner_image": {
                "label": (
                    toolchain_lock["runner_labels"]["arm64_smoke"]
                    if architecture == "arm64"
                    else toolchain_lock["runner_labels"]["build"]
                ),
                "image_os": os.environ.get("ImageOS"),
                "image_version": os.environ.get("ImageVersion"),
                "runner_name": os.environ.get("RUNNER_NAME"),
                "runner_architecture": os.environ.get("RUNNER_ARCH"),
            },
        },
        "executable": str(executable),
        "results": results,
        "errors": errors,
    }
    build_report_path = smoke_build_report_path(architecture)
    if build_report_path.is_file():
        with build_report_path.open("r", encoding="utf-8") as stream:
            report["build_toolchain"] = json.load(stream)
    write_json(staging / "smoke-report.json", report)
    if errors:
        raise RuntimeError("; ".join(errors))
    print(f"Native smoke tests passed: {architecture}")
