#!/usr/bin/env python3
"""Package the curated Rubinato inputs for a standalone documentation download.

The app bundles the same source directory through CMake. This archive has a
fixed timestamp and an explicit input-only payload; run outputs are excluded.
"""
import argparse
from pathlib import Path
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'examples/rubinato2017'
PAYLOAD = (
    'README.md', 'example.json', 'model.inp', 'steady_s1.inp', 'steady_s2.inp',
    'steady_s3.inp', 'steady_s4.inp', 'table1_unsteady_tests.csv',
    'table2_steady_exchange.csv', 'table3_coefficients.csv', 'rig_geometry.yaml',
    'steady_comparison.csv',
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path,
                        default=ROOT/'docs/manual/tutorials/models/rubinato2017_examples.zip')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    content = {'rubinato2017/'+name: (SOURCE/name).read_bytes() for name in sorted(PAYLOAD)}
    if args.check:
        with ZipFile(args.output) as archive:
            if sorted(archive.namelist()) != sorted(content):
                raise SystemExit('Archive payload differs from curated file list')
            for name, data in content.items():
                if archive.read(name) != data:
                    raise SystemExit('Archive has stale content: '+name)
        print('Verified 5 models and 7 supporting files:', args.output)
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(args.output, 'w', compression=ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in content.items():
            info = ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    print(args.output)


if __name__ == '__main__':
    main()
