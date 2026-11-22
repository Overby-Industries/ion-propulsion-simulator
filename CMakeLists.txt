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
        tests/test_atmosphere.cpp
        tests/test_constants.cpp
        tests/test_csv.cpp
        tests/test_drag.cpp
        tests/test_eclipse.cpp
        tests/test_flight.cpp
        tests/test_mission.cpp
        tests/test_orbit.cpp
        tests/test_power.cpp
        tests/test_published.cpp
        tests/test_romano2018.cpp
        tests/test_run_config.cpp
        tests/test_standard_atmosphere_1976.cpp
        tests/test_summary.cpp
        tests/test_sweep.cpp
        tests/test_thermal.cpp
        tests/test_thrust.cpp
        tests/test_units.cpp
        tests/test_version.cpp
    )
    target_link_libraries(abep_tests PRIVATE abep)
    # The standard-atmosphere test reads the transcribed table from data/.
    target_compile_definitions(abep_tests PRIVATE ABEP_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    set_target_properties(abep_tests PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(abep_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(abep_tests PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()

    add_test(NAME abep_tests COMMAND abep_tests)
endif()

# The command-line program: runs a mission and writes CSV (docs/OUTPUT.md).
option(ABEP_BUILD_EXAMPLES "Build the example programs" ${ABEP_IS_TOP_LEVEL})
if(ABEP_BUILD_EXAMPLES)
    add_executable(abep_run examples/abep_run.cpp)
    target_link_libraries(abep_run PRIVATE abep)
    set_target_properties(abep_run PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(abep_run PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(abep_run PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()

    if(ABEP_BUILD_TESTS)
        # A short run end to end: the program has to build, run and write its files.
        add_test(NAME abep_run_smoke COMMAND abep_run duration_days=0.05 out=${CMAKE_CURRENT_BINARY_DIR}/abep_run_smoke)
    endif()
endif()

# The sweep program: the same mission over a grid of settings, one row of results per point (docs/SWEEPS.md).
if(ABEP_BUILD_EXAMPLES)
    add_executable(abep_sweep examples/abep_sweep.cpp)
    target_link_libraries(abep_sweep PRIVATE abep)
    set_target_properties(abep_sweep PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(abep_sweep PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(abep_sweep PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()

    if(ABEP_BUILD_TESTS)
        # Four short runs end to end: the program has to build, run and write its table.
        add_test(NAME abep_sweep_smoke COMMAND abep_sweep altitude_km=200:250:2 thrust_density_mn_per_kw=10,14
                 duration_days=0.05 decay_max_years=0.02 out=${CMAKE_CURRENT_BINARY_DIR}/abep_sweep_smoke.csv)
        # And it has to refuse a sweep of nothing, and a setting that does not exist (exit code 2, not 0).
        add_test(NAME abep_sweep_needs_an_axis COMMAND abep_sweep altitude_km=250)
        add_test(NAME abep_sweep_unknown_setting COMMAND abep_sweep nonsense=1,2)
        set_tests_properties(abep_sweep_needs_an_axis abep_sweep_unknown_setting PROPERTIES WILL_FAIL TRUE)
    endif()
endif()

# The Python module (pybind11): run(), write_csv() and settings(), the same run as abep_run (docs/OUTPUT.md). Off by default, so
# that the library and its tests need nothing but a C++17 compiler. Build it with
#     cmake -S . -B build -DABEP_BUILD_PYTHON=ON -Dpybind11_DIR=$(python -m pybind11 --cmakedir)
option(ABEP_BUILD_PYTHON "Build the Python module (needs pybind11)" OFF)
if(ABEP_BUILD_PYTHON)
    set(PYBIND11_FINDPYTHON ON)  # let pybind11 use CMake's own FindPython (CMake 3.18 or newer); it sets Python_EXECUTABLE
    find_package(pybind11 CONFIG REQUIRED)
    pybind11_add_module(abep_python MODULE python/abep_module.cpp)
    target_link_libraries(abep_python PRIVATE abep)
    # The target has another name than the module because `abep` is the library; the module has to be called abep.
    set_target_properties(abep_python PROPERTIES OUTPUT_NAME abep CXX_EXTENSIONS OFF)
    if(ABEP_BUILD_TESTS)
        if(DEFINED Python_EXECUTABLE)
            set(ABEP_PYTHON_FOR_TESTS "${Python_EXECUTABLE}")
        else()
            set(ABEP_PYTHON_FOR_TESTS "${PYTHON_EXECUTABLE}")
        endif()
        add_test(NAME abep_python COMMAND "${ABEP_PYTHON_FOR_TESTS}" "${CMAKE_CURRENT_SOURCE_DIR}/python/test_abep.py")
        set_tests_properties(abep_python PROPERTIES ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:abep_python>")
    endif()
endif()
