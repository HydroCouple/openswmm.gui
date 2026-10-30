"""Focused persistence probes; no simulation and no user files changed."""
from probe import *
import subprocess

GP = 'org.hydrocouple.openswmm.plugins.geopackage'
results = {}

def getdouble(e, name, idx=0):
    v = D()
    check(bind(name, [P, I, C.POINTER(D)])(e, idx, C.byref(v)), e)
    return v.value

def roundtrip(name, text, prepare, snapshot):
    src = ROOT / (name + '.inp')
    src.write_text(text)
    e = load(src)
    prepare(e)
    row = {'before': snapshot(e), 'formats': {}}
    for ext, plugin in [('inp', None), ('gpkg', GP)]:
        dest = ROOT / (name + '.saved.' + ext)
        if dest.exists(): dest.unlink()
        check(write(e, enc(dest), enc(plugin) if plugin else None), e)
        e2 = load(dest, plugin)
        row['formats'][ext] = snapshot(e2)
        release(e2)
    release(e)
    results[name] = row

snowtext = (ENGINE / 'tests/unit/engine/data/precip_scaling/snow_scaled.inp').read_text()
def snowprepare(e):
    check(bind('swmm_gage_set_scale_factor', [P,I,D])(e,0,10),e)
    check(bind('swmm_snow_set_step', [P,D])(e,73),e)
def snowsnapshot(e):
    v = D()
    check(bind('swmm_snow_get_step', [P,C.POINTER(D)])(e,C.byref(v)),e)
    return {
        'gage_scale': getdouble(e,'swmm_gage_get_scale_factor'),
        'subcatch_rain_scale': getdouble(e,'swmm_subcatch_get_rain_scale_factor'),
        'subcatch_snow_scale': getdouble(e,'swmm_subcatch_get_snow_scale_factor'),
        'snowpack': bind('swmm_subcatch_get_snowpack',[P,I],S)(e,0),
        'snow_step': v.value,
    }
roundtrip('snow',snowtext,snowprepare,snowsnapshot)
roundtrip('groundwater',(GUI/'tests/gui/data/gw_exchange_fixture.inp').read_text(),lambda e:None,
          lambda e:{'aquifer_count':bind('swmm_aquifer_count',[P])(e)})

base = (GUI/'tests/gui/data/mini_2d.inp').read_text()
cop = '\n[POLLUTANTS]\nP1 MG/L 0 0 0 0 NO P2 0.5 0 0\nP2 MG/L 0 0 0 0 NO * 0 0 0\n'
def copsnapshot(e):
    pairs=[]
    for n in range(2):
        i,d=I(),D()
        check(bind('swmm_pollutant_get_co_pollutant',[P,I,C.POINTER(I),C.POINTER(D)])(e,n,C.byref(i),C.byref(d)),e)
        pairs.append([i.value,d.value])
    return {'co_pollutants':pairs}
roundtrip('copollutant',base+cop,lambda e:None,copsnapshot)

weir='\n[WEIRS]\nW1 J1 O1 TRANSVERSE 1.2 3.33 NO 0 0\n[XSECTIONS]\nW1 RECT_OPEN 1 2 0 0\n'
def weirprepare(e):
    check(bind('swmm_link_set_offset_up',[P,I,D])(e,1,0.7),e)
    check(bind('swmm_link_set_offset_dn',[P,I,D])(e,1,0.8),e)
roundtrip('weir',base+weir,weirprepare,
          lambda e:{'offset_up':getdouble(e,'swmm_link_get_offset_up',1),'offset_down':getdouble(e,'swmm_link_get_offset_dn',1)})

outlet='\n[OUTLETS]\nD1 J1 O1 1.2 FUNCTIONAL/HEAD 2 0.5 NO\n'
roundtrip('outlet',base+outlet,weirprepare,
          lambda e:{'offset_up':getdouble(e,'swmm_link_get_offset_up',1),'offset_down':getdouble(e,'swmm_link_get_offset_dn',1)})

no_mesh=base.split('[2D_OPTIONS]')[0]+'\n[2D_OPTIONS]\nMAX_TIMESTEP 1.2345\n'
roundtrip('no_mesh',no_mesh,lambda e:None,lambda e:{'MAX_TIMESTEP':readopt(e,'MAX_TIMESTEP',True)})

# Isolate the native crash from the rest of the probes and capture its exit status.
child = "from probe import *; e=load(ROOT/'saved.gpkg','"+GP+"'); v=D(); bind('swmm_pollutant_get_mwt',[P,I,C.POINTER(D)])(e,0,C.byref(v)); print(v.value)"
crash = subprocess.run([sys.executable,'-X','faulthandler','-c',child],cwd=ROOT,capture_output=True,text=True)
results['gpkg_mwt_getter']={'returncode':crash.returncode,'stdout':crash.stdout,'stderr':crash.stderr}
(ROOT/'additional_probe_results.json').write_text(json.dumps(results,indent=2,default=lambda b:b.decode())+'\n')
print(json.dumps(results,indent=2,default=lambda b:b.decode()))
