/**
 * @file      meshcellparams.h
 * @brief     Registry of the per-cell (per-triangle) 2D mesh parameters the user can prescribe
 *            interactively, plus the read/write dispatch every editing surface shares.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 *
 * Registry of the per-cell (per-triangle) 2D mesh parameters the user can
 * prescribe interactively, plus the read/write dispatch every editing surface
 * shares.
 *
 * One table drives the mesh-editing toolbar's parameter selector, the
 * Cell Data assignment dialog's target selector, and the undo command's
 * value plumbing, so a new parameter is added in exactly one place.
 *
 * This header is deliberately read-only and layer-free. Writes live in
 * map/meshcommands.h, where they funnel through
 * SWMM2DMeshLayer::applyMeshTriangle* (which emits `attributeChanged`, the
 * signal that keeps the toolbar, the properties panel and the map in sync no
 * matter which surface made the edit) and onto the undo stack.
 *
 * Groundwater parameters use a separate assignment workflow. Their entries
 * remain disabled in this generic mesh-field selector and direct users to it.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHCELLPARAMS_H
#define OPENSWMMVIS_MESH_MESHCELLPARAMS_H

#include "meshinfil.h"
#include "meshresult.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mesh
{

/*! \brief Describes one editable per-cell parameter: how to label it, how to
 *         configure a numeric editor for it, and whether it is live yet. */
struct CellParamSpec {
    /*! What kind of value the parameter carries. GUI plan §3.5(1): the
     *  infiltration METHOD is an enumeration, not a magic double, so every
     *  surface fed by this registry (attribute table, mesh-editing toolbar,
     *  property adapter, assign dialog) can pick a combo editor for it from
     *  one place. */
    enum class Kind { Double, Enum };

    QByteArray key;          ///< stable id, e.g. "mannings", "initDepth"
    QString label;           ///< combo / property label, already translated
    QString prefix;          ///< spin-box prefix, e.g. "n=" ({} for none)
    bool lengthUnit = false; ///< true → append the project depth unit
    double min = 0.0;
    double max = 1.0;
    double step = 0.01;
    double defaultValue = 0.0;
    int decimals = 4;
    bool enabled = true; ///< false → unavailable in this generic editor
    QString tooltip;
    /*! Value kind. New members stay at the END of the struct: the registry
     *  builds its entries with positional aggregate initialisers, which fall
     *  back to these default member initialisers for anything they omit. */
    Kind kind = Kind::Double;
    /*! Choice labels for \c Kind::Enum, in the enumeration's own order. The
     *  stored value is the enum's integer, not the label index — the labels
     *  are display-only. Empty for \c Kind::Double. */
    QStringList enumLabels;
};

/**
 * @brief Return the immutable shared registry of mesh cell parameter/editor specifications.
 * @return Borrowed reference to a process-lifetime static vector in display order; never delete
 * or mutate. Initialization is lazy and labels are translated once using the then-current
 * application translators; later translator changes do not rebuild the vector. Includes disabled
 * preview/snow/groundwater entries as well as editable parameters. First call allocates;
 * subsequent reads retain stable spec addresses.
 *
 * @details The registry, in display order. Stable for the process lifetime.
 */
[[nodiscard]] const QVector<CellParamSpec> &cellParamSpecs();

/**
 * @brief Find a registry specification by its case-sensitive stable byte key.
 * @param[in] key Borrowed key, unchanged; unknown/empty keys do not match.
 * @return Borrowed process-lifetime pointer into cellParamSpecs(), or null when unknown. Linear
 * registry scan, lazy initialization may allocate; caller must not delete or mutate the result.
 *
 * @details Look up one spec by key; returns nullptr when unknown.
 */
[[nodiscard]] const CellParamSpec *cellParamSpec(const QByteArray &key);

/**
 * @brief Build a display label using the registry and an optional project depth-unit suffix.
 * @param[in] key Borrowed case-sensitive parameter key, unchanged.
 * @param[in] depthUnitLabel Borrowed display unit text, normally UnitSystem::depthLabel(); no
 * conversion occurs.
 * @return Owned translated spec label, with parenthesized unit only for lengthUnit entries and a
 * nonempty unit label. Unknown keys fall back to QString::fromUtf8(key). Does not change
 * registry or project data.
 *
 * @details Full label for \p key including the unit suffix when the parameter is a length. \p
 * depthUnitLabel comes from UnitSystem::depthLabel().
 */
[[nodiscard]] QString cellParamLabel(const QByteArray &key, const QString &depthUnitLabel);

/**
 * @brief Read a stored or inheritance-resolved cell parameter without substituting editor
 * defaults.
 * @param[in] mesh Borrowed stable mesh and authoring-preview arrays, unchanged.
 * @param[in] tri Cell/preview index; ordinary parameters require [0,triangles.size()). Preview
 * owner/weather keys instead validate only their corresponding preview array extent, so a
 * preview may exist without a cell.
 * @param[in] key Borrowed case-sensitive registry key; method-specific infiltration keys resolve
 * override, region tag then wildcard, and unused method slots are unavailable.
 * @return Stored value, NaN for missing/out-of-range/unknown/unset/unused values, or numeric
 * enum including -1 for infiltration None. Depth and infiltration values retain their stored
 * project units, percentages are 0..100 by convention and owner/conflict codes are integers
 * represented as doubles. Does not clamp, validate physical ranges, convert units, materialize
 * overrides or write engine state.
 *
 * @details Current value of \p key on triangle \p tri.
 *
 * The `infil.*` keys resolve through mesh::resolveInfil (override → tag row → '*' row), so an
 * inherited value reads back here exactly as the engine would see it; a parameter the resolved
 * METHOD does not use returns NaN, which is what drives the attribute table's per-row column
 * masking. `infil.method` returns the mesh::InfilMethod integer (-1 = None).
 */
[[nodiscard]] double cellParamValue(const MeshResult &mesh, int tri, const QByteArray &key);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHCELLPARAMS_H
