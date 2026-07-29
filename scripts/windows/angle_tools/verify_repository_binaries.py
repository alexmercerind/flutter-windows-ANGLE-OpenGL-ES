from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

from .collect_artifacts import HEADER_DIRECTORIES, REQUIRED_LIBS, REQUIRED_RUNTIME
from .common import (
    REPO_ROOT,
    load_lock,
    staging_dir,
    validate_architecture,
    write_json,
)
from .pe_utils import inspect_import_library, inspect_pe, sha256


def file_map(root: Path, *, recursive: bool) -> dict[str, Path]:
    if not root.is_dir():
        return {}
    paths = root.rglob("*") if recursive else root.iterdir()
    return {
        path.relative_to(root).as_posix(): path
        for path in paths
        if path.is_file()
    }


def compare_file_sets(
    label: str,
    expected_names: set[str],
    staging_root: Path,
    repository_root: Path,
    errors: list[str],
    *,
    recursive: bool = False,
    staging_ignored: set[str] | None = None,
) -> dict[str, Any]:
    staging = file_map(staging_root, recursive=recursive)
    repository = file_map(repository_root, recursive=recursive)
    for name in staging_ignored or set():
        staging.pop(name, None)
    for side, actual in (("staging", staging), ("repository", repository)):
        missing = sorted(expected_names - set(actual))
        extra = sorted(set(actual) - expected_names)
        if missing:
            errors.append(f"{label}: {side} missing files: {', '.join(missing)}")
        if extra:
            errors.append(f"{label}: {side} has extra files: {', '.join(extra)}")

    changed: list[str] = []
    metadata: dict[str, Any] = {}
    for name in sorted(expected_names):
        staging_path = staging.get(name)
        repository_path = repository.get(name)
        if staging_path is None or repository_path is None:
            continue
        staging_hash = sha256(staging_path)
        repository_hash = sha256(repository_path)
        metadata[name] = {
            "staging_sha256": staging_hash,
            "repository_sha256": repository_hash,
            "size": staging_path.stat().st_size,
        }
        if (
            staging_path.stat().st_size != repository_path.stat().st_size
            or staging_hash != repository_hash
        ):
            changed.append(name)
    if changed:
        errors.append(f"{label}: byte mismatches: {', '.join(changed)}")
    return {
        "expected": sorted(expected_names),
        "changed": changed,
        "files": metadata,
    }


def verify(args: argparse.Namespace) -> None:
    lock = load_lock()
    architecture = validate_architecture(args.architecture, lock)
    staging = (
        Path(args.staging).resolve()
        if args.staging
        else staging_dir(architecture)
    )
    repository_bin = REPO_ROOT / "windows" / "bin" / architecture
    repository_include = REPO_ROOT / "windows" / "include" / "external"
    expected_machine = lock["architectures"][architecture]["pe_machine"].lower()
    errors: list[str] = []

    print(f"[1/4] Compare exact {architecture} runtime file set and bytes", flush=True)
    runtime = compare_file_sets(
        "runtime",
        set(REQUIRED_RUNTIME),
        staging,
        repository_bin / "dll",
        errors,
        staging_ignored={
            "collect-report.json",
            "manifest.json",
            "repository-verify-report.json",
            "smoke-report.json",
            "verify-report.json",
        },
    )

    print(f"[2/4] Compare exact {architecture} import libraries and bytes", flush=True)
    import_libraries = compare_file_sets(
        "import libraries",
        set(REQUIRED_LIBS),
        staging / "lib",
        repository_bin / "lib",
        errors,
    )

    print("[3/4] Compare repository headers byte-for-byte with staging", flush=True)
    header_names = {
        path.relative_to(staging / "include").as_posix()
        for directory in HEADER_DIRECTORIES
        for path in (staging / "include" / directory).rglob("*")
        if path.is_file()
    }
    headers = compare_file_sets(
        "headers",
        header_names,
        staging / "include",
        repository_include,
        errors,
        recursive=True,
    )

    print("[4/4] Verify repository PE and COFF machine types", flush=True)
    pe_files: dict[str, Any] = {}
    for name in REQUIRED_RUNTIME:
        if not name.lower().endswith(".dll"):
            continue
        path = repository_bin / "dll" / name
        if not path.is_file():
            continue
        info = inspect_pe(path)
        pe_files[name] = info
        if info["machine"].lower() != expected_machine:
            errors.append(
                f"{name}: expected PE machine {expected_machine}, "
                f"got {info['machine']}"
            )

    coff_files: dict[str, Any] = {}
    for name in REQUIRED_LIBS:
        path = repository_bin / "lib" / name
        if not path.is_file():
            continue
        info = inspect_import_library(path)
        coff_files[name] = info
        actual = [machine.lower() for machine in info["machines"]]
        if actual != [expected_machine]:
            errors.append(
                f"{name}: expected only COFF machine {expected_machine}, "
                f"got {info['machines']}"
            )

    report = {
        "passed": not errors,
        "architecture": architecture,
        "staging": str(staging),
        "repository": str(repository_bin),
        "runtime": runtime,
        "import_libraries": import_libraries,
        "headers": headers,
        "pe_files": pe_files,
        "coff_files": coff_files,
        "errors": errors,
    }
    report_path = (
        Path(args.report).resolve()
        if args.report
        else staging / "repository-verify-report.json"
    )
    write_json(report_path, report)
    if errors:
        raise RuntimeError(
            "repository binaries do not match staging:\n- " + "\n- ".join(errors)
        )
    print(f"Repository {architecture} binaries match staging exactly")
