from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from .collect_artifacts import (
    CONDITIONAL_RUNTIME,
    HEADER_DIRECTORIES,
    REQUIRED_LIBS,
    REQUIRED_RUNTIME,
)
from .common import (
    angle_source,
    load_lock,
    staging_dir,
    validate_architecture,
    write_json,
)
from .pe_utils import (
    inspect_import_library,
    inspect_pe,
    is_debug_runtime,
    is_system_import,
    prerequisite_for,
    sha256,
)


def compare_headers(staging: Path) -> dict[str, Any]:
    expected_root = angle_source() / "include"
    actual_root = staging / "include"
    expected: dict[str, str] = {}
    actual: dict[str, str] = {}
    for directory in HEADER_DIRECTORIES:
        for path in (expected_root / directory).rglob("*"):
            if path.is_file():
                expected[path.relative_to(expected_root).as_posix()] = sha256(path)
        for path in (actual_root / directory).rglob("*"):
            if path.is_file():
                actual[path.relative_to(actual_root).as_posix()] = sha256(path)
    missing = sorted(set(expected) - set(actual))
    extra = sorted(set(actual) - set(expected))
    changed = sorted(name for name in expected.keys() & actual.keys() if expected[name] != actual[name])
    return {
        "passed": not missing and not extra and not changed,
        "file_count": len(actual),
        "missing": missing,
        "extra": extra,
        "changed": changed,
    }


def verify(args: argparse.Namespace) -> None:
    lock = load_lock()
    architecture = validate_architecture(args.architecture, lock)
    staging = staging_dir(architecture)
    if not (staging / "collect-report.json").exists():
        raise RuntimeError(f"Missing collection report in {staging}")
    expected_machine = lock["architectures"][architecture]["pe_machine"].lower()
    errors: list[str] = []

    print("[1/5] Check mandatory runtime and import-library files", flush=True)
    for name in REQUIRED_RUNTIME:
        if not (staging / name).is_file():
            errors.append(f"missing required runtime file: {name}")
    for name in REQUIRED_LIBS:
        if not (staging / "lib" / name).is_file():
            errors.append(f"missing required import library: {name}")

    pe_files: dict[str, Any] = {}
    packaged_names = {path.name.lower() for path in staging.glob("*.dll")}
    prerequisites: dict[str, list[str]] = {}
    unresolved: dict[str, list[str]] = {}
    print(
        f"[2/5] Inspect every staged PE; expected machine={expected_machine}",
        flush=True,
    )
    for path in sorted(staging.glob("*.dll"), key=lambda item: item.name.lower()):
        info = inspect_pe(path)
        print(
            f"  {path.name}: machine={info['machine']} "
            f"imports={', '.join(info['imports']) or '(none)'} "
            f"delay_imports={', '.join(info['delay_imports']) or '(none)'}",
            flush=True,
        )
        pe_files[path.name] = {
            **info,
            "size": path.stat().st_size,
            "sha256": sha256(path),
        }
        if info["machine"].lower() != expected_machine:
            errors.append(
                f"{path.name}: expected machine {expected_machine}, got {info['machine']}"
            )
        for imported in info["runtime_dependencies"]:
            if is_debug_runtime(imported):
                errors.append(f"{path.name}: imports forbidden Debug CRT {imported}")
            lower = imported.lower()
            if lower in packaged_names or is_system_import(imported):
                continue
            prerequisite = prerequisite_for(imported)
            if prerequisite:
                prerequisites.setdefault(prerequisite, []).append(
                    f"{path.name} -> {imported}"
                )
            else:
                unresolved.setdefault(path.name, []).append(imported)
    if unresolved:
        for owner, names in unresolved.items():
            errors.append(f"{owner}: unresolved non-system imports: {', '.join(names)}")

    import_libraries: dict[str, Any] = {}
    print("[3/5] Inspect COFF machine values in import libraries", flush=True)
    for name in REQUIRED_LIBS:
        path = staging / "lib" / name
        if not path.is_file():
            continue
        info = inspect_import_library(path)
        import_libraries[name] = {
            **info,
            "size": path.stat().st_size,
            "sha256": sha256(path),
        }
        actual_machines = [item.lower() for item in info["machines"]]
        if actual_machines != [expected_machine]:
            errors.append(
                f"{name}: expected only COFF machine {expected_machine}, "
                f"got {info['machines']}"
            )

    icd_path = staging / "vk_swiftshader_icd.json"
    icd: dict[str, Any] | None = None
    print("[4/5] Validate adjacent SwiftShader ICD and conditional runtimes", flush=True)
    if icd_path.is_file():
        try:
            icd = json.loads(icd_path.read_text(encoding="utf-8"))
            library_path = icd["ICD"]["library_path"]
            normalized = library_path.replace("\\", "/")
            if normalized not in ("vk_swiftshader.dll", "./vk_swiftshader.dll"):
                errors.append(
                    f"ICD library_path must reference adjacent vk_swiftshader.dll, got {library_path!r}"
                )
            if not (staging / "vk_swiftshader.dll").is_file():
                errors.append("ICD references missing vk_swiftshader.dll")
        except (json.JSONDecodeError, KeyError, TypeError) as error:
            errors.append(f"invalid vk_swiftshader_icd.json: {error}")

    conditional: dict[str, Any] = {}
    for name in CONDITIONAL_RUNTIME:
        importers = [
            owner
            for owner, info in pe_files.items()
            if name.lower()
            in [item.lower() for item in info["runtime_dependencies"]]
        ]
        included = (staging / name).is_file()
        conditional[name] = {"importers": importers, "included": included}
        if bool(importers) != included:
            errors.append(
                f"{name}: inclusion mismatch (importers={importers}, included={included})"
            )

    print("[5/5] Compare staged headers byte-for-byte with pinned ANGLE", flush=True)
    headers = compare_headers(staging)
    if not headers["passed"]:
        errors.append("staged headers do not exactly match the pinned ANGLE checkout")

    report = {
        "passed": not errors,
        "architecture": architecture,
        "expected_machine": expected_machine,
        "pe_files": pe_files,
        "import_libraries": import_libraries,
        "icd": icd,
        "headers": headers,
        "conditional_runtime": conditional,
        "external_prerequisites": prerequisites,
        "unresolved_imports": unresolved,
        "errors": errors,
    }
    write_json(staging / "verify-report.json", report)
    if errors:
        raise RuntimeError("artifact verification failed:\n- " + "\n- ".join(errors))
    print(f"Artifact verification passed: {staging}")
