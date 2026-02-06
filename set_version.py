#!/usr/bin/env python3
"""Update version numbers across the PicoROM project."""

import argparse
import re
import sys
from pathlib import Path


def parse_version(version_str: str) -> tuple[int, int, int]:
    """Parse a semantic version string into (major, minor, patch)."""
    match = re.match(r'^(\d+)\.(\d+)\.(\d+)$', version_str)
    if not match:
        raise ValueError(f"Invalid version format: {version_str}. Expected X.Y.Z")
    return int(match.group(1)), int(match.group(2)), int(match.group(3))


def update_cmake(cmake_path: Path, major: int, minor: int) -> None:
    """Update VERSION in CMakeLists.txt to major.minor."""
    content = cmake_path.read_text()
    new_content = re.sub(
        r'^set\(VERSION\s+[\d.]+\)',
        f'set(VERSION {major}.{minor})',
        content,
        flags=re.MULTILINE
    )
    if content == new_content:
        print(f"Warning: No VERSION found in {cmake_path}")
        return
    cmake_path.write_text(new_content)
    print(f"Updated {cmake_path}: VERSION = {major}.{minor}")


def update_cargo_toml(cargo_path: Path, version: str) -> None:
    """Update version in Cargo.toml."""
    content = cargo_path.read_text()
    new_content = re.sub(
        r'^version\s*=\s*"[\d.]+"',
        f'version = "{version}"',
        content,
        count=1,
        flags=re.MULTILINE
    )
    if content == new_content:
        print(f"Warning: No version found in {cargo_path}")
        return
    cargo_path.write_text(new_content)
    print(f"Updated {cargo_path}: version = {version}")


def main():
    parser = argparse.ArgumentParser(description="Update version numbers across PicoROM")
    parser.add_argument("version", help="Semantic version (X.Y.Z)")
    args = parser.parse_args()

    try:
        major, minor, patch = parse_version(args.version)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    full_version = f"{major}.{minor}.{patch}"
    script_dir = Path(__file__).parent.resolve()

    # Update firmware CMakeLists.txt with major.minor
    cmake_path = script_dir / "firmware" / "CMakeLists.txt"
    if cmake_path.exists():
        update_cmake(cmake_path, major, minor)
    else:
        print(f"Error: {cmake_path} not found", file=sys.stderr)
        sys.exit(1)

    # Update all Cargo.toml files in host/
    host_dir = script_dir / "host"
    cargo_files = list(host_dir.glob("*/Cargo.toml"))
    if not cargo_files:
        print(f"Error: No Cargo.toml files found in {host_dir}", file=sys.stderr)
        sys.exit(1)

    for cargo_path in cargo_files:
        update_cargo_toml(cargo_path, full_version)

    print(f"\nVersion updated to {full_version}")


if __name__ == "__main__":
    main()
