/*!
 * \file   raingageassignment.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Spatial rain-gage assignment for subcatchments and RDII inflows, split into
 * three steps so the expensive middle one can run off the GUI thread:
 *
 *   1. gatherInput()  — GUI thread. Snapshots everything the plan needs from
 *                        the engine: gage sites, subcatchment rings, RDII
 *                        entries and their unit-hydrograph groups, source
 *                        rainfall series, and what earlier runs generated.
 *   2. computePlan()  — any thread; pure. Thiessen area majority (nearest) or
 *                        natural-neighbour weights (interpolated), clustering,
 *                        blending, idempotent reuse of earlier output, and the
 *                        unit-hydrograph group splits RDII needs.
 *   3. makeApplyCommand() — GUI thread. One BulkEditCommand holding every
 *                        mutation, so the whole run is one undo step and one
 *                        view refresh.
 *
 * RDII: SWMM binds the rain gage to the UNIT HYDROGRAPH GROUP, not the node,
 * and nodes may share a group. When a group's nodes need different gages the
 * group is split: it keeps the gage carrying the most sewer area and a copy,
 * named <group>_<gage>, takes each other gage. A copy that already serves that
 * gage is reused, so a re-run changes nothing
 * (workplans/RAINGAGE_ASSIGNMENT_PERF_AND_RDII_PLAN_2026-10-02.md).
 */
#ifndef OPENSWMMVIS_ASSIGNMENT_RAINGAGEASSIGNMENT_H
#define OPENSWMMVIS_ASSIGNMENT_RAINGAGEASSIGNMENT_H

#include "core/gageblend.h"

#include <QMap>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>

class MapCanvas;
class QUndoCommand;
class SWMMModelLayer;

namespace openswmmvis::assignment::raingage {

enum class Method { Nearest, Interpolated };

struct Options
{
    Method method        = Method::Nearest;
    bool   laplace       = false;   ///< Interpolated: Laplace instead of Sibson.
    double tolerance     = 0.01;    ///< Interpolated: cluster agreement (fraction).
    bool   subcatchments = true;    ///< Assign subcatchment gages.
    bool   rdii          = false;   ///< Assign RDII unit-hydrograph group gages.
    bool   selectedOnly  = false;   ///< Restrict to the names below.
    QStringList selectedSubcatchments;
    QStringList selectedNodes;      ///< RDII scope when selectedOnly.
};

/*! \brief A located, distinct rain gage usable as a site. */
struct GageSite
{
    int     index = -1;   ///< Engine gage index.
    QString name;
    QPointF pos;          ///< Engine coordinate, never the display cache.
};

struct SubcatchInput
{
    QString          name;
    QString          oldGage;
    QVector<QPointF> ring;    ///< Empty when the subcatchment has no polygon.
};

/*! \brief One [RDII] entry. */
struct RdiiInput
{
    QString node;
    QString group;
    double  area    = 0.0;
    QPointF pos;
    bool    inScope = true;   ///< False: kept on its group's current gage.
};

/*! \brief A unit-hydrograph group: its gage and every row a copy needs. */
struct UhGroup
{
    struct Row   { int month = -1, response = 0; double r = 0, t = 0, k = 0,
                   dmax = 0, drecov = 0, dinit = 0; };
    struct Decay { int response = 0; double kDep = 0, k0 = 0, kT = 0, tRef = 0,
                   thetaRec = 0, tFreeze = 0; int snowOn = 0;
                   double snowT = 0, snowDdf = 0; };
    QString        gage;
    QVector<Row>   rows;
    QVector<Decay> decay;
};

struct Input
{
    Options                        options;
    QVector<GageSite>              gages;
    QStringList                    unlocated;   ///< Gages at (0,0) — excluded.
    QStringList                    coincident;  ///< Duplicates — excluded.
    QVector<SubcatchInput>         subcatchments;
    QVector<RdiiInput>             rdii;
    QMap<QString, UhGroup>         groups;      ///< Every group in the model.
    QVector<GageBlend::SourceGage> sources;     ///< Interpolated only; per gage.
    double                         scfMin = 1.0, scfMax = 1.0;
    /*! Generated (NNG_###) gage -> members that use it now: subcatchment
     *  names, "rdii:<node>", and "uh:<group>" for a group no node uses. */
    QMap<QString, QStringList>     existingClaims;
    QSet<QString>                  gageNames;   ///< Every gage in the model.
    QString                        error;       ///< Non-empty aborts the run.
};

struct RowPlan
{
    enum class Kind { Subcatchment, Rdii };
    Kind    kind = Kind::Subcatchment;
    QString object;      ///< Subcatchment or node name.
    QString oldGage;
    QString newGage;
    QString oldGroup;    ///< RDII only.
    QString newGroup;    ///< RDII only.
    QString detail;
    bool    changed = false;
};

struct GeneratedGage
{
    QString                         gageName;
    QString                         seriesName;
    QString                         key;          ///< Canonical weight key.
    QVector<GageBlend::SeriesPoint> points;
    qint64                          intervalSec = 0;
    double                          snowFactor  = 1.0;
    double                          relError    = 0.0;
    QStringList                     members;      ///< Sorted claim members.
    bool                            isNew    = false;
    bool                            isUpdate = false;
};

/*! \brief A unit-hydrograph group to create as a copy of \ref source. */
struct NewGroup
{
    QString name;
    QString source;
    QString gage;
};

struct Plan
{
    QVector<RowPlan>       rows;
    QVector<GeneratedGage> generated;
    QStringList            staleGages;   ///< Generated gages now unused.
    QMap<QString, QString> groupGages;   ///< Existing group -> new gage (changes only).
    QVector<NewGroup>      newGroups;
    QStringList            warnings;
    QString                error;        ///< Non-empty aborts everything.
    QPointF                parkAt;       ///< Where generated gages are placed.
    int                    scanned = 0;
    int                    skipped = 0;
    int                    changed = 0;
    int                    splitGroups = 0;
};

/*! \brief Shared progress / cancellation for computePlan(). */
struct Progress
{
    std::atomic<int>  done{0};
    std::atomic<int>  total{0};
    std::atomic<bool> cancel{false};
};

/*! \brief Located, distinct gages; fills \p unlocated / \p coincident. */
[[nodiscard]] QVector<GageSite> eligibleGages(SWMMModelLayer *layer,
                                              QStringList *unlocated,
                                              QStringList *coincident);

/*! \brief Step 1 (GUI thread). Also refreshes rain files, as before. */
[[nodiscard]] Input gatherInput(SWMMModelLayer *layer, const Options &options);

/*! \brief Step 2 (any thread). \p progress may be null. A cancelled run
 *  returns a plan whose error says so. */
[[nodiscard]] Plan computePlan(const Input &input, Progress *progress = nullptr);

/*! \brief Step 3 (GUI thread). The returned command has NOT been run: push it
 *  onto an undo stack (which runs it), or call redo() and delete it. Null when
 *  the plan changes nothing. */
[[nodiscard]] QUndoCommand *makeApplyCommand(SWMMModelLayer *layer, MapCanvas *canvas,
                                             const Plan &plan, const QString &text);

} // namespace openswmmvis::assignment::raingage

#endif // OPENSWMMVIS_ASSIGNMENT_RAINGAGEASSIGNMENT_H
