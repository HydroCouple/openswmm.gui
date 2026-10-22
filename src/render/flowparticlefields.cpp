// GPL-3.0-or-later

/**
 * @file      flowparticlefields.cpp
 * @brief     Velocity fields and sampling used by flow particle animation.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "render/flowparticlefields.h"
#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{
namespace
{
/** @brief Compute the signed two-dimensional cross product.
 * @param a First vector.
 * @param b Second vector in the same coordinate unit.
 * @return a.x*b.y-a.y*b.x in squared coordinate units, without validation. */
double cross(QPointF a, QPointF b)
{
    return a.x() * b.y() - a.y() * b.x();
}
/** @brief Transform a physical vector by the triangle's affine metre-to-scene map.
 * @param in Native triangle with finite nondegenerate metre coordinates.
 * @param mesh Shared scene geometry with valid corresponding vertex indices.
 * @param cell In-range triangle index.
 * @param v Physical vector in metre-based units (e.g. m/s).
 * @return Corresponding scene-space vector (e.g. scene units/s), without
 * translation. No bounds, degeneracy or finite checks are performed here. */
QPointF toScene(const FlowParticleNativeTriangle &in, const FlowParticleMesh &mesh, int cell,
                QPointF v)
{
    const auto ab = in.metres[1] - in.metres[0], ac = in.metres[2] - in.metres[0];
    const double det = cross(ab, ac);
    const auto &ids = mesh.triangles[size_t(cell)];
    return (mesh.vertices[ids[1]] - mesh.vertices[ids[0]]) * (cross(v, ac) / det) +
           (mesh.vertices[ids[2]] - mesh.vertices[ids[0]]) * (cross(ab, v) / det);
}
}
std::shared_ptr<const FlowParticleSnapshot>
buildFlowParticleField(const FlowParticleNativeInput &in, const std::atomic_bool &cancelled)
{
    if (!in.mesh || in.triangles.size() != in.mesh->triangles.size())
        return {};
    in.mesh->prepare(cancelled);
    if (cancelled.load())
        return {};
    auto out = std::make_shared<FlowParticleSnapshot>();
    out->mesh = in.mesh;
    out->geometry = in.geometry;
    out->revision = in.revision;
    out->fields.resize(in.triangles.size());
    std::vector<std::array<QPointF, 3>> discharge(in.triangles.size());
    for (size_t i = 0; i < in.triangles.size(); ++i) {
        if (i % 4096 == 0 && cancelled.load())
            return {};
        auto t = in.triangles[i];
        auto &f = out->fields[i];
        f.depth = t.depth;
        f.residentEdges = t.residentEdges;
        f.available = t.available;
        f.exits = 0;
        f.velocityDepth = t.meanDepth > in.cutoff ? t.meanDepth : 1;
        const double area2 = std::abs(cross(t.metres[1] - t.metres[0], t.metres[2] - t.metres[0]));
        if (!std::isfinite(area2) || area2 < 1e-20 || !std::isfinite(t.meanDepth))
            f.available = false;
        if (t.velocityOnly) {
            if (!std::isfinite(t.velocitySI.x()) || !std::isfinite(t.velocitySI.y()))
                f.available = false;
            const double orientation =
                cross(t.metres[1] - t.metres[0], t.metres[2] - t.metres[0]) > 0 ? 1 : -1;
            for (int e = 0; e < 3; ++e) {
                const auto edge = t.metres[(e + 2) % 3] - t.metres[(e + 1) % 3];
                t.flux[e] = t.meanDepth * orientation *
                            (t.velocitySI.x() * edge.y() - t.velocitySI.y() * edge.x());
            }
        }
        for (double flux : t.flux)
            if (!std::isfinite(flux))
                f.available = false;
        for (auto p : t.metres)
            if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
                f.available = false;
        f.outwardFlux = t.flux;
        if (!f.available || t.meanDepth <= in.cutoff)
            continue;
        for (int e = 0; e < 3; ++e)
            if (t.flux[e] > 0)
                f.exits |= 1 << e;
        for (int k = 0; k < 3; ++k) {
            for (int e = 0; e < 3; ++e)
                discharge[i][k] += (t.metres[k] - t.metres[e]) * (t.flux[e] / area2);
            f.velocity[k] = toScene(t, *in.mesh, int(i), discharge[i][k] / t.meanDepth);
            f.speed[k] = float(std::hypot(discharge[i][k].x(), discharge[i][k].y()) / t.meanDepth);
        }
    }
    // Correct only tangential velocity on a connected face. Closed/dry faces
    // never borrow another fan's vector, and normal flux stays exactly RT0.
    for (size_t i = 0; i < in.triangles.size(); ++i) {
        if (i % 4096 == 0 && cancelled.load())
            return {};
        const auto &t = in.triangles[i];
        auto &f = out->fields[i];
        if (!f.available || t.velocityOnly || t.meanDepth <= in.cutoff)
            continue;
        for (int e = 0; e < 3; ++e) {
            const int neighbor = in.mesh->neighbors[i][e];
            if (neighbor < 0 || !out->fields[size_t(neighbor)].available ||
                in.triangles[size_t(neighbor)].meanDepth <= in.cutoff ||
                std::abs(t.flux[e]) < 1e-12)
                continue;
            int otherEdge = -1;
            for (int k = 0; k < 3; ++k)
                if (in.mesh->neighbors[size_t(neighbor)][k] == int(i))
                    otherEdge = k;
            if (otherEdge < 0)
                continue;
            const auto &other = in.triangles[size_t(neighbor)];
            if (other.velocityOnly)
                continue;
            // Incoherent hydraulic packets must not be blended into a vector
            // that disagrees with either side's reported crossing direction.
            if (t.flux[e] * other.flux[otherEdge] >= 0)
                continue;
            const auto edge = t.metres[(e + 2) % 3] - t.metres[(e + 1) % 3];
            const double length = std::hypot(edge.x(), edge.y());
            if (length <= 1e-12)
                continue;
            const auto tangent = edge / length;
            const auto a = (discharge[i][(e + 1) % 3] + discharge[i][(e + 2) % 3]) * .5;
            const auto b = (discharge[size_t(neighbor)][(otherEdge + 1) % 3] +
                            discharge[size_t(neighbor)][(otherEdge + 2) % 3]) *
                           .5;
            const auto correction =
                tangent *
                (QPointF::dotProduct(b / other.meanDepth - a / t.meanDepth, tangent) * .5);
            f.edgeCorrection[e] = toScene(t, *in.mesh, int(i), correction);
        }
    }
    return out;
}

FlowParticleSeedDistribution buildFlowParticleSeeds(const FlowParticleSnapshot &field, QRectF view,
                                                    double cutoff)
{
    FlowParticleSeedDistribution out;
    if (!field.mesh || !view.isValid() || !std::isfinite(cutoff) ||
        field.fields.size() != field.mesh->triangles.size() ||
        (!field.mesh->seedWeights.empty() && field.mesh->seedWeights.size() != field.fields.size()))
        return out;
    struct Corner {
        QPointF point;
        double depth;
    };
    struct Speed {
        double scene, si, area;
    };
    // A triangle clipped by five half-planes keeps at most eight corners.
    // Fixed ping-pong buffers avoid six heap allocations per regional triangle
    // on every view change; the clipping arithmetic itself is unchanged.
    std::array<Corner, 10> buffers[2];
    Corner *polygon = buffers[0].data(), *next = buffers[1].data();
    int corners = 0;
    std::vector<Speed> speeds;
    std::vector<double> areas, sceneSpeeds;
    out.triangles.reserve(field.fields.size());
    areas.reserve(field.fields.size());
    sceneSpeeds.reserve(field.fields.size());
    speeds.reserve(field.fields.size());
    for (size_t i = 0; i < field.fields.size(); ++i) {
        const auto &f = field.fields[i];
        if (!f.available)
            continue;
        const double seedWeight = field.mesh->seedWeights.empty() ? 1 : field.mesh->seedWeights[i];
        if (seedWeight <= 0)
            continue;
        bool finite = true;
        corners = 0;
        for (int k = 0; k < 3; ++k) {
            const auto point = field.mesh->vertices[field.mesh->triangles[i][k]];
            finite &=
                std::isfinite(f.depth[k]) && std::isfinite(point.x()) && std::isfinite(point.y());
            polygon[size_t(corners++)] = {point, f.depth[k]};
        }
        if (!finite)
            continue;
        const auto clip = [&](auto distance) {
            if (!corners)
                return;
            int count = 0;
            auto a = polygon[size_t(corners - 1)];
            double da = distance(a);
            for (int j = 0; j < corners; ++j) {
                const auto &b = polygon[size_t(j)];
                const double db = distance(b);
                if ((da > 0) != (db > 0)) {
                    const double t = da / (da - db);
                    next[size_t(count++)] = {a.point + (b.point - a.point) * t,
                                             a.depth + (b.depth - a.depth) * t};
                }
                if (db > 0)
                    next[size_t(count++)] = b;
                a = b;
                da = db;
            }
            std::swap(polygon, next);
            corners = count;
        };
        clip([&](Corner p) { return p.depth - cutoff; });
        clip([&](Corner p) { return p.point.x() - view.left(); });
        clip([&](Corner p) { return view.right() - p.point.x(); });
        clip([&](Corner p) { return p.point.y() - view.top(); });
        clip([&](Corner p) { return view.bottom() - p.point.y(); });
        if (corners < 3)
            continue;
        const auto &ids = field.mesh->triangles[i];
        const auto origin = field.mesh->vertices[ids[0]];
        const auto ab = field.mesh->vertices[ids[1]] - origin,
                   ac = field.mesh->vertices[ids[2]] - origin;
        const double determinant = cross(ab, ac);
        if (std::abs(determinant) < 1e-20)
            continue;
        for (int k = 1; k + 1 < corners; ++k) {
            const std::array<QPointF, 3> points{polygon[0].point, polygon[size_t(k)].point,
                                                polygon[size_t(k + 1)].point};
            const double area = std::abs(cross(points[1] - points[0], points[2] - points[0])) * .5;
            if (!std::isfinite(area) || area <= 1e-20)
                continue;
            const auto center = (points[0] + points[1] + points[2]) / 3.;
            const double b = cross(center - origin, ac) / determinant,
                         c = cross(ab, center - origin) / determinant;
            const std::array<double, 3> w{1 - b - c, b, c};
            auto velocity = f.velocity[0] * w[0] + f.velocity[1] * w[1] + f.velocity[2] * w[2];
            for (int e = 0; e < 3; ++e)
                velocity += f.edgeCorrection[e] * (4 * w[(e + 1) % 3] * w[(e + 2) % 3]);
            const double speed = std::hypot(velocity.x(), velocity.y());
            const double si = f.speed[0] * w[0] + f.speed[1] * w[1] + f.speed[2] * w[2];
            if (!std::isfinite(speed) || !std::isfinite(si))
                continue;
            out.triangles.push_back({points, int(i), 0});
            areas.push_back(area * seedWeight);
            sceneSpeeds.push_back(speed);
            out.wetArea += area * seedWeight;
            speeds.push_back({speed, si, area * seedWeight});
        }
    }
    // The reference speed is the first key, in ascending order, whose running
    // wet area reaches 90 %. Keep the sorted accumulation exactly: a selection
    // algorithm would change the summation order at the threshold.
    const auto percentile = [&](bool si) {
        std::sort(speeds.begin(), speeds.end(), [&](const Speed &a, const Speed &b) {
            return si ? a.si < b.si : a.scene < b.scene;
        });
        double area = 0;
        for (const auto &speed : speeds) {
            area += speed.area;
            if (area >= .9 * out.wetArea)
                return si ? speed.si : speed.scene;
        }
        return 0.;
    };
    out.referenceSpeed = percentile(false);
    out.referenceSpeedSI = percentile(true);
    double weight = 0;
    for (size_t i = 0; i < out.triangles.size(); ++i) {
        const double relative =
            out.referenceSpeed > 0 ? std::min(sceneSpeeds[i] / out.referenceSpeed, 1.) : 0;
        weight += areas[i] * (.4 + .6 * relative);
        out.triangles[i].cumulativeWeight = weight;
    }
    return out;
}
std::pair<QPointF, int> FlowParticleSeedDistribution::sample(quint64 seed) const
{
    if (triangles.empty())
        return {{}, -1};
    const auto random = [](quint64 bits) {
        bits = (bits ^ (bits >> 30)) * 0xbf58476d1ce4e5b9ULL;
        bits = (bits ^ (bits >> 27)) * 0x94d049bb133111ebULL;
        return std::clamp(double((bits ^ (bits >> 31)) >> 11) / 9007199254740992., 1e-12,
                          1. - 1e-12);
    };
    const double choice = random(seed + 0x9e3779b97f4a7c15ULL) * triangles.back().cumulativeWeight;
    const auto it = std::lower_bound(triangles.begin(), triangles.end(), choice,
                                     [](const FlowParticleSeedTriangle &t, double weight) {
                                         return t.cumulativeWeight < weight;
                                     });
    const auto &points = it->points;
    const double a = std::sqrt(random(seed + 0x94d049bb133111ebULL)),
                 b = random(seed + 0xbf58476d1ce4e5b9ULL);
    return {points[0] * (1 - a) + points[1] * (a * (1 - b)) + points[2] * (a * b), it->cell};
}
double FlowParticleSeedDistribution::motionGain(double scale, double pixelsPerSecond,
                                                double floorSI) const
{
    if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(pixelsPerSecond) ||
        pixelsPerSecond < 0 || referenceSpeed <= 0)
        return 0;
    const double floor =
        referenceSpeedSI > 0 ? std::max(0., floorSI) * referenceSpeed / referenceSpeedSI : 0;
    return pixelsPerSecond / (scale * std::max(referenceSpeed, floor));
}
int FlowParticleSeedDistribution::population(double scale, double spacing, int capacity) const
{
    if (wetArea <= 0 || !std::isfinite(scale) || scale <= 0 || !std::isfinite(spacing) ||
        spacing <= 0 || capacity <= 0)
        return 0;
    return int(std::clamp(wetArea * scale * scale / (spacing * spacing),
                          std::min(50., double(capacity)), double(capacity)));
}
double flowParticleStagnationFloor(std::vector<float> envelope)
{
    std::erase_if(envelope, [](float speed) { return !std::isfinite(speed) || speed <= 0; });
    if (envelope.empty())
        return 0;
    // A single transient/dry-cell extreme must not suppress an entire run.
    const auto index = size_t(std::ceil(.99 * envelope.size())) - 1;
    std::nth_element(envelope.begin(), envelope.begin() + index, envelope.end());
    return .01 * envelope[index];
}
} // namespace OpenSWMM::Render
