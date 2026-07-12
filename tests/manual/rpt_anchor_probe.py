#!/usr/bin/env python3
"""Replicates RptParser::parse + StatusReportDialog anchor math to prove where
the Report Viewer loses/misplaces content.

Usage: python3 tests/manual/rpt_anchor_probe.py <file.rpt> [...]
"""
import re
import sys

STARS = re.compile(r"^(\s*)(\*{3,})")
COLGAP = re.compile(r"\s{2,}")
MAX_TITLE_LINES = 8


def title_text(line, indent, length):
    raw = line[indent:].strip()
    if not raw:
        return ""
    for m in COLGAP.finditer(raw):
        if m.end() >= length:
            return raw[: m.start()].strip()
    return raw


def parse(lines):
    sections = []
    cur = {"title": "", "body": ""}

    def flush():
        nonlocal cur
        if cur["title"] or cur["body"].strip():
            sections.append(cur)
        cur = {"title": "", "body": ""}

    i = 0
    while i < len(lines):
        m = STARS.match(lines[i])
        if m:
            indent, length = len(m.group(1)), len(m.group(2))
            j = i + 1
            tl = []
            while j < len(lines) and (j - i) <= MAX_TITLE_LINES and not STARS.match(lines[j]):
                tl.append(title_text(lines[j], indent, length))
                j += 1
            title = " ".join(" ".join(tl).split())
            if j < len(lines) and (j - i) <= MAX_TITLE_LINES and STARS.match(lines[j]) and title:
                flush()
                cur["title"] = title
                for k in range(i, j + 1):
                    cur["body"] += lines[k] + "\n"
                i = j + 1
                continue
        cur["body"] += lines[i] + "\n"
        i += 1
    flush()
    return sections


def probe(path):
    raw = open(path, encoding="utf-8", errors="replace").read()
    raw = raw.replace("\r\n", "\n").replace("\r", "\n")
    if raw and not raw.endswith("\n"):
        raw += "\n"
    lines = raw.split("\n")[:-1]  # QTextStream::readLine() drops the trailing empty
    secs = parse(lines)

    print(f"\n=== {path}")
    print(f"raw chars      : {len(raw)}")
    print(f"sum(body chars): {sum(len(s['body']) for s in secs)}")
    print(f"sections       : {len(secs)}")
    print(f"DROPPED CHARS  : {len(raw) - sum(len(s['body']) for s in secs)}")

    # Mirrors StatusReportDialog::populateText() bookmark construction.
    NOTICE = re.compile(r"^[ \t]*(?:WARNING|ERROR)\b", re.M)
    bookmarks = []  # (label, anchor)
    anchor = 0
    for idx, s in enumerate(secs):
        start = anchor
        anchor += len(s["body"])
        if s["title"]:
            bookmarks.append((s["title"], start))
            continue
        if idx != 0:
            bookmarks.append(("(untitled)", start))
            continue
        bookmarks.append(("Report Header & Notes", start))
        hits = list(NOTICE.finditer(s["body"]))
        if hits:
            bookmarks.append(
                (f"Warnings & Errors ({len(hits)})", start + hits[0].start())
            )

    bad = 0
    for i, (label, pos) in enumerate(bookmarks):
        lands = raw[pos : pos + 70].split("\n")[0]
        # A bookmark must land at the start of a line.
        line_start = pos == 0 or raw[pos - 1] == "\n"
        if not line_start:
            bad += 1
        print(f"  [{i:2}] anchor={pos:8} {'OK ' if line_start else 'MID-LINE'} "
              f"{label[:36]:38} -> {lands.strip()[:44]!r}")
    print(f"bookmarks: {len(bookmarks)}  bad anchors: {bad}")


for p in sys.argv[1:]:
    probe(p)
