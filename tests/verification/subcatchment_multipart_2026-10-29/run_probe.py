"""Reuse the built application objects for a focused end-to-end regression probe."""
from pathlib import Path
import json
import os
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
build = root / 'build'
source = Path(__file__).with_name('probe.cpp')
output = root / 'tests/output/subcatchment_multipart'
output.mkdir(parents=True, exist_ok=True)
entries = json.loads((build / 'compile_commands.json').read_text())
entry = next(e for e in entries if e['file'] == str(root / 'src/main.cpp'))
compile_args = shlex.split(entry['command'])
if Path(compile_args[0]).name == 'ccache':
    compile_args.pop(0)
compile_args[compile_args.index('-o') + 1] = str(output / 'probe.o')
compile_args[compile_args.index('-c') + 1] = str(source)
subprocess.run(compile_args, cwd=build, check=True)
commands = subprocess.check_output(['ninja', '-t', 'commands', 'SWMMVis'], cwd=build, text=True)
link = next(line for line in commands.splitlines() if '-o SWMMVis.app/Contents/MacOS/SWMMVis ' in line)
args = shlex.split(link)
start = next(i for i, arg in enumerate(args) if arg.endswith('/c++') or arg.endswith('/clang++'))
args = args[start:]
if '&&' in args:
    args = args[:args.index('&&')]
args[args.index('-o') + 1] = str(output / 'probe')
args.insert(1, '-Wl,-dead_strip')
args = [str(output / 'probe.o') if a == 'CMakeFiles/SWMMVis.dir/src/main.cpp.o' else a for a in args]
subprocess.run(args, cwd=build, check=True)
env = os.environ.copy()
env['QT_QPA_PLATFORM'] = 'cocoa' if '--native' in sys.argv else 'offscreen'
env['PROJ_DATA'] = str(build / 'vcpkg_installed/arm64-osx/share/proj')
env['GDAL_DATA'] = str(build / 'vcpkg_installed/arm64-osx/share/gdal')
log_path = output / ('probe-native.log' if '--native' in sys.argv else 'probe.log')
with log_path.open('w') as log:
    result = subprocess.run([str(output / 'probe')], cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT)
print(log_path.read_text())
raise SystemExit(result.returncode)
