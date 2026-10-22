// GPL-3.0-or-later

/**
 * @file      flowparticleribbons.h
 * @brief     Ribbon geometry for animated flow particle trails.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include <QPointF>
#include <QRectF>
#include <QColor>
#include <array>
#include <functional>
#include <vector>
#include <algorithm>

namespace OpenSWMM::Render
{
struct FlowParticleFrame;
struct FlowParticleVertex {
    float x, y, nx, ny, age, particle;
    unsigned char r, g, b, a;
};
static_assert(sizeof(FlowParticleVertex) == 28);
// Exact float bits in RGBA8 texels avoid float-texture/backend requirements.
// Previous/current age, life, stop clock, tail age, and previous/current visibility.
using FlowParticleTiming = std::array<std::array<unsigned char, 4>, 8>;
// All fields are copied on the GUI thread. A palette callback must own its
// data; it must never borrow a mutable QObject from the worker thread.
struct FlowParticleAppearance {
    QPointF anchor;
    QRectF viewport;
    QColor color = Qt::white;
    double opacity = 1;
    bool fade = true, taper = true;
    quint64 revision = 0;
    int particleCapacity = 0, historyCapacity = 0;
    std::function<QColor(double)> colorForSpeed;
};
struct FlowParticleRibbon {
    std::vector<FlowParticleVertex> vertices;
    std::vector<FlowParticleTiming> timing;
    std::vector<quint64> particleIds;
    QPointF anchor;
    QRectF viewport;
    QColor color;
    double scale = 0, buildMs = 0;
    quint64 revision = 0, trackedId = 0;
    int particles = 0, history = 0;
};
/** @brief Decode exact IEEE float bits stored in one RGBA8 timing column.
 * @param timing Borrowed eight-column timing packet.
 * @param column Index in [0,8), unchecked; column seven is currently unused.
 * @return Float reconstructed from little-endian byte significance, including
 * special values unchanged. No numerical conversion or validation occurs. */
float flowParticleTimingValue(const FlowParticleTiming &timing, size_t column);
/** @brief Smoothly reveal trails between two and four pixels of spatial extent.
 * @param pixels Finite trail bounding-box diagonal in screen pixels.
 * @return Zero at/below two, one at/above four, cubic smoothstep in between.
 * Input is not explicitly checked for NaN; callers supply finite geometry. */
inline double flowParticleTrailVisibility(double pixels)
{
    const double t = std::clamp((pixels - 2.) / 2., 0., 1.);
    return t * t * (3 - 2 * t);
}
/** @brief Rebuild packed triangle-strip trail vertices and interpolation timing.
 * @param frame Borrowed immutable frame; particles/history contain finite positions/
 * ages and birth IDs in ascending order when previous-frame lookup is needed.
 * Only trails with at least two samples intersecting the viewport are included.
 * @param scale Positive finite pixels per scene unit. Nonpositive values clear
 * geometry; derived sizing/count arithmetic must fit int and allocation sizes.
 * @param trackedId Particle ID highlighted in orange and exempt from palette lookup.
 * @param appearance Immutable copied appearance: anchor/viewport use scene units,
 * color is unpremultiplied and colorForSpeed (if present) receives history speed
 * in m/s. The callback must own thread-safe captured data, never mutable QObjects.
 * An invalid viewport disables view culling. Capacity hints control reserve/high-
 * water growth but do not cap the number of actual visible particles. opacity,
 * fade and taper are not applied by this tessellation routine.
 * @param[in,out] out Owned destination, overwritten with anchor-relative float
 * positions, one-pixel-width normals and per-sample colors/ages. Capacity/high-
 * water counts can exceed actual particles/history; particleIds gives the used
 * timing rows. Unused vertices are padded degenerately; unused timing rows are
 * not meaningful. No caller pointers are retained.
 * @param previous Optional immutable prior frame, distinct from a destination
 * whose storage is being mutated. Matching geometry/scale enables visibility
 * interpolation by birth ID; otherwise prior particle extents can provide a fallback.
 * Call synchronously with stable inputs and exclusive destination access. Allocation
 * and palette exceptions propagate with partially updated output; no rollback or
 * cancellation is provided. buildMs records wall-clock preparation time on success. */
void buildFlowParticleRibbon(const FlowParticleFrame &frame, double scale, quint64 trackedId,
                             const FlowParticleAppearance &appearance, FlowParticleRibbon &out,
                             const FlowParticleFrame *previous = nullptr);
} // namespace OpenSWMM::Render
