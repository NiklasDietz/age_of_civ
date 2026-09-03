# Drive aoc_simulate, then assert the run is healthy.
#
# A two-step CTest needs a driver script because add_test() takes a single
# command. Mirrors the shape of scripts/test_determinism.sh, but in CMake so it
# works without a shell.
#
# Expects: SIM, PY, SCRIPT, OUT.

if(NOT SIM OR NOT PY OR NOT SCRIPT OR NOT OUT)
    message(FATAL_ERROR "run_sim_health.cmake requires -DSIM, -DPY, -DSCRIPT, -DOUT")
endif()

# 200 turns / 6 players / fixed seed. Long enough for the era clock to advance
# several times and for expansion and trade to have clearly happened or clearly
# not; short enough to stay a routine gate.
execute_process(
    COMMAND "${SIM}" --turns 200 --players 6 --seed 42 --output "${OUT}"
    RESULT_VARIABLE sim_rc
    OUTPUT_VARIABLE sim_out
    ERROR_VARIABLE sim_err)

if(NOT sim_rc EQUAL 0)
    message(FATAL_ERROR "aoc_simulate failed (rc=${sim_rc}):\n${sim_err}")
endif()

execute_process(
    COMMAND "${PY}" "${SCRIPT}" "${OUT}"
    RESULT_VARIABLE health_rc
    OUTPUT_VARIABLE health_out
    ERROR_VARIABLE health_err)

# Always show the per-check report: on a red run the measured values are the
# whole point, and on a green one they are a free record of where the sim sits.
message("${health_out}")

if(NOT health_rc EQUAL 0)
    message(FATAL_ERROR "sim health checks failed:\n${health_err}")
endif()
