# Physics Rewrite — Session Handoff

Paste the prompt below into a fresh Claude Code session to continue the
multi-week physics-first map-generation rewrite without re-loading any
of the prior session's chat context.

## Continuation prompt (copy-paste verbatim)

```
Continue physics-first plate-tectonic rewrite.

Read these first (in order):
  1. ~/.claude/projects/-home-ndietz-Repositories-private-age-of-civ/memory/project_physics_rewrite_2026-05-05.md
  2. docs/SPHERE_MIGRATION.md
  3. include/aoc/map/gen/PlatePhysics.hpp
  4. src/map/gen/PlatePhysics.cpp

Memory file lists the most recent SHIPPED phase + the NEXT sub-step.
Pick up at the NEXT sub-step. Do exactly one sub-step per turn:
  - implement
  - build clean (cmake --build build -j$(nproc))
  - 8-12 seed audit at 140x90 default (mtn count via tr -cd '^' on map text)
  - update the memory file: mark sub-step DONE, write the next NEXT
  - one-line summary to me

After each sub-step, stop and wait. I will say "go on" to continue.
```

## How the workflow works

The memory file at
`~/.claude/projects/-home-ndietz-Repositories-private-age-of-civ/memory/project_physics_rewrite_2026-05-05.md`
is the single source of truth for "where we are." Each session updates
it before stopping. New sessions read it and pick up.

This keeps the chat context small because:
- No need to re-explain the goal — memory has it.
- No need to re-list ripped LOC, audit numbers, tuning constants — memory
  has them.
- The prompt above is ~150 tokens; the memory file is ~3 KB; that's the
  full handoff.

## Pattern for any long multi-phase task

1. **Pick a project memory filename up front.** e.g.
   `project_<task>_<date>.md`. All sessions write to that one file.

2. **Memory file structure:**
   - Goal + scope at top (immutable).
   - Per phase: `SHIPPED <date>` block with what changed + audit numbers.
   - One `NEXT` block listing the next concrete sub-step.

3. **Each session does exactly ONE sub-step**, audits, updates memory,
   stops. Don't chain — chained work bloats context and the audit signal
   gets lost.

4. **Continuation prompts are tiny.** Just "read memory file X, do the
   NEXT block." The agent does not need to know what came before — the
   memory file is the codified summary.

5. **When a phase finishes**, append a one-line entry to MEMORY.md
   pointing to the project memory file, so future general queries find
   it.

## Why not /compact

`/compact` summarises the in-flight chat. It loses precise audit
numbers, tuning constants, and the "what comes next" list. Memory files
preserve those exactly. Use `/compact` for general drift; use this
pattern for multi-week structured work.
