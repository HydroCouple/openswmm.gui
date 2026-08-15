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

/*! Material pattern per layer — what the layer is MADE OF, which is the thing
 *  a colour band alone cannot convey (and which survives greyscale printing).
 *  Permeable pavement gets pavers rather than porous asphalt because that is
 *  what the [LID_CONTROLS] pavement layer is drawn as in the SWMM manual. */
DiagramTexture textureFor(LidLayer layer, LidType type)
{
    switch (layer) {
    case LidLayer::Surface:
        // The surface layer is ponding volume above whatever is beneath it, so
        // it carries no material of its own — the planting does the work.
        return DiagramTexture::None;
    case LidLayer::Pavement:
        return (type == LidType::PermPavement) ? DiagramTexture::Brick
                                               : DiagramTexture::Aggregate;
    case LidLayer::Soil:
        return DiagramTexture::Stipple;
    case LidLayer::Storage:
        return DiagramTexture::Gravel;
    case LidLayer::Drainmat:
        return DiagramTexture::Lattice;
    }
    return DiagramTexture::None;
}

/*! How the surface of each LID type is planted. */
struct SurfaceCover
{
    bool planted = false;   //!< Draw vegetation at all.
    bool grass   = false;   //!< Turf tufts rather than shrubs.
    int  density = 9;       //!< Plants across the cell.
};

SurfaceCover coverFor(LidType type)
{
    switch (type) {
    case LidType::BioCell:        return { true,  false, 7 };
    case LidType::RainGarden:     return { true,  false, 9 };
    case LidType::GreenRoof:      return { true,  true, 16 };
    case LidType::VegSwale:       return { true,  true, 14 };
    case LidType::RooftopDisconn: return { true,  true, 12 };  // lawn it drains to
    case LidType::InfilTrench:    return { false, false, 0 };  // stone-filled
    case LidType::PermPavement:   return { false, false, 0 };
    case LidType::RainBarrel:     return { false, false, 0 };
    }
    return {};
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

/*!
 * Per-type illustration drawn around the layer stack.
 *
 * The layer stack alone cannot distinguish several of the eight types — a
 * bioretention cell and an infiltration trench differ mainly in what sits on
 * top, and a rain barrel is not a stratum at all. These ornaments are what make
 * the drawing say WHICH control is being edited.
 */
void appendTypeOrnaments(SectionDiagramModel &m, const LidDiagramInput &in,
                         double halfW, double stackTop, double stackBottom,
                         double nativeH, double refTotal)
{
    const double span = stackTop - stackBottom;

    switch (in.type) {
    case LidType::RainBarrel: {
        // A barrel is a vessel standing on the ground, not a soil profile:
        // draw its shell around the storage layer, with a downspout feeding it
        // and a lid on top.
        DiagramPoly shell;
        shell.role = DiagramRole::Structure;
        const double wall = halfW * 0.09;
        shell.pts << QPointF(-halfW - wall, stackTop + refTotal * 0.05)
                  << QPointF( halfW + wall, stackTop + refTotal * 0.05)
                  << QPointF( halfW + wall, stackBottom)
                  << QPointF(-halfW - wall, stackBottom);
        m.polys << shell;

        m.polylines << DiagramPolyline{
            QPolygonF({ QPointF(-halfW - wall, stackTop + refTotal * 0.05),
                        QPointF( halfW + wall, stackTop + refTotal * 0.05) }),
            DiagramRole::Structure, false, tr_("lid") };

        // Downspout from the roof into the barrel.
        m.polylines << DiagramPolyline{
            QPolygonF({ QPointF(-halfW * 0.55, stackTop + refTotal * 0.55),
                        QPointF(-halfW * 0.55, stackTop + refTotal * 0.08) }),
            DiagramRole::Structure, false, QString() };
        m.arrows << DiagramArrow{
            QPointF(-halfW * 0.55, stackTop + refTotal * 0.30),
            QPointF(-halfW * 0.55, stackTop + refTotal * 0.10),
            tr_("roof leader"), DiagramRole::Accent };
        break;
    }

    case LidType::GreenRoof: {
        // Roof deck under the drainage mat, with a parapet — the thing that
        // makes a green roof a roof rather than a shallow rain garden.
        DiagramPoly deck;
        deck.role = DiagramRole::Structure;
        deck.pts << QPointF(-halfW * 1.08, stackBottom)
                 << QPointF( halfW * 1.08, stackBottom)
                 << QPointF( halfW * 1.08, stackBottom - nativeH * 0.45)
                 << QPointF(-halfW * 1.08, stackBottom - nativeH * 0.45);
        deck.insetLabel = tr_("Roof deck");
        m.polys << deck;

        for (double sx : { -1.0, 1.0 }) {
            DiagramPoly parapet;
            parapet.role = DiagramRole::Structure;
            const double x0 = sx * halfW * 1.08, x1 = sx * halfW * 0.98;
            parapet.pts << QPointF(x0, stackTop + span * 0.28)
                        << QPointF(x1, stackTop + span * 0.28)
                        << QPointF(x1, stackBottom)
                        << QPointF(x0, stackBottom);
            m.polys << parapet;
        }
        break;
    }

    case LidType::PermPavement: {
        // Traffic on top: the surface is a driving course, so show the wearing
        // surface line and a vehicle load arrow instead of ponding.
        m.polylines << DiagramPolyline{
            QPolygonF({ QPointF(-halfW * 1.06, stackTop),
                        QPointF( halfW * 1.06, stackTop) }),
            DiagramRole::Structure, false, QString() };
        m.arrows << DiagramArrow{
            QPointF(0.0, stackTop + refTotal * 0.22),
            QPointF(0.0, stackTop + refTotal * 0.03),
            tr_("rainfall on pavement"), DiagramRole::Accent };
        break;
    }

    case LidType::VegSwale: {
        // A swale is a channel, not a stack: overlay the trapezoidal section on
        // the surface layer so the shape the runoff actually sees is visible.
        const double lip = stackTop;
        const double inv = stackTop - span * 0.55;
        DiagramPoly channel;
        channel.role = DiagramRole::Water;
        channel.pts << QPointF(-halfW * 0.95, lip)
                    << QPointF(-halfW * 0.30, inv)
                    << QPointF( halfW * 0.30, inv)
                    << QPointF( halfW * 0.95, lip);
        m.polys << channel;
        m.polylines << DiagramPolyline{
            QPolygonF({ QPointF(-halfW * 0.95, lip),
                        QPointF(-halfW * 0.30, inv),
                        QPointF( halfW * 0.30, inv),
                        QPointF( halfW * 0.95, lip) }),
            DiagramRole::Vegetation, false, QString() };
        m.leaders << DiagramLeader{ QPointF(0.0, inv),
                                    tr_("swale invert"), QPointF(28.0, 14.0) };
        break;
    }

    case LidType::RooftopDisconn: {
        // Roof + downspout discharging to the lawn it is disconnected onto.
        m.polylines << DiagramPolyline{
            QPolygonF({ QPointF(-halfW * 1.15, stackTop + refTotal * 0.85),
                        QPointF(-halfW * 0.25, stackTop + refTotal * 0.55) }),
            DiagramRole::Structure, false, tr_("roof") };
        m.polylines << DiagramPolyline{
            QPolygonF({ QPointF(-halfW * 0.25, stackTop + refTotal * 0.55),
                        QPointF(-halfW * 0.25, stackTop + refTotal * 0.10) }),
            DiagramRole::Structure, false, QString() };
        m.arrows << DiagramArrow{
            QPointF(-halfW * 0.25, stackTop + refTotal * 0.16),
            QPointF(-halfW * 0.10, stackTop + refTotal * 0.02),
            tr_("disconnected downspout"), DiagramRole::Accent };
        break;
    }

    case LidType::InfilTrench: {
        // Stone-filled trench: no planting, but a geotextile wrap, which is the
        // detail that distinguishes it from a bare gravel pit.
        m.polylines << DiagramPolyline{
            QPolygonF({ QPointF(-halfW * 1.03, stackTop),
                        QPointF(-halfW * 1.03, stackBottom),
                        QPointF( halfW * 1.03, stackBottom),
                        QPointF( halfW * 1.03, stackTop) }),
            DiagramRole::Muted, true, QString() };
        m.leaders << DiagramLeader{ QPointF(halfW * 1.03, stackBottom),
                                    tr_("geotextile wrap"), QPointF(26.0, 12.0) };
        break;
    }

    case LidType::BioCell:
    case LidType::RainGarden:
        // The layer stack plus planting already reads correctly for these.
        break;
    }
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
        box.texture = textureFor(layer, in.type);
        box.unknown = unknown;
        box.pts << QPointF(-kHalfWidth, tops.at(i))
                << QPointF( kHalfWidth, tops.at(i))
                << QPointF( kHalfWidth, bottoms.at(i))
                << QPointF(-kHalfWidth, bottoms.at(i));
        box.insetLabel = lidLayerName(layer);
        m.polys << box;

        // Ponded water sitting in the surface layer, to the berm height. The
        // surface layer IS the ponding volume, so showing it filled is what
        // makes "storage depth" mean something visually.
        if (layer == LidLayer::Surface && !unknown && f.thickness > 0.0) {
            const double pond = bottoms.at(i) + (tops.at(i) - bottoms.at(i)) * 0.45;
            DiagramPoly water;
            water.role = DiagramRole::Water;
            water.pts << QPointF(-kHalfWidth, pond)
                      << QPointF( kHalfWidth, pond)
                      << QPointF( kHalfWidth, bottoms.at(i))
                      << QPointF(-kHalfWidth, bottoms.at(i));
            m.polys << water;
            m.polylines << DiagramPolyline{
                QPolygonF({ QPointF(-kHalfWidth, tops.at(i)),
                            QPointF( kHalfWidth, tops.at(i)) }),
                DiagramRole::Accent, true, tr_("berm") };
        }

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
    const double nativeH = refTotal * 0.18;
    DiagramPoly native;
    native.role    = DiagramRole::Soil;
    native.texture = DiagramTexture::Hatch;
    native.pts << QPointF(-kHalfWidth, stackBottom)
               << QPointF( kHalfWidth, stackBottom)
               << QPointF( kHalfWidth, stackBottom - nativeH)
               << QPointF(-kHalfWidth, stackBottom - nativeH);
    native.insetLabel = tr_("Native soil");
    m.polys << native;

    // ---- Planting ----------------------------------------------------------
    const SurfaceCover cover = coverFor(in.type);
    if (cover.planted && layers.first() == LidLayer::Surface) {
        m.vegetation << DiagramVegetation{
            -kHalfWidth * 0.92, kHalfWidth * 0.92, tops.first(),
            refTotal * (cover.grass ? 0.10 : 0.20), cover.density, cover.grass };
    }

    // ---- Underdrain --------------------------------------------------------
    if (lidHasDrain(in.type)) {
        // Drawn as a perforated pipe in section rather than a line: the offset
        // is measured to the pipe, and a pipe is what the user is specifying.
        const double drainY = stackBottom
            + std::max(in.drainOffset, refTotal * 0.05);
        m.circles << DiagramCircle{ QPointF(0.0, drainY), refTotal * 0.045,
                                    DiagramRole::Conduit, /*perforated=*/true };
        m.leaders << DiagramLeader{
            QPointF(refTotal * 0.045, drainY),
            tr_("underdrain  C %1 · n %2 · offset %3 %4")
                .arg(num(in.drainCoeff, 2), num(in.drainExponent, 2),
                     num(in.drainOffset, 2), in.lengthLabel),
            QPointF(26.0, 18.0) };
        if (in.drainOffset > 0.0) {
            DiagramDim d;
            d.from = QPointF(-kHalfWidth * 0.45, stackBottom);
            d.to   = QPointF(-kHalfWidth * 0.45, drainY);
            d.text = tr_("offset %1 %2").arg(num(in.drainOffset, 2), in.lengthLabel);
            d.pixelOffset = -18.0;
            m.dims << d;
        }
    }

    // ---- Flow paths --------------------------------------------------------
    // Runoff in at the surface, infiltration out through the native soil: the
    // two boundary fluxes that decide whether the control works at all.
    if (layers.first() == LidLayer::Surface) {
        m.arrows << DiagramArrow{
            QPointF(-kHalfWidth * 1.45, tops.first() + refTotal * 0.16),
            QPointF(-kHalfWidth * 0.98, tops.first()),
            tr_("runoff in"), DiagramRole::Accent };
    }
    for (double x : { -0.45, 0.15 }) {
        m.arrows << DiagramArrow{
            QPointF(kHalfWidth * x, stackBottom - nativeH * 0.15),
            QPointF(kHalfWidth * x, stackBottom - nativeH * 0.85),
            QString(), DiagramRole::Muted };
    }
    m.leaders << DiagramLeader{
        QPointF(kHalfWidth * 0.15, stackBottom - nativeH * 0.85),
        tr_("infiltration"), QPointF(26.0, 8.0) };

    // ---- Type-specific illustration ---------------------------------------
    appendTypeOrnaments(m, in, kHalfWidth, tops.first(), stackBottom,
                        nativeH, refTotal);

    m.footer = tr_("%1 layer(s)%2 · thicknesses in %3")
                   .arg(static_cast<int>(layers.size()))
                   .arg(lidHasDrain(in.type) ? tr_(" + underdrain") : QString(),
                        in.lengthLabel);
    return m;
}

} // namespace openswmmvis::sectionview
