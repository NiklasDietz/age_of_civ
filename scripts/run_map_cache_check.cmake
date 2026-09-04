# Prove the headless map cache is lossless: run one seed twice, the first run
# generating the world and writing the cache, the second loading it, and
# require byte-identical CSV output (main, events, tiles).
#
# Expects: SIM, OUT (a directory).

if(NOT SIM OR NOT OUT)
    message(FATAL_ERROR "run_map_cache_check.cmake requires -DSIM and -DOUT")
endif()

file(MAKE_DIRECTORY "${OUT}")
file(REMOVE "${OUT}/map.aocmap")

foreach(pass generated cached)
    execute_process(
        COMMAND "${SIM}" --turns 30 --players 4 --seed 42
                --map-cache "${OUT}/map.aocmap" --output "${OUT}/${pass}.csv"
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "aoc_simulate (${pass}) failed (rc=${rc}):\n${err}")
    endif()
    set(log_${pass} "${out}${err}")
endforeach()

if(NOT log_cached MATCHES "MapFile: loaded")
    message(FATAL_ERROR "the second run did not load the map cache:\n${log_cached}")
endif()

foreach(suffix "" "_events" "_tiles")
    set(a "${OUT}/generated${suffix}.csv")
    set(b "${OUT}/cached${suffix}.csv")
    if(EXISTS "${a}" OR EXISTS "${b}")
        execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files "${a}" "${b}"
                        RESULT_VARIABLE diff_rc)
        if(NOT diff_rc EQUAL 0)
            message(FATAL_ERROR "cached run diverged from the generated run: ${a} vs ${b}")
        endif()
    endif()
endforeach()
message("map cache equivalence: identical CSV output over 30 turns")
