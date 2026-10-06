"""Consolidate complete checkpointed runs and issue matching GUI downloads.
Failed attempts and preliminary reports remain separate evidence.
"""
from pathlib import Path
import json,shutil,hashlib,gzip,re
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[3];RESULTS=HERE.parent/'results/richards';MODELS=ROOT/'docs/manual/tutorials/models'
def load(folder):
 p=RESULTS/folder/'summary.json';s=json.loads(p.read_text());assert all(r['accepted'] for r in s['runs']);return s
s=load('baseline');assert len(s['runs'])==4
sources=[('baseline',r) for r in s['runs']]
second=load('second-storm');second_folder='second-storm'
if (RESULTS/'second-storm-fine/summary.json').exists():second=load('second-storm-fine');second_folder='second-storm-fine'
sources.append((second_folder,second['runs'][0]))
closed=load('stuck-closed');sources.append(('stuck-closed',min(closed['runs'],key=lambda r:r['step_seconds'])))
assert len({r['case'] for _,r in sources})==6
published={}
for folder,r in sources:
 assert load(folder)['library_sha256']==s['library_sha256']
 stem=r['case']+f'_dt{r["step_seconds"]:g}'
 for suffix in ['.csv','.rpt']:shutil.copy2(RESULTS/folder/(stem+suffix),RESULTS/(stem+suffix))
 source=MODELS/'lid_richards_chain'/(r['case']+'.inp');assert hashlib.sha256(source.read_bytes()).hexdigest()==r['input_sha256']
 text=re.sub(r'(?m)^ROUTING_STEP\s+\S+',f'ROUTING_STEP {r["step_seconds"]:g}',source.read_text())
 dest=MODELS/('richards_'+source.name);dest.write_text(text);published[dest.name]=hashlib.sha256(dest.read_bytes()).hexdigest()
s['runs']=[r for _,r in sources];s['published_models']=published;s['illustration_csv_sample_seconds']=30
(RESULTS/'summary.json').write_text(json.dumps(s,indent=2)+'\n')
# Compress completed profile CSVs losslessly; retain the short-cycle hashed CSVs.
for p in RESULTS.rglob('0*.csv'):
 raw=p.read_bytes();packed=p.with_suffix(p.suffix+'.gz')
 with packed.open('wb') as f:
  with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as z:z.write(raw)
 assert gzip.decompress(packed.read_bytes())==raw;p.unlink()
print('Published six complete native examples, matching default routing steps and lossless compressed profiles.')
