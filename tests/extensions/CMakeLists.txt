find_package(Qt6 REQUIRED COMPONENTS Test)
get_target_property(_sdk_qmake Qt6::qmake IMPORTED_LOCATION)
execute_process(COMMAND "${_sdk_qmake}" -query QT_INSTALL_PLUGINS
    OUTPUT_VARIABLE _sdk_qt_plugins OUTPUT_STRIP_TRAILING_WHITESPACE)
set(OpenSWMMVisSdk_MANIFEST "${CMAKE_BINARY_DIR}/sdk/sdk-manifest.json")
include("${PROJECT_SOURCE_DIR}/cmake/OpenSWMMVisExtension.cmake")
openswmmvis_add_extension(sdk_conformance
    ID org.openswmm.conformance CAPTION "SDK conformance" VERSION 1.0.0
    SOURCES conformance.cpp)
openswmmvis_add_extension(sdk_failed_initialize
    ID org.openswmm.failed CAPTION "Failed initialization" VERSION 1.0.0
    SOURCES conformance.cpp)
target_compile_definitions(sdk_failed_initialize PRIVATE SDK_CONFORMANCE_FAIL=1)
foreach(_target sdk_conformance sdk_failed_initialize)
    set_target_properties(${_target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_target}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_target}")
endforeach()
add_executable(test_gui_extensions test_extensions.cpp)
set_target_properties(test_gui_extensions PROPERTIES AUTOMOC ON)
target_link_libraries(test_gui_extensions PRIVATE openswmmvis_extensions Qt6::Test)
target_compile_definitions(test_gui_extensions PRIVATE
    SDK_CONFORMANCE_DIRECTORY="$<TARGET_FILE_DIR:sdk_conformance>"
    SDK_FAILED_DIRECTORY="$<TARGET_FILE_DIR:sdk_failed_initialize>"
    SDK_VERIFICATION_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/verification/extension_sdk_2026-10")
add_dependencies(test_gui_extensions sdk_conformance sdk_failed_initialize)
add_executable(test_gui_extension_events test_events.cpp)
set_target_properties(test_gui_extension_events PROPERTIES AUTOMOC ON)
target_link_libraries(test_gui_extension_events PRIVATE openswmmvis_extensions Qt6::Test)
openswmmvis_add_event_adapter(test_gui_extension_events)
target_compile_definitions(test_gui_extension_events PRIVATE
    SDK_EVENT_VERIFICATION_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/verification/extension_events_2026-10/ev1/core")
add_test(NAME test_gui_extension_events COMMAND test_gui_extension_events)
set_tests_properties(test_gui_extension_events PROPERTIES LABELS "gui;sdk;events"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
add_executable(test_gui_extension_stateevents test_stateevents.cpp)
set_target_properties(test_gui_extension_stateevents PROPERTIES AUTOMOC ON)
target_link_libraries(test_gui_extension_stateevents PRIVATE openswmmvis_extensions Qt6::Test)
target_compile_definitions(test_gui_extension_stateevents PRIVATE
    SDK_STATE_VERIFICATION_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/verification/extension_events_2026-10/ev2/core")
add_test(NAME test_gui_extension_stateevents COMMAND test_gui_extension_stateevents)
set_tests_properties(test_gui_extension_stateevents PROPERTIES LABELS "gui;sdk;lifecycle"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
target_sources(test_gui_extensions PRIVATE
    ${PROJECT_SOURCE_DIR}/include/ui/toolbars/compacttoolbarcontroller.h
    ${PROJECT_SOURCE_DIR}/src/ui/toolbars/compacttoolbarcontroller.cpp)

openswmmvis_add_extension(sdk_model_conformance
    ID org.openswmm.model-conformance CAPTION "Model conformance" VERSION 1.0.0
    SOURCES modelconformance.cpp REQUIRES_ENGINE_ABI)
target_link_libraries(sdk_model_conformance PRIVATE openswmm_engine)
set_target_properties(sdk_model_conformance PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sdk_model_conformance"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sdk_model_conformance")
set(_sdk_model_evidence "${PROJECT_SOURCE_DIR}/tests/verification/extension_sdk_2026-10/model")
file(MAKE_DIRECTORY "${_sdk_model_evidence}")
configure_file(data/model.inp "${_sdk_model_evidence}/model.inp" COPYONLY)
configure_file(data/boundary.geojson "${_sdk_model_evidence}/boundary.geojson" COPYONLY)
file(WRITE "${_sdk_model_evidence}/settings-scope.json"
    "{\"defaults\":{\"outDir\":\"${_sdk_model_evidence}\"},\"figures\":[]}")
add_test(NAME test_gui_extension_models COMMAND "${CMAKE_COMMAND}"
    "-DAPP=$<TARGET_FILE:SWMMVis>"
    "-DPLUGIN_DIR=$<TARGET_FILE_DIR:sdk_model_conformance>"
    "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/data"
    "-DQT_PLATFORM_PLUGINS=${_sdk_qt_plugins}/platforms"
    "-DEVIDENCE=${_sdk_model_evidence}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_model_test.cmake")
set_tests_properties(test_gui_extension_models PROPERTIES LABELS "gui;sdk;integration"
    TIMEOUT 90)
add_test(NAME test_gui_extensions COMMAND test_gui_extensions)
set_tests_properties(test_gui_extensions PROPERTIES LABELS "gui;sdk"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
if(SWMMVIS_TEST_ENV)
    set_tests_properties(test_gui_extensions test_gui_extension_models test_gui_extension_events test_gui_extension_stateevents PROPERTIES
        ENVIRONMENT_MODIFICATION "${SWMMVIS_TEST_ENV}")
endif()
