# @file run_model_test.cmake
# @brief Run the real-GUI SDK conformance plugin with fresh, reviewable test data and settings.
# @details Each invocation gets an independent fixture directory, avoiding overwrite prompts and
# preserving prior reports. Failures include bounded application output and the JSON check report.
# @author Caleb Buahin <caleb.buahin@gmail.com>
# @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _run_id)
if(NOT QT_PLATFORM_PLUGINS)
    set(QT_PLATFORM_PLUGINS "$ENV{QT_QPA_PLATFORM_PLUGIN_PATH}")
endif()
set(_run "${EVIDENCE}/runs/${_run_id}")
file(MAKE_DIRECTORY "${_run}")
file(COPY "${FIXTURES}/model.inp" "${FIXTURES}/boundary.geojson"
    "${FIXTURES}/catalogue-sites.json" "${FIXTURES}/catalogue-terrain.json" DESTINATION "${_run}")
set(_extension_paths "${PLUGIN_DIR}")
if(WIN32)
    set(_separator ";")
else()
    set(_separator ":")
endif()
set(_data_catalogue "")
set(_data_terrain "")
if(DATA_PLUGIN_DIR)
    string(APPEND _extension_paths "${_separator}${DATA_PLUGIN_DIR}")
    set(_data_catalogue "${_run}/catalogue-sites.json")
    set(_data_terrain "${_run}/catalogue-terrain.json")
endif()
set(_data_typed_environment "")
if(DATA_TYPED_CONSUMER)
    set(_data_typed_environment "SDK_DATA_TYPED_CONSUMER=1")
endif()
set(_typed_consumer_environment "")
if(TYPED_PLUGIN_DIRS)
    string(APPEND _extension_paths "${_separator}${TYPED_PLUGIN_DIRS}")
    set(_typed_consumer_environment "SDK_TYPED_CONSUMERS=1")
endif()
set(_lifecycle_environment "")
if(LIFECYCLE_PLUGIN_DIR)
    string(APPEND _extension_paths "${_separator}${LIFECYCLE_PLUGIN_DIR}")
    set(_lifecycle_environment "SDK_LIFECYCLE_CONSUMER=1")
endif()
set(_view_consumer_environment "")
if(VIEW_PLUGIN_DIRS)
    string(APPEND _extension_paths "${_separator}${VIEW_PLUGIN_DIRS}")
    set(_view_consumer_environment "SDK_VIEW_CONSUMERS=1")
endif()
# Optional independent installed-SDK 1.5 consumers share native runs while
# keeping their worker tasks private. Paths use the platform extension separator.
set(_activity_consumer_environment "")
if(ACTIVITY_PLUGIN_DIRS)
    string(APPEND _extension_paths "${_separator}${ACTIVITY_PLUGIN_DIRS}")
    set(_activity_consumer_environment "SDK_ACTIVITY_CONSUMERS=1")
endif()
# Each value consumer receives a distinct explicitly requested JSONL capture.
# This verifies copied Qt value representation without embedding an interpreter.
set(_value_consumer_environment "")
if(VALUE_PLUGIN_DIRS)
    string(APPEND _extension_paths "${_separator}${VALUE_PLUGIN_DIRS}")
    set(_value_consumer_environment "SDK_VALUE_CONSUMERS=1")
endif()
set(_startup_project "${_run}/model.oswp")
set(_multi_model_environment "")
file(WRITE "${_run}/model.oswp"
    "{\"schemaVersion\":5,\"sessions\":[{\"inpPath\":\"model.inp\",\"instanceId\":\"{124ed789-2774-4e8f-a166-4bdf76e83e09}\",\"extensions\":{\"org.openswmm.model-conformance\":{\"restoredMarker\":\"primary\"}}}]}")
if(EV2_MULTI_MODEL)
    # Distinct input paths avoid normal document deduplication. The intentionally repeated
    # persistent UUID must not collapse the independent open-session identities.
    configure_file("${_run}/model.inp" "${_run}/other.inp" COPYONLY)
    set(_startup_project "${_run}/chosen-project.oswp")
    file(WRITE "${_startup_project}"
        "{\"schemaVersion\":5,\"sessions\":[{\"inpPath\":\"model.inp\",\"instanceId\":\"{124ed789-2774-4e8f-a166-4bdf76e83e09}\",\"extensions\":{\"org.openswmm.model-conformance\":{\"restoredMarker\":\"primary\"}}},{\"inpPath\":\"other.inp\",\"instanceId\":\"{124ed789-2774-4e8f-a166-4bdf76e83e09}\",\"extensions\":{\"org.openswmm.model-conformance\":{\"restoredMarker\":\"secondary\"}}}]}")
    # Sibling sidecars are decoys: opening the explicitly selected project must restore
    # its individual session entries rather than substituting either sibling file.
    foreach(_input model other)
        file(WRITE "${_run}/${_input}.oswp"
            "{\"schemaVersion\":5,\"sessions\":[{\"inpPath\":\"${_input}.inp\",\"instanceId\":\"{d3a4013e-2106-4dce-9ac4-5ba842c35ca0}\",\"extensions\":{\"org.openswmm.model-conformance\":{\"restoredMarker\":\"decoy\"}}}]}")
    endforeach()
    set(_multi_model_environment "SDK_EV2_MULTI_MODEL=1")
endif()
file(WRITE "${_run}/settings-scope.json" "{\"defaults\":{\"outDir\":\"${_run}\"},\"figures\":[]}")
file(WRITE "${EVIDENCE}/last-run.txt" "${_run}\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "QT_QPA_PLATFORM=offscreen"
    "QT_QPA_PLATFORM_PLUGIN_PATH=${QT_PLATFORM_PLUGINS}"
    "SWMMVIS_GUI_EXTENSION_PATH=${_extension_paths}"
    "SWMMVIS_OPEN_ON_STARTUP=${_startup_project}"
    "SDK_MODEL_REPORT=${_run}/report.json"
    "SDK_MODEL_PRIMARY_INPUT=${_run}/model.inp"
    "SDK_EV2_PROJECT_PATH=${_startup_project}"
    "SDK_DATA_CATALOGUE=${_data_catalogue}"
    "SDK_DATA_TERRAIN=${_data_terrain}"
    "SDK_VIEW_PLUGIN_DIRS=${VIEW_PLUGIN_DIRS}"
    "SDK_ACTIVITY_PLUGIN_DIRS=${ACTIVITY_PLUGIN_DIRS}"
    "SDK_VALUE_PLUGIN_DIRS=${VALUE_PLUGIN_DIRS}"
    "SDK_VALUE_CAPTURE_PATH=${_run}/values-{pluginId}.jsonl"
    ${_data_typed_environment}
    ${_typed_consumer_environment}
    ${_lifecycle_environment}
    ${_view_consumer_environment}
    ${_activity_consumer_environment}
    ${_value_consumer_environment}
    ${_multi_model_environment}
    "SWMMVIS_CAPTURE_MANIFEST=${_run}/settings-scope.json"
    "SWMM_LOG_FILE=${_run}/application.log"
    "${APP}"
    RESULT_VARIABLE _result TIMEOUT 80
    OUTPUT_FILE "${_run}/stdout.log" ERROR_FILE "${_run}/stderr.log")
if(NOT _result EQUAL 0 OR NOT EXISTS "${_run}/report.json")
    file(READ "${_run}/stderr.log" _stderr)
    if(EXISTS "${_run}/report.json")
        file(READ "${_run}/report.json" _report)
    endif()
    string(LENGTH "${_stderr}" _length)
    if(_length GREATER 8000)
        math(EXPR _start "${_length} - 8000")
        string(SUBSTRING "${_stderr}" ${_start} -1 _stderr)
    endif()
    message(FATAL_ERROR "GUI extension conformance failed (${_result}).\n${_report}\n${_stderr}\nEvidence: ${_run}")
endif()
file(READ "${_run}/report.json" _report)
string(JSON _ok GET "${_report}" ok)
if(NOT _ok)
    message(FATAL_ERROR "GUI extension checks failed:\n${_report}\nEvidence: ${_run}")
endif()
message(STATUS "GUI extension model/run/plot checks passed. Evidence: ${_run}")
