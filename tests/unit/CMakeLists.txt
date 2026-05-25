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
endfunction()

# ---- Test targets ----
# Add the corresponding production .cpp when the class is implemented.

add_swmmvis_unit_test(test_unitsystem
    test_unitsystem.cpp
    # ${PROJECT_SOURCE_DIR}/src/core/unitsystem.cpp   # uncomment when implemented
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

# Slice BB Phase 8.6.16 — shared model for legend overlay chrome.
# Needs Qt6::Gui for QColor/QFont; no widget dependencies.
# Header listed in sources so AUTOMOC picks up the Q_OBJECT macro.
add_swmmvis_unit_test(test_legendoverlaystyle
    test_legendoverlaystyle.cpp
    ${PROJECT_SOURCE_DIR}/include/render/legendoverlaystyle.h
    ${PROJECT_SOURCE_DIR}/src/render/legendoverlaystyle.cpp
)
target_link_libraries(test_legendoverlaystyle PRIVATE Qt6::Gui)

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
