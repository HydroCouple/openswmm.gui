"""Create a reviewable site in /tmp; never writes the real website checkout."""
from pathlib import Path
import shutil, subprocess, importlib.util, re, zipfile, json
HERE=Path(__file__).resolve().parent; SOURCE=HERE.parent; GUI=HERE.parents[3]
WEBSITE=GUI.parent/'hydrocouple.github.io'; DEST=Path('/tmp/hydrocouple-lid-site')
# The complete article is shared by both destinations; regenerate the derived draft.
(SOURCE/'linkedin-article.md').write_text((SOURCE/'article.md').read_text())
DEST.mkdir(exist_ok=True)
shutil.copy2(WEBSITE/'articles.html',DEST/'articles.html')
# Preserve original relative stylesheet, scripts and logo references for browser review.
for folder in ['css','js','fonts']:
 if (WEBSITE/folder).exists():shutil.copytree(WEBSITE/folder,DEST/folder,dirs_exist_ok=True)
for f in WEBSITE.glob('*.css'):shutil.copy2(f,DEST/f.name)
(DEST/'img').mkdir(exist_ok=True)
shutil.copy2(WEBSITE/'img/hydrocouple.png',DEST/'img/hydrocouple.png')
article=DEST/'articles/lid-storage-node';article.mkdir(parents=True,exist_ok=True)
assets=DEST/'img/articles/lid-storage-node';assets.mkdir(parents=True,exist_ok=True)
for f in SOURCE.glob('*.md'):shutil.copy2(f,article/f.name)
shutil.copy2(SOURCE/'build.py',article/'build.py')
(article/'README.md').write_text('''# Distributed LID article source

`article.md` is the website source; `build.py` updates this article and its
index entry in `../../articles.html`. Run `python3 build.py` from this folder.
GIFs, static posters and SVG sources live in `../../img/articles/lid-storage-node/`.

The LinkedIn article is an identical export of `article.md`; `build.py` keeps
its Markdown and HTML preview synchronized. The companion post is a separate
announcement draft. The standalone
`tutorial.html` documents the Richards chain models (and archived existing-model comparison) plus the supplementary
`models/lid_richards_resaturation.inp` reversal/recession test; `validation.html`
and `validation.md` record the solver checks and interpretation limits.

The reproducible example generation and figure scripts, sampled CSVs and
native reports are maintained in the sibling openswmm.gui repository under
`docs/articles/lid-storage-node/`. GUI T10 is in `docs/manual/tutorials/`.

This local website update has not been pushed or published. GIFs include
static alternatives and website controls for reduced-motion viewing.
''')
for f in (SOURCE/'assets').iterdir():
 if not f.name.endswith('-preview.jpg'):shutil.copy2(f,assets/f.name)
# Keep images native-relative in the published Markdown source.
for filename in ['article.md','linkedin-article.md']:
 p=article/filename;p.write_text(p.read_text().replace('(assets/','(../../img/articles/lid-storage-node/'))
models=article/'models';models.mkdir(exist_ok=True)
for f in (GUI/'docs/manual/tutorials/models/lid_active_chain').glob('*.inp'):shutil.copy2(f,models/f.name)
with zipfile.ZipFile(models/'lid_active_chain.zip','w',zipfile.ZIP_DEFLATED) as z:
 for f in sorted((GUI/'docs/manual/tutorials/models/lid_active_chain').glob('*.inp')):z.write(f,f.name)
 z.writestr('README.txt','Synthetic 24-hour CFS / Dynamic Wave / Legacy-quality tests. Requires the storage-node LID legacy-hydrology adaptation and pollutant corrections. See tutorial.html and validation.html beside this models folder. Bottom boundaries are closed; groundwater is not simulated.\n')
shutil.copy2(GUI/'docs/manual/tutorials/models/lid_resaturation.inp',models/'lid_resaturation.inp')
shutil.copy2(GUI/'docs/manual/tutorials/models/lid_richards_resaturation.inp',models/'lid_richards_resaturation.inp')
for f in (GUI/'docs/manual/tutorials/models/lid_richards_chain').glob('*.inp'):
 shutil.copy2(GUI/'docs/manual/tutorials/models'/('richards_'+f.name),models/('richards_'+f.name))
with zipfile.ZipFile(models/'lid_richards_chain.zip','w',zipfile.ZIP_DEFLATED) as z:
 for f in sorted((GUI/'docs/manual/tutorials/models/lid_richards_chain').glob('*.inp')):z.write(GUI/'docs/manual/tutorials/models'/('richards_'+f.name),'richards_'+f.name)
 z.write(models/'lid_richards_resaturation.inp','lid_richards_resaturation.inp')
 z.writestr('README.txt','Synthetic Richards 1D CFS / Dynamic Wave / Legacy-quality tests. Explicit illustrative retention properties, not calibrated materials. Review routing/cell/tolerance sensitivity in validation.html. Closed bottoms; runtime aquifer-bed exchange is pending.\n')
# Load the same renderer for standalone tutorial and validation pages.
spec=importlib.util.spec_from_file_location('lid_article_builder',SOURCE/'build.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
def page(title,body):
 # Standalone asset paths differ from those in the top-level articles page.
 rendered='\n'.join(mod.render_body(body)).replace('src="img/articles/','src="../../img/articles/').replace('srcset="img/articles/','srcset="../../img/articles/').replace('data-animated="img/articles/','data-animated="../../img/articles/').replace('data-static="img/articles/','data-static="../../img/articles/')
 rendered=rendered.replace('href="articles/lid-storage-node/','href="')
 support=mod.ANIMATION_SUPPORT.replace('#lid-storage-node .lid-animation','.lid-animation')
 return '<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>'+title+'</title><style>body{font:18px/1.65 system-ui,sans-serif;color:#123047;background:#f7fafb;max-width:1000px;margin:3rem auto;padding:0 1.4rem}img{max-width:100%;height:auto}figure{margin:2rem 0}h1,h2,h3{line-height:1.2}table{border-collapse:collapse;width:100%;font-size:16px}th,td{padding:.5rem;border-bottom:1px solid #dce6eb;text-align:left}.table-scroll{overflow:auto}pre{padding:1rem;background:#eaf1f4;overflow:auto}a{color:#007a80}blockquote{border-left:4px solid #008f83;padding-left:1rem}</style><body><nav><a href="../../articles.html#lid-storage-node">← Article</a> · <a href="models/lid_active_chain.zip">Download original six models</a></nav><h1>'+title+'</h1>'+rendered+support+'</body></html>'
tutorial=(GUI/'docs/manual/tutorials/t10_lid_active_chain.md').read_text().split('\n',1)[1]
tutorial=re.sub(r'\\fig\{([^,]+), ([^}]+)\}',lambda m:'!['+m[2]+'](assets/'+m[1].replace('t10_','')+')',tutorial)
tutorial=tutorial.replace('t10_network.gif','assets/network.gif').replace('t10_pollutant-fate.gif','assets/pollutant-fate.gif').replace('t10_resaturation.gif','assets/resaturation.gif')
tutorial=re.sub(r'\\ref (\w+)',lambda m:'[SWMMVis manual](https://hydrocouple.github.io/openswmm.gui/'+m[1]+'.html)',tutorial)
tutorial=re.sub(r'\]\(((?:(?:richards_)?0\d_[^)]*|lid_(?:richards_)?resaturation)\.inp)\)',r'](models/\1)',tutorial)
(article/'tutorial.html').write_text(page('T10 — Distributed LIDs: Active Control and Backwater',tutorial))
validation=(SOURCE/'validation.md').read_text().split('\n',1)[1]
(article/'validation.html').write_text(page('Validation and reproducibility',validation))
# Website links lead to rendered validation; Markdown source remains a downloadable reference.
p=article/'article.md';p.write_text(p.read_text().replace('(validation.md)','(validation.html)'))
subprocess.run(['python3',str(article/'build.py'),str(DEST)],check=True)
# Idempotence and preservation of every preexisting article are required.
first=(DEST/'articles.html').read_text();subprocess.run(['python3',str(article/'build.py'),str(DEST)],check=True);assert first==(DEST/'articles.html').read_text()
assert (article/'article.md').read_bytes()==(article/'linkedin-article.md').read_bytes()
old=(WEBSITE/'articles.html').read_text();oldblocks=re.findall(r'<article id="([^"]+)".*?</article>',old,re.S)
for slug in oldblocks:
 if slug=='lid-storage-node':continue
 pattern=r'<article id="'+re.escape(slug)+r'".*?</article>'
 assert re.search(pattern,old,re.S)[0]==re.search(pattern,first,re.S)[0],slug
assert first.count('<article id="lid-storage-node">')==1
assert first.count('data-target="lid-storage-node"')==1
print('Staged website:',DEST,'; existing article blocks preserved; build idempotent')
