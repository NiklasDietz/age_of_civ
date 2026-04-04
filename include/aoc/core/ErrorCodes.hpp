#pragma once

/**
 * @file ErrorCodes.hpp
 * @brief Project-wide error codes with human-readable descriptions.
 */

#include <cstdint>
#include <string_view>

namespace aoc {

/// Numeric error codes for all subsystems.
/// Each subsystem occupies a range of 100 codes.
enum class ErrorCode : uint16_t {
    // -- General (0-99) --
    Ok                         = 0,
    Unknown                    = 1,
    InvalidArgument            = 2,
    OutOfMemory                = 3,
    NotImplemented             = 4,
    InvalidState               = 5,

    // -- Window / Platform (100-199) --
    WindowCreationFailed       = 100,
    WindowSurfaceCreationFailed = 101,
    WindowAlreadyInitialized   = 102,

    // -- Vulkan / Renderer (200-299) --
    VulkanInitFailed           = 200,
    VulkanDeviceCreationFailed = 201,
    VulkanSwapchainFailed      = 202,
    PipelineCreationFailed     = 203,
    ShaderLoadFailed           = 204,

    // -- ECS (300-399) --
    EntityLimitReached         = 300,
    EntityNotFound             = 301,
    ComponentNotFound          = 302,
    ComponentAlreadyExists     = 303,
    SystemDependencyCycle      = 304,

    // -- Map (400-499) --
    MapGenerationFailed        = 400,
    InvalidHexCoordinate       = 401,
    PathNotFound               = 402,

    // -- Serialization (500-599) --
    SaveFailed                 = 500,
    LoadFailed                 = 501,
    SaveVersionMismatch        = 502,
    SaveCorrupted              = 503,

    // -- Economy (600-699) --
    InsufficientResources      = 600,
    TradeRouteInvalid          = 601,
    MarketPriceOverflow        = 602,

    // -- Simulation (700-799) --
    InvalidUnitAction          = 700,
    InvalidCityAction          = 701,
    TechPrerequisiteNotMet     = 702,
    InvalidMonetaryTransition  = 703,
};

/**
 * @brief Returns a human-readable description for an error code.
 * @param code The error code to describe.
 * @return A string_view with the description. Never empty.
 */
[[nodiscard]] constexpr std::string_view describeError(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:                          return "No error";
        case ErrorCode::Unknown:                     return "Unknown error";
        case ErrorCode::InvalidArgument:             return "Invalid argument provided";
        case ErrorCode::OutOfMemory:                 return "Out of memory";
        case ErrorCode::NotImplemented:              return "Feature not implemented";
        case ErrorCode::InvalidState:                return "Object in invalid state for this operation";

        case ErrorCode::WindowCreationFailed:        return "Failed to create platform window";
        case ErrorCode::WindowSurfaceCreationFailed: return "Failed to create Vulkan surface from window";
        case ErrorCode::WindowAlreadyInitialized:    return "Window already initialized";

        case ErrorCode::VulkanInitFailed:            return "Vulkan instance initialization failed";
        case ErrorCode::VulkanDeviceCreationFailed:  return "Vulkan device creation failed";
        case ErrorCode::VulkanSwapchainFailed:       return "Vulkan swapchain creation/recreation failed";
        case ErrorCode::PipelineCreationFailed:      return "Graphics pipeline creation failed";
        case ErrorCode::ShaderLoadFailed:            return "Shader module loading failed";

        case ErrorCode::EntityLimitReached:          return "Maximum entity count reached";
        case ErrorCode::EntityNotFound:              return "Entity does not exist or was destroyed";
        case ErrorCode::ComponentNotFound:           return "Entity does not have the requested component";
        case ErrorCode::ComponentAlreadyExists:      return "Entity already has this component type";
        case ErrorCode::SystemDependencyCycle:        return "Cycle detected in system dependency graph";

        case ErrorCode::MapGenerationFailed:         return "Procedural map generation failed";
        case ErrorCode::InvalidHexCoordinate:        return "Hex coordinate out of map bounds";
        case ErrorCode::PathNotFound:                return "No valid path between source and destination";

        case ErrorCode::SaveFailed:                  return "Failed to write save file";
        case ErrorCode::LoadFailed:                  return "Failed to read save file";
        case ErrorCode::SaveVersionMismatch:         return "Save file version not compatible";
        case ErrorCode::SaveCorrupted:               return "Save file data integrity check failed";

        case ErrorCode::InsufficientResources:       return "Not enough resources for this action";
        case ErrorCode::TradeRouteInvalid:           return "Trade route cannot be established";
        case ErrorCode::MarketPriceOverflow:         return "Market price calculation overflow";

        case ErrorCode::InvalidUnitAction:           return "Unit cannot perform this action";
        case ErrorCode::InvalidCityAction:           return "City cannot perform this action";
        case ErrorCode::TechPrerequisiteNotMet:      return "Technology prerequisites not researched";
        case ErrorCode::InvalidMonetaryTransition:   return "Cannot transition to this monetary system";
    }
    return "Unrecognized error code";
}

} // namespace aoc
