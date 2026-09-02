#pragma once

/**
 * @file WilsonSchedule.hpp
 * @brief Prescribed Wilson-cycle schedule for terrane motion.
 *
 * **Why prescribed rather than emergent.**  The thermal-age trigger
 * (`applyWilsonRifting`) asked whether one PLATE held 25 % of continental
 * crust, while the supercontinent is crust welded ACROSS 12–15 plates.
 * Measured: max plate crust share sat at 0.19–0.25 against the 0.25
 * threshold for the entire run, so 2–5 rifts fired in 3 Gy and the land
 * never dispersed.  Lowering the threshold to 0.18 fired more rifts but
 * made things worse: it fragmented the plate tessellation while the land
 * stayed joined, and largest_share_of_land rose from 0.622 to 0.658.
 * No constant can answer a question about the wrong object; that is the
 * evidence for this prescribed approach (commit `4086c17`).
 *
 * **Design (V4 dispersal-only — DISABLED, rate = 0).** A schedule of
 * `numCycles` purely dispersal phases is drawn ONCE at t = 0.  Every cycle
 * has `assemblyStartMy = -9999` (prehistory sentinel), so no assembly phase
 * ever fires.  Assembly poles are still drawn per cycle and serve as the
 * reference axis for per-terrane dispersal targets.
 *
 * Dispersal: each terrane gets a unique target 90 degrees from the current
 * cycle's assembly pole, derived from hash(terrane.id, cycleIndex).  The 90
 * degree offset gives maximum drift-axis strength even when worldPos is near
 * the assembly pole, and unique per-terrane hashes produce N separate
 * destinations rather than all terranes heading to one point.
 *
 * **Why disabled (rate = 0).** Measured on the 24 `DEFAULT_SEEDS` of
 * `tools/mapgen_metrics.py`: V4 at rate = 0.150 costs 18 gates
 * (173/288 -> 155/288) while moving `crust_largest_component_share` by a
 * single seed (9/24 -> 8/24), which is inside the +/-2.2 gate resolution.
 * Root cause: hash-based targets are not guaranteed to spread terranes -- two
 * terranes can draw targets in the same hemisphere -- so the schedule
 * reshuffles which seeds cluster rather than reducing clustering.  The right
 * fix is repulsion-based dispersal (each terrane pushed away from all others
 * via cross-product forces weighted by inverse cosine distance), which
 * reaches Thomson-problem equilibrium and leaves already-separated terranes
 * alone.  Set rate > 0 and replace the `applyTo` hash logic with repulsion
 * sums to activate.
 *
 * At rate = 0 this class is gate-identical to not calling it at all (all
 * twelve gate medians reproduce the `e5be590` baseline), since
 * `advanceTerraneRotations` then integrates zero motion.
 *
 * **Per-epoch update.** `applyTo` updates each terrane's
 * `driftPoleLatDeg / driftPoleLonDeg / driftRateDegPerMy` for the
 * current simulation time.  `advanceTerraneRotations` (unchanged)
 * then integrates the motion.
 *
 * All angles in degrees on the public API.  Internal trig in radians.
 */

#include "aoc/map/gen/SphereGeometry.hpp"
#include "aoc/map/gen/Terrane.hpp"

#include <cstdint>
#include <vector>

namespace aoc::map::gen {

/// One Wilson cycle: assembly phase followed by a dispersal phase.
/// Times are in My from the start of the simulation (t = 0).
struct WilsonCycle {
    float assemblyStartMy;    ///< terranes begin converging on the assembly pole
    float peakMy;             ///< full assembly; dispersal begins immediately after
    float dispersalEndMy;     ///< dispersal complete; next cycle's assemblyStartMy
    float assemblePoleLatDeg; ///< pole terranes converge toward
    float assemblePoleLonDeg;
};

/// Prescribed Wilson schedule.  Immutable after construction; one instance
/// lives for the whole simulation.
struct WilsonSchedule {
    /// Guaranteed-ocean pole.  Assembly poles are placed ≥ `capRadDeg` away
    /// from this direction, so the hemisphere it covers is never assembled
    /// and an ocean basin always exists there.
    Vec3 superoceanPole = {0.0f, 0.0f, 1.0f};
    float capRadDeg     = 60.0f;

    /// Terrane drift speed during dispersal phases (deg/My).
    /// Earth-like range: 0.005 – 0.30 deg/My.  At 0.15 deg/My a 350 My
    /// dispersal phase moves terranes ~52°, enough to open an ocean basin.
    /// Set to 0 while the hash-based target logic is replaced with repulsion.
    float rateDegPerMy = 0.0f;

    std::vector<WilsonCycle> cycles;

    /// Update each alive terrane's drift fields for `timeMy`.
    /// Must be called once per epoch, BEFORE `advanceTerraneRotations`.
    void applyTo(std::vector<Terrane>& terranes, float timeMy) const;

    /// Build a schedule backwards from the required end state.
    /// @param seed   XorShift seed (from config.seed); deterministic.
    /// @param totalMy  Total simulated time in My (positive).
    [[nodiscard]] static WilsonSchedule build(uint32_t seed, float totalMy);
};

} // namespace aoc::map::gen
