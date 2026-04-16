# --------------------------------------------------------------------------
# C++ Genetic Algorithm for Utility AI optimization
#
# Builds the aoc_evolve binary which embeds the full game simulation
# for fitness evaluation. Links against aoc_lib (the game library).
# --------------------------------------------------------------------------

add_executable(aoc_evolve
    main.cpp
    GeneticAlgorithm.cpp
    FitnessEvaluator.cpp
)

target_include_directories(aoc_evolve PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(aoc_evolve PRIVATE aoc_lib)

# Threading support (required for std::jthread, std::counting_semaphore)
find_package(Threads REQUIRED)
target_link_libraries(aoc_evolve PRIVATE Threads::Threads)

# Compiler warnings (match root project settings)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(aoc_evolve PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        -Wnon-virtual-dtor -Woverloaded-virtual
        -Wnull-dereference -Wdouble-promotion
        -Wformat=2
        -pipe
    )
endif()
