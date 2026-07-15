#
# tests/unit/CMakeLists.txt
#
# Each call to add_swmmvis_unit_test() produces one CTest-registered executable.
# Pass the test .cpp file plus any production .cpp files it needs as ARGN.
# All targets link Qt6::Core and Qt6::Test; no widget dependencies.
#

# Helper — compile test + listed sources into one executable, register with CTest
function(add_swmmvis_unit_test TEST_NAME)
    add_executable(${TEST_NAME} ${ARGN})
    target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_link_libraries(${TEST_NAME}
        PRIVATE
            GTest::gtest
            GTest::gtest_main
            Qt6::Core
            Qt6::Test
    )
    add_test(
        NAME    ${TEST_NAME}
        COMMAND ${TEST_NAME}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data
    )
    set_tests_properties(${TEST_NAME} PROPERTIES LABELS "unit")

    # Runtime env: Windows DLL search path + PROJ/GDAL data dirs. Without the DLL
    # path the loader cannot find openswmm.engine.dll and the test HANGS at load;
    # without PROJ_DATA/GDAL_DATA every CRS operation fails with "Cannot find
    # proj.db". See tests/CMakeLists.txt for the full explanation.
    if(SWMMVIS_TEST_ENV)
        set_tests_properties(${TEST_NAME} PROPERTIES
            ENVIRONMENT_MODIFICATION "${SWMMVIS_TEST_ENV}")
    endif()
endfunction()

# ---- Test targets ----
# Add the corresponding production .cpp when the class is implemented.

add_swmmvis_unit_test(test_unitsystem
    test_unitsystem.cpp
    # ${PROJECT_SOURCE_DIR}/src/core/unitsystem.cpp   # uncomment when implemented
)

add_swmmvis_unit_test(test_numberformat
    test_numberformat.cpp
    ${PROJECT_SOURCE_DIR}/src/plot/numberformat.cpp
)

add_swmmvis_unit_test(test_projectserializer
    test_projectserializer.cpp
    # ${PROJECT_SOURCE_DIR}/src/project/projectserializer.cpp
)

add_swmmvis_unit_test(test_calibrationstats
    test_calibrationstats.cpp
    # ${PROJECT_SOURCE_DIR}/src/calibration/calibrationmanager.cpp
)

add_swmmvis_unit_test(test_thememanager
    test_thememanager.cpp
    # ${PROJECT_SOURCE_DIR}/src/theming/thememanager.cpp
)

# ---- Basemap tests (schema v3 / AB-2) ----

find_package(OpenSSL QUIET)

add_swmmvis_unit_test(test_basemapcrypto
    test_basemapcrypto.cpp
    ${PROJECT_SOURCE_DIR}/src/core/basemapcrypto.cpp
)
if(OpenSSL_FOUND OR OPENSSL_FOUND)
    target_link_libraries(test_basemapcrypto PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

add_swmmvis_unit_test(test_basemapconnectionstore
    test_basemapconnectionstore.cpp
    ${PROJECT_SOURCE_DIR}/include/connections/basemapconnectionstore.h
    ${PROJECT_SOURCE_DIR}/src/core/basemapcrypto.cpp
    ${PROJECT_SOURCE_DIR}/src/connections/basemapconnectionstore.cpp
)
if(OpenSSL_FOUND OR OPENSSL_FOUND)
    target_link_libraries(test_basemapconnectionstore PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

add_swmmvis_unit_test(test_projectserializer_basemaps
    test_projectserializer_basemaps.cpp
    # No production .cpp needed — only tests JSON structure and kCurrentSchemaVersion.
)

add_swmmvis_unit_test(test_projectserializer_paths
    test_projectserializer_paths.cpp
    # No production .cpp needed — toRelativePath / resolveStoredPath are
    # inline static methods on ProjectSerializer (Slice AA-3.2).
)

# Slice RA.4 — Save-As path normalizer. Pure function, no widget deps.
add_swmmvis_unit_test(test_saveaspathnormalizer
    test_saveaspathnormalizer.cpp
    ${PROJECT_SOURCE_DIR}/src/project/saveaspathnormalizer.cpp
)

# Slice QB.5 — Run-path resolver (Strategy C, GUI-only). Pure function,
# no widget or QSettings dependency in the core resolver — the
# resolveRunOutputPathFromSettings wrapper is a thin shim and is covered
# by higher-level integration tests once a QSettings sandbox is wired.
add_swmmvis_unit_test(test_runpathresolver
    test_runpathresolver.cpp
    ${PROJECT_SOURCE_DIR}/src/simulation/runpathresolver.cpp
)

# Slice BE — section-aware .rpt parser for the Report Viewer. Exercised
# against a real engine-written report (data/weir_culvert.rpt) so the
# indented / column-annotated header style is locked in.
add_swmmvis_unit_test(test_rptparser
    test_rptparser.cpp
    ${PROJECT_SOURCE_DIR}/src/io/rptparser.cpp
)

# Slice QA — OutputStatsRegistry smoke. Full register/unregister coverage
# with real SWMMResultsLayer instances lives in the GUI integration suite
# once .out fixtures are reused; here we lock in the API surface.
add_swmmvis_unit_test(test_outputstatsregistry
    test_outputstatsregistry.cpp
    ${PROJECT_SOURCE_DIR}/include/output/outputstatsregistry.h
    ${PROJECT_SOURCE_DIR}/src/output/outputstatsregistry.cpp
)
# Needs Qt6::Gui because outputstatsregistry.cpp's transitive includes
# (swmmresultslayer.h → openswmmvislayer.h) pull in <QPaintDevice>. Needs
# openswmm_engine because the same chain pulls in swmmmodellayer.h which
# pulls in <openswmm/engine/openswmm_callbacks.h>.
target_link_libraries(test_outputstatsregistry PRIVATE Qt6::Gui openswmm_engine)

# Slice RB.5 — Sidecar auto-create regression coverage (sidecarPathFor +
# Preferences/AutoCreateOswpOnOpen default). The full integration tests
# (SWMMVisProjectWindow::saveAs writes sidecar; openSingleINP creates
# missing sidecar) live with the higher-level GUI suite once the
# project-window scaffolding lands.
add_swmmvis_unit_test(test_sidecar_autocreate
    test_sidecar_autocreate.cpp
    # No production .cpp needed — sidecarPathFor is inline in the header.
)

# Slice Z.2 — WHERE-clause parser/evaluator for the Attribute Table.
# Pure Qt; no engine/widget deps.
add_swmmvis_unit_test(test_queryparser
    test_queryparser.cpp
    ${PROJECT_SOURCE_DIR}/src/core/queryparser.cpp
)

# Slice BC, Stage 1 — Yen's k-shortest-simple-paths over an abstract graph.
add_swmmvis_unit_test(test_profilerouter
    test_profilerouter.cpp
    ${PROJECT_SOURCE_DIR}/src/plot/profilerouter.cpp
)

# Slice §V.VA — Mesh Editing Toolbar foundation: pure helpers, no layer
# dependency (MeshObjectRef factories take a source-path QString).
add_swmmvis_unit_test(test_meshobjectref
    test_meshobjectref.cpp
    ${PROJECT_SOURCE_DIR}/include/mesh/meshobjectref.h
    ${PROJECT_SOURCE_DIR}/src/mesh/meshobjectref.cpp
)

add_swmmvis_unit_test(test_meshbctype
    test_meshbctype.cpp
    ${PROJECT_SOURCE_DIR}/include/mesh/meshbctype.h
    ${PROJECT_SOURCE_DIR}/src/mesh/meshbctype.cpp
)

# CSR-backed spatial index used by SWMM2DMeshLayer to cull triangles /
# edges to the visible map extent. Extracted from the layer's nested
# struct so this test can link only the index source, with no widget /
# engine / renderer dependencies.
add_swmmvis_unit_test(test_meshspatialgrid
    test_meshspatialgrid.cpp
    ${PROJECT_SOURCE_DIR}/include/layers/meshspatialgrid.h
    ${PROJECT_SOURCE_DIR}/src/layers/meshspatialgrid.cpp
)

# Slice §V.VF adapter unit tests — DEFERRED to §V.VF.2 GUI integration
# suite (requires linking SWMM2DMeshLayer + its renderer / layer
# transitive cascade). The adapter classes themselves are verified by
# the SWMMVis target's compile of meshvertexpropertyadapter.cpp +
# meshedgepropertyadapter.cpp (Q_PROPERTY / MOC validity).

# Slice §V.VD.1 — [2D_BOUNDARY_CONDITIONS] writer / reader round-trip.
add_swmmvis_unit_test(test_meshinpbcroundtrip
    test_meshinpbcroundtrip.cpp
    ${PROJECT_SOURCE_DIR}/include/mesh/meshbctype.h
    ${PROJECT_SOURCE_DIR}/include/mesh/meshedgebc.h
    ${PROJECT_SOURCE_DIR}/include/mesh/inpmeshreader.h
    ${PROJECT_SOURCE_DIR}/include/mesh/inpmeshwriter.h
    ${PROJECT_SOURCE_DIR}/src/mesh/meshbctype.cpp
    ${PROJECT_SOURCE_DIR}/src/mesh/inpmeshreader.cpp
    ${PROJECT_SOURCE_DIR}/src/mesh/inpmeshwriter.cpp
)

# Coincident-node matching behind the mesh toolbar's Auto-couple action.
# Pure functions over MeshResult — no layer / widget / engine deps.
add_swmmvis_unit_test(test_meshautocouple
    test_meshautocouple.cpp
    ${PROJECT_SOURCE_DIR}/include/mesh/meshautocouple.h
    ${PROJECT_SOURCE_DIR}/src/mesh/meshautocouple.cpp
)

# Mesh-edit persistence — layer edits pushed into the engine survive the
# .inp write (vertex Z / conveyance / BC), with Manning's n + coupling kept.
# Needs the real engine, so it is only built when the engine target exists.
if(TARGET openswmm_engine)
    add_swmmvis_unit_test(test_meshenginesync
        test_meshenginesync.cpp
        ${PROJECT_SOURCE_DIR}/include/mesh/meshbctype.h
        ${PROJECT_SOURCE_DIR}/include/mesh/meshedgebc.h
        ${PROJECT_SOURCE_DIR}/include/mesh/inpmeshreader.h
        ${PROJECT_SOURCE_DIR}/include/mesh/meshenginesync.h
        ${PROJECT_SOURCE_DIR}/src/mesh/meshbctype.cpp
        ${PROJECT_SOURCE_DIR}/src/mesh/inpmeshreader.cpp
        ${PROJECT_SOURCE_DIR}/src/mesh/meshenginesync.cpp
    )
    target_link_libraries(test_meshenginesync PRIVATE Qt6::Gui openswmm_engine)
endif()

# Slice BB Phase 8.6.16 — shared model for legend overlay chrome.
# Needs Qt6::Gui for QColor/QFont; no widget dependencies.
# Header listed in sources so AUTOMOC picks up the Q_OBJECT macro.
add_swmmvis_unit_test(test_legendoverlaystyle
    test_legendoverlaystyle.cpp
    ${PROJECT_SOURCE_DIR}/include/render/legendoverlaystyle.h
    ${PROJECT_SOURCE_DIR}/src/render/legendoverlaystyle.cpp
)
target_link_libraries(test_legendoverlaystyle PRIVATE Qt6::Gui)

# Slice US.1 — shared classification scheme (UNIFIED_STYLING plan S1).
# Needs Qt6::Gui for QColor; symbolstyle/symbollayer link in for the
# legend-row swatch construction.
add_swmmvis_unit_test(test_classificationscheme
    test_classificationscheme.cpp
    ${PROJECT_SOURCE_DIR}/src/render/classificationscheme.cpp
    ${PROJECT_SOURCE_DIR}/src/render/colorramp.cpp
    ${PROJECT_SOURCE_DIR}/src/render/intervalbinner.cpp
    ${PROJECT_SOURCE_DIR}/src/render/symbolstyle.cpp
    ${PROJECT_SOURCE_DIR}/src/render/symbollayer.cpp
)
target_link_libraries(test_classificationscheme PRIVATE Qt6::Gui)

# Slice US.B3 — shared label painter (UNIFIED_STYLING plan B3). Pure logic
# (scale gating + placement); needs Qt6::Gui for QFont/QColor in LabelConfig.
add_swmmvis_unit_test(test_labelpainter
    test_labelpainter.cpp
    ${PROJECT_SOURCE_DIR}/src/render/labelpainter.cpp
    ${PROJECT_SOURCE_DIR}/src/render/labelconfig.cpp
)
target_link_libraries(test_labelpainter PRIVATE Qt6::Gui)

# Slice BC, Stage 2 — chainage, HGL/EGL, and envelope computation.
add_swmmvis_unit_test(test_profilebuilder
    test_profilebuilder.cpp
    ${PROJECT_SOURCE_DIR}/src/plot/profilebuilder.cpp
)

# Slice BC, Stage 3a — pure-logic adapter (graph build + path materialize).
# Live-model glue (`*FromModel` in profilenetworkadapter_model.cpp) is
# excluded here; integration tests later.
add_swmmvis_unit_test(test_profilenetworkadapter
    test_profilenetworkadapter.cpp
    ${PROJECT_SOURCE_DIR}/src/plot/profilebuilder.cpp
    ${PROJECT_SOURCE_DIR}/src/plot/profilenetworkadapter.cpp
)

# ── Slice BQ Phase 6.7.3 — TimeseriesProvider foundation layer ──────────────
# Four tests covering the shared CSV parser, the MVC model + invariants,
# the QUndoCommand subclasses, and the project-scoped registry.

add_swmmvis_unit_test(test_timeseries_parse
    test_timeseries_parse.cpp
    ${PROJECT_SOURCE_DIR}/src/io/timeseriesparse.cpp
)
# timeseriesparse.cpp includes core/swmmdatetime.h directly (GH #1/#2
# consolidation), which delegates to the engine's datetime C API; link the
# engine so swmm_datetime_* resolve and its include dir is on the path.
if(TARGET openswmm_engine)
    target_link_libraries(test_timeseries_parse PRIVATE openswmm_engine)
endif()

# ── SWMM DateTime consolidation (GH #1, #2) ─────────────────────────────────
# Canonical QDateTime<->SWMM converter (include/core/swmmdatetime.h) parity
# against the engine's native encode/decode. Header-only under test; links the
# engine for the swmm_datetime_* symbols + include path.
add_swmmvis_unit_test(test_swmm_datetime
    test_swmm_datetime.cpp
)
if(TARGET openswmm_engine)
    target_link_libraries(test_swmm_datetime PRIVATE openswmm_engine)
endif()

add_swmmvis_unit_test(test_timeseries_provider
    test_timeseries_provider.cpp
    ${PROJECT_SOURCE_DIR}/include/timeseries/timeseriesprovider.h
    ${PROJECT_SOURCE_DIR}/src/timeseries/timeseriesprovider.cpp
)

add_swmmvis_unit_test(test_timeseries_undocommands
    test_timeseries_undocommands.cpp
    ${PROJECT_SOURCE_DIR}/include/timeseries/timeseriesprovider.h
    ${PROJECT_SOURCE_DIR}/include/timeseries/timeseriesundocommands.h
    ${PROJECT_SOURCE_DIR}/src/timeseries/timeseriesprovider.cpp
    ${PROJECT_SOURCE_DIR}/src/timeseries/timeseriesundocommands.cpp
)
target_link_libraries(test_timeseries_undocommands PRIVATE Qt6::Widgets)

add_swmmvis_unit_test(test_timeseries_registry
    test_timeseries_registry.cpp
    ${PROJECT_SOURCE_DIR}/include/timeseries/timeseriesprovider.h
    ${PROJECT_SOURCE_DIR}/include/timeseries/timeseriesregistry.h
    ${PROJECT_SOURCE_DIR}/src/timeseries/timeseriesprovider.cpp
    ${PROJECT_SOURCE_DIR}/src/timeseries/timeseriesregistry.cpp
)
# TimeseriesRegistry::loadFromEngine pulls in openswmm engine headers + symbols.
if(TARGET openswmm_engine)
    target_link_libraries(test_timeseries_registry PRIVATE openswmm_engine)
endif()

add_swmmvis_unit_test(test_timeseries_table_model
    test_timeseries_table_model.cpp
    ${PROJECT_SOURCE_DIR}/include/timeseries/timeseriesprovider.h
    ${PROJECT_SOURCE_DIR}/include/timeseries/timeseriesundocommands.h
    ${PROJECT_SOURCE_DIR}/include/ui/panels/timeseriestablemodel.h
    ${PROJECT_SOURCE_DIR}/src/timeseries/timeseriesprovider.cpp
    ${PROJECT_SOURCE_DIR}/src/timeseries/timeseriesundocommands.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/panels/timeseriestablemodel.cpp
)
target_link_libraries(test_timeseries_table_model PRIVATE Qt6::Widgets)

# ---- GDAL runtime driver probe (honest-filter work; HANDOFF §3.1) ----
# Links only src/io/gdaldrivers.cpp + GDAL — no widget deps.
add_swmmvis_unit_test(test_gdaldrivers
    test_gdaldrivers.cpp
    ${PROJECT_SOURCE_DIR}/src/io/gdaldrivers.cpp
)
find_package(GDAL CONFIG REQUIRED)
target_link_libraries(test_gdaldrivers PRIVATE GDAL::GDAL)

# ---- Sublayer-picker fixtures + enumeration contract (HANDOFF §5 / §3.2) ----
# GDAL-only: provisions tests/output/{multi.gpkg,single_points.shp} and checks
# the exact GDAL enumeration sequence enumerateSublayers() relies on, without
# linking gisvectorlayer.cpp's render closure. Outputs go to a reviewable path.
add_swmmvis_unit_test(test_sublayer_fixtures
    test_sublayer_fixtures.cpp
)
target_link_libraries(test_sublayer_fixtures PRIVATE GDAL::GDAL)
target_compile_definitions(test_sublayer_fixtures
    PRIVATE FIXTURE_OUT_DIR="${PROJECT_SOURCE_DIR}/tests/output")

# ---- QSG 2D 1M-cell rendering helpers (QSG_2D_1M_RENDERING plan) ----
# All pure Qt-Core helpers extracted from the 2D mesh/results QSG renderers
# so rendering decisions are unit-testable without Qt Quick.

# Phase 1 — per-sync render stats / OPENSWMM_RENDER_PERF logger.
add_swmmvis_unit_test(test_qsg2d_renderstats
    test_qsg2d_renderstats.cpp
    ${PROJECT_SOURCE_DIR}/src/render/qsg2drenderstats.cpp
)

# Phase 2 — dirty-domain separation (header-only) + Phase 7 stale-result
# guard (header-only).
add_swmmvis_unit_test(test_qsg2d_dirtystate
    test_qsg2d_dirtystate.cpp
)

# Phase 3 — deterministic LOD policy.
add_swmmvis_unit_test(test_qsg2d_lodpolicy
    test_qsg2d_lodpolicy.cpp
    ${PROJECT_SOURCE_DIR}/src/render/qsg2dlodpolicy.cpp
)

# Phase 4 — render chunk index (batch culling, separate from MeshSpatialGrid).
add_swmmvis_unit_test(test_meshrenderchunkindex
    test_meshrenderchunkindex.cpp
    ${PROJECT_SOURCE_DIR}/src/render/meshrenderchunkindex.cpp
)

# Phase 5 — persistent static geometry buffers (header-only).
add_swmmvis_unit_test(test_meshstaticgeometrybuffers
    test_meshstaticgeometrybuffers.cpp
)

# Phase 7 — async contour worker (pure snapshot function).
# marchingtriangles.h uses QColor (viridis helper) → needs Qt6::Gui, but no
# widgets, so it still runs headlessly.
add_swmmvis_unit_test(test_contourjob
    test_contourjob.cpp
    ${PROJECT_SOURCE_DIR}/src/render/contourjob.cpp
)
target_link_libraries(test_contourjob PRIVATE Qt6::Gui)

# Phase 8 — scalar→ramp normalization/clamp contract (header-only).
add_swmmvis_unit_test(test_scalarramplut
    test_scalarramplut.cpp
)
