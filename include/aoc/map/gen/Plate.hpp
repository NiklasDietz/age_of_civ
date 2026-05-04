#pragma once

/**
 * @file Plate.hpp
 * @brief Tectonic-plate description shared by extracted MapGenerator passes.
 *
 * Pulled out of src/map/MapGenerator.cpp on 2026-05-03 so post-sim and
 * elevation passes can live in their own files. The struct's data layout is
 * MapGenerator-internal -- new fields can still be added without affecting
 * downstream consumers as long as they only read existing accessors.
 *
 * SutureSeam tracks plate-merger events that the post-sim ophiolite pass
 * uses to mark suture rock type.
 */

#include <cstdint>
#include <utility>
#include <vector>

namespace aoc::map::gen {

struct Plate {
    float cx;
    float cy;
    bool  isLand;
    float vx;
    float vy;
    float aspect;
    float rot;
    float baseRot;
    float baseAspect;
    float seedX;
    float seedY;
    float landFraction;
    int32_t ageEpochs = 0;
    float weight = 1.0f;
    std::vector<std::pair<float, float>> extraSeeds;
    bool isPolar = false;
    std::vector<std::pair<float, float>> hotspotTrail;
    std::vector<float> orogenyLocal;
    float crustArea        = 1.0f;
    float crustAreaInitial = 1.0f;
    int32_t mergesAbsorbed = 0;
    int8_t  slabTornThisEpoch = 0;
    float crustAge         = 0.0f;
    // 2026-05-03: Euler-pole rotation. Each plate spins around a fixed
    // axis (eulerPoleX, eulerPoleY) at angularRate rad/epoch. Real plates
    // rotate as well as translate -- pure translation produces unrealistically
    // straight hotspot trails and parallel margins. The hybrid model keeps
    // linear (vx,vy) drift and adds a small rotational component on top.
    float eulerPoleX  = 0.0f;
    float eulerPoleY  = 0.0f;
    float angularRate = 0.0f;
};

struct SutureSeam {
    float x;
    float y;
    float r;
};

} // namespace aoc::map::gen
