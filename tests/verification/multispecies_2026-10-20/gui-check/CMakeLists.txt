cmake_minimum_required(VERSION 3.21)
project(MultispeciesGuiCheck LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)
get_filename_component(GUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
get_filename_component(ENGINE_ROOT "${GUI_ROOT}/../openswmm.engine" ABSOLUTE)
add_executable(reaction_editor_check
    "${GUI_ROOT}/tests/gui/test_reactionsystemeditor.cpp"
    "${GUI_ROOT}/include/ui/dialogs/reactionsystemeditordialog.h"
    "${GUI_ROOT}/src/ui/dialogs/reactionsystemeditordialog.cpp"
    "${GUI_ROOT}/include/ui/widgets/reactionexpressionedit.h"
    "${GUI_ROOT}/src/ui/widgets/reactionexpressionedit.cpp")
target_include_directories(reaction_editor_check PRIVATE "${GUI_ROOT}/include" "${ENGINE_ROOT}/include")
target_link_libraries(reaction_editor_check PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
    "${ENGINE_ROOT}/build/component-catalogue-20261016/src/engine/libopenswmm.engine.dylib")
