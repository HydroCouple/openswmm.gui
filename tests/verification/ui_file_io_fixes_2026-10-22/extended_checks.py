"""Compare authored native sections through every model container."""
from probe import *
import re
FORMATS=[('inp',None),('gpkg','org.hydrocouple.openswmm.plugins.geopackage'),
         ('h5','org.hydrocouple.openswmm.plugins.hdf5')]
CASES={
 'surface_quality':ENGINE/'tests/unit/engine/data/_s7_rt_saved.inp',
 'mesh_heat':ENGINE/'tests/output/mesh_heat/_channel-ground-roundtrip.inp',
 'radiation_snow':ENGINE/'tests/unit/engine/data/shared_radiation_snow_roundtrip.inp',
 'lid':ENGINE/'tests/unit/engine/data/lid_chain_mass_balance.inp',
}
def sections(path):
 out={};name=''
 for raw in path.read_text().splitlines():
  line=raw.strip()
  if line.startswith('['):name=line[1:line.index(']')];continue
  if not line or line.startswith(';'):continue
  if name.startswith(('2D_', 'HEAT_', 'UEB_', 'SNOW', 'REACTION_', 'AQUIFER', 'GROUNDWATER')) or name in ['TEMPERATURE','LID_CONTROLS','LID_USAGE','INITIAL_QUALITY']:
   out.setdefault(name,[]).append(re.sub(r'\s+',' ',line))
 return out
results={}
for name,source in CASES.items():
 folder=ROOT/('extended_'+name);folder.mkdir(exist_ok=True)
 e=load(source);baseline=folder/'baseline.inp'
 check(write(e,enc(baseline),None),e);expected=sections(baseline)
 assert expected,(name,'no tested sections')
 row={}
 for extension,plugin in FORMATS:
  target=folder/('saved.'+extension)
  if target.exists():target.unlink()
  check(write(e,enc(target),enc(plugin) if plugin else None),e)
  restored=load(target,plugin);decoded=folder/(extension+'.decoded.inp')
  check(write(restored,enc(decoded),None),restored);release(restored)
  actual=sections(decoded)
  diff={key:{'expected':expected.get(key),'actual':actual.get(key)} for key in expected.keys()|actual.keys() if expected.get(key)!=actual.get(key)}
  row[extension]={'section_count':len(expected),'differences':diff}
 release(e);results[name]=row
(ROOT/'extended_results.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
assert all(not v['differences'] for row in results.values() for v in row.values())
