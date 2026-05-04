/**
 * @file PolygonClipping.cpp
 * @brief Implementations for the leaf-level polygon helpers declared in
 *        PolygonClipping.hpp (Sutherland-Hodgman + small geometric utils).
 *
 * Added 2026-05-04. No allocation tricks beyond what std::vector does for
 * us; the working-buffer ping-pong inside clipPolygonSH reuses two local
 * vectors so an N-edge clip costs at most 2 allocations regardless of
 * subject size.
 */

#include "aoc/map/gen/PolygonClipping.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace aoc::map::gen {

namespace {

/// Cross product (edge a->b, point p): >0 means p is left of a->b (inside
/// for a CCW clip polygon).
inline float crossSide(const std::pair<float, float>& a,
                       const std::pair<float, float>& b,
                       const std::pair<float, float>& p) {
    return (b.first - a.first) * (p.second - a.second)
         - (b.second - a.second) * (p.first - a.first);
}

/// Intersect segment s1->s2 against the infinite line through c1->c2.
/// Caller guarantees the segment crosses the line, so the denominator is
/// non-zero in normal use; we still fall back to s2 if it underflows.
inline std::pair<float, float> intersectLine(
        const std::pair<float, float>& s1,
        const std::pair<float, float>& s2,
        const std::pair<float, float>& c1,
        const std::pair<float, float>& c2) {
    const float x1 = s1.first,  y1 = s1.second;
    const float x2 = s2.first,  y2 = s2.second;
    const float x3 = c1.first,  y3 = c1.second;
    const float x4 = c2.first,  y4 = c2.second;

    const float denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::fabs(denom) < std::numeric_limits<float>::epsilon()) {
        return s2; // parallel / coincident: degenerate, prefer s2
    }
    const float t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / denom;
    return { x1 + t * (x2 - x1), y1 + t * (y2 - y1) };
}

/// Proper segment-segment intersection (open segments: shared endpoints
/// don't count). Used by isSelfIntersecting.
inline bool segmentsCrossOpen(const std::pair<float, float>& a,
                              const std::pair<float, float>& b,
                              const std::pair<float, float>& c,
                              const std::pair<float, float>& d) {
    const float d1 = crossSide(c, d, a);
    const float d2 = crossSide(c, d, b);
    const float d3 = crossSide(a, b, c);
    const float d4 = crossSide(a, b, d);

    if (((d1 > 0.0f && d2 < 0.0f) || (d1 < 0.0f && d2 > 0.0f)) &&
        ((d3 > 0.0f && d4 < 0.0f) || (d3 < 0.0f && d4 > 0.0f))) {
        return true;
    }
    return false; // collinear-overlap is not flagged here -- simplify first
}

} // namespace

PolygonRing clipPolygonSH(const PolygonRing& subject,
                           const PolygonRing& clip) {
    if (subject.size() < 3 || clip.size() < 3) {
        return {};
    }

    PolygonRing output = subject;
    PolygonRing input;
    input.reserve(subject.size() + clip.size());

    const std::size_t clipN = clip.size();
    for (std::size_t i = 0; i < clipN; ++i) {
        if (output.empty()) {
            return {};
        }
        const auto& c1 = clip[i];
        const auto& c2 = clip[(i + 1) % clipN];

        input.swap(output);
        output.clear();

        const std::size_t inN = input.size();
        for (std::size_t j = 0; j < inN; ++j) {
            const auto& cur  = input[j];
            const auto& prev = input[(j + inN - 1) % inN];

            const float prevSide = crossSide(c1, c2, prev);
            const float curSide  = crossSide(c1, c2, cur);

            // CCW clip: inside is left of edge (side >= 0).
            const bool prevIn = prevSide >= 0.0f;
            const bool curIn  = curSide  >= 0.0f;

            if (curIn) {
                if (!prevIn) {
                    output.push_back(intersectLine(prev, cur, c1, c2));
                }
                output.push_back(cur);
            } else if (prevIn) {
                output.push_back(intersectLine(prev, cur, c1, c2));
            }
        }
    }

    if (output.size() < 3) {
        return {};
    }
    return output;
}

float polygonArea(const PolygonRing& poly) {
    if (poly.size() < 3) {
        return 0.0f;
    }
    float sum = 0.0f;
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto& a = poly[i];
        const auto& b = poly[(i + 1) % n];
        sum += (a.first * b.second) - (b.first * a.second);
    }
    return 0.5f * sum;
}

bool isCCW(const PolygonRing& poly) {
    return polygonArea(poly) > 0.0f;
}

void ensureCCW(PolygonRing& poly) {
    if (poly.size() < 3) {
        return;
    }
    if (polygonArea(poly) < 0.0f) {
        // reverse keeping vertex 0 in place is not required; plain reverse
        // is enough for orientation purposes.
        const std::size_t n = poly.size();
        for (std::size_t i = 0, j = n - 1; i < j; ++i, --j) {
            std::swap(poly[i], poly[j]);
        }
    }
}

AABB polygonAABB(const PolygonRing& poly) {
    if (poly.empty()) {
        return AABB{ 0.0f, 0.0f, 0.0f, 0.0f };
    }
    float minX = poly[0].first;
    float maxX = poly[0].first;
    float minY = poly[0].second;
    float maxY = poly[0].second;
    for (std::size_t i = 1; i < poly.size(); ++i) {
        const float x = poly[i].first;
        const float y = poly[i].second;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }
    return AABB{ minX, minY, maxX, maxY };
}

bool pointInPolygon(float px, float py,
                     const PolygonRing& poly,
                     const AABB& box) {
    if (poly.size() < 3) {
        return false;
    }
    if (px < box.minX || px > box.maxX || py < box.minY || py > box.maxY) {
        return false;
    }
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const float xi = poly[i].first,  yi = poly[i].second;
        const float xj = poly[j].first,  yj = poly[j].second;
        const bool crosses = ((yi > py) != (yj > py)) &&
            (px < (xj - xi) * (py - yi) / (yj - yi + std::numeric_limits<float>::epsilon()) + xi);
        if (crosses) {
            inside = !inside;
        }
    }
    return inside;
}

std::pair<float, float> polygonCentroid(const PolygonRing& poly) {
    if (poly.size() < 3) {
        return { 0.0f, 0.0f };
    }
    float cx = 0.0f;
    float cy = 0.0f;
    float doubleArea = 0.0f;
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto& a = poly[i];
        const auto& b = poly[(i + 1) % n];
        const float cross = a.first * b.second - b.first * a.second;
        doubleArea += cross;
        cx += (a.first + b.first)  * cross;
        cy += (a.second + b.second) * cross;
    }
    if (std::fabs(doubleArea) < std::numeric_limits<float>::epsilon()) {
        return { 0.0f, 0.0f };
    }
    const float scale = 1.0f / (3.0f * doubleArea);
    return { cx * scale, cy * scale };
}

bool isSelfIntersecting(const PolygonRing& poly) {
    const std::size_t n = poly.size();
    if (n < 4) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const auto& a = poly[i];
        const auto& b = poly[(i + 1) % n];
        // start at i+2 to skip the adjacent edge; stop before wrapping
        // back to the edge that shares a vertex with edge i.
        for (std::size_t k = i + 2; k < n; ++k) {
            // skip the edge that connects back to vertex i
            if (i == 0 && k == n - 1) {
                continue;
            }
            const auto& c = poly[k];
            const auto& d = poly[(k + 1) % n];
            if (segmentsCrossOpen(a, b, c, d)) {
                return true;
            }
        }
    }
    return false;
}

void simplifyPolygon(PolygonRing& poly, float epsilon) {
    if (poly.size() < 3) {
        return;
    }
    const float epsSq = epsilon * epsilon;

    // Pass 1: remove near-duplicate consecutive vertices.
    PolygonRing tmp;
    tmp.reserve(poly.size());
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const auto& cur = poly[i];
        if (!tmp.empty()) {
            const auto& prev = tmp.back();
            const float dx = cur.first - prev.first;
            const float dy = cur.second - prev.second;
            if (dx * dx + dy * dy <= epsSq) {
                continue;
            }
        }
        tmp.push_back(cur);
    }
    // Wrap-around dedup against vertex 0.
    if (tmp.size() >= 2) {
        const auto& first = tmp.front();
        const auto& last  = tmp.back();
        const float dx = last.first - first.first;
        const float dy = last.second - first.second;
        if (dx * dx + dy * dy <= epsSq) {
            tmp.pop_back();
        }
    }

    if (tmp.size() < 3) {
        poly = std::move(tmp);
        return;
    }

    // Pass 2: drop collinear vertices.
    PolygonRing out;
    out.reserve(tmp.size());
    const std::size_t m = tmp.size();
    for (std::size_t i = 0; i < m; ++i) {
        const auto& prev = tmp[(i + m - 1) % m];
        const auto& cur  = tmp[i];
        const auto& next = tmp[(i + 1) % m];
        const float cx = crossSide(prev, next, cur);
        if (std::fabs(cx) > epsilon) {
            out.push_back(cur);
        }
    }

    if (out.size() < 3) {
        poly = std::move(tmp);
    } else {
        poly = std::move(out);
    }
}

} // namespace aoc::map::gen


// ---------------------------------------------------------------------------
// Inline tests. Compile a small standalone harness with:
//   g++ -std=c++20 -DAOC_POLYGON_CLIPPING_TESTS -Iinclude
//   src/map/gen/PolygonClipping.cpp -o /tmp/poly_tests && /tmp/poly_tests
// ---------------------------------------------------------------------------
#ifdef AOC_POLYGON_CLIPPING_TESTS

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

using aoc::map::gen::PolygonRing;
using aoc::map::gen::clipPolygonSH;
using aoc::map::gen::isCCW;
using aoc::map::gen::polygonArea;
using aoc::map::gen::isSelfIntersecting;
using aoc::map::gen::polygonCentroid;
using aoc::map::gen::polygonAABB;
using aoc::map::gen::pointInPolygon;

int g_passed = 0;
int g_failed = 0;

void expect(bool cond, const char* name) {
    if (cond) {
        ++g_passed;
        std::printf("[PASS] %s\n", name);
    } else {
        ++g_failed;
        std::printf("[FAIL] %s\n", name);
    }
}

bool nearlyEqual(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

void testSquareClipsSquare() {
    PolygonRing a{ {0, 0}, {10, 0}, {10, 10}, {0, 10} };
    PolygonRing b{ {5, 5}, {15, 5}, {15, 15}, {5, 15} };
    auto out = clipPolygonSH(a, b);
    expect(out.size() == 4, "square clip square: 4 verts");
    expect(nearlyEqual(polygonArea(out), 25.0f), "square clip square: area 25");
}

void testTriangleClipsSquare() {
    PolygonRing tri{ {0, 0}, {10, 0}, {5, 10} };
    PolygonRing sq { {2, 0}, {8, 0}, {8, 8}, {2, 8} };
    auto out = clipPolygonSH(tri, sq);
    expect(out.size() >= 3, "triangle clip square: non-empty");
    expect(polygonArea(out) > 0.0f, "triangle clip square: positive area");
    expect(polygonArea(out) < polygonArea(sq), "triangle clip square: smaller than square");
}

void testDisjoint() {
    PolygonRing a{ {0, 0}, {1, 0}, {1, 1}, {0, 1} };
    PolygonRing b{ {10, 10}, {11, 10}, {11, 11}, {10, 11} };
    auto out = clipPolygonSH(a, b);
    expect(out.empty(), "disjoint polygons: empty result");
}

void testFullyContained() {
    PolygonRing inner{ {2, 2}, {3, 2}, {3, 3}, {2, 3} };
    PolygonRing outer{ {0, 0}, {10, 0}, {10, 10}, {0, 10} };
    auto out = clipPolygonSH(inner, outer);
    expect(out.size() == 4, "fully contained: 4 verts");
    expect(nearlyEqual(polygonArea(out), 1.0f), "fully contained: area 1");
}

void testCCWDetection() {
    PolygonRing ccw{ {0, 0}, {10, 0}, {10, 10}, {0, 10} };
    PolygonRing cw { {0, 0}, {0, 10}, {10, 10}, {10, 0} };
    expect(isCCW(ccw), "CCW detection: ccw == true");
    expect(!isCCW(cw),  "CCW detection: cw  == false");
}

void testBowtieSelfIntersect() {
    // Bowtie: edges (0->1) and (2->3) cross.
    PolygonRing bow{ {0, 0}, {10, 10}, {10, 0}, {0, 10} };
    expect(isSelfIntersecting(bow), "bowtie: self-intersecting");

    PolygonRing ok{ {0, 0}, {10, 0}, {10, 10}, {0, 10} };
    expect(!isSelfIntersecting(ok), "square: not self-intersecting");
}

void testCentroidAndAABB() {
    PolygonRing sq{ {0, 0}, {10, 0}, {10, 10}, {0, 10} };
    auto c = polygonCentroid(sq);
    expect(nearlyEqual(c.first, 5.0f) && nearlyEqual(c.second, 5.0f),
           "centroid: square center");
    auto box = polygonAABB(sq);
    expect(nearlyEqual(box.minX, 0.0f) && nearlyEqual(box.maxX, 10.0f),
           "AABB: x bounds");
    expect(pointInPolygon(5.0f, 5.0f, sq, box), "PIP: center inside");
    expect(!pointInPolygon(20.0f, 20.0f, sq, box), "PIP: far outside rejected");
}

} // namespace

int main() {
    testSquareClipsSquare();
    testTriangleClipsSquare();
    testDisjoint();
    testFullyContained();
    testCCWDetection();
    testBowtieSelfIntersect();
    testCentroidAndAABB();
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}

#endif // AOC_POLYGON_CLIPPING_TESTS
