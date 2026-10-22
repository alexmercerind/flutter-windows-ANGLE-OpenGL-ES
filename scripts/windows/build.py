"""Fetch the pinned ANGLE checkout and build one Windows architecture."""
from __future__ import annotations

import json
import os
import shutil
from pathlib import Path

from common import (CONFIG, DEPOT, DLLS, HEADERS, LIBS, ROOT, RUNTIME_FILES, SOURCE, WORK,
                    architecture, copy_file, environment, executable, output,
                    run, sdk_root, staging)


def setup() -> None:
    depot_pin = CONFIG["depot_tools"]
    angle_pin = CONFIG["angle"]
    WORK.mkdir(exist_ok=True)
    if not (DEPOT / ".git").is_dir():
        run("git", "clone", "--depth", "1", depot_pin["repository"], DEPOT, env=os.environ.copy())
    if run("git", "rev-parse", "HEAD", cwd=DEPOT, capture=True, env=os.environ.copy()) != depot_pin["commit"]:
        run("git", "fetch", "--depth", "1", "origin", depot_pin["commit"], cwd=DEPOT, env=os.environ.copy())
        run("git", "checkout", "--detach", depot_pin["commit"], cwd=DEPOT, env=os.environ.copy())
    assert run("git", "rev-parse", "HEAD", cwd=DEPOT, capture=True, env=os.environ.copy()) == depot_pin["commit"]
    bootstrap = DEPOT / "bootstrap" / "win_tools.bat"
    if bootstrap.is_file():
        run(bootstrap, cwd=DEPOT)
    checkout = SOURCE.parent
    checkout.mkdir(parents=True, exist_ok=True)
    if not (checkout / ".gclient").is_file():
        run(executable("gclient.bat", "gclient"), "config", "--name=angle",
            angle_pin["repository"], cwd=checkout)
    run(executable("gclient.bat", "gclient"), "sync", "--no-history", "-D",
        "--revision", f"angle@{angle_pin['commit']}", cwd=checkout)
    actual = run("git", "rev-parse", "HEAD", cwd=SOURCE, capture=True)
    if actual != angle_pin["commit"] or not (SOURCE / "DEPS").is_file():
        raise RuntimeError(f"ANGLE checkout/DEPS mismatch: {actual}")
    print(f"ANGLE {actual} ({angle_pin['branch']}) ready")


def find_output(out: Path, name: str) -> Path:
    direct = out / name
    if direct.is_file():
        return direct
    found = [p for p in out.rglob(name) if p.is_file() and "obj" not in p.parts]
    if len(found) != 1:
        raise RuntimeError(f"Expected one {name} in {out}; found {found}")
    return found[0]


def build(arch: str) -> None:
    arch = architecture(arch)
    pin = CONFIG["angle"]["commit"]
    actual = run("git", "rev-parse", "HEAD", cwd=SOURCE, capture=True)
    if actual != pin:
        raise RuntimeError(f"ANGLE checkout is {actual}, expected {pin}; run setup")
    sdk = sdk_root()
    out = output(arch)
    out.mkdir(parents=True, exist_ok=True)
    args = {**CONFIG["gn_args"], "target_cpu": arch}
    (out / "args.gn").write_text("\n".join(f"{key} = {json.dumps(value)}" for key, value in sorted(args.items())) + "\n", encoding="utf-8")
    env = environment()
    env["NINJA_STATUS"] = "[%f/%t %p %e] "
    run(executable("gn.bat", "gn.exe", "gn"), "gen", out, "--fail-on-unused-args", cwd=SOURCE, env=env)
    run(executable("autoninja.bat", "autoninja"), "-C", out,
        "libEGL", "libGLESv2", "third_party/vulkan-loader/src:libvulkan",
        "third_party/SwiftShader/src/Vulkan:swiftshader_libvulkan",
        cwd=SOURCE, env=env)

    stage = staging(arch)
    if stage.exists():
        shutil.rmtree(stage)
    lib = stage / "lib"
    lib.mkdir(parents=True)
    for name in (*DLLS[:-1], *LIBS, "vk_swiftshader_icd.json"):
        copy_file(find_output(out, name), lib / name)
    compiler = sdk / "Redist" / "D3D" / arch / "d3dcompiler_47.dll"
    copy_file(compiler, lib / compiler.name)
    for name in HEADERS:
        source = SOURCE / "include" / name
        if not source.is_dir():
            raise FileNotFoundError(source)
        shutil.copytree(source, stage / "include" / name)
    licenses = stage / "LICENSES"
    copy_file(SOURCE / "LICENSE", licenses / "ANGLE.txt")
    loader_license = SOURCE / "third_party" / "vulkan-loader" / "src" / "LICENSE.txt"
    if loader_license.is_file():
        copy_file(loader_license, licenses / "Vulkan-Loader.txt")
    copy_file(SOURCE / "third_party" / "swiftshader" / "LICENSE.txt",
              licenses / "SwiftShader.txt")
    sdk_versions = sorted((p.name for p in (sdk / "Licenses").iterdir()
                           if p.is_dir() and p.name.startswith(CONFIG["windows_sdk_line"] + ".")),
                          key=lambda name: tuple(map(int, name.split("."))))
    if not sdk_versions:
        raise RuntimeError(f"Windows SDK {CONFIG['windows_sdk_line']}.x license not found")
    copy_file(sdk / "Licenses" / sdk_versions[-1] / "sdk_license.rtf",
              licenses / "Microsoft-Windows-SDK-License.rtf")
    (stage / "build-info.json").write_text(json.dumps({
        "angle_commit": actual,
        "depot_tools_commit": CONFIG["depot_tools"]["commit"],
        "architecture": arch,
    }, indent=2) + "\n", encoding="utf-8")
    print(f"Built and staged {arch}: {stage}")


def install(arch: str, *, stage: Path | None = None) -> None:
    arch = architecture(arch)
    stage = (stage or staging(arch)).resolve()
    for name in (*RUNTIME_FILES, *LIBS):
        if not (stage / "lib" / name).is_file():
            raise FileNotFoundError(stage / "lib" / name)
    for name in HEADERS:
        if not (stage / "include" / name).is_dir():
            raise FileNotFoundError(stage / "include" / name)
    destination = ROOT / "windows" / "bin" / arch
    for name in RUNTIME_FILES:
        copy_file(stage / "lib" / name, destination / "dll" / name)
    for name in LIBS:
        copy_file(stage / "lib" / name, destination / "lib" / name)
    includes = ROOT / "windows" / "include" / "external"
    for name in HEADERS:
        target = includes / name
        if not target.resolve().is_relative_to(ROOT.resolve()):
            raise RuntimeError(f"Header destination outside repository: {target}")
        if target.exists():
            shutil.rmtree(target)
        shutil.copytree(stage / "include" / name, target)
    print(f"Installed {arch} runtime and headers")
