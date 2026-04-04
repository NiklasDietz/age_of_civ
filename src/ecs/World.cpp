/**
 * @file World.cpp
 * @brief ECS World implementation -- entity lifecycle.
 */

#include "aoc/ecs/World.hpp"

#include <cassert>

namespace aoc::ecs {

World::World() {
    this->m_entitySlots.reserve(4096);
    this->m_freeIndices.reserve(256);
}

World::~World() = default;

EntityId World::createEntity() {
    uint32_t index = 0;
    uint16_t generation = 0;

    if (!this->m_freeIndices.empty()) {
        // Reuse a recycled index
        index = this->m_freeIndices.back();
        this->m_freeIndices.pop_back();
        generation = this->m_entitySlots[index].generation;
    } else {
        // Allocate a new index
        if (this->m_nextIndex >= EntityId::MAX_ENTITIES) {
            return NULL_ENTITY;
        }
        index = this->m_nextIndex++;
        if (index >= this->m_entitySlots.size()) {
            this->m_entitySlots.resize(static_cast<std::size_t>(index) + 1);
        }
        generation = 0;
        this->m_entitySlots[index].generation = generation;
    }

    this->m_entitySlots[index].alive = true;
    ++this->m_aliveCount;

    EntityId entity{};
    entity.index = index;
    entity.generation = generation;
    return entity;
}

void World::destroyEntity(EntityId entity) {
    assert(this->isAlive(entity));

    // Remove all components for this entity
    for (auto& [typeId, pool] : this->m_pools) {
        pool->remove(entity);
    }

    EntitySlot& slot = this->m_entitySlots[entity.index];
    slot.alive = false;

    // Increment generation for reuse (wraps within 12 bits)
    slot.generation = static_cast<uint16_t>((slot.generation + 1) & 0xFFF);

    this->m_freeIndices.push_back(entity.index);
    --this->m_aliveCount;
}

bool World::isAlive(EntityId entity) const {
    if (!entity.isValid()) {
        return false;
    }
    if (entity.index >= this->m_entitySlots.size()) {
        return false;
    }
    const EntitySlot& slot = this->m_entitySlots[entity.index];
    return slot.alive && slot.generation == entity.generation;
}

} // namespace aoc::ecs
