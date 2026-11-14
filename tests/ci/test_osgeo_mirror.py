"""Verify mirror routing, pinned checksums and safe vcpkg cache publication."""

import hashlib
import importlib.util
import os
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("osgeo_mirror", ROOT / "scripts/ci_osgeo_mirror.py")
mirror = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mirror)
URL = "https://download.osgeo.org/geos/geos-3.14.0.tar.bz2"
MIRROR = URL.replace("download.osgeo.org", "download-cache.osgeo.org")


class OsgeoMirrorTest(unittest.TestCase):
    def setUp(self):
        base = ROOT / "build/ci_osgeo_mirror_tests"
        base.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(dir=base))
        self.destination = self.root / "archive with spaces.tar.bz2"

    def test_verified_archive_replaces_destination(self):
        data = b"geos" * (1024 * 1024)
        self.destination.write_bytes(b"previous")
        def download(command, check):
            self.assertTrue(check)
            self.assertEqual(command[-1], MIRROR)
            self.assertIn("--fail", command)
            self.assertIn("--max-time", command)
            Path(command[command.index("--output") + 1]).write_bytes(data)

        with patch.object(mirror.subprocess, "run", side_effect=download):
            mirror.fetch(URL, hashlib.sha512(data).hexdigest().upper(), self.destination)
        self.assertEqual(self.destination.read_bytes(), data)
        self.assertFalse(list(self.root.glob("*.part")))

    def test_wrong_checksum_preserves_destination(self):
        self.destination.write_bytes(b"previous")
        def download(command, check):
            Path(command[command.index("--output") + 1]).write_bytes(b"corrupt")

        with patch.object(mirror.subprocess, "run", side_effect=download):
            with self.assertRaisesRegex(ValueError, "SHA-512 mismatch"):
                mirror.fetch(URL, "0" * 128, self.destination)
        self.assertEqual(self.destination.read_bytes(), b"previous")
        self.assertFalse(list(self.root.glob("*.part")))

    def test_interrupted_stream_is_not_published(self):
        def download(command, check):
            Path(command[command.index("--output") + 1]).write_bytes(b"partial")
            raise subprocess.CalledProcessError(28, command)

        with patch.object(mirror.subprocess, "run", side_effect=download):
            with self.assertRaises(subprocess.CalledProcessError):
                mirror.fetch(URL, "0" * 128, self.destination)
        self.assertFalse(self.destination.exists())
        self.assertFalse(list(self.root.glob("*.part")))

    def test_unavailable_mirror_leaves_normal_downloader_a_miss(self):
        with patch.object(mirror.subprocess, "run", side_effect=OSError("offline")):
            with self.assertRaises(OSError):
                mirror.fetch(URL, "0" * 128, self.destination)
        self.assertFalse(self.destination.exists())

    def test_unrelated_hosts_and_invalid_hashes_are_not_downloaded(self):
        cases = [("https://github.com/project/archive", "0" * 128),
                 ("https://download.osgeo.org.evil.test/geos", "0" * 128),
                 ("http://download.osgeo.org/geos", "0" * 128),
                 (URL, ""), (URL, "0" * 127)]
        with patch.object(mirror.subprocess, "run") as download:
            for url, digest in cases:
                with self.subTest(url=url, digest=digest), self.assertRaises(ValueError):
                    mirror.fetch(url, digest, self.destination)
            download.assert_not_called()

    def test_provider_quotes_native_windows_paths_and_retains_existing_sources(self):
        env_file = self.root / "github-env"
        script = self.root / "provider with spaces" / "mirror.py"
        with patch.dict(os.environ, {"GITHUB_ENV": str(env_file),
                                     "X_VCPKG_ASSET_SOURCES": "x-azurl,file:///cache,,read"}), \
                patch.object(mirror.sys, "executable", "C:\\Program Files\\Python\\python.exe"), \
                patch.object(mirror, "__file__", str(script)):
            mirror.configure()
        self.assertEqual(env_file.read_text(),
                         'X_VCPKG_ASSET_SOURCES=x-azurl,file:///cache,,read;x-script,'
                         f'"C:/Program Files/Python/python.exe" "{script.as_posix()}" '
                         '{url} {sha512} {dst}\n')


if __name__ == "__main__":
    unittest.main()
