#!/usr/bin/env python3
"""Regenerate the small, dependency-free terrain and coupling tutorial inputs."""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent.parent / 'docs/manual/tutorials/models'


def build(name, title, nx, ny, dx, dy, origin, elevation, nodes, links, coupling, inflows='', pulse=0.03, coupling_area=0.5):
    vertices = [(origin[0]+i*dx, origin[1]+j*dy) for j in range(ny) for i in range(nx)]
    text = f'''[TITLE]
{title}
Synthetic teaching geometry; not a calibrated flood-risk model.

[OPTIONS]
FLOW_UNITS CMS
CRS EPSG:3857
FLOW_ROUTING DYNWAVE
INFILTRATION HORTON
LINK_OFFSETS DEPTH
START_DATE 01/01/2024
START_TIME 00:00:00
REPORT_START_DATE 01/01/2024
REPORT_START_TIME 00:00:00
END_DATE 01/01/2024
END_TIME 04:00:00
REPORT_STEP 00:00:30
WET_STEP 00:05:00
DRY_STEP 01:00:00
ROUTING_STEP 00:00:01
SKIP_STEADY_STATE NO
ALLOW_PONDING NO

[EVAPORATION]
CONSTANT 0
DRY_ONLY NO

[RAINGAGES]
RAIN INTENSITY 0:05 1 TIMESERIES STORM

[TIMESERIES]
'''
    text += '\n'.join(f'STORM 00:{m:02d} 20' for m in range(0,60,5))
    text += f'\nSTORM 01:00 0\nSTORM 04:00 0\nPULSE 00:00 {pulse}\nPULSE 00:25 {pulse}\nPULSE 00:30 0\nPULSE 04:00 0\n'
    if name == 'coupled_plaza':
        text = text.replace('LINK_OFFSETS DEPTH', 'LINK_OFFSETS DEPTH\nNODE_CONTINUITY SEMI_IMPLICIT')
    text += '\n[JUNCTIONS]\n' + '\n'.join(f'{n} {z} {depth} 0 0 0' for n,z,depth,x,y in nodes)
    text += '\n\n[OUTFALLS]\nOUT1 95.5 FREE * NO\n' if name=='terrain_bowl' else '\n\n[OUTFALLS]\nOUT1 98 FREE * NO\n'
    text += '\n[CONDUITS]\n' + '\n'.join(f'{n} {a} {b} {length} 0.013 0 0 0 0' for n,a,b,length,diameter in links)
    text += '\n\n[XSECTIONS]\n' + '\n'.join(f'{n} CIRCULAR {diameter} 0 0 0 1' for n,a,b,length,diameter in links)
    if inflows:
        text += '\n\n[INFLOWS]\n'+inflows+'\n'
    text += '\n[REPORT]\nINPUT NO\nCONTROLS NO\nSUBCATCHMENTS ALL\nNODES ALL\nLINKS ALL\n\n[COORDINATES]\n'
    text += '\n'.join(f'{n} {x} {y}' for n,z,depth,x,y in nodes)
    text += '\nOUT1 130 0\n' if name=='terrain_bowl' else '\nOUT1 70 20\n'
    text += f'\n[2D_OPTIONS]\nINTEGRATOR EXPLICIT\nMAX_TIMESTEP 2\nCFL_NUMBER 0.7\nRAINFALL_MODE SYSTEM\nREPORT_2D YES\nOUTPUT_FILE {name}.2d.h5\nDRY_DEPTH 0.001\nCELL_CLOSURE FLAT\nFACE_RECONSTRUCTION MEAN\n\n[2D_VERTICES]\n'
    text += '\n'.join(f'{x:g} {y:g} {elevation(x,y):.6f}' for k,(x,y) in enumerate(vertices))
    text += '\n\n[2D_TRIANGLES]\n'
    for j in range(ny-1):
        for i in range(nx-1):
            a=j*nx+i; b=a+1; c=a+nx; d=c+1
            text += f'{a} {b} {d} 0.035\n{a} {d} {c} 0.035\n'
    text += '\n[2D_VERTEX_NODE_MAP]\n' + '\n'.join(f'{v} {n} 0.65 {coupling_area}' for v,n in coupling)+'\n'
    (ROOT/f'{name}.inp').write_text(text)


def main():
    bowl=lambda x,y:97+3*min((x*x+y*y)/10000,1)
    build('terrain_bowl','T3: square terrain bowl with a central drain',9,9,25,25,(-100,-100),bowl,
          [('J1',96.5,0.5,0,0),('J2',96.25,1,32.5,0),('J3',96,1,65,0),('J4',95.75,1,97.5,0)],
          [('C1','J1','J2',32.5,0.6),('C2','J2','J3',32.5,0.6),('C3','J3','J4',32.5,0.6),('C4','J4','OUT1',32.5,0.6)],[(40,'J1')])
    # GDAL-readable ESRI ASCII grid: northernmost row first, cell-centre samples.
    rows=['ncols 240','nrows 240','xllcorner -120','yllcorner -120','cellsize 1','NODATA_value -9999']
    rows += [' '.join(f'{bowl(-119.5+i,119.5-j):.4f}' for i in range(240)) for j in range(240)]
    (ROOT/'terrain_bowl.asc').write_text('\n'.join(rows)+'\n')
    (ROOT/'terrain_bowl.prj').write_text((ROOT.parents[3]/'examples/channel_burn_boundary/terrain.prj').read_text())
    domain = {'type':'FeatureCollection','crs':{'type':'name','properties':{'name':'EPSG:3857'}},'features':[{'type':'Feature','properties':{'name':'bowl domain'},'geometry':{'type':'Polygon','coordinates':[[[-100,-100],[100,-100],[100,100],[-100,100],[-100,-100]]]}}]}
    (ROOT/'terrain_bowl_domain.geojson').write_text(json.dumps(domain,indent=2)+'\n')
    build('coupled_plaza','T4: metre-based plaza with two coupled junctions',7,5,10,10,(0,0),lambda x,y:100,
          [('J1',99,1,20,20),('J2',98.8,1.2,40,20)],
          [('C1','J1','J2',20,0.10),('C2','J2','OUT1',30,0.10)],[(16,'J1'),(18,'J2')],
          'J1 FLOW PULSE FLOW 1 1 0',pulse=0.1,coupling_area=0.02)

if __name__=='__main__':
    main()
