from __future__ import annotations

from .common import load_lock


def main(tag: str) -> None:
    version = load_lock()["release_version"]
    expected = f"v{version}"
    if tag != expected:
        raise RuntimeError(f"Release tag {tag!r} does not match lock version {expected!r}")
    print(f"Release tag matches lock version: {tag}")
