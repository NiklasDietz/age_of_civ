#pragma once

/**
 * @file PlayerIndex.hpp
 * @brief Per-player component index for O(1) player-filtered lookups.
 *
 * The codebase has a pervasive pattern:
 * @code
 *   for (uint32_t i = 0; i < pool->size(); ++i) {
 *       if (pool->data()[i].owner == player) { ... }
 *   }
 * @endcode
 *
 * This is O(n) per query. With hundreds of components, it adds up in hot paths
 * that run every turn for every player.
 *
 * PlayerIndex provides O(1) lookup: given a player ID, get a pointer to their
 * component (or nullptr). It works for any component type that has a `PlayerId owner`
 * member field.
 *
 * Usage:
 * @code
 *   // Build once per turn (or when components change)
 *   PlayerIndex<MonetaryStateComponent> monetaryIndex;
 *   monetaryIndex.rebuild(world);
 *
 *   // O(1) lookup
 *   MonetaryStateComponent* state = monetaryIndex.get(player);
 *   if (state != nullptr) { ... }
 * @endcode
 *
 * The index is a snapshot -- if components are added/removed, call rebuild().
 * For per-turn processing, rebuild once at the start of the turn.
 */

#include "aoc/ecs/World.hpp"
#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>

namespace aoc::ecs {

/// Maximum number of players the index supports.
inline constexpr int32_t MAX_INDEXED_PLAYERS = 16;

/**
 * @brief Fast per-player component lookup. Requires T to have a `PlayerId owner` member.
 *
 * @tparam T Component type with a `PlayerId owner` field.
 */
template<std::movable T>
class PlayerIndex {
public:
    PlayerIndex() {
        this->clear();
    }

    /// Rebuild the index from the current World state.
    void rebuild(World& world) {
        this->clear();
        ComponentPool<T>* pool = world.getPool<T>();
        if (pool == nullptr) {
            return;
        }
        for (uint32_t i = 0; i < pool->size(); ++i) {
            PlayerId owner = pool->data()[i].owner;
            if (owner < MAX_INDEXED_PLAYERS) {
                this->m_components[owner] = &pool->data()[i];
                this->m_entities[owner] = pool->entities()[i];
            }
        }
    }

    /// Rebuild from const World (returns const pointers).
    void rebuild(const World& world) {
        this->clear();
        const ComponentPool<T>* pool = world.getPool<T>();
        if (pool == nullptr) {
            return;
        }
        for (uint32_t i = 0; i < pool->size(); ++i) {
            PlayerId owner = pool->data()[i].owner;
            if (owner < MAX_INDEXED_PLAYERS) {
                this->m_components[owner] = const_cast<T*>(&pool->data()[i]);
                this->m_entities[owner] = pool->entities()[i];
            }
        }
    }

    /// Get a player's component, or nullptr if they don't have one.
    [[nodiscard]] T* get(PlayerId player) const {
        if (player >= MAX_INDEXED_PLAYERS) { return nullptr; }
        return this->m_components[player];
    }

    /// Get the entity that holds a player's component.
    [[nodiscard]] EntityId entity(PlayerId player) const {
        if (player >= MAX_INDEXED_PLAYERS) { return NULL_ENTITY; }
        return this->m_entities[player];
    }

    /// Check if a player has this component.
    [[nodiscard]] bool has(PlayerId player) const {
        if (player >= MAX_INDEXED_PLAYERS) { return false; }
        return this->m_components[player] != nullptr;
    }

    void clear() {
        for (int32_t i = 0; i < MAX_INDEXED_PLAYERS; ++i) {
            this->m_components[i] = nullptr;
            this->m_entities[i] = NULL_ENTITY;
        }
    }

private:
    std::array<T*, MAX_INDEXED_PLAYERS> m_components = {};
    std::array<EntityId, MAX_INDEXED_PLAYERS> m_entities = {};
};

} // namespace aoc::ecs
