from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import zipfile
from pathlib import Path
from typing import Any

from .common import (
    dist_dir,
    load_lock,
    output_dir,
    reset_directory,
    staging_dir,
    validate_architecture,
    work_root,
    write_json,
)
from .pe_utils import sha256


ZIP_TIMESTAMP = (2000, 1, 1, 0, 0, 0)
REPORT_NAMES = {
    "collect-report.json",
    "verify-report.json",
    "smoke-report.json",
}


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def archive_files(package_root: Path) -> list[Path]:
    return sorted(
        (path for path in package_root.rglob("*") if path.is_file()),
        key=lambda path: path.relative_to(package_root).as_posix(),
    )


def file_metadata(package_root: Path) -> list[dict[str, Any]]:
    result = []
    for path in archive_files(package_root):
        relative = path.relative_to(package_root).as_posix()
        result.append(
            {
                "path": relative,
                "size": path.stat().st_size,
                "sha256": sha256(path),
            }
        )
    return result


def build_manifest(
    architecture: str,
    package_root: Path,
    lock: dict[str, Any],
    verify_report: dict[str, Any],
    smoke_report: dict[str, Any],
    build_report: dict[str, Any],
) -> dict[str, Any]:
    return {
        "schema_version": 2,
        "release_version": lock["release_version"],
        "architecture": architecture,
        "angle": lock["angle"],
        "flutter_angle_reference": lock["flutter_angle_reference"],
        "gn_args": build_report["gn_args"],
        "build_targets": build_report["targets"],
        "toolchain": {
            **build_report["toolchain"],
            "native_smoke": smoke_report.get("build_toolchain"),
        },
        "windows_sdk": {
            **lock["windows_sdk"],
            "resolved_build_version": build_report["toolchain"][
                "windows_sdk_version"
            ],
        },
        "dependencies": lock["dependencies"],
        "runtime": {
            "pe_files": verify_report["pe_files"],
            "import_libraries": verify_report["import_libraries"],
            "external_prerequisites": verify_report["external_prerequisites"],
            "libcxx": verify_report["conditional_runtime"]["libc++.dll"],
            "zlib": verify_report["conditional_runtime"]["zlib.dll"],
        },
        "validation": {
            "artifacts": {"passed": verify_report["passed"]},
            "headers": verify_report["headers"],
            "d3d11": smoke_report["results"]["d3d11"],
            "swiftshader": smoke_report["results"]["swiftshader"],
            "interop_d3d11": smoke_report["results"]["interop_d3d11"],
            "interop_warp": smoke_report["results"]["interop_warp"],
            "native_execution": {
                "tested": True,
                "passed": smoke_report["passed"],
                "architecture": smoke_report["architecture"],
            },
            "hardware_vulkan": {
                "tested": False,
                "supported": None,
                "note": "No hardware Vulkan claim is made by this package.",
            },
        },
        "build_host": build_report["build_host"],
        "native_test_host": smoke_report["native_test_host"],
        "files": file_metadata(package_root),
    }


def create_zip(package_root: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        destination.unlink()
    with zipfile.ZipFile(
        destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        for path in archive_files(package_root):
            relative = path.relative_to(package_root).as_posix()
            info = zipfile.ZipInfo(relative, ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 0
            info.external_attr = 0
            archive.writestr(info, path.read_bytes(), compresslevel=9)


def verify_zip(
    package_root: Path,
    archive_path: Path,
    expected_manifest: dict[str, Any],
) -> None:
    expected_names = [
        path.relative_to(package_root).as_posix()
        for path in archive_files(package_root)
    ]
    with zipfile.ZipFile(archive_path, "r") as archive:
        entries = archive.infolist()
        actual_names = [entry.filename for entry in entries]
        if len(actual_names) != len(set(actual_names)):
            raise RuntimeError(f"Archive contains duplicate paths: {archive_path}")
        if actual_names != expected_names:
            raise RuntimeError(
                "Archive content mismatch: "
                f"expected {expected_names}, got {actual_names}"
            )
        for entry in entries:
            if entry.date_time != ZIP_TIMESTAMP:
                raise RuntimeError(
                    f"Non-deterministic ZIP timestamp for {entry.filename}: "
                    f"{entry.date_time}"
                )
        archived_manifest = json.loads(archive.read("manifest.json"))
        if archived_manifest != expected_manifest:
            raise RuntimeError("Archived manifest does not match generated manifest")
        manifest_files = {
            item["path"]: item for item in archived_manifest.get("files", [])
        }
        expected_manifest_names = set(actual_names) - {"manifest.json"}
        if set(manifest_files) != expected_manifest_names:
            raise RuntimeError(
                "Manifest file list does not match archive content: "
                f"expected {sorted(expected_manifest_names)}, "
                f"got {sorted(manifest_files)}"
            )
        for name, metadata in manifest_files.items():
            payload = archive.read(name)
            actual_hash = hashlib.sha256(payload).hexdigest()
            if len(payload) != metadata["size"] or actual_hash != metadata["sha256"]:
                raise RuntimeError(f"Archived file metadata mismatch: {name}")


def package(args: argparse.Namespace) -> None:
    lock = load_lock()
    architecture = validate_architecture(args.architecture, lock)
    staging = staging_dir(architecture)
    verify_path = staging / "verify-report.json"
    smoke_path = staging / "smoke-report.json"
    build_path = output_dir(architecture) / "build-report.json"
    for required in (verify_path, smoke_path, build_path):
        if not required.is_file():
            raise RuntimeError(f"Required successful report is missing: {required}")
    verify_report = load_json(verify_path)
    smoke_report = load_json(smoke_path)
    build_report = load_json(build_path)
    for report_name, report in (
        ("verify-report.json", verify_report),
        ("smoke-report.json", smoke_report),
        ("build-report.json", build_report),
    ):
        if report.get("architecture") != architecture:
            raise RuntimeError(
                f"{report_name} architecture mismatch: expected {architecture}, "
                f"got {report.get('architecture')!r}"
            )
        if report.get("errors"):
            raise RuntimeError(f"{report_name} contains errors: {report['errors']}")
    if verify_report.get("passed") is not True:
        raise RuntimeError("Artifact verification did not pass")
    if smoke_report.get("passed") is not True:
        raise RuntimeError("All native smoke tests must pass before packaging")
    for backend in ("d3d11", "swiftshader", "interop_d3d11", "interop_warp"):
        if not smoke_report.get("results", {}).get(backend, {}).get("passed"):
            raise RuntimeError(f"{backend} smoke test did not pass")

    package_root = work_root() / "package" / architecture
    reset_directory(package_root)
    print(f"[1/4] Copy verified {architecture} staging into package root", flush=True)
    for path in staging.iterdir():
        if path.name in REPORT_NAMES:
            continue
        destination = package_root / path.name
        if path.is_dir():
            shutil.copytree(path, destination)
        elif path.is_file():
            shutil.copy2(path, destination)
    print("[2/4] Generate manifest from build, PE, and smoke reports", flush=True)
    manifest = build_manifest(
        architecture,
        package_root,
        lock,
        verify_report,
        smoke_report,
        build_report,
    )
    write_json(package_root / "manifest.json", manifest)
    archive = dist_dir() / f"ANGLE-windows-{architecture}.zip"
    print(f"[3/4] Create deterministic archive {archive}", flush=True)
    create_zip(package_root, archive)
    print("[4/4] Reopen ZIP and verify paths, timestamps, manifest, and hashes", flush=True)
    verify_zip(package_root, archive, manifest)
    print(f"Created {archive}")
    print(f"SHA-256 {sha256(archive)}")


def checksums(_: argparse.Namespace) -> None:
    lock = load_lock()
    archives = [
        dist_dir() / f"ANGLE-windows-{architecture}.zip"
        for architecture in lock["architectures"]
    ]
    missing = [str(path) for path in archives if not path.is_file()]
    if missing:
        raise RuntimeError("Cannot create checksums; missing: " + ", ".join(missing))
    lines = [f"{sha256(path)}  {path.name}" for path in sorted(archives)]
    output = dist_dir() / "SHA256SUMS.txt"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(output.read_text(encoding="utf-8"), end="")
