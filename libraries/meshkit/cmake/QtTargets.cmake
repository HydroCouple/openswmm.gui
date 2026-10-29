# Transitional targets preserve the existing Qt value/container API. No GUI
# application or engine dependencies are permitted in this directory.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Concurrent)
add_library(meshkit_predicates STATIC vendor/predicates/predicates.c)
set_target_properties(meshkit_predicates PROPERTIES
    EXPORT_NAME predicates POSITION_INDEPENDENT_CODE ON
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(meshkit_predicates PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/predicates>)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU|AppleClang")
    target_compile_options(meshkit_predicates PRIVATE
        -Wno-deprecated-non-prototype -Wno-strict-prototypes
        -Wno-unused-function -Wno-unused-variable
        -Wno-implicit-function-declaration -Wno-old-style-definition)
endif()
if(MSVC)
    target_compile_options(meshkit_predicates PRIVATE /wd4996 /wd4244 /wd4267 /wd4101)
endif()

function(meshkit_qt_target name)
    add_library(meshkit_${name} STATIC ${ARGN})
    add_library(meshkit::${name} ALIAS meshkit_${name})
    set_target_properties(meshkit_${name} PROPERTIES EXPORT_NAME ${name}
        POSITION_INDEPENDENT_CODE ON AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_features(meshkit_${name} PUBLIC cxx_std_17)
    target_include_directories(meshkit_${name} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(meshkit_${name} PUBLIC Qt6::Core Qt6::Gui)
    if(MSVC)
        target_compile_definitions(meshkit_${name} PUBLIC _USE_MATH_DEFINES)
    endif()
endfunction()

meshkit_qt_target(model_qt
    src/mesh/meshinfil.cpp
    src/mesh/meshbctype.cpp
    src/mesh/meshcellparams.cpp
    include/mesh/meshbctype.h)
set_target_properties(meshkit_model_qt PROPERTIES AUTOMOC ON)

meshkit_qt_target(core_qt
    src/mesh/constraintsimplifier.cpp
    src/mesh/constraintshared.cpp
    src/mesh/constraintaudit.cpp
    src/mesh/constraintpreview.cpp
    src/mesh/meshcdt.cpp
    src/mesh/meshgenerator.cpp
    src/mesh/pslgprep.cpp
    src/mesh/breaklinestrips.cpp
    src/mesh/meshpatch.cpp
    src/mesh/meshquadregion.cpp
    src/mesh/meshquadmatch.cpp
    src/mesh/meshquadcleanup.cpp
    src/mesh/sizefield.cpp
    src/mesh/quadblocks.cpp
    src/mesh/meshreorder.cpp
    src/mesh/meshcellstats.cpp
    src/mesh/naturalnbinterpolator.cpp
    src/core/editgeometry.cpp)
target_link_libraries(meshkit_core_qt PUBLIC meshkit::model_qt Qt6::Concurrent
    PRIVATE meshkit_predicates)

meshkit_qt_target(io_qt
    src/mesh/inpmeshreader.cpp
    src/mesh/inpmeshwriter.cpp
    src/mesh/sms2dmreader.cpp)
target_link_libraries(meshkit_io_qt PUBLIC meshkit::model_qt)

install(TARGETS meshkit_model_qt meshkit_core_qt meshkit_io_qt meshkit_predicates
    EXPORT MeshKitTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES vendor/predicates/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/MeshKit RENAME predicates.txt)
