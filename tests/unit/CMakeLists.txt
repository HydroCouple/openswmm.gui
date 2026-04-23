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
