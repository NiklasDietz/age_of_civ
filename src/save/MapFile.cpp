/**
 * @file MapFile.cpp
 * @brief Full-fidelity HexGrid file (see MapFile.hpp).
 *
 * Layout: u32 magic "AOCM", u32 version, i32 width, i32 height, u8 topology,
 * u64 generator seed, then the writeGridLayers() block (u32 record count, per
 * record: string name, u8 element kind, u32 element count, elements). The save
 * format's SectionId::MapLayers is the same block restricted to the game layers
 * (writeGameGridLayers). Little-endian via WriteBuffer.
 */

#include "aoc/save/MapFile.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexGridLayers.hpp"
#include "aoc/save/Serializer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aoc::save {

namespace {

constexpr uint32_t MAP_FILE_MAGIC   = 0x4D434F41u; // "AOCM"
constexpr uint32_t MAP_FILE_VERSION = 1;
constexpr std::size_t HEADER_BYTES  = 4 + 4 + 4 + 4 + 1 + 8;

enum class LayerKind : uint8_t { U8 = 1, I8, U16, I16, I32, F32, PairF32, MapI32U16 };

template <class T> constexpr LayerKind kindOf() {
    if constexpr (std::is_enum_v<T>) {
        return kindOf<std::underlying_type_t<T>>();
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        return LayerKind::U8;
    } else if constexpr (std::is_same_v<T, int8_t>) {
        return LayerKind::I8;
    } else if constexpr (std::is_same_v<T, uint16_t> || std::is_same_v<T, ResourceId>) {
        return LayerKind::U16;
    } else if constexpr (std::is_same_v<T, int16_t>) {
        return LayerKind::I16;
    } else if constexpr (std::is_same_v<T, int32_t>) {
        return LayerKind::I32;
    } else if constexpr (std::is_same_v<T, float>) {
        return LayerKind::F32;
    } else if constexpr (std::is_same_v<T, std::pair<float, float>>) {
        return LayerKind::PairF32;
    } else {
        static_assert(!std::is_same_v<T, T>, "unsupported HexGrid layer element type");
    }
}

constexpr std::size_t elementBytes(LayerKind kind) {
    switch (kind) {
    case LayerKind::U8:
    case LayerKind::I8:
        return 1;
    case LayerKind::U16:
    case LayerKind::I16:
        return 2;
    case LayerKind::I32:
    case LayerKind::F32:
        return 4;
    case LayerKind::PairF32:
        return 8;
    case LayerKind::MapI32U16:
        return 6;
    }
    return 0;
}

template <class T> void writeElement(WriteBuffer& out, const T& value) {
    if constexpr (std::is_enum_v<T>) {
        writeElement(out, static_cast<std::underlying_type_t<T>>(value));
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        out.writeU8(value);
    } else if constexpr (std::is_same_v<T, int8_t>) {
        out.writeU8(static_cast<uint8_t>(value));
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        out.writeU16(value);
    } else if constexpr (std::is_same_v<T, ResourceId>) {
        out.writeU16(value.value);
    } else if constexpr (std::is_same_v<T, int16_t>) {
        out.writeU16(static_cast<uint16_t>(value));
    } else if constexpr (std::is_same_v<T, int32_t>) {
        out.writeI32(value);
    } else if constexpr (std::is_same_v<T, float>) {
        out.writeF32(value);
    } else {
        out.writeF32(value.first);
        out.writeF32(value.second);
    }
}

template <class T> T readElement(ReadBuffer& in) {
    if constexpr (std::is_enum_v<T>) {
        return static_cast<T>(readElement<std::underlying_type_t<T>>(in));
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        return in.readU8();
    } else if constexpr (std::is_same_v<T, int8_t>) {
        return static_cast<int8_t>(in.readU8());
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        return in.readU16();
    } else if constexpr (std::is_same_v<T, ResourceId>) {
        return ResourceId{in.readU16()};
    } else if constexpr (std::is_same_v<T, int16_t>) {
        return static_cast<int16_t>(in.readU16());
    } else if constexpr (std::is_same_v<T, int32_t>) {
        return in.readI32();
    } else if constexpr (std::is_same_v<T, float>) {
        return in.readF32();
    } else {
        const float first  = in.readF32();
        const float second = in.readF32();
        return T{first, second};
    }
}

std::string indexedName(std::string_view name, std::size_t index) {
    return std::string(name) + "[" + std::to_string(index) + "]";
}

/// visitLayers() visitor: appends one record per layer (array layers as one
/// record per slice, "name[i]").
struct LayerWriter {
    WriteBuffer& out;
    bool gameLayersOnly;
    uint32_t layerCount   = 0;
    uint64_t m_totalBytes = 0;
    std::vector<std::pair<std::string, uint64_t>> m_tally;

    LayerWriter(WriteBuffer& target, bool gameOnly) : out(target), gameLayersOnly(gameOnly) {}

    template <class T> void operator()(std::string_view name, const std::vector<T>& values) {
        if (this->gameLayersOnly && !isGameGridLayer(name)) {
            return;
        }
        this->out.writeString(name);
        this->out.writeU8(static_cast<uint8_t>(kindOf<T>()));
        this->out.writeU32(static_cast<uint32_t>(values.size()));
        for (const T& value : values) {
            writeElement(this->out, value);
        }
        const uint64_t lb = static_cast<uint64_t>(elementBytes(kindOf<T>())) * values.size();
        this->m_tally.push_back({std::string(name), lb});
        this->m_totalBytes += lb;
        ++this->layerCount;
    }

    template <class T, std::size_t N>
    void operator()(std::string_view name, const std::array<std::vector<T>, N>& slices) {
        if (this->gameLayersOnly && !isGameGridLayer(name)) {
            return;
        }
        for (std::size_t i = 0; i < N; ++i) {
            (*this)(indexedName(name, i), slices[i]);
        }
    }

    void operator()(std::string_view name, const std::unordered_map<int32_t, uint16_t>& sparse) {
        if (this->gameLayersOnly && !isGameGridLayer(name)) {
            return;
        }
        std::vector<std::pair<int32_t, uint16_t>> sorted(sparse.begin(), sparse.end());
        std::sort(sorted.begin(), sorted.end());
        this->out.writeString(name);
        this->out.writeU8(static_cast<uint8_t>(LayerKind::MapI32U16));
        this->out.writeU32(static_cast<uint32_t>(sorted.size()));
        for (const std::pair<int32_t, uint16_t>& entry : sorted) {
            this->out.writeI32(entry.first);
            this->out.writeU16(entry.second);
        }
        const uint64_t lb = static_cast<uint64_t>(6) * sorted.size();
        this->m_tally.push_back({std::string(name), lb});
        this->m_totalBytes += lb;
        ++this->layerCount;
    }
};

/// Reads one layer's elements into its target; false when the stored kind
/// does not match the member's element type (the caller then skips it).
using LayerReader = std::function<bool(ReadBuffer&, LayerKind, uint32_t)>;

/// visitLayers() visitor: one LayerReader per layer name, bound to the grid
/// member it fills. Bind after initialize(): the members must already exist.
struct LayerBinder {
    std::unordered_map<std::string, LayerReader> readers;

    template <class T> void operator()(std::string_view name, std::vector<T>& values) {
        std::vector<T>* target = &values;
        this->readers.emplace(std::string(name),
                              [target](ReadBuffer& in, LayerKind kind, uint32_t count) {
                                  if (kind != kindOf<T>()) {
                                      return false;
                                  }
                                  target->resize(count);
                                  for (uint32_t i = 0; i < count && !in.isCorrupt(); ++i) {
                                      (*target)[i] = readElement<T>(in);
                                  }
                                  return true;
                              });
    }

    template <class T, std::size_t N>
    void operator()(std::string_view name, std::array<std::vector<T>, N>& slices) {
        for (std::size_t i = 0; i < N; ++i) {
            (*this)(indexedName(name, i), slices[i]);
        }
    }

    void operator()(std::string_view name, std::unordered_map<int32_t, uint16_t>& sparse) {
        std::unordered_map<int32_t, uint16_t>* target = &sparse;
        this->readers.emplace(std::string(name),
                              [target](ReadBuffer& in, LayerKind kind, uint32_t count) {
                                  if (kind != LayerKind::MapI32U16) {
                                      return false;
                                  }
                                  target->clear();
                                  for (uint32_t i = 0; i < count && !in.isCorrupt(); ++i) {
                                      const int32_t key    = in.readI32();
                                      const uint16_t value = in.readU16();
                                      (*target)[key]       = value;
                                  }
                                  return true;
                              });
    }
};

/// The per-tile state HexGrid::initialize() sizes. Every other layer is a
/// worldgen product with an absent-layer fallback (HexGrid.cpp), so the save
/// omits it: 17 MB -> about 2 MB for a 400x200 map (Block 0.2, 2026-09-04).
constexpr std::array<std::string_view, 17> GAME_GRID_LAYERS = {
    "terrain",     "feature",       "elevation",      "riverEdges",
    "resource",    "reserves",      "prospectCooldown", "owner",
    "improvement", "road",          "tileInfra",      "greenhouseCrop",
    "naturalWonder", "chokepoint",  "falloutTurns",   "preFalloutFeature",
    "antiquitySite",   // v14
};

void writeLayerBlock(WriteBuffer& out, const aoc::map::HexGrid& grid, bool gameLayersOnly) {
    WriteBuffer records;
    LayerWriter writer{records, gameLayersOnly};
    grid.visitLayers(writer);
    out.writeU32(writer.layerCount);
    out.writeBytes(records.data().data(), records.size());
#ifndef NDEBUG
    // Per-layer byte tally, largest first (LOG_DEBUG is compiled out under NDEBUG).
    std::sort(writer.m_tally.begin(), writer.m_tally.end(),
              [](const std::pair<std::string, uint64_t>& a,
                 const std::pair<std::string, uint64_t>& b) { return a.second > b.second; });
    for (const std::pair<std::string, uint64_t>& entry : writer.m_tally) {
        LOG_DEBUG("[MapFile] layer %-40s %7llu KB", entry.first.c_str(),
                  static_cast<unsigned long long>(entry.second / 1024));
    }
#endif
    LOG_INFO("[MapFile] %s: %zu records, total payload %.1f MB",
             gameLayersOnly ? "game layers" : "all layers",
             static_cast<std::size_t>(writer.m_tally.size()),
             static_cast<double>(writer.m_totalBytes) / (1024.0 * 1024.0));
}

} // namespace

bool isGameGridLayer(std::string_view name) {
    return std::find(GAME_GRID_LAYERS.begin(), GAME_GRID_LAYERS.end(), name) !=
           GAME_GRID_LAYERS.end();
}

void writeGridLayers(WriteBuffer& out, const aoc::map::HexGrid& grid) {
    writeLayerBlock(out, grid, false);
}

void writeGameGridLayers(WriteBuffer& out, const aoc::map::HexGrid& grid) {
    writeLayerBlock(out, grid, true);
}

ErrorCode readGridLayers(ReadBuffer& in, aoc::map::HexGrid& grid, const char* source) {
    if (!in.hasRemaining(4)) {
        LOG_ERROR("MapFile: '%s' ends before the layer count", source);
        return ErrorCode::SaveCorrupted;
    }
    const uint32_t layerCount = in.readU32();
    LayerBinder binder;
    grid.visitLayers(binder);

    uint32_t applied = 0;
    uint32_t skipped = 0;
    for (uint32_t i = 0; i < layerCount; ++i) {
        const std::string name     = in.readString();
        const LayerKind kind       = static_cast<LayerKind>(in.readU8());
        const uint32_t count       = in.readU32();
        const std::size_t perEntry = elementBytes(kind);
        if (in.isCorrupt() || perEntry == 0 || !in.canReadRecords(count, perEntry)) {
            LOG_ERROR("MapFile: layer '%s' (%u entries) exceeds '%s'", name.c_str(), count,
                      source);
            return ErrorCode::SaveCorrupted;
        }
        const std::unordered_map<std::string, LayerReader>::iterator reader =
            binder.readers.find(name);
        if (reader == binder.readers.end() || !reader->second(in, kind, count)) {
            in.skip(static_cast<std::size_t>(count) * perEntry);
            ++skipped;
            continue;
        }
        ++applied;
    }
    if (in.isCorrupt()) {
        LOG_ERROR("MapFile: '%s' ended inside a layer", source);
        return ErrorCode::SaveCorrupted;
    }
    if (skipped > 0) {
        LOG_WARN("MapFile: %u unknown or mismatched layers skipped in '%s'", skipped, source);
    }
    LOG_INFO("MapFile: %u layers read from '%s'", applied, source);
    return ErrorCode::Ok;
}

ErrorCode saveMapFile(const std::string& filepath, const aoc::map::HexGrid& grid,
                      const MapFileInfo& info) {
    WriteBuffer out;
    out.writeU32(MAP_FILE_MAGIC);
    out.writeU32(MAP_FILE_VERSION);
    out.writeI32(grid.width());
    out.writeI32(grid.height());
    out.writeU8(static_cast<uint8_t>(grid.topology()));
    out.writeU64(info.generatorSeed);
    writeGridLayers(out, grid);

    const std::string tmpPath = filepath + ".tmp";
    {
        std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            LOG_ERROR("MapFile: cannot open '%s' for writing", tmpPath.c_str());
            return ErrorCode::SaveFailed;
        }
        file.write(reinterpret_cast<const char*>(out.data().data()),
                   static_cast<std::streamsize>(out.size()));
        if (!file.good()) {
            LOG_ERROR("MapFile: write to '%s' failed", tmpPath.c_str());
            file.close();
            std::remove(tmpPath.c_str());
            return ErrorCode::SaveFailed;
        }
    }
    if (std::rename(tmpPath.c_str(), filepath.c_str()) != 0) {
        LOG_ERROR("MapFile: rename '%s' -> '%s' failed", tmpPath.c_str(), filepath.c_str());
        std::remove(tmpPath.c_str());
        return ErrorCode::SaveFailed;
    }
    LOG_INFO("MapFile: wrote %dx%d, %zu bytes to '%s'", grid.width(), grid.height(), out.size(),
             filepath.c_str());
    return ErrorCode::Ok;
}

ErrorCode loadMapFile(const std::string& filepath, aoc::map::HexGrid& grid, MapFileInfo* info) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("MapFile: cannot open '%s'", filepath.c_str());
        return ErrorCode::LoadFailed;
    }
    const std::streamsize fileSize = file.tellg();
    if (fileSize < 0) {
        LOG_ERROR("MapFile: cannot size '%s'", filepath.c_str());
        return ErrorCode::LoadFailed;
    }
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<std::size_t>(fileSize));
    file.read(reinterpret_cast<char*>(bytes.data()), fileSize);
    if (!file.good()) {
        LOG_ERROR("MapFile: short read on '%s'", filepath.c_str());
        return ErrorCode::LoadFailed;
    }

    ReadBuffer in(bytes);
    if (!in.hasRemaining(HEADER_BYTES)) {
        LOG_ERROR("MapFile: '%s' is shorter than a header", filepath.c_str());
        return ErrorCode::SaveCorrupted;
    }
    if (in.readU32() != MAP_FILE_MAGIC) {
        LOG_ERROR("MapFile: '%s' is not a map file", filepath.c_str());
        return ErrorCode::SaveCorrupted;
    }
    const uint32_t version = in.readU32();
    if (version != MAP_FILE_VERSION) {
        LOG_ERROR("MapFile: '%s' is version %u, this build reads %u", filepath.c_str(), version,
                  MAP_FILE_VERSION);
        return ErrorCode::SaveVersionMismatch;
    }
    const int32_t width                  = in.readI32();
    const int32_t height                 = in.readI32();
    const aoc::map::MapTopology topology = static_cast<aoc::map::MapTopology>(in.readU8());
    const uint64_t seed                  = in.readU64();
    if (width <= 0 || height <= 0 || width > aoc::map::HexGrid::MAX_MAP_DIMENSION ||
        height > aoc::map::HexGrid::MAX_MAP_DIMENSION) {
        LOG_ERROR("MapFile: dimensions %dx%d outside (0, %d]", width, height,
                  aoc::map::HexGrid::MAX_MAP_DIMENSION);
        return ErrorCode::SaveCorrupted;
    }

    aoc::map::HexGrid loaded;
    loaded.initialize(width, height, topology);
    const ErrorCode layers = readGridLayers(in, loaded, filepath.c_str());
    if (layers != ErrorCode::Ok) {
        return layers;
    }

    grid = std::move(loaded);
    if (info != nullptr) {
        info->generatorSeed = seed;
    }
    LOG_INFO("MapFile: loaded %dx%d from '%s'", width, height, filepath.c_str());
    return ErrorCode::Ok;
}

} // namespace aoc::save
