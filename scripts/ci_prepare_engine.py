#!/usr/bin/env python3
"""Apply the GUI's companion engine API patch before building its dependency."""

from pathlib import Path
import re
import subprocess
import sys


def prepare(engine: Path, patch: Path) -> None:
    header = engine / "include/openswmm/engine/openswmm_edit.h"
    code = re.sub(r"/\*.*?\*/|//[^\n]*", "", header.read_text(), flags=re.S)
    if re.search(r"\bSWMM_ENGINE_API\s+int\s+swmm_convert_types\s*\(", code):
        print("Engine already provides swmm_convert_types.")
        return
    # A conflicting patch must fail before modifying any source file.
    subprocess.run(["git", "-C", str(engine), "apply", "--check", str(patch)], check=True)
    subprocess.run(["git", "-C", str(engine), "apply", str(patch)], check=True)
    print("Applied the GUI bulk-conversion engine API patch.")


if __name__ == "__main__":
    prepare(Path(sys.argv[1]).resolve(),
            Path(__file__).resolve().parents[1] / "patches/openswmm-engine-bulk-conversion.patch")
