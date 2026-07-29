from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

from .common import (
    REPO_ROOT,
    load_lock,
    validate_architecture,
    work_root,
    write_json,
)
from .pe_utils import inspect_pe


def verify(args: argparse.Namespace) -> None:
    lock = load_lock()
    architecture = validate_architecture(args.architecture, lock)
    release = (
        Path(args.release).resolve()
        if args.release
        else REPO_ROOT
        / "example"
        / "build"
        / "windows"
        / architecture
        / "runner"
        / "Release"
    )
    expected_machine = lock["architectures"][architecture]["pe_machine"].lower()
    errors: list[str] = []
    outputs: dict[str, Any] = {}
    if not release.is_dir():
        raise RuntimeError(f"Flutter release directory is missing: {release}")
    paths = sorted(
        (
            path
            for path in release.iterdir()
            if path.is_file() and path.suffix.lower() in (".exe", ".dll")
        ),
        key=lambda path: path.name.lower(),
    )
    if not paths:
        errors.append(f"no PE outputs found in {release}")
    required_names = {
        "flutter_windows_angle_opengl_es_example.exe",
        "flutter_windows_angle_opengl_es_plugin.dll",
    }
    actual_names = {path.name for path in paths}
    missing = sorted(required_names - actual_names)
    if missing:
        errors.append("missing Flutter outputs: " + ", ".join(missing))
    for path in paths:
        info = inspect_pe(path)
        outputs[path.name] = info
        print(f"{path.name}: machine={info['machine']} ({info['machine_name']})")
        if info["machine"].lower() != expected_machine:
            errors.append(
                f"{path.name}: expected PE machine {expected_machine}, "
                f"got {info['machine']}"
            )
    report = {
        "passed": not errors,
        "architecture": architecture,
        "expected_machine": expected_machine,
        "release_directory": str(release),
        "outputs": outputs,
        "errors": errors,
    }
    report_path = (
        Path(args.report).resolve()
        if args.report
        else work_root() / f"flutter-{architecture}-report.json"
    )
    write_json(report_path, report)
    if errors:
        raise RuntimeError("Flutter output validation failed:\n- " + "\n- ".join(errors))
    print(f"Flutter {architecture} PE validation passed")
