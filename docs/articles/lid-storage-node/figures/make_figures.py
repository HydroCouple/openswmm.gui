"""Data-driven SVG/PNG figures and looping GIFs for article and T10.
Requires rsvg-convert and ImageMagick. CSVs are sampled engine results, not
particle trajectories. Dot movement is a direction cue, not tracer tracking.
"""
from pathlib import Path
import csv, json, math, html, subprocess
HERE=Path(__file__).resolve().parent; ROOT=HERE.parents[3]
RESULTS=HERE.parent/'results'; ASSETS=HERE.parent/'assets'; ASSETS.mkdir(exist_ok=True)
FRAMES=Path('/tmp/lid_article_frames'); FRAMES.mkdir(exist_ok=True)
COLORS={'ink':'#123047','muted':'#557080','blue':'#188fb6','teal':'#008f83','amber':'#db8b24','red':'#b84e5b','paper':'#f7fafb','gray':'#dce6eb'}
LABELS=['Passive / free outlet','Passive / backwater','Timed hold / backwater','Hold + head guard','Second storm / guard','Both valves stuck closed']
summary=json.loads((RESULTS/'summary.json').read_text()); figure_step=min(r['step_seconds'] for r in summary['runs'])
runs=[r for r in summary['runs'] if r['step_seconds']==figure_step]
assert len(runs)==6 and all(r['accepted'] for r in runs), 'Publish only a complete set of accepted finest-step runs'
data={r['case']:[{k:float(v) for k,v in row.items()} for row in csv.DictReader((RESULTS/(r['case']+f'_dt{figure_step:g}.csv')).open())] for r in runs}
def t(x,y,s,size=18,color='ink',weight='normal',anchor='start'):
 return f'<text x="{x}" y="{y}" font-size="{size}" fill="{COLORS.get(color,color)}" font-weight="{weight}" text-anchor="{anchor}">{html.escape(str(s))}</text>'
def rect(x,y,w,h,fill,rx=0,opacity=1,stroke='none'):
 return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{COLORS.get(fill,fill)}" opacity="{opacity}" stroke="{COLORS.get(stroke,stroke)}"/>'
def line(x1,y1,x2,y2,color='gray',width=2,dash=''):
 return f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{COLORS.get(color,color)}" stroke-width="{width}"'+(f' stroke-dasharray="{dash}"' if dash else '')+'/>'
def svg(items,w=1100,h=700):
 return f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" font-family="Arial, sans-serif">'+rect(0,0,w,h,'paper')+''.join(items)+'</svg>'
def save(name,items,w=1100,h=700,where=ASSETS):
 source=where/(name+'.svg'); source.write_text(svg(items,w,h)); png=where/(name+'.png')
 subprocess.run(['rsvg-convert','-o',str(png),str(source)],check=True); return png

def row_at(case,hour):
 rows=data[case]; return rows[min(len(rows)-1,max(0,round(hour*120)-1))]
def pulse(h,repeat=False):
 return max(0,.30*(1-abs(h-1))) + (max(0,.45*(1-abs(h-10))) if repeat else 0)
def arrow(x1,x2,y,q,setting,phase):
 color='blue' if q>=0 else 'amber'; items=[line(x1,y,x2,y,'gray',5)]
 center=(x1+x2)/2
 if setting<.5:
  items += [line(center-7,y-8,center+7,y+8,'red',4),line(center-7,y+8,center+7,y-8,'red',4)]
 elif abs(q)>.00005:
  direction=1 if q>=0 else -1
  items += [line(x1,y,x2,y,color,4)]
  for k in range(3):
   frac=(phase+k/3)%1; frac=frac if direction>0 else 1-frac
   x=x1+frac*(x2-x1)
   items += [f'<path d="M{x-5*direction},{y-5} L{x},{y} L{x-5*direction},{y+5}" fill="none" stroke="{COLORS[color]}" stroke-width="3"/>']
 return items

def network(hour,frame=0):
 a=[t(32,40,'Distributed LIDs: downstream conditions reach upstream',27,weight='bold'),t(32,70,'One storm. Three control strategies. The same receiving-water stage.',18,'muted'),t(1068,70,f'{hour:04.1f} h / 24 h',20,weight='bold',anchor='end')]
 cases=[runs[i]['case'] for i in (1,2,3)]
 for idx,case in enumerate(cases):
  row=row_at(case,hour); y=100+idx*170
  a += [rect(20,y,1060,158,'#ffffff',12),t(38,y+28,LABELS[(1,2,3)[idx]],20,weight='bold')]
  for node,x,inv in [('A',275,.3),('B',535,0)]:
   base=y+140-inv*40; top=base-100; head=row['head_'+node]
   a += [rect(x,top,160,20,'#eef4e5'),rect(x,top+20,160,40,'#e2d5bd'),rect(x,top+60,160,40,'#b7c5cc')]
   depth=max(0,min(2.5,head-inv)); a += [rect(x,base-depth*40,160,depth*40,'blue',opacity=.48)]
   ponding=max(0,min(.5,row['ponding_'+node]))
   if ponding>0:a += [rect(x,top+20-ponding*40,160,ponding*40,'teal',opacity=.65)]
   a += [rect(x,top,160,100,'none',stroke='ink'),t(x+80,top-9,f'LID {node}',17,weight='bold',anchor='middle'),t(x+80,base+15,f'mobile {head:.2f} ft',14,'muted',anchor='middle')]
   if row['q_W'+node]>.00005:
    a += [t(x+80,top+15,'BYPASS',13,'red',weight='bold',anchor='middle')]
  # All three heads share the same datum and scale within this schematic.
  a += [rect(830,y+40,95,100,'#edf5f8'),rect(830,y+140-max(0,row['head_R'])*40,95,max(0,row['head_R'])*40,'blue',opacity=.48),t(878,y+31,'Receiver',17,weight='bold',anchor='middle'),t(878,y+155,f'stage {row["head_R"]:.2f} ft',14,'muted',anchor='middle')]
  a += arrow(435,535,y+126,row['q_AB'],row['setting_AB'],frame*.21)
  a += arrow(695,830,y+140,row['q_BR'],row['setting_BR'],frame*.21)
  a += [t(485,y+111,'V_AB',13,'muted',anchor='middle'),t(760,y+122,'V_BR',13,'muted',anchor='middle'),t(38,y+62,'Inflow',14,'muted'),t(38,y+85,f'{pulse(hour):.3f} cfs',17,'blue',weight='bold')]
  a += [t(962,y+61,'Valves',13,'muted'),t(962,y+85,'A: '+('HOLD' if row['setting_AB']<.5 else 'OPEN'),14,'red' if row['setting_AB']<.5 else 'teal'),t(962,y+108,'B: '+('HOLD' if row['setting_BR']<.5 else 'OPEN'),14,'red' if row['setting_BR']<.5 else 'teal')]
 a += [line(32,626,62,626,'blue',4),t(70,632,'Forward flow',15),line(235,626,265,626,'amber',4),t(273,632,'Reverse flow',15),t(463,632,'× Closed valve',15,'red'),t(660,632,'Surface / media / aggregate',15,'muted')]
 a += [t(32,666,'Blue: mobile water · teal: perched ponding · dots indicate direction, not tracked particles · schematic sections',14,'muted')]
 return a

# Compare cumulative exported tracer and the final 24-hour reactive budget.
series={}
for r in runs:
 values=[]; total=0
 for row in data[r['case']]:
  total+=sum(max(0,row[q])*row[f'tracer_{j}']*30*28.316846592/453592.37 for j,q in [(1,'q_BR'),(2,'q_WA'),(3,'q_WB')])
  values.append((row['hour'],100*total/r['mass_in_lbs'][1]))
 series[r['case']]=values

def poly(points,color,width=3):
 return '<polyline fill="none" stroke="'+COLORS.get(color,color)+'" stroke-width="'+str(width)+'" points="'+' '.join(f'{x:.2f},{y:.2f}' for x,y in points)+'"/>'
def fate(hour):
 a=[t(32,42,'Delayed export is different from treatment',28,weight='bold'),t(32,72,'Tracer passage through all three exits; reactive mass accounted for at 24 h',17,'muted')]
 x0,y0,w,h=70,365,505,250
 for tick in (0,25,50,75,100):
  yy=y0-tick*h/100; a += [line(x0,yy,x0+w,yy),t(x0-12,yy+5,str(tick),13,'muted',anchor='end')]
 for tick in (0,6,12,18,24):
  xx=x0+tick*w/24;a += [t(xx,y0+24,str(tick),13,'muted',anchor='middle')]
 a += [t(x0,99,'Cumulative tracer exported (%)',15,weight='bold'),t(x0+w/2,y0+48,'Elapsed simulation time (hours)',14,'muted',anchor='middle')]
 for idx,color in [(0,'muted'),(1,'amber'),(3,'teal')]:
  vals=series[runs[idx]['case']]; a += [poly([(x0+hr*w/24,y0-val*h/100) for hr,val in vals if hr<=hour],color)]
  a += [line(70,452+idx*22,99,452+idx*22,color,4),t(108,457+idx*22,LABELS[idx],15)]
 a += [line(x0+hour*w/24,115,x0+hour*w/24,y0,'ink',1,'4 4'),t(570,530,f'Time cursor: {hour:.1f} h',16,weight='bold',anchor='end')]
 a += [t(640,112,'Reactive mass fate at 24 hours',18,weight='bold'),t(640,140,'All input mass stays in the accounting',14,'muted')]
 for ii,idx in enumerate([0,1,3,4,5]):
  r=runs[idx]; y=177+ii*66; inp=r['mass_in_lbs'][0]
  reacted=r['mass_reacted_lbs'][0]/inp; exported=r['mass_out_lbs'][0]/inp; held=r['mass_final_lbs'][0]/inp; flood=r['mass_flood_lbs'][0]/inp
  a += [t(640,y-9,LABELS[idx],14,weight='bold')]; xx=640
  for frac,col in [(reacted,'teal'),(exported,'blue'),(held,'amber'),(flood,'red')]:
   a += [rect(xx,y,360*frac,22,col)];xx+=360*frac
  a += [t(1010,y+17,f'{reacted*100:.1f}%',14,'teal')]
 a += [rect(640,519,12,12,'teal'),t(660,530,'Reacted',13),rect(752,519,12,12,'blue'),t(772,530,'Exported',13),rect(872,519,12,12,'amber'),t(892,530,'Stored',13),rect(970,519,12,12,'red'),t(990,530,'Flood loss',13)]
 a += [t(32,583,'A held tracer can leave later. Only reacted mass represents the assumed treatment process.',17,weight='bold'),t(32,617,'Synthetic first-order constituent: 2/day in porous layers · no field calibration · CSV flux integrals sampled at 30 s',13,'muted')]
 return a

save('network-static',network(5.5),1100,690)
save('pollutant-fate-static',fate(24),1100,640)
# Static hydrograph: same storm plus receiving outlet, with controlled hold.
a=[t(32,40,'Control shifts the release; check the resulting peak',28,weight='bold'),t(32,70,'First storm only · receiving outlet V_BR · negative values show receiver backflow',17,'muted')]
x0,y0,w,h=80,490,950,350; lo,hi=-.06,.06
for tick in [-.06,-.03,0,.03,.06]:
 yy=y0-(tick-lo)/(hi-lo)*h;a += [line(x0,yy,x0+w,yy),t(65,yy+5,f'{tick:.2f}',14,'muted',anchor='end')]
for tick in [0,4,8,12,16,20,24]:a += [t(x0+tick*w/24,y0+24,str(tick),14,'muted',anchor='middle')]
a += [rect(x0+4*w/24,y0-h,3*w/24,h,'amber',opacity=.07),t(x0+5.5*w/24,127,'High tailwater',13,'amber',anchor='middle'),t(x0,104,'Flow (cfs)',14,weight='bold'),t(555,540,'Elapsed time (hours)',15,'muted',anchor='middle')]
for i,color in [(0,'muted'),(1,'amber'),(3,'teal')]:
 pts=[(x0+r['hour']*w/24,y0-(r['q_BR']-lo)/(hi-lo)*h) for r in data[runs[i]['case']]];a += [poly(pts,color),line(80+i*260,588,107+i*260,588,color,4),t(115+i*260,593,LABELS[i],14)]
a += [t(32,640,'The 0.30 cfs inlet peak is outside this outlet-scale plot. Emergency bypass is assessed separately.',14,'muted')]
save('release-hydrograph',a,1100,665)

for kind,draw,height in [('network',network,690),('pollutant-fate',lambda hour,frame:fate(hour),640)]:
 paths=[]
 for i in range(64):
  # Start / finish dwell without disguising the 24-hour time scale.
  hour=0 if i<3 else 24 if i>58 else (i-3)/55*24
  paths.append(save(f'{kind}-{i:03d}',draw(hour,i),1100,height,FRAMES))
 subprocess.run(['magick','-delay','16',*[str(x) for x in paths],'-loop','0','-layers','Optimize',str(ASSETS/(kind+'.gif'))],check=True)
 print(kind,'GIF complete',flush=True)
# Manual static figures only; animations copied via HTML_EXTRA_FILES.
manual=ROOT/'docs/manual/images'
for name in ['network-static','pollutant-fate-static','release-hydrograph']:
 subprocess.run(['magick',str(ASSETS/(name+'.png')),'-strip','-define','png:compression-level=9',str(manual/('t10_'+name+'.png'))],check=True)
import shutil
for stem in ['network','pollutant-fate']:shutil.copy2(ASSETS/(stem+'.gif'),manual/('t10_'+stem+'.gif'))
print('Assets:',ASSETS)
