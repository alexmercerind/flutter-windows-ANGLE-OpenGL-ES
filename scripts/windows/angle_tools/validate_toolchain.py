from __future__ import annotations

import os
import platform
import re
import subprocess
import sys
from typing import Any

from .common import REPO_ROOT
from .smoke_test import find_cmake


WORKFLOW_PATH = REPO_ROOT / ".github" / "workflows" / "build-angle.yml"


def command_output(command: list[str]) -> str:
    print("+", subprocess.list2cmdline(command), flush=True)
    completed = subprocess.run(
        command,
        check=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
    )
    output = completed.stdout.strip()
    if output:
        print(output, flush=True)
    return output


def validate_workflow(lock: dict[str, Any]) -> None:
    print("[1/4] Validate exact GitHub Action release tags", flush=True)
    content = WORKFLOW_PATH.read_text(encoding="utf-8")
    uses = re.findall(
        r"^\s*(?:-\s*)?uses:\s*([^@\s]+)@([^\s#]+)\s*$",
        content,
        re.MULTILINE,
    )
    if not uses:
        raise RuntimeError(f"No GitHub Actions found in {WORKFLOW_PATH}")
    for repository, version in uses:
        if not re.fullmatch(r"v\d+\.\d+\.\d+", version):
            raise RuntimeError(
                f"Action {repository}@{version} must use a complete release tag"
            )

    print(
        "[2/4] Validate configured Python version",
        flush=True,
    )
    python_version = lock["host_tools"]["python"]["version"]
    python_pins = re.findall(r'python-version:\s*["\']([^"\']+)["\']', content)
    if not python_pins or set(python_pins) != {python_version}:
        raise RuntimeError(
            f"Every setup-python step must use {python_version}; got {python_pins}"
        )
    print("[3/4] Validate required runner labels", flush=True)
    runner_labels = set(re.findall(r"^\s*runs-on:\s*(\S+)\s*$", content, re.MULTILINE))
    expected_labels = set(lock["runner_labels"].values())
    if not expected_labels.issubset(runner_labels):
        raise RuntimeError(
            f"Workflow runner labels {sorted(runner_labels)} do not include "
            f"{sorted(expected_labels)}"
        )

    print("[4/4] Report locked workflow toolchain", flush=True)
    print(f"python={python_version}")
    for repository, version in sorted(set(uses)):
        print(f"action={repository}@{version}")


def validate_runtime(
    toolchain_lock: dict[str, Any], angle_lock: dict[str, Any]
) -> None:
    print("[runtime 1/3] Validate Python", flush=True)
    expected_python = toolchain_lock["host_tools"]["python"]["version"]
    actual_python = platform.python_version()
    print(f"python={actual_python} executable={sys.executable}")
    if actual_python != expected_python:
        raise RuntimeError(
            f"Python mismatch: expected {expected_python}, got {actual_python}"
        )

    print("[runtime 2/3] Report runner-provided CMake", flush=True)
    cmake = find_cmake(dict(os.environ))
    output = command_output([cmake, "--version"])
    match = re.search(r"cmake version ([^\s]+)", output)
    actual_cmake = match.group(1) if match else ""
    if not actual_cmake:
        raise RuntimeError(f"Could not parse CMake version from: {output}")
    print(f"cmake={actual_cmake} source=runner")

    print("[runtime 3/3] Report preferred Windows SDK reference", flush=True)
    sdk = angle_lock["windows_sdk"]
    print(
        f"windows_sdk_preferred_release={sdk['preferred_release_version']} "
        f"preferred_include_version={sdk['preferred_include_version']} "
        f"enforced={str(sdk['enforced']).lower()}"
    )
