# --------------------------------------------------------------------------
# C++ Genetic Algorithm for Utility AI optimization
#
# Builds:
#   aoc_evolve_lib  - static lib with the GA, fitness evaluator, and balance
#                     tuner. Split out from the executable so unit tests can
#                     link the internals (GA operators, fitness components,
#                     balance metrics) without going through main().
#   aoc_evolve      - thin executable (main.cpp) linking aoc_evolve_lib.
#
# Both embed the full game simulation for fitness evaluation and link aoc_lib.
# --------------------------------------------------------------------------

find_package(Threads REQUIRED)

# Shared compiler warnings (match root project settings).
function(aoc_evolve_warnings tgt)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${tgt} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
            -Wnon-virtual-dtor -Woverloaded-virtual
            -Wnull-dereference -Wdouble-promotion
            -Wformat=2
            -pipe
        )
    endif()
endfunction()

add_library(aoc_evolve_lib STATIC
    GeneticAlgorithm.cpp
    FitnessEvaluator.cpp
    BalanceTuner.cpp
)

# Consumers (aoc_evolve, tests) get this dir on their include path so they can
# #include "GeneticAlgorithm.hpp" etc.
target_include_directories(aoc_evolve_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(aoc_evolve_lib PUBLIC aoc_lib Threads::Threads)
aoc_evolve_warnings(aoc_evolve_lib)

add_executable(aoc_evolve main.cpp)
target_link_libraries(aoc_evolve PRIVATE aoc_evolve_lib)
aoc_evolve_warnings(aoc_evolve)
