/*!
 * \file   meshbctype.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/meshbctype.h"

#include <QCoreApplication>

namespace mesh {

QString MeshBCTypes::label(Type t)
{
    switch (t) {
    case Type::Wall:                return QCoreApplication::translate("MeshBCTypes", "Wall");
    case Type::NormalFlow:          return QCoreApplication::translate("MeshBCTypes", "Normal Flow");
    case Type::SpecifiedStageConst: return QCoreApplication::translate("MeshBCTypes", "Specified Stage (Constant)");
    case Type::SpecifiedStageTS:    return QCoreApplication::translate("MeshBCTypes", "Specified Stage (Timeseries)");
    case Type::SpecifiedFlowConst:  return QCoreApplication::translate("MeshBCTypes", "Specified Flow (Constant)");
    case Type::SpecifiedFlowTS:     return QCoreApplication::translate("MeshBCTypes", "Specified Flow (Timeseries)");
    case Type::RatingCurve:         return QCoreApplication::translate("MeshBCTypes", "Rating Curve");
    }
    return {};
}

QString MeshBCTypes::inpToken(Type t)
{
    switch (t) {
    case Type::Wall:                return QStringLiteral("WALL");
    case Type::NormalFlow:          return QStringLiteral("NORMAL_FLOW");
    case Type::SpecifiedStageConst: return QStringLiteral("SPECIFIED_STAGE");
    case Type::SpecifiedStageTS:    return QStringLiteral("TS_STAGE");
    case Type::SpecifiedFlowConst:  return QStringLiteral("SPECIFIED_FLOW");
    case Type::SpecifiedFlowTS:     return QStringLiteral("TS_FLOW");
    case Type::RatingCurve:         return QStringLiteral("RATING_CURVE");
    }
    return {};
}

MeshBCTypes::Type MeshBCTypes::fromInpToken(const QString &token, bool *ok)
{
    const QString u = token.trimmed().toUpper();
    auto set = [&](bool v) { if (ok) *ok = v; };
    set(true);
    if (u == QStringLiteral("WALL"))            return Type::Wall;
    if (u == QStringLiteral("NORMAL_FLOW"))     return Type::NormalFlow;
    if (u == QStringLiteral("SPECIFIED_STAGE")) return Type::SpecifiedStageConst;
    if (u == QStringLiteral("TS_STAGE"))        return Type::SpecifiedStageTS;
    if (u == QStringLiteral("SPECIFIED_FLOW"))  return Type::SpecifiedFlowConst;
    if (u == QStringLiteral("TS_FLOW"))         return Type::SpecifiedFlowTS;
    if (u == QStringLiteral("RATING_CURVE"))    return Type::RatingCurve;
    set(false);
    return Type::Wall;
}

int MeshBCTypes::engineBCType(Type t)
{
    switch (t) {
    case Type::Wall:                return 0;  // SWMM_2D_BC_WALL
    case Type::NormalFlow:          return 1;  // SWMM_2D_BC_NORMAL_FLOW
    case Type::SpecifiedStageConst: return 2;  // SWMM_2D_BC_SPECIFIED_STAGE
    case Type::SpecifiedStageTS:    return 2;  // same engine type — TS via name
    case Type::SpecifiedFlowConst:  return 3;  // SWMM_2D_BC_SPECIFIED_FLOW   (V-E4)
    case Type::SpecifiedFlowTS:     return 3;
    case Type::RatingCurve:         return 4;  // SWMM_2D_BC_RATING_CURVE     (V-E5)
    }
    return 0;
}

} // namespace mesh
