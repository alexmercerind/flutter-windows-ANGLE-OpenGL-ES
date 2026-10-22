"""Create versioned 7z packages with repository-commit binary mtimes."""
from __future__ import annotations

import json
import os
import shutil
import subprocess
from datetime import datetime
from pathlib import Path

from common import (CONFIG, DLLS, LIBS, ROOT, WORK, archive_name, architecture,
                    executable, sha256, staging)


def verify_build_info(stage: Path, arch: str) -> None:
    info = json.loads((stage / "build-info.json").read_text(encoding="utf-8"))
    expected = {
        "angle_commit": CONFIG["angle"]["commit"],
        "depot_tools_commit": CONFIG["depot_tools"]["commit"],
        "architecture": arch,
    }
    if info != expected:
        raise RuntimeError(f"Stale staging build: {info!r}; rebuild {arch}")


def package(arch: str) -> Path:
    arch = architecture(arch)
    stage = staging(arch)
    verify_build_info(stage, arch)
    package_root = WORK / "package" / arch
    if package_root.exists():
        shutil.rmtree(package_root)
    shutil.copytree(stage, package_root)
    for report in package_root.glob("*-report.json"):
        report.unlink()
    repository_commit = subprocess.run(
        ["git", "show", "-s", "--format=%H", "HEAD"], cwd=ROOT, check=True,
        capture_output=True, text=True, encoding="utf-8").stdout.strip()
    repository_commit_timestamp = subprocess.run(
        ["git", "show", "-s", "--format=%cI", "HEAD"], cwd=ROOT, check=True,
        capture_output=True, text=True, encoding="utf-8").stdout.strip()
    timestamp = datetime.fromisoformat(repository_commit_timestamp)
    manifest = {
        "release_version": CONFIG["release_version"],
        "angle_commit": CONFIG["angle"]["commit"],
        "architecture": arch,
        "repository_commit": repository_commit,
        "repository_commit_timestamp": repository_commit_timestamp,
    }
    (package_root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    for name in (*DLLS, *LIBS):
        path = package_root / "lib" / name
        os.utime(path, (timestamp.timestamp(), timestamp.timestamp()))
    archive = ROOT / "dist" / archive_name(arch)
    archive.parent.mkdir(exist_ok=True)
    archive.unlink(missing_ok=True)
    seven_zip = executable("7z.exe", "7z")
    subprocess.run([seven_zip, "a", "-t7z", "-mx=9", "-mtm=on", str(archive),
                    "include", "lib", "LICENSES", "manifest.json"], cwd=package_root, check=True)
    listing = subprocess.run([seven_zip, "l", "-slt", str(archive)],
                             check=True, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    entries = {}
    item = {}
    for line in listing.splitlines() + [""]:
        if not line.strip():
            if "Path" in item and "Modified" in item:
                entries[item["Path"].replace("\\", "/")] = item["Modified"]
            item = {}
        elif " = " in line:
            key, value = line.split(" = ", 1)
            item[key] = value
    expected = timestamp.astimezone().strftime("%Y-%m-%d %H:%M:%S")
    print(f"{arch} repository_commit_timestamp: {repository_commit_timestamp}")
    for name in (*DLLS, *LIBS):
        actual = entries.get(f"lib/{name}")
        print(f"{name}: {actual}")
        if not actual or actual[:19] != expected:
            raise RuntimeError(f"7z timestamp mismatch for {name}: {actual!r}, expected {expected}")
    subprocess.run([seven_zip, "t", str(archive)], check=True, stdout=subprocess.DEVNULL)
    print(f"Created {archive.name} SHA256 {sha256(archive)}")
    return archive


def checksums() -> Path:
    archives = [ROOT / "dist" / archive_name(arch) for arch in CONFIG["architectures"]]
    for archive in archives:
        if not archive.is_file():
            raise FileNotFoundError(archive)
    sums = ROOT / "dist" / "SHA256SUMS"
    sums.write_text("".join(f"{sha256(archive)}  {archive.name}\n" for archive in archives), encoding="utf-8")
    print(sums.read_text(encoding="utf-8"), end="")
    return sums
