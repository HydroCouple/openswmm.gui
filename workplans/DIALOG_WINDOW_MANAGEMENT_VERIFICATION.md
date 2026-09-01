# Dialog Window Management — Verification Handoff

**Date:** 2026-08-16
**Implements:** `workplans/DIALOG_WINDOW_MANAGEMENT_PLAN_2026-08-16.md` (Phase A complete, Phase B behind a switch)
**Status of the change set:** written and self-reviewed, **NOT COMPILED** — no Qt toolchain was available to the implementing agent. Building is the first task below.

---

## 0. What you are verifying

Two user-reported defects on macOS with multiple monitors:

1. **Dialogs get lost.** Moved to another screen, they sometimes disappear with no way to bring them back.
2. **Dialogs are linked.** Opening a time-series plot from the profile plot, then moving the profile dialog, moves the time-series dialog with it.

Root cause of both was `src/platform/macoswindowutils.mm`: every non-modal `QDialog` was attached as an **AppKit child window** (`-addChildWindow:`) of its Qt parent's window. AppKit child windows move rigidly with their parent and are invisible to Mission Control, so a dialog attached to another dialog got dragged around by it (defect 2) and a dialog dragged into dead space had no recovery affordance (defect 1). A weak restore-time geometry clamp compounded defect 1.

---

## 1. Build and unit tests (do this first)

```bash
cd /Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui
cmake --preset <your usual preset>
cmake --build build -j
ctest --test-dir build -L gui -R "dialog" --output-on-failure
```

Expect these to build and pass:

| Test | Covers |
|---|---|
| `test_dialog_layout_persistence` | 5 new clamp cases + 10 pre-existing persistence cases |
| `test_dialog_registry` | 9 new cases — MRU ordering, tracking filters, lifetime, stacking-mode override |

New files that must be picked up by the build: `include/ui/dialogs/dialogregistry.h`, `src/ui/dialogs/dialogregistry.cpp` (registered in `CMakeLists.txt` alongside the `dialoglayout*` entries), `tests/gui/test_dialog_registry.cpp` (`tests/gui/CMakeLists.txt`).

**If it does not compile,** the most likely spots, in order: the `QPointer<QDialog>` return type change on `DialogRegistry::openDialogs()` (call sites in `src/swmmvis.cpp` `rebuildWindowMenu` and `resetWindowPositions` use `.data()`); `QEvent::ApplicationStateChange` / `Q_FALLTHROUGH` in `dialogregistry.cpp`; `QWidget::screen()` in `resetWindowPositions`.

Then run the full GUI suite to catch collateral damage — `rebuildWindowMenu` and the dialog persistence path are widely touched:

```bash
ctest --test-dir build -L gui --output-on-failure
```

---

## 2. Manual verification — REQUIRES a Mac with two monitors

CI runs single-screen offscreen QPA, so the multi-monitor behaviour is **not** covered by any automated test. These steps are the real acceptance criteria.

### 2.1 Defect 2 — linked dialogs (the primary report)

1. Open a model, run a simulation so results exist.
2. Select a path and **Analysis ▸ Plot Profile** → profile plot opens.
3. Right-click a node in the profile → **Plot Time Series ▸** any attribute → overlay time-series dialog opens.
4. **Drag the profile dialog around, including onto the second monitor.**
   - ✅ PASS: the time-series dialog stays exactly where it is.
   - ❌ FAIL: it moves with the profile dialog — the `stackingHostFor_` walk in `macoswindowutils.mm` is not finding a non-dialog host.
5. Close the profile dialog.
   - ✅ PASS: the time-series overlay closes with it (Qt parentage still owns lifetime — this must NOT have regressed).
6. Repeat for the other dialog-parented-to-dialog cases, which the same fix covers:
   - profile plot toolbar ▸ **Display Options**
   - 2D mesh profile plot ▸ **Display Options**
   - raster profile plot ▸ **Display Options**

### 2.2 Defect 2b — the two time-series dialogs no longer collide

1. With the profile plot's overlay time-series dialog open, go to the **map** and right-click an object ▸ **Plot Time Series**.
   - ✅ PASS: a *separate* time-series dialog opens (the map's own, parented to the main window).
   - ❌ FAIL: series are added to the profile's overlay dialog — the `Qt::FindDirectChildrenOnly` fix in `ensureComparisonPlotDialog()` did not take.
2. Move each to a different position, close both, reopen both.
   - ✅ PASS: each returns to its own position (separate geometry keys).

### 2.3 Defect 1 — lost dialogs and recovery

1. Open several dialogs. Drag one onto the second monitor.
2. Open the **Window** menu.
   - ✅ PASS: every open dialog is listed, most recently used first.
3. **Unplug / disable the second monitor** (or use System Settings ▸ Displays to mirror).
4. Window menu → click the dialog that was on the removed screen.
   - ✅ PASS: it reappears on the remaining screen, fully visible, title bar grabbable.
5. **Window ▸ Reset Window Positions.**
   - ✅ PASS: main window and every open dialog gather onto the current screen; a log message confirms.
   - ✅ PASS: splitter positions, table column widths and dock/toolbar layout are unchanged (only *position* is reset).
6. Quit and relaunch.
   - ✅ PASS: nothing is restored off-screen.

### 2.4 Defect 1b — restore-time clamping

Craft bad geometry directly, then relaunch. Settings live under the `hydrocouple` / *OpenSWMM Stormwater Management Model* domain:

```bash
# inspect
defaults read com.hydrocouple.<app-domain> 2>/dev/null | grep -i geometry
```

Cases to force and relaunch after each:

| Injected geometry | Expected after restart |
|---|---|
| Far off-screen, e.g. `5000,5000 400x300` | Window appears fully on a connected screen |
| Larger than the current screen (e.g. 2× width and height) | Shrunk to fit; title bar and buttons reachable |
| Client top 4 px below the top of the available area | Pushed down so the title bar clears the menu bar |
| Only ~10 px overlapping a screen edge | Pulled back until a grabbable slice is visible |

Do this for **both** a dialog (`Dialogs/<Name>/geometry`) and the main window (`SWMMVis::MainWindow/SWMMVis::Geometry`) — the main window previously had no clamping at all.

Also: set the main window maximized on the external display, unplug it, relaunch, then **Reset Window Positions** — it must restore down and move (`showNormal()` before clamping handles this).

### 2.5 Stacking order (the behaviour that must NOT regress)

1. Open three dialogs in sequence.
   - ✅ Newest is on top.
2. Click the oldest to bring it forward, then switch to another app (Finder) and back.
   - ✅ PASS: the one you clicked is still in front — ordering is most-recently-used, not open order.
3. Click the main window / map.
   - ✅ PASS: dialogs stay above it.
4. Switch to another application entirely.
   - ✅ PASS: OpenSWMM dialogs drop **behind** that app's windows — they must not float over other applications.

---

## 3. Phase B — pure-Qt stacking (opt-in, needs the freeze retest)

Phase B replaces the AppKit attachment with a portable raise-on-activate pass. It is **off by default**. Enable per-run:

```bash
OPENSWMM_DIALOG_STACKING=qt /path/to/OpenSWMM.app/Contents/MacOS/OpenSWMM
# or persist:  Window/DialogStacking = qt
# force the old path:  OPENSWMM_DIALOG_STACKING=native
```

Verify in `qt` mode:

1. All of §2.5 again — stacking must still behave.
2. §2.1 step 4 — dialogs must be independent (trivially true here; no native attachment exists).
3. Drag the **main window** between monitors.
   - In `native` mode dialogs follow it (they are its AppKit children).
   - In `qt` mode dialogs **stay put**. This is the intended difference and the reason Phase B exists.

### 3.1 Freeze retest — MANDATORY before defaulting to `qt`

`workplans/UI_REDESIGN_ITER2_WORKPLAN.md` item 7 and phase R1 record that this attachment was once removed (`5d43e28`) after an app-wide input freeze, then restored when `ddca63d` re-diagnosed the freeze as a modal-exec-during-`mousePressEvent` `QNSView` button latch. **Run the 7-step freeze checklist in ITER2 phase R1 (lines ~180-199) in `qt` mode before recommending it as the default.** If a freeze appears, look first for a dialog being shown from inside a `mousePressEvent` — not at this change.

Do **not** delete `macoswindowutils.mm` yet; that is Phase B4, a later release.

---

## 4. Files changed, and what to look at in each

| File | Change |
|---|---|
| `src/platform/macoswindowutils.mm` | **Core fix.** New `stackingHostFor_()` walks up the parent chain past any `QDialog` to the first ordinary window; bounded to 16 hops; returns null (no attachment) if the chain is all dialogs. |
| `include/platform/macoswindowutils.h` | Doc updated to state the host is deliberately *not* `parentWidget()->window()`. |
| `src/ui/dialogs/dialoglayoutpersistence.cpp` | Old `clampToAvailableScreens_()` (center-point only) replaced by public `clampToVisibleScreen()` — largest-overlap screen choice, size clamp, minimum visible area, title-bar reachability, frame-vs-client coordinate handling. New `ensureWindowOnScreen()`. |
| `include/ui/dialogs/dialoglayoutpersistence.h` | Both helpers exported and documented. |
| `include/ui/dialogs/dialogregistry.h`, `src/ui/dialogs/dialogregistry.cpp` | **New.** MRU register of open modeless dialogs + `StackingMode`. |
| `src/swmmvisapplication.cpp` | Installs the registry; gates `attachAsChildWindow` on `StackingMode::NativeChildWindow`. |
| `src/swmmvis.cpp` | `ensureComparisonPlotDialog` uses `FindDirectChildrenOnly`; overlay dialog gets its own `objectName`; main-window geometry now clamped; `rebuildWindowMenu` lists dialogs and adds Reset; new `resetWindowPositions()`; stripped menu actions now `deleteLater()`d. |
| `include/swmmvis.h` | `resetWindowPositions()` slot, `mActionWindowResetPositions`. |
| `src/ui/dialogs/profileplotdialog.cpp` | Comment corrected (claimed the dialog was parented to `nullptr`; it is parented to the project window). |
| `tests/gui/test_dialog_layout_persistence.cpp` | 5 new clamp tests; existing tests unchanged and still expected to pass. |
| `tests/gui/test_dialog_registry.cpp` | **New**, 9 tests. |
| `CHANGELOG.md` | Added/Fixed entries under `[Unreleased]`. |

---

## 5. Known deviations from the plan, and open risks

**Deviation — plan item A2 was not implemented as written.** The plan called for reparenting the overlay `ComparisonPlotDialog` away from the profile dialog and re-coupling lifetime via a `destroyed()` connection. Once A1 (the `stackingHostFor_` walk) was in place this became unnecessary: Qt parentage alone never moved the child window — only the AppKit attachment did. Reparenting would have changed working lifetime semantics for no behavioural gain, against `CLAUDE.md` §2/§3. The Qt parentage is retained and the misleading comments were corrected instead. **If §2.1 step 4 fails, revisit this decision** — it is the one place the implementation intentionally diverges.

**Risks to probe:**

1. **All dialogs are now AppKit children of the main window** (in `native` mode), where before some were children of other dialogs. Consequence: dragging the *main window* now drags *every* dialog. That is consistent and expected, but it is a behaviour change — confirm it is acceptable, or move to `qt` mode where it does not happen.
2. **`rebuildWindowMenu()` now runs far more often** — on `openDialogsChanged` and on `QMenu::aboutToShow`, not just MDI activation. Stripped actions are `deleteLater()`d (previously leaked). Watch for any crash or missing-action behaviour around the Window menu.
3. **Overlay dialog geometry key renamed** to `ComparisonPlotDialogProfileOverlay` — existing users lose that one saved position once. Noted in the CHANGELOG.
4. **`Q_FALLTHROUGH()` between the Hide and Close cases** in `dialogregistry.cpp` — confirm the compiler accepts it and the spontaneous-hide guard behaves (Cmd+H must not empty the Window menu).
5. **`clampToVisibleScreen` models only the title-bar strip**, not left/right/bottom frame borders. On Windows/Linux a restored window may sit a few border pixels past those edges. Documented, deliberate.

---

## 6. Reporting back

For each of §1, §2.1–2.5 and §3, report PASS/FAIL with the observed behaviour. For any FAIL include: the step, what happened instead, the machine's monitor layout (arrangement and scaling), and whether `OPENSWMM_DIALOG_STACKING` was set. Attach the relevant lines from the Message Log for the Reset Window Positions step.
