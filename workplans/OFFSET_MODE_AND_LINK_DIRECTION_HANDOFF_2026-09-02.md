# Handoff — Offset Mode & Link Direction Fix (2026-09-02)

**For:** the verifying agent. **Goal:** build both repos, run the new and
adjacent tests, fix anything that fails, and report residual issues.
Read `OFFSET_MODE_AND_LINK_DIRECTION_FIX_PLAN_2026-09-02.md` first for the
defect analysis. Follow `CLAUDE.md` (surgical changes, no speculative
refactors, test outputs in reviewable locations).

## 1. What was changed (uncompiled — the engine sources pass
`g++ -std=c++20 -fsyntax-only`; the GUI and both test files were NOT compiled)

### openswmm.engine

| File | Change |
|---|---|
| `src/engine/input/PostParseResolver.hpp/.cpp` | New `needs_authored_conversion(ctx)`, `restore_authored_orientation(ctx)` (un-reverses `direction == -1` conduits: node1/node2, offset1/offset2, q0 sign, loss_inlet/outlet, slope sign; returns count), `convert_internal_to_authored(ctx)` (orientation + depth→elevation for conduit/orifice `offset1/2` and weir/outlet `crest_height` when `link_offsets == 1`). Added after `convert_internal_to_display`. |
| `src/engine/core/InpWriter.cpp` `writeInpFile` | Local copy is now taken when `needs_display_conv || needs_authored_conv`; applies display conversion then authored conversion. Live context untouched. |
| `src/engine/core/openswmm_links_impl.cpp` + `include/openswmm/engine/openswmm_links.h` | New C API `swmm_links_restore_authored_orientation(engine, int* count)`. |
| `tests/unit/engine/data/inp_roundtrip/authored_form.inp` | Fixture: ELEVATION mode; `C_ADV` adverse J1(10)→J2(12) with offsets 10.5/12.25, InitFlow 0.75, losses 0.1/0.2, two vertices; `C_OK`; orifice/weir/outlet with elevation crests. |
| `tests/unit/engine/test_inp_writer_roundtrip.cpp` | 4 new tests appended: `ElevationOffsetsAreWrittenAsElevations`, `AdverseConduitKeepsAuthoredOrientation`, `AuthoredFormIsSaveIdempotentAndReopensClean`, `RestoreAuthoredOrientationApiUnreversesTheLiveContext`. |
| `CHANGELOG.md` | Unreleased → Fixed entry. |

### openswmm.gui

| File | Change |
|---|---|
| `src/swmmvis.cpp` ~1914-1970 | Status-bar order is now `Depth [toggle] Elevation` (labels swapped; `mLabelOffsetDepth` created before the checkbox, `mLabelOffsetElevation` after). Prompt result now passed as `pw->convertLinkOffsets(on, choice == Yes)`. |
| `include/swmmvis.h:214` | Doc comment updated. |
| `src/layers/swmmmodellayer.cpp` `openEngine…` (~line 821) | Calls `swmm_links_restore_authored_orientation(eng, nullptr)` right after a successful `swmm_engine_open`. |
| `src/layers/swmmmodellayer.cpp` `convertLinkOffsets` (~4575) + `include/layers/swmmmodellayer.h:1443` | Signature is now `(bool toElevation, bool convertValues)`. Yes → no value change (store is depth in both modes); No → reinterpret (`toElevation ? max(0, d − inv) : d + inv`). Weir/outlet handled via `swmm_link_get/set_crest_height`. Always emits `geometryChanged()`. |
| `src/swmmvisprojectwindow.cpp:562` + `include/swmmvisprojectwindow.h:83` | Forwarding signature updated. |
| `include/ui/linkoffsetdisplay.h` (new) | Mode-aware C-signature accessors `get/setOffsetUp`, `get/setOffsetDn`, `get/setCrestHeight` (add/subtract node invert when `LINK_OFFSETS` is ELEVATION; clamp ≥0 on set). |
| `src/ui/panels/swmmattributetablemodel.cpp` ~1609 | `link_offset_up/dn`, `link_crest_height` bindings use the `linkoffsetdisplay::` accessors. Include added near line 37. |
| `src/ui/properties/swmmlinkpropertyadapter.cpp` 144-146, 440-442 | `GETTER_D`/`SETTER_D` for offsetUp/offsetDn/crestHeight use `linkoffsetdisplay::`. Include added. |
| `tests/gui/test_offsetmode_roundtrip.cpp` (new), `tests/gui/data/offset_authored_fixture.inp` (new), `tests/gui/CMakeLists.txt` (target added after `test_linkflip`) | 4 tests: authored orientation after load, editors show elevations, save-without-edits preserves authored form, prompt Yes/No semantics. Writes `offset_authored_out.inp` and `offset_authored_depth_out.inp` into the GUI test data dir. |
| `docs/manual/02_interface.md:85`, `CHANGELOG.md` | Updated. |

## 2. Build & verify

```
# Engine (existing preset/build dir; the GUI consumes it — rebuild engine first)
cd openswmm.engine && cmake --build <build-dir> --target test_engine_inp_writer_roundtrip
ctest --test-dir <build-dir> -R "inp_writer_roundtrip|postparse|options_parser|model_write" --output-on-failure

# GUI
cd openswmm.gui && cmake --build build-rt10 --target SWMMVis test_offsetmode_roundtrip test_linkflip \
   test_options_hydration_contract test_profilenetworkadapter_model test_asyncload test_nodepropertyadapter
ctest --test-dir build-rt10 -R "offsetmode|linkflip|options_hydration|profilenetworkadapter|asyncload|attributetable|propertyadapter" --output-on-failure
```

Expected: all green. Then run the full GUI ctest once.

## 3. Things most likely to need attention

1. **Test helper API names.** Engine test uses `swmm_link_index`,
   `swmm_node_index`, `swmm_get_warning_count/at`, `swmm_link_get_offset_up/dn`.
   GUI test uses `SWMMModelLayer::linkIndex/linkFromNodeIdx/linkToNodeIdx`,
   `swmm_link_get_initial_flow`, `swmm_node_id`, `swmm_model_write`. Verify
   signatures; adjust the tests, not the API.
2. **Fixture parse.** `authored_form.inp` / `offset_authored_fixture.inp` must
   open with zero errors under a strict open. If the parser rejects something
   (e.g. `[OUTLETS]` FUNCTIONAL row layout, `[XSECTIONS]` for the weir), fix
   the fixture. C_ADV must actually be reversed at parse — the engine test
   `RestoreAuthoredOrientationApi…` asserts `from == J2` before restore; if the
   slope logic does not fire, check `MIN_SLOPE 0` and the DYNWAVE option.
3. **QCOMPARE on doubles.** Uses fuzzy compare; values were chosen to be
   exactly representable sums, but if a comparison like `QCOMPARE(v, 0.25)`
   fails by 1e-12, switch to `qFuzzyCompare`/`QVERIFY(qAbs(...) < 1e-9)`.
4. **Attribute table binding struct.** The `link_offset_*` entries are
   brace-initialised `{EntityKind::Link, setFn, getFn}`; the inline functions
   in `linkoffsetdisplay.h` must decay to `int(*)(SWMM_Engine,int,double)` /
   `int(*)(SWMM_Engine,int,double*)`. If the struct has a different member
   order, mirror the neighbouring entries.
5. **Refresh after toggle.** `convertLinkOffsets` emits `geometryChanged()`
   in both branches so Attribute Table/Properties re-read the (now
   mode-dependent) display values. Manually confirm in the app: open an
   ELEVATION deck, toggle, values in the table change accordingly.
6. **Existing tests that assumed the old semantics.**
   `test_options_hydration_contract::linkOffsets_fileRoundTripsThroughSave`
   only checks the token — should still pass (file now carries depths under
   DEPTH, which is the correct "Yes" outcome).
   `test_profilenetworkadapter_model` pins that offsets read back as depths
   in both modes — unchanged.
   Search `tests/` for any test that saved after loading an adverse-slope or
   ELEVATION fixture and asserted the *old* (wrong) output.
7. **GeoPackage path.** `GeoPackageWriter.cpp:504` persists `direction`;
   `GeoPackageReader.cpp:554` restores it. `convert_internal_to_authored`
   only runs in the `.inp` writer, so gpkg behaviour is unchanged. But check
   whether a gpkg round-trip in ELEVATION mode re-applies the depth
   normalisation (PostParseResolver 1738/2208 are not gated on
   `gpkg_units_internal`) — pre-existing, out of scope; report if confirmed.
8. **Python bindings** (`python/openswmm/engine/_links.pyx`, `_common.pxd`)
   do not expose `swmm_links_restore_authored_orientation`. Optional; add
   only if the binding layer mirrors every links call.
9. **Adverse-slope conduits created by editing.** With the edit context now
   holding authored orientation, nothing re-reverses if the user edits
   inverts into an adverse slope — same as before (the resolver ran only at
   open). Runs use a fresh strict open (`SimulationRunner`), so hydraulics are
   unaffected. No action; noted for completeness.

## 4. Manual check in SWMMVis (after build)

1. Open `tests/gui/data/offset_authored_fixture.inp`. Status bar shows
   `Depth [●→] **Elevation**`. Select C_ADV: From J1, To J2; Inlet Offset
   10.5, Outlet Offset 12.25. Attribute Table matches.
2. Save As → diff against the fixture: `[CONDUITS]`, `[ORIFICES]`, `[WEIRS]`,
   `[OUTLETS]`, `[LOSSES]`, `[VERTICES]` values identical.
3. Toggle to Depth, answer Yes → C_OK shows 0.5 / 0.0. Toggle back, answer
   No → C_OK shows 12.0 / 11.0 (0 → clamped, then re-added invert). Undo is
   not wired to the prompt (matches legacy).
4. Flip C_ADV via context menu, save, confirm the file shows J2→J1 with
   offsets 12.25/10.5 and losses 0.2/0.1.

## 5. Report back

List: build status per repo, ctest summary, any test/fixture edits made,
and anything from §3 that turned out to be real.
