/*!
 * \file   aquiferdiagram.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/sectionview/aquiferdiagram.h"

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

// SWMM_AquiferParam codes (openswmm_subcatchments.h), spelled out so the
// callouts below read as the parameters they annotate.
constexpr int kPorosity       = 0;
constexpr int kWiltingPoint   = 1;
constexpr int kFieldCapacity  = 2;
constexpr int kConductivity   = 3;
constexpr int kConductSlope   = 4;
constexpr int kTensionSlope   = 5;
constexpr int kUpperEvapFrac  = 6;
constexpr int kLowerEvapDepth = 7;
constexpr int kLowerLossCoeff = 8;
constexpr int kBottomElev     = 9;
constexpr int kWaterTableElev = 10;
constexpr int kUpperMoisture  = 11;

//! Floor on the drawn saturated-zone height, as a fraction of the profile,
//! so a thin aquifer under a deep unsaturated zone stays visible.
constexpr double kMinZoneFraction = 0.08;

} // namespace

QString aquiferActiveCalloutPrefix()
{
    return QStringLiteral("▶ ");
}

QString aquiferWarningCalloutPrefix()
{
    return QStringLiteral("⚠ ");
}

SectionDiagramModel buildAquiferDiagram(const AquiferDiagramInput &in)
{
    SectionDiagramModel m;
    m.uniformScale = false;   // depths vs a nominal plan width.

    m.title    = in.name.isEmpty() ? tr_("Aquifer") : in.name;
    m.subtitle = tr_("Two-zone groundwater");

    const QString &L = in.lengthLabel;
    const QString &R = in.rateLabel;

    // ---- Vertical layout (model y grows upward; aquifer bottom at 0) -------
    // The saturated zone is the one thickness the engine knows (Egw − Ebot).
    // The unsaturated zone reaches up to a ground surface the aquifer does
    // not own, so it is sized to hold ETs and never thinner than a fraction
    // of the saturated zone.
    const bool   wtValid = in.waterTableElev > in.bottomElev;
    const double satH    = wtValid ? (in.waterTableElev - in.bottomElev) : 0.0;
    double unsatH = std::max(in.lowerEvapDepth, satH * 0.35);
    if (!(unsatH > 0.0)) unsatH = 1.0;                      // nothing entered yet
    double satDrawn = wtValid ? satH : unsatH * 0.25;       // hatched slab when unknown
    satDrawn = std::max(satDrawn, (satDrawn + unsatH) * kMinZoneFraction);

    const double wtY    = satDrawn;
    const double surfY  = satDrawn + unsatH;
    const double total  = surfY;
    const double hatchH = total * 0.20;                     // deep ground below Ebot

    constexpr double W = 1.0;                               // plan half-width.

    // ---- Zones -------------------------------------------------------------
    DiagramPoly upper;
    upper.role    = DiagramRole::Soil;
    upper.texture = DiagramTexture::Stipple;
    upper.pts << QPointF(-W, surfY) << QPointF(W, surfY)
              << QPointF( W, wtY)   << QPointF(-W, wtY);
    upper.insetLabel = tr_("Upper (unsaturated) zone");
    m.polys << upper;

    DiagramPoly lower;
    lower.role    = DiagramRole::Water;
    lower.texture = DiagramTexture::Sand;
    lower.unknown = !wtValid;
    lower.pts << QPointF(-W, wtY) << QPointF(W, wtY)
              << QPointF( W, 0.0) << QPointF(-W, 0.0);
    lower.insetLabel = wtValid ? tr_("Lower (saturated) zone")
                               : tr_("Water table not above bottom");
    m.polys << lower;

    DiagramPoly deep;
    deep.role    = DiagramRole::Soil;
    deep.texture = DiagramTexture::Hatch;
    deep.pts << QPointF(-W, 0.0) << QPointF(W, 0.0)
             << QPointF( W, -hatchH) << QPointF(-W, -hatchH);
    m.polys << deep;

    // Water table as a wavy free surface, so it reads as water and not as
    // another layer boundary.
    m.polylines << DiagramPolyline{
        QPolygonF({ QPointF(-W, wtY), QPointF(W, wtY) }),
        DiagramRole::Water, false, QString(), /*wavy=*/true };

    // ---- Ground surface (per subcatchment) --------------------------------
    m.grounds << DiagramGround{ -W * 1.3, W * 1.9, surfY };
    m.vegetation << DiagramVegetation{ -W * 0.9, W * 0.9, surfY,
                                       unsatH * 0.12, 10, /*grass=*/true };
    m.leaders << DiagramLeader{
        QPointF(-W * 0.75, surfY),
        tr_("ground surface (Esurf — per subcatchment)"),
        QPointF(-30.0, -18.0) };

    // ---- Callout helper ----------------------------------------------------
    // One leader per parameter; the focused field's callout is prefixed so it
    // stands out (DiagramLeader carries no accent flag).
    auto callout = [&m, &in](int param, const QPointF &anchor,
                             const QString &text, const QPointF &offset) {
        m.leaders << DiagramLeader{
            anchor,
            (in.activeParam == param) ? aquiferActiveCalloutPrefix() + text : text,
            offset };
    };

    // ---- Surface fluxes ----------------------------------------------------
    // Infiltration in from the surface.
    m.arrows << DiagramArrow{
        QPointF(-W * 0.35, surfY + unsatH * 0.25),
        QPointF(-W * 0.35, surfY - unsatH * 0.10),
        tr_("FI — infiltration"), DiagramRole::Accent };

    // Upper-zone ET, scaled by ETu (and its monthly pattern when set).
    m.arrows << DiagramArrow{
        QPointF(W * 0.15, wtY + unsatH * 0.45),
        QPointF(W * 0.15, surfY + unsatH * 0.25),
        tr_("ETu"), DiagramRole::Accent };
    callout(kUpperEvapFrac, QPointF(W * 0.15, surfY + unsatH * 0.12),
            in.evapPattern.isEmpty()
                ? tr_("ETu — Upper Evap. Fraction %1").arg(num(in.upperEvapFrac))
                : tr_("ETu — Upper Evap. Fraction %1 · pattern %2")
                      .arg(num(in.upperEvapFrac), in.evapPattern),
            QPointF(60.0, -22.0));

    // Lower-zone ET reaches down to ETs below the surface; dimension it from
    // the surface so the depth reads against the zones it crosses.
    const double etsDrawn = (in.lowerEvapDepth > 0.0)
        ? std::min(in.lowerEvapDepth, total) : unsatH * 0.5;
    if (in.lowerEvapDepth > 0.0) {
        m.arrows << DiagramArrow{
            QPointF(W * 0.55, surfY - etsDrawn),
            QPointF(W * 0.55, surfY + unsatH * 0.25),
            tr_("ETs"), DiagramRole::Accent };
    }
    {
        DiagramDim d;
        d.from        = QPointF(W, surfY);
        d.to          = QPointF(W, surfY - etsDrawn);
        d.text        = tr_("ETs — Lower Evap. Depth %1 %2")
                            .arg(num(in.lowerEvapDepth, 2), L);
        d.pixelOffset = -48.0;
        d.accent      = (in.activeParam == kLowerEvapDepth);
        m.dims << d;
    }

    // ---- Upper-zone soil callouts (left edge, top to bottom) --------------
    callout(kPorosity,      QPointF(-W, wtY + unsatH * 0.88),
            tr_("Porosity %1").arg(num(in.porosity)),            QPointF(-30.0, 0.0));
    callout(kWiltingPoint,  QPointF(-W, wtY + unsatH * 0.74),
            tr_("Wilting Point %1").arg(num(in.wiltingPoint)),   QPointF(-30.0, 0.0));
    callout(kFieldCapacity, QPointF(-W, wtY + unsatH * 0.60),
            tr_("Field Capacity %1").arg(num(in.fieldCapacity)), QPointF(-30.0, 0.0));
    callout(kUpperMoisture, QPointF(-W, wtY + unsatH * 0.46),
            tr_("Initial Upper Moisture (Umc) %1").arg(num(in.upperMoisture)),
            QPointF(-30.0, 0.0));

    // ---- Percolation upper → lower with its conductivity callouts ---------
    m.arrows << DiagramArrow{
        QPointF(-W * 0.55, wtY + unsatH * 0.30),
        QPointF(-W * 0.55, wtY - satDrawn * 0.25),
        tr_("percolation"), DiagramRole::Accent };
    callout(kConductivity,  QPointF(-W, wtY + unsatH * 0.30),
            tr_("Ksat — Conductivity %1 %2").arg(num(in.conductivity, 2), R),
            QPointF(-30.0, 0.0));
    callout(kConductSlope,  QPointF(-W, wtY + unsatH * 0.18),
            tr_("Kslope — Conductivity Slope %1").arg(num(in.conductSlope, 2)),
            QPointF(-30.0, 0.0));
    callout(kTensionSlope,  QPointF(-W, wtY + unsatH * 0.06),
            tr_("Tslope — Tension Slope %1").arg(num(in.tensionSlope, 2)),
            QPointF(-30.0, 0.0));

    // ---- Water table + bottom elevations (right edge) ---------------------
    {
        DiagramDim d;
        d.from        = QPointF(W, wtY);
        d.to          = QPointF(W, 0.0);
        d.text        = wtValid
            ? tr_("Egw — Initial Water Table Elev. %1 %2")
                  .arg(num(in.waterTableElev, 2), L)
            : tr_("Egw %1 %2 — not above Ebot")
                  .arg(num(in.waterTableElev, 2), L);
        d.pixelOffset = -22.0;
        d.accent      = (in.activeParam == kWaterTableElev);
        m.dims << d;
    }
    callout(kBottomElev, QPointF(W, 0.0),
            tr_("Ebot — Bottom Elev. %1 %2").arg(num(in.bottomElev, 2), L),
            QPointF(60.0, 16.0));

    // ---- Lateral flow to the receiving node (per subcatchment) ------------
    // A schematic shaft with a cover: the node has no drawable geometry here,
    // and the flow into it is governed by the subcatchment's [GROUNDWATER]
    // coefficients, not by the aquifer.
    const double shaftX0 = W * 1.55, shaftX1 = W * 1.75;
    DiagramPoly shaft;
    shaft.role = DiagramRole::Structure;
    shaft.pts << QPointF(shaftX0, surfY + unsatH * 0.05)
              << QPointF(shaftX1, surfY + unsatH * 0.05)
              << QPointF(shaftX1, satDrawn * 0.15)
              << QPointF(shaftX0, satDrawn * 0.15);
    m.polys << shaft;
    m.symbols << DiagramSymbol{
        QPointF((shaftX0 + shaftX1) * 0.5, surfY + unsatH * 0.05),
        DiagramSymbolKind::ManholeCover, 18.0, false, DiagramRole::Structure };
    m.arrows << DiagramArrow{
        QPointF(W, wtY * 0.5),
        QPointF(shaftX0, wtY * 0.5),
        tr_("lateral GW flow"), DiagramRole::Water };
    m.leaders << DiagramLeader{
        QPointF(shaftX1, surfY + unsatH * 0.05),
        tr_("to receiving node (per subcatchment)"),
        QPointF(30.0, -18.0) };

    // ---- Deep percolation through the bottom ------------------------------
    m.arrows << DiagramArrow{
        QPointF(W * 0.15, satDrawn * 0.20),
        QPointF(W * 0.15, -hatchH * 0.70),
        tr_("deep percolation"), DiagramRole::Muted };
    callout(kLowerLossCoeff, QPointF(W * 0.15, -hatchH * 0.70),
            tr_("Seep — Lower Loss Coefficient %1 %2")
                .arg(num(in.lowerLossCoeff, 3), R),   // typical 0.002 in/hr
            QPointF(60.0, 10.0));

    // ---- Soft-validation warnings (plan D6) -------------------------------
    for (int i = 0; i < in.warnings.size(); ++i) {
        m.leaders << DiagramLeader{
            QPointF(0.0, wtY + unsatH * 0.5),
            aquiferWarningCalloutPrefix() + in.warnings.at(i),
            QPointF(60.0, 30.0 + 16.0 * i) };
    }

    m.footer = wtValid
        ? tr_("saturated thickness %1 %2 · elevations and depths in %2")
              .arg(num(satH, 2), L)
        : tr_("water table not above bottom · elevations and depths in %1").arg(L);
    return m;
}

} // namespace openswmmvis::sectionview
