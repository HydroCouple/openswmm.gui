"""Publish explicit Richards evidence and retain historical gravity-model results.
Does not claim convergence or re-use a passing test count from another formulation.
"""
from pathlib import Path
import runpy
import csv,json,re,hashlib,shutil
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[3];ARTICLE=HERE.parent;RESULTS=ARTICLE/'results/richards'
s=json.loads((RESULTS/'summary.json').read_text());assert s['formulation']=='richards'
cases=sorted({r['case'] for r in s['runs']});assert len(cases)==6 and len(s['runs'])==6 and all(r['accepted'] for r in s['runs'])
runs=[next(r for r in s['runs'] if r['case']==case) for case in cases]
def load(folder):
 x=json.loads((RESULTS/folder/'summary.json').read_text());assert x['library_sha256']==s['library_sha256'] and all(r['accepted'] for r in x['runs']);return x['runs']
mesh4=load('cells4')[0];mesh16=load('cells16')[0];tight=load('tight')[0];fine=load('routing-fine');cycle=json.loads((RESULTS/'resaturation_cycle.json').read_text());assert cycle['library_sha256']==s['library_sha256']
kernel=json.loads((RESULTS/'kernel_tests.json').read_text());nodes=json.loads((RESULTS/'lid_node_tests.json').read_text());assert kernel['failures']==0
labels=['Passive / free outlet','Passive / backwater','Timed hold','Hold + head guard','Second storm / guard','Valves stuck closed']
def half(r):return f'{r["tracer_half_export_hour"]:.2f}' if r['tracer_half_export_hour'] is not None else 'Not reached'
table='| Richards first-storm strategy (1.25 s, 8 cells/material) | Sampled V_BR peak (cfs) | Tracer 50% export (h) | Reacted by 24 h |\n|---|---|---|---|\n'
for i,r in enumerate(runs[:4]):table+=f'| {labels[i]} | {r["peak_receiving_cfs"]:.4f} | {half(r)} | {r["reacted_percent"]:.1f}% |\n'
old=json.loads((ARTICLE/'results/existing-model/summary.json').read_text());previous=[min((r for r in old['runs'] if r['case']==case),key=lambda r:r['step_seconds']) for case in cases]
comparison='| First-storm strategy | Earlier sampled gravity/front peak (cfs) | Sampled Richards peak (cfs) | Earlier reacted | Richards reacted |\n|---|---|---|---|---|\n'
for i in [0,1,3]:comparison+=f'| {labels[i]} | {previous[i]["peak_receiving_cfs"]:.4f} | {runs[i]["peak_receiving_cfs"]:.4f} | {previous[i]["reacted_percent"]:.1f}% | {runs[i]["reacted_percent"]:.1f}% |\n'
mesh='| Guarded Richards case at 1.25 s | Sampled V_BR peak (cfs) | Reacted by 24 h | Emergency bypass (ft³) |\n|---|---|---|---|\n'
for count,r in [(4,mesh4),(8,runs[3]),(16,mesh16)]:mesh+=f'| {count} cells per porous material | {r["peak_receiving_cfs"]:.4f} | {r["reacted_percent"]:.2f}% | {r["bypass_ft3_engine"]:.1f} |\n'
allruns=s['runs']+[mesh4,mesh16,tight]+fine
for r in load('stuck-closed'):
 if r['step_seconds']!=runs[5]['step_seconds']:allruns.append(r)
for r in load('second-storm'):
 if r['step_seconds']!=runs[4]['step_seconds']:allruns.append(r)
errors=max(abs(r[k]) for r in allruns for k in ['flow_error_percent','reactive_error_percent','tracer_error_percent'])
validation=f'The six examples were run with Richards explicitly enabled. The first four illustrated strategies use eight cells per porous material and a common 1.25-second fixed routing step; the second-storm figure uses {runs[4]["step_seconds"]:g} s and the stuck-closed case {runs[5]["step_seconds"]:g} s. The publication package contains {len(allruns)} checkpointed complete case/numerics combinations, including finer routing, 4/16-cell and tighter ODE-tolerance checks. All these completed runs passed the 0.5% water/tracer/reactive continuity criterion without engine warnings; maximum reported error was {errors:.3g}%. **The two-storm case failed at 1.25 s with a Richards adaptive-step-limit error; it is retained as a failed check, not counted as a pass.** The pressure-profile fixture passed at 0.1 and 0.05 s. **These are not numerically converged performance predictions:** routing-step and cell-count changes materially affect outlet peaks, reverse volumes and treatment exposure. The current engine passed {kernel["tests"]} Richards kernel tests and {nodes["tests"]-nodes["failures"]} of {nodes["tests"]} LID-node integration tests; the US/SI storage-equivalence fixture remains failing. The older 241-test/20-run evidence applies to the previous gravity/front implementation, not Richards.'
validation+='\n\n'+mesh+'\nThe grid dependence is large enough that these runs cannot establish a robust ranking of control strategies. The guarded case’s tighter ODE tolerance changes reacted fraction by '+f'{abs(tight["reacted_percent"]-runs[3]["reacted_percent"]):.3f} percentage points'+'; this does not resolve the distinct cell-size/routing coupling sensitivity. Further numerical verification and measured retention properties are needed before design use.'
for r in fine:
 b=next(q for q in runs if q['case']==r['case']);validation+=f' The {r["case"]} peak changes from {b["peak_receiving_cfs"]:.4f} to {r["peak_receiving_cfs"]:.4f} cfs at 0.625 s.'
def insert(text,key,value):
 pattern=r'<!-- START '+key+r' -->.*?<!-- END '+key+r' -->'
 if not re.search(pattern,text,re.S):
  assert key=='VALIDATION' and '## What to look for' in text
  text=text.replace('## What to look for','<!-- START VALIDATION -->\n<!-- END VALIDATION -->\n\n## What to look for')
 return re.sub(pattern,lambda _: '<!-- START '+key+' -->\n'+value.rstrip()+'\n<!-- END '+key+' -->',text,flags=re.S)
for p in [ARTICLE/'article.md',ROOT/'docs/manual/tutorials/t10_lid_active_chain.md']:
 t=insert(p.read_text(),'RESULTS_TABLE',table);t=insert(t,'VALIDATION',validation)
 t=re.sub(r'(?<=engine records approximately )[\d,]+(?= ft³ of emergency-weir discharge)',f'{runs[4]["bypass_ft3_engine"]:,.0f}',t)
 t=re.sub(r'(?<=two-storm run conveys approximately )[\d,]+(?= ft³)',f'{runs[4]["bypass_ft3_engine"]:,.0f}',t)
 t=re.sub(r'(?<=that volume rises to approximately )[\d,]+(?= ft³)',f'{runs[5]["bypass_ft3_engine"]:,.0f}',t)
 t=re.sub(r'(?<=stuck-closed run conveys approximately )[\d,]+(?= ft³)',f'{runs[5]["bypass_ft3_engine"]:,.0f}',t)
 if p.name=='article.md':
  marker='## Longer detention can help treatment—and change the risk'
  # Historical comparison is labeled and never presented as independent validation.
  block='### Comparing formulations, with explicit limits\n\n'+comparison+'\nThe earlier column uses legacy exponential drainage and Green–Ampt entry. Richards adds explicit, uncalibrated retention/specific-storage properties and has different initial water ownership; the inter-facility head guards also use surface-ponding thresholds. These are illustrative configurations, not a calibrated equivalence experiment or proof that either formulation is superior. The Richards grid/timestep sensitivity below limits quantitative comparisons.\n\n'
  if '### Comparing formulations, with explicit limits' not in t:t=t.replace(marker,block+marker)
 else:
  t=t.replace('- Timed holding delays tracer export relative to the free-outlet case. Here its receiving-outlet peak is lower, but emergency bypass is larger.','- Timed holding changes tracer export and mass fate; it does not guarantee a lower peak.\n  These runs remain sensitive to routing step and cell count.')
 p.write_text(t)
(ARTICLE/'linkedin-article.md').write_text((ARTICLE/'article.md').read_text())
report='| Case | Step (s) | Water error (%) | Reactive error (%) | Tracer error (%) | Sampled V_BR peak (cfs) | Reacted (%) | Bypass (ft³) |\n|---|---|---|---|---|---|---|---|\n'
for r in allruns:report+=f'| {r["case"]} | {r["step_seconds"]:g} | {r["flow_error_percent"]:.3g} | {r["reactive_error_percent"]:.3g} | {r["tracer_error_percent"]:.3g} | {r["peak_receiving_cfs"]:.4f} | {r["reacted_percent"]:.2f} | {r["bypass_ft3_engine"]:.1f} |\n'
notes=f'''# Validation and reproducibility — Richards distributed LID examples

Updated October 5, 2026. The article now explicitly selects **Richards 1D**. The failed two-storm
1.25-s run is recorded in `results/richards/failure.json` and `failed/`.
The prior gravity/front matrix is retained under `results/existing-model/`;
its 20 case/step combinations and 241-test evidence do not validate Richards.
The current checkout includes other development work; the tested binary hash
identifies the implementation actually loaded, rather than implying a clean
HEAD build. Source/model hashes and native reports accompany the CSV samples.

Library: `{s["library_filename"]}`

SHA-256: `{s["library_sha256"]}`

## Actual checks and interpretation limits

{validation}

The tighter options are atol = 1e-9, rtol = 1e-7, compared with 1e-7/1e-5.
Material properties are explicit synthetic van Genuchten–Mualem laws, not
calibrated media or defaults. Specific storage is a physical input, not a
solver stabilization parameter. The first four strategies use the same 1.25-s routing step. The second-storm
and failure-mode mass bars identify their separately tested steps. Sampled peaks and tracer timing use 30-second live-API
samples; cumulative bypass uses engine statistics. This sampling can miss
brief instantaneous peaks and is not a residence-time distribution.

### Current test failure, retained rather than hidden

`LidNodes.RichardsPhysicalStateIsConsistentAcrossInputUnits` fails its exact
storage-equivalence assertion: 65.005715 versus 64.9984766687 ft³, a difference
of 0.0072383313 ft³ (about 0.0111%). The existing functional-storage geometry
preserves SWMM's truncated SI volume conversion (0.02832 m³/ft³), while the
fixture authors its SI area using exact 0.3048². This mismatch requires a
separate unit-convention/fixture correction; it is not evidence of exact unit
parity. These article runs use CFS exclusively. The complete failing test
output is preserved in `results/richards/lid_node_tests.json`.

Richards kernel: {kernel['tests']} tests, {kernel['failures']} failures.
LID-node integration: {nodes['tests']} tests, {nodes['failures']} failure.
The tests include storage/retention inversion, hydrostatic equilibrium,
capillary rise, layered resistance, tolerance refinement, batch rollback,
port budgets, input/API/restart and pollutant routing. Broader application
verification remains necessary; passing balances do not establish accuracy.

## Reproduce

From the GUI checkout with a rebuilt current native library:

```text
python3 docs/articles/lid-storage-node/figures/generate_examples.py --richards
python3 docs/articles/lid-storage-node/figures/run_examples.py --library /absolute/path/to/libopenswmm.engine.dylib --decks docs/manual/tutorials/models/lid_richards_chain --formulation richards --cases 01_passive_free 02_passive_backwater 03_timed_hold 04_head_guard --steps 1.25 --output docs/articles/lid-storage-node/results/richards/baseline
python3 docs/articles/lid-storage-node/figures/run_examples.py --library /absolute/path/to/libopenswmm.engine.dylib --decks docs/manual/tutorials/models/lid_richards_chain --formulation richards --cases 02_passive_backwater 04_head_guard --steps 0.625 --output docs/articles/lid-storage-node/results/richards/routing-fine
python3 docs/articles/lid-storage-node/figures/generate_examples.py --richards --cells 4 --destination /tmp/lid-richards-cells4
python3 docs/articles/lid-storage-node/figures/generate_examples.py --richards --cells 16 --destination /tmp/lid-richards-cells16
python3 docs/articles/lid-storage-node/figures/generate_examples.py --richards --atol 1e-9 --rtol 1e-7 --destination /tmp/lid-richards-tight
```

Run each generated sensitivity deck's `04_head_guard.inp` at 1.25 s with
`run_examples.py`, recording outputs in `results/richards/cells4`, `cells16`
and `tight`. Run the second-storm case separately at its illustrated step and at 2.5 s,
and the stuck-closed case at 5/2.5/1.25 s. The second-storm 1.25-s failure is
not treated as successful evidence. `publish_richards_runs.py` consolidates
the checkpointed summaries and prepares matching downloads. The totals,
sampled profiles and reports are recorded below. `input_sha256` is recorded for every deck in the summaries.

Run `test_engine_richards_column` and `test_engine_lid_nodes` with JSON output
and copy the records as `kernel_tests.json` and `lid_node_tests.json` beside
this matrix. The current failure must remain disclosed until corrected.
Then:

```text
python3 docs/articles/lid-storage-node/figures/run_richards_cycle.py --library /absolute/path/to/libopenswmm.engine.dylib
python3 docs/articles/lid-storage-node/figures/publish_richards_runs.py
python3 docs/articles/lid-storage-node/figures/finalize_richards_text.py
python3 docs/articles/lid-storage-node/figures/make_figures.py
python3 docs/articles/lid-storage-node/figures/make_richards_figures.py
python3 docs/articles/lid-storage-node/figures/stage_site.py
```

GIF playback uses 24 centiseconds/frame over 64 source frames: 15.36 s per
cycle, versus 10.24 s previously. Simulation timestamps are unchanged.
Static alternatives and reduced-motion controls remain available. The
orifice GIF is analytical; the pressure/moisture cycle is native output.
The separate six-minute fixture passes at 0.1/0.05 s and does not assert
complete saturation of all media cells.

## Complete Richards run matrix

{report}

## Storage, coupling and pollutant scope

The porous column owns complete pore/elastic water and local pressure;
surface ponding belongs to the network reservoir. Signed face fluxes use
arithmetic conductivity within a material and half-cell resistances across
materials. Adaptive BDF1 accepts two half steps. Surface entry is integrated
with Richards, not Green–Ampt. Network port trials reset their budgets and
commit accepted transfers once. Routing-step sensitivity tests this split
coupling separately from the ODE solver.

Each porous cell owns its pollutant inventory, including saturation.
Directional accepted transfers debit resident donor mass and credit the
recipient; evaporation retains solute. Layer-exit removal is not repeated
at each numerical face. The 2/day decay assumption implies an 8.3-hour
half-life; longer exposure gives more reaction by construction.

Closed bottoms are used here. The kernel has a prescribed-head verification
boundary, but runtime aquifer-bed coupling is pending and rejected explicitly.
Water-age, heat and MSX porous-cell adapters and saved profile histories are
also pending. The sampled profile figures use the live API.

Primary scientific references are cited in the article. Local code evidence
belongs in this companion: `RichardsColumn.cpp`, `LidNodeRichards.cpp`,
`LidNode.cpp`, `LidNodeTreatment.cpp`, hydraulic ports and hotstart code.
The hydraulic and water-quality references document the formulations; the
GUI T10 tutorial documents explicit properties, controls and test workflow.
'''
(ARTICLE/'validation.md').write_text(notes)
(RESULTS/'publication_metrics.json').write_text(json.dumps({'library_sha256':s['library_sha256'],'first_storm_illustration_step_seconds':1.25,'second_storm_illustration_step_seconds':runs[4]['step_seconds'],'cells_per_material':8,'matrix_runs':len(allruns),'current_regression_tests':kernel['tests']+nodes['tests'],'current_regression_failures':nodes['failures'],'grid_runs':[mesh4,runs[3],mesh16],'ode_tight_run':tight,'routing_fine_runs':fine},indent=2)+'\n')
print('Published',len(allruns),'Richards runs and the actual numerical/test limitations.')
if (ARTICLE/'results/partition-fix/after/summary.json').exists():
 runpy.run_path(str(HERE/'finalize_partition_text.py'), run_name='__main__')
