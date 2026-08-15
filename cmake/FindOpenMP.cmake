include_guard(GLOBAL)

# Apple-specific: try Homebrew locations.
#
# Gate on the imported TARGET, not the cached OpenMP_FOUND bool. CMake caches
# OpenMP_FOUND (set CACHE INTERNAL below) but does NOT persist imported targets
# across configure runs — they are recreated each run. On any reconfigure of an
# existing build tree OpenMP_FOUND is already TRUE while OpenMP::OpenMP_CXX does
# not yet exist; gating on `NOT OpenMP_FOUND` would then skip this body and never
# recreate the target, so every downstream `target_link_libraries(... OpenMP::
# OpenMP_CXX)` — including the engine's Kokkos targets consumed via
# find_package(OpenSWMMEngine) — fails "target was not found". Gating on
# `NOT TARGET` recreates it whenever missing. Mirrors openswmm.engine's
# cmake/FindOpenMP.cmake so both projects locate libomp identically.
if(APPLE AND NOT TARGET OpenMP::OpenMP_CXX)
    message(STATUS "Searching for Homebrew OpenMP...")
    
    # Common Homebrew paths
    set(HOMEBREW_PATHS
        /opt/homebrew/opt/libomp  # Apple Silicon
        /usr/local/opt/libomp     # Intel
    )
    
    foreach(HOMEBREW_PATH ${HOMEBREW_PATHS})
        if(EXISTS "${HOMEBREW_PATH}")
            message(STATUS "Found Homebrew libomp at ${HOMEBREW_PATH}")
            
            # Create imported targets. GLOBAL so they outlive the directory
            # scope of whichever CMakeLists.txt first triggers this finder;
            # without GLOBAL a later consume site (or find_dependency) in another
            # scope would not see OpenMP::OpenMP_CXX and would fail to configure.
            if(NOT TARGET OpenMP::OpenMP_C)
                add_library(OpenMP::OpenMP_C INTERFACE IMPORTED GLOBAL)
                target_compile_options(OpenMP::OpenMP_C INTERFACE -Xpreprocessor -fopenmp)
                target_include_directories(OpenMP::OpenMP_C INTERFACE "${HOMEBREW_PATH}/include")
                target_link_libraries(OpenMP::OpenMP_C INTERFACE "${HOMEBREW_PATH}/lib/libomp.dylib")
            endif()
            
            if(NOT TARGET OpenMP::OpenMP_CXX)
                add_library(OpenMP::OpenMP_CXX INTERFACE IMPORTED GLOBAL)
                target_compile_options(OpenMP::OpenMP_CXX INTERFACE -Xpreprocessor -fopenmp)
                target_include_directories(OpenMP::OpenMP_CXX INTERFACE "${HOMEBREW_PATH}/include")
                target_link_libraries(OpenMP::OpenMP_CXX INTERFACE "${HOMEBREW_PATH}/lib/libomp.dylib")
            endif()
            
            # Cache (INTERNAL) so the found-state is visible in every scope, not
            # just the caller's — a PARENT_SCOPE set would only reach one level
            # up and leave sibling/nested scopes (and a later find_dependency)
            # thinking OpenMP was not found.
            set(OpenMP_FOUND TRUE CACHE INTERNAL "OpenMP located via Homebrew libomp")
            set(OpenMP_C_FOUND TRUE CACHE INTERNAL "OpenMP C located via Homebrew libomp")
            set(OpenMP_CXX_FOUND TRUE CACHE INTERNAL "OpenMP CXX located via Homebrew libomp")
            
            return()
        endif()
    endforeach()
    
    message(WARNING "OpenMP not found. Install via: brew install libomp")
endif()