"""Reuse the just-deployed runtime after relinking the final rendering fix."""
from pathlib import Path
import runpy
import subprocess
root=Path(__file__).resolve().parents[3]
bundle=root/'build/animation-verification/vfr-flow-testing/SWMMVis.app'
exe=bundle/'Contents/MacOS/SWMMVis'
commands=runpy.run_path(str(root/'scripts/verify_macos_bundle.py'))['commands']
loads,rpaths=commands(exe)
for load in loads:
    if not load.startswith(('@rpath/','/usr/lib/','/System/Library/')):
        raise SystemExit('Review unexpected dependency before launch: '+load)
frameworks='@executable_path/../Frameworks'
for path in rpaths:
    if path!=frameworks:
        subprocess.run(['install_name_tool','-delete_rpath',path,str(exe)],check=True)
if frameworks not in rpaths:
    subprocess.run(['install_name_tool','-add_rpath',frameworks,str(exe)],check=True)
subprocess.run(['codesign','--force','--deep','--sign','-',str(bundle)],check=True)
subprocess.run(['codesign','--verify','--deep','--strict',str(bundle)],check=True)
print('Final executable uses bundled frameworks; strict/deep signature verified.')
