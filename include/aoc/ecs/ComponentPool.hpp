#pragma once

/**
 * @file ComponentPool.hpp
 * @brief Sparse-set based component storage with O(1) add/remove/lookup
 *        and cache-friendly dense iteration.
 */

#include "aoc/core/Types.hpp"

#include <cassert>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

namespace aoc::ecs {

/// Type-erased base so World can store heterogeneous pools.
class IComponentPool {
public:
    virtual ~IComponentPool() = default;

    /// Remove the component for this entity (if present). Returns true if removed.
    virtual bool remove(EntityId entity) = 0;

    /// Check whether this entity has a component in this pool.
    [[nodiscard]] virtual bool contains(EntityId entity) const = 0;

    /// Number of active components.
    [[nodiscard]] virtual uint32_t size() const = 0;
};

/**
 * @brief Dense sparse-set storage for a single component type.
 *
 * Sparse array: indexed by EntityId::index, stores the dense index (or INVALID).
 * Dense array:  contiguous component data, iterable with no gaps.
 * Entity array: parallel to dense, maps dense index -> EntityId.
 *
 * All operations are O(1) amortized. Iteration is cache-friendly over the
 * dense array. Removal uses swap-and-pop to keep the dense array packed.
 *
 * @tparam T Component type. Must be movable.
 */
template<std::movable T>
class ComponentPool final : public IComponentPool {
public:
    static constexpr uint32_t INVALID = std::numeric_limits<uint32_t>::max();

    ComponentPool() {
        this->m_sparse.resize(1024, INVALID);  // Initial capacity, grows on demand
    }

    /**
     * @brief Add a component for an entity. Asserts the entity doesn't already have one.
     * @return Reference to the newly inserted component.
     */
    T& add(EntityId entity, T component) {
        assert(entity.isValid());
        this->ensureSparseCapacity(entity.index);
        assert(this->m_sparse[entity.index] == INVALID && "Entity already has this component");

        const uint32_t denseIndex = static_cast<uint32_t>(this->m_dense.size());
        this->m_sparse[entity.index] = denseIndex;
        this->m_dense.push_back(std::move(component));
        this->m_entities.push_back(entity);

        return this->m_dense.back();
    }

    bool remove(EntityId entity) override {
        if (!this->contains(entity)) {
            return false;
        }

        const uint32_t denseIndex = this->m_sparse[entity.index];
        const uint32_t lastIndex  = static_cast<uint32_t>(this->m_dense.size()) - 1;

        if (denseIndex != lastIndex) {
            // Swap with last element
            this->m_dense[denseIndex]    = std::move(this->m_dense[lastIndex]);
            this->m_entities[denseIndex] = this->m_entities[lastIndex];

            // Update the moved entity's sparse entry
            this->m_sparse[this->m_entities[denseIndex].index] = denseIndex;
        }

        this->m_sparse[entity.index] = INVALID;
        this->m_dense.pop_back();
        this->m_entities.pop_back();

        return true;
    }

    [[nodiscard]] bool contains(EntityId entity) const override {
        if (!entity.isValid() || entity.index >= this->m_sparse.size()) {
            return false;
        }
        return this->m_sparse[entity.index] != INVALID;
    }

    /**
     * @brief Get the component for an entity. Asserts the entity has one.
     */
    [[nodiscard]] T& get(EntityId entity) {
        assert(this->contains(entity));
        return this->m_dense[this->m_sparse[entity.index]];
    }

    [[nodiscard]] const T& get(EntityId entity) const {
        assert(this->contains(entity));
        return this->m_dense[this->m_sparse[entity.index]];
    }

    /**
     * @brief Try to get the component. Returns nullptr if not present.
     */
    [[nodiscard]] T* tryGet(EntityId entity) {
        if (!this->contains(entity)) {
            return nullptr;
        }
        return &this->m_dense[this->m_sparse[entity.index]];
    }

    [[nodiscard]] const T* tryGet(EntityId entity) const {
        if (!this->contains(entity)) {
            return nullptr;
        }
        return &this->m_dense[this->m_sparse[entity.index]];
    }

    [[nodiscard]] uint32_t size() const override {
        return static_cast<uint32_t>(this->m_dense.size());
    }

    [[nodiscard]] bool empty() const {
        return this->m_dense.empty();
    }

    // ========================================================================
    // Iteration -- direct access to dense arrays for cache-friendly loops
    // ========================================================================

    [[nodiscard]] T* data() { return this->m_dense.data(); }
    [[nodiscard]] const T* data() const { return this->m_dense.data(); }

    [[nodiscard]] const EntityId* entities() const { return this->m_entities.data(); }

    /// Standard begin/end for range-based for loops over components.
    [[nodiscard]] typename std::vector<T>::iterator begin() { return this->m_dense.begin(); }
    [[nodiscard]] typename std::vector<T>::iterator end()   { return this->m_dense.end(); }
    [[nodiscard]] typename std::vector<T>::const_iterator begin() const { return this->m_dense.begin(); }
    [[nodiscard]] typename std::vector<T>::const_iterator end()   const { return this->m_dense.end(); }

private:
    void ensureSparseCapacity(uint32_t index) {
        if (index >= this->m_sparse.size()) {
            this->m_sparse.resize(static_cast<std::size_t>(index) + 1, INVALID);
        }
    }

    std::vector<uint32_t> m_sparse;    ///< EntityId::index -> dense index
    std::vector<T>        m_dense;     ///< Packed component data
    std::vector<EntityId> m_entities;  ///< Parallel to m_dense: dense index -> EntityId
};

} // namespace aoc::ecs
