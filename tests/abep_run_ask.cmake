# The questions of abep_run and its start of the orbit view (docs/OUTPUT.md), run by ctest as
#
#     cmake -DABEP_RUN=<abep_run> -DFAKE_VIEWER=<fake_viewer> -DWORK=<folder> -P abep_run_ask.cmake
#
# The answers are given to abep_run on its standard input, from files. The real orbit view is a Godot program, which the tests must not
# need, so ABEP_VIEWER names a stand-in (tests/tools/fake_viewer.cpp) that writes viewer_started.txt, with the arguments it was started
# with, into the folder of results it is given. What each run printed is kept in <WORK>/<name>_output.txt, for a test that fails.

cmake_minimum_required(VERSION 3.16)

foreach(required ABEP_RUN FAKE_VIEWER WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

# Runs abep_run in the folder `cwd`, with `input` on its standard input and the rest of the arguments as its arguments. Leaves what it
# printed and its exit code in <name>_OUT, <name>_ERR and <name>_CODE.
function(run_abep name cwd input)
    file(MAKE_DIRECTORY "${cwd}")
    file(WRITE "${WORK}/${name}_input.txt" "${input}")
    execute_process(COMMAND "${ABEP_RUN}" ${ARGN}
                    INPUT_FILE "${WORK}/${name}_input.txt"
                    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE code
                    WORKING_DIRECTORY "${cwd}")
    file(WRITE "${WORK}/${name}_output.txt" "${out}\n--- standard error:\n${err}\n--- exit code: ${code}\n")
    set(${name}_OUT "${out}" PARENT_SCOPE)
    set(${name}_ERR "${err}" PARENT_SCOPE)
    set(${name}_CODE "${code}" PARENT_SCOPE)
endfunction()

function(expect_contains text needle what)
    string(FIND "${text}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${what}: expected to find\n    ${needle}\nin\n${text}")
    endif()
endfunction()

function(expect_missing text needle what)
    string(FIND "${text}" "${needle}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "${what}: did not expect to find\n    ${needle}\nin\n${text}")
    endif()
endfunction()

function(expect_equal actual expected what)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "${what}: expected '${expected}', got '${actual}'")
    endif()
endfunction()

function(expect_file path what)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${what}: ${path} does not exist")
    endif()
endfunction()

function(expect_no_file path what)
    if(EXISTS "${path}")
        message(FATAL_ERROR "${what}: ${path} exists, and the orbit view should not have been started")
    endif()
endfunction()

# The stand-in is started without being waited for, so its file may come a moment after abep_run has exited.
function(wait_for_file path what)
    foreach(attempt RANGE 1 150)
        if(EXISTS "${path}")
            return()
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.1)
    endforeach()
    message(FATAL_ERROR "${what}: the stand-in for the orbit view did not write ${path}")
endfunction()

# The orbit view was started the way abep_run says it starts it: `viewer -- FOLDER`, with the folder as a full path, on results that
# are there.
function(expect_viewer_started results what)
    wait_for_file("${results}/viewer_started.txt" "${what}")
    file(READ "${results}/viewer_started.txt" started)
    expect_contains("${started}" "argument 1: --" "${what}")
    if(NOT started MATCHES "argument 2: (/|[A-Za-z]:)[^\n]*${results_name}")
        message(FATAL_ERROR "${what}: the second argument should be the full path of the results folder (${results_name}), got:\n${started}")
    endif()
    expect_missing("${started}" "argument 3" "${what}")
    expect_contains("${started}" "samples.csv found: yes" "${what}")
endfunction()

# The first row of samples.csv starts at the altitude asked for.
function(expect_start_altitude results altitude what)
    file(READ "${results}/samples.csv" samples)
    if(NOT samples MATCHES "\n0,[A-Za-z]+,(${altitude})[.,]")
        message(FATAL_ERROR "${what}: the first sample should be at ${altitude} km, got:\n${samples}")
    endif()
endfunction()

set(common duration_days=0.05 decay_max_years=0.02)
string(REPLACE ";" " " common_text "${common}")  # the same, as it is printed
set(ENV{ABEP_PAUSE} 0)
set(ENV{ABEP_VIEWER} "${FAKE_VIEWER}")

# --- 1. --ask: the answers are used, an argument gives the value a question starts from, a name=value line sets another setting,
#        and y starts the orbit view.
set(results "${WORK}/ask_yes")
set(results_name ask_yes)
run_abep(ask_yes "${WORK}/cwd" "400\n200\n\n\n2\n\nmass_kg=210\n\ny\n" --ask ${common} "out=${results}")
expect_equal("${ask_yes_CODE}" 0 "--ask with answers")
expect_contains("${ask_yes_OUT}" "Orbit altitude, km [250]: " "--ask with answers")
expect_contains("${ask_yes_OUT}" "Length of the run, days [0.05]: " "--ask with answers")
expect_contains("${ask_yes_OUT}" "altitude_km=400 mass_kg=200 atmosphere=2 mass_kg=210" "--ask with answers")
expect_contains("${ask_yes_OUT}" "atmosphere: ECSS moderate solar activity" "--ask with answers")
expect_contains("${ask_yes_OUT}" "Show this run in the orbit view? [Y/n] " "--ask with answers")
expect_contains("${ask_yes_OUT}" "Starting the orbit view" "--ask with answers")
expect_file("${results}/summary.csv" "--ask with answers")
expect_file("${results}/orbits.csv" "--ask with answers")
expect_start_altitude("${results}" "399|400" "--ask with answers")
expect_viewer_started("${results}" "--ask with answers")

# --- 2. A wrong answer is said to be wrong and asked again; n does not start the orbit view.
set(results "${WORK}/ask_bad")
run_abep(ask_bad "${WORK}/cwd" "abc\n3,5\n300\n\n\n\n9\n1\n\n\nn\n" --ask ${common} "out=${results}")
expect_equal("${ask_bad_CODE}" 0 "--ask with wrong answers")
expect_contains("${ask_bad_OUT}" "'abc' is not a number." "--ask with wrong answers")
expect_contains("${ask_bad_OUT}" "'3,5' is not a number (use a point for decimals)." "--ask with wrong answers")
expect_contains("${ask_bad_OUT}" "setting 'atmosphere' must be 0 (the 1976 standard)" "--ask with wrong answers")
expect_contains("${ask_bad_OUT}" "altitude_km=300 atmosphere=1" "--ask with wrong answers")
expect_missing("${ask_bad_OUT}" "altitude_km=300 mass_kg" "--ask with wrong answers")
expect_contains("${ask_bad_OUT}" "atmosphere: ECSS low solar activity" "--ask with wrong answers")
expect_start_altitude("${results}" "299|300" "--ask with wrong answers")
expect_missing("${ask_bad_OUT}" "Starting the orbit view" "--ask with wrong answers")
expect_no_file("${results}/viewer_started.txt" "--ask answered n")

# --- 3. A name=value line is checked too: a setting that does not exist, a value that is not a number and one that makes no sense are
#        refused, ? lists the settings, and out= names the folder.
set(results "${WORK}/ask_lines")
run_abep(ask_lines "${WORK}/cwd"
         "\n\n\n\n\n\nnonsense=1\nmass_kg=heavy\nsample_every=2.5\n?\nthrust_in_eclipse=1\nout=${results}\n\nn\n"
         --ask ${common})
expect_equal("${ask_lines_CODE}" 0 "--ask with name=value lines")
expect_contains("${ask_lines_OUT}" "unknown setting 'nonsense'" "--ask with name=value lines")
expect_contains("${ask_lines_OUT}" "'heavy' is not a number." "--ask with name=value lines")
expect_contains("${ask_lines_OUT}" "setting 'sample_every' must be a whole number" "--ask with name=value lines")
expect_contains("${ask_lines_OUT}" "  altitude_km = 250   " "--ask with name=value lines")
expect_contains("${ask_lines_OUT}" "thrust_in_eclipse=1" "--ask with name=value lines")
expect_missing("${ask_lines_OUT}" "sample_every=2.5" "--ask with name=value lines")
expect_file("${results}/samples.csv" "--ask with name=value lines")

# --- 4. The end of the input in the middle of the questions leaves the rest as they were, and does not start the orbit view. The input
#        starts with a byte order mark, as a file saved by some editors does, which is not part of the first answer.
string(ASCII 239 187 191 bom)
set(results "${WORK}/ask_eof")
run_abep(ask_eof "${WORK}/cwd" "${bom}350\n" --ask ${common} "out=${results}")
expect_equal("${ask_eof_CODE}" 0 "--ask with the input ending")
expect_contains("${ask_eof_OUT}" "Running: abep_run ${common_text} out=${results} altitude_km=350" "--ask with the input ending")
expect_start_altitude("${results}" "349|350" "--ask with the input ending")
expect_no_file("${results}/viewer_started.txt" "--ask with the input ending")

# --- 5. A wrong argument stops the run before the first question: one that is not a number, and one that makes no sense (which would
#        otherwise make every answer look wrong).
run_abep(ask_wrong "${WORK}/cwd" "" --ask altitude_km=high)
expect_equal("${ask_wrong_CODE}" 2 "--ask with a wrong argument")
expect_contains("${ask_wrong_ERR}" "'high' is not a number" "--ask with a wrong argument")
expect_missing("${ask_wrong_OUT}" "Orbit altitude" "--ask with a wrong argument")
run_abep(ask_senseless "${WORK}/cwd" "" --ask sample_every=2.5)
expect_equal("${ask_senseless_CODE}" 2 "--ask with an argument that makes no sense")
expect_contains("${ask_senseless_ERR}" "setting 'sample_every' must be a whole number" "--ask with an argument that makes no sense")
expect_missing("${ask_senseless_OUT}" "Orbit altitude" "--ask with an argument that makes no sense")

# --- 6. Started by a double-click (ABEP_PAUSE=1 stands in for it): with nothing to do it asks, and Enter at "Show this run" says yes.
set(ENV{ABEP_PAUSE} 1)
set(results "${WORK}/double_click/abep_out")
set(results_name abep_out)
run_abep(double_click "${WORK}/double_click" "300\n\n\n\n0\n0.05\ndecay_max_years=0.02\n\n\n\n")
expect_equal("${double_click_CODE}" 0 "a double-click")
expect_contains("${double_click_OUT}" "Running: abep_run altitude_km=300 atmosphere=0 duration_days=0.05 decay_max_years=0.02" "a double-click")
expect_contains("${double_click_OUT}" "Starting the orbit view" "a double-click")
expect_contains("${double_click_OUT}" "The orbit view is opening" "a double-click")
expect_contains("${double_click_OUT}" "Press Enter to close this window." "a double-click")
expect_start_altitude("${results}" "299|300" "a double-click")
expect_viewer_started("${results}" "a double-click")

# --- 7. The same without an orbit view to offer: nobody is asked about it, and the window says what the program is.
unset(ENV{ABEP_VIEWER})
set(results "${WORK}/double_click_no_viewer/abep_out")
run_abep(double_click_no_viewer "${WORK}/double_click_no_viewer" "\n\n\n\n\n0.05\ndecay_max_years=0.02\n\n\n")
expect_equal("${double_click_no_viewer_CODE}" 0 "a double-click without an orbit view")
expect_missing("${double_click_no_viewer_OUT}" "orbit view?" "a double-click without an orbit view")
expect_contains("${double_click_no_viewer_OUT}" "abep_run is a command-line program" "a double-click without an orbit view")
expect_contains("${double_click_no_viewer_OUT}" "Press Enter to close this window." "a double-click without an orbit view")
expect_file("${results}/summary.csv" "a double-click without an orbit view")

# --- 8. A double-click whose run stops with an error says so in its window, and waits. (decay_max_years=0 passes the questions, which
#        check what make_mission() checks, and is refused by the run.)
run_abep(double_click_error "${WORK}/double_click_error" "\n\n\n\n\n0.05\ndecay_max_years=0\n\n\n")
expect_equal("${double_click_error_CODE}" 2 "a double-click that fails")
expect_contains("${double_click_error_ERR}" "setting 'decay_max_years' must be positive" "a double-click that fails")
expect_contains("${double_click_error_OUT}" "abep_run stopped with an error" "a double-click that fails")
expect_contains("${double_click_error_OUT}" "Press Enter to close this window." "a double-click that fails")

# --- 9. --view: starts the orbit view without asking; with no orbit view to start it is an error, after the run.
set(ENV{ABEP_PAUSE} 0)
set(ENV{ABEP_VIEWER} "${FAKE_VIEWER}")
set(results "${WORK}/view")
set(results_name view)
run_abep(view "${WORK}/cwd" "" ${common} "out=${results}" --view)
expect_equal("${view_CODE}" 0 "--view")
expect_missing("${view_OUT}" "orbit view? [Y/n]" "--view")
expect_contains("${view_OUT}" "Starting the orbit view" "--view")
expect_viewer_started("${results}" "--view")

set(ENV{ABEP_VIEWER} "${WORK}/no_such_program")
set(results "${WORK}/view_missing")
run_abep(view_missing "${WORK}/cwd" "" ${common} "out=${results}" --view)
expect_equal("${view_missing_CODE}" 2 "--view with an ABEP_VIEWER that names nothing")
expect_contains("${view_missing_ERR}" "ABEP_VIEWER names does not exist" "--view with an ABEP_VIEWER that names nothing")
expect_file("${results}/summary.csv" "--view with an ABEP_VIEWER that names nothing")

unset(ENV{ABEP_VIEWER})
set(results "${WORK}/view_none")
run_abep(view_none "${WORK}/cwd" "" ${common} "out=${results}" --view)
expect_equal("${view_none_CODE}" 2 "--view with no orbit view")
expect_contains("${view_none_ERR}" "the orbit view was not found" "--view with no orbit view")

# --- 10. Without --ask, and not started by a double-click, nothing is asked: the input is never read.
run_abep(quiet "${WORK}/cwd" "400\n" ${common} "out=${WORK}/quiet")
expect_equal("${quiet_CODE}" 0 "a plain run")
expect_missing("${quiet_OUT}" "Orbit altitude" "a plain run")
expect_start_altitude("${WORK}/quiet" "249|250" "a plain run")

# --- 11. --help lists the new options and still lists the settings in the form that deploy_to_itch.ps1 reads.
run_abep(help "${WORK}/cwd" "" --help)
expect_equal("${help_CODE}" 0 "--help")
expect_contains("${help_OUT}" "--ask" "--help")
expect_contains("${help_OUT}" "--view" "--help")
expect_contains("${help_OUT}" "ABEP_VIEWER" "--help")
expect_contains("${help_OUT}" "  altitude_km = 250   circular orbit" "--help")

message(STATUS "abep_run_ask: all checks passed")
