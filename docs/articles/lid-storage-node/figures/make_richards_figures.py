"""Scientific SVG/PNG schematics and a native Richards profile GIF.
No image-generation model. Captions distinguish calculated examples from native results.
"""
from pathlib import Path
import csv,json,math,hashlib,subprocess,shutil
from make_formulation_figures import text,rect,line,circle,color,save,hydraulic_frame,treatment,ASSETS
FRAMES=Path('/tmp/lid_richards_frames');FRAMES.mkdir(exist_ok=True)
ROOT=Path(__file__).resolve().parents[4];RESULTS=ASSETS.parent/'results/richards'

def stores():
 p=[text(36,48,'Richards 1D: one water inventory in each porous cell',31,weight='bold'),text(36,84,'Surface ponding belongs to the node. Pore and elastic water belong to the column.',22,'muted')]
 p += [rect(28,112,562,648,rx=14),rect(614,112,558,648,rx=14)]
 x,y,w=176,184,240
 p += [rect(x,y,w,78,'#eef4e5'),text(56,217,'SURFACE',18,weight='bold'),rect(x,y+50,w,28,'teal',opacity=.55),text(436,244,'Ponding',19,'teal')]
 for i in range(8):
  yy=y+78+i*47;phi=.45 if i<4 else .40
  p += [rect(x,yy,w,47,'#e2d5bd' if i<4 else '#b7c5cc'),rect(x,yy,w,47,'blue',opacity=.15+.075*i),line(x,yy,x+w,yy,'white',2),text(x+w/2,yy+29,f'W{i+1}  ·  ψ{i+1}',21,'ink',anchor='middle')]
  if i<7:p += [line(x+w+28,yy+28,x+w+28,yy+64,'teal' if i<4 else 'amber',3,arrow=True)]
 p += [rect(x,y,w,454,'none',stroke='ink'),text(52,343,'MEDIA',18,weight='bold'),text(52,536,'AGGREGATE',17,weight='bold'),line(x,y+454,x+w,y+454,'ink',4),text(296,686,'Sealed bottom in these tests',20,'muted',anchor='middle'),text(296,724,'Diagram shows 4 cells/material; tests use 8.',18,'muted',anchor='middle')]
 for yy,title,formula,note in [(154,'POROUS-CELL STORAGE','Wᵢ = Gᵢ [θ(ψᵢ) + Sₛ max(ψᵢ, 0)]','θ ≤ porosity; pressure can rise above zero.'),(313,'CONSERVATIVE FACE BALANCE','dWᵢ/dt = Q_in − Q_out − Eᵢ','One face transfer: equal loss and gain.'),(475,'LOCAL TOTAL HEAD','Hᵢ = zᵢ + ψᵢ','Buried connections use local cell pressure.')]:
  p += [text(644,yy,title,20,'teal','bold'),text(644,yy+48,formula,25),text(644,yy+84,note,20,'muted')]
 p += [text(644,653,'Total facility water = ponding + Σ Wᵢ',24,'blue','bold'),text(644,695,'No duplicate saturated/mobile inventory.',21,'muted'),text(36,803,'Formulation schematic · illustrative section · elastic storage Ss is a physical material input',19,'muted')]
 return p

def flux():
 p=[text(36,48,'Interlayer flux uses both conductivity and total head',31,weight='bold'),text(36,84,'Positive Q is downward. Pressure differences can reinforce, oppose or reverse gravity.',21,'muted')]
 for x,label,H1,H2,c in [(28,'DOWNWARD',1.4,.9,'blue'),(419,'EQUILIBRIUM',1.,1.,'muted'),(810,'UPWARD',.4,.9,'amber')]:
  p += [rect(x,112,362,335,rx=14),text(x+22,154,label,22,c,'bold'),rect(x+52,192,258,82,'#e2d5bd'),rect(x+52,330,258,82,'#b7c5cc'),text(x+181,240,f'Upper H = {H1:.1f} m',24,anchor='middle'),text(x+181,378,f'Lower H = {H2:.1f} m',24,anchor='middle')]
  if H1!=H2:p += [line(x+181,278 if H1>H2 else 326,x+181,326 if H1>H2 else 278,c,5,arrow=True)]
  else:p += [text(x+181,309,'Q = 0',25,c,anchor='middle')]
 p += [rect(28,469,1144,309,rx=14),text(53,513,'Q_f = A_f K_f (H_upper − H_lower) / d_f',30,weight='bold'),text(53,559,'Same material, equal cells: K_f = (K_upper + K_lower) / 2',25,'teal'),text(53,610,'Different materials: R_f = Δz_upper/(2K_upper) + Δz_lower/(2K_lower)',23,'blue'),text(53,652,'Q_f = A_f (H_upper − H_lower) / R_f',27,'blue'),text(53,696,'Example: A = 1 m², K_f = 10 mm/h, d = 0.2 m, ΔH = −0.10 m',21,'muted'),text(53,738,'Q = −5 litres/hour: capillary redistribution upward',25,'amber','bold'),text(36,810,'Calculated equation example · no field calibration · hydraulic/physical material interfaces need measured properties',18,'muted')]
 return p

def comparison():
 p=[text(36,48,'Two selectable models: label the formulation being tested',30,weight='bold'),text(36,84,'Both conserve water. Their moisture dynamics, parameters and hydraulic state differ.',22,'muted')]
 for x,title,c,lines in [(28,'EXISTING FORMULATION','muted',['Gravity drainage Kₛ exp[−slope(φ − θ)]','Field-capacity cutoff in media','Modified Green–Ampt surface entry','Retained cells + shared mobile reservoir','Backwater reconciles front history','No intercell matric-pressure gradient']), (614,'SEMI-DISCRETE RICHARDS 1D','teal',['Signed flux from gradients of H = z + ψ','van Genuchten–Mualem retention / K','Pond contact integrated with the column','Cell-owned pore and elastic storage','Local pressure responds to reverse inflow','Adaptive implicit integration'])]:
  p += [rect(x,112,558,498,rx=14),text(x+24,156,title,23,c,'bold')]
  for i,phrase in enumerate(lines):p += [text(x+24,214+i*65,phrase,21)]
 p += [rect(28,632,1144,146,'#eaf1f4',12),text(53,673,'Still 1D matrix flow: no lateral unsaturated gradients, preferential flow or hysteresis.',23,weight='bold'),text(53,715,'Richards requires explicit retention / specific-storage inputs and cell/time refinement.',22,'muted'),text(53,752,'A passing gravity-model example cannot establish Richards accuracy.',23,'teal','bold')]
 return p

meta=json.loads((RESULTS/'resaturation_cycle.json').read_text());record=min(meta['runs'],key=lambda r:r['step_seconds']);source=RESULTS/record['csv'];assert hashlib.sha256(source.read_bytes()).hexdigest()==record['csv_sha256']
rows=[{k:float(v) for k,v in r.items()} for r in csv.DictReader(source.open())];geometry=record['geometry']
def cycle(frame):
 sec=0 if frame<3 else 360 if frame>58 else (frame-3)/55*360;r=rows[min(len(rows)-1,max(0,round(sec)-1))]
 p=[text(36,48,'Backwater changes the Richards moisture / pressure profile',29,weight='bold'),text(36,84,f'Native 6-minute test · 10 ft² footprint · receiving-stage pulse, then surface inflow · {sec/60:.2f} min',20,'muted'),rect(28,112,388,666,rx=14),rect(440,112,732,666,rx=14)]
 x,y,w=135,189,178;soil_y=y+91;bottom=y+455
 p += [rect(x,y,w,91,'#eef4e5')]
 pond=max(0,r['head']-1.5)
 if pond>0:p += [rect(x,soil_y-min(.5,pond)*182,w,min(.5,pond)*182,'teal',opacity=.5)]
 for g in geometry:
  i=g['cell'];yy=soil_y+(1.5-g['top_ft'])*182;hh=(g['top_ft']-g['bottom_ft'])*182;phi=.45 if g['layer']==2 else .4
  p += [rect(x,yy,w,hh,'#e2d5bd' if g['layer']==2 else '#b7c5cc'),rect(x,yy,w,hh,'blue',opacity=.15+.6*r[f'theta_{i}']/phi),line(x,yy,x+w,yy,'white',1)]
  if r[f'pressure_{i}']>=0:p += [line(x,yy+2,x+w,yy+2,'teal',2)]
 p += [rect(x,y,w,364,'none',stroke='ink'),text(55,245,'Pond',18),text(55,373,'Media',18),text(55,574,'Gravel',18),text(224,679,f'Ponding {pond:.3f} ft',21,'teal',anchor='middle'),text(224,713,f'Tailwater {r["stage"]:.2f} ft',21,anchor='middle'),text(224,747,f'Link flow {r["q"]:+.4f} cfs',20,'amber' if r['q']<0 else 'blue',anchor='middle')]
 for px,width,title,key,xmin,xmax in [(513,252,'Water content θ','theta',0,.5),(866,252,'Pressure head ψ (ft)','pressure',-5,2.5)]:
  py,ph=194,399
  p += [text(px+width/2,157,title,21,weight='bold',anchor='middle')]
  def xx(v):return px+(v-xmin)/(xmax-xmin)*width
  def yy(z):return py+(1.5-z)/1.5*ph
  for z in [0,.5,1.,1.5]:p += [line(px,yy(z),px+width,yy(z),'gray',1),text(px-12,yy(z)+6,f'{z:.1f}',16,'muted',anchor='end')]
  for tick in ([0,.25,.5] if key=='theta' else [-5,0,2.5]):p += [text(xx(tick),py+ph+27,f'{tick:g}',16,'muted',anchor='middle')]
  points=' '.join('{:.2f},{:.2f}'.format(xx(max(xmin,min(xmax,r[key+'_'+str(g['cell'])]))),yy((g['bottom_ft']+g['top_ft'])/2)) for g in geometry)
  p += [f'<polyline points="{points}" fill="none" stroke="{color("blue" if key=="theta" else "amber")}" stroke-width="4"/>',line(px,py+ph,px+width,py+ph,'muted')]
  if key=='pressure':p += [line(xx(0),py,xx(0),py+ph,'teal',2,'5 5')]
 p += [text(808,667,'Vertical coordinate: feet above invert',20,'muted',anchor='middle'),text(464,709,'Blue shade: moisture · teal marks: nonnegative pressure',19,'muted'),text(464,744,'Pressure axis clips ψ < −5 ft; original values remain in CSV.',18,'muted'),text(36,809,'Frames are native states, not tracked particles · no Green–Ampt deficit/front history in Richards mode',18,'muted')]
 return p

def main():
 for stem,parts in [('water-stores',stores()),('interlayer-flux',flux()),('richards-approximation',comparison())]:save(stem,parts)
 # Retain the verified closed-parcel decay curves, replacing the ownership labels.
 parts=treatment(richards=True);joined=''.join(parts)
 save('treatment-formulation',[joined]);save('hydraulic-control-static',hydraulic_frame(31));save('resaturation-static',cycle(37))
 for stem,draw in [('hydraulic-control',hydraulic_frame),('resaturation',cycle)]:
  frames=[save(f'{stem}-{i:03d}',draw(i),directory=FRAMES) for i in range(64)]
  subprocess.run(['magick','-delay','24',*map(str,frames),'-loop','0','-layers','Optimize',str(ASSETS/(stem+'.gif'))],check=True)
 manual=ROOT/'docs/manual/images'
 for stem in ['water-stores','interlayer-flux','richards-approximation','treatment-formulation','hydraulic-control-static','resaturation-static']:
  shutil.copy2(ASSETS/(stem+'.png'),manual/('t10_'+stem+'.png'))
 for stem in ['hydraulic-control','resaturation']:shutil.copy2(ASSETS/(stem+'.gif'),manual/('t10_'+stem+'.gif'))
 print('Richards schematics and two slowed 15.36-second GIFs generated.')
if __name__=='__main__':main()
