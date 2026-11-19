#!/usr/bin/env python3
"""vcpkg asset provider for OSGeo's alternate host, with the pinned SHA-512."""

import hashlib
import os
from pathlib import Path
import re
import sys
import subprocess
from urllib.parse import urlsplit, urlunsplit


def fetch(url: str, expected: str, destination: Path) -> None:
    source = urlsplit(url)
    if source.scheme != "https" or source.netloc != "download.osgeo.org":
        raise ValueError("Not an OSGeo source URL")
    if not re.fullmatch(r"[0-9a-fA-F]{128}", expected):
        raise ValueError("Missing pinned SHA-512")
    mirror = urlunsplit(source._replace(netloc="download-cache.osgeo.org"))
    partial = destination.with_name(destination.name + ".osgeo.part")
    try:
        subprocess.run(["curl", "--fail", "--location", "--silent", "--show-error",
                        "--connect-timeout", "15", "--max-time", "90",
                        "--output", str(partial), mirror], check=True)
        digest = hashlib.sha512()
        with partial.open("rb") as archive:
            while chunk := archive.read(1024 * 1024):
                digest.update(chunk)
        if digest.hexdigest() != expected.lower():
            raise ValueError("OSGeo mirror SHA-512 mismatch")
        partial.replace(destination)
    finally:
        partial.unlink(missing_ok=True)


def configure() -> None:
    # Native forward-slash paths and quoted arguments also work in Windows'
    # vcpkg build environment, where the Git Bash PATH is not retained.
    python = sys.executable.replace("\\", "/")
    script = Path(__file__).resolve().as_posix()
    # vcpkg escapes substituted arguments itself, including spaced destinations.
    provider = f'x-script,"{python}" "{script}" {{url}} {{sha512}} {{dst}}'
    existing = os.environ.get("X_VCPKG_ASSET_SOURCES", "")
    value = f"{existing};{provider}" if existing else provider
    with Path(os.environ["GITHUB_ENV"]).open("a", encoding="utf-8") as output:
        output.write(f"X_VCPKG_ASSET_SOURCES={value}\n")


if __name__ == "__main__":
    if sys.argv[1:] == ["--configure"]:
        configure()
    elif len(sys.argv) != 4:
        sys.exit("Usage: ci_osgeo_mirror.py URL SHA512 DESTINATION | --configure")
    else:
        try:
            fetch(sys.argv[1], sys.argv[2], Path(sys.argv[3]))
        except Exception as error:
            # A provider miss leaves vcpkg's normal source download in place.
            if sys.argv[1].startswith("https://download.osgeo.org/"):
                print(f"OSGeo mirror: {error}", file=sys.stderr)
            sys.exit(1)
