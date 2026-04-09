#pragma once

/**
 * @file World.hpp
 * @brief ECS World -- entity lifecycle and component management.
 *
 * The World owns all component pools and manages entity creation/destruction.
 * Components are stored in sparse-set pools for O(1) access and cache-friendly
 * iteration. Entities are generational indices for safe reuse after destruction.
 */

#include "aoc/ecs/ComponentPool.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Types.hpp"

#include <cassert>
#include <concepts>
#include <cstdint>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace aoc::ecs {

class World {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept = default;
    World& operator=(World&&) noexcept = default;

    // ========================================================================
    // Entity lifecycle
    // ========================================================================

    /**
     * @brief Create a new entity.
     * @return A valid EntityId, or NULL_ENTITY if the limit is reached.
     */
    [[nodiscard]] EntityId createEntity();

    /**
     * @brief Destroy an entity and all its components.
     *
     * The entity's index is recycled with an incremented generation.
     * Any EntityId referencing the old generation becomes stale.
     */
    void destroyEntity(EntityId entity);

    /**
     * @brief Check whether an entity is alive (created and not destroyed).
     */
    [[nodiscard]] bool isAlive(EntityId entity) const;

    /**
     * @brief Number of currently alive entities.
     */
    [[nodiscard]] uint32_t entityCount() const { return this->m_aliveCount; }

    // ========================================================================
    // Component access
    // ========================================================================

    /**
     * @brief Add a component to an entity. The entity must be alive and must
     *        not already have a component of this type.
     * @return Reference to the newly added component.
     */
    template<std::movable T>
    T& addComponent(EntityId entity, T component) {
        assert(this->isAlive(entity));
        return this->getOrCreatePool<T>().add(entity, std::move(component));
    }

    /**
     * @brief Remove a component from an entity.
     * @return true if the component was present and removed.
     */
    template<std::movable T>
    bool removeComponent(EntityId entity) {
        ComponentPool<T>* pool = this->getPool<T>();
        if (pool == nullptr) {
            return false;
        }
        return pool->remove(entity);
    }

    /**
     * @brief Get a component. Asserts that the entity has it.
     */
    template<std::movable T>
    [[nodiscard]] T& getComponent(EntityId entity) {
        ComponentPool<T>* pool = this->getPool<T>();
        assert(pool != nullptr && "Component type not registered");
        return pool->get(entity);
    }

    template<std::movable T>
    [[nodiscard]] const T& getComponent(EntityId entity) const {
        const ComponentPool<T>* pool = this->getPool<T>();
        assert(pool != nullptr && "Component type not registered");
        return pool->get(entity);
    }

    /**
     * @brief Try to get a component. Returns nullptr if not present.
     */
    template<std::movable T>
    [[nodiscard]] T* tryGetComponent(EntityId entity) {
        ComponentPool<T>* pool = this->getPool<T>();
        if (pool == nullptr) {
            return nullptr;
        }
        return pool->tryGet(entity);
    }

    template<std::movable T>
    [[nodiscard]] const T* tryGetComponent(EntityId entity) const {
        const ComponentPool<T>* pool = this->getPool<T>();
        if (pool == nullptr) {
            return nullptr;
        }
        return pool->tryGet(entity);
    }

    /**
     * @brief Check if an entity has a specific component type.
     */
    template<std::movable T>
    [[nodiscard]] bool hasComponent(EntityId entity) const {
        const ComponentPool<T>* pool = this->getPool<T>();
        return pool != nullptr && pool->contains(entity);
    }

    // ========================================================================
    // Pool access (for systems that iterate all components of a type)
    // ========================================================================

    /**
     * @brief Get the component pool for a type. Returns nullptr if no
     *        component of this type has ever been added.
     */
    template<std::movable T>
    [[nodiscard]] ComponentPool<T>* getPool() {
        std::type_index typeId = std::type_index(typeid(T));
        std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>>::iterator it = this->m_pools.find(typeId);
        if (it == this->m_pools.end()) {
            return nullptr;
        }
        return static_cast<ComponentPool<T>*>(it->second.get());
    }

    template<std::movable T>
    [[nodiscard]] const ComponentPool<T>* getPool() const {
        std::type_index typeId = std::type_index(typeid(T));
        std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>>::const_iterator it = this->m_pools.find(typeId);
        if (it == this->m_pools.end()) {
            return nullptr;
        }
        return static_cast<const ComponentPool<T>*>(it->second.get());
    }

    // ========================================================================
    // Multi-component iteration
    // ========================================================================

    /**
     * @brief Iterate all entities that have ALL of the specified component types.
     *
     * Iterates the smallest pool and checks membership in the others.
     * The callback receives (EntityId, T1&, T2&, ...).
     *
     * Usage:
     * @code
     *   world.forEach<Position, Velocity>([](aoc::EntityId id, Position& pos, Velocity& vel) {
     *       pos.x += vel.dx;
     *   });
     * @endcode
     */
    template<std::movable... Components, typename Func>
    void forEach(Func&& func) {
        // Ensure all pools exist; if any is missing, there are zero matches
        std::tuple<ComponentPool<Components>*...> pools = std::make_tuple(this->getPool<Components>()...);
        if (((std::get<ComponentPool<Components>*>(pools) == nullptr) || ...)) {
            return;
        }

        // Find the smallest pool to iterate (reduces membership checks)
        uint32_t minSize = std::numeric_limits<uint32_t>::max();
        IComponentPool* smallestPool = nullptr;

        // auto required: lambda type is unnameable
        auto findSmallest = [&]<typename T>(ComponentPool<T>* pool) {
            if (pool->size() < minSize) {
                minSize = pool->size();
                smallestPool = pool;
            }
        };
        (findSmallest(std::get<ComponentPool<Components>*>(pools)), ...);

        // Iterate the smallest pool's entities, check membership in all others
        // We need to iterate by index because entities() gives us the EntityId array
        // auto required: lambda type is unnameable
        auto iteratePool = [&]<typename First>(ComponentPool<First>* pool) {
            if (static_cast<IComponentPool*>(pool) != smallestPool) {
                return;
            }
            for (uint32_t i = 0; i < pool->size(); ++i) {
                EntityId entity = pool->entities()[i];
                // Check entity is alive (generation match)
                if (!this->isAlive(entity)) {
                    continue;
                }
                // Check membership in all other pools
                bool hasAll = ((std::get<ComponentPool<Components>*>(pools)->contains(entity)) && ...);
                if (hasAll) {
                    func(entity, std::get<ComponentPool<Components>*>(pools)->get(entity)...);
                }
            }
        };
        (iteratePool(std::get<ComponentPool<Components>*>(pools)), ...);
    }

private:
    template<std::movable T>
    ComponentPool<T>& getOrCreatePool() {
        std::type_index typeId = std::type_index(typeid(T));
        std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>>::iterator it = this->m_pools.find(typeId);
        if (it != this->m_pools.end()) {
            return *static_cast<ComponentPool<T>*>(it->second.get());
        }
        std::unique_ptr<ComponentPool<T>> pool = std::make_unique<ComponentPool<T>>();
        ComponentPool<T>& ref = *pool;
        this->m_pools.emplace(typeId, std::move(pool));
        return ref;
    }

    /// Entity metadata: stores the current generation per index slot.
    struct EntitySlot {
        uint16_t generation = 0;  ///< Current generation (12-bit in EntityId)
        bool     alive      = false;
    };

    std::vector<EntitySlot> m_entitySlots;
    std::vector<uint32_t>   m_freeIndices;  ///< Recycled entity indices
    uint32_t                m_nextIndex = 0;
    uint32_t                m_aliveCount = 0;

    /// Component pools keyed by type_index.
    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> m_pools;
};

} // namespace aoc::ecs
