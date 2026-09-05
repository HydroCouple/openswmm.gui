# SWMMVis user manual — authoring notes

The manual is a set of Doxygen Markdown pages under `docs/manual/`. It is built
by `docs/Doxyfile` together with the API reference and published by the
`documentation.yml` workflow. `docs/manual/manual.md` is the table of contents
(`@page user_manual`); every chapter is a `\subpage` of it.

This file is *not* part of the built site (no `@page` header).

## Layout

```
docs/manual/
  manual.md                 TOC — @page user_manual
  NN_topic.md               Parts I–IV, one @page per chapter
  tutorials/tNN_topic.md    Part V — @page tutorial_*
  tutorials/models/         small .inp models referenced by \include / \snippet
  appendices/aNN_topic.md   @page manual_* appendices
  images/                   screenshots referenced by \fig{...}
```

## Page header

Every chapter starts with a Doxygen page command on line 1:

```
@page manual_layers 07 — Layers and Data Sources
```

The page id (`manual_layers`) is what other chapters link to with
`\ref manual_layers` or `\ref manual_layers "the Layers chapter"`. Ids are
listed in `manual.md`; do not rename one without updating every `\ref`.

## Chapter skeleton

```
## What you'll do
## Where to find it
## Step-by-step            (reference material, sub-headed per dialog/tool)
## Tips and gotchas
## Related
```

Tutorials use: **Goal / Capabilities exercised / Files / Steps / What to look
for / Variations / Related**.

## Figures

Figures are declared with two aliases defined in `docs/Doxyfile`
(`ALIASES`). While a screenshot has not been captured yet use the placeholder
form; when the PNG exists in `docs/manual/images/`, change `\figtodo` to
`\fig` — nothing else changes.

```
\figtodo{07_layers_panel.png, The Layers panel with a basemap, a DEM and a model layer}
\fig{07_layers_panel.png, The Layers panel with a basemap, a DEM and a model layer}
```

Naming: `NN_short_description.png` where `NN` is the chapter number
(`t03_…` for tutorials, `a02_…` for appendices). Capture at 2× scale on a
light theme, crop to the dialog or the relevant region, and keep the width
under 1600 px. Because the caption is the second alias argument it must not
contain a comma; use a semicolon or a dash instead.

The list of every placeholder still to capture:

```
grep -rn "figtodo\|videotodo" docs/manual
```

## Videos

YouTube embeds use the same pattern:

```
\videotodo{Adding an XYZ basemap and reprojecting the project}
\video{dQw4w9WgXcQ, Adding an XYZ basemap and reprojecting the project}
```

`\video{ID, caption}` expands to a responsive 16:9 iframe pointing at
`https://www.youtube.com/embed/ID`.

## Including model snippets

`EXAMPLE_PATH` covers `examples/` and `docs/manual/tutorials/models/`, so a
chapter can quote an input file directly:

```
\include site_drainage/site_drainage_model.inp
\snippet street_inlet_junction.inp inlet_junctions
```

For `\snippet`, wrap the block in the `.inp` with `//! [tag]` … `//! [tag]`
comment lines — SWMM ignores lines starting with `;`, so use
`;//! [inlet_junctions]` on its own line before and after the block.

## Style

- Name UI elements exactly as they appear in the application (`Add Vector Data…`,
  `Zoom To Selection`). Menu paths use `→`.
- Bold UI labels, `monospace` for SWMM keywords, file names and paths.
- Tables for reference material (one row per control / option / column).
- Say what a control *does to the model* (which `.inp` section or `[OPTIONS]`
  key it writes) — the engine manuals cover the numerics.
- Do not document planned features as if they existed; if a control is
  present but inert, say so.
