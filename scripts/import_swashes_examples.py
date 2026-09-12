#!/usr/bin/env python3
"""Import curated SWASHES analytical-verification cases as bundled GUI examples.

The SWASHES QA suite does not commit its SWMM decks -- ``swasheslib.gen1d`` /
``gen2d`` regenerate them into ``runs/<case>/<solver>/model.inp`` on every run.
This script lifts the generated decks for a curated subset of cases, together
with each case's analytic ``reference.csv``, into ``examples/swashes_<case>/``
so the Welcome screen can ship them.

Input files ONLY: ``.rpt``, ``.out``, ``surface.h5``, ``extracted.csv`` and the
suite's figures are never copied.

Usage (from the repo root):

    python3 scripts/import_swashes_examples.py \
        --suite ~/Downloads/epaswmm5_qa/suites/swashes

Re-run after the QA suite regenerates its decks to refresh the payload.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
import textwrap
from pathlib import Path

CATEGORY = "Analytical Verification (SWASHES)"

# Deck file name -> solver directory under runs/<case>/. Named so the
# alphabetically-first entry (1d_dynwave.inp) is what the Welcome screen opens
# by default -- discoverExamples() picks the first *.inp by name.
DECKS = {
    "1d_dynwave.inp": "1d-dynwave",
    "1d_fv.inp": "1d-fv",
    "2d_explicit.inp": "2d-explicit",
}

DECK_PURPOSE = {
    "1d_dynwave.inp": "1D dynamic-wave routing (`FLOW_ROUTING DYNWAVE`)",
    "1d_fv.inp": "1D finite-volume routing (`FLOW_ROUTING FV`)",
    "2d_explicit.inp": "2D shallow-water mesh, explicit local-inertial integrator",
}

# The curated subset: one or two cases per SWASHES section, chosen to cover
# well-balancedness, steady transitions, friction, rainfall, variable width,
# dam breaks and wetting/drying while staying small enough to ship and quick
# enough to run interactively.
CASES = [
    (
        "lake-at-rest-immersed",
        "Lake at Rest: Immersed Bump (SWASHES 3.1.1)",
        "Still water over a fully submerged bump. The well-balancedness test: a "
        "correct scheme holds the lake exactly at rest and generates no spurious "
        "velocity. 25 m frictionless channel.",
    ),
    (
        "bump-subcritical",
        "Bump: Subcritical Flow (SWASHES 3.1.3)",
        "Steady subcritical flow over a bump, everywhere Fr < 1. Checks that the "
        "scheme recovers the smooth analytic backwater profile and conserves "
        "energy over the obstacle.",
    ),
    (
        "bump-transcritical",
        "Bump: Transcritical, No Shock (SWASHES 3.1.4)",
        "Steady flow that accelerates through critical depth at the crest and "
        "stays supercritical downstream. Tests the critical-point transition with "
        "no shock present.",
    ),
    (
        "bump-shock",
        "Bump: Transcritical with Shock (SWASHES 3.1.5)",
        "Flow chokes at the crest, goes supercritical, then returns to subcritical "
        "through a stationary hydraulic jump. Tests shock capture and jump "
        "position (analytic x = 11.67 m).",
    ),
    (
        "macdonald-short-sub2sup",
        "MacDonald: Short Channel, Sub-to-Supercritical (SWASHES 3.2.2)",
        "MacDonald's 100 m analytic friction profile passing smoothly through "
        "critical depth at x = 50 m. Tests the balance of bed slope, Manning "
        "friction and convective acceleration.",
    ),
    (
        "macdonald-rain-sub",
        "MacDonald: Subcritical Channel with Rain (SWASHES 3.3.1)",
        "Steady subcritical profile in a 1000 m friction channel fed by uniform "
        "rainfall along its length, so discharge grows with x. Tests distributed "
        "lateral inflow against a closed-form solution. 1D only.",
    ),
    (
        "p2d-jump-short",
        "Pseudo-2D: Hydraulic Jump in a Varying-Width Channel (SWASHES 3.5.4)",
        "A 200 m channel whose width B(x) varies along its length, holding a "
        "hydraulic jump at x = 120 m. Tests width-change source terms together "
        "with shock capture. 1D only.",
    ),
    (
        "stoker-wet-dam-break",
        "Stoker: Dam Break on a Wet Bed (SWASHES 4.1.1)",
        "Instantaneous dam break over standing water: a rarefaction running "
        "upstream and a bore running downstream. The classic transient Riemann "
        "test, compared at t = 2, 4 and 6 s.",
    ),
    (
        "ritter-dry-dam-break",
        "Ritter: Dam Break on a Dry Bed (SWASHES 4.1.2)",
        "Dam break onto a dry bed -- Ritter's solution. Tests the wet/dry front "
        "and its propagation speed, the hardest part of any shallow-water scheme.",
    ),
    (
        "thacker-radial-2d",
        "Thacker: Radial Paraboloid Oscillation (SWASHES 4.2.2)",
        "Frictionless water sloshing in a radially symmetric paraboloid basin, a "
        "periodic analytic solution with a continuously moving shoreline. Tests "
        "2D wetting and drying. 2D only.",
    ),
]

CITATION = (
    "Analytical benchmark from Delestre et al. (2013), *SWASHES: a compilation "
    "of shallow water analytic solutions for hydraulic and environmental "
    "studies*, International Journal for Numerical Methods in Fluids "
    "72(3):269-300, doi [10.1002/fld.3741](https://doi.org/10.1002/fld.3741)"
)

PROVENANCE_NOTE = """\
## Provenance

The decks were generated by the SWASHES suite of the openswmm.engine
benchmarks (`swasheslib.gen1d` / `swasheslib.gen2d`) and imported by
`scripts/import_swashes_examples.py`. All SWASHES decks use SI units
(`FLOW_UNITS CMS`) and share one normalized `[OPTIONS]` block so that solver
columns differ only in the routing scheme.

`reference.csv` is an independent Python reimplementation of the published
closed-form solutions (`swasheslib/analytic.py`); no output of the CeCILL-V2
SWASHES tool is vendored here.
"""

def _num(v: str) -> str:
    """Trim provenance floats for display: "25.0" -> "25"."""
    try:
        return f"{float(v):g}"
    except ValueError:
        return v


def _times(v: str) -> str:
    """Render a provenance list literal as a plain list: "[2.0, 4.0]" -> "2, 4"."""
    return ", ".join(_num(t.strip()) for t in v.strip("[]").split(",") if t.strip())


# Rows of the case-parameters table: provenance key -> (label, formatter).
PARAM_ROWS = [
    ("L_m", "Domain length", lambda v: f"{_num(v)} m"),
    ("nx", "Cells (nx)", str),
    ("n_manning", "Manning n", lambda v: "0 (frictionless)" if float(v) == 0 else _num(v)),
    ("W1d_m", "1D channel width", lambda v: f"{_num(v)} m"),
    ("W2d_m", "2D mesh width", lambda v: f"{_num(v)} m"),
    ("upstream", "Upstream boundary", str),
    ("downstream", "Downstream boundary", str),
    ("dt_routing_s", "Routing step", lambda v: "adaptive" if float(v) == 0 else f"{_num(v)} s"),
    ("report_step_s", "Report step", lambda v: f"{_num(v)} s"),
    ("t_end_s", "Simulation end", lambda v: f"{_num(v)} s"),
    ("compare_times_s", "Comparison times", lambda v: f"{_times(v)} s"),
    ("x_shock_m", "Analytic shock position", lambda v: f"x = {_num(v)} m"),
]


def wrap(text: str) -> str:
    return textwrap.fill(" ".join(text.split()), width=78)


def parse_provenance(path: Path) -> dict[str, object]:
    """Pull `title`, `source.section` and the flat `parameters:` block out of a
    provenance.yaml. Deliberately a small regex reader rather than a PyYAML
    dependency -- the keys consumed here are all flat scalars."""
    text = path.read_text(encoding="utf-8")

    out: dict[str, object] = {}
    if m := re.search(r'^id:\s*"?(.*?)"?\s*$', text, re.M):
        out["id"] = m.group(1)
    if m := re.search(r'^title:\s*"?(.*?)"?\s*$', text, re.M):
        out["title"] = m.group(1)
    if m := re.search(r'^\s+section:\s*"?(.*?)"?\s*$', text, re.M):
        out["section"] = m.group(1)

    params: dict[str, str] = {}
    in_params = False
    for line in text.splitlines():
        if re.match(r"^parameters:\s*$", line):
            in_params = True
            continue
        if in_params:
            if re.match(r"^\S", line):  # next top-level key ends the block
                break
            if m := re.match(r"^\s+([A-Za-z_0-9]+):\s*(.*?)\s*$", line):
                params[m.group(1)] = m.group(2)
    out["parameters"] = params
    return out


def render_readme(name: str, blurb: str, prov: dict, decks: list[str]) -> str:
    params: dict[str, str] = prov["parameters"]  # type: ignore[assignment]
    section = prov.get("section", "")
    title = prov.get("title", "")

    lines = [f"# {name}", ""]
    if title:
        lines += [wrap(f'SWASHES case `{prov.get("id", "")}` -- "{title}".'), ""]
    lines += [wrap(blurb), "", wrap(f"{CITATION}, {section}."), ""]

    lines += ["## What ships here", "", "| File | Purpose |", "|---|---|"]
    for i, deck in enumerate(decks):
        suffix = " -- opened by default" if i == 0 else ""
        lines.append(f"| `{deck}` | {DECK_PURPOSE[deck]}{suffix} |")
    has_1d = any(d.startswith("1d_") for d in decks)
    has_2d = any(d.startswith("2d_") for d in decks)
    results = "`.out`, `.rpt` and `surface.h5`" if has_2d else "`.out` and `.rpt`"
    lines += [
        "| `reference.csv` | Analytic solution, columns `t_s,x_m,h_m,q_m2_per_s` |",
        "| `example.json` | Welcome-screen display metadata |",
        "",
        wrap(f"Input files only. Results ({results}) are written next to the "
             "copied `.inp` when you run, never into the bundled baseline."),
        "",
    ]

    # Suppress the width of a dimension this case does not ship a deck for.
    skip = set()
    if not has_1d:
        skip.add("W1d_m")
    if not has_2d:
        skip.add("W2d_m")

    lines += ["## Case parameters", "", "| Parameter | Value |", "|---|---|"]
    for key, label, fmt in PARAM_ROWS:
        if key in params and key not in skip:
            lines.append(f"| {label} | {fmt(params[key])} |")
    lines.append("")

    n_manning = float(params.get("n_manning", 0) or 0)
    w1d = float(params.get("W1d_m", 0) or 0)
    if n_manning > 0 and w1d >= 100:
        lines += [
            wrap(f"The 1D channel is {w1d:g} m wide because SWASHES derives its "
                 "friction solutions assuming hydraulic radius equals depth. A "
                 "wide rectangular section makes R -> h, so the 1D deck can be "
                 "compared against the published profile directly."),
            "",
        ]

    lines += [
        "## Comparing against the analytic solution",
        "",
        wrap("`reference.csv` tabulates depth `h` and unit discharge `q` on a "
             "grid refined 20x relative to the model, at the comparison times "
             "listed above. Run a deck, export the depth profile along the "
             "channel at one of those times, and plot it against the matching "
             "`t_s` rows."),
        "",
        PROVENANCE_NOTE,
    ]
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--suite",
        type=Path,
        default=Path.home() / "Downloads/epaswmm5_qa/suites/swashes",
        help="Root of the SWASHES QA suite (must contain cases/ and runs/).",
    )
    ap.add_argument(
        "--examples",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "examples",
        help="Destination examples/ directory.",
    )
    args = ap.parse_args()

    suite: Path = args.suite.expanduser()
    for sub in ("cases", "runs"):
        if not (suite / sub).is_dir():
            print(f"error: {suite / sub} not found -- is --suite correct?", file=sys.stderr)
            return 2

    total = 0
    strays: list[str] = []
    for case, name, blurb in CASES:
        prov_path = suite / "cases" / case / "provenance.yaml"
        ref_path = suite / "cases" / case / "reference.csv"
        if not prov_path.is_file() or not ref_path.is_file():
            print(f"error: {case}: missing provenance.yaml or reference.csv", file=sys.stderr)
            return 1

        dest = args.examples / ("swashes_" + case.replace("-", "_"))
        dest.mkdir(parents=True, exist_ok=True)

        copied: list[str] = []
        for deck_name, solver in DECKS.items():
            src = suite / "runs" / case / solver / "model.inp"
            if not src.is_file():
                continue  # 1D-only and 2D-only cases legitimately lack decks
            shutil.copy2(src, dest / deck_name)
            copied.append(deck_name)
        if not copied:
            print(f"error: {case}: no generated decks under runs/ -- run the "
                  f"QA suite first", file=sys.stderr)
            return 1

        shutil.copy2(ref_path, dest / "reference.csv")

        prov = parse_provenance(prov_path)
        (dest / "example.json").write_text(
            json.dumps(
                {"name": name, "description": blurb, "category": CATEGORY},
                indent=4,
            )
            + "\n",
            encoding="utf-8",
        )
        (dest / "README.md").write_text(
            render_readme(name, blurb, prov, copied), encoding="utf-8"
        )

        # Files are overwritten in place, never deleted -- report anything the
        # payload should not contain (e.g. a deck a case no longer generates,
        # or results from a stray run) so it can be removed deliberately.
        expected = set(copied) | {"reference.csv", "example.json", "README.md"}
        for stray in sorted(p.name for p in dest.iterdir() if p.name not in expected):
            print(f"  warning: unexpected file in {dest.name}: {stray}", file=sys.stderr)
            strays.append(f"{dest.name}/{stray}")

        size = sum(f.stat().st_size for f in dest.iterdir())
        total += size
        print(f"{dest.name:44s} {len(copied)} deck(s)  {size / 1024:8.0f} KiB")

    print(f"{'TOTAL':44s} {len(CASES)} cases    {total / 1024:8.0f} KiB")
    if strays:
        print(f"\n{len(strays)} unexpected file(s) left in place -- delete them "
              f"if they are stale:", file=sys.stderr)
        for s in strays:
            print(f"  {s}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
