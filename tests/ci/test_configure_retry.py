"""Exercise the CI wrapper with a failing dependency downloader and real processes."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


WRAPPER = Path(__file__).resolve().parents[2] / "scripts/ci_configure_retry.sh"


class ConfigureRetryTest(unittest.TestCase):
    def run_configure(self, outcomes):
        # Keep local evidence under the ignored build tree, including mktemp logs.
        base = WRAPPER.parent.parent / "build/ci_configure_retry_tests"
        base.mkdir(parents=True, exist_ok=True)
        root = Path(tempfile.mkdtemp(dir=base))
        bin_dir = root / "bin"
        bin_dir.mkdir()
        sleep = bin_dir / "sleep"
        sleep.write_text('#!/usr/bin/env bash\nprintf "%s\\n" "$1" >> "$EVIDENCE/delays"\n')
        sleep.chmod(0o755)
        command = root / "configure.py"
        command.write_text('''import json, os, pathlib, sys
root = pathlib.Path(os.environ["EVIDENCE"])
state = root / "attempts"
attempt = int(state.read_text()) if state.exists() else 0
state.write_text(str(attempt + 1))
(root / "args.json").write_text(json.dumps(sys.argv[1:]))
outcome = json.loads(os.environ["OUTCOMES"])[attempt]
print("configure stdout", flush=True)
if outcome == "dns":
    print("error: curl: (6) Could not resolve host: github.com", file=sys.stderr)
    print("CMake Error: CMAKE_CXX_COMPILER not set, after EnableLanguage", file=sys.stderr)
    sys.exit(1)
if outcome.startswith("curl"):
    print("error: curl: (" + outcome[4:] + ") dependency download failed", file=sys.stderr)
    sys.exit(1)
if outcome.startswith("http"):
    print("curl: (22) The requested URL returned error: " + outcome[4:], file=sys.stderr)
    sys.exit(1)
if outcome == "cmake":
    print("CMake Error: Required package is missing", file=sys.stderr)
    sys.exit(23)
sys.exit(0)
''')
        args = ["--preset=Darwin", "-DCMAKE_PREFIX_PATH=/path with spaces;other"]
        env = dict(os.environ, EVIDENCE=str(root), OUTCOMES=json.dumps(outcomes),
                   RUNNER_TEMP=str(root), PATH=str(bin_dir) + os.pathsep + os.environ["PATH"])
        result = subprocess.run(["bash", str(WRAPPER), sys.executable, str(command), *args],
                                text=True, capture_output=True, env=env)
        (root / "output.log").write_text(result.stdout + result.stderr)
        self.assertEqual(json.loads((root / "args.json").read_text()), args)
        self.assertIn("configure stdout", result.stdout)
        self.assertEqual(list(root.glob("openswmm-configure.*")), [])
        delays = (root / "delays").read_text().splitlines() if (root / "delays").exists() else []
        return result, int((root / "attempts").read_text()), delays

    def test_success_runs_once(self):
        result, attempts, delays = self.run_configure(["ok"])
        self.assertEqual((result.returncode, attempts, delays), (0, 1, []))

    def test_download_dns_failure_recovers(self):
        result, attempts, delays = self.run_configure(["dns", "ok"])
        self.assertEqual((result.returncode, attempts, delays), (0, 2, ["20"]))
        self.assertIn("Could not resolve host: github.com", result.stdout)

    def test_persistent_dns_failure_stops_after_three_attempts(self):
        result, attempts, delays = self.run_configure(["dns", "dns", "dns"])
        self.assertEqual((result.returncode, attempts, delays), (1, 3, ["20", "40"]))

    def test_transient_http_download_failures_recover(self):
        for status in (408, 429, 500, 502, 503, 504):
            with self.subTest(status=status):
                result, attempts, delays = self.run_configure([f"http{status}", "ok"])
                self.assertEqual((result.returncode, attempts, delays), (0, 2, ["20"]))

    def test_persistent_gateway_failure_stops_after_three_attempts(self):
        result, attempts, delays = self.run_configure(["http504"] * 3)
        self.assertEqual((result.returncode, attempts, delays), (1, 3, ["20", "40"]))

    def test_permanent_http_errors_fail_immediately(self):
        for status in (400, 401, 403, 404, 501):
            with self.subTest(status=status):
                result, attempts, delays = self.run_configure([f"http{status}"])
                self.assertEqual((result.returncode, attempts, delays), (1, 1, []))

    def test_old_gateway_output_does_not_retry_a_new_configuration_error(self):
        result, attempts, delays = self.run_configure(["http504", "cmake"])
        self.assertEqual((result.returncode, attempts, delays), (23, 2, ["20"]))

    def test_transient_connection_timeout_and_reset_failures_recover(self):
        for code in (7, 28, 35, 52, 56):
            with self.subTest(code=code):
                result, attempts, delays = self.run_configure([f"curl{code}", "ok"])
                self.assertEqual((result.returncode, attempts, delays), (0, 2, ["20"]))

    def test_persistent_timeout_is_bounded(self):
        result, attempts, delays = self.run_configure(["curl28"] * 3)
        self.assertEqual((result.returncode, attempts, delays), (1, 3, ["20", "40"]))

    def test_certificate_failure_is_not_retried(self):
        result, attempts, delays = self.run_configure(["curl60"])
        self.assertEqual((result.returncode, attempts, delays), (1, 1, []))

    def test_old_timeout_does_not_retry_a_configuration_error(self):
        result, attempts, delays = self.run_configure(["curl28", "cmake"])
        self.assertEqual((result.returncode, attempts, delays), (23, 2, ["20"]))

    def test_configuration_error_fails_immediately(self):
        result, attempts, delays = self.run_configure(["cmake"])
        self.assertEqual((result.returncode, attempts, delays), (23, 1, []))

    def test_old_dns_output_does_not_retry_a_new_configuration_error(self):
        result, attempts, delays = self.run_configure(["dns", "cmake"])
        self.assertEqual((result.returncode, attempts, delays), (23, 2, ["20"]))


if __name__ == "__main__":
    unittest.main()
