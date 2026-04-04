#pragma once

/**
 * @file SystemScheduler.hpp
 * @brief Ordered execution of game systems with dependency tracking.
 *
 * Systems are registered with a name, a set of dependencies (names of systems
 * that must run first), and a callable. The scheduler topologically sorts
 * them once, then executes in that order each tick.
 */

#include "aoc/core/ErrorCodes.hpp"

#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace aoc::ecs {

class World;

class SystemScheduler {
public:
    using SystemFunc = std::function<void(World&)>;

    /**
     * @brief Register a system with dependencies.
     *
     * @param name       Unique system name.
     * @param dependencies  Names of systems that must execute before this one.
     * @param func       The system function to call.
     */
    void registerSystem(std::string name,
                        std::vector<std::string> dependencies,
                        SystemFunc func) {
        assert(this->m_systemMap.find(name) == this->m_systemMap.end()
               && "System already registered");

        uint32_t index = static_cast<uint32_t>(this->m_systems.size());
        this->m_systemMap[name] = index;
        this->m_systems.push_back(SystemEntry{
            std::move(name),
            std::move(dependencies),
            std::move(func)
        });
        this->m_sorted = false;
    }

    /**
     * @brief Execute all registered systems in dependency order.
     *
     * Topological sort is computed once and cached until a new system
     * is registered.
     *
     * @param world The ECS world passed to each system.
     * @return Ok, or SystemDependencyCycle if the graph has a cycle.
     */
    [[nodiscard]] ErrorCode executeTick(World& world) {
        if (!this->m_sorted) {
            ErrorCode result = this->buildExecutionOrder();
            if (result != ErrorCode::Ok) {
                return result;
            }
            this->m_sorted = true;
        }

        for (uint32_t index : this->m_executionOrder) {
            this->m_systems[index].func(world);
        }
        return ErrorCode::Ok;
    }

    /**
     * @brief Number of registered systems.
     */
    [[nodiscard]] uint32_t systemCount() const {
        return static_cast<uint32_t>(this->m_systems.size());
    }

private:
    struct SystemEntry {
        std::string name;
        std::vector<std::string> dependencies;
        SystemFunc func;
    };

    /**
     * @brief Kahn's algorithm for topological sort.
     * @return Ok on success, SystemDependencyCycle if a cycle is detected.
     */
    [[nodiscard]] ErrorCode buildExecutionOrder() {
        const uint32_t count = static_cast<uint32_t>(this->m_systems.size());

        // Build adjacency list and in-degree counts
        std::vector<std::vector<uint32_t>> adjacency(count);
        std::vector<uint32_t> inDegree(count, 0);

        for (uint32_t i = 0; i < count; ++i) {
            for (const std::string& dep : this->m_systems[i].dependencies) {
                auto it = this->m_systemMap.find(dep);
                if (it == this->m_systemMap.end()) {
                    continue;  // Unknown dependency -- skip gracefully
                }
                uint32_t depIndex = it->second;
                adjacency[depIndex].push_back(i);  // dep -> this system
                ++inDegree[i];
            }
        }

        // Collect nodes with zero in-degree
        std::vector<uint32_t> queue;
        queue.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (inDegree[i] == 0) {
                queue.push_back(i);
            }
        }

        this->m_executionOrder.clear();
        this->m_executionOrder.reserve(count);

        std::size_t front = 0;
        while (front < queue.size()) {
            uint32_t current = queue[front++];
            this->m_executionOrder.push_back(current);

            for (uint32_t neighbor : adjacency[current]) {
                --inDegree[neighbor];
                if (inDegree[neighbor] == 0) {
                    queue.push_back(neighbor);
                }
            }
        }

        if (this->m_executionOrder.size() != count) {
            return ErrorCode::SystemDependencyCycle;
        }
        return ErrorCode::Ok;
    }

    std::vector<SystemEntry> m_systems;
    std::unordered_map<std::string, uint32_t> m_systemMap;  ///< name -> index
    std::vector<uint32_t> m_executionOrder;                 ///< Cached topo sort
    bool m_sorted = false;
};

} // namespace aoc::ecs
