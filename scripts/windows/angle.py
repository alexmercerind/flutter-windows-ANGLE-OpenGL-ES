"""ANGLE Windows release pipeline: setup, build, install, package."""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

import build as angle_build
import package as angle_package
from common import CONFIG


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("setup")
    for name in ("build", "install", "package"):
        command = commands.add_parser(name)
        command.add_argument("architecture", choices=CONFIG["architectures"])
        if name == "install":
            command.add_argument("--staging", type=Path)
    commands.add_parser("checksums")
    args = parser.parse_args()
    try:
        if args.command == "setup":
            angle_build.setup()
        elif args.command == "build":
            angle_build.build(args.architecture)
        elif args.command == "install":
            angle_build.install(args.architecture, stage=args.staging)
        elif args.command == "package":
            angle_package.package(args.architecture)
        elif args.command == "checksums":
            angle_package.checksums()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
