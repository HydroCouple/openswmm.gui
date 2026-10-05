"""Validate T10 with a supplied current native engine library; retain reproducible evidence.
Example: python run_examples.py --library /absolute/path/libopenswmm.engine.dylib
Sampled CSVs at 30 seconds; reported continuity and component volumes use engine totals.
"""
import argparse, ctypes as c, csv, hashlib, json, math, re
from pathlib import Path
ROOT = Path(__file__).resolve().parents[4]
DECKS = ROOT / 'docs/manual/tutorials/models/lid_active_chain'
OUT = Path(__file__).resolve().parents[1] / 'results'
p = argparse.ArgumentParser()
p.add_argument('--library', type=Path, required=True)
p.add_argument('--steps', nargs='+', type=float, default=[.5, .25])
p.add_argument('--cases', nargs='+')
p.add_argument('--output', type=Path, default=OUT)
p.add_argument('--append', action='store_true', help='Merge these runs into a same-library summary.')
a = p.parse_args(); OUT=a.output; OUT.mkdir(parents=True,exist_ok=True)
lib = c.CDLL(str(a.library.resolve())); H=c.c_void_p; D=c.POINTER(c.c_double)
def fn(name, args, result=c.c_int):
    f=getattr(lib,name); f.argtypes=args; f.restype=result; return f
create=fn('swmm_engine_create',[],H)
open_=fn('swmm_engine_open',[H,c.c_char_p,c.c_char_p,c.c_char_p,c.c_char_p])
init=fn('swmm_engine_initialize',[H]); start=fn('swmm_engine_start',[H,c.c_int])
stride=fn('swmm_engine_stride',[H,c.c_int,D])
end=fn('swmm_engine_end',[H]); report=fn('swmm_engine_report',[H]); close=fn('swmm_engine_close',[H]); destroy=fn('swmm_engine_destroy',[H],None)
errcount=fn('swmm_get_error_count',[H]); err=fn('swmm_get_error_at',[H,c.c_int],c.c_char_p)
wc=fn('swmm_get_warning_count',[H]); warn=fn('swmm_get_warning_at',[H,c.c_int],c.c_char_p)
getters={n:fn('swmm_'+n,[H,c.c_int,D]) for n in ['node_get_head','node_get_volume','link_get_flow','link_get_target_setting','get_routing_total','link_get_stat_vol_flow']}
flowerr=fn('swmm_get_routing_continuity_error',[H,D]); qualerr=fn('swmm_get_quality_continuity_error',[H,c.c_int,D])
link_quality=fn('swmm_link_get_quality',[H,c.c_int,c.c_int,D])
profile=fn('swmm_lid_node_state_get',[H,c.c_int,c.c_int,c.POINTER(c.c_int),D,D,D])
# Names are declared in fixed order in generated decks: nodes A/B/R/FA/FB; links V_AB/V_BR/W_A/W_B.
def checked(code,h):
    if code: raise RuntimeError(f'Engine code {code}: '+ '; '.join(err(h,i).decode() for i in range(errcount(h))))
def get(f,h,*args):
    x=c.c_double(); checked(f(h,*args,c.byref(x)),h); return x.value
summary=[]
library_hash=hashlib.sha256(a.library.read_bytes()).hexdigest()
if a.append and (OUT/'summary.json').exists():
    previous=json.loads((OUT/'summary.json').read_text())
    assert previous['library_sha256']==library_hash, 'Do not combine runs from different binaries.'
    summary=previous['runs']
for source in sorted(DECKS.glob('*.inp')):
    if a.cases and source.stem not in a.cases: continue
    for dt in a.steps:
        label=f'{source.stem}_dt{dt:g}'
        inp=OUT/f'{label}.inp'; inp.write_text(source.read_text().replace('ROUTING_STEP 0.5',f'ROUTING_STEP {dt:g}'))
        rpt=OUT/f'{label}.rpt'; binary=OUT/f'{label}.out'
        h=create(); rows=[]
        try:
            checked(open_(h,str(inp).encode(),str(rpt).encode(),str(binary).encode(),None),h)
            checked(init(h),h); checked(start(h,1),h)
            warnings=[warn(h,i).decode() for i in range(wc(h))]
            t=c.c_double()
            while True:
                checked(stride(h,round(30/dt),c.byref(t)),h)
                if t.value<=0: break
                row={'hour':t.value*24}
                for key,i,f in [('head_A',0,'node_get_head'),('head_B',1,'node_get_head'),('head_R',2,'node_get_head'),('vol_A',0,'node_get_volume'),('vol_B',1,'node_get_volume'),('q_AB',0,'link_get_flow'),('q_BR',1,'link_get_flow'),('q_WA',2,'link_get_flow'),('q_WB',3,'link_get_flow'),('setting_AB',0,'link_get_target_setting'),('setting_BR',1,'link_get_target_setting')]:
                    row[key]=get(getters[f],h,i)
                for link in (1,2,3):
                    row[f'tracer_{link}']=get(link_quality,h,link,1)
                    row[f'reactive_{link}']=get(link_quality,h,link,0)
                for node,i in [('A',0),('B',1)]:
                    layer=c.c_int();bottom=c.c_double();top=c.c_double();moisture=c.c_double()
                    checked(profile(h,i,0,c.byref(layer),c.byref(bottom),c.byref(top),c.byref(moisture)),h)
                    assert layer.value==1
                    row[f'ponding_{node}']=(top.value-bottom.value)*moisture.value/.9
                rows.append(row)
            checked(end(h),h); checked(report(h),h)
            warnings=[warn(h,i).decode() for i in range(wc(h))]
            if warnings: print('WARNINGS', warnings[:5], 'count', len(warnings),flush=True)
            totals={key:get(getters['get_routing_total'],h,code) for key,code in [('inflow_ft3',4),('outflow_ft3',6),('flood_ft3',5),('initial_ft3',9),('final_ft3',10)]}
            entry={'case':source.stem,'step_seconds':dt,'warnings':warnings,'flow_error_percent':100*get(flowerr,h),'reactive_error_percent':100*get(qualerr,h,0),'tracer_error_percent':100*get(qualerr,h,1),**totals}
            text=rpt.read_text(encoding='latin1'); quality=text.split('Quality Routing Continuity',1)[1].split('Continuity Error',1)[0]
            for label2,key in [('External Inflow','mass_in_lbs'),('External Outflow','mass_out_lbs'),('Mass Reacted','mass_reacted_lbs'),('Flooding Loss','mass_flood_lbs'),('Initial Stored Mass','mass_initial_lbs'),('Final Stored Mass','mass_final_lbs')]:
                m=re.search(re.escape(label2)+r'\s*\.+\s*([\d.Ee+-]+)\s+([\d.Ee+-]+)',quality)
                entry[key]=[float(m[1]),float(m[2])]
            entry['peak_receiving_cfs']=max(max(0,r['q_BR']) for r in rows)
            entry['peak_all_exits_cfs']=max(max(0,r['q_BR'])+max(0,r['q_WA'])+max(0,r['q_WB']) for r in rows)
            entry['reverse_AB_ft3_sampled']=sum(max(0,-r['q_AB'])*30 for r in rows)
            entry['reverse_BR_ft3_sampled']=sum(max(0,-r['q_BR'])*30 for r in rows)
            entry['bypass_ft3_engine']=sum(get(getters['link_get_stat_vol_flow'],h,j) for j in [2,3])
            entry['bypass_ft3_sampled']=sum((max(0,r['q_WA'])+max(0,r['q_WB']))*30 for r in rows)
            entry['max_depth_A_ft']=max(r['head_A']-.3 for r in rows)
            entry['max_depth_B_ft']=max(r['head_B'] for r in rows)
            entry['reacted_percent']=100*entry['mass_reacted_lbs'][0]/entry['mass_in_lbs'][0]
            cumulative=0.; entry['tracer_half_export_hour']=None
            for r in rows:
                cumulative+=sum(max(0,r[key])*r[f'tracer_{link}']*30*28.316846592/453592.37 for link,key in [(1,'q_BR'),(2,'q_WA'),(3,'q_WB')])
                if cumulative>=.5*entry['mass_in_lbs'][1] and entry['tracer_half_export_hour'] is None: entry['tracer_half_export_hour']=r['hour']
            entry['accepted_hydraulics']=not warnings and abs(entry['flow_error_percent'])<.5
            entry['accepted']=not warnings and all(abs(entry[k])<.5 for k in ['flow_error_percent','reactive_error_percent','tracer_error_percent'])
            summary=[r for r in summary if (r['case'],r['step_seconds'])!=(entry['case'],entry['step_seconds'])]
            summary.append(entry)
            with (OUT/f'{source.stem}_dt{dt:g}.csv').open('w') as f:
                w=csv.DictWriter(f,fieldnames=rows[0]); w.writeheader(); w.writerows(rows)
            print(label, 'accepted',entry['accepted'],'errors',*[round(entry[k],4) for k in ['flow_error_percent','reactive_error_percent','tracer_error_percent']],'peak',round(entry['peak_receiving_cfs'],4),'reacted',round(entry['reacted_percent'],2),'backflow',round(entry['reverse_AB_ft3_sampled'],1),round(entry['reverse_BR_ft3_sampled'],1),flush=True)
        finally:
            close(h); destroy(h)
        # Runtime binary and expanded decks are reproducible intermediates, keep evidence compact.
        binary.unlink(missing_ok=True); inp.unlink(missing_ok=True)
summary.sort(key=lambda r:(r['case'],-r['step_seconds']))
provenance={'library_filename':a.library.name,'library_sha256':library_hash,'sample_seconds':30,'acceptance_error_percent':.5,'runs':summary}
(OUT/'summary.json').write_text(json.dumps(provenance,indent=2)+'\n')
if not all(e['accepted'] for e in summary):
    raise SystemExit('Acceptance failure; inspect summary.json before publishing.')
print('All hydraulic and pollutant continuity checks passed.')
