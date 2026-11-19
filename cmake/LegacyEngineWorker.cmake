# cmake/LegacyEngineWorker.cmake
#
# add_legacy_engine_worker(VERSION <x.y.z> SOURCE_DIR <tree>)
#
# Compiles a fetched SWMM 5.x engine tree — src/solver/*.c plus the SWMMVis
# worker src/worker/main.cpp — STATICALLY into one executable named
# openswmm-legacy-worker-<version>. No shared library, so nothing can collide
# with the 5.3.0 legacy dylib the 6.x engine install ships, and nothing else
# needs bundling beside the executable (libomp is already in the bundle).
#
# The tree's own CMake is never consulted (see EngineVersions.cmake), so the
# things it would have set up are re-applied here, deliberately:
#
#   * FP policy. The 5.x branch keeps its floating-point flags in
#     CMakePresets.json (-ffp-contract=off -fno-fast-math -fno-math-errno,
#     /fp:precise), which a source-only consumer does not inherit. Without
#     them the bundled worker would not reproduce the numbers the branch's
#     stock-vs-branch parity gate certifies. (MULTI_ENGINE plan amendment 2.)
#   * OpenMP, optional. The branch hard-requires it standalone; in the worker
#     it is a speed-up, not a requirement. Compile flag and runtime library
#     travel together via OpenMP::OpenMP_C — project.c stubs
#     omp_get_max_threads() when _OPENMP is undefined, so linking libomp
#     without the flag would duplicate that symbol.
#   * Output location. RUNTIME_OUTPUT_DIRECTORY is ${CMAKE_BINARY_DIR}/bin
#     (multi-config generators append /<CONFIG>), which is where
#     findLegacyWorker() searches when running from the build tree, and where
#     the GUI tests find it.
#
# Written version-agnostic so the base plan's later 5.3.0 symmetry can reuse it.

include_guard(GLOBAL)

function(add_legacy_engine_worker)
    cmake_parse_arguments(ARG "" "VERSION;SOURCE_DIR" "" ${ARGN})
    if(NOT ARG_VERSION OR NOT ARG_SOURCE_DIR)
        message(FATAL_ERROR "add_legacy_engine_worker: VERSION and SOURCE_DIR are required")
    endif()
    string(REPLACE "." "" _v "${ARG_VERSION}")          # 5.2.4 -> 524
    set(_t openswmm_legacy_worker_${_v})

    file(GLOB _solver CONFIGURE_DEPENDS "${ARG_SOURCE_DIR}/src/solver/*.c")
    list(LENGTH _solver _n)
    set(_has_worker "no")
    if(EXISTS "${ARG_SOURCE_DIR}/src/worker/main.cpp")
        set(_has_worker "yes")
    endif()
    if(_n LESS 50 OR _has_worker STREQUAL "no")
        message(FATAL_ERROR
            "add_legacy_engine_worker(${ARG_VERSION}): ${ARG_SOURCE_DIR} is not a "
            "build-v${ARG_VERSION} tree with the SWMMVis worker "
            "(${_n} solver .c files; src/worker/main.cpp present: ${_has_worker}). "
            "Is the checkout at the pinned tag?")
    endif()

    add_executable(${_t}
        ${_solver}
        "${ARG_SOURCE_DIR}/src/worker/main.cpp"
    )
    target_include_directories(${_t} PRIVATE
        "${ARG_SOURCE_DIR}/src/solver/include"    # swmm5.h (public API)
        "${ARG_SOURCE_DIR}/src/solver"            # headers.h chain
        "${ARG_SOURCE_DIR}/src/worker"            # worker_progress.h
    )
    target_compile_definitions(${_t} PRIVATE
        SWMM_WORKER_VERSION="${ARG_VERSION}"
        $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS;_CRT_NONSTDC_NO_DEPRECATE>
    )
    set_target_properties(${_t} PROPERTIES
        OUTPUT_NAME               "openswmm-legacy-worker-${ARG_VERSION}"
        RUNTIME_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/bin"
        C_STANDARD 99   C_STANDARD_REQUIRED ON   C_EXTENSIONS ON
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        POSITION_INDEPENDENT_CODE ON
    )
    if(APPLE)
        set_target_properties(${_t} PROPERTIES
            INSTALL_RPATH "@loader_path;@loader_path/../Frameworks")
    elseif(NOT WIN32)
        set_target_properties(${_t} PROPERTIES
            INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib")
    endif()

    # ── FP policy (mirrors the branch presets) ──────────────────────────────
    if(MSVC)
        target_compile_options(${_t} PRIVATE /fp:precise /W3 /wd4996 /wd4244 /wd4267)
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${_t} PRIVATE -ffp-contract=off -fno-fast-math -fno-math-errno)
        # Stock EPA C under this project's -Wall -Wextra is pure noise;
        # silence the solver TUs only, keep warnings on for the worker.
        set_source_files_properties(${_solver} PROPERTIES COMPILE_OPTIONS "-w")
        target_link_libraries(${_t} PRIVATE m)
    endif()

    # ── OpenMP (optional) ───────────────────────────────────────────────────
    # On Apple the project's cmake/FindOpenMP.cmake shim locates Homebrew's
    # libomp; it must be included by FULL PATH. Elsewhere that same file
    # SHADOWS CMake's built-in FindOpenMP because cmake/ sits first on
    # CMAKE_MODULE_PATH and the shim's body is Apple-only — so drop cmake/
    # from the module path for the duration of the probe or Linux/Windows
    # workers silently build serial.
    if(APPLE)
        include("${CMAKE_SOURCE_DIR}/cmake/FindOpenMP.cmake")
    else()
        set(_mp "${CMAKE_MODULE_PATH}")
        list(REMOVE_ITEM CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
        find_package(OpenMP COMPONENTS C)
        set(CMAKE_MODULE_PATH "${_mp}")
    endif()
    if(TARGET OpenMP::OpenMP_C)
        target_link_libraries(${_t} PRIVATE OpenMP::OpenMP_C)
        message(STATUS "Legacy worker ${ARG_VERSION}: OpenMP ON")
    else()
        message(STATUS "Legacy worker ${ARG_VERSION}: OpenMP not found — building serial")
    endif()

    # macOS ships the whole .app (install(TARGETS SWMMVis BUNDLE …)), and the
    # bundle block copies the worker into Contents/MacOS. Elsewhere the worker
    # is installed beside the executable like the 5.3.0 one.
    if(NOT APPLE)
        install(TARGETS ${_t} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
    endif()
endfunction()
