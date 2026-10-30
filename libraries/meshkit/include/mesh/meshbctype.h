/**
 * @file      meshbctype.h
 * @brief     Slice §V.VA / §V.VC — enumeration of 2D mesh boundary-condition types surfaced by the
 *            Mesh Editing toolbar.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHBCTYPE_H
#define OPENSWMMVIS_MESH_MESHBCTYPE_H

#include <QObject>
#include <QString>

namespace mesh
{

class MeshBCTypes : public QObject
{
    Q_OBJECT
public:
    enum class Type : int {
        Wall = 0,                ///< Zero-flux barrier (default)
        NormalFlow = 1,          ///< Manning outflow at bed slope
        SpecifiedStageConst = 2, ///< Fixed water-surface elevation
        SpecifiedStageTS = 3,    ///< Time-varying water-surface elevation
        SpecifiedFlowConst = 4,  ///< Fixed discharge per metre of edge
        SpecifiedFlowTS = 5,     ///< Time-varying discharge per metre
        RatingCurve = 6,         ///< Stage → flow lookup
    };
    Q_ENUM(Type)

    /**
     * @brief Translate the display label for a mesh boundary-condition variant.
     * @param[in] t GUI boundary type, including distinct constant and timeseries variants.
     * @return Owned translated label in the MeshBCTypes context, or an empty string for an
     * unknown enum value. NormalFlow displays as Normal Depth. Translation follows the
     * application's installed translators; no boundary record is modified and the returned
     * string does not borrow caller storage.
     *
     * @details Human-readable label suitable for combos / tooltips.
     */
    static QString label(Type t);

    /**
     * @brief Encode a GUI boundary type as its stable INP TYPE token.
     * @param[in] t GUI boundary type, preserved.
     * @return Owned uppercase token WALL, NORMAL_FLOW, SPECIFIED_STAGE, TS_STAGE,
     * SPECIFIED_FLOW, TS_FLOW or RATING_CURVE; empty for an unknown enum value. Locale
     * independent, no mesh mutation.
     *
     * @details INP TYPE token (uppercase, no spaces).
     */
    static QString inpToken(Type t);

    /**
     * @brief Parse a trimmed case-insensitive INP boundary token into a GUI variant.
     * @param[in] token Borrowed text, whitespace trimmed and uppercase-normalized internally;
     * unchanged.
     * @param[out] ok Optional borrowed result flag; true for any recognized token, false
     * otherwise. May be null, in which case recognition failure is available only through the
     * fallback behavior.
     * @return Recognized GUI type, or Wall on an unrecognized/empty token. Use ok to distinguish
     * an explicit WALL from failure. No project state mutation or ownership retention.
     *
     * @details Parse an INP TYPE token (uppercase or mixed case).
     */
    static Type fromInpToken(const QString &token, bool *ok = nullptr);

    /**
     * @brief Collapse GUI constant/timeseries variants into the engine boundary vocabulary.
     * @param[in] t GUI boundary type, preserved.
     * @return Engine integer 0 wall, 1 normal flow, 2 specified stage, 3 specified flow or 4
     * rating curve; unknown values fall back to 0. Both stage variants map to 2 and both flow
     * variants to 3; timeseries names are carried separately by boundary records. Does not
     * validate or install a BC.
     *
     * @details Engine-side BC enum (0..4) — collapses TS variants onto stage/flow.
     */
    static int engineBCType(Type t);
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHBCTYPE_H
