# Data Objects editor routing review

Reviewed GUI revision `bf4bcb7e5` on November 10, 2026. Qt 6.9.3 on macOS was used to exercise the Object Browser context menu for every data category. The checks recorded the dialog class and, for list editors, the selected object ID. A second loaded model had distinct pattern, transect, inlet, rule and hydrograph IDs so that the owning model could be distinguished.

## Repairs in the working tree

| Surface | Current behaviour |
| --- | --- |
| Object Browser Edit and double-click, Properties header, Attribute Table row Edit | All 13 categories use the dedicated editor and select the requested ID. A tree refresh while the menu is open preserves its target. A missing ID produces a message. |
| Project switching and filtering | Reusable windows check their project before opening a target. Rules and hydrographs use the same ownership check for Browse and Add. Opening a named object reveals it through a filter. |
| Reference fields | Properties, the Attribute Table and the inline picker share typed dispatch. Spatial references keep their dropdown or map selection. The picker checks compatibility before assigning a new ID. Empty street, transect, aquifer and pollutant pickers leave creation to the Add button. |
| Detailed properties | The Attribute Table opens the records editor for the selected rows when the clicked row is selected, or for the clicked row alone. Scalar Apply actions are omitted for records. |
| Selection synchronization | The Object Browser restores a row by object type and ID, including when a spatial object shares its name. |

The regression suite exercises each category through all four single-object entry points. It checks the selected ID, object count and model-edited signal. It also covers two projects with identical rule names and separate Undo stacks, filtered patterns, all 21 reference kinds, street assignment, missing targets and bulk records. [Regression tests](../../tests/gui/test_dataobjectattributetable.cpp) · [Test model](../../tests/gui/data/data_editor_routing.inp)

The macOS application rebuilt successfully. Five Qt GUI suites passed with the offscreen platform on November 10, 2026. They were `test_dataobjectattributetable`, `test_objectbrowser_tree_refresh`, `test_datafamilybulk`, `test_remainingdatabulk` and `test_lidsnowpackbulk`. A separate two-project check observed the selected IDs in the repaired Browse and Add windows, exercised inline LID assignment and confirmed rejection of an incompatible time pattern.

## Object Browser results before the repair

| Right-clicked category | Example ID | Expected editor | Observed result |
| --- | --- | --- | --- |
| Time Series | TS_B | Time Series Editor | Opened **Properties — 1 selected** |
| Curves | C1_B | Curve Editor | Opened **Properties — 1 selected** |
| Control Rules | R_B | Control Rules Editor | Opened **Properties — 1 selected** |
| Unit Hydrographs | H_B | Hydrograph Group Editor | Opened **Properties — 1 selected** |
| Time Patterns | OtherPattern | Time Pattern Editor | Correct initially. Can retain the previous model or a filtered selection |
| Transects | Profile | Transect Editor | Correct initially. Can retain the previous model |
| Inlets | CURB1 | Inlet Editor | Correct initially. Can retain the previous model |
| LID Controls | Stack | LID Controls | Opened the editor on Stack |
| Pollutants | Lead | Pollutants | Opened the editor on Lead |
| Land Uses | Commercial | Land Uses | Opened the editor on Commercial |
| Aquifers | AQ2 | Aquifers | Opened the editor on AQ2 |
| Snowpacks | Snow2 | Snow Packs | Opened the editor on Snow2 |
| Streets | ST2 | Street Cross-Sections | Opened the editor on ST2 |

**Four single-object routes open the bulk dialog.** Commit `43011e87a` replaced exactly four existing-object callbacks with calls to `openDataRecords` on a one-name list. The menu labels still advertise the dedicated editors. The same callbacks serve Object Browser **Edit…**, browser double-click, the Properties header’s **Open … in …** button and the Attribute Table row’s **Edit … in …** action. The records dialog is appropriate for an explicit **Detailed properties** request. For Time Series, **Model → Data Objects → Time Series…** still opens the full editor. [Editor registrations and callbacks](../../src/ui/editors/comprehensiveeditorregistry.cpp) · [Record dialog](../../src/ui/properties/datarecordsref.cpp)

**Three single-object routes can open the previous model.** With model A still loaded, open and close a pattern, transect or inlet editor. Switch to model B and right-click **Edit…** on a B-only object. In the test, requests for `ModelB_OtherPattern`, `ModelB_Profile` and `ModelB_CURB1` reopened windows showing `OtherPattern`, `Profile` and `CURB1` from A. Each callback retains a dialog without checking its registry or layer. An edit in that window can change A. The six successful routes at the bottom of the table compare registry identity before reuse. The related **Browse** routes for Unit Hydrographs and Control Rules also displayed A’s list after switching to B with the window open. The **Add New…** callbacks for those two categories reuse the same windows. [Window ownership](../../src/ui/editors/comprehensiveeditorregistry.cpp)

**A pattern filter can defeat an explicit Edit request in the same model.** With the editor filtered to `OtherPattern`, right-clicking `EvapPat` in the browser raised the editor with `OtherPattern` still selected. The requested pattern exists in the registry, but the filtered list has no visible index for it. [Pattern selection](../../src/ui/dialogs/patterneditordialog.cpp)

## Reference fields before the repair

Editing an object and editing a field that refers to an object take different code paths. The two field menus maintain their own type switches. Both start with Time Series as the default. Selecting an unhandled kind can therefore open **New Time Series**, or open an unrelated existing series when a name happens to match. A second switch handles just four editor families, so some correctly labeled actions return without opening a window. [Properties menu](../../src/ui/panels/propertiespanel.cpp) · [Attribute Table menu](../../src/ui/panels/attributetablepanel.cpp)

| Reference kind passed to the field menu | Properties right-click | Attribute Table network-cell right-click |
| --- | --- | --- |
| Diversion curve | Opens Time Series | Opens Curve Editor |
| Transect, street, snowpack | Opens Time Series | Opens Time Series |
| Subcatchment or node target | Opens Time Series | Falls through to Time Series if this handler receives it |
| Pollutant, LID control, aquifer, inlet | Correct editor label, action opens no dialog | Same missing handlers |
| Time series, time pattern, unit hydrograph | Corresponding picker | Corresponding picker |
| Tidal, storage, pump, rating or unrestricted curve | Opens Curve Editor | Opens Curve Editor |
| Rain gage, combined subcatchment outlet, capture node | Skips the data-editor menu | Skips the data-editor menu |

The matrix describes dispatch for each input kind. The available fields depend on the object and surface. For example, mesh coupled-node cells have a separate menu. A real conduit street-reference cell reproduced the Time Series launch. All six fallbacks and four no-op cases in the Properties column were also exercised with typed field values. The regular field **…** picker has routes for streets, transects, snowpacks, aquifers and inlets. Its pollutant and LID control branches also lack a launch handler. [Reference kinds](../../include/ui/properties/dataobjectref.h) · [Field picker](../../src/ui/properties/dataobjectpickereditor.cpp)

## Related targeting problems before the repair

- **Multi-row Detailed properties in the Attribute Table.** For Curves, Time Series, Control Rules, Unit Hydrographs and Land Uses, right-clicking the Detailed properties cell with two rows selected offered the single-row editor and scalar Apply actions. The handler for **Edit keyed records for N objects…** sits after the data-source menu has already returned. The records field has its own editor and rejects scalar writes. [Table menus](../../src/ui/panels/attributetablepanel.cpp) · [Detailed fields](../../src/ui/panels/datarecords_p.h)
- **Browser selection restored by name alone.** Reverse selection uses a spatial-name lookup. A time series called `TS_B` had no reverse tree index. A time series called `C1` resolved to the same-named conduit. This affects the highlighted row when selection is synchronized. The direct right-click dispatch reads the clicked row’s typed reference. It currently reads that index again after the popup returns, so a queued tree reset during the menu also warrants a regression test. [Tree lookup](../../src/ui/panels/swmmobjecttreemodel.cpp) · [Context menu and selection synchronization](../../src/ui/panels/objectbrowserpanel.cpp)

## Original acceptance criteria

1. **Bind each launch to the clicked object and model.** Capture the typed identity and owning layer when the menu opens. Revalidate them when it is activated. Scope reusable editors to their registry or layer, including the two shared Browse/Add windows. An explicit Edit should reveal the requested object through any existing filter. If the target disappeared, report that and end the action.
2. **Restore the four dedicated single-object callbacks.** Select the existing provider or group by ID. Opening an editor should create no object and make no model change. Preserve the current editor write and Undo behaviour. Explicit Detailed properties actions should continue to use the records dialog, including the selected rows in a data-object table.
3. **Unify typed reference routing.** Have both field menus and the field picker obtain the label, availability and launch behaviour from the same mapping. Preserve curve, pattern and inlet compatibility constraints and the destination field’s assignment behaviour. Selection-only references should expose their supported selection action.
4. **Test observable outcomes.** Open all 13 actual context-menu actions and assert the dialog class, selected ID and owning layer. Include non-first rows, a filtered editor, two open models with overlapping and distinct names, a refreshed tree, and the four equivalent single-object entry points. Exercise each reference kind, and compare single-object Edit with multi-row Detailed properties. For a switched-model edit, verify the other model’s value and both Undo histories.

The original `test_dataobjectattributetable` suite passed during the review. Its `everyEditableCategoryOpensExistingObjects` check verified the presence of a callback. It did not open a dialog or inspect the selected object, which allowed the four callback replacements to pass. [Existing regression test](../../tests/gui/test_dataobjectattributetable.cpp)
