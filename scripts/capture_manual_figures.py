#!/usr/bin/env python3
"""Capture real SWMMVis widgets with isolated settings and retained provenance.

Select the application with -a, use -l for a native display and -d for each
batch's directory. Supply working copies of example models. Capture success
is not visual acceptance: review each PNG before publishing it.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def fingerprint(path: Path) -> dict:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return {"path": str(path), "sha256": digest.hexdigest(), "bytes": path.stat().st_size}


def git_output(*args: str) -> str:
    result = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else "unavailable"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-a", "--app", type=Path, default=ROOT / "build/SWMMVis.app/Contents/MacOS/SWMMVis")
    parser.add_argument("-f", "--manifest", type=Path, default=ROOT / "docs/manual/figures.json")
    parser.add_argument("-d", "--output", type=Path, default=ROOT / "tests/output/manual_figures")
    parser.add_argument("-m", "--model", type=Path)
    parser.add_argument("-o", "--only", help="Comma-separated figure filenames")
    parser.add_argument("-l", "--live", action="store_true", help="Use the native display")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--prepare-only", action="store_true", help="Validate and prepare without launching")
    args = parser.parse_args()
    app, source, output = args.app.resolve(), args.manifest.resolve(), args.output.resolve()
    if not app.is_file() or not os.access(app, os.X_OK):
        parser.error(f"Application is not executable: {app}")
    if args.timeout <= 0:
        parser.error("Timeout must be positive")
    manifest = json.loads(source.read_text())
    requested = set(args.only.split(",")) if args.only else None
    rows = manifest.get("figures", [])
    names = [row["name"] for row in rows]
    if len(names) != len(set(names)):
        parser.error("Manifest has duplicate figure filenames")
    if requested:
        unknown = requested - set(names)
        if unknown:
            parser.error(f"Unknown figures: {', '.join(sorted(unknown))}")
        rows = [row for row in rows if row["name"] in requested]
    if not rows:
        parser.error("No figures selected")
    for row in rows:
        name = row["name"]
        if Path(name).name != name or not name.endswith(".png"):
            parser.error(f"Invalid figure filename: {name}")
        if not args.live and row.get("lane") == "live":
            parser.error(f"{name} needs a native display; use -l")
    model = args.model.resolve() if args.model else None
    if model and not model.is_file():
        parser.error(f"Model does not exist: {model}")
    if not args.live and sys.platform == "darwin" and not os.environ.get("QT_PLUGINS"):
        parser.error("Set QT_PLUGINS to this build's Qt platform-plugin directory or use -l")
    output.mkdir(parents=True, exist_ok=True)
    run_manifest = output / "capture-manifest.json"
    if run_manifest == source:
        parser.error("Output directory would overwrite the source manifest")
    manifest["figures"] = rows
    manifest.setdefault("defaults", {})["outDir"] = str(output)
    run_manifest.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
    baseline = {
        "capturedAt": datetime.now(timezone.utc).isoformat(),
        "application": fingerprint(app), "sourceManifest": fingerprint(source),
        "runManifest": fingerprint(run_manifest),
        "model": fingerprint(model) if model else None,
        "guiHead": git_output("rev-parse", "HEAD"),
        "workingTree": git_output("status", "--short"),
        "sourceDiffSha256": hashlib.sha256(git_output("diff", "--", "src", "include", "forms").encode()).hexdigest(),
        "platform": platform.platform(), "nativeDisplay": args.live,
        "figures": [row["name"] for row in rows], "visualReview": "pending",
    }
    frameworks = app.parent.parent / "Frameworks"
    baseline["bundledEngines"] = [fingerprint(p) for p in sorted(frameworks.glob("*openswmm*"))
                                  if p.is_file() and not p.is_symlink()]
    baseline_path = output / "capture-baseline.json"
    baseline_path.write_text(json.dumps(baseline, indent=2) + "\n")
    if args.prepare_only:
        print(f"Prepared {len(rows)} figures: {run_manifest}")
        return 0
    env = dict(os.environ)
    for key in ("SWMMVIS_OPEN_ON_STARTUP", "SWMMVIS_CAPTURE_ONLY"):
        env.pop(key, None)
    env["SWMMVIS_CAPTURE_MANIFEST"] = str(run_manifest)
    env["QT_LOGGING_RULES"] = env.get("QT_LOGGING_RULES", "") + "\nopenswmm.figcap=true"
    if model:
        env["SWMMVIS_OPEN_ON_STARTUP"] = str(model)
    if args.live:
        if sys.platform == "darwin":
            env["QT_QPA_PLATFORM"] = "cocoa"
        else:
            env.pop("QT_QPA_PLATFORM", None)
    else:
        env["QT_QPA_PLATFORM"] = "offscreen"
        if env.get("QT_PLUGINS"):
            env["QT_QPA_PLATFORM_PLUGIN_PATH"] = env["QT_PLUGINS"]
    report = output / "run.json"
    if report.exists():
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%f")
        report.rename(output / f"run.previous-{stamp}.json")
    log_path = output / "capture.log"
    with log_path.open("w") as log:
        try:
            result = subprocess.run([str(app)], cwd=ROOT, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=args.timeout)
            code = result.returncode
        except subprocess.TimeoutExpired:
            code = 124
    baseline["exitCode"] = code
    baseline["applicationUnchanged"] = fingerprint(app)["sha256"] == baseline["application"]["sha256"]
    baseline_path.write_text(json.dumps(baseline, indent=2) + "\n")
    print(f"Capture exit: {code}; report: {report}; log: {log_path}")
    if not report.exists():
        print("No capture report was produced", file=sys.stderr)
        return code if code > 0 else 1
    if not baseline["applicationUnchanged"]:
        print("Application changed during capture; repeat with a stable build", file=sys.stderr)
        return 1
    return code if code >= 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
