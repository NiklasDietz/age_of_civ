/**
 * @file Pathfinding.cpp
 * @brief A* and flood-fill pathfinding on the hex grid.
 */

#include "aoc/map/Pathfinding.hpp"
#include "aoc/map/HexGrid.hpp"

#include <algorithm>
#include <queue>
#include <unordered_map>

namespace aoc::map {

// ============================================================================
// A* pathfinding
// ============================================================================

std::optional<PathResult> findPath(const HexGrid& grid,
                                    hex::AxialCoord start,
                                    hex::AxialCoord goal,
                                    int32_t maxCost) {
    if (!grid.isValid(start) || !grid.isValid(goal)) {
        return std::nullopt;
    }

    if (start == goal) {
        return PathResult{{start}, 0};
    }

    // Check goal is passable
    if (grid.movementCost(grid.toIndex(goal)) == 0) {
        return std::nullopt;
    }

    struct Node {
        hex::AxialCoord coord;
        int32_t priority;  ///< f = g + h
    };

    auto cmp = [](const Node& a, const Node& b) { return a.priority > b.priority; };
    std::priority_queue<Node, std::vector<Node>, decltype(cmp)> openSet(cmp);

    std::unordered_map<hex::AxialCoord, int32_t> costSoFar;
    std::unordered_map<hex::AxialCoord, hex::AxialCoord> cameFrom;

    openSet.push({start, 0});
    costSoFar[start] = 0;

    while (!openSet.empty()) {
        hex::AxialCoord current = openSet.top().coord;
        openSet.pop();

        if (current == goal) {
            // Reconstruct path
            PathResult result;
            result.totalCost = costSoFar[goal];
            hex::AxialCoord step = goal;
            while (!(step == start)) {
                result.path.push_back(step);
                step = cameFrom[step];
            }
            result.path.push_back(start);
            std::reverse(result.path.begin(), result.path.end());
            return result;
        }

        int32_t currentCost = costSoFar[current];

        for (const hex::AxialCoord& neighbor : hex::neighbors(current)) {
            if (!grid.isValid(neighbor)) {
                continue;
            }

            int32_t moveCost = grid.movementCost(grid.toIndex(neighbor));
            if (moveCost == 0) {
                continue;  // Impassable
            }

            int32_t newCost = currentCost + moveCost;
            if (maxCost > 0 && newCost > maxCost) {
                continue;  // Over budget
            }

            auto it = costSoFar.find(neighbor);
            if (it == costSoFar.end() || newCost < it->second) {
                costSoFar[neighbor] = newCost;
                int32_t heuristic = hex::distance(neighbor, goal);
                openSet.push({neighbor, newCost + heuristic});
                cameFrom[neighbor] = current;
            }
        }
    }

    return std::nullopt;  // No path found
}

// ============================================================================
// Flood-fill reachability
// ============================================================================

std::vector<ReachableTile> findReachable(const HexGrid& grid,
                                          hex::AxialCoord start,
                                          int32_t maxCost) {
    if (!grid.isValid(start)) {
        return {};
    }

    struct FrontierNode {
        hex::AxialCoord coord;
        int32_t costSpent;
    };

    std::vector<ReachableTile> result;
    std::unordered_map<hex::AxialCoord, int32_t> visited;
    std::queue<FrontierNode> frontier;

    frontier.push({start, 0});
    visited[start] = 0;
    result.push_back({start, maxCost});

    while (!frontier.empty()) {
        FrontierNode current = frontier.front();
        frontier.pop();

        for (const hex::AxialCoord& neighbor : hex::neighbors(current.coord)) {
            if (!grid.isValid(neighbor)) {
                continue;
            }

            int32_t moveCost = grid.movementCost(grid.toIndex(neighbor));
            if (moveCost == 0) {
                continue;
            }

            int32_t newCost = current.costSpent + moveCost;
            if (newCost > maxCost) {
                continue;
            }

            auto it = visited.find(neighbor);
            if (it == visited.end() || newCost < it->second) {
                visited[neighbor] = newCost;
                frontier.push({neighbor, newCost});
                result.push_back({neighbor, maxCost - newCost});
            }
        }
    }

    return result;
}

} // namespace aoc::map
