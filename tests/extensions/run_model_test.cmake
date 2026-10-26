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
set(_typed_consumer_environment "")
if(TYPED_PLUGIN_DIRS)
    string(APPEND _extension_paths "${_separator}${TYPED_PLUGIN_DIRS}")
    set(_typed_consumer_environment "SDK_TYPED_CONSUMERS=1")
endif()
file(WRITE "${_run}/model.oswp"
    "{\"schemaVersion\":5,\"sessions\":[{\"inpPath\":\"model.inp\",\"instanceId\":\"{124ed789-2774-4e8f-a166-4bdf76e83e09}\"}]}")
file(WRITE "${_run}/settings-scope.json" "{\"defaults\":{\"outDir\":\"${_run}\"},\"figures\":[]}")
file(WRITE "${EVIDENCE}/last-run.txt" "${_run}\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "QT_QPA_PLATFORM=offscreen"
    "QT_QPA_PLATFORM_PLUGIN_PATH=${QT_PLATFORM_PLUGINS}"
    "SWMMVIS_GUI_EXTENSION_PATH=${_extension_paths}"
    "SWMMVIS_OPEN_ON_STARTUP=${_run}/model.oswp"
    "SDK_MODEL_REPORT=${_run}/report.json"
    "SDK_DATA_CATALOGUE=${_data_catalogue}"
    "SDK_DATA_TERRAIN=${_data_terrain}"
    ${_typed_consumer_environment}
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
