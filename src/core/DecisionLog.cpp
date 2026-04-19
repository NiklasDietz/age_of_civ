/**
 * @file DecisionLog.cpp
 * @brief Binary decision log writer + reader implementation.
 */

#include "aoc/core/DecisionLog.hpp"
#include "aoc/core/Log.hpp"

#include <cstring>
#include <vector>

namespace aoc::core {

namespace {

void writeU8(std::FILE* f, uint8_t v)   { std::fwrite(&v, 1, 1, f); }
void writeU16(std::FILE* f, uint16_t v) { std::fwrite(&v, sizeof(v), 1, f); }
void writeU32(std::FILE* f, uint32_t v) { std::fwrite(&v, sizeof(v), 1, f); }
void writeU64(std::FILE* f, uint64_t v) { std::fwrite(&v, sizeof(v), 1, f); }
void writeI64(std::FILE* f, int64_t v)  { std::fwrite(&v, sizeof(v), 1, f); }
void writeF32(std::FILE* f, float v)    { std::fwrite(&v, sizeof(v), 1, f); }

bool readU8 (std::FILE* f, uint8_t&  v) { return std::fread(&v, 1, 1, f) == 1; }
bool readU16(std::FILE* f, uint16_t& v) { return std::fread(&v, sizeof(v), 1, f) == 1; }
bool readU32(std::FILE* f, uint32_t& v) { return std::fread(&v, sizeof(v), 1, f) == 1; }
bool readU64(std::FILE* f, uint64_t& v) { return std::fread(&v, sizeof(v), 1, f) == 1; }
bool readI64(std::FILE* f, int64_t&  v) { return std::fread(&v, sizeof(v), 1, f) == 1; }
bool readF32(std::FILE* f, float&    v) { return std::fread(&v, sizeof(v), 1, f) == 1; }

} // namespace

DecisionLog::~DecisionLog() {
    this->close();
}

bool DecisionLog::open(const std::string& path, const FileHeader& header) {
    if (this->m_file != nullptr) { this->close(); }
    this->m_file = std::fopen(path.c_str(), "wb");
    if (this->m_file == nullptr) {
        LOG_ERROR("[DecisionLog.cpp:open] failed to open '%s' for write", path.c_str());
        return false;
    }
    writeU32(this->m_file, header.magic);
    writeU16(this->m_file, header.version);
    writeU8 (this->m_file, header.numPlayers);
    writeU8 (this->m_file, header.mapType);
    writeU32(this->m_file, header.numTurns);
    writeU64(this->m_file, header.seed);
    return true;
}

void DecisionLog::close() {
    if (this->m_file != nullptr) {
        std::fflush(this->m_file);
        std::fclose(this->m_file);
        this->m_file = nullptr;
    }
}

void DecisionLog::writeRaw(const void* data, std::size_t bytes) {
    std::fwrite(data, 1, bytes, this->m_file);
}

void DecisionLog::writeHeader(DecisionKind kind, uint16_t turn, uint8_t player,
                               uint16_t payloadBytes) {
    writeU8 (this->m_file, static_cast<uint8_t>(kind));
    writeU16(this->m_file, turn);
    writeU8 (this->m_file, player);
    writeU16(this->m_file, payloadBytes);
}

// Production record payload:
//   u16 cityIdx
//   u8  chosenKind
//   u32 chosenId
//   f32 chosenScore
//   u8  altCount
//   [altCount × (u32 itemId, f32 score, u8 kind)]
void DecisionLog::logProduction(uint16_t turn, uint8_t player, uint16_t cityIdx,
                                 ProductionItemKind chosenKind, uint32_t chosenId,
                                 float chosenScore,
                                 std::span<const ProductionAlt> alternates) {
    if (this->m_file == nullptr) { return; }
    const uint8_t altCount = static_cast<uint8_t>(
        alternates.size() > 255 ? 255 : alternates.size());
    const uint16_t payloadBytes = static_cast<uint16_t>(
        2 + 1 + 4 + 4 + 1 + altCount * (4 + 4 + 1));
    this->writeHeader(DecisionKind::Production, turn, player, payloadBytes);
    writeU16(this->m_file, cityIdx);
    writeU8 (this->m_file, static_cast<uint8_t>(chosenKind));
    writeU32(this->m_file, chosenId);
    writeF32(this->m_file, chosenScore);
    writeU8 (this->m_file, altCount);
    for (uint8_t i = 0; i < altCount; ++i) {
        writeU32(this->m_file, alternates[i].itemId);
        writeF32(this->m_file, alternates[i].score);
        writeU8 (this->m_file, alternates[i].kind);
    }
}

// Research record payload:
//   u16 chosenTechId
//   f32 chosenScore
//   u8  altCount
//   [altCount × (u16, f32)]
void DecisionLog::logResearch(uint16_t turn, uint8_t player,
                               uint16_t chosenTechId, float chosenScore,
                               std::span<const ResearchAlt> alternates) {
    if (this->m_file == nullptr) { return; }
    const uint8_t altCount = static_cast<uint8_t>(
        alternates.size() > 255 ? 255 : alternates.size());
    const uint16_t payloadBytes = static_cast<uint16_t>(
        2 + 4 + 1 + altCount * (2 + 4));
    this->writeHeader(DecisionKind::Research, turn, player, payloadBytes);
    writeU16(this->m_file, chosenTechId);
    writeF32(this->m_file, chosenScore);
    writeU8 (this->m_file, altCount);
    for (uint8_t i = 0; i < altCount; ++i) {
        writeU16(this->m_file, alternates[i].techId);
        writeF32(this->m_file, alternates[i].score);
    }
}

// TurnSummary payload (fixed 25 bytes):
//   u8 era
//   u16 cityCount
//   u16 unitCount
//   i64 treasury
//   f32 science
//   f32 culture
//   f32 faith
//   u16 techsResearched
//   u16 grievanceCount
//   u8  warCount
//   u8  victoryTypeLead
void DecisionLog::logTurnSummary(uint16_t turn, uint8_t player,
                                  const TurnSummary& s) {
    if (this->m_file == nullptr) { return; }
    constexpr uint16_t payloadBytes = 1 + 2 + 2 + 8 + 4 + 4 + 4 + 2 + 2 + 1 + 1;
    this->writeHeader(DecisionKind::TurnSummary, turn, player, payloadBytes);
    writeU8 (this->m_file, s.era);
    writeU16(this->m_file, s.cityCount);
    writeU16(this->m_file, s.unitCount);
    writeI64(this->m_file, s.treasury);
    writeF32(this->m_file, s.science);
    writeF32(this->m_file, s.culture);
    writeF32(this->m_file, s.faith);
    writeU16(this->m_file, s.techsResearched);
    writeU16(this->m_file, s.grievanceCount);
    writeU8 (this->m_file, s.warCount);
    writeU8 (this->m_file, s.victoryTypeLead);
}

// ============================================================================
// Reader
// ============================================================================

bool readDecisionLog(const std::string& path, FileHeader& outHeader,
                      const DecisionLogVisitor& visitor) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        LOG_ERROR("[DecisionLog.cpp:readDecisionLog] cannot open '%s'", path.c_str());
        return false;
    }

    if (!readU32(f, outHeader.magic) || outHeader.magic != DECISION_LOG_MAGIC) {
        LOG_ERROR("[DecisionLog.cpp:readDecisionLog] bad magic in '%s'", path.c_str());
        std::fclose(f);
        return false;
    }
    if (!readU16(f, outHeader.version) || outHeader.version != DECISION_LOG_VERSION) {
        LOG_ERROR("[DecisionLog.cpp:readDecisionLog] unsupported version %u in '%s'",
                  static_cast<unsigned>(outHeader.version), path.c_str());
        std::fclose(f);
        return false;
    }
    if (!readU8(f, outHeader.numPlayers) || !readU8(f, outHeader.mapType)
        || !readU32(f, outHeader.numTurns) || !readU64(f, outHeader.seed)) {
        std::fclose(f);
        return false;
    }

    std::vector<uint8_t> skipBuf;

    while (true) {
        uint8_t  kindRaw = 0;
        uint16_t turn    = 0;
        uint8_t  player  = 0;
        uint16_t payloadBytes = 0;
        if (!readU8(f, kindRaw)) { break; } // EOF
        if (!readU16(f, turn) || !readU8(f, player) || !readU16(f, payloadBytes)) {
            std::fclose(f);
            return false;
        }

        const DecisionKind kind = static_cast<DecisionKind>(kindRaw);
        switch (kind) {
            case DecisionKind::Production: {
                ProductionRecord rec{};
                rec.turn = turn;
                rec.player = player;
                uint8_t chosenKindRaw = 0;
                uint8_t altCount = 0;
                if (!readU16(f, rec.cityIdx) || !readU8(f, chosenKindRaw)
                    || !readU32(f, rec.chosenId) || !readF32(f, rec.chosenScore)
                    || !readU8(f, altCount)) {
                    std::fclose(f); return false;
                }
                rec.chosenKind = static_cast<ProductionItemKind>(chosenKindRaw);
                rec.alternates.resize(altCount);
                for (uint8_t i = 0; i < altCount; ++i) {
                    uint8_t k = 0;
                    if (!readU32(f, rec.alternates[i].itemId)
                        || !readF32(f, rec.alternates[i].score)
                        || !readU8(f, k)) {
                        std::fclose(f); return false;
                    }
                    rec.alternates[i].kind = k;
                }
                if (visitor.onProduction != nullptr) {
                    visitor.onProduction(visitor.ctx, rec);
                }
                break;
            }
            case DecisionKind::Research: {
                ResearchRecord rec{};
                rec.turn = turn;
                rec.player = player;
                uint8_t altCount = 0;
                if (!readU16(f, rec.chosenTechId) || !readF32(f, rec.chosenScore)
                    || !readU8(f, altCount)) {
                    std::fclose(f); return false;
                }
                rec.alternates.resize(altCount);
                for (uint8_t i = 0; i < altCount; ++i) {
                    if (!readU16(f, rec.alternates[i].techId)
                        || !readF32(f, rec.alternates[i].score)) {
                        std::fclose(f); return false;
                    }
                }
                if (visitor.onResearch != nullptr) {
                    visitor.onResearch(visitor.ctx, rec);
                }
                break;
            }
            case DecisionKind::TurnSummary: {
                TurnSummaryRecord rec{};
                rec.turn = turn;
                rec.player = player;
                TurnSummary& s = rec.summary;
                if (!readU8(f, s.era) || !readU16(f, s.cityCount)
                    || !readU16(f, s.unitCount) || !readI64(f, s.treasury)
                    || !readF32(f, s.science) || !readF32(f, s.culture)
                    || !readF32(f, s.faith) || !readU16(f, s.techsResearched)
                    || !readU16(f, s.grievanceCount) || !readU8(f, s.warCount)
                    || !readU8(f, s.victoryTypeLead)) {
                    std::fclose(f); return false;
                }
                if (visitor.onTurnSummary != nullptr) {
                    visitor.onTurnSummary(visitor.ctx, rec);
                }
                break;
            }
            default: {
                // Unknown kind: skip payloadBytes.
                if (skipBuf.size() < payloadBytes) { skipBuf.resize(payloadBytes); }
                if (payloadBytes > 0
                    && std::fread(skipBuf.data(), 1, payloadBytes, f) != payloadBytes) {
                    std::fclose(f); return false;
                }
                break;
            }
        }
    }

    std::fclose(f);
    return true;
}

// ============================================================================
// Thread-local active logger
// ============================================================================

namespace {
thread_local DecisionLog* tlsCurrent = nullptr;
} // namespace

void setCurrentDecisionLog(DecisionLog* log) { tlsCurrent = log; }
DecisionLog* currentDecisionLog() { return tlsCurrent; }

std::string_view decisionKindName(DecisionKind kind) {
    switch (kind) {
        case DecisionKind::None:        return "none";
        case DecisionKind::Production:  return "production";
        case DecisionKind::Research:    return "research";
        case DecisionKind::TurnSummary: return "turn_summary";
        case DecisionKind::Event:       return "event";
        case DecisionKind::Spy:         return "spy";
        case DecisionKind::Government:  return "government";
        case DecisionKind::Investment:  return "investment";
        case DecisionKind::GreatPerson: return "great_person";
    }
    return "?";
}

std::string_view productionItemKindName(ProductionItemKind k) {
    switch (k) {
        case ProductionItemKind::Building: return "building";
        case ProductionItemKind::Unit:     return "unit";
        case ProductionItemKind::Wonder:   return "wonder";
        case ProductionItemKind::District: return "district";
        case ProductionItemKind::Project:  return "project";
        case ProductionItemKind::Unknown:  return "?";
    }
    return "?";
}

} // namespace aoc::core
