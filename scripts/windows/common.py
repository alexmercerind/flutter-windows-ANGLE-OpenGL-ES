"""Small shared helpers for the pinned Windows ANGLE build."""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / ".angle-work"
CONFIG = json.loads((ROOT / "config" / "angle.lock.json").read_text(encoding="utf-8-sig"))
SOURCE = WORK / "checkout" / "angle"
DEPOT = WORK / "depot_tools"
DLLS = ("libEGL.dll", "libGLESv2.dll", "vulkan-1.dll", "vk_swiftshader.dll",
        "d3dcompiler_47.dll")
LIBS = ("libEGL.dll.lib", "libGLESv2.dll.lib")
RUNTIME_FILES = (*DLLS, "vk_swiftshader_icd.json")
HEADERS = ("EGL", "GLES2", "GLES3", "KHR")


def architecture(value: str) -> str:
    if value not in CONFIG["architectures"]:
        raise ValueError(f"Expected x64 or arm64, got {value!r}")
    return value


def staging(arch: str) -> Path:
    return WORK / "staging" / architecture(arch)


def output(arch: str) -> Path:
    return SOURCE / "out" / architecture(arch)


def environment() -> dict[str, str]:
    env = os.environ.copy()
    # MSBuild treats differently cased Path entries as duplicates.
    path = next((value for key, value in env.items() if key.lower() == "path"), "")
    for key in list(env):
        if key.lower() == "path":
            del env[key]
    temporary = WORK / "tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    env["TEMP"] = env["TMP"] = env["TMPDIR"] = str(temporary)
    env["VPYTHON_VIRTUALENV_ROOT"] = str(WORK / "vpython")
    env["CIPD_CACHE_DIR"] = str(WORK / "cipd-cache")
    env["UV_CACHE_DIR"] = str(WORK / "uv-cache")
    env["Path"] = os.pathsep.join((str(DEPOT), str(SOURCE / "third_party" / "ninja"), path))
    env["DEPOT_TOOLS_WIN_TOOLCHAIN"] = "0"
    env["DEPOT_TOOLS_UPDATE"] = "0"
    env["PYTHONUTF8"] = "1"
    return env


def run(*args: str | Path, cwd: Path | None = None, capture: bool = False,
        env: dict[str, str] | None = None) -> str:
    command = [str(arg) for arg in args]
    print("+", subprocess.list2cmdline(command), flush=True)
    result = subprocess.run(command, cwd=cwd, env=env or environment(),
                            text=True, encoding="utf-8", errors="replace",
                            stdout=subprocess.PIPE if capture else None, check=True)
    return result.stdout.strip() if capture else ""


def executable(*names: str) -> str:
    for name in names:
        found = shutil.which(name, path=environment()["Path"])
        if found:
            return found
    raise FileNotFoundError(f"Required tool not found: {', '.join(names)}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def archive_name(arch: str) -> str:
    return f"ANGLE-{CONFIG['angle']['commit'][:7]}-windows-{architecture(arch)}.7z"


def sdk_root() -> Path:
    root = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits" / "10"
    line = CONFIG["windows_sdk_line"] + "."
    versions = sorted((p.name for p in (root / "Include").iterdir()
                       if p.is_dir() and p.name.startswith(line)),
                      key=lambda name: tuple(map(int, name.split("."))))
    if not versions:
        raise RuntimeError(f"Windows SDK {CONFIG['windows_sdk_line']}.x is required")
    selected = versions[-1]
    print(f"Windows SDK available: {selected}")
    return root


def copy_file(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
