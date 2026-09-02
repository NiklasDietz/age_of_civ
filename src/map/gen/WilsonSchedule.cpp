#include "aoc/map/gen/WilsonSchedule.hpp"

#include "aoc/map/gen/SphereField.hpp"
#include "aoc/map/gen/SphereGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::map::gen {

namespace {

// ---------------------------------------------------------------------------
// Minimal XorShift32 used for schedule construction only.
// Deterministic per-world-seed; does not share state with physicsRngState.
// ---------------------------------------------------------------------------
inline uint32_t xorshift32(uint32_t& s) noexcept {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

inline float xorshift01(uint32_t& s) noexcept {
    return static_cast<float>(xorshift32(s)) / static_cast<float>(0xFFFFFFFFu);
}

inline float dot3(Vec3 a, Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross3(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float len3(Vec3 v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline Vec3 normalize3(Vec3 v) noexcept {
    const float l = len3(v);
    if (l < 1e-9f) return {0.0f, 0.0f, 1.0f};
    return {v.x / l, v.y / l, v.z / l};
}

/// Angular separation in degrees between two unit vectors.
inline float angSepDeg(Vec3 a, Vec3 b) noexcept {
    const float d = std::clamp(dot3(a, b), -1.0f, 1.0f);
    return std::acos(d) * (180.0f / static_cast<float>(M_PI));
}

/// Random unit vector at least `minSepDeg` away from `avoid`.
Vec3 randomPoleAvoiding(Vec3 avoid, float minSepDeg, uint32_t& rng) noexcept {
    for (int32_t attempt = 0; attempt < 64; ++attempt) {
        const float z   = xorshift01(rng) * 2.0f - 1.0f;
        const float phi = xorshift01(rng) * 2.0f * static_cast<float>(M_PI);
        const float r   = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const Vec3 pole = {r * std::cos(phi), r * std::sin(phi), z};
        if (angSepDeg(pole, avoid) >= minSepDeg) return pole;
    }
    return normalize3({-avoid.z, avoid.y, avoid.x});
}

} // namespace

// ---------------------------------------------------------------------------
// WilsonSchedule::build
//
// FORWARD CONSTRUCTION — starts at t = 0 in a dispersal phase so the seeded
// cratons are spreading from the first epoch, not converging.
//
// Geological motivation: the simulation opens at ~3 Ga, well into the
// Archean.  By then the cratons had already been through at least one
// assembly cycle (the Kenorland/Nuna chain), and the opening state IS a
// dispersal.  Starting with an assembly phase instead would re-concentrate
// the freshly-seeded compact blocks before any ocean opens, and the
// resulting supercontinent could not escape: all terrane drift axes point
// in the same direction when the centroid is the assembly pole, so
// cross(assemblePole, worldPos) ≈ 0 and terranes drift as one blob.
//
// Phase order within each cycle: DISPERSAL then ASSEMBLY.  After assembly
// the terranes are again near the assembly pole; the next cycle's dispersal
// (toward a unique per-terrane target 90° away) separates them again.
//
// Per-terrane dispersal targets: each terrane k gets a unique direction 90°
// from the assembly pole, computed from hash(terraneId, cycleIndex).
// This is what produces N separate continents rather than N terranes all
// moving in the same direction.  It is stored by deriving the target at
// each applyTo call from the hash — deterministic, zero per-terrane storage.
// ---------------------------------------------------------------------------

WilsonSchedule WilsonSchedule::build(uint32_t seed, float totalMy) {
    WilsonSchedule sched;
    if (totalMy <= 0.0f) return sched;

    uint32_t rng = seed ^ 0x57494C53u; // "WILS"
    if (rng == 0u) rng = 0xBEEFCAFEu;

    // Superocean pole.
    {
        const float z        = xorshift01(rng) * 2.0f - 1.0f;
        const float phi      = xorshift01(rng) * 2.0f * static_cast<float>(M_PI);
        const float r        = std::sqrt(std::max(0.0f, 1.0f - z * z));
        sched.superoceanPole = {r * std::cos(phi), r * std::sin(phi), z};
    }

    // Number of cycles: totalMy / 600 My, clamped [3, 6].
    const int32_t numCycles = std::clamp(static_cast<int32_t>(totalMy / 600.0f + 0.5f), 3, 6);

    // Forward construction: the first cycle starts with dispersal at t = 0.
    // Within each cycle: dispersal phase, then assembly phase.
    // The assembly pole for each cycle is drawn independently, ≥ capRadDeg
    // from the superoceanPole.
    float cursor = 0.0f;
    for (int32_t ci = 0; ci < numCycles; ++ci) {
        const float jA = (xorshift01(rng) - 0.5f) * 60.0f; // ±30 My
        const float jD = (xorshift01(rng) - 0.5f) * 80.0f; // ±40 My
        [[maybe_unused]] const float assemblyDuration =
            std::clamp(250.0f + jA, 150.0f, 400.0f); // kept for rng parity
        const float dispersalDuration = std::clamp(350.0f + jD, 200.0f, 500.0f);

        WilsonCycle cyc;
        // Phase order within cycle: dispersal [cursor, cursor+D), then
        // assembly [cursor+D, cursor+D+A).
        //
        // For cycle 0: assembly was in prehistory, so assemblyStartMy is a
        // sentinel. dispersalEnd is when the first assembly begins.
        cyc.assemblePoleLatDeg = 0.0f; // filled below
        cyc.assemblePoleLonDeg = 0.0f;

        if (ci == 0) {
            // Dispersal runs from t=0.  No assembly phase for this cycle
            // (it happened in the geologic past before the simulation opens).
            cyc.assemblyStartMy = -9999.0f; // prehistory sentinel
            cyc.peakMy          = 0.0f;     // dispersal starts now
            cyc.dispersalEndMy  = dispersalDuration;
        } else {
            // V4 dispersal-only: no assembly phase for any cycle.
            // The assembly pole is still drawn so each cycle has a unique
            // reference pole for the per-terrane dispersal hash.  Terranes
            // change direction every ~350 My but never converge.
            cyc.assemblyStartMy = -9999.0f;
            cyc.peakMy          = cursor;
            cyc.dispersalEndMy  = cursor + dispersalDuration;
        }
        cursor = cyc.dispersalEndMy;

        const Vec3 pole        = randomPoleAvoiding(sched.superoceanPole, sched.capRadDeg, rng);
        const LatLon latLon    = vec3ToLatLon(pole);
        cyc.assemblePoleLatDeg = latLon.latDeg;
        cyc.assemblePoleLonDeg = latLon.lonDeg;

        sched.cycles.push_back(cyc);
    }

    // Scale all cycle times so the last cycle ends at exactly totalMy.
    // This preserves the relative phase structure while fitting totalMy.
    if (cursor > 0.0f && cursor != totalMy) {
        const float scale = totalMy / cursor;
        for (WilsonCycle& cyc : sched.cycles) {
            if (cyc.assemblyStartMy > -100.0f) cyc.assemblyStartMy *= scale;
            cyc.peakMy *= scale;
            cyc.dispersalEndMy *= scale;
        }
    }

    return sched;
}

// ---------------------------------------------------------------------------
// WilsonSchedule::applyTo
//
// Per-terrane drift for this epoch.  In the V4 dispersal-only schedule all
// cycles have assemblyStartMy = -9999 (prehistory sentinel), so only the
// dispersal branch fires; the assembly branch is retained for reference.
//
// ASSEMBLY phase: each terrane moves toward the assembly pole.
//   driftAxis = cross(worldPos, assemblePole)  -- sweeps worldPos toward assemblePole.
//
// DISPERSAL phase: each terrane moves toward a unique equatorial target.
//   The target is 90 degrees from the current cycle's assembly pole, derived
//   from hash(t.id, cycleIdx).  cross(worldPos, target) sweeps worldPos toward
//   target.  Magnitude is ~1 even when worldPos is near the assembly pole
//   (unlike radial-away which falls to zero there), so separation is immediate.
//   Per-terrane uniqueness prevents N terranes all heading to the same point.
//
// QUIESCENT (between cycles): rate = 0.
// ---------------------------------------------------------------------------

void WilsonSchedule::applyTo(std::vector<Terrane>& terranes, float timeMy) const {
    // Find active cycle and phase.
    const WilsonCycle* activeCycle = nullptr;
    bool inAssembly                = false;
    int32_t cycleIdx               = 0;
    for (int32_t ci = 0; ci < static_cast<int32_t>(cycles.size()); ++ci) {
        const WilsonCycle& cyc = cycles[static_cast<std::size_t>(ci)];
        if (cyc.assemblyStartMy > -100.0f && timeMy >= cyc.assemblyStartMy && timeMy < cyc.peakMy) {
            activeCycle = &cyc;
            inAssembly  = true;
            cycleIdx    = ci;
            break;
        }
        if (timeMy >= cyc.peakMy && timeMy < cyc.dispersalEndMy) {
            activeCycle = &cyc;
            inAssembly  = false;
            cycleIdx    = ci;
            break;
        }
    }

    constexpr int32_t LON = SphereField::LON_CELLS;

    for (Terrane& t : terranes) {
        if (!t.alive || t.bodyCells.empty()) continue;

        if (activeCycle == nullptr) {
            t.driftRateDegPerMy = 0.0f;
            continue;
        }

        // Current terrane centroid in world frame: body-frame centroid
        // rotated by t.rot.
        double cx = 0.0, cy = 0.0, cz = 0.0;
        for (const int32_t cell : t.bodyCells) {
            const LatLon p = SphereField::cellCenter(cell % LON, cell / LON);
            const Vec3 v   = latLonToVec3(p);
            cx += static_cast<double>(v.x);
            cy += static_cast<double>(v.y);
            cz += static_cast<double>(v.z);
        }
        const double cl = std::sqrt(cx * cx + cy * cy + cz * cz);
        if (cl < 1e-9) continue;
        const Vec3 bodyPos  = {static_cast<float>(cx / cl), static_cast<float>(cy / cl),
                               static_cast<float>(cz / cl)};
        const Vec3 worldPos = applyRot(t.rot, bodyPos);

        const Vec3 assemblePole =
            latLonToVec3({activeCycle->assemblePoleLatDeg, activeCycle->assemblePoleLonDeg});

        Vec3 driftAxis;

        if (inAssembly) {
            // Move toward assembly pole.
            driftAxis = cross3(worldPos, assemblePole);
        } else {
            // DISPERSAL: each terrane gets a unique target 90° from the assembly
            // pole, derived from hash(t.id, cycleIdx).  The target is 90° from
            // the assembly pole so the drift axis is strong even for terranes that
            // are still near the assembly pole right after the assembly phase.
            // cross(worldPos, target) → rotates worldPos toward target.
            uint32_t tRng = (static_cast<uint32_t>(t.id) * 2654435761u) ^
                            (static_cast<uint32_t>(cycleIdx) * 0x9E3779B9u) ^ 0xC0DEC0DEu;
            float nx      = xorshift01(tRng) * 2.0f - 1.0f;
            float ny      = xorshift01(tRng) * 2.0f - 1.0f;
            float nz      = xorshift01(tRng) * 2.0f - 1.0f;
            const float d = dot3(assemblePole, {nx, ny, nz});
            const Vec3 np = normalize3(
                {nx - d * assemblePole.x, ny - d * assemblePole.y, nz - d * assemblePole.z});
            const Vec3 target = normalize3(cross3(assemblePole, np));
            driftAxis         = cross3(worldPos, target);
        }

        const float axisLen = len3(driftAxis);
        if (axisLen < 1e-6f) {
            // Degenerate: terrane is already at its target (or at the
            // assembly pole during assembly).  No-op this epoch.
            t.driftRateDegPerMy = 0.0f;
            continue;
        }

        driftAxis           = normalize3(driftAxis);
        const LatLon dAxis  = vec3ToLatLon(driftAxis);
        t.driftPoleLatDeg   = dAxis.latDeg;
        t.driftPoleLonDeg   = dAxis.lonDeg;
        t.driftRateDegPerMy = rateDegPerMy;
    }
}

} // namespace aoc::map::gen
