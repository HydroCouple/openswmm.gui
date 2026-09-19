#!/usr/bin/env python3
"""Generate appendix A8 — the error and warning catalogue — from the engine.

The engine is the only source of truth for these codes, and SWMMVis shows two
numbering systems side by side: `simulationstatusmodel.cpp` renders a failed run
as `ERROR [<cffi code>] <engine message>`, where the message itself embeds
`ERROR nnn:`. A reader looking up either number needs one page that has both, so
the GUI carries its own catalogue rather than linking out.

The page is generated in full — skeleton included — so `--check` is a plain file
diff with no in-file edit markers to respect.

Why it reads engine source rather than a header we ship against: `ErrorCodes.hpp`
lives at `src/engine/core/` and is not part of the engine's installed public
include tree, and `swmm_error_message()` covers only the CFFI codes plus the
609-635 range, so neither an installed header nor a link-time probe can stand in.

    gen_error_catalogue.py                 regenerate the page
    gen_error_catalogue.py --check         exit non-zero if the page is stale
    gen_error_catalogue.py --engine PATH   use this engine checkout

Both modes exit 0 with a message when the engine checkout is absent, so the
docs-only CI workflow never needs it. The real gate lives in build_and_test.yml,
which already checks the engine out.
"""

from __future__ import annotations

import argparse
import datetime
import difflib
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAGE = ROOT / "docs" / "manual" / "appendices" / "a08_error_codes.md"
HEADER_REL = pathlib.PurePosixPath("src/engine/core/ErrorCodes.hpp")

# `    ERR_MEMORY                  = 101,  ///< memory allocation error`
ENTRY_RE = re.compile(r"^\s*([A-Z][A-Z0-9_]*)\s*=\s*(-?\d+)\s*,?\s*///<\s*(.+?)\s*$", re.M)

TABLES = [
    ("ErrorCode", "Engine errors", "ERROR",
     "Reported by the engine and shown in the Message Logs and the status "
     "report. `%s` stands for the object name the engine names in the message."),
    ("WarnCode", "Engine warnings", "WARNING",
     "The run continues. Warnings are worth reading — several of them mean the "
     "engine silently changed something you asked for."),
    ("CffiErrorCode", "API error codes", "API ERROR",
     "The bracketed number in `ERROR [n] …` in the simulation status panel. "
     "These come from the C API layer; the engine error above is the cause."),
    ("CffiWarnCode", "API warning codes", "API WARNING",
     "The bracketed number of a non-fatal API condition."),
]


def enum_body(text: str, name: str) -> str:
    m = re.search(rf"enum\s+{name}\s*:\s*int\s*\{{(.*?)^\}};", text, re.S | re.M)
    if not m:
        raise SystemExit(f"FAIL: could not find `enum {name}` in {HEADER_REL}")
    return m.group(1)


def parse(text: str, name: str) -> list[tuple[int, str, str]]:
    rows = []
    for m in ENTRY_RE.finditer(enum_body(text, name)):
        symbol, value, desc = m.group(1), int(m.group(2)), m.group(3)
        if value == 0:
            continue  # the NONE sentinel is not a message
        rows.append((value, symbol, desc))
    return sorted(rows)


def git_describe(engine: pathlib.Path) -> tuple[str, str]:
    def run(*args: str) -> str:
        try:
            return subprocess.run(args, cwd=engine, check=True,
                                  capture_output=True, text=True).stdout.strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            return "unknown"
    return run("git", "rev-parse", "--abbrev-ref", "HEAD"), run("git", "rev-parse", "--short", "HEAD")


def escape(s: str) -> str:
    return s.replace("|", "\\|")


def render(engine: pathlib.Path) -> str:
    text = (engine / HEADER_REL).read_text(encoding="utf-8")
    tables = {name: parse(text, name) for name, _, _, _ in TABLES}
    branch, sha = git_describe(engine)
    today = datetime.date.today().isoformat()
    counts = " · ".join(f"{len(tables[n])} {t}" for n, t, _, _ in TABLES)

    out: list[str] = []
    w = out.append
    w("@page manual_error_codes A8 — Error and Warning Codes")
    w("")
    w("## What you'll do")
    w("")
    w("Look up a numbered message that a run produced, and find out which of the")
    w("two numbering systems SWMMVis shows you it belongs to.")
    w("")
    w("A failed run is reported in the simulation status panel as")
    w("`ERROR [n] ERROR mmm: …` — the bracketed `n` is an **API error code** and")
    w("the `mmm` inside the message is an **engine error**. They are different")
    w("catalogues; both are below.")
    w("")
    w("## Where to find it")
    w("")
    w("**Message Logs** panel, the simulation status panel while a run is active,")
    w("and the `.rpt` status report opened from **Analysis → Report**. See")
    w("\\ref manual_running and \\ref manual_tabular_results.")
    w("")
    w("For what to *do* about the common ones, see \\ref manual_troubleshooting,")
    w("which carries remediation advice for the messages users actually hit. This")
    w("appendix is the complete index, not a troubleshooting guide.")
    w("")
    w("## Step-by-step")
    w("")
    w("### How this page is maintained")
    w("")
    w("This page is generated by `scripts/gen_error_catalogue.py` from the engine's")
    w("own enumerations — do not edit it by hand.")
    w("")
    w("| | |")
    w("|---|---|")
    w(f"| Source | `openswmm.engine` `{HEADER_REL}` |")
    w(f"| Engine revision | `{branch}` @ `{sha}` |")
    w(f"| Generated | {today} |")
    w(f"| Codes | {counts} |")
    w("")
    w("`scripts/gen_error_catalogue.py --check` fails when the engine has gained a")
    w("code this page does not list; it runs in CI against the engine checkout that")
    w("`build_and_test.yml` already makes.")
    w("")

    for name, title, label, blurb in TABLES:
        rows = tables[name]
        w(f"### {title}")
        w("")
        w(blurb)
        w("")
        w(f"| {label} | Symbol | Message |")
        w("|---|---|---|")
        for value, symbol, desc in rows:
            w(f"| {value} | `{symbol}` | {escape(desc)} |")
        w("")

    w("## Tips and gotchas")
    w("")
    w("- **Two numbers, two catalogues.** `ERROR [1] ERROR 141: …` is API error 1")
    w("  reporting engine error 141. Look up the inner number first — it is the one")
    w("  that names the object and the cause.")
    w("- **`%s` is a placeholder.** The engine substitutes the offending object's")
    w("  ID, so the message you see names the junction, conduit or subcatchment.")
    w("- **A warning is not a failure, but it may have changed your model.**")
    w("  Several warnings report that the engine substituted a value for one it")
    w("  could not use.")
    w("- **A code missing here means the page is stale**, not that the code is")
    w("  invalid. Regenerate with `scripts/gen_error_catalogue.py`.")
    w("")
    w("## Related")
    w("")
    w("- \\ref manual_troubleshooting — what to do about the common messages")
    w("- \\ref manual_running — the Message Logs and the simulation status panel")
    w("- \\ref manual_tabular_results — the status report viewer")
    w("- \\ref manual_file_formats — the `.rpt` status report")
    w("")
    return "\n".join(out)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--check", action="store_true",
                   help="exit non-zero if the committed page differs")
    p.add_argument("--engine", help="path to an openswmm.engine checkout")
    args = p.parse_args()

    engine = pathlib.Path(args.engine).expanduser() if args.engine else ROOT.parent / "openswmm.engine"
    if not (engine / HEADER_REL).exists():
        print(f"engine checkout not found at {engine} — skipped")
        return 0

    fresh = render(engine)

    if not args.check:
        PAGE.parent.mkdir(parents=True, exist_ok=True)
        PAGE.write_text(fresh, encoding="utf-8")
        print(f"wrote {PAGE.relative_to(ROOT)}")
        return 0

    if not PAGE.exists():
        print(f"FAIL: {PAGE.relative_to(ROOT)} does not exist; run without --check",
              file=sys.stderr)
        return 1

    current = PAGE.read_text(encoding="utf-8")
    if current == fresh:
        print(f"a08 is current with {engine.name} {HEADER_REL}")
        return 0

    # The provenance block carries a date and a SHA, so report whether anything
    # beyond those actually moved — a stale date is not a reason to fail a build.
    def codes_only(t: str) -> list[str]:
        return [l for l in t.splitlines() if re.match(r"^\| -?\d+ \| `", l)]

    if codes_only(current) == codes_only(fresh):
        print("a08 provenance is stale (date or engine revision) but every code matches")
        return 0

    sys.stderr.write("FAIL: a08 is out of date with the engine\n")
    sys.stderr.writelines(difflib.unified_diff(
        codes_only(current), codes_only(fresh),
        fromfile="a08_error_codes.md", tofile="ErrorCodes.hpp", lineterm="", n=0))
    sys.stderr.write("\nregenerate with scripts/gen_error_catalogue.py\n")
    return 1


if __name__ == "__main__":
    sys.exit(main())
