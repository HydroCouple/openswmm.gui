#!/usr/bin/env python3
"""Refresh a documentation coverage ledger without discarding review decisions.

Source discovery is not runtime verification. Generated rows remain pending
until their application workflow and documentation have been reviewed.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
LEDGER = ROOT / "workplans/DOCUMENTATION_COVERAGE.json"
MANUAL = ROOT / "docs/manual"


def discover() -> dict[str, dict]:
    rows = {}
    forms = ET.parse(ROOT / "forms/swmmvis.ui")
    labels = {a.attrib["name"]: a.findtext("property[@name='text']/string", "")
              for a in forms.findall(".//action")}
    catalog = ROOT / "include/ui/actioncatalog.h"
    for line_number, line in enumerate(catalog.read_text().splitlines(), 1):
        values = re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', line)
        if len(values) != 7 or not line.lstrip().startswith('{"'):
            continue
        identity, action, group, shortcut, icon, tab, menu = values
        rows[f"action:{identity}"] = dict(kind="action", label=labels.get(action) or identity,
            actionObject=action, source=f"include/ui/actioncatalog.h:{line_number}",
            group=group, shortcut=shortcut, ribbon=tab, menu=menu)
    for path in sorted((ROOT / "include/ui").rglob("*.h")):
        text = path.read_text()
        for match in re.finditer(r'class\s+(\w+)\s*(?:final\s*)?:\s*public\s+(QDialog|QWidget|QScrollArea|SimOptionsPage)\b', text):
            name, base = match.groups()
            if base != "QDialog" and "dialogs" not in path.parts:
                continue
            rows[f"surface:{name}"] = dict(kind="surface", label=name,
                base=base, source=f"{path.relative_to(ROOT)}:{text.count(chr(10), 0, match.start()) + 1}")
    manifest = {row["name"]: row for row in json.loads((MANUAL / "figures.json").read_text())["figures"]}
    for path in sorted(MANUAL.rglob("*.md")):
        text = path.read_text()
        if not text.startswith("@page"):
            continue
        page = str(path.relative_to(ROOT))
        rows[f"page:{page}"] = dict(kind="page", label=text.splitlines()[0], source=page)
        for match in re.finditer(r'\\(figtodo|fig)\{([^,}]+),([^}]+)\}', text):
            mode, name, caption = match.groups()
            recipe = manifest.get(name, {})
            rows[f"figure:{name}"] = dict(kind="figure", label=caption.strip(), source=page,
                figure=name, published=mode == "fig", recipePresent=bool(recipe),
                captureLane=recipe.get("lane"), recipeNote=recipe.get("_note", ""))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check discovery against the saved ledger")
    args = parser.parse_args()
    current = discover()
    old = json.loads(LEDGER.read_text()) if LEDGER.exists() else {"entries": {}}
    previous = old["entries"]
    if args.check:
        added, removed = current.keys() - previous.keys(), previous.keys() - current.keys()
        print(f"Coverage discovery: {len(current)} entries; {len(added)} new; {len(removed)} retired")
        for key in sorted(added): print(f"NEW {key}")
        for key in sorted(removed): print(f"RETIRED {key}")
        return int(bool(added or removed))
    entries = {}
    for key, row in current.items():
        review = previous.get(key, {}).get("review", {})
        row["review"] = review or {"status": "pending", "chapter": row["source"] if row["kind"] in ("page", "figure") else "",
                                   "runtimeEvidence": [], "notes": "Source discovery; requires workflow and content review."}
        entries[key] = row
    retired = dict(old.get("retired", {}))
    retired.update({key: row for key, row in previous.items() if key not in current})
    result = {"scope": "Source-discovered actions and UI surfaces plus manual pages and figures; not a claim of runtime completeness.",
              "entries": entries, "retired": retired}
    LEDGER.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n")
    counts = Counter(row["kind"] for row in entries.values())
    missing = [row for row in entries.values() if row["kind"] == "figure" and not row["recipePresent"]]
    print(f"Saved {LEDGER.relative_to(ROOT)}: {dict(counts)}; {len(missing)} figures need recipes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
