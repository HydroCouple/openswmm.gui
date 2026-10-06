from pathlib import Path
import subprocess,os,hashlib,json
repo=Path(__file__).resolve().parents[3];e=Path(__file__).resolve().parent
engine=repo.parent/'openswmm.engine';library=engine/'build/surface-r4-isolated/src/engine'
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',SWMMVIS_GUI_TEST_DATA=str(repo/'tests/gui/data'),SWMMVIS_R4_OUT=str(e/'qt'),QTEST_FUNCTION_TIMEOUT='30000',DYLD_LIBRARY_PATH=str(library),DYLD_PRINT_LIBRARIES='1')
for key,name in [('PROJ_DATA','proj'),('PROJ_LIB','proj'),('GDAL_DATA','gdal')]:env[key]=str(repo/'build/vcpkg_installed/arm64-osx/share'/name)
with (e/'qt-tests.log').open('w') as log:r=subprocess.run([str(repo/'build/lid-nodes/tests/gui/test_surfaceownershipdialog'),'-o',str(e/'qt-results.xml')+',junitxml','-o','-,txt'],stdout=log,stderr=subprocess.STDOUT,env=env,cwd=repo)
text=(e/'qt-tests.log').read_text();loaded=[s for s in text.splitlines() if 'libopenswmm.engine' in s and 'dyld' in s]
assert any(str(library) in s for s in loaded),loaded
paths=['tests/gui/test_surfaceownershipdialog.cpp','src/ui/dialogs/surfaceownershipdialog.cpp','include/ui/dialogs/surfaceownershipdialog.h','src/assignment/surfaceownership.cpp','include/assignment/surfaceownership.h']
audit={'engine_source_audit':json.loads((engine/'tests/verification/surface_r4_footprints_2026-10-06/source-audit.json').read_text()),'gui_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo).decode().strip(),'files':{p:hashlib.sha256((repo/p).read_bytes()).hexdigest() for p in paths if (repo/p).exists()},'loaded_library_lines':loaded,'exit_code':r.returncode,'offscreen_map_rendering':False}
(e/'qt-source-audit.json').write_text(json.dumps(audit,indent=2)+'\n');print('Qt exit:',r.returncode);print('\n'.join(text.splitlines()[-20:]));raise SystemExit(r.returncode)
