#pragma once

/**
 * @file BalanceConfig.hpp
 * @brief Centralized balance constants for tuning gameplay.
 *
 * All magic numbers that affect game balance are collected here for
 * easy playtesting and iteration. Grouped by system.
 */

#include <cstdint>

namespace aoc::sim::balance {

// ============================================================================
// City Growth
// ============================================================================
inline constexpr int32_t BASE_FOOD_PER_CITIZEN = 2;
inline constexpr float   GROWTH_PENALTY_AT_HOUSING_CAP = 0.50f;
inline constexpr float   GROWTH_PENALTY_OVER_HOUSING = 0.25f;

// ============================================================================
// Combat
// ============================================================================
inline constexpr float   RIVER_DEFENSE_BONUS = 1.25f;
inline constexpr float   ELEVATION_BONUS_PER_LEVEL = 0.10f;
inline constexpr float   FORTIFICATION_BONUS = 1.25f;
inline constexpr float   FLANKING_BONUS_PER_UNIT = 0.10f;
inline constexpr int32_t CORPS_STRENGTH_BONUS = 10;
inline constexpr int32_t ARMY_STRENGTH_BONUS = 17;

// ============================================================================
// Economy
// ============================================================================
inline constexpr int32_t COIN_TIER_THRESHOLD = 5;
inline constexpr float   BARTER_TRADE_EFFICIENCY = 0.50f;
inline constexpr float   COPPER_TRADE_EFFICIENCY = 0.65f;
inline constexpr float   SILVER_TRADE_EFFICIENCY = 0.80f;
inline constexpr float   GOLD_TRADE_EFFICIENCY = 0.95f;
inline constexpr float   DEBASEMENT_MAX_RATIO = 0.50f;

// ============================================================================
// Monetary
// ============================================================================
inline constexpr float   BANK_RUN_DEBT_RATIO = 1.5f;
inline constexpr float   BANK_RUN_INFLATION_THRESHOLD = 0.08f;
inline constexpr float   HYPERINFLATION_THRESHOLD = 0.25f;
inline constexpr int32_t HYPERINFLATION_CONSEC_TURNS = 3;
inline constexpr int32_t DEFAULT_COOLDOWN_TURNS = 10;
inline constexpr float   FIAT_BASE_TRUST = 0.30f;
inline constexpr float   RESERVE_ACQUIRE_THRESHOLD = 0.80f;
inline constexpr float   RESERVE_LOSE_THRESHOLD = 0.70f;

// ============================================================================
// Production
// ============================================================================
inline constexpr float   MAX_EXPERIENCE_BONUS = 0.40f;
inline constexpr float   POWER_BROWNOUT_FLOOR = 0.30f;
inline constexpr float   NUCLEAR_MELTDOWN_CHANCE = 0.002f;
inline constexpr int32_t ROBOT_MAINTENANCE_INTERVAL = 10;
inline constexpr int32_t ROBOT_ENERGY_DEMAND = 5;

// ============================================================================
// Communication / Empire Size
// ============================================================================
inline constexpr float   COMM_LOYALTY_PER_TURN = 2.0f;
inline constexpr float   COMM_CORRUPTION_PER_TURN = 0.01f;
inline constexpr float   COMM_PROD_PENALTY_PER_TURN = 0.03f;
inline constexpr float   COMM_SCIENCE_PENALTY_PER_TURN = 0.02f;
inline constexpr float   GARRISON_LOYALTY_REDUCTION = 0.50f;
inline constexpr int32_t REGIONAL_CAPITAL_RADIUS = 5;

// ============================================================================
// Government
// ============================================================================
inline constexpr int32_t ANARCHY_DURATION = 5;
inline constexpr int32_t ANARCHY_AMENITY_PENALTY = 3;
inline constexpr float   MAX_CORRUPTION_RATE = 0.30f;

// ============================================================================
// Trade
// ============================================================================
inline constexpr float   SEA_TRADE_MULTIPLIER = 10.0f;
inline constexpr float   RIVER_TRADE_MULTIPLIER = 5.0f;
inline constexpr float   SEA_TRANSPORT_COST_MULT = 0.30f;
inline constexpr float   RIVER_TRANSPORT_COST_MULT = 0.40f;

// ============================================================================
// Victory / CSI
// ============================================================================
inline constexpr int32_t ERA_EVALUATION_INTERVAL = 30;
inline constexpr float   INTEGRATION_THRESHOLD = 1.5f;
inline constexpr int32_t INTEGRATION_TURNS_REQUIRED = 10;
inline constexpr int32_t ECONOMIC_COLLAPSE_TURNS = 10;
inline constexpr int32_t REVOLUTION_LOYALTY_TURNS = 5;
inline constexpr float   REVOLUTION_LOYALTY_THRESHOLD = 30.0f;

// ============================================================================
// Labor / Migration
// ============================================================================
inline constexpr int32_t STRIKE_DURATION = 3;
inline constexpr int32_t MIN_INDUSTRIAL_FOR_STRIKE = 3;
inline constexpr float   MIGRATION_QOL_THRESHOLD = 3.0f;
inline constexpr int32_t MIGRATION_DISTANCE_MAX = 10;

// ============================================================================
// Disasters
// ============================================================================
inline constexpr float   DISASTER_BASE_FREQUENCY = 0.012f;
inline constexpr float   DISASTER_TEMP_SCALING = 0.5f;

} // namespace aoc::sim::balance
