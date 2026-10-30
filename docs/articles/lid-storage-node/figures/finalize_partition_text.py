"""Publish the independently reported legacy retained/mobile partition correction.

Keep the original records, replace performance comparisons with corrected
runs, and distinguish these results from the separately hashed Richards runs.
"""
from pathlib import Path
import json, re

HERE = Path(__file__).resolve().parent
ARTICLE = HERE.parent
ROOT = HERE.parents[3]
RESULTS = ARTICLE / 'results/partition-fix'
after = json.loads((RESULTS / 'after/summary.json').read_text())
before = json.loads((RESULTS / 'before/summary.json').read_text())
diagnostics = json.loads((RESULTS / 'diagnostics.json').read_text())
tests = json.loads((RESULTS / 'after_tests.json').read_text())
assert len(after['runs']) == 12 and all(r['accepted'] for r in after['runs'])
assert after['library_sha256'] != before['library_sha256']
richards = json.loads((ARTICLE / 'results/richards/summary.json').read_text())
labels = {'01_passive_free': 'Passive / free outlet',
          '02_passive_backwater': 'Passive / backwater',
          '04_head_guard': 'Hold + head guard'}
table = '| First-storm strategy | Corrected gravity/front sampled peak (cfs; 0.5 s) | Richards sampled peak (cfs; 1.25 s) | Corrected gravity/front reacted | Richards reacted |\n|---|---|---|---|---|\n'
for case, label in labels.items():
    old = next(r for r in after['runs'] if r['case'] == case and r['step_seconds'] == .5)
    new = next(r for r in richards['runs'] if r['case'] == case)
    table += f'| {label} | {old["peak_receiving_cfs"]:.4f} | {new["peak_receiving_cfs"]:.4f} | {old["reacted_percent"]:.1f}% | {new["reacted_percent"]:.1f}% |\n'
comparison = ('### Comparing formulations, with explicit limits\n\n' + table +
    '\nThe gravity/front results above were rerun after correcting the retained/mobile partition bug described below; the earlier comparison values are withdrawn. This column uses legacy exponential drainage and Green–Ampt entry. Richards adds explicit, uncalibrated retention/specific-storage properties and has different initial water ownership; the inter-facility head guards also use surface-ponding thresholds. The routing steps differ and are identified in the table. These are illustrative configurations, not a calibrated equivalence experiment or proof that either formulation is superior. The Richards grid/timestep sensitivity below limits quantitative comparisons.\n\n')
article = ARTICLE / 'article.md'
text = article.read_text()
text, count = re.subn(r'### Comparing formulations, with explicit limits.*?(?=## Longer detention)', lambda _: comparison, text, flags=re.S)
assert count == 1
# Correct the legacy caption on the current Richards animation.
text = text.replace('Blue fill shows mobile water; teal shows perched surface ponding.',
    'Blue shading shows porous-cell moisture; teal marks surface ponding and saturated cells.')
article.write_text(text)

paragraph = (
    'Independent testing identified another problem in the earlier gravity/front model: '
    'an inlet above the reported water table could keep filling a partially submerged cell as retained moisture, '
    'while that cell stopped free drainage as soon as its bottom was submerged. '
    'At porosity, its remaining mobile capacity collapsed and its water inventory was abruptly reclassified. '
    'Water was conserved, but the resulting head and hydrograph were wrong. '
    'The reproduced free-outlet case rose 0.993 ft in 30 seconds while total water fell from about 517 to 514 ft³; '
    'the backwater case rose 0.631 ft at about hour 2.675. '
    '**The correction keeps partially submerged cells draining retained excess and routes incoming water to mobile storage when the receiving cell intersects the water table**, '
    'even if its inlet is above that table. Only fully submerged cells skip free drainage. '
    'This is the reduced model’s cell-level coupling approximation, not a resolved local Richards pressure field. '
    'After correction, the largest 30-second rise in B is about 0.0031 ft in both examples, and the free-outlet case no longer has the artificial reversal into A. '
    'All six legacy examples passed water/tracer/reactive balances at 0.5 and 0.25 s (12 completed runs); '
    f'the current LID-node suite passes {tests["tests"] - tests["failures"]} of {tests["tests"]} tests, '
    'including three new regressions for partial-cell drainage, inlet ownership and per-step routed-head continuity. '
    'The previously reported Richards US/SI fixture still fails. '
    'These results replace the affected legacy performance comparison; the Richards figures retain their separately identified run provenance. '
    '**Continuity is necessary, but head continuity and a physically consistent storage partition must also be checked.**')

def update_block(path, before_heading):
    text = path.read_text()
    block = '<!-- START PARTITION_VALIDATION -->\n' + paragraph + '\n<!-- END PARTITION_VALIDATION -->\n\n'
    pattern = r'<!-- START PARTITION_VALIDATION -->.*?<!-- END PARTITION_VALIDATION -->\s*'
    if re.search(pattern, text, re.S):
        text = re.sub(pattern, lambda _: block, text, flags=re.S)
    else:
        assert before_heading in text
        text = text.replace(before_heading, block + before_heading, 1)
    text = text.replace('The current engine passed 9 Richards kernel tests and 49 of 50 LID-node integration tests;',
                        'The recorded Richards publication verification passed 9 kernel tests and 49 of 50 LID-node integration tests;')
    path.write_text(text)

update_block(article, '## Configure the same example in SWMMVis')
update_block(ROOT / 'docs/manual/tutorials/t10_lid_active_chain.md', '## What to look for')
update_block(ARTICLE / 'validation.md', '## Reproduce')
(ARTICLE / 'linkedin-article.md').write_bytes(article.read_bytes())
print('Published corrected legacy comparison, continuity diagnostics and matching LinkedIn source.')
