"""Compile real table controls from the built app and capture reproducible figures.

Outputs, disposable model and capture provenance stay in a reviewable batch.
Run after building SWMMVis: python3 tests/manual/manual_figures/capture_table_queries.py
"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import os
import shlex
import shutil
import subprocess

root = Path(__file__).resolve().parents[3]
build = root / 'build'
out = root / 'tests/output/manual_figures/table_queries_2026-10-04'
out.mkdir(parents=True, exist_ok=True)
source = Path(__file__).with_suffix('.cpp')

def commands(target):
    return subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()

def link(lines, target):
    for line in reversed(lines):
        for part in line.split(' && '):
            if ' -o ' + target + ' ' in part:
                return shlex.split(part)
    raise RuntimeError('Missing build command for ' + target)

app_link = link(commands('SWMMVis'), 'SWMMVis.app/Contents/MacOS/SWMMVis')
objects = [arg for arg in app_link if arg.endswith('.o') and not arg.endswith('/src/main.cpp.o')]
test_source = root / 'tests/gui/test_gisselectionsync.cpp'
compile_line = next(line for line in commands('test_gisselectionsync') if line.endswith(' -c ' + str(test_source)))
args = shlex.split(compile_line)
if 'ccache' in args[0]:
    args.pop(0)
clean = []
i = 0
while i < len(args):
    if args[i] in ['-o', '-MT', '-MF']:
        i += 2
    elif args[i] == '-MD':
        i += 1
    else:
        clean.append(str(source) if args[i] == str(test_source) else args[i])
        i += 1
binary = out / 'capture_table_queries'
obj = out / 'capture_table_queries.o'
test_link = link(commands('test_gisselectionsync'), 'tests/gui/test_gisselectionsync')
relink = []
i = 0
while i < len(test_link):
    if test_link[i].endswith('.o'):
        i += 1
    elif test_link[i] == '-o':
        relink.extend(['-o', str(binary)]); i += 2
    else:
        relink.append(test_link[i]); i += 1
relink[1:1] = [str(obj), *objects]
with (out / 'build.log').open('w') as log:
    subprocess.run([*clean, '-o', str(obj)], check=True, cwd=build, stdout=log, stderr=log)
    subprocess.run(relink, check=True, cwd=build, stdout=log, stderr=log)
fixture = out / 'fixtures/site'
shutil.copytree(root / 'examples/site_drainage', fixture, dirs_exist_ok=True)
model = fixture / 'site_drainage_model.inp'
env = dict(os.environ, QT_QPA_PLATFORM='offscreen',
           XDG_CONFIG_HOME=str(out / 'settings'),
           PROJ_DATA=str(build / 'vcpkg_installed/arm64-osx/share/proj'),
           GDAL_DATA=str(build / 'vcpkg_installed/arm64-osx/share/gdal'))
with (out / 'capture.log').open('w') as log:
    result = subprocess.run([str(binary), str(model), str(out)], cwd=out, env=env, stdout=log, stderr=log)
baseline = {'capturedAt':datetime.now(timezone.utc).isoformat(),
            'guiHead':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
            'source':str(source.relative_to(root)), 'platform':'Qt offscreen; production widgets; light theme; 2x rendering',
            'model':str(model.relative_to(root)), 'exitCode':result.returncode,
            'figures':{}, 'visualReview':'pending'}
for name in ['11_query_exclusion_sorted.png', '11_query_completion.png']:
    image = out / name
    if image.exists():
        baseline['figures'][name]={'sha256':hashlib.sha256(image.read_bytes()).hexdigest(), 'bytes':image.stat().st_size}
(out / 'capture-baseline.json').write_text(json.dumps(baseline,indent=2)+'\n')
print((out / 'capture.log').read_text())
if result.returncode:
    raise SystemExit(result.returncode)
print('Figures and provenance: ' + str(out))
