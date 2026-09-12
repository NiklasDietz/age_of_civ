# Drive aoc_simulate on several seeds, then assert the runs are healthy.
#
# A multi-step CTest needs a driver script because add_test() takes a single
# command. Mirrors the shape of scripts/test_determinism.sh, but in CMake so it
# works without a shell.
#
# Expects: SIM, PY, SCRIPT, OUT. Optional: SEEDS (a ;-list, default 42;43;44).

if(NOT SIM OR NOT PY OR NOT SCRIPT OR NOT OUT)
    message(FATAL_ERROR "run_sim_health.cmake requires -DSIM, -DPY, -DSCRIPT, -DOUT")
endif()

if(NOT SEEDS)
    set(SEEDS "42;43;44")
endif()

# 200 turns / 6 players / three fixed seeds, of which a majority must pass.
# Long enough for the era clock to advance several times and for expansion and
# trade to have clearly happened or clearly not; short enough to stay a routine
# gate.
#
# Why a majority rather than a single verdict: one 200-turn trajectory is
# chaotic. On 2026-09-12 seed 42 failed H5 (city-count Gini 0.56 at turn 100)
# because one unhappy eleven-city empire flipped wholesale to its neighbour
# over twelve turns, while seeds 43 and 44 passed every check on the same
# build. A real regression shows up on every seed; a chaotic tail shows up on
# one, and a gate that cannot tell them apart gets ignored.

get_filename_component(_out_dir "${OUT}" DIRECTORY)
get_filename_component(_out_name "${OUT}" NAME_WE)

set(_passed 0)
set(_report "")

foreach(_seed IN LISTS SEEDS)
    set(_csv "${_out_dir}/${_out_name}_seed${_seed}.csv")
    execute_process(
        COMMAND "${SIM}" --turns 200 --players 6 --seed "${_seed}" --output "${_csv}"
        RESULT_VARIABLE sim_rc
        OUTPUT_VARIABLE sim_out
        ERROR_VARIABLE sim_err)

    if(NOT sim_rc EQUAL 0)
        message(FATAL_ERROR "aoc_simulate failed on seed ${_seed} (rc=${sim_rc}):\n${sim_err}")
    endif()

    execute_process(
        COMMAND "${PY}" "${SCRIPT}" "${_csv}"
        RESULT_VARIABLE health_rc
        OUTPUT_VARIABLE health_out
        ERROR_VARIABLE health_err)

    # Always show the per-check report: on a red run the measured values are
    # the whole point, and on a green one they are a free record of where the
    # simulation sits.
    message("=== seed ${_seed} ===\n${health_out}")

    if(health_rc EQUAL 0)
        math(EXPR _passed "${_passed} + 1")
        string(APPEND _report "  seed ${_seed}: PASS\n")
    else()
        string(APPEND _report "  seed ${_seed}: FAIL\n${health_err}")
    endif()
endforeach()

list(LENGTH SEEDS _seed_count)
math(EXPR _required "${_seed_count} / 2 + 1")

message("sim health: ${_passed} of ${_seed_count} seeds pass, ${_required} required\n${_report}")

if(_passed LESS _required)
    message(FATAL_ERROR
            "sim health checks failed: ${_passed} of ${_seed_count} seeds pass, ${_required} required\n${_report}")
endif()
