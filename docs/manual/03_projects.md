# 03 — Working with Projects

## What you'll do

Open, save, and manage SWMM projects. Understand the difference between an `.inp`
single-model file and an `.oswp` multi-session project. Track unsaved changes and use
the recent-files list.

## Where to find it

`File` menu, the Welcome tab's **Start Modeling** and **Open Recent Files** sections,
and the dirty-state indicator in the title bar (`*` after the filename).

## Step-by-step

### 1. Open an existing model

Three equivalent paths:

- **Welcome tab → Open…**
- `File → Open` (<kbd>Ctrl</kbd>+<kbd>O</kbd>)
- Drag a `.inp` or `.oswp` file onto the application icon (planned).

The file-picker filter accepts:

| Extension | Meaning |
|-----------|---------|
| `*.inp` | A single SWMM input file (legacy SWMM 5 format). |
| `*.oswp` | An OpenSWMM project envelope (JSON) referencing one or more `.inp` files plus GUI state. |

### 2. Save your work

| Action | Shortcut | Behavior |
|--------|----------|----------|
| `File → Save` | <kbd>Ctrl</kbd>+<kbd>S</kbd> | Write the active project back to its current path via `swmm_model_write`. If no path is set, falls through to **Save As**. |
| `File → Save As` | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> | Pick a new path; subsequent **Save** writes there. |

Save runs the engine's writer, which serializes the entire model — including any edits
made through the GUI — back to a valid `.inp`. The new file replaces the old in the
recent-files list.

### 3. Track unsaved changes

The window title shows `*` after the filename when the project has unsaved edits:

```
test_Example1            ← clean, no unsaved edits
test_Example1 *          ← dirty, unsaved edits present
```

What marks a project dirty in the current slice:

- Changing flow units via the status-bar combo.
- (More edit operations land in Slice D when map editing is wired.)

Closing a dirty tab or quitting the application prompts:

> The model "X" has unsaved changes. Save before closing?
>
> [Save] [Discard] [Cancel]

### 4. Use Recent Files

The 20 most recently opened files appear in two places:

- **File → Open Recent ▶** menu.
- The **Open Recent Files** scroll panel on the Welcome tab.

Click any entry to reopen. **Clear Recent Files** (Welcome tab) empties the list.

### 5. Open multiple projects at once

Each `File → Open` adds a new MDI tab. The tab bar at the top of the central area lets
you switch between them. Per-project state (CRS, units, undo) follows the active tab.

### 6. Close a tab

- Click the **×** on the tab itself, or use `Window → Close All` to close everything.
- If the project is dirty, the Save? prompt appears as described above.

## Tips and gotchas

- **Save writes via the engine, not the GUI.** This means the round-trip through
  `swmm_model_write` is **lossy for unrecognized custom sections** in the `.inp`. GUI-only
  state (layer visibility, themes) is stored separately in `.oswp` once that workflow
  ships in Slice E.
- **No undo for "Save As".** Save As is not pushed onto the undo stack — it's a destination
  change, not a model change.
- **Recent files are global, not per-tab.** Opening the same file in two tabs only adds
  one entry to the recent list.
- **Discard vs Cancel.** Discard at the close prompt loses all unsaved edits permanently.
  If unsure, choose Cancel and decide later.

## Related

- [02 — The Interface](02_interface.md) — where the menus and status bar live.
- [04 — Coordinate Reference Systems](04_coordinate_systems.md) (planned, Slice C) — what
  happens to coordinates when you Save with a different CRS.
- [10 — Running Simulations](10_simulation.md) (planned, Slice E) — the next step after
  opening a model.
