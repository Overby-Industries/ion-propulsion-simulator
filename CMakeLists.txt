cmake_minimum_required(VERSION 3.16)
project(ion-propulsion-simulator LANGUAGES CXX VERSION 0.0.1)

# abep -- an open, deterministic simulator of drag and air-breathing electric
# propulsion in low Earth orbit. See docs/SPEC.md.
#
# Consume it from another CMake project with:
#
#     add_subdirectory(path/to/ion-propulsion-simulator)
#     target_link_libraries(your_target PRIVATE abep::abep)
#
# That adds the library only. The tests are built when this repository is the
# top-level project.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ABEP_IS_TOP_LEVEL ON)
else()
    set(ABEP_IS_TOP_LEVEL OFF)
endif()

option(ABEP_BUILD_TESTS "Build the abep test suite" ${ABEP_IS_TOP_LEVEL})

# Header-only for now; it becomes a compiled library when the propagator lands.
add_library(abep INTERFACE)
add_library(abep::abep ALIAS abep)

target_compile_features(abep INTERFACE cxx_std_17)

target_include_directories(abep INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(ABEP_BUILD_TESTS)
    enable_testing()

    add_executable(abep_tests
        tests/test_main.cpp
        tests/test_units.cpp
        tests/test_version.cpp
    )
    target_link_libraries(abep_tests PRIVATE abep)
    set_target_properties(abep_tests PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(abep_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(abep_tests PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()

    add_test(NAME abep_tests COMMAND abep_tests)
endif()
