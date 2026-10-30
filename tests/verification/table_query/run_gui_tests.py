"""Compile targeted Qt tests against the verified application's object files.

Avoid rebuilding the same several hundred application sources for each test.
Run after `cmake --build build --target SWMMVis test_queryparser`.
All generated files and logs are retained beside this script.
"""
from pathlib import Path
import os
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
build = root / 'build'
out = Path(__file__).resolve().parent

def commands(target):
    return subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()

def link_args(lines, target):
    for line in reversed(lines):
        for part in line.split(' && '):
            if ' -o ' + target + ' ' in part:
                return shlex.split(part)
    raise RuntimeError('Cannot find link command for ' + target)

app_link = link_args(commands('SWMMVis'), 'SWMMVis.app/Contents/MacOS/SWMMVis')
app_objects = [arg for arg in app_link if arg.endswith('.o') and not arg.endswith('/src/main.cpp.o')]
assert app_objects and not any('/main.cpp.o' in arg for arg in app_objects)

full_test_link = link_args(commands('test_gisselectionsync'), 'tests/gui/test_gisselectionsync')

for target in ['test_gisselectionsync', 'test_attributetableschema', 'test_meshattributetable',
               'test_dataobjectattributetable', 'test_dialog_layout_persistence',
               'test_pattern_editor_dialog', 'test_curve_editor_dialog', 'test_timeseries_editor_dialog',
               'test_transect_editor_dialog', 'test_rainfallvisualizationdialog', 'test_groundwaterassigndialog']:
    requested = {arg for arg in sys.argv[1:] if not arg.startswith("--")}
    if requested and target not in requested:
        continue
    lines = commands(target)
    source = root / 'tests/gui' / (target + '.cpp')
    compile_line = next(line for line in lines if line.endswith(' -c ' + str(source)))
    args = shlex.split(compile_line)
    if 'ccache' in args[0]:
        args.pop(0)
    cleaned = []
    i = 0
    while i < len(args):
        if args[i] in ['-o', '-MT', '-MF']:
            i += 2
        elif args[i] in ['-MD']:
            i += 1
        else:
            cleaned.append(args[i]); i += 1
    includes = [arg for arg in cleaned if arg.startswith('-I') or arg.startswith('-D')]
    framework = next(arg for arg in cleaned if arg.endswith('/macos/lib') and cleaned[cleaned.index(arg)-1] == '-iframework')
    moc = Path(framework).parent / 'libexec/moc'
    obj = out / (target + '.o')
    binary = out / target
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen',
               SWMMVIS_GUI_TEST_DATA=str(root / 'tests/gui/data'),
               PROJ_DATA=str(build / 'vcpkg_installed/arm64-osx/share/proj'),
               GDAL_DATA=str(build / 'vcpkg_installed/arm64-osx/share/gdal'),
               SWMMVIS_FORCING_TEST_OUTPUT=str(out / 'groundwater_fixtures'))
    with (out / (target + '.build.log')).open('w') as log:
        subprocess.run([str(moc), *includes, str(source), '-o', str(out / (target + '.moc'))], check=True, cwd=build, stdout=log, stderr=log)
        subprocess.run([cleaned[0], '-I' + str(out), *cleaned[1:], '-o', str(obj)], check=True, cwd=build, stdout=log, stderr=log)
        if "--compile-only" in sys.argv:
            print(target + ": compiled", flush=True)
            continue
        link = full_test_link
        relink = []
        i = 0
        while i < len(link):
            if link[i].endswith('.o'):
                i += 1
            elif link[i] == '-o':
                relink.extend(['-o', str(binary)]); i += 2
            else:
                relink.append(link[i]); i += 1
        relink[1:1] = [str(obj), *app_objects]
        subprocess.run(relink, check=True, cwd=build, stdout=log, stderr=log)
    with (out / (target + '.tests.log')).open('w') as log:
        result = subprocess.run([str(binary)], cwd=out, env=env, stdout=log, stderr=log)
    print(target + ': ' + ('PASS' if result.returncode == 0 else 'FAIL'), flush=True)
    if result.returncode:
        print((out / (target + '.tests.log')).read_text()[-5000:], flush=True)
        raise SystemExit(result.returncode)
