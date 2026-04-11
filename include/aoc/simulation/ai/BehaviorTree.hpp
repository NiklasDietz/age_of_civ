#pragma once

/**
 * @file BehaviorTree.hpp
 * @brief Behavior tree framework for AI decision-making.
 *
 * A behavior tree (BT) is a directed tree where:
 *   - Internal nodes control execution flow (Sequence, Selector, etc.)
 *   - Leaf nodes perform actions or check conditions
 *   - Each tick, the tree is traversed from root
 *   - Nodes return Success, Failure, or Running
 *
 * Node types:
 *   Sequence:  Ticks children left-to-right. Fails on first failure. (AND)
 *   Selector:  Ticks children left-to-right. Succeeds on first success. (OR)
 *   Decorator: Wraps a single child. Modifies return value.
 *     - Inverter: flips Success/Failure
 *     - Repeater: repeats child N times
 *     - AlwaysSucceed: returns Success regardless
 *     - WeightedChance: runs child only if random check passes (for personality variance)
 *   Action:    Leaf node. Executes a game action (build unit, select research, etc.)
 *   Condition: Leaf node. Checks a game state condition (has military? needs settler?)
 *
 * Blackboard:
 *   Shared data store per-AI that nodes read/write. Contains:
 *   - Player ID, difficulty, leader personality
 *   - Cached counts (cities, military, population, treasury)
 *   - Threat assessment, target enemies
 *   - Current turn, game pace
 *
 * Trees are built programmatically from leader personality data, or can
 * be loaded from a definition file (future mod support).
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

class Market;
class DiplomacyManager;

namespace bt {

// ============================================================================
// Node return status
// ============================================================================

enum class Status : uint8_t {
    Success,
    Failure,
    Running,   ///< Action is in progress (multi-turn actions)
};

// ============================================================================
// Blackboard: shared data store for a single AI player
// ============================================================================

struct Blackboard {
    // Identity
    PlayerId player = INVALID_PLAYER;
    CivId    civId = 0;
    int32_t  difficulty = 1;
    const LeaderBehavior* behavior = nullptr;

    // World references (set before each tick)
    aoc::game::GameState* gameState = nullptr;
    aoc::map::HexGrid* grid = nullptr;
    const Market* market = nullptr;
    DiplomacyManager* diplomacy = nullptr;
    aoc::Random* rng = nullptr;

    // Cached state (refreshed at start of each tick)
    int32_t ownedCities = 0;
    int32_t totalPopulation = 0;
    int32_t militaryUnits = 0;
    int32_t builderUnits = 0;
    int32_t settlerUnits = 0;
    CurrencyAmount treasury = 0;
    int32_t techsResearched = 0;
    bool    isAtWar = false;
    bool    isThreatened = false;
    int32_t targetMaxCities = 4;
    int32_t desiredMilitary = 4;

    // Generic key-value store for custom data
    std::unordered_map<std::string, float> values;

    void set(const std::string& key, float val) { this->values[key] = val; }
    [[nodiscard]] float get(const std::string& key, float defaultVal = 0.0f) const {
        std::unordered_map<std::string, float>::const_iterator it = this->values.find(key);
        return (it != this->values.end()) ? it->second : defaultVal;
    }
};

// ============================================================================
// Base node
// ============================================================================

class Node {
public:
    virtual ~Node() = default;
    virtual Status tick(Blackboard& bb) = 0;
    [[nodiscard]] virtual std::string_view name() const = 0;
};

using NodePtr = std::unique_ptr<Node>;

// ============================================================================
// Composite nodes
// ============================================================================

/// Sequence: ticks children in order. Fails on first failure.
class Sequence final : public Node {
public:
    explicit Sequence(std::string nodeName, std::vector<NodePtr> children)
        : m_name(std::move(nodeName)), m_children(std::move(children)) {}

    Status tick(Blackboard& bb) override {
        for (std::unique_ptr<Node>& child : this->m_children) {
            Status s = child->tick(bb);
            if (s != Status::Success) { return s; }
        }
        return Status::Success;
    }

    [[nodiscard]] std::string_view name() const override { return this->m_name; }

private:
    std::string m_name;
    std::vector<NodePtr> m_children;
};

/// Selector: ticks children in order. Succeeds on first success.
class Selector final : public Node {
public:
    explicit Selector(std::string nodeName, std::vector<NodePtr> children)
        : m_name(std::move(nodeName)), m_children(std::move(children)) {}

    Status tick(Blackboard& bb) override {
        for (std::unique_ptr<Node>& child : this->m_children) {
            Status s = child->tick(bb);
            if (s != Status::Failure) { return s; }
        }
        return Status::Failure;
    }

    [[nodiscard]] std::string_view name() const override { return this->m_name; }

private:
    std::string m_name;
    std::vector<NodePtr> m_children;
};

// ============================================================================
// Decorator nodes
// ============================================================================

/// Inverter: flips Success to Failure and vice versa.
class Inverter final : public Node {
public:
    explicit Inverter(NodePtr child) : m_child(std::move(child)) {}

    Status tick(Blackboard& bb) override {
        Status s = this->m_child->tick(bb);
        if (s == Status::Success) { return Status::Failure; }
        if (s == Status::Failure) { return Status::Success; }
        return s;
    }

    [[nodiscard]] std::string_view name() const override { return "Inverter"; }

private:
    NodePtr m_child;
};

/// WeightedChance: runs child only if personality weight exceeds threshold.
/// Used to make leaders skip actions they don't care about.
class WeightedChance final : public Node {
public:
    WeightedChance(float weight, NodePtr child)
        : m_weight(weight), m_child(std::move(child)) {}

    Status tick(Blackboard& bb) override {
        // If weight is below 0.3, almost never do this. Above 1.5, always do it.
        // Between: use a deterministic check.
        if (this->m_weight < 0.3f) { return Status::Failure; }
        if (this->m_weight >= 1.0f) { return this->m_child->tick(bb); }
        // Moderate weight: succeed with probability proportional to weight
        float roll = bb.get("_tick_counter", 0.0f);
        float threshold = this->m_weight;
        if (std::fmod(roll, 1.0f) < threshold) {
            return this->m_child->tick(bb);
        }
        return Status::Failure;
    }

    [[nodiscard]] std::string_view name() const override { return "WeightedChance"; }

private:
    float m_weight;
    NodePtr m_child;
};

// ============================================================================
// Leaf nodes: Conditions
// ============================================================================

/// Condition that checks a blackboard value against a threshold.
class CheckCondition final : public Node {
public:
    using CheckFn = std::function<bool(const Blackboard&)>;

    CheckCondition(std::string nodeName, CheckFn fn)
        : m_name(std::move(nodeName)), m_fn(std::move(fn)) {}

    Status tick(Blackboard& bb) override {
        return this->m_fn(bb) ? Status::Success : Status::Failure;
    }

    [[nodiscard]] std::string_view name() const override { return this->m_name; }

private:
    std::string m_name;
    CheckFn m_fn;
};

// ============================================================================
// Leaf nodes: Actions
// ============================================================================

/// Action that executes a game action via a callback.
class ExecuteAction final : public Node {
public:
    using ActionFn = std::function<Status(Blackboard&)>;

    ExecuteAction(std::string nodeName, ActionFn fn)
        : m_name(std::move(nodeName)), m_fn(std::move(fn)) {}

    Status tick(Blackboard& bb) override {
        return this->m_fn(bb);
    }

    [[nodiscard]] std::string_view name() const override { return this->m_name; }

private:
    std::string m_name;
    ActionFn m_fn;
};

// ============================================================================
// Helper: build common condition/action nodes
// ============================================================================

/// Condition: needs more cities?
[[nodiscard]] inline NodePtr needsSettler() {
    return std::make_unique<CheckCondition>("NeedsSettler",
        [](const Blackboard& bb) {
            return bb.ownedCities < bb.targetMaxCities && bb.settlerUnits == 0;
        });
}

/// Condition: needs more military?
[[nodiscard]] inline NodePtr needsMilitary() {
    return std::make_unique<CheckCondition>("NeedsMilitary",
        [](const Blackboard& bb) {
            return bb.militaryUnits < bb.desiredMilitary;
        });
}

/// Condition: needs builders?
[[nodiscard]] inline NodePtr needsBuilder() {
    return std::make_unique<CheckCondition>("NeedsBuilder",
        [](const Blackboard& bb) {
            return bb.builderUnits == 0;
        });
}

/// Condition: is threatened?
[[nodiscard]] inline NodePtr isThreatened() {
    return std::make_unique<CheckCondition>("IsThreatened",
        [](const Blackboard& bb) { return bb.isThreatened; });
}

/// Condition: is at war?
[[nodiscard]] inline NodePtr isAtWar() {
    return std::make_unique<CheckCondition>("IsAtWar",
        [](const Blackboard& bb) { return bb.isAtWar; });
}

/// Condition: has enough population to settle?
[[nodiscard]] inline NodePtr hasPopToSettle(int32_t threshold) {
    return std::make_unique<CheckCondition>("HasPopToSettle",
        [threshold](const Blackboard& bb) {
            return bb.totalPopulation >= threshold * bb.ownedCities;
        });
}

// ============================================================================
// Tree builder: create a complete AI behavior tree from leader personality
// ============================================================================

/**
 * @brief Build a complete AI behavior tree for a leader.
 *
 * The tree structure:
 *   Root (Selector):
 *     1. Emergency response (if at war / threatened)
 *        Sequence: [IsThreatened] -> [BuildMilitary]
 *     2. Expansion (if below city target)
 *        Sequence: [NeedsSettler, HasPop] -> [BuildSettler]
 *     3. Infrastructure
 *        Sequence: [NeedsBuilder] -> [BuildBuilder]
 *     4. Development (weighted by personality)
 *        Selector:
 *          WeightedChance(militaryAggression): [BuildMilitary]
 *          WeightedChance(cultureFocus): [BuildWonder/Culture]
 *          WeightedChance(economicFocus): [BuildEconomicBuilding]
 *          WeightedChance(religiousZeal): [BuildReligious]
 *          WeightedChance(scienceFocus): [BuildScienceBuilding]
 *     5. Fallback: [BuildMilitary]
 *
 * @param personality  Leader's personality definition.
 * @return Root node of the behavior tree.
 */
[[nodiscard]] NodePtr buildLeaderBehaviorTree(const LeaderPersonalityDef& personality);

/**
 * @brief Refresh the blackboard with current game state.
 *
 * Called at the start of each turn before ticking the tree.
 */
void refreshBlackboard(Blackboard& bb);

} // namespace bt
} // namespace aoc::sim
