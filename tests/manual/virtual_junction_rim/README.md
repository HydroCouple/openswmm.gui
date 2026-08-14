# Virtual junction rendering MaxDepth — by-eye check

`vj_rim_demo.inp` is a 200-ft reach split by two virtual junctions, one with
the new optional `[VIRTUAL_JUNCTIONS]` MaxDepth and one without, so a single
profile shows both behaviours next to each other.

| Node | Invert | Ground drawn at | Why |
|---|---|---|---|
| J1   | 100.00 | 110.00 | junction max depth 10.0 |
| VJ_A | 98.75  | 107.75 | declared MaxDepth 9.0 (rendering only) |
| VJ_B | 97.50  | 98.50  | nothing declared → falls back to the pipe crown |
| J2   | 96.25  | 104.25 | junction max depth 8.0 |
| OUT  | 95.00  | 95.00  | outfall, no depth |

## What to look for

1. Open the model, select the path **J1 → OUT**, open the profile plot.
   - The soil runs unbroken over **both** virtual junctions — no manhole
     shaft, no glyph. They are break points inside one pipe, not structures.
   - Over **VJ_A** the ground line passes through 107.75, close to the
     straight line between J1's and J2's rims.
   - Over **VJ_B** it dives to the crown at 98.50. That dip is the old
     behaviour, kept as the fallback whenever no MaxDepth is supplied.
2. Select VJ_B in the map and set **Max Depth, Display** in the property
   browser (or the *Max depth* cell in the node attribute table — both write
   the same field). The dip should close as you type.
3. Split any conduit with the insert-virtual-junction tool: the new node
   comes out with an interpolated MaxDepth already set, so the ground line
   stays continuous with no typing.
4. Save to `.inp` and reopen — `[VIRTUAL_JUNCTIONS]` grows a third column,
   and only for the rows that have a value. Save to `.gpkg` and reopen —
   both nodes are still virtual junctions (before this change a GeoPackage
   round-trip demoted them to plain junctions).

## What must NOT change

MaxDepth here is a drawing property. Running the model with and without it
produces bit-identical results — `VirtualJunction.RimDepthIsHydraulicallyInert`
in the engine's `test_virtual_junction.cpp` asserts exactly that.
