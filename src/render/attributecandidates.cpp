/*!
 * \file   attributecandidates.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "render/attributecandidates.h"

namespace OpenSWMM::Render::AttributeCandidates {

QStringList modelLayerNumeric(SWMMModelLayer::Category cat)
{
    switch (cat) {
    case SWMMModelLayer::CatJunctions:
        return {QStringLiteral("invertElev"), QStringLiteral("maxDepth"),
                QStringLiteral("initialDepth"), QStringLiteral("surchargeDepth"),
                QStringLiteral("pondedArea")};
    case SWMMModelLayer::CatOutfalls:
        return {QStringLiteral("invertElev"), QStringLiteral("fixedStage")};
    case SWMMModelLayer::CatStorage:
        return {QStringLiteral("invertElev"), QStringLiteral("maxDepth"),
                QStringLiteral("initialDepth"), QStringLiteral("evapFactor"),
                QStringLiteral("infiltrationA"), QStringLiteral("infiltrationB"),
                QStringLiteral("infiltrationC")};
    case SWMMModelLayer::CatDividers:
        return {QStringLiteral("invertElev"), QStringLiteral("maxDepth"),
                QStringLiteral("initialDepth")};
    case SWMMModelLayer::CatConduits:
        return {QStringLiteral("length"), QStringLiteral("geom1"),
                QStringLiteral("geom2"), QStringLiteral("geom3"),
                QStringLiteral("geom4"), QStringLiteral("roughness"),
                QStringLiteral("inletOffset"), QStringLiteral("outletOffset")};
    case SWMMModelLayer::CatPumps:
        return {QStringLiteral("startupDepth"), QStringLiteral("shutoffDepth")};
    case SWMMModelLayer::CatOrifices:
        return {QStringLiteral("offset"), QStringLiteral("dischargeCoeff")};
    case SWMMModelLayer::CatWeirs:
        return {QStringLiteral("offset"), QStringLiteral("dischargeCoeff"),
                QStringLiteral("crestHeight")};
    case SWMMModelLayer::CatOutlets:
        return {QStringLiteral("offset"), QStringLiteral("q1"),
                QStringLiteral("q2")};
    case SWMMModelLayer::CatSubcatchments:
        return {QStringLiteral("area"), QStringLiteral("width"),
                QStringLiteral("slope"), QStringLiteral("imperviousPct"),
                QStringLiteral("nImp"), QStringLiteral("nPerv"),
                QStringLiteral("sImp"), QStringLiteral("sPerv")};
    case SWMMModelLayer::CatRainGages:
        return {};
    case SWMMModelLayer::NumCategories:
        break;
    }
    return {};
}

QStringList modelLayerString(SWMMModelLayer::Category cat)
{
    switch (cat) {
    case SWMMModelLayer::CatJunctions:
    case SWMMModelLayer::CatStorage:
    case SWMMModelLayer::CatDividers:
        return {QStringLiteral("tag"), QStringLiteral("group")};
    case SWMMModelLayer::CatOutfalls:
        return {QStringLiteral("tag"), QStringLiteral("type"),
                QStringLiteral("group")};
    case SWMMModelLayer::CatConduits:
    case SWMMModelLayer::CatPumps:
    case SWMMModelLayer::CatOrifices:
    case SWMMModelLayer::CatWeirs:
    case SWMMModelLayer::CatOutlets:
        return {QStringLiteral("tag"), QStringLiteral("status"),
                QStringLiteral("group")};
    case SWMMModelLayer::CatSubcatchments:
        return {QStringLiteral("tag"), QStringLiteral("raingage"),
                QStringLiteral("outlet"), QStringLiteral("group")};
    case SWMMModelLayer::CatRainGages:
        return {QStringLiteral("tag"), QStringLiteral("dataSource")};
    case SWMMModelLayer::NumCategories:
        break;
    }
    return {};
}

QStringList resultsLayerNumeric(int kindOrdinal)
{
    if (kindOrdinal < 0) {
        return {QStringLiteral("NodeDepth"),    QStringLiteral("NodeHead"),
                QStringLiteral("NodeVolume"),   QStringLiteral("NodeInflow"),
                QStringLiteral("NodeOverflow"), QStringLiteral("NodeLateralInflow"),
                QStringLiteral("LinkFlow"),     QStringLiteral("LinkDepth"),
                QStringLiteral("LinkVelocity"), QStringLiteral("LinkCapacity"),
                QStringLiteral("SubcatchRunoff"), QStringLiteral("SubcatchInfiltration"),
                QStringLiteral("SubcatchEvaporation"), QStringLiteral("SubcatchSnowDepth")};
    }
    const auto cat = static_cast<SWMMModelLayer::Category>(kindOrdinal);
    switch (cat) {
    case SWMMModelLayer::CatJunctions:
    case SWMMModelLayer::CatOutfalls:
    case SWMMModelLayer::CatStorage:
    case SWMMModelLayer::CatDividers:
        return {QStringLiteral("NodeDepth"),  QStringLiteral("NodeHead"),
                QStringLiteral("NodeVolume"), QStringLiteral("NodeInflow"),
                QStringLiteral("NodeOverflow"), QStringLiteral("NodeLateralInflow")};
    case SWMMModelLayer::CatConduits:
    case SWMMModelLayer::CatPumps:
    case SWMMModelLayer::CatOrifices:
    case SWMMModelLayer::CatWeirs:
    case SWMMModelLayer::CatOutlets:
        return {QStringLiteral("LinkFlow"),     QStringLiteral("LinkDepth"),
                QStringLiteral("LinkVelocity"), QStringLiteral("LinkCapacity")};
    case SWMMModelLayer::CatSubcatchments:
        return {QStringLiteral("SubcatchRunoff"), QStringLiteral("SubcatchInfiltration"),
                QStringLiteral("SubcatchEvaporation"), QStringLiteral("SubcatchSnowDepth")};
    case SWMMModelLayer::CatRainGages:
    case SWMMModelLayer::NumCategories:
        return {};
    }
    return {};
}

} // namespace OpenSWMM::Render::AttributeCandidates
