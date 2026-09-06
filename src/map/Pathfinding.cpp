/**
 * @file Pathfinding.cpp
 * @brief A* and flood-fill pathfinding on the hex grid.
 */

#include "aoc/map/Pathfinding.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/ZoneOfControl.hpp"

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
                                    int32_t maxCost,
                                    const aoc::game::GameState* gameState,
                                    PlayerId movingPlayer,
                                    bool isNavalPath,
                                    bool avoidCanals) {
    if (!grid.isValid(start) || !grid.isValid(goal)) {
        return std::nullopt;
    }

    if (start == goal) {
        return PathResult{{start}, 0};
    }

    // Check goal is passable (use appropriate cost function)
    int32_t goalCost = 0;
    if (isNavalPath) {
        goalCost = avoidCanals
            ? grid.navalMovementCostNoCanals(grid.toIndex(goal))
            : grid.navalMovementCost(grid.toIndex(goal));
    } else {
        goalCost = grid.movementCost(grid.toIndex(goal));
    }
    if (goalCost == 0) {
        return std::nullopt;
    }

    struct Node {
        hex::AxialCoord coord;
        int32_t priority;  ///< f = g + h
        int32_t cost;      ///< g recorded when this entry was queued
    };

    // auto required: lambda type is unnameable
    auto cmp = [](const Node& a, const Node& b) { return a.priority > b.priority; };
    std::priority_queue<Node, std::vector<Node>, decltype(cmp)> openSet(cmp);

    // DEBT(perf, WP-11): costSoFar/cameFrom rehash per insert. Flat arrays
    // indexed by grid.toIndex() would cut hashing, but each findPath call
    // would then pay an O(tileCount) sentinel-fill up front -- a net loss for
    // the common case (short unit moves on a large map) and a behaviour risk
    // (must replicate the map's "absent vs present" distinction exactly).
    // Deferred: needs a reusable thread-local scratch keyed by grid size, or
    // a generation-counter trick, to be unconditionally faster AND identical.
    std::unordered_map<hex::AxialCoord, int32_t> costSoFar;
    std::unordered_map<hex::AxialCoord, hex::AxialCoord> cameFrom;

    // Canonical form of a tile: the grid wraps columns on a Cylindrical map,
    // so (145, 10) and (5, 10) are the SAME tile with different coordinates.
    // The search keys costSoFar and cameFrom by coordinate and compares
    // `current == goal` by coordinate, so without canonicalising, a search
    // that runs off the eastern edge keeps generating fresh coordinates for
    // tiles it has already visited. isValid only checks the row on a cylinder,
    // so every one of them is "valid" and the frontier never closes: the open
    // set grew past twelve million nodes and the process died of bad_alloc.
    // Every map the game ships is cylindrical, so this was reachable in
    // ordinary play whenever a goal was unreachable or lay around the wrap.
    const auto canonical = [&grid](hex::AxialCoord c) {
        return hex::offsetToAxial(grid.toOffset(grid.toIndex(c)));
    };
    const hex::AxialCoord canonStart = canonical(start);
    const hex::AxialCoord canonGoal  = canonical(goal);

    openSet.push({canonStart, 0, 0});
    costSoFar[canonStart] = 0;

    while (!openSet.empty()) {
        const Node top = openSet.top();
        hex::AxialCoord current = top.coord;
        openSet.pop();

        // Stale-entry skip: a cheaper path to `current` was found after this
        // entry was queued, so a better entry has already been (or will be)
        // expanded. Re-expanding this outdated copy cannot improve any
        // neighbour (every relaxation gates on `newCost < best`), so the
        // returned path is unchanged -- we only avoid redundant work.
        // `find`, not `operator[]`: the subscript would insert a 0 cost for a
        // node that has none, which both grows the map and makes the comparison
        // meaningless for that node.
        const std::unordered_map<hex::AxialCoord, int32_t>::const_iterator best =
            costSoFar.find(current);
        if (best != costSoFar.end() && top.cost > best->second) {
            continue;
        }

        if (current == canonGoal) {
            // Reconstruct path
            PathResult result;
            result.totalCost = costSoFar[canonGoal];
            hex::AxialCoord step = canonGoal;
            // Walk the predecessor chain back to the start. This used to read
            // `cameFrom[step]`, and `operator[]` INSERTS a default-constructed
            // (0,0) for a missing key. A broken chain therefore walked to (0,0),
            // whose freshly inserted predecessor is (0,0) itself, and the loop
            // pushed that tile forever: an 8-byte vector doubling silently until
            // the process died of bad_alloc. Measured at 24 GB on a 200-turn
            // game. `find` cannot insert, and a self-referencing or missing
            // predecessor now means "no path" rather than an endless one.
            while (!(step == canonStart)) {
                result.path.push_back(step);
                const std::unordered_map<hex::AxialCoord, hex::AxialCoord>::const_iterator prev =
                    cameFrom.find(step);
                if (prev == cameFrom.end() || prev->second == step) {
                    return std::nullopt;
                }
                step = prev->second;
            }
            result.path.push_back(canonStart);
            std::reverse(result.path.begin(), result.path.end());
            return result;
        }

        int32_t currentCost = top.cost;

        for (const hex::AxialCoord& rawNeighbor : hex::neighbors(current)) {
            if (!grid.isValid(rawNeighbor)) {
                continue;
            }
            const hex::AxialCoord neighbor = canonical(rawNeighbor);

            int32_t moveCost = 0;
            if (isNavalPath) {
                moveCost = avoidCanals
                    ? grid.navalMovementCostNoCanals(grid.toIndex(neighbor))
                    : grid.navalMovementCost(grid.toIndex(neighbor));
            } else {
                moveCost = grid.movementCost(grid.toIndex(current), grid.toIndex(neighbor));
            }
            if (moveCost == 0) {
                continue;  // Impassable
            }

            // ZoC-aware costing: tiles in enemy zone of control cost +3
            if (gameState != nullptr && movingPlayer != INVALID_PLAYER) {
                if (aoc::game::isInEnemyZoneOfControl(*gameState, neighbor, movingPlayer)) {
                    moveCost += 3;
                }
            }

            int32_t newCost = currentCost + moveCost;
            if (maxCost > 0 && newCost > maxCost) {
                continue;  // Over budget
            }

            std::unordered_map<hex::AxialCoord, int32_t>::iterator it = costSoFar.find(neighbor);
            if (it == costSoFar.end() || newCost < it->second) {
                costSoFar[neighbor] = newCost;
                int32_t heuristic = grid.distance(neighbor, goal);
                openSet.push({neighbor, newCost + heuristic, newCost});
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
    // Maps each reported coord to its slot in `result` so a later, cheaper
    // path updates that tile's remaining cost in place instead of appending
    // a duplicate entry with a stale (lower) remaining-cost value.
    std::unordered_map<hex::AxialCoord, std::size_t> resultIndex;
    std::queue<FrontierNode> frontier;

    frontier.push({start, 0});
    visited[start] = 0;
    resultIndex[start] = result.size();
    result.push_back({start, maxCost});

    while (!frontier.empty()) {
        FrontierNode current = frontier.front();
        frontier.pop();

        for (const hex::AxialCoord& neighbor : hex::neighbors(current.coord)) {
            if (!grid.isValid(neighbor)) {
                continue;
            }

            int32_t moveCost =
                grid.movementCost(grid.toIndex(current.coord), grid.toIndex(neighbor));
            if (moveCost == 0) {
                continue;
            }

            int32_t newCost = current.costSpent + moveCost;
            if (newCost > maxCost) {
                continue;
            }

            std::unordered_map<hex::AxialCoord, int32_t>::iterator it = visited.find(neighbor);
            if (it == visited.end()) {
                visited[neighbor] = newCost;
                frontier.push({neighbor, newCost});
                resultIndex[neighbor] = result.size();
                result.push_back({neighbor, maxCost - newCost});
            } else if (newCost < it->second) {
                it->second = newCost;
                frontier.push({neighbor, newCost});
                result[resultIndex[neighbor]].remainingCost = maxCost - newCost;
            }
        }
    }

    return result;
}

} // namespace aoc::map
