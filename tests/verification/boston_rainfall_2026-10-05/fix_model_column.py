"""Correct only RG_8510's missing TSF column, keeping a complete backup."""
import hashlib
import json
import os
import shutil
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MODEL = Path('/Volumes/BaseI/Boston/BWSC_Combined_Model_July 2026_New GAAR_T Storm 2070 - v1/BWSC_Combined_Model_July 2026_New GAAR_T Storm 2070.inp')
backup = ROOT / 'model_before_column_fix.inp'
if backup.exists():
    raise RuntimeError('Backup already exists; refusing to overwrite it')
shutil.copy2(MODEL, backup)
original_stat = MODEL.stat()
old = b'"Precipication/1kmPixels_2023-12-18_PCSWMM_15min.tsf"'
new = b'"Precipication/1kmPixels_2023-12-18_PCSWMM_15min.tsf:8510"'
section = b''
count = 0
before = hashlib.sha256()
after = hashlib.sha256()
fd, name = tempfile.mkstemp(prefix='.rain-column-fix-', dir=MODEL.parent)
try:
    with os.fdopen(fd, 'wb') as out, backup.open('rb') as src:
        for line in src:
            before.update(line)
            stripped = line.strip()
            if stripped.startswith(b'['):
                section = stripped.upper()
            if section == b'[RAINGAGES]' and stripped.split()[:1] == [b'RG_8510']:
                if line.count(old) != 1:
                    raise RuntimeError('RG_8510 no longer has the expected missing column')
                line = line.replace(old, new)
                count += 1
            out.write(line)
            after.update(line)
        out.flush()
        os.fsync(out.fileno())
    assert count == 1, count
    assert MODEL.stat().st_mtime_ns == original_stat.st_mtime_ns
    os.chmod(name, original_stat.st_mode)
    os.replace(name, MODEL)
    result = {'model':str(MODEL),'backup':str(backup),'changed_gage':'RG_8510',
              'column':'8510','before_sha256':before.hexdigest(),'after_sha256':after.hexdigest()}
    (ROOT / 'model_column_fix.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
finally:
    if Path(name).exists():
        Path(name).unlink()
