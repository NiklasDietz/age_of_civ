/**
 * @file GlobeRenderer.cpp
 * @brief 3D textured-sphere renderer for the Continent Creator.
 *
 * Implementation strategy:
 *   - One Renderer3D instance, owned exclusively by this module.
 *   - Sphere geometry: rings (latitude) x segments (longitude) lattice
 *     of small quads, each tangent to the unit sphere. Per-terrain
 *     sub-meshes built each `updateFromGrid()` so a single draw call
 *     per terrain colours the matching faces.
 *   - Material palette: one PBR material per TerrainType, built once
 *     in `initialize()` and held for the lifetime of the renderer.
 *   - Camera: orbit camera built from yaw/pitch/zoom on every render()
 *     call. Sun light pinned at +Y world space.
 *
 * Rationale: Renderer3D's Material has no texture sampler -- albedo
 * is a flat RGB constant in the material UBO. Per-cell colouring on
 * a single mesh is impossible without modifying the shader. Splitting
 * by terrain into a handful of sub-meshes (10-11) keeps draw-call
 * count low and reuses the existing forward3d pipeline as-is.
 */

#include "aoc/render/GlobeRenderer.hpp"

#include "aoc/map/Terrain.hpp"

#include <renderer/Renderer3D.hpp>
#include <renderer/SceneTypes.hpp>
#include "vulkan_utils/Device.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace aoc::render {

namespace {

constexpr int32_t  GLOBE_RINGS    = 360;  ///< latitude bands (0.5 deg per quad, matches SphereField)
constexpr int32_t  GLOBE_SEGMENTS = 720;  ///< longitude bands (0.5 deg per quad, matches SphereField)
constexpr float    SPHERE_RADIUS  = 1.0f;
constexpr float    PI_F           = 3.14159265358979323846f;

/// Terrain ID set covered by the per-terrain sub-mesh palette. Keep
/// in sync with `aoc::map::TerrainType` -- any unmapped terrain
/// silently falls through to Plains in the lookup.
/// Topographic-relief palette. 32 tiers from deepest ocean to highest
/// peak, plus 4 polar/cold tints. Each cell on the sphere maps to
/// exactly one tier by its (elevation, latitude) coordinates. With a
/// fine continuous gradient, biome transitions read as smooth shading
/// rather than the chunky horizontal stripes a coarse-threshold scheme
/// produces.
constexpr std::size_t kPaletteSize = 32;

struct GlobeColor { float r, g, b; };

/// Lerp helper for compile-time gradient table.
constexpr GlobeColor mix(GlobeColor a, GlobeColor b, float t) {
    return {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
    };
}

/// Build the 32-step gradient: ocean depths -> coast -> beach ->
/// grassland -> forest -> hills -> mountain -> peak.
[[nodiscard]] constexpr std::array<GlobeColor, kPaletteSize>
buildPalette() {
    std::array<GlobeColor, kPaletteSize> p{};
    // 0..7  : ocean, deep -> shallow
    const GlobeColor abyss = {0.020f, 0.060f, 0.220f};
    const GlobeColor sea   = {0.130f, 0.310f, 0.620f};
    const GlobeColor shelf = {0.350f, 0.650f, 0.850f};
    for (std::size_t i = 0; i < 4; ++i) {
        p[i] = mix(abyss, sea, static_cast<float>(i) / 3.0f);
    }
    for (std::size_t i = 0; i < 4; ++i) {
        p[4 + i] = mix(sea, shelf, static_cast<float>(i) / 3.0f);
    }
    // 8..11 : beach + dry coast -> dark beach blend.
    const GlobeColor beach = {0.880f, 0.830f, 0.620f};
    const GlobeColor dryGrass = {0.700f, 0.730f, 0.420f};
    for (std::size_t i = 0; i < 4; ++i) {
        p[8 + i] = mix(beach, dryGrass, static_cast<float>(i) / 3.0f);
    }
    // 12..19 : grassland -> forest gradient.
    const GlobeColor lightGrass = {0.420f, 0.700f, 0.350f};
    const GlobeColor forest     = {0.180f, 0.520f, 0.220f};
    for (std::size_t i = 0; i < 8; ++i) {
        p[12 + i] = mix(lightGrass, forest, static_cast<float>(i) / 7.0f);
    }
    // 20..25 : highland transition into mountain.
    const GlobeColor uplands  = {0.520f, 0.500f, 0.320f};
    const GlobeColor mountain = {0.420f, 0.380f, 0.330f};
    for (std::size_t i = 0; i < 6; ++i) {
        p[20 + i] = mix(uplands, mountain, static_cast<float>(i) / 5.0f);
    }
    // 26..31 : peak / snow / ice.
    const GlobeColor peakRock = {0.560f, 0.530f, 0.500f};
    const GlobeColor snow     = {0.940f, 0.950f, 0.960f};
    for (std::size_t i = 0; i < 6; ++i) {
        p[26 + i] = mix(peakRock, snow, static_cast<float>(i) / 5.0f);
    }
    return p;
}

constexpr std::array<GlobeColor, kPaletteSize> kPaletteColors = buildPalette();


/// Project (latRow, lonCol) on the SPHERE to a lat/lon pair.
struct LatLonDeg { float latDeg; float lonDeg; };

[[nodiscard]] LatLonDeg cellCenterDeg(int32_t ring, int32_t seg) noexcept {
    const float v = (static_cast<float>(ring) + 0.5f)
                  / static_cast<float>(GLOBE_RINGS);
    const float u = (static_cast<float>(seg) + 0.5f)
                  / static_cast<float>(GLOBE_SEGMENTS);
    return {-90.0f + v * 180.0f, -180.0f + u * 360.0f};
}

/// Map (latDeg, lonDeg) to the unit-sphere position used by the
/// orbit camera. Y is up, +X is at lon=0/lat=0, +Z is at lon=90.
[[nodiscard]] vulkan_app::Vec3 sphereXYZ(float latDeg, float lonDeg) noexcept {
    const float latR = latDeg * PI_F / 180.0f;
    const float lonR = lonDeg * PI_F / 180.0f;
    const float cosLat = std::cos(latR);
    return {
        SPHERE_RADIUS * cosLat * std::cos(lonR),
        SPHERE_RADIUS * std::sin(latR),
        SPHERE_RADIUS * cosLat * std::sin(lonR),
    };
}

/// Build the four sphere-surface vertex positions for the quad
/// occupying ring/seg, with their normals (radial unit vectors). The
/// quad's UVs are unused by forward3d but populated for completeness.
struct QuadVerts {
    vulkan_app::Vertex3D v[4];
};

[[nodiscard]] QuadVerts buildSphereQuad(int32_t ring, int32_t seg) noexcept {
    const float lat0 = -90.0f + static_cast<float>(ring)
                              * (180.0f / static_cast<float>(GLOBE_RINGS));
    const float lat1 = -90.0f + static_cast<float>(ring + 1)
                              * (180.0f / static_cast<float>(GLOBE_RINGS));
    const float lon0 = -180.0f + static_cast<float>(seg)
                                * (360.0f / static_cast<float>(GLOBE_SEGMENTS));
    const float lon1 = -180.0f + static_cast<float>(seg + 1)
                                * (360.0f / static_cast<float>(GLOBE_SEGMENTS));
    const vulkan_app::Vec3 p00 = sphereXYZ(lat0, lon0);
    const vulkan_app::Vec3 p01 = sphereXYZ(lat0, lon1);
    const vulkan_app::Vec3 p10 = sphereXYZ(lat1, lon0);
    const vulkan_app::Vec3 p11 = sphereXYZ(lat1, lon1);
    QuadVerts q;
    q.v[0] = {p00.x, p00.y, p00.z, p00.x, p00.y, p00.z, 0.0f, 0.0f};
    q.v[1] = {p01.x, p01.y, p01.z, p01.x, p01.y, p01.z, 1.0f, 0.0f};
    q.v[2] = {p11.x, p11.y, p11.z, p11.x, p11.y, p11.z, 1.0f, 1.0f};
    q.v[3] = {p10.x, p10.y, p10.z, p10.x, p10.y, p10.z, 0.0f, 1.0f};
    return q;
}

/// Authoritative SphereField raster dimensions (must match
/// SphereField::LON_CELLS / LAT_CELLS in the generator).
constexpr int32_t SF_LON_CELLS = 720;
constexpr int32_t SF_LAT_CELLS = 360;

/// Sample the SphereField surface-elevation snapshot stored on the
/// HexGrid (720*360 lat/lon raster) at (latDeg, lonDeg) and bin into
/// a TerrainType for material colouring. This is independent of the
/// HexGrid's projection -- the snapshot is the un-projected truth,
/// so cycling Mollweide / Equirect / Mercator / Robinson on the flat
/// view leaves the globe unchanged.
/// Map elevation (metres above mantle datum, sea level ~ 0) to a
/// palette slot index in [0, kPaletteSize). The mapping is a smooth
/// piecewise-linear transfer:
///   z < -3000 -> 0 (deep abyss)
///   z = 0     -> 11 (beach)
///   z = 1500  -> 17 (forest mid)
///   z = 4000  -> 25 (mountain peak base)
///   z > 5000  -> 31 (snow cap)
/// Latitude shifts the upper tier into the snow band: at |lat| > 60,
/// every continental cell tips up by ~6 slots (Tundra/Snow look).
[[nodiscard]] std::size_t sampleGrid(
    const aoc::map::HexGrid& grid, float latDeg, float lonDeg) noexcept {
    const auto& snap = grid.sphereFieldElevationSnapshot();
    if (snap.empty()) return 0;
    float lon = lonDeg;
    while (lon >  180.0f) lon -= 360.0f;
    while (lon < -180.0f) lon += 360.0f;
    int32_t lonIdx = static_cast<int32_t>(
        (lon + 180.0f) / 360.0f * static_cast<float>(SF_LON_CELLS));
    int32_t latIdx = static_cast<int32_t>(
        (latDeg + 90.0f) / 180.0f * static_cast<float>(SF_LAT_CELLS));
    if (lonIdx < 0) lonIdx = 0;
    if (lonIdx >= SF_LON_CELLS) lonIdx = SF_LON_CELLS - 1;
    if (latIdx < 0) latIdx = 0;
    if (latIdx >= SF_LAT_CELLS) latIdx = SF_LAT_CELLS - 1;
    const std::size_t idx = static_cast<std::size_t>(latIdx)
                          * static_cast<std::size_t>(SF_LON_CELLS)
                          + static_cast<std::size_t>(lonIdx);
    if (idx >= snap.size()) return 0;
    const float z = snap[idx];
    const float absLat = std::fabs(latDeg);

    float t;
    if (z < 0.0f) {
        // Ocean. -3000 m -> 0, 0 m -> 11. (Use 12 ocean+coast slots.)
        t = (z + 3000.0f) / 3000.0f * 11.0f;
    } else {
        // Land. 0 m -> 11 (beach), 4000 m -> 25 (mountain peak base),
        // 5000 m -> 31 (snow cap). 14 slots over 0..4000, 6 over rest.
        if (z <= 4000.0f) {
            t = 11.0f + z / 4000.0f * 14.0f;
        } else {
            t = 25.0f + std::min(z - 4000.0f, 1000.0f) / 1000.0f * 6.0f;
        }
        // Cold-latitude push: high-lat continental cells climb 4-8
        // slots so polar regions go Tundra (~22) to Snow (~30) instead
        // of grass.
        const float coldShift = std::max(0.0f, (absLat - 50.0f) / 30.0f) * 8.0f;
        t += coldShift;
    }
    int32_t s = static_cast<int32_t>(std::floor(t + 0.5f));
    if (s < 0) s = 0;
    if (s >= static_cast<int32_t>(kPaletteSize)) {
        s = static_cast<int32_t>(kPaletteSize) - 1;
    }
    return static_cast<std::size_t>(s);
}

} // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct GlobeRenderer::Impl {
    std::unique_ptr<vulkan_app::Renderer3D> r3d;
    const vkutils::Device*                  device = nullptr;
    std::array<vulkan_app::MeshHandle, kPaletteSize> meshes{};
    std::array<vulkan_app::MaterialHandle, kPaletteSize> materials{};
    bool initialised = false;
    bool gridDirty   = true;
};

GlobeRenderer::GlobeRenderer() = default;
GlobeRenderer::~GlobeRenderer() {
    // Wait for any pending GPU work that may still reference our
    // mesh/descriptor allocations before the embedded Renderer3D's
    // destructor runs cleanup. Without this the swap-chain's still-
    // queued frame can race the resource teardown.
    if (this->m_impl && this->m_impl->device != nullptr) {
        vkDeviceWaitIdle(this->m_impl->device->handle());
    }
}

void GlobeRenderer::initialize(const vkutils::Device& device,
                               VkRenderPass renderPass,
                               VkExtent2D extent) {
    std::fprintf(stderr, "[globe] initialize extent=%ux%u\n",
                 extent.width, extent.height);
    this->m_impl = std::make_unique<Impl>();
    this->m_impl->device = &device;
    this->m_impl->r3d = std::make_unique<vulkan_app::Renderer3D>(
        device, renderPass, extent);
    std::fprintf(stderr, "[globe] Renderer3D constructed\n");

    // Material palette: one PBR material per TerrainType. Roughness
    // 0.85 + metallic 0.0 keeps the look matte; the small ambient
    // term in the forward3d shader handles unlit polar caps.
    for (std::size_t i = 0; i < kPaletteSize; ++i) {
        const GlobeColor c = kPaletteColors[i];
        vulkan_app::Material mat;
        mat.albedo    = {c.r, c.g, c.b, 1.0f};
        mat.metallic  = 0.0f;
        mat.roughness = 0.85f;
        mat.emissive  = 0.0f;
        this->m_impl->materials[i] = this->m_impl->r3d->uploadMaterial(mat);
    }
    this->m_impl->initialised = true;
    std::fprintf(stderr, "[globe] init complete\n");
}

void GlobeRenderer::setExtent(VkExtent2D extent) {
    if (!this->m_impl || !this->m_impl->r3d) return;
    this->m_impl->r3d->setExtent(extent);
}

void GlobeRenderer::markGridDirty() {
    if (this->m_impl) this->m_impl->gridDirty = true;
}

void GlobeRenderer::updateFromGrid(const aoc::map::HexGrid& grid) {
    if (!this->m_impl || !this->m_impl->initialised) return;
    auto& r3d = *this->m_impl->r3d;

    // Stall the GPU before tearing down GPU buffers that may still be
    // referenced by an in-flight frame's draw commands. Cost is paid
    // only on actual scrub events, not per frame.
    if (this->m_impl->device != nullptr) {
        vkDeviceWaitIdle(this->m_impl->device->handle());
    }
    for (auto& h : this->m_impl->meshes) {
        if (h.isValid()) {
            r3d.destroyMesh(h);
            h = vulkan_app::MeshHandle{};
        }
    }

    // Build per-terrain quad lists. Each sphere cell samples the
    // HexGrid via equirect mapping and is appended to its terrain's
    // sub-mesh. A single Renderer3D draw per terrain colours the
    // matching faces -- the forward3d pipeline has no texture
    // sampler so we cannot bake all colours into one mesh.
    std::array<vulkan_app::MeshData, kPaletteSize> bins;

    for (int32_t ring = 0; ring < GLOBE_RINGS; ++ring) {
        for (int32_t seg = 0; seg < GLOBE_SEGMENTS; ++seg) {
            const LatLonDeg c = cellCenterDeg(ring, seg);
            const std::size_t pi = sampleGrid(grid, c.latDeg, c.lonDeg);
            const QuadVerts q = buildSphereQuad(ring, seg);
            auto& md = bins[pi];
            const uint32_t base = static_cast<uint32_t>(md.vertices.size());
            md.vertices.push_back(q.v[0]);
            md.vertices.push_back(q.v[1]);
            md.vertices.push_back(q.v[2]);
            md.vertices.push_back(q.v[3]);
            // Vulkan Y-down NDC inverts triangle winding vs. world-
            // space CCW; emit indices that read as CW in world (=
            // CCW in NDC, hence front face for the back-cull pipeline).
            md.indices.push_back(base + 0);
            md.indices.push_back(base + 2);
            md.indices.push_back(base + 1);
            md.indices.push_back(base + 0);
            md.indices.push_back(base + 3);
            md.indices.push_back(base + 2);
        }
    }

    for (std::size_t i = 0; i < bins.size(); ++i) {
        if (bins[i].vertices.empty()) continue;
        this->m_impl->meshes[i] = r3d.uploadMesh(bins[i]);
    }
}

void GlobeRenderer::render(VkCommandBuffer cmd, uint32_t frameIndex,
                           const aoc::map::HexGrid& grid,
                           float yawDeg, float pitchDeg, float zoom,
                           float aspect) {
    if (!this->m_impl || !this->m_impl->initialised) return;
    if (this->m_impl->gridDirty) {
        this->updateFromGrid(grid);
        this->m_impl->gridDirty = false;
    }
    auto& r3d = *this->m_impl->r3d;

    // Orbit camera. Yaw rotates around +Y (longitude), pitch around
    // the camera-relative right axis (latitude). Camera distance is
    // `zoom` unit-spheres from origin.
    const float yawR   = yawDeg   * PI_F / 180.0f;
    const float pitchR = pitchDeg * PI_F / 180.0f;
    const float r = std::max(1.2f, zoom);
    vulkan_app::Camera3D cam;
    cam.position = {
        r * std::cos(pitchR) * std::sin(yawR),
        r * std::sin(pitchR),
        r * std::cos(pitchR) * std::cos(yawR),
    };
    cam.target = {0.0f, 0.0f, 0.0f};
    cam.up     = {0.0f, 1.0f, 0.0f};
    // Orthographic projection: every parallel ray hits the sphere at
    // the same angle, so continent shapes project rigidly under camera
    // rotation -- no perspective foreshortening at silhouettes, no
    // "trapezoid stretch" effect. The orbit camera's `zoom` term then
    // controls the framing-rectangle half-width: zoom=4 means the
    // visible viewport spans 4 unit-sphere radii so the sphere takes
    // ~50 % of the shorter screen axis.
    cam.isOrthographic = true;
    const float halfH  = 0.5f * std::max(1.2f, zoom) * 0.6f;
    const float halfW  = halfH * aspect;
    cam.orthoLeft   = -halfW;
    cam.orthoRight  =  halfW;
    cam.orthoTop    = -halfH;
    cam.orthoBottom =  halfH;
    cam.nearPlane = -10.0f;
    cam.farPlane  =  10.0f;

    r3d.setCamera(cam);

    vulkan_app::Light sun;
    sun.type      = vulkan_app::LightType::Directional;
    sun.direction = {-0.4f, -0.7f, -0.5f};
    sun.color     = {1.0f, 0.96f, 0.92f};
    sun.intensity = 1.4f;
    r3d.addLight(sun);

    // Fill light from the opposite direction at lower intensity.
    // Without this, the hemisphere facing away from the sun goes
    // near-black (forward3d's ambient term is only 0.03 * albedo).
    // Rotating the camera into the dark side then makes continents
    // visually "disappear", reading as distortion.
    vulkan_app::Light fill;
    fill.type      = vulkan_app::LightType::Directional;
    fill.direction = { 0.4f,  0.5f,  0.5f};
    fill.color     = {0.65f, 0.75f, 0.95f};
    fill.intensity = 0.6f;
    r3d.addLight(fill);

    // Camera-space "key light" pinned to the orbit camera so rotation
    // doesn't change which face is bright. Compensates for the fixed
    // sun making one hemisphere always brighter than the other.
    vulkan_app::Light keyFromCam;
    keyFromCam.type      = vulkan_app::LightType::Directional;
    keyFromCam.direction = {-cam.position.x, -cam.position.y, -cam.position.z};
    keyFromCam.color     = {1.0f, 1.0f, 0.95f};
    keyFromCam.intensity = 0.5f;
    r3d.addLight(keyFromCam);

    vulkan_app::Transform identity;
    int submitted = 0;
    const bool skipDraw = std::getenv("AOC_GLOBE_SKIP_DRAW") != nullptr;
    if (!skipDraw) {
        for (std::size_t i = 0; i < this->m_impl->meshes.size(); ++i) {
            const auto& mh = this->m_impl->meshes[i];
            if (!mh.isValid()) continue;
            vulkan_app::DrawCommand3D dc;
            dc.mesh      = mh;
            dc.material  = this->m_impl->materials[i];
            dc.transform = identity;
            r3d.submit(dc);
            ++submitted;
        }
    }
    static int frameCounter = 0;
    if ((frameCounter++ & 0x3F) == 0) {
        std::fprintf(stderr, "[globe] render frame=%d submitted=%d frameIndex=%u skip=%d\n",
                     frameCounter, submitted, frameIndex, skipDraw);
    }
    r3d.render(cmd, frameIndex);
    r3d.endFrame();
}

} // namespace aoc::render
