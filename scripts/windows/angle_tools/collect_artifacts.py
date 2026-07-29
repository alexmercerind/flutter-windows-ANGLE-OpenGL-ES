from __future__ import annotations

import argparse
import shutil
from pathlib import Path
from typing import Iterable

from .common import (
    angle_source,
    load_lock,
    output_dir,
    reset_directory,
    staging_dir,
    validate_architecture,
    write_json,
)
from .pe_utils import inspect_pe


REQUIRED_RUNTIME = (
    "d3dcompiler_47.dll",
    "libEGL.dll",
    "libGLESv2.dll",
    "vk_swiftshader.dll",
    "vk_swiftshader_icd.json",
    "vulkan-1.dll",
)
REQUIRED_LIBS = (
    "libEGL.dll.lib",
    "libGLESv2.dll.lib",
)
CONDITIONAL_RUNTIME = ("libc++.dll", "zlib.dll")
HEADER_DIRECTORIES = ("EGL", "GLES2", "GLES3", "KHR")

LICENSE_SOURCES = {
    "ANGLE.txt": (("LICENSE",),),
    "SwiftShader.txt": (
        ("third_party", "SwiftShader", "LICENSE.txt"),
        ("third_party", "SwiftShader", "LICENSE"),
    ),
    "Vulkan-Loader.txt": (
        ("third_party", "vulkan-loader", "src", "LICENSE.txt"),
        ("third_party", "vulkan-loader", "src", "LICENSE.md"),
    ),
    "Vulkan-Headers.txt": (
        ("third_party", "vulkan-headers", "src", "LICENSE.md"),
        ("third_party", "vulkan-headers", "src", "LICENSE.txt"),
    ),
    "VulkanMemoryAllocator.txt": (
        ("third_party", "vulkan_memory_allocator", "LICENSE.txt"),
        ("third_party", "vulkan_memory_allocator", "LICENSE.md"),
    ),
    "SPIRV-Tools.txt": (
        ("third_party", "spirv-tools", "src", "LICENSE"),
        ("third_party", "spirv-tools", "src", "LICENSE.txt"),
    ),
    "SPIRV-Headers.txt": (
        ("third_party", "spirv-headers", "src", "LICENSE"),
        ("third_party", "spirv-headers", "src", "LICENSE.txt"),
    ),
    "glslang.txt": (
        ("third_party", "glslang", "src", "LICENSE.txt"),
        ("third_party", "glslang", "src", "LICENSE"),
    ),
    "zlib.txt": (
        ("third_party", "zlib", "LICENSE"),
        ("third_party", "zlib", "LICENSE.txt"),
    ),
    "libcxx.txt": (
        ("third_party", "libc++", "src", "LICENSE.TXT"),
        ("third_party", "libc++", "src", "LICENSE.txt"),
    ),
    "libcxxabi.txt": (
        ("third_party", "libc++abi", "src", "LICENSE.TXT"),
        ("third_party", "libc++abi", "src", "LICENSE.txt"),
    ),
    "Abseil.txt": (
        ("third_party", "abseil-cpp", "LICENSE"),
        ("third_party", "abseil-cpp", "LICENSE.txt"),
    ),
}


def find_output(out: Path, name: str) -> Path:
    direct = out / name
    if direct.is_file():
        return direct
    matches = [
        path
        for path in out.rglob(name)
        if path.is_file() and not any(part in {"obj", "gen"} for part in path.relative_to(out).parts)
    ]
    if len(matches) != 1:
        rendered = ", ".join(str(path) for path in matches) or "none"
        raise RuntimeError(f"Expected exactly one output {name}; found {rendered}")
    return matches[0]


def copy_required(out: Path, destination: Path, names: Iterable[str]) -> dict[str, str]:
    copied: dict[str, str] = {}
    for name in names:
        source = find_output(out, name)
        target = destination / name
        shutil.copy2(source, target)
        copied[name] = str(source)
    return copied


def collect(args: argparse.Namespace) -> None:
    lock = load_lock()
    architecture = validate_architecture(args.architecture, lock)
    out = output_dir(architecture)
    source = angle_source()
    if not (out / "build-report.json").exists():
        raise RuntimeError(f"Missing successful build report in {out}")
    if not source.exists():
        raise RuntimeError("ANGLE source checkout is missing")

    staging = staging_dir(architecture)
    print(f"[1/5] Reset staging directory {staging}", flush=True)
    reset_directory(staging)
    include = staging / "include"
    lib = staging / "lib"
    licenses = staging / "LICENSES"
    include.mkdir()
    lib.mkdir()
    licenses.mkdir()

    print("[2/5] Copy required runtime DLLs, ICD JSON, and import libraries", flush=True)
    copied = copy_required(out, staging, REQUIRED_RUNTIME)
    copied.update(copy_required(out, lib, REQUIRED_LIBS))

    print("[3/5] Inspect core PE imports and resolve conditional runtimes", flush=True)
    imported: set[str] = set()
    while True:
        imported = {
            imported_name.lower()
            for path in staging.glob("*.dll")
            for imported_name in inspect_pe(path)["runtime_dependencies"]
        }
        added = False
        for name in CONDITIONAL_RUNTIME:
            if name.lower() in imported and not (staging / name).is_file():
                source_path = find_output(out, name)
                copied[name] = str(source_path)
                shutil.copy2(source_path, staging / name)
                added = True
        if not added:
            break

    print("[4/5] Copy exact pinned headers and dependency licenses", flush=True)
    for directory in HEADER_DIRECTORIES:
        header_source = source / "include" / directory
        if not header_source.is_dir():
            raise RuntimeError(f"Required ANGLE header directory is missing: {header_source}")
        shutil.copytree(header_source, include / directory)

    copied_licenses: dict[str, str] = {}
    for output_name, candidates in LICENSE_SOURCES.items():
        selected: Path | None = None
        for candidate in candidates:
            path = source.joinpath(*candidate)
            if path.is_file():
                selected = path
                break
        if selected:
            shutil.copy2(selected, licenses / output_name)
            copied_licenses[output_name] = str(selected)
    for mandatory in ("ANGLE.txt", "SwiftShader.txt", "Vulkan-Loader.txt"):
        if mandatory not in copied_licenses:
            raise RuntimeError(f"Mandatory license source was not found: {mandatory}")

    print("[5/5] Record collection sources and runtime decisions", flush=True)
    write_json(
        staging / "collect-report.json",
        {
            "architecture": architecture,
            "source_output": str(out),
            "files": copied,
            "licenses": copied_licenses,
            "conditional_runtime_decisions": {
                name: {
                    "imported_by_core": name.lower() in imported,
                    "included": (staging / name).is_file(),
                }
                for name in CONDITIONAL_RUNTIME
            },
        },
    )

    print(f"Artifacts collected: {staging}")
