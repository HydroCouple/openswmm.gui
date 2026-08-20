/*!
 * \file   meshbcrenderingprefs.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Q_PROPERTY facade over PreferencesManager's 2D-mesh BC edge
 *         defaults. See the header for the defaults-only contract.
 */
#include "core/meshbcrenderingprefs.h"

#include "core/preferencesmanager.h"

namespace {

//! mesh::MeshBCTypes::Type values, spelled out so the accessors below read
//! as the BC they serve rather than as array arithmetic.
enum : int {
    kWall = 0, kNormalFlow = 1, kStageConst = 2, kStageTS = 3,
    kFlowConst = 4, kFlowTS = 5, kRatingCurve = 6,
};

} // namespace

MeshBcRenderingPrefs::MeshBcRenderingPrefs(QObject *parent) : QObject(parent) {}

bool MeshBcRenderingPrefs::colorByType() const
{
    return PreferencesManager::instance()->meshBcColorByType();
}

void MeshBcRenderingPrefs::setColorByType(bool on)
{
    if (on == colorByType()) return;
    PreferencesManager::instance()->setMeshBcColorByType(on);
    emit colorByTypeChanged(on);
}

// ---- colours ---------------------------------------------------------------

#define OSV_BC_COLOR(Getter, Setter, Type)                                     \
    QColor MeshBcRenderingPrefs::Getter() const                                \
    { return PreferencesManager::instance()->meshBcColor(Type); }              \
    void MeshBcRenderingPrefs::Setter(const QColor &c)                         \
    {                                                                          \
        if (c == Getter()) return;                                             \
        PreferencesManager::instance()->setMeshBcColor(Type, c);               \
        emit changed();                                                        \
    }

OSV_BC_COLOR(wallColor,        setWallColor,        kWall)
OSV_BC_COLOR(normalFlowColor,  setNormalFlowColor,  kNormalFlow)
OSV_BC_COLOR(stageConstColor,  setStageConstColor,  kStageConst)
OSV_BC_COLOR(stageSeriesColor, setStageSeriesColor, kStageTS)
OSV_BC_COLOR(flowConstColor,   setFlowConstColor,   kFlowConst)
OSV_BC_COLOR(flowSeriesColor,  setFlowSeriesColor,  kFlowTS)
OSV_BC_COLOR(ratingCurveColor, setRatingCurveColor, kRatingCurve)

#undef OSV_BC_COLOR

// ---- widths ----------------------------------------------------------------

#define OSV_BC_WIDTH(Getter, Setter, Type)                                     \
    double MeshBcRenderingPrefs::Getter() const                                \
    { return PreferencesManager::instance()->meshBcWidthPx(Type); }            \
    void MeshBcRenderingPrefs::Setter(double px)                               \
    {                                                                          \
        if (qFuzzyCompare(px + 1.0, Getter() + 1.0)) return;                   \
        PreferencesManager::instance()->setMeshBcWidthPx(Type, px);            \
        emit changed();                                                        \
    }

OSV_BC_WIDTH(normalFlowWidthPx,  setNormalFlowWidthPx,  kNormalFlow)
OSV_BC_WIDTH(stageConstWidthPx,  setStageConstWidthPx,  kStageConst)
OSV_BC_WIDTH(stageSeriesWidthPx, setStageSeriesWidthPx, kStageTS)
OSV_BC_WIDTH(flowConstWidthPx,   setFlowConstWidthPx,   kFlowConst)
OSV_BC_WIDTH(flowSeriesWidthPx,  setFlowSeriesWidthPx,  kFlowTS)
OSV_BC_WIDTH(ratingCurveWidthPx, setRatingCurveWidthPx, kRatingCurve)

#undef OSV_BC_WIDTH
