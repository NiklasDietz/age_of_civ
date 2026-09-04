#pragma once

/**
 * @file GridLayerCompare.hpp
 * @brief Test helper: snapshot every HexGrid layer by name and compare a second
 *        grid against it, so a layer added to HexGrid later is covered without
 *        touching the tests that use this.
 */

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexGridLayers.hpp"

#include <any>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace aoc::test {

/// visitLayers() visitor: copies every layer by name.
struct LayerSnapshot {
    std::unordered_map<std::string, std::any> layers;

    template <class Container> void operator()(std::string_view name, const Container& c) {
        this->layers.emplace(std::string(name), c);
    }
};

/// visitLayers() visitor: counts layers that differ from a snapshot.
struct LayerCompare {
    const LayerSnapshot& reference;
    int32_t seen       = 0;
    int32_t mismatched = 0;
    std::string firstMismatch;

    template <class Container> void operator()(std::string_view name, const Container& c) {
        ++this->seen;
        const std::unordered_map<std::string, std::any>::const_iterator it =
            this->reference.layers.find(std::string(name));
        const Container* other = (it == this->reference.layers.end())
                                     ? nullptr
                                     : std::any_cast<Container>(&it->second);
        if (other == nullptr || !(*other == c)) {
            if (this->mismatched == 0) {
                this->firstMismatch = std::string(name);
            }
            ++this->mismatched;
        }
    }
};

} // namespace aoc::test
