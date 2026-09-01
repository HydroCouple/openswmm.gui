# Dialog Window Management Plan — Multi-Monitor Loss + Linked Dialogs

**Date:** 2026-08-16
**Status:** Proposed
**Relates to:** `workplans/UI_REDESIGN_ITER2_WORKPLAN.md` (item 7 "Modeless dialog stacking regression", phases R1, D1–D3)

## Symptoms

1. **Lost dialogs (multi-monitor macOS):** dialogs moved to another screen sometimes disappear and cannot be recovered — no Window-menu entry, no reset action.
2. **Linked dialogs:** the time-series plot dialog opened from the profile dialog moves rigidly with the profile dialog.

## Root causes

| # | Cause | Location |
|---|-------|----------|
| A | App-wide `QEvent::Show` filter attaches every non-modal `QDialog` as a native **NSWindow child** of its parent's window via `addChildWindow:`. AppKit child windows move rigidly with the parent and are excluded from Mission Control / Window menu — so they can be translated into dead space with no recovery path. | `src/swmmvisapplication.cpp:213-244` → `src/platform/macoswindowutils.mm:16-50` |
| B | The profile-launched time-series dialog is constructed with the **profile dialog as Qt parent**, so cause A glues it to the profile dialog (the reported "linking"). Same pattern in Mesh/Raster profile "Display Options" (`new QDialog(this)`) and `ProfileOptionsDialog`. | `src/swmmvis.cpp:3012`; `meshprofileplotdialog.cpp:281`; `rasterprofileplotdialog.cpp:240`; `profileplotdialog.cpp:394-400` |
| C | `clampToAvailableScreens_()` only checks the saved rect's **center point**; when the center is on any screen it applies the rect untouched — no size clamp, no title-bar-reachable check, no minimum-visible-area rule. A rect straddling a monitor gap can have its center in the gap. `setGeometry()` is client-rect on macOS, so restores near the top can tuck the title bar under the menu bar. | `src/ui/dialogs/dialoglayoutpersistence.cpp:44-61,138-144` |
| D | Main-window geometry is restored with **no clamping at all**. | `src/swmmvis.cpp:3936-3945` |
| E | `ensureComparisonPlotDialog()` uses a **recursive** `findChild`, so the map-launched time-series flow can hijack the profile-glued overlay dialog; both instances share `objectName "ComparisonPlotDialog"` and thus one geometry key. | `src/swmmvis.cpp:2493-2496`; `comparisonplotdialog.cpp:83` |

Note: `floatingPanelFlags()` / `stayAboveAppFlags()` / `applyAlwaysOnTopPolicy()` are deliberate no-ops on macOS (`include/ui/dialogs/dialoglayoutpersistence.h:79-99`) — **all** macOS stacking is delegated to cause A. Any removal of A must supply a stacking replacement, per ITER2 item 7 history (`attachAsChildWindow` added in `33dff07`, removed in `5d43e28`, restored after `ddca63d` re-diagnosed the freeze as a modal-exec-during-mousePress QNSView latch).

---

## Phase A — Guards + geometry hardening + recovery UX (low risk, ship first)

### A1. Never glue dialog-to-dialog
In `attachAsChildWindow` (`macoswindowutils.mm`), after resolving `parentTop`, walk up: if `parentTop` is itself a `QDialog` (or any non-main-window), re-resolve to the main window (`SWMMVis`) and attach there instead. Dialogs launched from dialogs stay above the app but no longer track their launcher.
→ verify: open profile dialog → right-click → Plot Time Series → drag profile dialog; time-series dialog must not move. Time-series dialog still stacks above main window on app activation.

### A2. Decouple overlay CPD parentage from lifetime
`src/swmmvis.cpp:3012`: construct the overlay `ComparisonPlotDialog` with the **project/main window** as Qt parent. Keep lifetime coupling via `connect(profileDlg, &QObject::destroyed, dlg, &QWidget::close)` instead of QObject parentage. Same treatment for `ProfileOptionsDialog` (`profileplotdialog.cpp:394-400`) and Mesh/Raster Display Options dialogs. Fix the stale "parented to nullptr" comment at `profileplotdialog.cpp:141-142`.
→ verify: closing the profile dialog still closes its overlay time-series dialog; moving one never moves the other.

### A3. Fix CPD instance collision
`src/swmmvis.cpp:2496`: add `Qt::FindDirectChildrenOnly`. Give the profile-overlay instance a distinct `objectName` (e.g. `"ComparisonPlotDialogOverlay"`) so the two instances stop sharing one geometry key (revisits ITER2 line 306 "last-close-wins" decision for this pair only).
→ verify: open both routes (map + profile); each reuses its own instance; geometries persist independently.

### A4. Strengthen `clampToAvailableScreens_()`
Replace center-point test with:
- Find the screen with the **largest intersection** with the saved rect.
- Require a minimum visible area (e.g. intersection ≥ 40×30 px **and** the top strip of the frame — title bar — inside that screen's `availableGeometry`; account for `setGeometry` being client-rect: reserve `frameGeometry().height() - geometry().height()` or a fixed 28 px above `y`).
- Clamp width/height to the target screen's available size, then translate (don't recenter) minimally so the rect fits.
- Only fall back to primary-screen centering when intersection with every screen is below the minimum.
Route the main-window restore (`swmmvis.cpp:3943`) through the same function.
→ verify: update `tests/gui/test_dialog_layout_persistence.cpp` (clamp test ~lines 234-243): saved rects fully off-screen, straddling a gap, oversized for the current screen, and title-bar-above-screen all restore fully usable. Existing pass-through test updated to assert minimum-visible rule instead of center rule.

### A5. Recovery UX — Window menu + reset
Extend `rebuildWindowMenu()` (`swmmvis.cpp:6086-6131`):
- List open modeless dialogs (seed a small registry from the `topLevelWidgets()` pattern already used at `swmmvis.cpp:4017`, or `QList<QPointer<QDialog>>` maintained by the existing `SwmmVisApplication` Show filter — this list is also Phase B's `mModelessOrder`). Selecting an entry calls `raise()` + `activateWindow()` + clamp-into-visible-screen if off-screen.
- Add **"Reset Window Positions"**: clears `Dialogs/*/geometry` and `SWMMVis::MainWindow` geometry keys, then re-clamps any currently open windows onto the screen containing the main window.
→ verify: manually drag a dialog off-screen (or fake a stale QRect in settings); Window menu entry and Reset both bring it back. GUI test: dialog with off-screen geometry + menu action → geometry inside a screen.

### A6. CHANGELOG
Update `CHANGELOG.md` on release per CLAUDE.md §5.2.

---

## Phase B — Replace NSWindow child attachment with pure-Qt stacking (robust end state)

Implements the fallback already designed in ITER2 phase R1 (lines 180-199). Ship only after Phase A is verified; keep behind a single switch so it can be reverted independently.

### B1. `mModelessOrder` registry
In `SwmmVisApplication`: `QList<QPointer<QDialog>> mModelessOrder`; append on NonModal `Show`, drop on `Close`/`Hide`/`destroyed`. (Shared with A5's Window menu.)
Maintain as **MRU order**, not open order: on a dialog's `WindowActivate`, move it to the end of the list. Newly opened dialogs still start on top (show is followed by activate), but the raise loop in B2 then restores the z-order the user last arranged by clicking, instead of re-imposing strict open order on every app re-activation.

### B2. Raise-on-activate
On `ApplicationActivate` and main-window `WindowActivate`: `raise()` each dialog in order — **never** `activateWindow()` (per ITER2 design, avoids focus stealing). Remove the `attachAsChildWindow` call at `swmmvisapplication.cpp:239`; keep `detachFromParentWindow` for one release as a no-op safety.

### B3. Regression gate
Run the 7-step manual freeze-retest checklist from ITER2 phase R1 (the modal-exec-during-mousePress QNSView latch scenario) before merging. Also retest: dialog stays above main window after app switch; dialog does not float over other apps when OpenSWMM is deactivated (the property the `addChildWindow` comment at `macoswindowutils.mm:46-48` guarantees today).

### B4. Cleanup
If B sticks for a release: delete `macoswindowutils.mm/.h`, the `#ifdef Q_OS_MACOS` branch in the Show filter, and A1's guard (obsolete). Re-evaluate whether `floatingPanelFlags()`/`stayAboveAppFlags()` macOS no-ops should be revisited.

---

## Explicit non-goals

- No per-screen geometry profiles / screen-serial persistence (QRect format retained per ITER2 line 296).
- No changes to modal dialog behavior or the MDI project-window menu section.
- No refactor of dialog parenting beyond the sites listed (CLAUDE.md §3 surgical-changes rule).

## Execution order & verification summary

1. A3, A4 (+ tests) → `ctest` GUI suite green.
2. A1, A2 → manual linked-dialog check on 2-monitor mac.
3. A5, A6 → manual recovery check; GUI test for reset action.
4. B1–B3 → freeze-retest checklist + stacking checks.
5. B4 in a later release.
