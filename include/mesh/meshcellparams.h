/*!
 * \file   meshcellparams.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Registry of the per-cell (per-triangle) 2D mesh parameters the user can
 * prescribe interactively, plus the read/write dispatch every editing surface
 * shares.
 *
 * One table drives the mesh-editing toolbar's parameter selector, the
 * Cell Data assignment dialog's target selector, and the undo command's
 * value plumbing, so a new parameter is added in exactly one place. Writes
 * always funnel through SWMM2DMeshLayer::applyMeshTriangle*, which emits
 * `attributeChanged` — that is what keeps the toolbar, the properties panel
 * and the map in sync no matter which surface made the edit.
 *
 * Parameters awaiting engine support (the 2D two-zone groundwater set) are
 * listed with `enabled = false`: they show greyed in every selector so the
 * roadmap is visible, and `applyCellParam` refuses them.
 */
#ifndef OPENSWMMVIS_MESH_MESHCELLPARAMS_H
#define OPENSWMMVIS_MESH_MESHCELLPARAMS_H

#include "meshresult.h"

#include <QByteArray>
#include <QString>
#include <QVector>

class SWMM2DMeshLayer;

namespace mesh {

/*! \brief Describes one editable per-cell parameter: how to label it, how to
 *         configure a numeric editor for it, and whether it is live yet. */
struct CellParamSpec
{
    QByteArray key;              ///< stable id, e.g. "mannings", "initDepth"
    QString    label;            ///< combo / property label, already translated
    QString    prefix;           ///< spin-box prefix, e.g. "n=" ({} for none)
    bool       lengthUnit = false; ///< true → append the project depth unit
    double     min          = 0.0;
    double     max          = 1.0;
    double     step         = 0.01;
    double     defaultValue = 0.0;
    int        decimals     = 4;
    bool       enabled      = true;  ///< false → engine support pending
    QString    tooltip;
};

/*! \brief The registry, in display order. Stable for the process lifetime. */
[[nodiscard]] const QVector<CellParamSpec> &cellParamSpecs();

/*! \brief Look up one spec by key; returns nullptr when unknown. */
[[nodiscard]] const CellParamSpec *cellParamSpec(const QByteArray &key);

/*! \brief Full label for \p key including the unit suffix when the parameter
 *         is a length. \p depthUnitLabel comes from UnitSystem::depthLabel(). */
[[nodiscard]] QString cellParamLabel(const QByteArray &key,
                                     const QString &depthUnitLabel);

/*! \brief Current value of \p key on triangle \p tri.
 *  \returns the stored value, or NaN when the triangle index is out of range,
 *           the key is unknown, or the attribute is unset (the NaN sentinel
 *           MeshTriangle uses for "column absent"). Callers substitute the
 *           spec's defaultValue for display. */
[[nodiscard]] double cellParamValue(const MeshResult &mesh, int tri,
                                    const QByteArray &key);

/*! \brief Write \p value to triangle \p tri through the layer's apply*
 *         helper, so every view refreshes.
 *  \returns false for an unknown/disabled key, a null layer, an out-of-range
 *           index, or a value the layer rejects. */
bool applyCellParam(SWMM2DMeshLayer *layer, int tri, const QByteArray &key,
                    double value);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHCELLPARAMS_H
