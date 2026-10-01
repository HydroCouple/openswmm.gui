#!/usr/bin/env python3
"""Copy portable manual examples to a fresh reviewable directory; optionally run them."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
MODEL_DIR = 'docs/manual/tutorials/models/'
CASES = {
    'site': ['examples/site_drainage/site_drainage_model.inp'],
    'street': [MODEL_DIR + 'street_inlet_junction.inp'],
    'twod': [MODEL_DIR + '2d_complete_example.inp'],
    'transport': [MODEL_DIR + 'transport_demo.' + ext for ext in ('inp', 'ard', 'rxn', 'age', 'heat')],
    'fv': ['examples/swashes_bump_shock/1d_fv.inp'],
}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='New directory; existing files are never overwritten')
    parser.add_argument('--case', choices=CASES, action='append', help='Repeat to select cases; default all examples')
    parser.add_argument('--engine', type=Path, help='Explicit OpenSWMM CLI executable; omit to prepare inputs only')
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error('Choose a new output directory so earlier runs remain reviewable')
    engine = args.engine.resolve() if args.engine else None
    if engine and not engine.is_file():
        parser.error(f'Engine not found: {engine}')
    cases = args.case or list(CASES)
    for case in cases:
        for filename in CASES[case]:
            if not (ROOT / filename).is_file():
                parser.error(f'Missing source: {filename}')
    output.mkdir(parents=True)
    report = {'preparedAt': datetime.now(timezone.utc).isoformat(),
              'engine': {'path': str(engine), 'sha256': sha(engine)} if engine else None, 'cases': {}}
    failed = False
    for case in cases:
        dest = output / case
        dest.mkdir()
        record = {'sources': [], 'status': 'prepared'}
        report['cases'][case] = record
        for filename in CASES[case]:
            source = ROOT / filename
            shutil.copy2(source, dest / source.name)
            record['sources'].append({'path': filename, 'sha256': sha(source)})
        model = dest / Path(CASES[case][0]).name
        if engine:
            with (dest / 'engine.log').open('w') as log:
                try:
                    result = subprocess.run([str(engine), model.name, model.with_suffix('.rpt').name,
                                             model.with_suffix('.out').name], cwd=dest,
                                            stdout=log, stderr=subprocess.STDOUT, timeout=300)
                    code = result.returncode
                except subprocess.TimeoutExpired:
                    code = 124
            record.update(exitCode=code, status='executed' if code == 0 else 'failed')
            failed |= code != 0
            if code == 0:
                session = {'inpPath': model.name, 'engineVersion': '6.0.0',
                           'resultLayers': [model.with_suffix('.out').name],
                           'resultLayerReports': {model.with_suffix('.out').name: model.with_suffix('.rpt').name}}
                model.with_suffix('.oswp').write_text(json.dumps({'schemaVersion': 5, 'sessions': [session]}, indent=2)+'\n')
            record['outputs'] = [{'file': f.name, 'bytes': f.stat().st_size, 'sha256': sha(f)}
                                 for f in sorted(dest.iterdir()) if f.suffix in ('.out', '.rpt', '.h5')]
        (output / 'fixtures.json').write_text(json.dumps(report, indent=2)+'\n')
        print(f'{case}: {record["status"]} — {dest}')
    print('Inspect the report warnings and continuity; a successful process exit is not scientific validation.')
    return int(failed)

if __name__ == '__main__':
    raise SystemExit(main())
