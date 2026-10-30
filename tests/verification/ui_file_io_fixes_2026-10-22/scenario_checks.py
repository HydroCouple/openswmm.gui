"""Run real models after each format round trip and check their saved results."""
from probe import *
import hashlib
import shutil
import sqlite3

run = bind('swmm_engine_run', [S,S,S,S])
gage_count = bind('swmm_gage_count', [P])
species_count = bind('swmm_reaction_species_count', [P])
results = {}
formats=[('inp',None),('gpkg','org.hydrocouple.openswmm.plugins.geopackage'),
         ('h5','org.hydrocouple.openswmm.plugins.hdf5')]
cases={
    'snow': ENGINE/'tests/unit/engine/data/precip_scaling/snow_scaled.inp',
    'groundwater': GUI/'tests/gui/data/gw_exchange_fixture.inp',
    'mesh': GUI/'tests/gui/data/mini_2d.inp',
}
for name, source in cases.items():
    folder=ROOT/('scenario_'+name);folder.mkdir(exist_ok=True)
    e=load(source)
    if gage_count(e)>0:
        check(bind('swmm_gage_set_scale_factor',[P,I,D])(e,0,10),e)
    outputs={}
    for extension,plugin in formats:
        path=folder/('model.'+extension)
        if path.exists():path.unlink()
        check(write(e,enc(path),enc(plugin) if plugin else None),e)
        out=folder/(extension+'.out');report=folder/(extension+'.rpt')
        rc=run(enc(path),enc(report),enc(out),enc(plugin) if plugin else None)
        assert rc==0,(name,extension,rc,report.read_text()[-3000:])
        outputs[extension]={'rc':rc,'size':out.stat().st_size,
                            'sha256':hashlib.sha256(out.read_bytes()).hexdigest()}
    release(e)
    expected=outputs['inp']['sha256']
    for extension,output in outputs.items():
        output['matches_inp']=output['sha256']==expected
    results[name]=outputs

# A process configuration must remain usable after its original file is gone.
source=ENGINE/'tests/unit/engine/data/component_metadata/reactions/model.inp'
folder=ROOT/'component_source';folder.mkdir(exist_ok=True)
for path in source.parent.iterdir():
    if path.is_file() and path.suffix in ('.inp','.rxn'):
        shutil.copy2(path,folder/path.name)
e=load(folder/source.name)
expected=species_count(e)
assert expected>0
archive=ROOT/'component_exports';archive.mkdir(exist_ok=True)
for extension,plugin in formats[1:]:
    path=archive/('model.'+extension)
    if path.exists():path.unlink()
    check(write(e,enc(path),enc(plugin)),e)
release(e)
for path in folder.glob('*.rxn'):path.unlink()
results['components']={}
for extension,plugin in formats[1:]:
    path=archive/('model.'+extension)
    e=load(path,plugin)
    actual=species_count(e)
    assert actual==expected,(extension,expected,actual)
    results['components'][extension]={'species_before':expected,'species_after':actual}
    release(e)

unit_root=ENGINE/'tests/unit/engine/data/ui_file_io_roundtrip'
results['writer_outputs']={}
for name in ['single_container','report_only']:
    path=unit_root/(name+'.gpkg')
    with sqlite3.connect(path) as db:
        rows=db.execute('SELECT simulation_id,status FROM simulations').fetchall()
        n=db.execute('SELECT count(*) FROM result_timeseries').fetchone()[0]
        summary=db.execute('SELECT count(*) FROM result_summary').fetchone()[0]
    assert rows and rows[0][1]=='completed',(name,rows)
    assert summary>0,(name,'missing report summaries')
    if name=='single_container':assert n>0
    results['writer_outputs'][name]={'runs':rows,'result_rows':n,'summary_rows':summary}
# An actual SQLite failure must escape report(), with its diagnostic intact.
folder=ROOT/'report_failure';folder.mkdir(exist_ok=True)
target=folder/'failed.gpkg'
if target.exists():target.unlink()
e=load(unit_root/'report_only.inp')
check(setext(e,b'IO_REPORT_PATH',enc(target.resolve())),e)
source=folder/'failure.inp'
check(write(e,enc(source),None),e);release(e)
e=load(source)
check(bind('swmm_engine_initialize',[P])(e),e)
check(bind('swmm_engine_start',[P,I])(e,1),e)
elapsed=D()
while True:
    check(bind('swmm_engine_step',[P,C.POINTER(D)])(e,C.byref(elapsed)),e)
    if elapsed.value<=0:break
check(bind('swmm_engine_end',[P])(e),e)
with sqlite3.connect(target) as db:
    db.execute("CREATE TRIGGER reject_summary BEFORE INSERT ON result_summary "
               "BEGIN SELECT RAISE(ABORT, 'report write failure regression'); END")
rc=bind('swmm_engine_report',[P])(e)
message=last_error(e).decode()
assert rc!=0 and 'report write failure regression' in message,(rc,message)
results['report_failure']={'returned_error':rc,'diagnostic':message}
release(e)

(ROOT/'scenario_results.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
assert all(v['matches_inp'] for case in ['snow','groundwater','mesh'] for v in results[case].values())
