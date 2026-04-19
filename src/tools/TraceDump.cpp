/**
 * @file TraceDump.cpp
 * @brief Convert binary DecisionLog (.aocl) traces into text CSV.
 *
 * Three CSVs are emitted, mirroring the three record kinds:
 *   <base>_production.csv
 *   <base>_research.csv
 *   <base>_summary.csv
 *
 * Each row carries the full record; alternates are flattened into fixed
 * columns (alt1_id, alt1_score, alt2_id, alt2_score, alt3_id, alt3_score).
 * Unused alternate slots write empty cells so Pandas/Excel keep typing.
 *
 * Usage:
 *   aoc_trace_dump <trace.aocl> [out_base_path]
 *
 * If out_base_path is omitted the input stem is reused.
 */

#include "aoc/core/DecisionLog.hpp"
#include "aoc/simulation/tech/TechTree.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

namespace {

struct Ctx {
    std::ofstream* prod = nullptr;
    std::ofstream* res  = nullptr;
    std::ofstream* sum  = nullptr;
};

[[nodiscard]] std::string_view techNameSafe(uint16_t id) {
    const aoc::TechId tid{id};
    if (!tid.isValid()) { return "?"; }
    return aoc::sim::techDef(tid).name;
}

void writeProduction(void* raw, const aoc::core::ProductionRecord& r) {
    auto* ctx = static_cast<Ctx*>(raw);
    std::ofstream& o = *ctx->prod;
    o << r.turn << ',' << static_cast<int>(r.player) << ',' << r.cityIdx
      << ',' << aoc::core::productionItemKindName(r.chosenKind)
      << ',' << r.chosenId
      << ',' << r.chosenScore;
    for (std::size_t i = 0; i < 3; ++i) {
        if (i < r.alternates.size()) {
            const aoc::core::ProductionAlt& a = r.alternates[i];
            o << ',' << a.itemId << ',' << a.score
              << ',' << aoc::core::productionItemKindName(
                         static_cast<aoc::core::ProductionItemKind>(a.kind));
        } else {
            o << ",,,";
        }
    }
    o << '\n';
}

void writeResearch(void* raw, const aoc::core::ResearchRecord& r) {
    auto* ctx = static_cast<Ctx*>(raw);
    std::ofstream& o = *ctx->res;
    const std::string_view name = techNameSafe(r.chosenTechId);
    o << r.turn << ',' << static_cast<int>(r.player)
      << ',' << r.chosenTechId
      << ',' << std::string(name)
      << ',' << r.chosenScore;
    for (std::size_t i = 0; i < 3; ++i) {
        if (i < r.alternates.size()) {
            const aoc::core::ResearchAlt& a = r.alternates[i];
            o << ',' << a.techId << ',' << a.score;
        } else {
            o << ",,";
        }
    }
    o << '\n';
}

void writeSummary(void* raw, const aoc::core::TurnSummaryRecord& r) {
    auto* ctx = static_cast<Ctx*>(raw);
    std::ofstream& o = *ctx->sum;
    const aoc::core::TurnSummary& s = r.summary;
    o << r.turn << ',' << static_cast<int>(r.player)
      << ',' << static_cast<int>(s.era)
      << ',' << s.cityCount
      << ',' << s.unitCount
      << ',' << s.treasury
      << ',' << s.science
      << ',' << s.culture
      << ',' << s.faith
      << ',' << s.techsResearched
      << ',' << s.grievanceCount
      << ',' << static_cast<int>(s.warCount)
      << ',' << static_cast<int>(s.victoryTypeLead)
      << '\n';
}

} // anonymous namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "usage: aoc_trace_dump <trace.aocl> [out_base_path]\n"
            "  writes <base>_production.csv, <base>_research.csv, <base>_summary.csv\n");
        return 2;
    }

    const std::string inPath = argv[1];
    std::string base = (argc >= 3) ? argv[2] : inPath;
    if (argc < 3) {
        const std::size_t dot = base.rfind('.');
        if (dot != std::string::npos) { base.erase(dot); }
    }

    std::ofstream prodCsv(base + "_production.csv");
    std::ofstream resCsv(base + "_research.csv");
    std::ofstream sumCsv(base + "_summary.csv");
    if (!prodCsv || !resCsv || !sumCsv) {
        std::fprintf(stderr, "error: could not open output csv near %s\n", base.c_str());
        return 1;
    }

    prodCsv << "turn,player,cityIdx,chosenKind,chosenId,chosenScore,"
               "alt1_id,alt1_score,alt1_kind,"
               "alt2_id,alt2_score,alt2_kind,"
               "alt3_id,alt3_score,alt3_kind\n";
    resCsv  << "turn,player,chosenTechId,chosenTechName,chosenScore,"
               "alt1_id,alt1_score,alt2_id,alt2_score,alt3_id,alt3_score\n";
    sumCsv  << "turn,player,era,cities,units,treasury,science,culture,faith,"
               "techsResearched,grievances,wars,victoryLead\n";

    Ctx ctx{};
    ctx.prod = &prodCsv;
    ctx.res  = &resCsv;
    ctx.sum  = &sumCsv;

    aoc::core::DecisionLogVisitor v{};
    v.ctx = &ctx;
    v.onProduction  = &writeProduction;
    v.onResearch    = &writeResearch;
    v.onTurnSummary = &writeSummary;

    aoc::core::FileHeader hdr{};
    if (!aoc::core::readDecisionLog(inPath, hdr, v)) {
        std::fprintf(stderr, "error: failed to read %s (bad magic or IO error)\n",
                     inPath.c_str());
        return 1;
    }

    std::fprintf(stderr,
        "wrote %s_{production,research,summary}.csv\n  version=%u numPlayers=%u numTurns=%u\n",
        base.c_str(),
        static_cast<unsigned>(hdr.version),
        static_cast<unsigned>(hdr.numPlayers),
        static_cast<unsigned>(hdr.numTurns));
    return 0;
}
