# Coding Standards

Before starting any task, read `ai_command/router.txt`. It maps task types to the specific standard files you must load. Always load `ai_command/general/core.txt` as a baseline, then load the additional files router.txt specifies for the task at hand. Load only what is relevant -- do not load everything.

For this project the typical set is:
- `ai_command/general/core.txt` (always)
- `ai_command/general/security.txt` (when touching security-relevant code)
- `ai_command/general/testing.txt` (when writing or reviewing tests)
- `ai_command/general/profiling.txt` (when optimizing performance)
- `ai_command/general/data_structures.txt` (when choosing data structures / algorithms)
- `ai_command/general/code_review.txt` (when reviewing code)
- `ai_command/general/debugging.txt` (when debugging)
- `ai_command/general/concurrency_pitfalls.txt` (when dealing with concurrency)
- `ai_command/general/technical_debt.txt` (when refactoring)
- `ai_command/languages/cpp/code_style.txt`
- `ai_command/languages/cpp/memory_and_perf.txt`
- `ai_command/languages/cpp/patterns.txt`
- `ai_command/languages/cpp/build.txt`
- `ai_command/languages/cpp/dependencies.txt`
- `ai_command/languages/cpp/benchmarking.txt`
- `ai_command/languages/cpp/cross_compilation.txt`
- `ai_command/languages/cpp/documentation.txt`
- `ai_command/languages/cpp/simd.txt`
- `ai_command/languages/cpp/embedded.txt`
- `ai_command/languages/cpp/testing.txt`

These files contain binding architectural requirements. Follow them -- they are not suggestions.

## Project Context

<!-- Fill in your project-specific details below: stack, architecture decisions, directory layout, build commands, constraints. -->
