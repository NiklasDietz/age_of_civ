A leader genome that makes the simulation allocate without bound.

Reproduce (release or release-portable; the DEBUG build does NOT reproduce,
and neither does an -O2 build with -g, so the trajectory is codegen-sensitive):

  ulimit -v 3000000
  build/release/aoc_simulate --turns 200 --players 6 --seed 20260906 \
      --tuned-dir tests/data/ga/runaway_genome --output /tmp/out.csv

Expected today: std::bad_alloc. A healthy run of that length is about 40
seconds and 44 MB.

Where it came from: generation 3, individual 10 of
  aoc_evolve --generations 3 --population 16 --games 1 --turns 200 \
      --players 6 --seed 20260906 --dump-population <dir>
It is the reason the genetic algorithm has never finished more than a couple
of generations at a realistic population size: generation 1 evaluates the
clean archetypes and passes, and a later generation's offspring hits this.

Nothing about the genome is extreme. Every gene sits in an ordinary range
(scienceFocus 2.3, prodBuildings 2.0, militaryAggression 0.65), which is why
this is worth fixing rather than clamping away: it is reachable in normal play.

Already ruled out as the cause: unit and city counts (the stuck games hold
4 cities and 13 units), worldgen (35-40 s and 44 MB standalone on this seed
family), deliberately extreme genes (a hand-built maximal genome runs fine),
a cross-thread override race (the override table is thread_local), and the
headless notification queue (only 465 entries in a failing run, though it is
never drained outside the GUI and does grow for the whole run).
