"""@file test_data_dependency.py
@brief Configure the actual Data dependency helper without Qt, downloads or GUI builds.
@details Each case retains its fake SDK, Data source, Git history and configure log
under tests/verification/data_bundling_cmake for review.
@copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
@license GPL-3.0-or-later
"""
import pathlib
import shutil
import subprocess
import unittest
import uuid


ROOT = pathlib.Path(__file__).resolve().parents[2]
HELPER = ROOT / "cmake/FetchOpenSWMMData.cmake"


@unittest.skipUnless(shutil.which("cmake") and shutil.which("git"), "CMake and Git are required")
class DataDependencyTest(unittest.TestCase):
    """@brief Exercise checkout selection and opt-out using real CMake/FetchContent."""

    def setUp(self):
        """@brief Create a unique retained case directory, never a temporary folder."""
        self.case = ROOT / "tests/verification/data_bundling_cmake" / self._testMethodName / str(uuid.uuid4())
        self.source = self.case / "source"
        self.build = self.case / "build"
        self.source.mkdir(parents=True)

    def command(self, arguments, cwd=None):
        """@brief Run a bounded command and retain its combined output."""
        result = subprocess.run(arguments, cwd=cwd, text=True, encoding="utf-8",
                                errors="replace", stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=90)
        with (self.case / "commands.log").open("a", encoding="utf-8") as log:
            log.write(repr(arguments) + "\n" + result.stdout + "\n")
        return result

    def configure(self, *options, sdk=True):
        """@brief Include the production helper in a minimal, otherwise independent project."""
        sdk_setup = """
add_library(fake_sdk INTERFACE)
add_library(OpenSWMMVis::Sdk ALIAS fake_sdk)
function(openswmmvis_add_extension target)
endfunction()
set(OpenSWMMVisSdk_MANIFEST "${CMAKE_BINARY_DIR}/manifest.json")
file(WRITE "${OpenSWMMVisSdk_MANIFEST}" "{}")
""" if sdk else ""
        (self.source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.21)\nproject(DataFetchContract LANGUAGES CXX)\n'
            + sdk_setup + f'include("{HELPER.as_posix()}")\n'
            + 'if(TARGET openswmm_data)\n'
              '  get_target_property(kind openswmm_data TYPE)\n'
              '  file(WRITE "${CMAKE_BINARY_DIR}/target-kind.txt" "${kind}")\n'
              'else()\n  file(WRITE "${CMAKE_BINARY_DIR}/target-kind.txt" "absent")\nendif()\n',
            encoding="utf-8")
        result = self.command(["cmake", "-S", str(self.source), "-B", str(self.build), *options])
        (self.case / "configure.log").write_text(result.stdout, encoding="utf-8")
        return result

    def data_source(self, name, marker="local"):
        """@brief Create a fake Data project with the required loadable module target."""
        source = self.case / name
        source.mkdir()
        (source / "plugin.cpp").write_text('extern "C" int fixture_data() { return 1; }\n', encoding="utf-8")
        (source / "marker.txt").write_text(marker, encoding="utf-8")
        (source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.21)\nproject(FakeData LANGUAGES CXX)\n'
            'add_library(openswmm_data MODULE plugin.cpp)\n'
            'file(READ "${CMAKE_CURRENT_SOURCE_DIR}/marker.txt" marker)\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/selected-marker.txt" "${marker}")\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/selected-source.txt" "${CMAKE_CURRENT_SOURCE_DIR}")\n',
            encoding="utf-8")
        return source

    def test_off_requires_no_sdk_or_checkout(self):
        """@brief Opting out performs no SDK validation, local validation or fetch."""
        result = self.configure("-DSWMMVIS_BUNDLE_DATA_PLUGIN=OFF",
                                f"-DOPENSWMM_DATA_SOURCE_DIR={self.case / 'missing'}", sdk=False)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual((self.build / "target-kind.txt").read_text(), "absent")
        self.assertFalse((self.build / "_deps").exists())

    def test_bad_explicit_checkout_fails(self):
        """@brief A mistyped explicit checkout fails instead of silently fetching another source."""
        result = self.configure(f"-DOPENSWMM_DATA_SOURCE_DIR={self.case / 'missing'}")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("checkout has no CMakeLists.txt", result.stdout)
        self.assertFalse((self.build / "_deps").exists())

    def test_standard_override_precedes_explicit_checkout(self):
        """@brief FetchContent's standard source override remains authoritative."""
        source = self.data_source("override-data")
        result = self.configure(f"-DFETCHCONTENT_SOURCE_DIR_OPENSWMMDATA={source}",
                                f"-DOPENSWMM_DATA_SOURCE_DIR={self.case / 'missing'}")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(pathlib.Path((self.build / "selected-source.txt").read_text()), source)
        self.assertEqual((self.build / "target-kind.txt").read_text(), "MODULE_LIBRARY")

    def test_git_fallback_uses_pinned_commit(self):
        """@brief With no sibling, a local Git URL checks out the pin rather than its newer tip."""
        source = self.data_source("upstream-data", "pinned")
        git = ["git", "-c", "user.name=Data build test", "-c", "user.email=test@example.invalid",
               "-c", "commit.gpgsign=false", "-c", "core.hooksPath=" + str(self.case / "empty-hooks")]
        for args in (["init", "-q"], ["add", "CMakeLists.txt", "plugin.cpp", "marker.txt"],
                     ["commit", "-qm", "Pinned fixture"]):
            result = self.command(git + args, cwd=source)
            self.assertEqual(result.returncode, 0, result.stdout)
        revision = self.command(git + ["rev-parse", "HEAD"], cwd=source).stdout.strip()
        (source / "marker.txt").write_text("newer-tip", encoding="utf-8")
        result = self.command(git + ["commit", "-qam", "Newer fixture"], cwd=source)
        self.assertEqual(result.returncode, 0, result.stdout)
        result = self.configure(f"-DOPENSWMM_DATA_GIT_REPOSITORY={source.as_uri()}",
                                f"-DOPENSWMM_DATA_GIT_TAG={revision}")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual((self.build / "selected-marker.txt").read_text(), "pinned")
        self.assertEqual((self.build / "target-kind.txt").read_text(), "MODULE_LIBRARY")


if __name__ == "__main__":
    unittest.main(verbosity=2)
