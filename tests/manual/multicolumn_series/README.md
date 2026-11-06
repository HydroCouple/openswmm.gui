# Manual test fixtures — multi-column series files

Test data for the multi-column CSV/TSV/TSF feature (time series editor column
picker, rain-gage "Rain File Column", and `path:column` persistence).

Regenerate any time with `python3 make_fixtures.py`.

## The files

| File | What it is | Use it to test |
|---|---|---|
| `rain_multicolumn.csv` | 4 rain columns, 37 rows, 5-min steps, `;` comment lines | the main column picker |
| `rain_multicolumn.tsv` | same data, tab-delimited | delimiter sniffing |
| `rain_multicolumn.tsf` | same data, PCSWMM 3-row `IDs:` header, 12-hour AM/PM stamps | `.tsf` support |
| `rain_headerless.csv` | first line is **data**, no header row | the headerless rules (below) |
| `rain_quoted_header.tsv` | tab-delimited, header names contain commas inside quotes | quote-aware delimiter sniff |

## Why the columns look the way they do

The four columns have deliberately **different shapes**, so picking a column is
unmistakable in the preview chart:

| Column | Shape | Peak |
|---|---|---|
| `RG_NORTH` | early peak, dry after 01:00 | 0.90 at 00:30 |
| `RG_SOUTH` | nothing until 01:00, late peak | 1.20 at 02:15 |
| `RG_EAST` | flat 0.20 the whole storm | — |
| `RG_WEST` | double peak with a dry gap 01:10–01:50 | 0.75 at 02:25 |

**If the chart shows the same early-peak shape no matter which column you pick,
the selector is being ignored and column 1 silently used** — that is the bug
these shapes are built to expose.

Units are in/hr (intensity). Timestamps run 2007-01-01 00:00 → 03:00.

## What to check

1. **Column picker.** Time Series editor → new series → Source = External File →
   Browse → `rain_multicolumn.csv`. The combo should list all four names. Step
   through them; the chart shape must change each time per the table above.
2. **`.tsf`.** Same with `rain_multicolumn.tsf` — `*.tsf` must appear in the file
   filter, `12:00:00 AM` must land at 00:00 and `1:30:00 PM` at 13:30.
3. **Series switch.** With a file-backed series selected, create a second inline
   series, click it, then click back. The combo must be **repopulated, enabled,
   and showing the column the series actually holds** (regression B3).
4. **Headerless.** Browse to `rain_headerless.csv`. Expect **36** points, not 37 —
   the first line is spent as the header row exactly as the engine spends it. The
   combo shows `col_1`…`col_4` but the stored selector stays **empty**, and
   picking `col_2` is **refused** with a message (the engine cannot resolve a
   fabricated name).
5. **Stale column.** Bind `RG_SOUTH`, then edit the CSV on disk to rename that
   column, and reopen the series. Expect **no points**, a status message naming
   the missing column, and a combo item `RG_SOUTH (not in file)`. The preview and
   the run must agree — that agreement is the point.
6. **Persistence round-trip.** With a file-backed series named e.g. `RAIN_TS` on
   column `RG_SOUTH`, save the project and inspect the `.inp`: `[TIMESERIES]`
   must read `RAIN_TS FILE "…/rain_multicolumn.csv:RG_SOUTH"`. Reopen — the
   series must come back as External File on `RG_SOUTH`.
7. **Rain gage + MVC sync.** Select a FILE-source rain gage. Set "Rain File
   (path)" to `rain_multicolumn.csv`, then set "Rain File Column" → Station ID
   greys out (format flips to USER_CSV). Open the Attribute Table on Rain Gages:
   the path cell has a browse button, the column cell is a combo listing the four
   names, and both show what you just set. Change the column in the table → the
   Property Browser row must update.
8. **Quoted headers.** `rain_quoted_header.tsv` must sniff as **tab**-delimited
   and yield two columns (`Gage A, north` / `Gage B, south`), not be split on the
   commas inside the quoted names.

## Verified

These fixtures were run through the shipping `readHeaders`/`readColumn` before
being committed here: all four CSV columns return 37 points with the peaks
tabulated above; the TSV and TSF return values identical to the CSV;
case-insensitive matching works; the headerless file returns 36 points and
refuses `col_2`; the quoted-header file sniffs as tab; and an unknown column
name is refused rather than falling back to column 1.
