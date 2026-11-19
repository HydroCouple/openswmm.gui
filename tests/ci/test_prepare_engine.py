"""Exercise companion patch application and refuse conflicts before mutation."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("prepare_engine", ROOT / "scripts/ci_prepare_engine.py")
prepare_engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare_engine)


class PrepareEngineTest(unittest.TestCase):
    def setUp(self):
        base = ROOT / "build/ci_prepare_engine_tests"
        base.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(dir=base))
        self.engine = self.root / "engine with spaces"
        self.header = self.engine / "include/openswmm/engine/openswmm_edit.h"
        self.header.parent.mkdir(parents=True)
        self.header.write_text("// original API\n")
        subprocess.run(["git", "init", "-q", str(self.engine)], check=True)
        self.patch = self.root / "companion patch.diff"
        self.patch.write_text(
            "--- a/include/openswmm/engine/openswmm_edit.h\n"
            "+++ b/include/openswmm/engine/openswmm_edit.h\n"
            "@@ -1 +1,2 @@\n"
            " // original API\n"
            "+SWMM_ENGINE_API int swmm_convert_types(void);\n")

    def test_companion_patch_is_applied_once(self):
        prepare_engine.prepare(self.engine, self.patch)
        expected = self.header.read_bytes()
        self.assertIn(b"swmm_convert_types", expected)
        prepare_engine.prepare(self.engine, self.patch)
        self.assertEqual(self.header.read_bytes(), expected)

    def test_a_comment_mention_is_not_an_upstream_api(self):
        self.header.write_text("/* SWMM_ENGINE_API int swmm_convert_types(void); */\n"
                               "// original API\n")
        self.patch.write_text(self.patch.read_text().replace(
            "@@ -1 +1,2 @@\n", "@@ -1,2 +1,3 @@\n"
            " /* SWMM_ENGINE_API int swmm_convert_types(void); */\n"))
        prepare_engine.prepare(self.engine, self.patch)
        self.assertIn("\nSWMM_ENGINE_API int swmm_convert_types(void);\n", self.header.read_text())

    def test_upstream_api_does_not_need_the_patch(self):
        text = "SWMM_ENGINE_API\nint swmm_convert_types\n(void);\n"
        self.header.write_text(text)
        prepare_engine.prepare(self.engine, self.root / "missing patch")
        self.assertEqual(self.header.read_text(), text)

    def test_incompatible_source_stays_unmodified(self):
        self.header.write_text("// conflicting source\n")
        before = self.header.read_bytes()
        with self.assertRaises(subprocess.CalledProcessError):
            prepare_engine.prepare(self.engine, self.patch)
        self.assertEqual(self.header.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
