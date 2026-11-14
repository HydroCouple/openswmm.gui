# UI and file persistence review

Review date: 22 October 2026.

**38 confirmed gap groups. The current UI and file formats do not preserve all configurable settings.** The most serious results are a GeoPackage pollutant crash, a rainfall scale of 10 reverting to 1, missing groundwater and snowpack assignments, and changes to active solver and transport settings.

P1 means model behavior can change, saving/opening can fail, or a selected output configuration cannot work as presented. P2 means narrower persistence, precision, metadata or usability loss. “Reproduced” means a focused probe exercised the affected API or actual Qt page; “source confirmed” means the omission or incorrect wiring is directly visible in the read/write path.

## Scope and evidence

The review covers the GUI property adapters and compound editors, simulation-options pages, built-in INP and mesh text serialization, GeoPackage and HDF5 model plugins, OSWP project state, writer selection and destinations, calibration bindings, and tabular CSV/TSV export. It uses the current working sources, including existing uncommitted changes. The source inventory records file hashes and repository revisions.

The discovery inventory contains 271 writable property declarations across 11 SWMM adapter headers, plus 133 literal simulation-option tags. These counts include repeated declarations and exclude dynamically constructed keys; they are an inventory, not a claim that every field has a passing test.

The engine probes opened, changed, wrote and reopened small models without running a hydraulic simulation. The Qt harness instantiated the real options pages and called their normal write methods, using an isolated settings directory. It linked existing GUI target objects and the existing hotstart-format engine build; this was not a fresh full application rebuild. Every reproduced finding was also checked against its source path.

Raw evidence:

- [Engine round trips](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/tests/verification/ui_file_io_audit_2026-10-22/probe_results.json), [additional round trips and isolated crash](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/tests/verification/ui_file_io_audit_2026-10-22/additional_probe_results.json), [Qt page results](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/tests/verification/ui_file_io_audit_2026-10-22/ui_probe_results.json), and [adjustment-pattern database evidence](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/tests/verification/ui_file_io_audit_2026-10-22/adjustment_probe_results.json).
- [Property, option and precision inventory](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/tests/verification/ui_file_io_audit_2026-10-22/source_inventory.json).
- [Engine probe](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/tests/verification/ui_file_io_audit_2026-10-22/probe.py), [additional probes](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/tests/verification/ui_file_io_audit_2026-10-22/additional_probe.py), and [Qt harness](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/tests/verification/ui_file_io_audit_2026-10-22/ui_probe.cpp).

## Gap list

### 1 Pollutant molecular weight access crashes after reopening

**P1 · GeoPackage · Reproduced.** The reader does not populate the molecular-weight vector. Calling the same getter used by the pollutant property editor after reopening a GeoPackage terminates the process with SIGSEGV (exit −11). The original value was 58.44. This is both a missing field and an unsafe reader invariant.

Correction: Persist molecular weight and initialize every pollutant column when importing older files; guard public accessors.

Source: [GeoPackageReader.cpp:1426](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageReader.cpp:1426), [openswmm_pollutants_impl.cpp:392](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/openswmm_pollutants_impl.cpp:392).

### 2 Rain-gage rainfall scale is lost

**P1 · GeoPackage · Reproduced.** A rain-gage scale of 10 becomes 1 after export and reopen. The snow catch column is present, but the separate rainfall scale is absent. The same fixture retains gage scale 10 through INP. Subcatchment rain and snow factors 0.8 and 1.3 survive both formats.

Correction: Add the gage scale to the GeoPackage schema, writer and reader, with default 1 for older files.

Source: [GeoPackageWriter.cpp:980](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:980), [GeoPackageReader.cpp:1253](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageReader.cpp:1253).

### 3 Legacy aquifers and groundwater exchanges are omitted

**P1 · GeoPackage · Reproduced and source confirmed.** A model containing one 1D aquifer reopens with zero. The writer has no serialization of the legacy AQUIFERS, GROUNDWATER or GWF data. The newer 2D aquifer and groundwater block is a separate implementation and does not preserve these 1D records.

Correction: Persist the 1D aquifer registry, subcatchment assignments and groundwater expressions.

Source: [GeoPackageWriter.cpp:3140](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:3140), [GeoPackageWriter.cpp:3185](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:3185), [groundwaterexchangedialog.cpp:1](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/groundwaterexchangedialog.cpp:1).

### 4 Subcatchment snowpack assignments disappear

**P1 · GeoPackage · Reproduced.** The snowpack SP1 is written as a pack definition, but the subcatchment-to-pack assignment is not a subcatchment column. Reopening changes the subcatchment snowpack from SP1 to empty, disconnecting snow accumulation and melt from that catchment.

Correction: Store and resolve the snowpack name on each subcatchment.

Source: [GeoPackageWriter.cpp:882](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:882), [GeoPackageReader.cpp:1141](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageReader.cpp:1141).

### 5 Co-pollutant references are reconstructed incorrectly

**P1 · GeoPackage · Reproduced.** P1 referring to the later-defined P2 reopens with co-pollutant index −1 while retaining fraction 0.5. P2 with no co-pollutant reopens with index 0 instead of −1. The reader resolves names before all pollutants exist and initializes missing references to zero.

Correction: Initialize references to −1 and resolve names after registering all pollutants.

Source: [GeoPackageReader.cpp:1426](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageReader.cpp:1426), [GeoPackageReader.cpp:1450](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageReader.cpp:1450).

### 6 Hydraulic solver configuration reverts to defaults

**P1 · GeoPackage · Reproduced and source confirmed.** The options writer omits RULE_STEP, SKIP_STEADY_STATE and most finite-volume settings. Examples: RULE_STEP 37→0, FV_CFL 0.63→0.5, order 2→1, SUPERBEE→MINMOD, RK2→EULER, LTS NO→YES, and pressurized implicit YES→NO. Cell length/count, celerity, backend, parallel threshold, LTS tiers, census and structure coupling also change. The extended FV_IMPLICIT settings and compaction settings are absent from this writer as well.

Correction: Serialize the complete hydraulic option registry, including extended options, using one schema shared with the reader.

Source: [GeoPackageWriter.cpp:346](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:346), [hydraulicspage.cpp:561](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/hydraulicspage.cpp:561).

### 7 Water-quality and heat configuration reverts to defaults

**P1 · GeoPackage · Reproduced.** QUALITY_SOLVER LAGRANGIAN→LEGACY, QUALITY_STEP 0.25→0, DISPERSION RWPT→OFF, WATER_AGE and HEAT_TRANSPORT YES→NO, and outfall backflow ZERO→LAST. Segment limits, random seed, water density, heat capacity, wind coefficients and pressure ratio also reset. These are active model settings, not display preferences.

Correction: Include transport switches and physical constants in both options directions.

Source: [GeoPackageWriter.cpp:346](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:346), [qualitypage.cpp:1](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/qualitypage.cpp:1).

### 8 Newer 2D solver and thermal options are omitted

**P1 · GeoPackage · Reproduced and source confirmed.** Reconstruction order 2→1, THETA_REFERENCE_STEP 7.5→0 and infiltration step 120 s→0 were reproduced. The 2D writer also lacks the authored heat-model, bed-heat and surface/radiative exchange settings emitted by the INP writer. Existing THETA, several older hydraulic options and output precision/compression settings do have serialization.

Correction: Bring the 2D option schema into parity with the current solver and UI.

Source: [GeoPackageWriter.cpp:2642](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:2642), [InpWriter.cpp:1235](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:1235), [twodpage.cpp:996](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/twodpage.cpp:996).

### 9 Extended per-cell 2D physics is omitted

**P1 · GeoPackage · Source confirmed.** The mesh writer covers geometry, roughness, initial depth, tags, coupling, boundary conditions and conveyance, but has no equivalent of the INP serializers for initial velocity, infiltration defaults/overrides, initial water/bed quality, boundary quality, surface coverages/loadings/curb length or mesh snow configuration. The separate conditional 2D aquifer block does not cover these surface records.

Correction: Serialize these mesh-associated records and retain authored indices and source assignments.

Source: [GeoPackageWriter.cpp:2781](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:2781), [GeoPackageWriter.cpp:3185](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:3185), [InpWriter.cpp:1450](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:1450), [InpWriter.cpp:1591](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:1591), [meshsnowdialog.cpp:149](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/meshsnowdialog.cpp:149).

### 10 Advanced snow and meteorological configuration is omitted

**P1 · GeoPackage · Reproduced and source confirmed.** Snow-process cadence 73 s becomes 3600 s. The climate and snowpack writers retain legacy temperature/wind/snow fields, but omit the newer snow model and UEB configuration, as well as humidity and solar/radiation forcing configuration. Keeping conventional snowpack coefficients does not preserve these settings.

Correction: Persist the full climate/snow configuration with explicit versioning and defaults for older files.

Source: [GeoPackageWriter.cpp:1262](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:1262), [GeoPackageWriter.cpp:1324](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:1324), [climatologydialog.cpp:660](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/climatologydialog.cpp:660).

### 11 Process components and general reaction systems are not preserved

**P1 · GeoPackage · Source confirmed.** The model writer does not serialize PROCESS_COMPONENTS bindings and their configurations. Reaction serialization occurs only inside a special block requiring a 2D aquifer/groundwater configuration and subsurface reaction laws. A general 1D reaction model therefore has no complete GeoPackage persistence route.

Correction: Store component identity, arguments, configuration content and all reaction sections independently of the 2D aquifer condition.

Source: [GeoPackageWriter.cpp:3185](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:3185), [InpWriter.cpp:4877](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:4877), [reactionsystemeditordialog.cpp:1383](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/reactionsystemeditordialog.cpp:1383).

### 12 Initial-quality overrides and model extension records are omitted

**P1 · GeoPackage · Source confirmed.** There is no write/read path in the model serializer for per-element INITIAL_QUALITY records, USER_FLAGS and USER_FLAG_VALUES, EVENTS windows, or PLUGINS configuration. The GUI exposes these through object properties and the simulation-options editors. Global pollutant Cinit is written, but is not a substitute for per-element overrides.

Correction: Add explicit tables or lossless section storage for each exposed record family.

Source: [GeoPackageWriter.cpp:3140](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:3140), [initialqualitydialog.cpp:143](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/initialqualitydialog.cpp:143), [datespage.cpp:259](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/datespage.cpp:259), [filespage.cpp:842](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/filespage.cpp:842).

### 13 Conventional LID drain curves and removal lists are omitted

**P1 · GeoPackage · Source confirmed.** Conventional LID controls are saved as fixed numeric SURFACE/SOIL/PAVEMENT/STORAGE/DRAIN/DRAINMAT rows. The named drain curve and conventional pollutant-removal list are not emitted or read. The separate newer node-LID layer treatment tables do not replace these fields.

Correction: Persist conventional drain references and removal lists alongside the numeric layer records.

Source: [GeoPackageWriter.cpp:1729](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:1729), [GeoPackageWriter.cpp:1740](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:1740), [GeoPackageReader.cpp:2083](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageReader.cpp:2083).

### 14 Subcatchment adjustment patterns use the table registry

**P1 · GeoPackage · Reproduced incorrect reference; source confirmed.** N-PERV, DSTORE and INFIL pattern indices are dereferenced through ctx.tables instead of ctx.patterns. A PAT assignment was written into the database as TS1, a time-series name. The reader likewise resolves it as a table. The small fixture happened to recover the same numeric index, masking the defect; different registry ordering or counts can corrupt or lose the assignment, and the unchecked writer index can be out of bounds.

Correction: Serialize pattern names from the pattern registry and resolve against that same registry.

Source: [GeoPackageWriter.cpp:1417](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:1417), [GeoPackageReader.cpp:2052](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageReader.cpp:2052).

### 15 Additional weir inputs are not retained

**P2 · GeoPackage · Source confirmed.** The weir schema only carries type, primary discharge coefficient, crest height and end contractions. It omits the secondary coefficient, surcharge permission, roadway width/surface and coefficient-curve reference that the INP writer preserves. End contractions are additionally rounded from the UI/API double to an integer. Imported configurations can change when exported even if those advanced fields lack dedicated GUI rows.

Correction: Complete the weir schema and avoid unnecessary numeric narrowing.

Source: [GeoPackageWriter.cpp:669](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:669), [GeoPackageWriter.cpp:825](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:825), [GeoPackageReader.cpp:1072](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageReader.cpp:1072), [InpWriter.cpp:3553](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:3553).

### 16 Title notes and several object descriptions are lost

**P2 · GeoPackage · Source confirmed.** The writer has no title/notes serialization and does not write descriptions for nodes, links, subcatchments, gages, pollutants or table comments. Comments for land uses, transects and inlets are explicitly handled, so the loss is inconsistent across object types.

Correction: Persist project notes and object/table comments using consistent metadata fields.

Source: [GeoPackageWriter.cpp:496](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:496), [GeoPackageWriter.cpp:882](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:882), [GeoPackageWriter.cpp:1105](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:1105), [GeoPackageWriter.cpp:3140](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:3140), [titlenotespage.cpp:137](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/titlenotespage.cpp:137).

### 17 Pollutant molecular weight is not serialized

**P1 · INP · Reproduced.** The pollutant property editor accepts molecular weight 58.44, but the POLLUTANTS writer has no corresponding field or extension. Reopening returns 1.0. GeoPackage has the separate crash described in finding 1.

Correction: Define and implement persistence for molecular weight, with an explicit compatibility warning where a target format cannot carry it.

Source: [swmmpollutantpropertyadapter.h:42](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/include/ui/properties/swmmpollutantpropertyadapter.h:42), [InpWriter.cpp:4018](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:4018).

### 18 Transect encroachment and comments disappear

**P2 · INP · Reproduced.** Left/right encroachment stations 1.25/8.75 become 0/0 and the comment becomes empty. The UI exposes these fields, but the X1 writer emits zeros and the parser creates empty/zero values. GeoPackage retains the tested fields.

Correction: Add a supported extension or project-side persistence and make format limitations visible.

Source: [swmmtransectpropertyadapter.h:36](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/include/ui/properties/swmmtransectpropertyadapter.h:36), [InpWriter.cpp:3814](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:3814), [LinksHandler.cpp:830](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/handlers/LinksHandler.cpp:830).

### 19 Small valid routing steps serialize as zero

**P1 · INP · Reproduced.** ROUTING_STEP 0.0005 is accepted in memory but is formatted as 0:00:00. Writing returns success; reopening fails with ERROR 211. The current GUI stages and reparses built-in saves, so it should reject publishing this malformed save. The demonstrated defect is a failed save/unusable direct export, not proof that an existing good GUI project gets overwritten.

Correction: Use a round-trip-safe fractional-seconds representation and reject zero only where the option requires a positive value.

Source: [InpWriter.cpp:191](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:191), [swmmvisprojectwindow.cpp:2363](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/swmmvisprojectwindow.cpp:2363).

### 20 Numeric formatting silently changes accepted values

**P1 · INP and mesh text · Reproduced and source confirmed.** VARIABLE_STEP 0.755 becomes 0.76; pollutant rain concentration 0.000012345 becomes zero. Many remaining sections use fixed four-decimal or short significant-digit formats. The inventory identifies 141 formatting lines for follow-up, including climate, infiltration, aquifers/groundwater, LIDs, hydraulic objects, quality, patterns, time series and geometry. Time-series values use six decimals and timestamps are rounded to whole seconds. Some GUI mesh-save paths patch engine output, so mesh loss must also be checked through that final path.

Correction: Use sufficient significant digits for model values, preserve accepted time resolution, and regression-test tiny and high-precision values.

Source: [InpWriter.cpp:2075](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:2075), [InpWriter.cpp:4036](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:4036), [InpWriter.cpp:4494](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:4494).

### 21 Inactive solver settings are discarded

**P2 · INP and GeoPackage · Reproduced and source confirmed.** With SURCHARGE_METHOD EXTRAN, authored DPS celerity/alpha/decay 42/0.2/17 reopen as 25/3/0.5. INP also resets inactive TPA celerity 777→100. INP emits finite-volume settings only while FV is selected. Switching methods, saving and reopening therefore loses the prior method configuration.

Correction: Preserve inactive authored options, or store them in the project file when the target grammar cannot.

Source: [InpWriter.cpp:2060](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:2060), [InpWriter.cpp:2083](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:2083), [InpWriter.cpp:2107](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:2107), [GeoPackageWriter.cpp:398](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:398).

### 22 Weir and outlet offset controls do not match the saved crest field

**P1 · Regulator properties and model files · Reproduced.** The UI offset setters modify generic link offsets, while the INP writer reads the subtype crest. For both a weir and outlet, authored offsets 0.7/0.8 reopened from INP as 0/1.2 (the original crest was 1.2). GeoPackage retained the upstream 0.7 but its reopened downstream value became 1.2. The editable control and the authoritative regulator field are inconsistent.

Correction: Bind the offset control to the authoritative crest field and expose a separate downstream control only if the model can represent it.

Source: [swmmlinkpropertyadapter.h:873](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/include/ui/properties/swmmlinkpropertyadapter.h:873), [swmmlinkpropertyadapter.h:912](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/include/ui/properties/swmmlinkpropertyadapter.h:912), [openswmm_links_impl.cpp:311](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/openswmm_links_impl.cpp:311), [InpWriter.cpp:3553](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:3553), [InpWriter.cpp:3607](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:3607).

### 23 2D options configured before a mesh are dropped

**P2 · INP and GeoPackage · Reproduced.** A mesh-free draft with MAX_TIMESTEP 1.2345 reopens with 10 in both formats. Both writers return before writing 2D options when no mesh or external mesh reference is present.

Correction: Persist authored 2D options independently of mesh existence.

Source: [InpWriter.cpp:1115](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:1115), [GeoPackageWriter.cpp:2642](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageWriter.cpp:2642).

### 24 Embedded reaction sections are intentionally omitted

**P1 · INP · Source confirmed; warning exists.** Embedded component sections such as REACTION_* are not written back. The writer produces an explicit loss warning, which the current built-in GUI save path collects. An external component configuration has a separate save route; an imported embedded-only model still requires conversion before an ordinary project save can preserve it.

Correction: Serialize the embedded sections or convert them to a staged external configuration as part of save, preserving the edited content.

Source: [InpWriter.cpp:4902](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/InpWriter.cpp:4902), [swmmvisprojectwindow.cpp:2257](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/swmmvisprojectwindow.cpp:2257).

### 25 Applying unchanged controls modifies imported settings

**P1 · Simulation options · Reproduced with real Qt pages.** Opening the actual options pages and calling their normal write methods changed MINIMUM_STEP 0.001→0.01, FV_CFL 0.025→0.05, QUALITY_STEP 0.001→0 and DRY_DAYS 1.2345→1.23 without editing those controls. Ranges and decimal precision change the displayed value, which Apply then writes. QUALITY_STEP zero changes the setting to automatic.

Correction: Retain the original value until a user edits the control; align control ranges with the accepted engine domain.

Source: [hydraulicspage.cpp:189](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/hydraulicspage.cpp:189), [qualitypage.cpp:161](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/qualitypage.cpp:161), [datespage.cpp:252](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/datespage.cpp:252), [simulationoptionsdialog.cpp:740](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simulationoptionsdialog.cpp:740).

### 26 A valid small limiter edit is treated as unchanged

**P2 · Simulation options · Reproduced with real Qt pages.** Editing 2D LIMITER_EPSILON from 0 to 1e-9 leaves the engine at 0. The shared comparison uses an absolute floor of 1e-9. The recently specialized FV_IMPLICIT comparator does not protect this 2D setting.

Correction: Compare authored numeric values exactly or use tolerances appropriate to each field.

Source: [simulationoptionshelpers.cpp:158](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simulationoptionshelpers.cpp:158), [twodpage.cpp:1509](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/twodpage.cpp:1509).

### 27 Fixed read buffers truncate configurable text

**P2 · Simulation options and notes · Reproduced API truncation; source confirmed UI paths.** A report selector containing 499 bytes reaches the options UI as only 255 bytes. The same pattern uses 256-byte buffers for 2D option strings, including OUTPUT_FILE; title notes use 1024 bytes per line. Unchanged Apply may preserve some underlying values, but editing the truncated value can discard the hidden tail.

Correction: Use required-length queries or growing buffers, detect truncation, and preserve unedited originals.

Source: [simoptionscontext.cpp:24](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/simoptionscontext.cpp:24), [twodpage.cpp:1291](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/twodpage.cpp:1291), [titlenotespage.cpp:137](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/titlenotespage.cpp:137).

### 28 Writer selections do not persist as selected roles

**P1 · Output configuration · Reproduced with real Qt pages.** Starting with GeoPackage, setting all three writer combos to built-in, applying and reopening the dialog shows GeoPackage again. The write path only adds nonempty plugin IDs; it never records role-specific choices or clears an old selection. The read path chooses the first plugin advertising a role, so independent writer selections are not represented.

Correction: Persist an explicit selection for input, results and report roles, separate from plugin discovery/configuration.

Source: [filespage.cpp:1096](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/filespage.cpp:1096), [filespage.cpp:1218](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/filespage.cpp:1218).

### 29 Writer and single-container controls do not configure destinations

**P1 · Output configuration · Source confirmed.** Selecting a writer adds a PLUGINS row with no initialization arguments. GeoPackage output requires a path argument. The single-container checkbox only synchronizes combo IDs; it does not supply a shared path or save a container mode. The report/results path fields are separate settings used for the built-in run paths. These controls therefore do not implement their promised one-file behavior.

Correction: Resolve the chosen container/output paths into role-specific plugin initialization and keep the selections and paths consistent.

Source: [filespage.cpp:1255](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/filespage.cpp:1255), [filespage.cpp:1434](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/filespage.cpp:1434), [GeoPackageOutputPlugin.cpp:138](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageOutputPlugin.cpp:138), [runpathresolver.cpp:55](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/simulation/runpathresolver.cpp:55).

### 30 Saved initialization arguments are discarded for report plugins

**P1 · Report and state plugins · Source confirmed.** The factory retains PLUGINS arguments only for output plugins. It initializes every report plugin with an empty argument vector; the GeoPackage report plugin explicitly requires a database path. Supplying arguments manually in the GUI table cannot repair that report path. State-IO plugins are initialized with the same empty-vector pattern.

Correction: Retain and forward each instance’s own arguments and component metadata for every role.

Source: [PluginFactory.cpp:440](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/plugins/PluginFactory.cpp:440), [PluginFactory.cpp:502](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/plugins/PluginFactory.cpp:502), [GeoPackageReportPlugin.cpp:20](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/input/geopackage/GeoPackageReportPlugin.cpp:20).

### 31 The offered model format is incomplete and cannot be reopened

**P1 · HDF5 model files · Reproduced and source confirmed.** HDF5 model export returns success, but the input reader always reports that reading schema 1.0 is not implemented. The writer also omits controls, LIDs, aquifers/groundwater, snowpacks, pollutants/land-use quality, inflows/DWF/RDII, patterns, inlets, options/report and 2D sections. This concerns HDF5 model export, not the separate HDF5 results or hotstart implementations.

Correction: Advertise it as a limited export only until read/write parity exists; do not offer it as a normal model-open format.

Source: [Hdf5InputPlugin.cpp:50](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/io/hdf5/Hdf5InputPlugin.cpp:50), [Hdf5ModelWriter.cpp:1089](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/io/hdf5/Hdf5ModelWriter.cpp:1089), [filefilterregistry.cpp:252](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/plugins/filefilterregistry.cpp:252).

### 32 Plugin data-loss warnings do not reach the GUI

**P1 · Plugin save diagnostics · Reproduced and source confirmed.** The HDF5 writer records its omitted sections in skipped_sections(), but swmm_model_write_with_plugin deletes the writer without forwarding that channel. The probe received success and no engine warnings. The GUI reads only the engine warning delta, so it cannot display the warning that was generated. Finalize errors are also ignored.

Correction: Forward skipped-section and error diagnostics before destroying the writer and handle finalization failure.

Source: [openswmm_model_impl.cpp:515](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/core/openswmm_model_impl.cpp:515), [Hdf5InputPlugin.cpp:61](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.engine/src/engine/io/hdf5/Hdf5InputPlugin.cpp:61), [swmmvisprojectwindow.cpp:2257](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/swmmvisprojectwindow.cpp:2257).

### 33 Imported vector-layer styles and filters are not saved

**P2 · Project file · Source confirmed.** For ordinary GISVectorLayer objects the project stores only path, name, visibility, opacity and OGR layer name. Symbol/renderer configuration, labels and the filter expression are not included. Raster styles and editable FeatureLayer symbols have separate persistence, so this finding specifically concerns ordinary vector layers.

Correction: Save and restore the renderer, label configuration and filter for each vector layer.

Source: [projectserializer.cpp:1871](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/project/projectserializer.cpp:1871), [gisvectorlayer.h:120](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/include/layers/gisvectorlayer.h:120).

### 34 Parent 1D results-layer display state is not saved

**P2 · Project file · Source confirmed.** The 1D results list stores paths, report associations, child sublayer state and per-kind renderers, but omits the parent layer name, visibility and opacity. Restore constructs a new layer and resets its name to the output filename. Child-layer persistence does not preserve the parent state.

Correction: Persist parent display properties along with each result-layer record.

Source: [projectserializer.cpp:630](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/project/projectserializer.cpp:630), [projectserializer.cpp:1033](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/project/projectserializer.cpp:1033).

### 35 A custom canvas coordinate system has no persistence fallback

**P2 · Project file · Source confirmed.** Canvas CRS persistence writes only an authority and positive numeric code. A custom CRS without that pair has no WKT fallback, while the saved view extent is still retained. On restore the extent may therefore be interpreted under a different canvas CRS.

Correction: Store a full CRS definition and restore it before applying the saved extent.

Source: [projectserializer.cpp:1425](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/project/projectserializer.cpp:1425), [projectserializer.cpp:1662](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/project/projectserializer.cpp:1662).

### 36 Calibration bindings are global and use unstable attribute labels

**P2 · Calibration configuration · Source confirmed.** Calibration save clears the application-wide project/calibrationData group; the reader ignores its project parameter. Editing one project’s bindings therefore replaces another’s, and the bindings do not travel in the project file. Attribute identity is also reconstructed from English substrings in translated display labels, with Depth always mapped to NodeDepth.

Correction: Store calibration bindings per project in the project file and persist stable object-kind/attribute IDs.

Source: [calibrationdatadialog.cpp:136](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/calibrationdatadialog.cpp:136), [calibrationdatadialog.cpp:170](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/calibrationdatadialog.cpp:170).

### 37 Custom report and results destinations are not portable

**P2 · Output configuration and project portability · Source confirmed.** Custom ReportFilePath and OutputFilePath values live only in local QSettings keyed by the model path. Copying the model and OSWP project to another installation does not carry them. Save As on the same installation explicitly migrates this settings group, so that case is not the defect.

Correction: Serialize project-specific destination overrides in OSWP, using relative paths where appropriate.

Source: [filespage.cpp:1269](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/simoptions/filespage.cpp:1269), [runpathresolver.cpp:55](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/simulation/runpathresolver.cpp:55), [swmmvisprojectwindow.cpp:2403](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/swmmvisprojectwindow.cpp:2403).

### 38 Table exports do not escape text or report write failures

**P2 · CSV and TSV export · Source confirmed.** Tabular Results and Custom Report write headers and displayed cell strings directly with delimiters. A comma, quote or newline in a custom label can change the CSV column/row structure. Open failure simply returns and stream errors are not checked. The separate series-data exporter already has CSV quoting, but these paths do not use it.

Correction: Use a shared quoted delimited-text writer, check final stream/file status, and surface actionable errors.

Source: [tabularresultsdialog.cpp:337](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/tabularresultsdialog.cpp:337), [customreportdialog.cpp:382](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/ui/dialogs/customreportdialog.cpp:382).

## Save verification gap

Built-in project-save validation reparses staged files and checks resource references plus node/link/subcatchment and mesh counts. It does not compare configurable values. A file can therefore pass those checks while losing a factor, an assignment or an option. Plugin-driven saves bypass that built-in validation path. This is a detection gap underlying the findings, rather than an additional missing model field.

Source: [projectsavevalidation.h:270](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/include/project/projectsavevalidation.h:270), [swmmvisprojectwindow.cpp:2363](/Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui/src/swmmvisprojectwindow.cpp:2363).

A complete acceptance check needs a field-to-control-to-file matrix and automated before/after comparisons for every supported target format. Include unset/default/nondefault values, inactive solver settings, small and high-precision numbers, long strings, Unicode/quoted paths, cross-reference ordering, Save As, and repeat saves. Unsupported fields should produce a visible warning or make that target unavailable.

## Coverage boundaries

This is a broad source audit with targeted reproductions, not proof that no additional defects exist. Full GUI-driven round trips for every compound editor, final GUI mesh patching, cross-platform path/encoding behavior, every external climate/rainfall/routing format, and full numerical parity of OUT/HDF5/GeoPackage results and hotstarts remain unverified. Those require the systematic matrix above. The review does not claim a new defect in the previously fixed INP rainfall multiplier path.

Only review files, probe code and generated fixtures were added for this audit. No production fix is included.
