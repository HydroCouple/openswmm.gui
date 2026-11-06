"""Native six-minute Richards backwater/recession/second-event profile test.
Samples the live API, checks total water and tracer continuity, and records provenance.
"""
from pathlib import Path
import argparse,ctypes as c,csv,hashlib,json,re
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[3]
p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--steps',nargs='+',type=float,default=[.1,.05]);a=p.parse_args()
models=ROOT/'docs/manual/tutorials/models';out=HERE.parent/'results/richards';out.mkdir(exist_ok=True)
text=(models/'lid_resaturation.inp').read_text().replace('Stack MEDIA 12 .45 .2 .08 2 10 3','Stack MEDIA 12 .45 .2 .08 24 10 3').replace('S Stack 0','S Stack 10')
text=text.replace('[LID_NODES]','[LID_RICHARDS]\nStack OPTIONS 8 1e-7 1e-5 30\nStack 2 .03 4 1.8 .5 .0001\nStack 3 .01 20 2.5 .5 .0001\n[LID_NODES]')
model=models/'lid_richards_resaturation.inp';model.write_text(text)
lib=c.CDLL(str(a.library.resolve()));H=c.c_void_p;D=c.POINTER(c.c_double)
def fn(name,args,result=c.c_int):
 f=getattr(lib,name);f.argtypes=args;f.restype=result;return f
create=fn('swmm_engine_create',[],H);open_=fn('swmm_engine_open',[H,c.c_char_p,c.c_char_p,c.c_char_p,c.c_char_p]);init=fn('swmm_engine_initialize',[H]);start=fn('swmm_engine_start',[H,c.c_int]);stride=fn('swmm_engine_stride',[H,c.c_int,D]);end=fn('swmm_engine_end',[H]);report=fn('swmm_engine_report',[H]);close=fn('swmm_engine_close',[H]);destroy=fn('swmm_engine_destroy',[H],None)
count=fn('swmm_lid_node_state_count',[H,c.c_int]);profile=fn('swmm_lid_node_state_get',[H,c.c_int,c.c_int,c.POINTER(c.c_int),D,D,D]);pressure=fn('swmm_lid_richards_state_get',[H,c.c_int,c.c_int,D,D,D]);head=fn('swmm_node_get_head',[H,c.c_int,D]);flow=fn('swmm_link_get_flow',[H,c.c_int,D]);water=fn('swmm_get_routing_continuity_error',[H,D]);quality=fn('swmm_get_quality_continuity_error',[H,c.c_int,D]);wc=fn('swmm_get_warning_count',[H]);warn=fn('swmm_get_warning_at',[H,c.c_int],c.c_char_p);ec=fn('swmm_get_error_count',[H]);error=fn('swmm_get_error_at',[H,c.c_int],c.c_char_p)
def check(code,h):
 if code:raise RuntimeError(str(code)+': '+'; '.join(error(h,i).decode() for i in range(ec(h))))
def get(f,h,*args):
 v=c.c_double();check(f(h,*args,c.byref(v)),h);return v.value
runs=[]
for dt in a.steps:
 stem=f'resaturation_dt{dt:g}';inp=out/(stem+'.inp');inp.write_text(text.replace('ROUTING_STEP 0.1',f'ROUTING_STEP {dt:g}'));rpt=out/(stem+'.rpt');binary=out/(stem+'.out');h=create();rows=[];geometry=[]
 try:
  check(open_(h,str(inp).encode(),str(rpt).encode(),str(binary).encode(),None),h);check(init(h),h);check(start(h,1),h);elapsed=c.c_double()
  while True:
   check(stride(h,round(1/dt),c.byref(elapsed)),h)
   if elapsed.value<=0:break
   sec=elapsed.value*86400;row={'second':sec,'head':get(head,h,0),'stage':get(head,h,1),'q':get(flow,h,0)}
   for i in range(1,count(h,0)):
    layer=c.c_int();bot=c.c_double();top=c.c_double();theta=c.c_double();psi=c.c_double();Hcell=c.c_double();W=c.c_double()
    check(profile(h,0,i,c.byref(layer),c.byref(bot),c.byref(top),c.byref(theta)),h);check(pressure(h,0,i,c.byref(psi),c.byref(Hcell),c.byref(W)),h)
    row[f'theta_{i}']=theta.value;row[f'pressure_{i}']=psi.value;row[f'head_{i}']=Hcell.value;row[f'water_{i}']=W.value
    if not rows:geometry.append({'cell':i,'layer':layer.value,'bottom_ft':bot.value,'top_ft':top.value})
   row['flow_error_percent']=100*get(water,h);row['tracer_error_percent']=100*get(quality,h,0)
   assert all(abs(row[k])<.5 for k in ['flow_error_percent','tracer_error_percent']),row
   rows.append(row)
  check(end(h),h);check(report(h),h)
  warnings=[warn(h,i).decode() for i in range(wc(h))];assert not warnings,warnings
  path=out/(stem+'.csv')
  with path.open('w') as f:
   w=csv.DictWriter(f,fieldnames=rows[0],lineterminator='\n');w.writeheader();w.writerows(rows)
  reporttext=rpt.read_text(encoding='latin1');rpt.write_text('\n'.join(l.rstrip() for l in reporttext.splitlines())+'\n',encoding='latin1')
  runs.append({'step_seconds':dt,'csv':path.name,'csv_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'warnings':warnings,'flow_error_percent':100*get(water,h),'tracer_error_percent':100*get(quality,h,0),'max_abs_sample_flow_error_percent':max(abs(r['flow_error_percent']) for r in rows),'max_abs_sample_tracer_error_percent':max(abs(r['tracer_error_percent']) for r in rows),'reverse_flow_ft3_sampled':sum(max(0,-r['q']) for r in rows),'max_top_media_theta':max(r['theta_1'] for r in rows),'max_top_media_pressure_ft':max(r['pressure_1'] for r in rows),'geometry':geometry})
  print(stem,'passed continuity and one-second profile sampling',flush=True)
 finally:close(h);destroy(h);inp.unlink(missing_ok=True);binary.unlink(missing_ok=True)
(out/'resaturation_cycle.json').write_text(json.dumps({'library_sha256':hashlib.sha256(a.library.read_bytes()).hexdigest(),'model_sha256':hashlib.sha256(model.read_bytes()).hexdigest(),'sample_seconds':1,'runs':runs},indent=2)+'\n')
