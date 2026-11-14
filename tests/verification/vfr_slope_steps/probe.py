import math
# Triangles in four unit squares; z=-x, h=.01
xy=[(x,y) for x in range(5) for y in (0,1)]
cells=[]
for j in range(4): cells += [(2*j,2*j+2,2*j+3),(2*j,2*j+3,2*j+1)]
centers=[tuple(sum(xy[i][k] for i in c)/3 for k in (0,1)) for c in cells]
z=[-p[0] for p in xy]
adj=[[j for j,c1 in enumerate(cells) if j!=i and len(set(c)&set(c1))==2] for i,c in enumerate(cells)]
stencils=[list(set(ns+[k for j in ns for k in adj[j]])-{i}) for i,ns in enumerate(adj)]
def mean(eta,zb):
 a,b,c=sorted(zb)
 if eta<=a:return 0
 if eta>=c:return eta-(a+b+c)/3
 if eta<=b:return (eta-a)**3/(3*(b-a)*(c-a))
 return eta-(a+b+c)/3+(c-eta)**3/(3*(c-a)*(c-b))
def invert(zb):
 lo=min(zb);hi=max(zb)+.01
 for _ in range(60):
  eta=(lo+hi)/2
  if mean(eta,zb)<.01:lo=eta
  else:hi=eta
 return (lo+hi)/2
stage=[invert([z[k] for k in c]) for c in cells]
for iteration in range(30):
 nex=[];minq=100
 for i,c in enumerate(cells):
  a=b=d=u=v=0
  for j in stencils[i]:
   dx=centers[j][0]-centers[i][0];dy=centers[j][1]-centers[i][1]
   w=1/(dx*dx+dy*dy); dh=stage[j]-stage[i]
   a+=w*dx*dx;b+=w*dx*dy;d+=w*dy*dy;u+=w*dx*dh;v+=w*dy*dh
  det=a*d-b*b;gx=(d*u-b*v)/det;gy=(a*v-b*u)/det
  limiter=1
  for j in stencils[i]:
   dx=centers[j][0]-centers[i][0];dy=centers[j][1]-centers[i][1]
   pred=gx*dx+gy*dy;delta=stage[j]-stage[i]
   if pred:limiter=min(limiter,max(0,delta/pred))
  gx*=limiter;gy*=limiter
  eff=[z[k]-gx*(xy[k][0]-centers[i][0])-gy*(xy[k][1]-centers[i][1]) for k in c]
  alpha=invert(eff);nex.append(alpha);minq=min(minq,min(alpha-e for e in eff))
 stage=nex
 print(iteration,minq,max(abs(stage[i]-(-centers[i][0]+.01)) for i in range(len(cells))))
