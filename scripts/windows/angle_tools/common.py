from __future__ import annotations

import json
import os
import queue
import shutil
import subprocess
import threading
import time
from pathlib import Path
from typing import Any, Iterable


REPO_ROOT = Path(__file__).resolve().parents[3]
LOCK_PATH = REPO_ROOT / "config" / "angle.lock.json"
TOOLCHAIN_LOCK_PATH = REPO_ROOT / "config" / "toolchain.lock.json"
DEFAULT_WORK_ROOT = REPO_ROOT / ".angle-work"


def load_lock() -> dict[str, Any]:
    with LOCK_PATH.open("r", encoding="utf-8") as stream:
        lock = json.load(stream)
    if lock.get("schema_version") != 1:
        raise RuntimeError(f"Unsupported lock schema: {lock.get('schema_version')}")
    return lock


def load_toolchain_lock() -> dict[str, Any]:
    with TOOLCHAIN_LOCK_PATH.open("r", encoding="utf-8") as stream:
        lock = json.load(stream)
    if lock.get("schema_version") != 1:
        raise RuntimeError(
            f"Unsupported toolchain lock schema: {lock.get('schema_version')}"
        )
    return lock


def validate_architecture(architecture: str, lock: dict[str, Any]) -> str:
    value = architecture.lower()
    if value not in lock["architectures"]:
        choices = ", ".join(sorted(lock["architectures"]))
        raise ValueError(f"Unsupported architecture {architecture!r}; expected {choices}")
    return value


def work_root() -> Path:
    override = os.environ.get("ANGLE_WORK_DIR")
    return Path(override).resolve() if override else DEFAULT_WORK_ROOT


def checkout_root() -> Path:
    return work_root() / "checkout"


def angle_source() -> Path:
    return checkout_root() / "angle"


def depot_tools_dir() -> Path:
    return work_root() / "depot_tools"


def output_dir(architecture: str) -> Path:
    return work_root() / "out" / architecture


def staging_dir(architecture: str) -> Path:
    return work_root() / "staging" / architecture


def smoke_build_dir(architecture: str) -> Path:
    return work_root() / "smoke" / architecture


def dist_dir() -> Path:
    return REPO_ROOT / "dist"


def command_environment() -> dict[str, str]:
    env = dict(os.environ)
    depot = str(depot_tools_dir())
    project_ninja = angle_source() / "third_party" / "ninja"
    path_parts = [depot]
    if project_ninja.is_dir():
        # depot_tools requires a project Ninja to appear after depot_tools.
        path_parts.append(str(project_ninja))
    path_parts.append(env.get("PATH", ""))
    env["PATH"] = os.pathsep.join(path_parts)
    env["DEPOT_TOOLS_WIN_TOOLCHAIN"] = "0"
    env["DEPOT_TOOLS_UPDATE"] = "0"
    env["PYTHONUTF8"] = "1"
    return env


def git_safe_environment(
    paths: Iterable[Path], *, env: dict[str, str] | None = None
) -> dict[str, str]:
    result = dict(env or os.environ)
    try:
        index = int(result.get("GIT_CONFIG_COUNT", "0"))
    except ValueError as error:
        raise RuntimeError(
            f"Invalid GIT_CONFIG_COUNT={result.get('GIT_CONFIG_COUNT')!r}"
        ) from error
    result[f"GIT_CONFIG_KEY_{index}"] = "core.longpaths"
    result[f"GIT_CONFIG_VALUE_{index}"] = "true"
    index += 1
    for path in paths:
        result[f"GIT_CONFIG_KEY_{index}"] = "safe.directory"
        result[f"GIT_CONFIG_VALUE_{index}"] = str(path.resolve())
        index += 1
    result["GIT_CONFIG_COUNT"] = str(index)
    return result


def run(
    command: Iterable[str | os.PathLike[str]],
    *,
    cwd: Path | None = None,
    capture: bool = False,
    check: bool = True,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    args = [os.fspath(item) for item in command]
    print("+", subprocess.list2cmdline(args), flush=True)
    try:
        return subprocess.run(
            args,
            cwd=os.fspath(cwd) if cwd else None,
            env=env or command_environment(),
            check=check,
            text=True,
            encoding="utf-8",
            errors="replace",
            capture_output=capture,
        )
    except subprocess.CalledProcessError as error:
        raise RuntimeError(
            f"Command failed with exit code {error.returncode}: "
            f"{subprocess.list2cmdline(args)}"
        ) from error


def run_logged(
    command: Iterable[str | os.PathLike[str]],
    *,
    log_path: Path,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
    heartbeat_seconds: int = 15,
) -> None:
    args = [os.fspath(item) for item in command]
    print("+", subprocess.list2cmdline(args), flush=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    output_queue: queue.Queue[str | None] = queue.Queue()
    started = time.monotonic()
    with log_path.open("a", encoding="utf-8", newline="\n") as log:
        log.write(f"\n$ {subprocess.list2cmdline(args)}\n")
        log.flush()
        process = subprocess.Popen(
            args,
            cwd=os.fspath(cwd) if cwd else None,
            env=env or command_environment(),
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=1,
        )

        def read_output() -> None:
            assert process.stdout is not None
            for line in process.stdout:
                output_queue.put(line)
            output_queue.put(None)

        reader = threading.Thread(target=read_output, daemon=True)
        reader.start()
        finished_output = False
        while not finished_output:
            try:
                line = output_queue.get(timeout=heartbeat_seconds)
            except queue.Empty:
                elapsed = int(time.monotonic() - started)
                message = (
                    f"[heartbeat] process {process.pid} is still running "
                    f"after {elapsed}s; log={log_path}"
                )
                print(message, flush=True)
                log.write(message + "\n")
                log.flush()
                continue
            if line is None:
                finished_output = True
                continue
            print(line, end="", flush=True)
            log.write(line)
            log.flush()
        return_code = process.wait()
        reader.join(timeout=1)
        elapsed = int(time.monotonic() - started)
        summary = f"[completed] exit={return_code} elapsed={elapsed}s"
        print(summary, flush=True)
        log.write(summary + "\n")
        log.flush()
        if return_code != 0:
            raise RuntimeError(
                f"Command failed with exit code {return_code}; see {log_path}"
            )


def find_executable(names: Iterable[str], *, env: dict[str, str] | None = None) -> str:
    search_path = (env or command_environment()).get("PATH")
    for name in names:
        result = shutil.which(name, path=search_path)
        if result:
            return result
    raise FileNotFoundError(f"Executable not found: {', '.join(names)}")


def reset_directory(path: Path) -> None:
    if path.exists():
        resolved = path.resolve()
        allowed = (
            work_root().resolve(),
            (REPO_ROOT / "windows" / "bin").resolve(),
            (REPO_ROOT / "windows" / "include" / "external").resolve(),
        )
        if not any(resolved == root or root in resolved.parents for root in allowed):
            raise RuntimeError(f"Refusing to reset directory outside managed roots: {resolved}")
        shutil.rmtree(resolved)
    path.mkdir(parents=True, exist_ok=True)


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
