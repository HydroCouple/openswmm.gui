/*!
 * \file   lidlayerdiagram.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/sectionview/lidlayerdiagram.h"

#include <QCoreApplication>

#include <algorithm>

namespace openswmmvis::sectionview {

namespace {

inline QString tr_(const char *s)
{
    return QCoreApplication::translate("openswmmvis::sectionview", s);
}

inline QString num(double v, int decimals = 3)
{
    return QString::number(v, 'f', decimals);
}

//! Relative height for a layer whose thickness the engine can't report.
constexpr double kUnknownThickness = 1.0;
//! Floor on a known layer's drawn height, as a fraction of the stack.
constexpr double kMinLayerFraction = 0.06;

DiagramRole roleFor(LidLayer layer)
{
    switch (layer) {
    case LidLayer::Surface:  return DiagramRole::Vegetation;
    case LidLayer::Pavement: return DiagramRole::Structure;
    case LidLayer::Soil:     return DiagramRole::Media;
    case LidLayer::Storage:  return DiagramRole::Gravel;
    case LidLayer::Drainmat: return DiagramRole::Structure;
    }
    return DiagramRole::Muted;
}

/*! Thickness + the parameter summary shown for each layer. */
struct LayerFacts
{
    double  thickness = 0.0;
    QString params;
};

LayerFacts factsFor(LidLayer layer, const LidDiagramInput &in)
{
    const QString &u = in.lengthLabel;
    switch (layer) {
    // The leader carries the parameters the drawing can NOT show; the layer's
    // thickness (berm depth for the surface) is already dimensioned against the
    // right edge of its own block, so repeating it here only made the label too
    // long for the margin and elided the parameters that are unique to it.
    case LidLayer::Surface:
        return { in.surfaceStorage,
                 tr_("n %1 · slope %2 %")
                     .arg(num(in.surfaceRoughness, 3),
                          num(in.surfaceSlope, 2)) };
    case LidLayer::Soil:
        return { in.soilThickness,
                 tr_("porosity %1 · K %2")
                     .arg(num(in.soilPorosity, 3),
                          num(in.soilConductivity, 2)) };
    case LidLayer::Storage:
        return { in.storageThickness,
                 tr_("void %1 · seepage %2")
                     .arg(num(in.storageVoidFrac, 3),
                          num(in.storageSeepage, 2)) };
    case LidLayer::Pavement:
        return { in.pavementThickness,
                 tr_("pavement layer — no editor fields yet") };
    case LidLayer::Drainmat:
        return { in.drainmatThickness,
                 tr_("drainage mat — no editor fields yet") };
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// Type → layer stack
// ---------------------------------------------------------------------------

QVector<LidLayer> lidLayersFor(LidType type)
{
    using L = LidLayer;
    switch (type) {
    case LidType::BioCell:        return { L::Surface, L::Soil, L::Storage };
    case LidType::RainGarden:     return { L::Surface, L::Soil };
    case LidType::GreenRoof:      return { L::Surface, L::Soil, L::Drainmat };
    case LidType::InfilTrench:    return { L::Surface, L::Storage };
    case LidType::PermPavement:   return { L::Surface, L::Pavement,
                                           L::Soil, L::Storage };
    case LidType::RainBarrel:     return { L::Storage };
    case LidType::RooftopDisconn: return { L::Surface };
    case LidType::VegSwale:       return { L::Surface };
    }
    return { L::Surface };
}

bool lidHasDrain(LidType type)
{
    switch (type) {
    case LidType::BioCell:
    case LidType::GreenRoof:
    case LidType::InfilTrench:
    case LidType::PermPavement:
    case LidType::RainBarrel:
    case LidType::RooftopDisconn:
        return true;
    case LidType::RainGarden:
    case LidType::VegSwale:
        return false;
    }
    return false;
}

QString lidLayerName(LidLayer layer)
{
    switch (layer) {
    case LidLayer::Surface:  return tr_("Surface");
    case LidLayer::Pavement: return tr_("Pavement");
    case LidLayer::Soil:     return tr_("Soil");
    case LidLayer::Storage:  return tr_("Storage");
    case LidLayer::Drainmat: return tr_("Drainage Mat");
    }
    return {};
}

// ---------------------------------------------------------------------------
// Diagram
// ---------------------------------------------------------------------------

SectionDiagramModel buildLidLayerDiagram(const LidDiagramInput &in)
{
    SectionDiagramModel m;
    m.uniformScale = false;   // layer thicknesses vs a nominal plan width.

    const QVector<LidLayer> layers = lidLayersFor(in.type);
    m.title    = in.name.isEmpty() ? tr_("LID Control") : in.name;
    m.subtitle = [t = in.type]() {
        switch (t) {
        case LidType::BioCell:        return tr_("Bio-Retention Cell");
        case LidType::RainGarden:     return tr_("Rain Garden");
        case LidType::GreenRoof:      return tr_("Green Roof");
        case LidType::InfilTrench:    return tr_("Infiltration Trench");
        case LidType::PermPavement:   return tr_("Permeable Pavement");
        case LidType::RainBarrel:     return tr_("Rain Barrel");
        case LidType::RooftopDisconn: return tr_("Rooftop Disconnection");
        case LidType::VegSwale:       return tr_("Vegetative Swale");
        }
        return QString();
    }();

    if (layers.isEmpty()) {
        m.emptyText = tr_("No layers for this LID type.");
        return m;
    }

    // Drawn heights: proportional where known, a fixed slab where not, with a
    // floor so a 25 mm surface layer under a 600 mm soil layer stays visible.
    QVector<LayerFacts> facts;
    facts.reserve(layers.size());
    double knownTotal = 0.0;
    for (LidLayer l : layers) {
        facts << factsFor(l, in);
        if (facts.last().thickness > 0.0) knownTotal += facts.last().thickness;
    }
    const double refTotal = (knownTotal > 0.0)
        ? knownTotal
        : kUnknownThickness * static_cast<double>(layers.size());
    const double floorH = refTotal * kMinLayerFraction;

    constexpr double kHalfWidth = 1.0;   // arbitrary plan half-width.

    double y = 0.0;                       // running top-of-stack, downward.
    QVector<double> tops, bottoms;
    tops.reserve(layers.size());
    bottoms.reserve(layers.size());
    for (const LayerFacts &f : facts) {
        const double h = (f.thickness > 0.0)
            ? std::max(f.thickness, floorH)
            : std::max(kUnknownThickness * refTotal / static_cast<double>(layers.size()), floorH);
        tops    << y;
        y      -= h;                      // model y grows up; stack downward.
        bottoms << y;
    }
    const double stackBottom = y;

    // ---- Layer boxes ------------------------------------------------------
    for (int i = 0; i < layers.size(); ++i) {
        const LidLayer  layer = layers.at(i);
        const LayerFacts &f   = facts.at(i);
        const bool unknown    = !(f.thickness > 0.0);

        DiagramPoly box;
        box.role    = roleFor(layer);
        box.unknown = unknown;
        box.pts << QPointF(-kHalfWidth, tops.at(i))
                << QPointF( kHalfWidth, tops.at(i))
                << QPointF( kHalfWidth, bottoms.at(i))
                << QPointF(-kHalfWidth, bottoms.at(i));
        box.insetLabel = lidLayerName(layer);
        m.polys << box;

        // Thickness dimension on the right of every layer.
        DiagramDim d;
        d.from        = QPointF(kHalfWidth, tops.at(i));
        d.to          = QPointF(kHalfWidth, bottoms.at(i));
        d.text        = unknown ? QStringLiteral("—")
                                : QStringLiteral("%1 %2")
                                      .arg(num(f.thickness, 2), in.lengthLabel);
        d.pixelOffset = -22.0;
        d.accent      = in.hasActiveLayer && in.activeLayer == layer;
        m.dims << d;

        // Parameter summary as a leader off the left edge.
        m.leaders << DiagramLeader{
            QPointF(-kHalfWidth, (tops.at(i) + bottoms.at(i)) * 0.5),
            unknown ? tr_("%1 — value not readable from the engine")
                          .arg(lidLayerName(layer))
                    : f.params,
            QPointF(-30.0, 0.0) };
    }

    // ---- Native soil below the stack --------------------------------------
    const double nativeH = refTotal * 0.16;
    DiagramPoly native;
    native.role = DiagramRole::Soil;
    native.pts << QPointF(-kHalfWidth, stackBottom)
               << QPointF( kHalfWidth, stackBottom)
               << QPointF( kHalfWidth, stackBottom - nativeH)
               << QPointF(-kHalfWidth, stackBottom - nativeH);
    native.insetLabel = tr_("Native soil");
    m.polys << native;

    // ---- Underdrain --------------------------------------------------------
    if (lidHasDrain(in.type)) {
        // Sits at the drain offset above the bottom of the lowest layer.
        const double drainY = stackBottom
            + std::max(in.drainOffset, refTotal * 0.04);
        m.polylines << DiagramPolyline{
            QPolygonF({ QPointF(-kHalfWidth * 0.55, drainY),
                        QPointF( kHalfWidth * 0.55, drainY) }),
            DiagramRole::Accent, false, QString() };
        m.leaders << DiagramLeader{
            QPointF(kHalfWidth * 0.55, drainY),
            tr_("underdrain  C %1 · n %2 · offset %3 %4")
                .arg(num(in.drainCoeff, 2), num(in.drainExponent, 2),
                     num(in.drainOffset, 2), in.lengthLabel),
            QPointF(24.0, 20.0) };
    }

    // ---- Inflow arrow (surface-fed types) ---------------------------------
    if (layers.first() == LidLayer::Surface) {
        m.polylines << DiagramPolyline{
            QPolygonF({ QPointF(-kHalfWidth * 1.35, tops.first() + refTotal * 0.10),
                        QPointF(-kHalfWidth * 1.02, tops.first()) }),
            DiagramRole::Accent, false, tr_("runoff") };
    }

    m.footer = tr_("%1 layer(s)%2 · thicknesses in %3")
                   .arg(static_cast<int>(layers.size()))
                   .arg(lidHasDrain(in.type) ? tr_(" + underdrain") : QString(),
                        in.lengthLabel);
    return m;
}

} // namespace openswmmvis::sectionview
