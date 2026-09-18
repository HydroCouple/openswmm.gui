#!/usr/bin/env python3
"""Manage the user manual's figures: audit, flip placeholders, refresh the TODO.

The manual declares every screenshot twice-over:

    \\figtodo{07_layers_panel.png, caption}   placeholder — renders a grey box
    \\fig{07_layers_panel.png, caption}       the real thing

Capturing a figure (scripts/capture_manual_figures.sh) stages a PNG under
tests/output/manual_figures/. Publishing it means copying it into
docs/manual/images/ and turning that one \\figtodo into \\fig — which is what
`flip` does, refusing the batch if anything about the PNG is wrong.

Subcommands
    audit   check every published figure; exit non-zero on any problem
    flip    publish staged PNGs and flip their placeholders
    todo    regenerate docs/manual/images/TODO.md from the live grep

Deliberately a script rather than sed: the flip has to refuse when a PNG is
missing, stay idempotent, and key on the exact file name instead of a regex
that could swallow a caption.
"""

from __future__ import annotations

import argparse
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MANUAL = REPO / "docs" / "manual"
IMAGES = MANUAL / "images"
STAGE = REPO / "tests" / "output" / "manual_figures"

# docs/manual/README.md: width under 1600 px. The byte ceiling is ours — 326
# figures land in plain git, so the cap is what keeps the repo sane.
MAX_WIDTH = 1600
MAX_BYTES = 400 * 1024

# \figtodo{name.png, caption}  /  \fig{name.png, caption}
FIG_RE = re.compile(r"\\(figtodo|fig)\{([^,}]+),([^}]*)\}")


def manual_pages() -> list[Path]:
    """Every built manual page.

    README.md is authoring notes and images/TODO.md is this script's own
    output — both mention \\figtodo and \\videotodo in prose, so counting them
    inflates the totals and makes each run disagree with the last.
    """
    skip = {(MANUAL / "README.md").resolve(), (IMAGES / "TODO.md").resolve()}
    return sorted(p for p in MANUAL.rglob("*.md") if p.resolve() not in skip)


def png_size(path: Path) -> tuple[int, int]:
    """(width, height) straight from the IHDR — no Pillow dependency."""
    with path.open("rb") as fh:
        head = fh.read(24)
    if len(head) < 24 or head[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path.name} is not a PNG")
    return struct.unpack(">II", head[16:24])


def scan() -> list[tuple[Path, int, str, str, str]]:
    """Every figure reference as (page, lineno, kind, filename, caption)."""
    out = []
    for page in manual_pages():
        for lineno, line in enumerate(page.read_text().splitlines(), 1):
            for m in FIG_RE.finditer(line):
                out.append((page, lineno, m.group(1), m.group(2).strip(),
                            m.group(3)))
    return out


# --------------------------------------------------------------------------
# audit
# --------------------------------------------------------------------------

def cmd_audit(_args: argparse.Namespace) -> int:
    refs = scan()
    problems: list[str] = []

    published = {name for _, _, kind, name, _ in refs if kind == "fig"}
    pending = {name for _, _, kind, name, _ in refs if kind == "figtodo"}

    # 1. every \fig has its PNG, within the size rules
    for page, lineno, kind, name, caption in refs:
        rel = page.relative_to(REPO)
        if kind != "fig":
            continue
        img = IMAGES / name
        if not img.exists():
            problems.append(f"{rel}:{lineno}: \\fig references missing {name}")
            continue
        try:
            width, _ = png_size(img)
        except ValueError as exc:
            problems.append(f"{rel}:{lineno}: {exc}")
            continue
        if width > MAX_WIDTH:
            problems.append(
                f"{rel}:{lineno}: {name} is {width}px wide (max {MAX_WIDTH})")
        size = img.stat().st_size
        if size > MAX_BYTES:
            problems.append(
                f"{rel}:{lineno}: {name} is {size // 1024} KB "
                f"(max {MAX_BYTES // 1024} KB)")

    # 2. a comma in the caption silently truncates it — the Doxygen alias
    #    takes exactly two arguments and splits on the first comma.
    for page, lineno, _kind, name, caption in refs:
        if "," in caption:
            problems.append(
                f"{page.relative_to(REPO)}:{lineno}: caption for {name} "
                f"contains a comma — use a semicolon or dash")

    # 3. no orphans: a PNG nothing references is dead weight in git
    known = published | pending
    for img in sorted(IMAGES.glob("*.png")):
        if img.name not in known:
            problems.append(f"docs/manual/images/{img.name}: referenced by no figure")

    # 4. manifest names must match live placeholders, or the manifest has
    #    drifted from the manual it serves
    manifest = MANUAL / "figures.json"
    if manifest.exists():
        import json
        data = json.loads(
            "\n".join(l for l in manifest.read_text().splitlines()))
        for row in data.get("figures", []):
            name = row.get("name")
            if name and name not in known:
                problems.append(
                    f"docs/manual/figures.json: '{name}' matches no "
                    f"\\figtodo or \\fig in the manual")

    print(f"figures published : {len(published)}")
    print(f"still placeholder : {len(pending)}")
    print(f"images on disk    : {len(list(IMAGES.glob('*.png')))}")
    if problems:
        print(f"\n{len(problems)} problem(s):", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1
    print("\naudit OK")
    return 0


# --------------------------------------------------------------------------
# flip
# --------------------------------------------------------------------------

def cmd_flip(args: argparse.Namespace) -> int:
    stage = Path(args.from_dir).resolve()
    refs = scan()
    pending = {name for _, _, kind, name, _ in refs if kind == "figtodo"}
    published = {name for _, _, kind, name, _ in refs if kind == "fig"}
    known = pending | published

    # A figure that is already \fig is not an error: re-capturing after a UI
    # change and republishing over it is the whole point of keeping a manifest.
    # Those rows are updated in place; only the \figtodo ones get flipped.
    if args.chapter:
        prefix = f"{args.chapter}_"
        names = sorted(n for n in known
                       if n.startswith(prefix) and (stage / n).exists())
        if not names:
            print(f"nothing staged in {stage} with prefix '{prefix}'",
                  file=sys.stderr)
            return 1
    else:
        names = args.names
        if not names:
            print("name nothing to flip? pass names or --chapter", file=sys.stderr)
            return 1

    # Verify the WHOLE batch before touching anything — a half-applied flip
    # leaves the manual referencing images that are not there.
    staged: dict[str, Path] = {}
    errors: list[str] = []
    for name in names:
        if name not in known:
            errors.append(f"{name}: no \\figtodo or \\fig in the manual "
                          f"references it")
            continue
        src = stage / name
        if not src.exists():
            errors.append(f"{name}: not staged in {stage}")
            continue
        try:
            width, _ = png_size(src)
        except ValueError as exc:
            errors.append(str(exc))
            continue
        if width > MAX_WIDTH:
            errors.append(f"{name}: {width}px wide (max {MAX_WIDTH})")
        if src.stat().st_size > MAX_BYTES:
            errors.append(
                f"{name}: {src.stat().st_size // 1024} KB (max {MAX_BYTES // 1024} KB)")
        staged[name] = src

    if errors:
        print(f"refusing to flip — {len(errors)} problem(s):", file=sys.stderr)
        for e in errors:
            print(f"  {e}", file=sys.stderr)
        return 1

    IMAGES.mkdir(parents=True, exist_ok=True)
    oxipng = shutil.which("oxipng")

    for name, src in staged.items():
        dst = IMAGES / name
        shutil.copy2(src, dst)
        if oxipng:
            subprocess.run([oxipng, "-o", "2", "-q", str(dst)], check=False)
        verb = "updated  " if name in published else "published"
        print(f"  {verb} {name} ({dst.stat().st_size // 1024} KB)")

    # Flip the placeholders. Anchored on the exact file name, so a caption can
    # never be matched by accident; all occurrences, since a figure may be
    # referenced from more than one page.
    flipped = 0
    for page in manual_pages():
        text = page.read_text()
        original = text
        for name in staged:
            text = text.replace(f"\\figtodo{{{name},", f"\\fig{{{name},")
        if text != original:
            page.write_text(text)
            flipped += sum(
                original.count(f"\\figtodo{{{n},") for n in staged)
            print(f"  flipped in {page.relative_to(REPO)}")

    print(f"\n{len(staged)} figure(s) published, {flipped} placeholder(s) flipped")
    if not oxipng:
        print("note: oxipng not on PATH — images copied unoptimised")
    return cmd_audit(args)


# --------------------------------------------------------------------------
# todo
# --------------------------------------------------------------------------

def cmd_todo(_args: argparse.Namespace) -> int:
    refs = scan()
    todo_by_page: dict[Path, list[tuple[int, str, str]]] = {}
    videos = 0
    figs = 0

    for page, lineno, kind, name, caption in refs:
        if kind == "figtodo":
            todo_by_page.setdefault(page, []).append((lineno, name, caption.strip()))
            figs += 1

    for page in manual_pages():
        for lineno, line in enumerate(page.read_text().splitlines(), 1):
            videos += line.count("\\videotodo")

    done = sum(1 for _, _, kind, _, _ in refs if kind == "fig")

    lines = [
        "# Figure and video placeholders",
        "",
        "Generated — do not edit by hand. Refresh with:",
        "",
        "    python3 scripts/manual_figures.py todo",
        "",
        "Capture a screenshot into `tests/output/manual_figures/`, review it, then",
        "publish it with `python3 scripts/manual_figures.py flip --chapter NN`",
        "(that copies the PNG into `docs/manual/images/` and turns `\\figtodo` into",
        "`\\fig`). Videos are swapped by hand: `\\videotodo{caption}` →",
        "`\\video{YOUTUBE_ID, caption}`.",
        "",
        f"Remaining: {figs} figures, {videos} videos. Published so far: {done}.",
        "",
    ]

    for page in sorted(todo_by_page):
        rel = page.relative_to(MANUAL)
        lines.append(f"## {rel}")
        lines.append("")
        for lineno, name, caption in todo_by_page[page]:
            lines.append(f"- [ ] `{name}` (line {lineno}) — {caption}")
        lines.append("")

    out = IMAGES / "TODO.md"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines))
    print(f"wrote {out.relative_to(REPO)}: {figs} figures, {videos} videos, "
          f"{done} published")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("audit", help="check published figures; non-zero on problems")

    f = sub.add_parser("flip", help="publish staged PNGs and flip placeholders")
    f.add_argument("names", nargs="*", help="figure file names")
    f.add_argument("--chapter", help="flip every staged figure with this prefix "
                                     "(e.g. 18, t01, a02)")
    f.add_argument("--from", dest="from_dir", default=str(STAGE),
                   help="staging directory (default tests/output/manual_figures)")

    sub.add_parser("todo", help="regenerate docs/manual/images/TODO.md")

    args = ap.parse_args()
    return {"audit": cmd_audit, "flip": cmd_flip, "todo": cmd_todo}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
