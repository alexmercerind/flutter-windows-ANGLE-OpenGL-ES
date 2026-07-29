from __future__ import annotations

import argparse
import hashlib
import json
import tempfile
import zipfile
from pathlib import Path, PurePosixPath
from typing import Any

from .collect_artifacts import REQUIRED_LIBS, REQUIRED_RUNTIME
from .common import dist_dir, load_lock, work_root, write_json
from .pe_utils import inspect_import_library, inspect_pe, sha256


def validate_path(name: str) -> None:
    path = PurePosixPath(name)
    if (
        not name
        or "\\" in name
        or path.is_absolute()
        or ".." in path.parts
        or "." in path.parts
    ):
        raise RuntimeError(f"Unsafe ZIP path: {name!r}")


def validate_archive(
    archive_path: Path, architecture: str, lock: dict[str, Any]
) -> dict[str, Any]:
    errors: list[str] = []
    expected_machine = lock["architectures"][architecture]["pe_machine"].lower()
    pe_files: dict[str, Any] = {}
    import_libraries: dict[str, Any] = {}
    with zipfile.ZipFile(archive_path) as archive:
        entries = archive.infolist()
        names = [entry.filename for entry in entries]
        if len(names) != len(set(names)):
            errors.append("archive contains duplicate paths")
        for name in names:
            try:
                validate_path(name)
            except RuntimeError as error:
                errors.append(str(error))
        if "manifest.json" not in names:
            raise RuntimeError(f"{archive_path.name}: manifest.json is missing")
        manifest = json.loads(archive.read("manifest.json"))
        if manifest.get("schema_version") != 2:
            errors.append(
                f"manifest schema_version must be 2, got "
                f"{manifest.get('schema_version')!r}"
            )
        if manifest.get("architecture") != architecture:
            errors.append(
                f"manifest architecture mismatch: "
                f"{manifest.get('architecture')!r}"
            )
        if manifest.get("release_version") != lock["release_version"]:
            errors.append(
                f"manifest release version mismatch: "
                f"{manifest.get('release_version')!r}"
            )

        metadata_items = manifest.get("files", [])
        metadata = {
            item["path"]: item
            for item in metadata_items
            if isinstance(item, dict) and "path" in item
        }
        if len(metadata) != len(metadata_items):
            errors.append("manifest contains duplicate or invalid file entries")
        expected_payload = set(names) - {"manifest.json"}
        if set(metadata) != expected_payload:
            errors.append("manifest file list does not match ZIP paths")
        for name, item in metadata.items():
            if name not in names:
                continue
            payload = archive.read(name)
            if len(payload) != item.get("size"):
                errors.append(f"{name}: manifest size mismatch")
            if hashlib.sha256(payload).hexdigest() != item.get("sha256"):
                errors.append(f"{name}: manifest SHA-256 mismatch")

        runtime_names = {
            name
            for name in names
            if "/" not in name
            and (name.lower().endswith(".dll") or name == "vk_swiftshader_icd.json")
        }
        if runtime_names != set(REQUIRED_RUNTIME):
            errors.append(
                "runtime file set mismatch: "
                f"expected {sorted(REQUIRED_RUNTIME)}, got {sorted(runtime_names)}"
            )
        library_names = {
            PurePosixPath(name).name
            for name in names
            if PurePosixPath(name).parent == PurePosixPath("lib")
        }
        if library_names != set(REQUIRED_LIBS):
            errors.append(
                "import-library set mismatch: "
                f"expected {sorted(REQUIRED_LIBS)}, got {sorted(library_names)}"
            )

        try:
            icd = json.loads(archive.read("vk_swiftshader_icd.json"))
            library_path = icd["ICD"]["library_path"].replace("\\", "/")
            if library_path not in ("vk_swiftshader.dll", "./vk_swiftshader.dll"):
                errors.append(
                    "vk_swiftshader_icd.json must reference adjacent "
                    "vk_swiftshader.dll"
                )
        except (KeyError, TypeError, json.JSONDecodeError) as error:
            errors.append(f"invalid SwiftShader ICD JSON: {error}")

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in REQUIRED_RUNTIME:
                if not name.lower().endswith(".dll") or name not in names:
                    continue
                path = root / name
                path.write_bytes(archive.read(name))
                info = inspect_pe(path)
                pe_files[name] = info
                if info["machine"].lower() != expected_machine:
                    errors.append(
                        f"{name}: expected PE machine {expected_machine}, "
                        f"got {info['machine']}"
                    )
            for name in REQUIRED_LIBS:
                archive_name = f"lib/{name}"
                if archive_name not in names:
                    continue
                path = root / name
                path.write_bytes(archive.read(archive_name))
                info = inspect_import_library(path)
                import_libraries[name] = info
                actual = [machine.lower() for machine in info["machines"]]
                if actual != [expected_machine]:
                    errors.append(
                        f"{name}: expected only COFF machine {expected_machine}, "
                        f"got {info['machines']}"
                    )

    return {
        "passed": not errors,
        "architecture": architecture,
        "archive": str(archive_path),
        "archive_sha256": sha256(archive_path),
        "archive_size": archive_path.stat().st_size,
        "pe_files": pe_files,
        "import_libraries": import_libraries,
        "errors": errors,
    }


def validate_checksums(path: Path, reports: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    expected = {
        f"ANGLE-windows-{architecture}.zip": report["archive_sha256"]
        for architecture, report in reports.items()
    }
    actual: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) != 2:
            errors.append(f"invalid SHA256SUMS line: {line!r}")
            continue
        digest, name = parts
        if name in actual:
            errors.append(f"duplicate SHA256SUMS entry: {name}")
        actual[name] = digest.lower()
    if actual != expected:
        errors.append(f"SHA256SUMS mismatch: expected {expected}, got {actual}")
    return errors


def validate(args: argparse.Namespace) -> None:
    lock = load_lock()
    root = Path(args.dist).resolve() if args.dist else dist_dir()
    reports: dict[str, Any] = {}
    for architecture in lock["architectures"]:
        archive = root / f"ANGLE-windows-{architecture}.zip"
        if not archive.is_file():
            raise RuntimeError(f"Missing release archive: {archive}")
        print(f"Validate {archive}", flush=True)
        reports[architecture] = validate_archive(archive, architecture, lock)

    checksum_path = root / "SHA256SUMS.txt"
    checksum_errors: list[str] = []
    if args.require_checksums:
        if not checksum_path.is_file():
            checksum_errors.append(f"missing {checksum_path}")
        else:
            checksum_errors.extend(validate_checksums(checksum_path, reports))

    errors = [
        f"{architecture}: {error}"
        for architecture, report in reports.items()
        for error in report["errors"]
    ]
    errors.extend(checksum_errors)
    output = {
        "passed": not errors,
        "archives": reports,
        "checksums": {
            "required": args.require_checksums,
            "path": str(checksum_path),
            "errors": checksum_errors,
        },
        "errors": errors,
    }
    report_path = (
        Path(args.report).resolve()
        if args.report
        else work_root() / "archive-validation-report.json"
    )
    write_json(report_path, output)
    if errors:
        raise RuntimeError("release archive validation failed:\n- " + "\n- ".join(errors))
    print("Both release archives and checksums passed independent validation")
