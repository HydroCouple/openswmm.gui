/*!
 * \file   meshbctype.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice §V.VA / §V.VC — enumeration of 2D mesh boundary-condition types
 * surfaced by the Mesh Editing toolbar. Order is locked because the int
 * value participates in `.oswp` round-trip and the `[2D_BOUNDARY_CONDITIONS]`
 * INP encoding (TYPE column).
 *
 * Engine vocabulary mapping (per openswmm_2d.h §V.11):
 *   Wall           → SWMM_2D_BC_WALL            (0)
 *   NormalFlow     → SWMM_2D_BC_NORMAL_FLOW     (1)
 *   SpecifiedStage → SWMM_2D_BC_SPECIFIED_STAGE (2)  (constant or timeseries)
 *   SpecifiedFlow  → SWMM_2D_BC_SPECIFIED_FLOW  (3)  (V-E4, constant or timeseries)
 *   RatingCurve    → SWMM_2D_BC_RATING_CURVE    (4)  (V-E5)
 *
 * The (Constant, Timeseries) variants for SpecifiedStage / SpecifiedFlow
 * collapse onto the same engine type — the GUI distinguishes by whether the
 * MeshEdgeBC carries a timeseries name. The toolbar combo surfaces them as
 * separate rows for clarity.
 */
#ifndef OPENSWMMVIS_MESH_MESHBCTYPE_H
#define OPENSWMMVIS_MESH_MESHBCTYPE_H

#include <QObject>
#include <QString>

namespace mesh {

class MeshBCTypes : public QObject
{
    Q_OBJECT
public:
    enum class Type : int {
        Wall                  = 0,  ///< Zero-flux barrier (default)
        NormalFlow            = 1,  ///< Manning outflow at bed slope
        SpecifiedStageConst   = 2,  ///< Fixed water-surface elevation
        SpecifiedStageTS      = 3,  ///< Time-varying water-surface elevation
        SpecifiedFlowConst    = 4,  ///< Fixed discharge per metre of edge
        SpecifiedFlowTS       = 5,  ///< Time-varying discharge per metre
        RatingCurve           = 6,  ///< Stage → flow lookup
    };
    Q_ENUM(Type)

    /*! \brief Human-readable label suitable for combos / tooltips. */
    static QString label(Type t);

    /*! \brief INP TYPE token (uppercase, no spaces). */
    static QString inpToken(Type t);

    /*! \brief Parse an INP TYPE token (uppercase or mixed case). */
    static Type fromInpToken(const QString &token, bool *ok = nullptr);

    /*! \brief Engine-side BC enum (0..4) — collapses TS variants onto stage/flow. */
    static int engineBCType(Type t);
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHBCTYPE_H
