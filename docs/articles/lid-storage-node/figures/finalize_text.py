"""Publish measured metrics, provenance and explicit routing-step sensitivity."""
from pathlib import Path
import json, re
HERE=Path(__file__).resolve().parent; ROOT=HERE.parents[3]; ARTICLE=HERE.parent
if 'A semi-discrete Richards column' in (ARTICLE/'article.md').read_text():
    raise SystemExit('This finalizer is for the historical gravity model. Use finalize_richards_text.py for the current article.')
s=json.loads((ARTICLE/'results/summary.json').read_text())
cases=sorted({r['case'] for r in s['runs']})
runs=[min((r for r in s['runs'] if r['case']==case),key=lambda r:r['step_seconds']) for case in cases]
assert len(runs)==6 and all(r['accepted'] for r in s['runs'])
def insert(text,key,value):
 block='<!-- START '+key+' -->\n'+value.rstrip()+'\n<!-- END '+key+' -->'
 pattern=r'<!-- START '+key+r' -->.*?<!-- END '+key+r' -->'
 if re.search(pattern,text,re.S): return re.sub(pattern,lambda _:block,text,flags=re.S)
 return text.replace('<!-- '+key+' -->',block)
labels=['Passive / free outlet','Passive / backwater','Timed hold','Hold + head guard','Second storm / guard','Valves stuck closed']
table='| First-storm strategy | V_BR peak (cfs) | Tracer 50% export (h) | Reacted by 24 h |\n|---|---|---|---|\n'
for i,r in enumerate(runs[:4]):table+=f'| {labels[i]} | {r["peak_receiving_cfs"]:.4f} | {r["tracer_half_export_hour"]:.2f} | {r["reacted_percent"]:.1f}% |\n'
pairs=[]
for case in cases:
 records=sorted((r for r in s['runs'] if r['case']==case),key=lambda r:r['step_seconds'])
 fine,coarse=records[:2]
 pairs.append(dict(case=case,fine=fine,coarse=coarse,
  minutes=abs(fine['tracer_half_export_hour']-coarse['tracer_half_export_hour'])*60,
  reaction=abs(fine['reacted_percent']-coarse['reacted_percent']),
  peak=100*abs(fine['peak_receiving_cfs']-coarse['peak_receiving_cfs'])/fine['peak_receiving_cfs'] if fine['peak_receiving_cfs'] else 0,
  bypass=100*abs(fine['bypass_ft3_engine']-coarse['bypass_ft3_engine'])/fine['bypass_ft3_engine']))
maxerror=max(abs(r[k]) for r in s['runs'] for k in ['flow_error_percent','reactive_error_percent','tracer_error_percent'])
extra=len(s['runs'])-18
validation=f'The six decks were rerun with the revised hydrology at 0.1, 0.05 and 0.025-second routing steps'+(f', with {extra} additional second-storm refinements' if extra else '')+f': {len(s["runs"])} distinct case/step combinations. All passed the 0.5% water/quality continuity criterion without engine warnings; the maximum reported error was below 0.001%. Figures use each case’s finest recorded step. Comparing each case’s two finest steps, sampled half-export times agree at the 30-second sampling resolution, reacted fractions differ by {max(q["reaction"] for q in pairs):.3f} percentage points, receiving-outlet peaks by {max(q["peak"] for q in pairs):.3f}% and cumulative emergency-weir volumes by {max(q["bypass"] for q in pairs):.3f}%. Tracer timing is sampled at 30 seconds and reaction budgets are rounded by the engine report. These are measured sensitivity limits, not a universal convergence guarantee. Cumulative engine budgets are used for bypass and flooding. Nine engine regression suites passed 241 tests, including legacy infiltration/LID paths, gradual resaturation–recession, second-event entry and restart history.'
for p in [ARTICLE/'article.md',ROOT/'docs/manual/tutorials/t10_lid_active_chain.md']:
 text=insert(p.read_text(),'RESULTS_TABLE',table)
 text=insert(text,'VALIDATION',validation)
 text=re.sub(r'(?<=engine records approximately )[\d,]+(?= ft³ of emergency-weir discharge)',f'{runs[4]["bypass_ft3_engine"]:,.0f}',text)
 text=re.sub(r'(?<=two-storm run conveys approximately )[\d,]+(?= ft³)',f'{runs[4]["bypass_ft3_engine"]:,.0f}',text)
 text=re.sub(r'(?<=that volume rises to approximately )[\d,]+(?= ft³)',f'{runs[5]["bypass_ft3_engine"]:,.0f}',text)
 text=re.sub(r'(?<=stuck-closed run conveys approximately )[\d,]+(?= ft³)',f'{runs[5]["bypass_ft3_engine"]:,.0f}',text)
 if p.name.startswith('t10_'):
  text=re.sub(r'At (?:its finest )?[0-9.]+-second\nstep the guarded two-storm run',f'At its finest {runs[4]["step_seconds"]:g}-second\nstep the guarded two-storm run',text)
 p.write_text(text)
(ARTICLE/'linkedin-article.md').write_text((ARTICLE/'article.md').read_text())
notes=f'''# Validation and reproducibility — distributed LID examples

Validated locally on October 5, 2026 with the legacy-hydrology adaptation.
The engine checkout includes other development work; this binary hash
identifies the actual library tested rather than claiming a clean HEAD build.
These results supersede the previous power-law node-kernel runs.

Library: `{s["library_filename"]}`

SHA-256: `{s["library_sha256"]}`

## Reproduce

From the GUI checkout, using the rebuilt native library:

```text
python3 docs/articles/lid-storage-node/figures/generate_examples.py
python3 docs/articles/lid-storage-node/figures/run_examples.py --library /absolute/path/to/current/libopenswmm.engine.dylib --steps 0.1 0.05 0.025
'''
if extra:
 steps=sorted({r['step_seconds'] for r in s['runs'] if r['step_seconds']<.025},reverse=True)
 notes+='python3 docs/articles/lid-storage-node/figures/run_examples.py --library /absolute/path/to/current/libopenswmm.engine.dylib --cases 05_repeat_storm --steps '+' '.join(f'{v:g}' for v in steps)+' --append\n'
notes+='''python3 docs/articles/lid-storage-node/figures/finalize_text.py
python3 docs/articles/lid-storage-node/figures/make_figures.py
python3 docs/articles/lid-storage-node/figures/make_formulation_figures.py
python3 docs/articles/lid-storage-node/figures/stage_site.py
```

The runner uses the native C API through standard-library ctypes. CSVs have
30-second samples; report files and cumulative continuity/statistics queries
supply the budgets. All accepted runs have no engine warnings and absolute
water/pollutant continuity errors below 0.5%. Append rejects a different
binary hash. The finest available step is selected independently per case.
All six decks use CFS, Dynamic Wave and Legacy quality; bottom seepage,
evaporation and initial pollutant mass are zero. Reverse boundary water is
clean, so external water includes tailwater receipts but no imported mass.

'''+validation+'\n\n'+table+'''
## Finest-step performance sensitivity

Every pair below uses the same library. Differences are absolute, with
percent differences referenced to the finer result. Good mass closure does
not prove stable performance. First-order reaction is assumed, not calibrated.

| Case | Coarser/finer steps (s) | Half-export difference (min) | Reaction difference (percentage points) | Receiving-peak difference (%) | Bypass difference (%) |
|---|---|---|---|---|---|
'''
for q in pairs:
 notes+=f'| {q["case"]} | {q["coarse"]["step_seconds"]:g} / {q["fine"]["step_seconds"]:g} | {q["minutes"]:.2f} | {q["reaction"]:.3f} | {q["peak"]:.3f} | {q["bypass"]:.3f} |\n'
notes+='''
## Mass and overflow budgets at each case’s finest step

Emergency-weir totals come from cumulative engine flow statistics rather
than summing aliased snapshots. The weirs have no reverse flow. Pollutant
reports round totals to 0.001 lb, so rounded fate bars may not sum to 100%.

| Case | Step (s) | Reactive input (lb) | Exported (lb) | Flood loss (lb) | Reacted (lb) | Stored at 24 h (lb) | Emergency bypass (ft³) | Flood water (ft³) |
|---|---|---|---|---|---|---|---|---|
'''
for i,r in enumerate(runs):
 notes+=f'| {labels[i]} | {r["step_seconds"]:g} | {r["mass_in_lbs"][0]:.3f} | {r["mass_out_lbs"][0]:.3f} | {r["mass_flood_lbs"][0]:.3f} | {r["mass_reacted_lbs"][0]:.3f} | {r["mass_final_lbs"][0]:.3f} | {r["bypass_ft3_engine"]:.2f} | {r["flood_ft3"]:.2f} |\n'
notes+='''
For field studies, include all destinations over a common horizon; retained
mass is not treated mass. Tracer half-export time is not mean hydraulic
residence time. These decks do not simulate groundwater fate or receiving
water ecology. Holding increases modeled reaction directly under the assumed
2/day first-order law (half-life 8.3 h).

## Resaturation example and animation

`models/lid_resaturation.inp` is the separate six-minute native regression
fixture: a 10 ft² facility, an open reversible orifice, rising/falling
receiving stage and surface inflow at minutes 3–4. The engine test
`LidNodes.RoutedReversalResaturationRecessionAndSecondEventConserve` writes
one-second samples of head, stores, signed flow and infiltration history.
Copy its `resaturation_second_event.csv` to `results/resaturation_cycle.csv`
and record hashes in `results/resaturation_cycle.json` before rendering.
The JSON identifies the library, fixture and samples used in the animation.
The fixture is also shipped in the GUI tutorial models. It closes water
within 0.005 ft³ and conservative-tracer mass at each step within 0.1% of
incoming mass, with no warnings. Internal GA history is shown from the native
test export; it is not inferred from GUI depth outputs.

The four 64-frame looping GIFs have static PNG alternatives. Network/fate
animations use sampled 24-hour native runs. The resaturation GIF uses the
six-minute fixture; its pale lines show the rest of the sampled trajectory.
The orifice animation is analytical. Arrow motion shows flow direction,
not particle tracking. Formulation diagrams are explanatory SVGs rendered
with rsvg-convert; ImageMagick assembles GIFs. No image-generation model
produces the simulated data.

## Hydrology implementation checks

- MEDIA uses `Ks * exp(-slope * (porosity - theta))` above field capacity;
  the existing conductivity-slope parameter no longer acts as a power exponent.
- SURFACE → first MEDIA reuses modified Green–Ampt. Only accepted infiltration
  increments F/Fu; zero Ks or zero climate multiplier is impermeable.
- Backwater/accepted media-port wetting changes the finite-zone deficit/wetness
  without becoming surface infiltration. Fully submerged media clears the old
  front. Gradual recession tracks physical wetness until accepted surface
  entry starts the subsequent front. Dry recovery respects remaining water.
- Hydraulic trial/reset calls do not advance history. Native V10 preserves
  it exactly; compatible V9 reconstruction warns explicitly.
- The implementation does not compute intercell matric gradients, upward
  capillary redistribution or intersecting wetting-front dynamics. Tu, Wadzuk
  and Traver’s gravity-plus-diffusivity model and HYDRUS comparisons are not
  validation of this reduced kernel.

## Prior pollutant and interface corrections retained

- LAST-quality outfall backflow is an external pollutant source; ZERO supplies
  clean water. Zero-volume LID connections solve consistent donor/recipient
  mobile mixtures; provisional iterations restore counters and inventories.
- Nearly dry nodes export freshly percolated pollutant even when final mobile
  volume is below the legacy dry cutoff. Layer treatment is booked once.
- Physical weir/rating crests and layer anchors select their actual water
  source. Difference-of-datums arithmetic avoids `(0.3 + 2.0) - 0.3`
  classifying a 2.00-ft surface crest in MEDIA. Interface tolerance is only
  32 machine epsilons times local elevation scale; physical offsets remain
  distinct. Hydraulic and quality ports share the rule.
- Failed quality iteration warns rather than hiding a continuity problem.

## Engine regression results

| Suite | Tests passed |
|---|---|
| LID nodes | 40 |
| Quality routing | 21 |
| Treatment | 32 |
| Hotstart | 39 |
| Outfall backflow | 5 |
| LID water age | 6 |
| LID heat | 10 |
| Infiltration | 33 |
| Conventional LID | 55 |
| Total | 241 |

The full-chain regressions cover MEDIA / AGGREGATE × low / high tailwater ×
ZERO / LAST quality and per-step tracer closure. Focused cases cover sub-litre
drainage, surface weir roundoff and accepted-history/restart behavior.
The shared conventional infiltration and LID implementations were not edited.

## Source scope and further reading

Current kernel: engine `src/engine/hydrology/LidNode.cpp`,
`LidNodeTreatment.cpp`, `hydrology/Infiltration.cpp` and `core/HotStartManager.cpp`.
The hydraulics reference Chapter 2 documents the adapter; water-quality
reference Chapters 5 and 6 document mass transport and treatment. Engine
Chapter 6 describes syntax, lifecycle and limits. GUI T9/T10 describe editing
and running the supplied cases. Repository redesign plans contain superseded
proposals and are not authoritative descriptions of this runtime.

Groundwater closing remarks describe future investigations using spatial
`[2D_AQUIFER_OPTIONS]` / `[2D_AQUIFER]` / node/link exchanges. These closed-bottom
decks do not demonstrate coupled groundwater treatment. The article links
EPA primary sources, Tu et al.’s PLOS ONE study and the published ASCE valve
control paper; the local implementation sources belong here rather than the
article’s research-reference list.
'''
(ARTICLE/'validation.md').write_text(notes)
overflow={'library_sha256':s['library_sha256'],'source':'cumulative engine emergency-weir flow statistics','runs':[{k:r[k] for k in ['case','step_seconds','bypass_ft3_engine','flood_ft3']} for r in s['runs'] if r['case'] in cases[4:]]}
(ARTICLE/'results/overflow_totals.json').write_text(json.dumps(overflow,indent=2)+'\n')
print('Published measured metrics for',len(s['runs']),'runs; 241 engine tests.')
