# SPDX-License-Identifier: Apache-2.0
"""Opt-in complete mixed-job capacity probe, including source and terrain audits.

python tests/mixed_scale_probe.py --full --report /path/to/report.json
This POSIX probe uses resource.getrusage for peak RSS. Requires the built meshkit Python package and NumPy. Excludes GUI rendering,
GDAL reprojection and file export. A pilot runs when --full is omitted.
"""
import meshkit,numpy as np,json,time,resource,pathlib,argparse,sys
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--full',action='store_true')
parser.add_argument('--report',type=pathlib.Path)
args=parser.parse_args()

full=args.full
side=2610. if full else 106.
step=260. if full else 52.
width=250. if full else 42.
count=10 if full else 2
hole0=(1302. if full else 54.)
# All crossings are authored junctions. Two owners share the horizontal source
# in reverse order; a hole remains in the clear triangular corridor.
pts=[[0,0],[side,0],[side,side],[0,side],[0,5],[5,5],[side,5],[5,0],[5,side],
     [hole0,hole0],[hole0+4,hole0],[hole0+4,hole0+4],[hole0,hole0+4]]
paths=[dict(vertices=[0,1,2,3],kind='outer',closed=True,id=1),dict(vertices=[9,10,11,12],kind='hole',closed=True,id=2),dict(vertices=[4,5,6],kind='line',marker=17,id=3),dict(vertices=[6,5,4],kind='line',marker=18,id=4),dict(vertices=[7,5,8],kind='line',marker=19,id=5)]
regions=[]
for y in range(count):
 for x in range(count):
  x0=10+x*step;y0=10+y*step
  regions.append(dict(ring=[[x0,y0],[x0+width,y0],[x0+width,y0+width],[x0,y0+width]],spacing=1.,cells='required_quads',tag=f'tile-{x}-{y}'))
# Node values define one exact bilinear terrain; facets need full coverage
# certification, including extrema between the nodes.
y,x=np.mgrid[0:65,0:65];spacing=side/62.;xx=(x-1)*spacing;yy=(y-1)*spacing
heights=.002*xx+.003*yy+2.e-5*xx*yy
request=dict(points=np.array(pts,dtype=np.float64),paths=paths,quad_regions=regions,options=dict(cells='mixed',minimum_target=.1,maximum_size=1.1,minimum_angle_degrees=20.,minimum_edge=.001,adjacent_ratio=4.,terrain_tolerance=.0001,require_terrain_certification=True,limits=dict(max_vertices=12000000,max_cells=14000000,max_workspace_bytes=8*1024**3)))
start=time.monotonic();last=''
def progress(e):
 global last
 if e['stage']!=last or e['stage']=='complete':print(e,flush=True);last=e['stage']
result=meshkit.run(request,raster=dict(heights=heights,origin=[-spacing,-spacing],spacing=[spacing,spacing]),provider_identity='bilinear z=.002*x+.003*y+2e-5*x*y',progress=progress,poll_interval=.5)
report=result.report
report.update(wall_seconds=time.monotonic()-start,peak_rss_bytes=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss*(1 if sys.platform=='darwin' else 1024),full=full,domain_side=side)
name='mixed-scale-full' if full else 'mixed-scale-pilot'
if args.report:args.report.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2),flush=True)
assert report['complete'],report
assert report['constraint_audit_passed'] and report['quads']>0 and report['triangles']>0
assert report['size_conflicts']==report['edge_conflicts']==report['transition_conflicts']==report['terrain_conflicts']==report['terrain_unknown']==0
assert {17,18,19}.issubset(set(np.asarray(result.edge_markers)))
assert len(result.paths)>=5
if full:assert report['vertices']>=6000000 and report['triangles']+report['quads']>=6000000
