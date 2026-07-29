from __future__ import annotations

import json
import shutil
from pathlib import Path
from typing import Any

from .collect_artifacts import HEADER_DIRECTORIES, REQUIRED_LIBS, REQUIRED_RUNTIME
from .common import (
    REPO_ROOT,
    load_lock,
    reset_directory,
    staging_dir,
    validate_architecture,
)


def load_successful_report(
    path: Path, report_name: str, architecture: str
) -> dict[str, Any]:
    if not path.is_file():
        raise RuntimeError(f"Required {report_name} is missing: {path}")
    with path.open("r", encoding="utf-8") as stream:
        report = json.load(stream)
    if report.get("architecture") != architecture:
        raise RuntimeError(
            f"{report_name} architecture mismatch: expected {architecture}, "
            f"got {report.get('architecture')!r}"
        )
    if report.get("passed") is not True:
        raise RuntimeError(f"{report_name} did not pass")
    errors = report.get("errors")
    if errors:
        raise RuntimeError(f"{report_name} contains errors: {errors}")
    return report


def install(architecture: str) -> None:
    lock = load_lock()
    architecture = validate_architecture(architecture, lock)
    staging = staging_dir(architecture)
    verify_report = load_successful_report(
        staging / "verify-report.json", "verify-report.json", architecture
    )
    smoke_report = load_successful_report(
        staging / "smoke-report.json", "smoke-report.json", architecture
    )
    for backend in ("d3d11", "swiftshader"):
        if smoke_report.get("results", {}).get(backend, {}).get("passed") is not True:
            raise RuntimeError(f"{backend} smoke result did not pass")
    if verify_report.get("errors") or smoke_report.get("errors"):
        raise RuntimeError("Verified staging reports contain errors")

    repository_bin = REPO_ROOT / "windows" / "bin" / architecture
    print(f"[1/2] Replace repository {architecture} binaries from verified staging", flush=True)
    reset_directory(repository_bin)
    repository_dll = repository_bin / "dll"
    repository_lib = repository_bin / "lib"
    repository_dll.mkdir()
    repository_lib.mkdir()
    for name in REQUIRED_RUNTIME:
        shutil.copy2(staging / name, repository_dll / name)
    for name in REQUIRED_LIBS:
        shutil.copy2(staging / "lib" / name, repository_lib / name)

    external_include = REPO_ROOT / "windows" / "include" / "external"
    print("[2/2] Replace repository headers from the same staging directory", flush=True)
    reset_directory(external_include)
    for directory in HEADER_DIRECTORIES:
        shutil.copytree(staging / "include" / directory, external_include / directory)
    print(f"Installed verified {architecture} staging into repository layout")
