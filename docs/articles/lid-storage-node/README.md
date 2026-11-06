# Distributed LID article package

The legacy retained/mobile partition correction is reproduced in
`results/partition-fix/`: two original runs, twelve corrected runs at
0.5/0.25 s, separate binary hashes, native reports, compressed sampled CSVs,
head-increment diagnostics and regression results. Earlier legacy performance
values are withdrawn from the article comparison. Run
`python3 figures/finalize_partition_text.py` to synchronize the corrected
comparison and diagnostics in both article sources, validation and T10.
The Richards finalizer also applies this correction when evidence is present.
The original Richards figures retain their own binary/run provenance.

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

GIFs run about 15 seconds and loop. Website controls switch between animation
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

The updated article explicitly selects the optional **Richards 1D** model.
`figures/generate_examples.py --richards` produces its six decks in
`docs/manual/tutorials/models/lid_richards_chain/`; uniquely named
`richards_*.inp` downloads are copied beside the GUI's other tutorial models.
`figures/run_examples.py --decks ... --formulation richards --output ...`
records 30-second pressure/moisture profiles, native continuity and mass
budgets, input hashes and binary provenance. The published figures use
eight cells/material and a common 1.25-s routing step. Cell-count, routing
and ODE-tolerance sensitivity is disclosed explicitly; these are diagnostic
examples, not converged controller-performance predictions.

`results/richards/` holds the new matrix and current regression JSON.
`results/existing-model/` preserves the previously published gravity/front
results as a separately labeled historical comparison. Its 241 passing tests
do not validate Richards. One current US/SI exact-storage-equivalence fixture
is failing; see `validation.md` for the recorded discrepancy and limits.

`run_richards_cycle.py` generates the separate six-minute fixture and native
pressure/moisture animation data. `make_richards_figures.py` generates the
new storage/flux/constitutive-comparison graphics and the slowed pressure
and analytical-orifice GIFs. `make_figures.py` draws the network and mass
fate from the Richards runs. All four GIF generators use 24 centiseconds
per source frame: 15.36 s/cycle, with unchanged simulation timestamps.
`finalize_richards_text.py` publishes actual evidence and synchronizes the
article/manual tables. The old `finalize_text.py` describes the earlier
gravity implementation and must not overwrite the Richards publication.

Reproduction commands and all numerical limitations are in `validation.md`.
No website publication or LinkedIn posting occurs during local staging.
