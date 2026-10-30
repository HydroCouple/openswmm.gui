#!/usr/bin/env python3
"""Audit Mach-O dependencies without relying on the developer's installed libraries.

System libraries may live in Apple's shared cache. All other load commands must
resolve inside this bundle through loader/executable paths or declared rpaths.
This complements codesign; a valid signature alone does not imply portability.
"""
import argparse
import json
from pathlib import Path
import subprocess

MACH_MAGIC = {b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
              b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca',
              b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca'}
LOAD_COMMANDS = {'LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB', 'LC_LOAD_UPWARD_DYLIB'}


def commands(binary):
    text = subprocess.check_output(['otool', '-arch', 'arm64', '-l', str(binary)], text=True)
    loads, rpaths, command = [], [], ''
    for line in text.splitlines():
        line = line.strip()
        if line.startswith('cmd '):
            command = line[4:]
        elif command in LOAD_COMMANDS and line.startswith('name '):
            loads.append(line[5:].rsplit(' (offset ', 1)[0])
        elif command == 'LC_RPATH' and line.startswith('path '):
            rpaths.append(line[5:].rsplit(' (offset ', 1)[0])
    return loads, rpaths


def audit(bundle):
    bundle = bundle.resolve()
    executable = bundle / 'Contents/MacOS/SWMMVis'
    _, app_rpaths = commands(executable)
    failures, binaries = [], []

    def expand(path, loader):
        return Path(path.replace('@loader_path', str(loader.parent))
                    .replace('@executable_path', str(executable.parent)))

    for binary in sorted(bundle.rglob('*')):
        if not binary.is_file() or binary.is_symlink():
            continue
        with binary.open('rb') as stream:
            if stream.read(4) not in MACH_MAGIC:
                continue
        try:
            loads, rpaths = commands(binary)
        except subprocess.CalledProcessError as error:
            failures.append({'binary': str(binary.relative_to(bundle)), 'error': str(error)})
            continue
        binaries.append(str(binary.relative_to(bundle)))
        roots = [expand(p, binary) for p in rpaths] + [expand(p, executable) for p in app_rpaths]
        for dependency in loads:
            if dependency.startswith(('/usr/lib/', '/System/Library/')):
                continue
            if dependency.startswith('@rpath/'):
                candidates = [p / dependency[len('@rpath/'):] for p in roots]
            else:
                candidates = [expand(dependency, binary)]
            if not any(p.is_file() and p.resolve().is_relative_to(bundle) for p in candidates):
                failures.append({'binary': str(binary.relative_to(bundle)), 'dependency': dependency})
    drivers = bundle / 'Contents/PlugIns/sqldrivers'
    if not (drivers / 'libqsqlite.dylib').is_file():
        failures.append({'error': 'Required SQLite driver is missing'})
    for name in ('qsqlmimer', 'qsqlodbc', 'qsqlpsql'):
        if (drivers / f'lib{name}.dylib').exists():
            failures.append({'error': f'Unsupported Qt driver remains: {name}'})
    return {'bundle': str(bundle), 'architecture': 'arm64', 'binaries_checked': len(binaries),
            'passed': not failures, 'failures': failures, 'binaries': binaries}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.bundle)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"Checked {result['binaries_checked']} binaries; {len(result['failures'])} failures")
    raise SystemExit(0 if result['passed'] else 1)
