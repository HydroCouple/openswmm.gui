#!/usr/bin/env python3
"""Audit the user manual against the sources it is supposed to track.

Three independent checks, each a subcommand, each exiting non-zero on failure:

    crosswalk        every heading of the retired legacy manuals is accounted for
    culvert-codes    A7's culvert table matches the combo the user actually sees
    pipe-sizes       A7's elliptical and arch tables match the engine's lookup arrays

`crosswalk` is the evidence for deleting docs/user-guide, docs/reference,
docs/basic-tutorial and docs/inlet-tutorial. It reads those files through
`git show <ref>:<path>` rather than from the working tree, so it keeps working
after the deletion lands — which is the whole point of keeping the record.

`culvert-codes` and `pipe-sizes` exist because three of A7's tables are not prose
but lookup data compiled into a binary: transcribing them from the EPA manual
would fork them. They are pinned to their real source instead.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CROSSWALK = ROOT / "docs" / "LEGACY_MANUAL_CROSSWALK_2026-09-19.md"
MANUAL = ROOT / "docs" / "manual"

LEGACY_FILES = [
    "docs/user-guide/user-guide.md",
    "docs/reference/reference.md",
    "docs/basic-tutorial/basic-tutorial.md",
    "docs/inlet-tutorial/inlet-tutorial.md",
]

DISPOSITIONS = {"ported", "superseded", "retired", "engine"}

# Legacy anchors come in two spellings, one of them a typo in the source:
#   "### Editing an Object {editing_object}"   (missing the #)
#   "### Adding an Object {#adding_object}"
ANCHOR_RE = re.compile(r"\s*\{#?[^}]*\}\s*$")
HEADING_RE = re.compile(r"^(#{2,3})\s+(.+?)\s*$")


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)


def read_at_ref(ref: str, path: str) -> str | None:
    """Return the file's content at `ref`, or None when it is not there."""
    try:
        return subprocess.run(
            ["git", "show", f"{ref}:{path}"],
            cwd=ROOT, check=True, capture_output=True, text=True,
        ).stdout
    except subprocess.CalledProcessError:
        return None


def headings_of(text: str) -> list[tuple[str, str]]:
    out = []
    for line in text.splitlines():
        m = HEADING_RE.match(line)
        if m:
            out.append((f"h{len(m.group(1))}", ANCHOR_RE.sub("", m.group(2)).strip()))
    return out


def parse_crosswalk() -> tuple[str, list[dict]]:
    if not CROSSWALK.exists():
        raise SystemExit(f"FAIL: {CROSSWALK.relative_to(ROOT)} does not exist")
    text = CROSSWALK.read_text(encoding="utf-8")
    m = re.search(r"<!--\s*crosswalk-source-ref:\s*(\S+)\s*-->", text)
    if not m:
        raise SystemExit("FAIL: the crosswalk has no `crosswalk-source-ref` header")
    ref = m.group(1)

    rows = []
    for line in text.splitlines():
        if not line.startswith("| "):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 5 or cells[1] not in ("h2", "h3"):
            continue
        rows.append({
            "source": cells[0], "level": cells[1], "heading": cells[2],
            "disposition": cells[3], "target": cells[4],
        })
    return ref, rows


def page_ids() -> set[str]:
    ids = set()
    for md in MANUAL.rglob("*.md"):
        for line in md.read_text(encoding="utf-8", errors="replace").splitlines():
            m = re.match(r"^@page\s+(\S+)", line)
            if m:
                ids.add(m.group(1))
    return ids


def cmd_crosswalk(args: argparse.Namespace) -> int:
    ref, rows = parse_crosswalk()
    problems = 0

    # The ref must resolve; a stale one would silently check nothing.
    missing_at_ref = [p for p in LEGACY_FILES if read_at_ref(ref, p) is None]
    if missing_at_ref:
        fail(f"crosswalk-source-ref '{ref}' does not contain: {', '.join(missing_at_ref)}")
        fail("point it at the last commit in which the legacy files existed")
        return 1

    by_source: dict[str, list[dict]] = {}
    for r in rows:
        by_source.setdefault(r["source"], []).append(r)

    total = 0
    for path in LEGACY_FILES:
        name = pathlib.PurePosixPath(path).name
        actual = headings_of(read_at_ref(ref, path) or "")
        total += len(actual)
        listed = by_source.get(name, [])

        counts: dict[tuple[str, str], int] = {}
        for r in listed:
            counts[(r["level"], r["heading"])] = counts.get((r["level"], r["heading"]), 0) + 1

        for key in actual:
            n = counts.get(key, 0)
            if n == 0:
                fail(f"{name}: heading not in the crosswalk — {key[0]} {key[1]!r}")
                problems += 1
            elif n > 1:
                fail(f"{name}: heading listed {n} times — {key[0]} {key[1]!r}")
                problems += 1
                counts[key] = 1  # report once

        actual_set = set(actual)
        for r in listed:
            if (r["level"], r["heading"]) not in actual_set:
                fail(f"{name}: crosswalk names a heading that does not exist — "
                     f"{r['level']} {r['heading']!r}")
                problems += 1

    for r in rows:
        if r["disposition"] not in DISPOSITIONS:
            fail(f"{r['source']}: unknown disposition {r['disposition']!r} "
                 f"for {r['heading']!r}")
            problems += 1
        if r["disposition"] in ("ported", "superseded") and not r["target"]:
            fail(f"{r['source']}: {r['disposition']} row needs a target — {r['heading']!r}")
            problems += 1
        if r["disposition"] in ("retired", "engine") and r["target"]:
            fail(f"{r['source']}: {r['disposition']} row must not name a target — "
                 f"{r['heading']!r}")
            problems += 1

    if args.strict:
        known = page_ids()
        for r in rows:
            t = r["target"]
            if t and t not in known:
                fail(f"{r['source']}: target {t!r} is not a @page id in docs/manual "
                     f"({r['heading']!r})")
                problems += 1

    tally: dict[str, int] = {}
    for r in rows:
        tally[r["disposition"]] = tally.get(r["disposition"], 0) + 1
    summary = "  ".join(f"{k}={tally.get(k, 0)}" for k in sorted(DISPOSITIONS))

    if problems:
        print(f"crosswalk: {problems} problem(s); {len(rows)} rows for {total} headings",
              file=sys.stderr)
        return 1
    print(f"crosswalk OK — {len(rows)} rows cover {total} headings at {ref}  ({summary})")
    return 0


def cmd_culvert_codes(_args: argparse.Namespace) -> int:
    src = ROOT / "src" / "ui" / "properties" / "culvertcodes.cpp"
    if not src.exists():
        fail(f"{src.relative_to(ROOT)} does not exist")
        return 1
    page = MANUAL / "appendices" / "a07_reference_tables.md"
    if not page.exists():
        fail(f"{page.relative_to(ROOT)} does not exist")
        return 1

    codes = {}
    for m in re.finditer(r'\{\s*(\d+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}',
                         src.read_text(encoding="utf-8")):
        codes[int(m.group(1))] = m.group(3)
    if not codes:
        fail("parsed no culvert codes — the table's shape in culvertcodes.cpp changed")
        return 1

    # Scope to the culvert section: the elliptical and arch tables also lead with
    # a bare code number and would otherwise be read as culvert codes.
    text = page.read_text(encoding="utf-8")
    m = re.search(r"^### Culvert Code Numbers\s*$(.*?)^### ", text, re.M | re.S)
    if not m:
        fail("A7 has no '### Culvert Code Numbers' section")
        return 1
    documented = {int(x.group(1)) for x in re.finditer(r"^\|\s*(\d+)\s*\|", m.group(1), re.M)}

    problems = 0
    for code in sorted(set(codes) - documented):
        fail(f"culvert code {code} ({codes[code]!r}) is not in A7")
        problems += 1
    for code in sorted(documented - set(codes)):
        fail(f"A7 documents culvert code {code}, which culvertcodes.cpp does not define")
        problems += 1

    if problems:
        return 1
    print(f"culvert-codes OK — {len(codes)}/{len(codes)} codes documented")
    return 0


def cmd_pipe_sizes(args: argparse.Namespace) -> int:
    engine = pathlib.Path(args.engine).expanduser() if args.engine else ROOT.parent / "openswmm.engine"
    header = engine / "src" / "engine" / "hydraulics" / "xsect_tables.hpp"
    if not header.exists():
        print(f"pipe-sizes: engine checkout not found at {engine} — skipped")
        return 0
    page = MANUAL / "appendices" / "a07_reference_tables.md"
    if not page.exists():
        fail(f"{page.relative_to(ROOT)} does not exist")
        return 1

    text = header.read_text(encoding="utf-8")
    expected = {}
    for name in ("NumCodesEllipse", "NumCodesArch"):
        m = re.search(rf"\b{name}\b\s*=\s*(\d+)", text)
        if not m:
            fail(f"could not read {name} from {header}")
            return 1
        expected[name] = int(m.group(1))

    page_text = page.read_text(encoding="utf-8")
    problems = 0
    for name, label in (("NumCodesEllipse", "elliptical"), ("NumCodesArch", "arch")):
        m = re.search(rf"<!--\s*{label}-sizes:\s*(\d+)\s*-->", page_text)
        if not m:
            fail(f"A7 has no `<!-- {label}-sizes: N -->` marker to check")
            problems += 1
            continue
        if int(m.group(1)) != expected[name]:
            fail(f"A7 claims {m.group(1)} {label} sizes; {name} = {expected[name]}")
            problems += 1

    if problems:
        return 1
    print(f"pipe-sizes OK — elliptical={expected['NumCodesEllipse']} "
          f"arch={expected['NumCodesArch']}")
    return 0


# The adapters that drive the twelve object kinds the legacy manual catalogued.
# A9 routes to the chapters; this check is what keeps those chapters complete.
LABEL_ADAPTERS = [
    "src/ui/properties/swmmnodepropertyadapter.cpp",
    "src/ui/properties/swmmlinkpropertyadapter.cpp",
    "src/ui/properties/swmmsubcatchpropertyadapter.cpp",
    "src/ui/properties/swmmraingagepropertyadapter.cpp",
]
ALLOWLIST = ROOT / "scripts" / "manual_docs_audit_allowlist.txt"

# `    if (property == QLatin1String("maxDepth")) return tr("Max Depth (%1)").arg(L);`
LABEL_RE = re.compile(
    r'property\s*==\s*QLatin1String\("([A-Za-z0-9_]+)"\)\s*\)?\s*'
    r'return\s+tr\("((?:[^"\\]|\\.)*)"\)')
# Unit suffixes are injected at runtime from UnitSystem, and the manual writes
# the bare label. Drop the whole parenthesised group when it holds a placeholder
# — "(%1)", but also "(%1³)" and "(%1/hr)" — then any placeholder left over.
UNIT_GROUP_RE = re.compile(r"\s*\([^()]*%\d[^()]*\)")
PLACEHOLDER_RE = re.compile(r"\s*%\d\s*")


def load_allowlist() -> dict[str, str]:
    out: dict[str, str] = {}
    if not ALLOWLIST.exists():
        return out
    for line in ALLOWLIST.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        prop, _, reason = line.partition("#")
        out[prop.strip()] = reason.strip()
    return out


def cmd_property_labels(_args: argparse.Namespace) -> int:
    # One haystack, whitespace collapsed: the manual line-wraps, so a label like
    # "Startup Depth" can be split across two lines and must still match.
    haystack = " ".join(
        " ".join(md.read_text(encoding="utf-8", errors="replace").split())
        for md in sorted(MANUAL.rglob("*.md"))
    )

    allowed = load_allowlist()
    problems = 0
    checked = 0
    skipped = 0

    for rel in LABEL_ADAPTERS:
        src = ROOT / rel
        if not src.exists():
            fail(f"{rel} does not exist")
            return 1
        pairs = LABEL_RE.findall(src.read_text(encoding="utf-8"))
        if not pairs:
            fail(f"parsed no labels from {rel} — displayLabelFor's shape changed")
            return 1
        for prop, label in pairs:
            text = PLACEHOLDER_RE.sub(" ", UNIT_GROUP_RE.sub("", label)).strip()
            if not text:
                continue
            if prop in allowed:
                skipped += 1
                continue
            checked += 1
            if " ".join(text.split()) not in haystack:
                fail(f"{pathlib.PurePosixPath(rel).name}: label {text!r} "
                     f"(property {prop!r}) appears nowhere in docs/manual")
                problems += 1

    if problems:
        print(f"property-labels: {problems} undocumented of {checked}", file=sys.stderr)
        print("document it, or add the property to "
              f"{ALLOWLIST.relative_to(ROOT)} with a reason", file=sys.stderr)
        return 1
    print(f"property-labels OK — {checked}/{checked} documented, {skipped} allowlisted")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("crosswalk", help="every legacy heading is accounted for")
    c.add_argument("--strict", action="store_true",
                   help="also assert every target is a real @page id")
    c.set_defaults(func=cmd_crosswalk)

    c = sub.add_parser("property-labels",
                       help="every property row label appears somewhere in the manual")
    c.set_defaults(func=cmd_property_labels)

    c = sub.add_parser("culvert-codes", help="A7 matches src/ui/properties/culvertcodes.cpp")
    c.set_defaults(func=cmd_culvert_codes)

    c = sub.add_parser("pipe-sizes", help="A7 matches the engine's xsect_tables.hpp")
    c.add_argument("--engine", help="path to an openswmm.engine checkout")
    c.set_defaults(func=cmd_pipe_sizes)

    args = p.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
