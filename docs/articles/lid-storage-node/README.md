# Distributed LID article package

- `article.md`: full website article, Markdown first, with primary references.
- `linkedin-article.md`: identical full article for LinkedIn, derived from `article.md`.
- `linkedin-post.md`: companion announcement post.
- `assets/`: four GIF animations, nine PNG posters/plots and editable SVGs.
- `validation.md`: solver corrections, test evidence and interpretation limits.
- `results/`: sampled CSVs, reports, main run provenance and overflow-statistics check.
- `figures/`: example generation, validation, rendering and website staging scripts.

The same six native INP files are in
`docs/manual/tutorials/models/lid_active_chain/`. T10 is integrated into the
GUI manual and Doxygen download assets.

The real website checkout is the sibling `hydrocouple.github.io`. Its article
source and assets use the site's existing inline article/index conventions.
`python3 figures/stage_site.py` prepares `/tmp/hydrocouple-lid-site` without
writing that checkout, tests idempotence and verifies that every existing
article block remains unchanged. The deployed `build.py` updates only this
article and its index entry, and regenerates the identical LinkedIn Markdown
and HTML preview. Edit `article.md` for changes that should appear in both
articles; the staging and results scripts synchronize the local LinkedIn draft.

GIFs run about 10 seconds and loop. Website controls switch between animation
and a static poster, honoring reduced-motion preferences. Use the individual
GIF assets when placing images into a LinkedIn article; keep the static PNGs
available as alternatives. The local drafts are not posted to LinkedIn or
published to the live website.

The formulation section includes water-store, interlayer-flux, Richards-approximation and
pollutant-reaction diagrams plus an analytical orifice animation. Reproduce
them with `python3 figures/make_formulation_figures.py`. The network/fate
figures use the finest accepted run for each case (0.025 s for the
first-storm comparisons; additional refinement for the second storm) and record perched ponding
separately from the mobile water-table head. See `validation.md` for the
nonzero-invert crest correction and routing-step sensitivity.

The revised kernel uses legacy exponential conductivity and modified
Green–Ampt entry, with finite-zone history reconciliation for backwater.
`models/lid_resaturation.inp` and `results/resaturation_cycle.csv` reproduce
the supplemental reversal–resaturation–recession–second-event animation;
run the engine's `LidNodes.RoutedReversalResaturationRecessionAndSecondEventConserve`
test to regenerate its one-second CSV. Provenance is in
`results/resaturation_cycle.json`. The six 24-hour decks remain separate.
