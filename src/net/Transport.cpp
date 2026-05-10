/**
 * @file Transport.cpp
 * @brief LocalTransport implementation (in-process, zero overhead).
 *
 * Threading: single-owner thread. See Transport.hpp for the full
 * contract. The two methods below assert the calling thread matches
 * the owner in debug builds via `assertOwner()`.
 */

#include "aoc/net/Transport.hpp"
#include "aoc/net/GameStateSnapshot.hpp"

#include <algorithm>

namespace aoc::net {

void LocalTransport::sendSnapshot(PlayerId player, GameStateSnapshot snapshot) {
    this->assertOwner();
    this->m_pendingSnapshots.push_back({player, std::move(snapshot)});
}

std::optional<GameStateSnapshot> LocalTransport::receiveSnapshot(PlayerId player) {
    this->assertOwner();
    for (std::size_t i = 0; i < this->m_pendingSnapshots.size(); ++i) {
        if (this->m_pendingSnapshots[i].first == player) {
            GameStateSnapshot result = std::move(this->m_pendingSnapshots[i].second);
            this->m_pendingSnapshots.erase(
                this->m_pendingSnapshots.begin() + static_cast<std::ptrdiff_t>(i));
            return result;
        }
    }
    return std::nullopt;
}

} // namespace aoc::net
