"""Render the repository review and its structured findings."""
from pathlib import Path
import json

root=Path(__file__).resolve().parent
gui=root.parents[2]
engine=gui.parent/'openswmm.engine'
findings=[]
def add(priority, status, area, title, detail, fix, *refs):
    findings.append(dict(id=len(findings)+1,priority=priority,evidence=status,area=area,title=title,detail=detail,correction=fix,refs=refs))

GW='engine:src/engine/input/geopackage/GeoPackageWriter.cpp'
GR='engine:src/engine/input/geopackage/GeoPackageReader.cpp'
IW='engine:src/engine/core/InpWriter.cpp'
FP='gui:src/ui/dialogs/simoptions/filespage.cpp'
PS='gui:src/project/projectserializer.cpp'

add('P1','Reproduced','GeoPackage','Pollutant molecular weight access crashes after reopening',
    'The reader does not populate the molecular-weight vector. Calling the same getter used by the pollutant property editor after reopening a GeoPackage terminates the process with SIGSEGV (exit −11). The original value was 58.44. This is both a missing field and an unsafe reader invariant.',
    'Persist molecular weight and initialize every pollutant column when importing older files; guard public accessors.',GR+':1426','engine:src/engine/core/openswmm_pollutants_impl.cpp:392')
add('P1','Reproduced','GeoPackage','Rain-gage rainfall scale is lost',
    'A rain-gage scale of 10 becomes 1 after export and reopen. The snow catch column is present, but the separate rainfall scale is absent. The same fixture retains gage scale 10 through INP. Subcatchment rain and snow factors 0.8 and 1.3 survive both formats.',
    'Add the gage scale to the GeoPackage schema, writer and reader, with default 1 for older files.',GW+':980',GR+':1253')
add('P1','Reproduced and source confirmed','GeoPackage','Legacy aquifers and groundwater exchanges are omitted',
    'A model containing one 1D aquifer reopens with zero. The writer has no serialization of the legacy AQUIFERS, GROUNDWATER or GWF data. The newer 2D aquifer and groundwater block is a separate implementation and does not preserve these 1D records.',
    'Persist the 1D aquifer registry, subcatchment assignments and groundwater expressions.',GW+':3140',GW+':3185','gui:src/ui/dialogs/groundwaterexchangedialog.cpp:1')
add('P1','Reproduced','GeoPackage','Subcatchment snowpack assignments disappear',
    'The snowpack SP1 is written as a pack definition, but the subcatchment-to-pack assignment is not a subcatchment column. Reopening changes the subcatchment snowpack from SP1 to empty, disconnecting snow accumulation and melt from that catchment.',
    'Store and resolve the snowpack name on each subcatchment.',GW+':882',GR+':1141')
add('P1','Reproduced','GeoPackage','Co-pollutant references are reconstructed incorrectly',
    'P1 referring to the later-defined P2 reopens with co-pollutant index −1 while retaining fraction 0.5. P2 with no co-pollutant reopens with index 0 instead of −1. The reader resolves names before all pollutants exist and initializes missing references to zero.',
    'Initialize references to −1 and resolve names after registering all pollutants.',GR+':1426',GR+':1450')
add('P1','Reproduced and source confirmed','GeoPackage','Hydraulic solver configuration reverts to defaults',
    'The options writer omits RULE_STEP, SKIP_STEADY_STATE and most finite-volume settings. Examples: RULE_STEP 37→0, FV_CFL 0.63→0.5, order 2→1, SUPERBEE→MINMOD, RK2→EULER, LTS NO→YES, and pressurized implicit YES→NO. Cell length/count, celerity, backend, parallel threshold, LTS tiers, census and structure coupling also change. The extended FV_IMPLICIT settings and compaction settings are absent from this writer as well.',
    'Serialize the complete hydraulic option registry, including extended options, using one schema shared with the reader.',GW+':346','gui:src/ui/dialogs/simoptions/hydraulicspage.cpp:561')
add('P1','Reproduced','GeoPackage','Water-quality and heat configuration reverts to defaults',
    'QUALITY_SOLVER LAGRANGIAN→LEGACY, QUALITY_STEP 0.25→0, DISPERSION RWPT→OFF, WATER_AGE and HEAT_TRANSPORT YES→NO, and outfall backflow ZERO→LAST. Segment limits, random seed, water density, heat capacity, wind coefficients and pressure ratio also reset. These are active model settings, not display preferences.',
    'Include transport switches and physical constants in both options directions.',GW+':346','gui:src/ui/dialogs/simoptions/qualitypage.cpp:1')
add('P1','Reproduced and source confirmed','GeoPackage','Newer 2D solver and thermal options are omitted',
    'Reconstruction order 2→1, THETA_REFERENCE_STEP 7.5→0 and infiltration step 120 s→0 were reproduced. The 2D writer also lacks the authored heat-model, bed-heat and surface/radiative exchange settings emitted by the INP writer. Existing THETA, several older hydraulic options and output precision/compression settings do have serialization.',
    'Bring the 2D option schema into parity with the current solver and UI.',GW+':2642',IW+':1235','gui:src/ui/dialogs/simoptions/twodpage.cpp:996')
add('P1','Source confirmed','GeoPackage','Extended per-cell 2D physics is omitted',
    'The mesh writer covers geometry, roughness, initial depth, tags, coupling, boundary conditions and conveyance, but has no equivalent of the INP serializers for initial velocity, infiltration defaults/overrides, initial water/bed quality, boundary quality, surface coverages/loadings/curb length or mesh snow configuration. The separate conditional 2D aquifer block does not cover these surface records.',
    'Serialize these mesh-associated records and retain authored indices and source assignments.',GW+':2781',GW+':3185',IW+':1450',IW+':1591','gui:src/ui/dialogs/meshsnowdialog.cpp:149')
add('P1','Reproduced and source confirmed','GeoPackage','Advanced snow and meteorological configuration is omitted',
    'Snow-process cadence 73 s becomes 3600 s. The climate and snowpack writers retain legacy temperature/wind/snow fields, but omit the newer snow model and UEB configuration, as well as humidity and solar/radiation forcing configuration. Keeping conventional snowpack coefficients does not preserve these settings.',
    'Persist the full climate/snow configuration with explicit versioning and defaults for older files.',GW+':1262',GW+':1324','gui:src/ui/dialogs/climatologydialog.cpp:660')
add('P1','Source confirmed','GeoPackage','Process components and general reaction systems are not preserved',
    'The model writer does not serialize PROCESS_COMPONENTS bindings and their configurations. Reaction serialization occurs only inside a special block requiring a 2D aquifer/groundwater configuration and subsurface reaction laws. A general 1D reaction model therefore has no complete GeoPackage persistence route.',
    'Store component identity, arguments, configuration content and all reaction sections independently of the 2D aquifer condition.',GW+':3185',IW+':4877','gui:src/ui/dialogs/reactionsystemeditordialog.cpp:1383')
add('P1','Source confirmed','GeoPackage','Initial-quality overrides and model extension records are omitted',
    'There is no write/read path in the model serializer for per-element INITIAL_QUALITY records, USER_FLAGS and USER_FLAG_VALUES, EVENTS windows, or PLUGINS configuration. The GUI exposes these through object properties and the simulation-options editors. Global pollutant Cinit is written, but is not a substitute for per-element overrides.',
    'Add explicit tables or lossless section storage for each exposed record family.',GW+':3140','gui:src/ui/dialogs/initialqualitydialog.cpp:143','gui:src/ui/dialogs/simoptions/datespage.cpp:259',FP+':842')
add('P1','Source confirmed','GeoPackage','Conventional LID drain curves and removal lists are omitted',
    'Conventional LID controls are saved as fixed numeric SURFACE/SOIL/PAVEMENT/STORAGE/DRAIN/DRAINMAT rows. The named drain curve and conventional pollutant-removal list are not emitted or read. The separate newer node-LID layer treatment tables do not replace these fields.',
    'Persist conventional drain references and removal lists alongside the numeric layer records.',GW+':1729',GW+':1740',GR+':2083')
add('P1','Reproduced incorrect reference; source confirmed','GeoPackage','Subcatchment adjustment patterns use the table registry',
    'N-PERV, DSTORE and INFIL pattern indices are dereferenced through ctx.tables instead of ctx.patterns. A PAT assignment was written into the database as TS1, a time-series name. The reader likewise resolves it as a table. The small fixture happened to recover the same numeric index, masking the defect; different registry ordering or counts can corrupt or lose the assignment, and the unchecked writer index can be out of bounds.',
    'Serialize pattern names from the pattern registry and resolve against that same registry.',GW+':1417',GR+':2052')
add('P2','Source confirmed','GeoPackage','Additional weir inputs are not retained',
    'The weir schema only carries type, primary discharge coefficient, crest height and end contractions. It omits the secondary coefficient, surcharge permission, roadway width/surface and coefficient-curve reference that the INP writer preserves. End contractions are additionally rounded from the UI/API double to an integer. Imported configurations can change when exported even if those advanced fields lack dedicated GUI rows.',
    'Complete the weir schema and avoid unnecessary numeric narrowing.',GW+':669',GW+':825',GR+':1072',IW+':3553')
add('P2','Source confirmed','GeoPackage','Title notes and several object descriptions are lost',
    'The writer has no title/notes serialization and does not write descriptions for nodes, links, subcatchments, gages, pollutants or table comments. Comments for land uses, transects and inlets are explicitly handled, so the loss is inconsistent across object types.',
    'Persist project notes and object/table comments using consistent metadata fields.',GW+':496',GW+':882',GW+':1105',GW+':3140','gui:src/ui/dialogs/simoptions/titlenotespage.cpp:137')
add('P1','Reproduced','INP','Pollutant molecular weight is not serialized',
    'The pollutant property editor accepts molecular weight 58.44, but the POLLUTANTS writer has no corresponding field or extension. Reopening returns 1.0. GeoPackage has the separate crash described in finding 1.',
    'Define and implement persistence for molecular weight, with an explicit compatibility warning where a target format cannot carry it.','gui:include/ui/properties/swmmpollutantpropertyadapter.h:42',IW+':4018')
add('P2','Reproduced','INP','Transect encroachment and comments disappear',
    'Left/right encroachment stations 1.25/8.75 become 0/0 and the comment becomes empty. The UI exposes these fields, but the X1 writer emits zeros and the parser creates empty/zero values. GeoPackage retains the tested fields.',
    'Add a supported extension or project-side persistence and make format limitations visible.','gui:include/ui/properties/swmmtransectpropertyadapter.h:36',IW+':3814','engine:src/engine/input/handlers/LinksHandler.cpp:830')
add('P1','Reproduced','INP','Small valid routing steps serialize as zero',
    'ROUTING_STEP 0.0005 is accepted in memory but is formatted as 0:00:00. Writing returns success; reopening fails with ERROR 211. The current GUI stages and reparses built-in saves, so it should reject publishing this malformed save. The demonstrated defect is a failed save/unusable direct export, not proof that an existing good GUI project gets overwritten.',
    'Use a round-trip-safe fractional-seconds representation and reject zero only where the option requires a positive value.',IW+':191','gui:src/swmmvisprojectwindow.cpp:2363')
add('P1','Reproduced and source confirmed','INP and mesh text','Numeric formatting silently changes accepted values',
    'VARIABLE_STEP 0.755 becomes 0.76; pollutant rain concentration 0.000012345 becomes zero. Many remaining sections use fixed four-decimal or short significant-digit formats. The inventory identifies 141 formatting lines for follow-up, including climate, infiltration, aquifers/groundwater, LIDs, hydraulic objects, quality, patterns, time series and geometry. Time-series values use six decimals and timestamps are rounded to whole seconds. Some GUI mesh-save paths patch engine output, so mesh loss must also be checked through that final path.',
    'Use sufficient significant digits for model values, preserve accepted time resolution, and regression-test tiny and high-precision values.',IW+':2075',IW+':4036',IW+':4494')
add('P2','Reproduced and source confirmed','INP and GeoPackage','Inactive solver settings are discarded',
    'With SURCHARGE_METHOD EXTRAN, authored DPS celerity/alpha/decay 42/0.2/17 reopen as 25/3/0.5. INP also resets inactive TPA celerity 777→100. INP emits finite-volume settings only while FV is selected. Switching methods, saving and reopening therefore loses the prior method configuration.',
    'Preserve inactive authored options, or store them in the project file when the target grammar cannot.',IW+':2060',IW+':2083',IW+':2107',GW+':398')
add('P1','Reproduced','Regulator properties and model files','Weir and outlet offset controls do not match the saved crest field',
    'The UI offset setters modify generic link offsets, while the INP writer reads the subtype crest. For both a weir and outlet, authored offsets 0.7/0.8 reopened from INP as 0/1.2 (the original crest was 1.2). GeoPackage retained the upstream 0.7 but its reopened downstream value became 1.2. The editable control and the authoritative regulator field are inconsistent.',
    'Bind the offset control to the authoritative crest field and expose a separate downstream control only if the model can represent it.','gui:include/ui/properties/swmmlinkpropertyadapter.h:873','gui:include/ui/properties/swmmlinkpropertyadapter.h:912','engine:src/engine/core/openswmm_links_impl.cpp:311',IW+':3553',IW+':3607')
add('P2','Reproduced','INP and GeoPackage','2D options configured before a mesh are dropped',
    'A mesh-free draft with MAX_TIMESTEP 1.2345 reopens with 10 in both formats. Both writers return before writing 2D options when no mesh or external mesh reference is present.',
    'Persist authored 2D options independently of mesh existence.',IW+':1115',GW+':2642')
add('P1','Source confirmed; warning exists','INP','Embedded reaction sections are intentionally omitted',
    'Embedded component sections such as REACTION_* are not written back. The writer produces an explicit loss warning, which the current built-in GUI save path collects. An external component configuration has a separate save route; an imported embedded-only model still requires conversion before an ordinary project save can preserve it.',
    'Serialize the embedded sections or convert them to a staged external configuration as part of save, preserving the edited content.',IW+':4902','gui:src/swmmvisprojectwindow.cpp:2257')
add('P1','Reproduced with real Qt pages','Simulation options','Applying unchanged controls modifies imported settings',
    'Opening the actual options pages and calling their normal write methods changed MINIMUM_STEP 0.001→0.01, FV_CFL 0.025→0.05, QUALITY_STEP 0.001→0 and DRY_DAYS 1.2345→1.23 without editing those controls. Ranges and decimal precision change the displayed value, which Apply then writes. QUALITY_STEP zero changes the setting to automatic.',
    'Retain the original value until a user edits the control; align control ranges with the accepted engine domain.','gui:src/ui/dialogs/simoptions/hydraulicspage.cpp:189','gui:src/ui/dialogs/simoptions/qualitypage.cpp:161','gui:src/ui/dialogs/simoptions/datespage.cpp:252','gui:src/ui/dialogs/simulationoptionsdialog.cpp:740')
add('P2','Reproduced with real Qt pages','Simulation options','A valid small limiter edit is treated as unchanged',
    'Editing 2D LIMITER_EPSILON from 0 to 1e-9 leaves the engine at 0. The shared comparison uses an absolute floor of 1e-9. The recently specialized FV_IMPLICIT comparator does not protect this 2D setting.',
    'Compare authored numeric values exactly or use tolerances appropriate to each field.','gui:src/ui/dialogs/simulationoptionshelpers.cpp:158','gui:src/ui/dialogs/simoptions/twodpage.cpp:1509')
add('P2','Reproduced API truncation; source confirmed UI paths','Simulation options and notes','Fixed read buffers truncate configurable text',
    'A report selector containing 499 bytes reaches the options UI as only 255 bytes. The same pattern uses 256-byte buffers for 2D option strings, including OUTPUT_FILE; title notes use 1024 bytes per line. Unchanged Apply may preserve some underlying values, but editing the truncated value can discard the hidden tail.',
    'Use required-length queries or growing buffers, detect truncation, and preserve unedited originals.','gui:src/ui/dialogs/simoptions/simoptionscontext.cpp:24','gui:src/ui/dialogs/simoptions/twodpage.cpp:1291','gui:src/ui/dialogs/simoptions/titlenotespage.cpp:137')
add('P1','Reproduced with real Qt pages','Output configuration','Writer selections do not persist as selected roles',
    'Starting with GeoPackage, setting all three writer combos to built-in, applying and reopening the dialog shows GeoPackage again. The write path only adds nonempty plugin IDs; it never records role-specific choices or clears an old selection. The read path chooses the first plugin advertising a role, so independent writer selections are not represented.',
    'Persist an explicit selection for input, results and report roles, separate from plugin discovery/configuration.',FP+':1096',FP+':1218')
add('P1','Source confirmed','Output configuration','Writer and single-container controls do not configure destinations',
    'Selecting a writer adds a PLUGINS row with no initialization arguments. GeoPackage output requires a path argument. The single-container checkbox only synchronizes combo IDs; it does not supply a shared path or save a container mode. The report/results path fields are separate settings used for the built-in run paths. These controls therefore do not implement their promised one-file behavior.',
    'Resolve the chosen container/output paths into role-specific plugin initialization and keep the selections and paths consistent.',FP+':1255',FP+':1434','engine:src/engine/input/geopackage/GeoPackageOutputPlugin.cpp:138','gui:src/simulation/runpathresolver.cpp:55')
add('P1','Source confirmed','Report and state plugins','Saved initialization arguments are discarded for report plugins',
    'The factory retains PLUGINS arguments only for output plugins. It initializes every report plugin with an empty argument vector; the GeoPackage report plugin explicitly requires a database path. Supplying arguments manually in the GUI table cannot repair that report path. State-IO plugins are initialized with the same empty-vector pattern.',
    'Retain and forward each instance’s own arguments and component metadata for every role.','engine:src/engine/plugins/PluginFactory.cpp:440','engine:src/engine/plugins/PluginFactory.cpp:502','engine:src/engine/input/geopackage/GeoPackageReportPlugin.cpp:20')
add('P1','Reproduced and source confirmed','HDF5 model files','The offered model format is incomplete and cannot be reopened',
    'HDF5 model export returns success, but the input reader always reports that reading schema 1.0 is not implemented. The writer also omits controls, LIDs, aquifers/groundwater, snowpacks, pollutants/land-use quality, inflows/DWF/RDII, patterns, inlets, options/report and 2D sections. This concerns HDF5 model export, not the separate HDF5 results or hotstart implementations.',
    'Advertise it as a limited export only until read/write parity exists; do not offer it as a normal model-open format.','engine:src/engine/io/hdf5/Hdf5InputPlugin.cpp:50','engine:src/engine/io/hdf5/Hdf5ModelWriter.cpp:1089','gui:src/plugins/filefilterregistry.cpp:252')
add('P1','Reproduced and source confirmed','Plugin save diagnostics','Plugin data-loss warnings do not reach the GUI',
    'The HDF5 writer records its omitted sections in skipped_sections(), but swmm_model_write_with_plugin deletes the writer without forwarding that channel. The probe received success and no engine warnings. The GUI reads only the engine warning delta, so it cannot display the warning that was generated. Finalize errors are also ignored.',
    'Forward skipped-section and error diagnostics before destroying the writer and handle finalization failure.','engine:src/engine/core/openswmm_model_impl.cpp:515','engine:src/engine/io/hdf5/Hdf5InputPlugin.cpp:61','gui:src/swmmvisprojectwindow.cpp:2257')
add('P2','Source confirmed','Project file','Imported vector-layer styles and filters are not saved',
    'For ordinary GISVectorLayer objects the project stores only path, name, visibility, opacity and OGR layer name. Symbol/renderer configuration, labels and the filter expression are not included. Raster styles and editable FeatureLayer symbols have separate persistence, so this finding specifically concerns ordinary vector layers.',
    'Save and restore the renderer, label configuration and filter for each vector layer.',PS+':1871','gui:include/layers/gisvectorlayer.h:120')
add('P2','Source confirmed','Project file','Parent 1D results-layer display state is not saved',
    'The 1D results list stores paths, report associations, child sublayer state and per-kind renderers, but omits the parent layer name, visibility and opacity. Restore constructs a new layer and resets its name to the output filename. Child-layer persistence does not preserve the parent state.',
    'Persist parent display properties along with each result-layer record.',PS+':630',PS+':1033')
add('P2','Source confirmed','Project file','A custom canvas coordinate system has no persistence fallback',
    'Canvas CRS persistence writes only an authority and positive numeric code. A custom CRS without that pair has no WKT fallback, while the saved view extent is still retained. On restore the extent may therefore be interpreted under a different canvas CRS.',
    'Store a full CRS definition and restore it before applying the saved extent.',PS+':1425',PS+':1662')
add('P2','Source confirmed','Calibration configuration','Calibration bindings are global and use unstable attribute labels',
    'Calibration save clears the application-wide project/calibrationData group; the reader ignores its project parameter. Editing one project’s bindings therefore replaces another’s, and the bindings do not travel in the project file. Attribute identity is also reconstructed from English substrings in translated display labels, with Depth always mapped to NodeDepth.',
    'Store calibration bindings per project in the project file and persist stable object-kind/attribute IDs.','gui:src/ui/dialogs/calibrationdatadialog.cpp:136','gui:src/ui/dialogs/calibrationdatadialog.cpp:170')
add('P2','Source confirmed','Output configuration and project portability','Custom report and results destinations are not portable',
    'Custom ReportFilePath and OutputFilePath values live only in local QSettings keyed by the model path. Copying the model and OSWP project to another installation does not carry them. Save As on the same installation explicitly migrates this settings group, so that case is not the defect.',
    'Serialize project-specific destination overrides in OSWP, using relative paths where appropriate.',FP+':1269','gui:src/simulation/runpathresolver.cpp:55','gui:src/swmmvisprojectwindow.cpp:2403')
add('P2','Source confirmed','CSV and TSV export','Table exports do not escape text or report write failures',
    'Tabular Results and Custom Report write headers and displayed cell strings directly with delimiters. A comma, quote or newline in a custom label can change the CSV column/row structure. Open failure simply returns and stream errors are not checked. The separate series-data exporter already has CSV quoting, but these paths do not use it.',
    'Use a shared quoted delimited-text writer, check final stream/file status, and surface actionable errors.','gui:src/ui/dialogs/tabularresultsdialog.cpp:337','gui:src/ui/dialogs/customreportdialog.cpp:382')

def resolve(ref):
    repo,rest=ref.split(':',1)
    path,line=rest.rsplit(':',1)
    p=(gui if repo=='gui' else engine)/path
    assert p.exists(),p
    assert 0<int(line)<=len(p.read_text().splitlines()),ref
    return f'[{p.name}:{line}]({p}:{line})'

inventory=json.loads((root/'source_inventory.json').read_text())
lines=['# UI and file persistence review', '',
       'Review date: 22 October 2026.', '',
       f'**{len(findings)} confirmed gap groups. The current UI and file formats do not preserve all configurable settings.** The most serious results are a GeoPackage pollutant crash, a rainfall scale of 10 reverting to 1, missing groundwater and snowpack assignments, and changes to active solver and transport settings.', '',
       'P1 means model behavior can change, saving/opening can fail, or a selected output configuration cannot work as presented. P2 means narrower persistence, precision, metadata or usability loss. “Reproduced” means a focused probe exercised the affected API or actual Qt page; “source confirmed” means the omission or incorrect wiring is directly visible in the read/write path.', '',
       '## Scope and evidence', '',
       'The review covers the GUI property adapters and compound editors, simulation-options pages, built-in INP and mesh text serialization, GeoPackage and HDF5 model plugins, OSWP project state, writer selection and destinations, calibration bindings, and tabular CSV/TSV export. It uses the current working sources, including existing uncommitted changes. The source inventory records file hashes and repository revisions.', '',
       f'The discovery inventory contains {len(inventory["writable_properties"])} writable property declarations across {len(set(p["file"] for p in inventory["writable_properties"]))} SWMM adapter headers, plus {len(inventory["literal_tagged_options"])} literal simulation-option tags. These counts include repeated declarations and exclude dynamically constructed keys; they are an inventory, not a claim that every field has a passing test.', '',
       'The engine probes opened, changed, wrote and reopened small models without running a hydraulic simulation. The Qt harness instantiated the real options pages and called their normal write methods, using an isolated settings directory. It linked existing GUI target objects and the existing hotstart-format engine build; this was not a fresh full application rebuild. Every reproduced finding was also checked against its source path.', '',
       'Raw evidence:', '',
       f'- [Engine round trips]({root}/probe_results.json), [additional round trips and isolated crash]({root}/additional_probe_results.json), [Qt page results]({root}/ui_probe_results.json), and [adjustment-pattern database evidence]({root}/adjustment_probe_results.json).',
       f'- [Property, option and precision inventory]({root}/source_inventory.json).',
       f'- [Engine probe]({root}/probe.py), [additional probes]({root}/additional_probe.py), and [Qt harness]({root}/ui_probe.cpp).', '',
       '## Gap list', '']
for f in findings:
    lines += [f'### {f["id"]} {f["title"]}', '',
              f'**{f["priority"]} · {f["area"]} · {f["evidence"]}.** {f["detail"]}', '',
              'Correction: '+f['correction'], '',
              'Source: '+', '.join(resolve(r) for r in f['refs'])+'.', '']
lines += ['## Save verification gap', '',
          'Built-in project-save validation reparses staged files and checks resource references plus node/link/subcatchment and mesh counts. It does not compare configurable values. A file can therefore pass those checks while losing a factor, an assignment or an option. Plugin-driven saves bypass that built-in validation path. This is a detection gap underlying the findings, rather than an additional missing model field.', '',
          'Source: '+resolve('gui:include/project/projectsavevalidation.h:270')+', '+resolve('gui:src/swmmvisprojectwindow.cpp:2363')+'.', '',
          'A complete acceptance check needs a field-to-control-to-file matrix and automated before/after comparisons for every supported target format. Include unset/default/nondefault values, inactive solver settings, small and high-precision numbers, long strings, Unicode/quoted paths, cross-reference ordering, Save As, and repeat saves. Unsupported fields should produce a visible warning or make that target unavailable.', '',
          '## Coverage boundaries', '',
          'This is a broad source audit with targeted reproductions, not proof that no additional defects exist. Full GUI-driven round trips for every compound editor, final GUI mesh patching, cross-platform path/encoding behavior, every external climate/rainfall/routing format, and full numerical parity of OUT/HDF5/GeoPackage results and hotstarts remain unverified. Those require the systematic matrix above. The review does not claim a new defect in the previously fixed INP rainfall multiplier path.', '',
          'Only review files, probe code and generated fixtures were added for this audit. No production fix is included.', '']
(root/'REVIEW.md').write_text('\n'.join(lines))
(root/'findings.json').write_text(json.dumps(findings,indent=2)+'\n')
print(json.dumps({'findings':len(findings),'P1':sum(f['priority']=='P1' for f in findings),'P2':sum(f['priority']=='P2' for f in findings),'report':str(root/'REVIEW.md')},indent=2))
