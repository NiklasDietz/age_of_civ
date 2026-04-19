#pragma once

/**
 * @file DecisionLog.hpp
 * @brief Compact binary log of AI decisions + per-turn summaries.
 *
 * Purpose: let users reconstruct *why* each AI leader chose what it chose,
 * without paying the cost of text logging for every decision. At ~20 bytes
 * per production choice vs ~200 bytes for an equivalent JSON line, a
 * 500-turn 6-player game produces ~200 KB binary instead of ~2 MB text.
 *
 * The log is opt-in: a non-null DecisionLog pointer on TurnContext enables
 * capture, a null pointer disables it. Call sites should check
 * `active()` before building the (sometimes expensive) alternates array.
 *
 * Not thread-safe by design. One DecisionLog per simulation. GA training
 * runs that spawn many parallel sims should either leave it null or create
 * a dedicated per-thread file.
 *
 * File layout:
 *   [FileHeader]
 *   [Record 0][Record 1]...
 *
 * Every record starts with a common prefix:
 *   u8  kind              // DecisionKind
 *   u16 turn
 *   u8  player
 *   u16 payloadBytes
 *   [payload: payloadBytes bytes]
 *
 * Payloads are packed little-endian. The reader uses `kind` to select
 * the payload decoder. Unknown kinds are skipped via payloadBytes.
 */

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aoc::core {

constexpr uint32_t DECISION_LOG_MAGIC = 0x4C434F41u; // "AOCL"
constexpr uint16_t DECISION_LOG_VERSION = 1u;

enum class DecisionKind : uint8_t {
    None        = 0,
    Production  = 1,
    Research    = 2,
    TurnSummary = 3,
    Event       = 4, ///< Reserved for future hook (WorldEvents choice).
    Spy         = 5, ///< Reserved for future hook.
    Government  = 6, ///< Reserved for future hook.
    Investment  = 7, ///< Reserved for future hook.
    GreatPerson = 8, ///< Reserved for future hook.
};

enum class ProductionItemKind : uint8_t {
    Building = 0,
    Unit     = 1,
    Wonder   = 2,
    District = 3,
    Project  = 4,
    Unknown  = 255,
};

/// File header written once at DecisionLog::open() time.
struct FileHeader {
    uint32_t magic      = DECISION_LOG_MAGIC;
    uint16_t version    = DECISION_LOG_VERSION;
    uint8_t  numPlayers = 0;
    uint8_t  mapType    = 0;
    uint32_t numTurns   = 0;
    uint64_t seed       = 0;
    // 16 bytes total. Extend version bump if fields added.
};

struct ProductionAlt {
    uint32_t itemId = 0;
    float    score  = 0.0f;
    uint8_t  kind   = static_cast<uint8_t>(ProductionItemKind::Unknown);
};

struct ResearchAlt {
    uint16_t techId = 0;
    float    score  = 0.0f;
};

/// Per-turn per-player aggregate snapshot.
struct TurnSummary {
    uint8_t  era             = 0;
    uint16_t cityCount       = 0;
    uint16_t unitCount       = 0;
    int64_t  treasury        = 0;
    float    science         = 0.0f;
    float    culture         = 0.0f;
    float    faith           = 0.0f;
    uint16_t techsResearched = 0;
    uint16_t grievanceCount  = 0;
    uint8_t  warCount        = 0;
    uint8_t  victoryTypeLead = 0; ///< Current front-running victory type (0 if none).
};

class DecisionLog {
public:
    DecisionLog() = default;
    DecisionLog(const DecisionLog&) = delete;
    DecisionLog& operator=(const DecisionLog&) = delete;
    ~DecisionLog();

    /// Open `path` for writing and emit the header. Returns false on I/O error.
    bool open(const std::string& path, const FileHeader& header);

    /// Flush + close the underlying stream.
    void close();

    /// True once open() has succeeded. Call sites should gate expensive
    /// alternates-array construction on this.
    [[nodiscard]] bool active() const { return this->m_file != nullptr; }

    void logProduction(uint16_t turn, uint8_t player, uint16_t cityIdx,
                       ProductionItemKind chosenKind, uint32_t chosenId,
                       float chosenScore,
                       std::span<const ProductionAlt> alternates);

    void logResearch(uint16_t turn, uint8_t player,
                     uint16_t chosenTechId, float chosenScore,
                     std::span<const ResearchAlt> alternates);

    void logTurnSummary(uint16_t turn, uint8_t player, const TurnSummary& summary);

private:
    std::FILE* m_file = nullptr;

    void writeRaw(const void* data, std::size_t bytes);
    void writeHeader(DecisionKind kind, uint16_t turn, uint8_t player,
                     uint16_t payloadBytes);
};

// ============================================================================
// Reader API (used by the aoc_trace_dump converter and any analysis code)
// ============================================================================

struct ProductionRecord {
    uint16_t turn;
    uint8_t  player;
    uint16_t cityIdx;
    ProductionItemKind chosenKind;
    uint32_t chosenId;
    float    chosenScore;
    std::vector<ProductionAlt> alternates;
};

struct ResearchRecord {
    uint16_t turn;
    uint8_t  player;
    uint16_t chosenTechId;
    float    chosenScore;
    std::vector<ResearchAlt> alternates;
};

struct TurnSummaryRecord {
    uint16_t turn;
    uint8_t  player;
    TurnSummary summary;
};

/// Visitor-style callback set for the decision-log reader.
struct DecisionLogVisitor {
    void (*onProduction) (void* ctx, const ProductionRecord&)  = nullptr;
    void (*onResearch)   (void* ctx, const ResearchRecord&)    = nullptr;
    void (*onTurnSummary)(void* ctx, const TurnSummaryRecord&) = nullptr;
    void* ctx = nullptr;
};

/// Thread-local active logger. Set by the turn processor at the start of
/// each turn so AI call sites can reach it without threading a pointer
/// through every signature. Null = logging off on this thread.
void setCurrentDecisionLog(DecisionLog* log);
[[nodiscard]] DecisionLog* currentDecisionLog();

/// RAII helper: installs `log` on this thread for the scope's lifetime,
/// restores the previous pointer on destruction. Safe to nest.
class ScopedDecisionLog {
public:
    explicit ScopedDecisionLog(DecisionLog* log) {
        this->m_prev = currentDecisionLog();
        setCurrentDecisionLog(log);
    }
    ~ScopedDecisionLog() { setCurrentDecisionLog(this->m_prev); }
    ScopedDecisionLog(const ScopedDecisionLog&) = delete;
    ScopedDecisionLog& operator=(const ScopedDecisionLog&) = delete;
private:
    DecisionLog* m_prev = nullptr;
};

/// Read every record from `path`, dispatch through the visitor. Returns false
/// on I/O error, bad magic, or version mismatch. `outHeader` is populated on
/// success.
[[nodiscard]] bool readDecisionLog(const std::string& path, FileHeader& outHeader,
                                    const DecisionLogVisitor& visitor);

/// Human-readable name for a DecisionKind (for CSV/JSON export).
[[nodiscard]] std::string_view decisionKindName(DecisionKind kind);
[[nodiscard]] std::string_view productionItemKindName(ProductionItemKind k);

} // namespace aoc::core
