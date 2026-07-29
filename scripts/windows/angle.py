from __future__ import annotations

import argparse
import json
import subprocess
import sys
import zipfile
from collections.abc import Callable

from angle_tools import (
    angle_build,
    collect_artifacts,
    install_staging,
    package_artifacts,
    smoke_test,
    validate_release,
    validate_release_archives,
    validate_toolchain,
    verify_artifacts,
    verify_flutter_output,
    verify_repository_binaries,
)
from angle_tools.common import (
    load_lock,
    load_toolchain_lock,
    validate_architecture,
    work_root,
)


def install(args: argparse.Namespace) -> None:
    install_staging.install(args.architecture)


def status(args: argparse.Namespace) -> None:
    architecture = validate_architecture(args.architecture, load_lock())
    log_path = work_root() / "logs" / f"build-{architecture}.log"
    if not log_path.is_file():
        raise RuntimeError(f"Build log does not exist: {log_path}")
    print(log_path.read_text(encoding="utf-8"), end="")


def validate_tag(args: argparse.Namespace) -> None:
    validate_release.main(args.tag)


def validate_tools(args: argparse.Namespace) -> None:
    toolchain_lock = load_toolchain_lock()
    angle_lock = load_lock()
    validate_toolchain.validate_workflow(toolchain_lock)
    if args.runtime:
        validate_toolchain.validate_runtime(toolchain_lock, angle_lock)


def add_architecture_command(
    subparsers: argparse._SubParsersAction[argparse.ArgumentParser],
    name: str,
    description: str,
    handler: Callable[[argparse.Namespace], None],
) -> argparse.ArgumentParser:
    command = subparsers.add_parser(name, help=description, description=description)
    command.add_argument("architecture")
    command.set_defaults(handler=handler)
    return command


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        description="Set up, build, validate, and package ANGLE for Windows"
    )
    subparsers = result.add_subparsers(dest="command", required=True)

    setup_parser = subparsers.add_parser(
        "setup", help="Set up the pinned ANGLE checkout"
    )
    setup_parser.set_defaults(handler=angle_build.setup)

    add_architecture_command(
        subparsers, "build", "Build ANGLE with GN and Ninja", angle_build.build
    )
    add_architecture_command(
        subparsers, "clean", "Remove one architecture build output", angle_build.clean
    )
    add_architecture_command(
        subparsers,
        "collect",
        "Collect build outputs into staging",
        collect_artifacts.collect,
    )
    add_architecture_command(
        subparsers,
        "verify",
        "Verify staged binaries, imports, and headers",
        verify_artifacts.verify,
    )
    add_architecture_command(
        subparsers,
        "install",
        "Install verified staging into windows/bin",
        install,
    )
    add_architecture_command(
        subparsers, "status", "Print the architecture build log", status
    )

    smoke_parser = subparsers.add_parser(
        "smoke", help="Build or run the native ANGLE smoke test"
    )
    smoke_subparsers = smoke_parser.add_subparsers(
        dest="smoke_command", required=True
    )
    smoke_build_parser = smoke_subparsers.add_parser("build")
    smoke_build_parser.add_argument("architecture")
    smoke_build_parser.add_argument("--staging")
    smoke_build_parser.set_defaults(handler=smoke_test.build_smoke)
    smoke_run_parser = smoke_subparsers.add_parser("run")
    smoke_run_parser.add_argument("architecture")
    smoke_run_parser.add_argument("--staging")
    smoke_run_parser.add_argument("--executable")
    smoke_run_parser.set_defaults(handler=smoke_test.run_smoke)

    add_architecture_command(
        subparsers,
        "package",
        "Package one verified architecture",
        package_artifacts.package,
    )
    checksums_parser = subparsers.add_parser(
        "checksums", help="Create SHA256SUMS.txt for both archives"
    )
    checksums_parser.set_defaults(handler=package_artifacts.checksums)

    validate_release_parser = subparsers.add_parser(
        "validate-release", help="Validate a tag against angle.lock.json"
    )
    validate_release_parser.add_argument("tag")
    validate_release_parser.set_defaults(handler=validate_tag)

    validate_archives_parser = subparsers.add_parser(
        "validate-archives", help="Independently validate release archives"
    )
    validate_archives_parser.add_argument("--dist")
    validate_archives_parser.add_argument("--report")
    validate_archives_parser.add_argument(
        "--require-checksums", action="store_true"
    )
    validate_archives_parser.set_defaults(
        handler=validate_release_archives.validate
    )

    validate_toolchain_parser = subparsers.add_parser(
        "validate-toolchain", help="Validate workflow and host tool versions"
    )
    validate_toolchain_parser.add_argument("--runtime", action="store_true")
    validate_toolchain_parser.set_defaults(handler=validate_tools)

    verify_flutter_parser = add_architecture_command(
        subparsers,
        "verify-flutter",
        "Verify Flutter Windows PE outputs",
        verify_flutter_output.verify,
    )
    verify_flutter_parser.add_argument("--release")
    verify_flutter_parser.add_argument("--report")

    verify_repository_parser = add_architecture_command(
        subparsers,
        "verify-repository",
        "Compare repository binaries with verified staging",
        verify_repository_binaries.verify,
    )
    verify_repository_parser.add_argument("--staging")
    verify_repository_parser.add_argument("--report")

    return result


def main() -> None:
    args = parser().parse_args()
    try:
        args.handler(args)
    except (
        json.JSONDecodeError,
        OSError,
        RuntimeError,
        subprocess.CalledProcessError,
        ValueError,
        zipfile.BadZipFile,
    ) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
