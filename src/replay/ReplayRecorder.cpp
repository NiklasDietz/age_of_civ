/**
 * @file ReplayRecorder.cpp
 * @brief Replay recording and serialization implementation.
 */

#include "aoc/replay/ReplayRecorder.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/core/Log.hpp"

#include <fstream>

namespace aoc::replay {

void ReplayRecorder::recordFrame(const aoc::game::GameState& gameState, TurnNumber turn) {
    ReplayFrame frame{};
    frame.turn = turn;

    const uint16_t techCnt = aoc::sim::techCount();

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        ReplayFrame::PlayerSnapshot snap{};
        snap.owner = playerPtr->id();

        snap.military = playerPtr->militaryUnitCount();

        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            ++snap.territory;
            snap.population += cityPtr->population();
        }

        for (uint16_t t = 0; t < techCnt; ++t) {
            if (playerPtr->tech().hasResearched(TechId{t})) {
                ++snap.techs;
            }
        }

        // Score = military * 5 + territory * 20 + population * 2 + techs * 10
        snap.score = snap.military * 5 + snap.territory * 20 +
                     snap.population * 2 + snap.techs * 10;

        if (snap.military > 0 || snap.territory > 0) {
            frame.players.push_back(snap);
        }
    }

    this->m_frames.push_back(std::move(frame));
}

void ReplayRecorder::clear() {
    this->m_frames.clear();
}

const std::vector<ReplayFrame>& ReplayRecorder::frames() const {
    return this->m_frames;
}

void ReplayRecorder::save(const std::string& filepath) const {
    std::ofstream file(filepath, std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to save replay to %s", filepath.c_str());
        return;
    }

    // Simple binary format: frameCount, then for each frame: turn, playerCount, snapshots
    const uint32_t frameCount = static_cast<uint32_t>(this->m_frames.size());
    file.write(reinterpret_cast<const char*>(&frameCount), sizeof(frameCount));

    for (const ReplayFrame& frame : this->m_frames) {
        file.write(reinterpret_cast<const char*>(&frame.turn), sizeof(frame.turn));
        const uint32_t playerCount = static_cast<uint32_t>(frame.players.size());
        file.write(reinterpret_cast<const char*>(&playerCount), sizeof(playerCount));

        for (const ReplayFrame::PlayerSnapshot& snap : frame.players) {
            file.write(reinterpret_cast<const char*>(&snap.owner), sizeof(snap.owner));
            file.write(reinterpret_cast<const char*>(&snap.score), sizeof(snap.score));
            file.write(reinterpret_cast<const char*>(&snap.territory), sizeof(snap.territory));
            file.write(reinterpret_cast<const char*>(&snap.military), sizeof(snap.military));
            file.write(reinterpret_cast<const char*>(&snap.population), sizeof(snap.population));
            file.write(reinterpret_cast<const char*>(&snap.techs), sizeof(snap.techs));
        }
    }

    LOG_INFO("Replay saved to %s (%u frames)", filepath.c_str(), frameCount);
}

bool ReplayRecorder::load(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to load replay from %s", filepath.c_str());
        return false;
    }

    this->m_frames.clear();

    uint32_t frameCount = 0;
    file.read(reinterpret_cast<char*>(&frameCount), sizeof(frameCount));

    if (frameCount > 10000) {
        LOG_ERROR("Replay file %s has too many frames (%u)", filepath.c_str(), frameCount);
        return false;
    }

    this->m_frames.resize(frameCount);
    for (uint32_t f = 0; f < frameCount; ++f) {
        ReplayFrame& frame = this->m_frames[f];
        file.read(reinterpret_cast<char*>(&frame.turn), sizeof(frame.turn));

        uint32_t playerCount = 0;
        file.read(reinterpret_cast<char*>(&playerCount), sizeof(playerCount));

        frame.players.resize(playerCount);
        for (uint32_t p = 0; p < playerCount; ++p) {
            ReplayFrame::PlayerSnapshot& snap = frame.players[p];
            file.read(reinterpret_cast<char*>(&snap.owner), sizeof(snap.owner));
            file.read(reinterpret_cast<char*>(&snap.score), sizeof(snap.score));
            file.read(reinterpret_cast<char*>(&snap.territory), sizeof(snap.territory));
            file.read(reinterpret_cast<char*>(&snap.military), sizeof(snap.military));
            file.read(reinterpret_cast<char*>(&snap.population), sizeof(snap.population));
            file.read(reinterpret_cast<char*>(&snap.techs), sizeof(snap.techs));
        }
    }

    LOG_INFO("Replay loaded from %s (%u frames)", filepath.c_str(), frameCount);
    return true;
}

} // namespace aoc::replay
