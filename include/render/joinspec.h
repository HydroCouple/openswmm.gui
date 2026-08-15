/*!
 * \file   joinspec.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Typed config for the Layer Properties → Joins tab (Slice Z.16).
 *
 *         RENDERING_RULE_MODEL_PLAN.md §11.5 — join external
 *         CSV / DBF / SQLite to a layer's attribute table on a key
 *         field. SWMM use cases:
 *           - Join observed-depth CSV to a node layer; symbology can
 *             target the joined `observed_depth` field
 *           - Join calibration metadata to subcatchments
 *           - Cross-reference cost data on conduits
 *
 *         Joins are lazy (rebuilt on attribute access at paint time
 *         / Identify hover). The join definition lives in the layer's
 *         `.oswp` block.
 *
 *         A layer may have multiple Join specs (different sources or
 *         different key pairs). This spec describes one join; the
 *         Joins tab manages a `QList<JoinSpec>` per layer.
 *
 *         Slice Z.16-data ships the value type + JSON round-trip. The
 *         tab UI widget, runtime join engine, and `.oswp` persistence
 *         are separate slices.
 */

#ifndef OPENSWMM_RENDER_JOINSPEC_H
#define OPENSWMM_RENDER_JOINSPEC_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace OpenSWMM::Render
{

/*!
 * \struct JoinSpec
 * \brief One external-table join.
 */
struct JoinSpec
{
    /*! \brief Master switch — when false, the join isn't applied at
     *         paint / Identify time (but stays defined for later
     *         re-enable). */
    bool         enabled = false;

    /*! \brief Filesystem path to the source data. Relative paths are
     *         resolved against the project directory at runtime. */
    QString      sourcePath;

    /*! \brief Source layer / sheet name (for multi-table sources like
     *         SQLite). Empty for single-table sources (CSV/DBF). */
    QString      sourceLayerName;

    /*! \brief Column name in the source that matches `targetKeyField`. */
    QString      sourceKeyField;

    /*! \brief Attribute name on the host layer to match. */
    QString      targetKeyField;

    /*! \brief Names of fields to import from the source. When empty,
     *         every non-key column is imported. */
    QStringList  joinFields;

    /*! \brief Prefix applied to joined field names on the host layer
     *         (e.g. "obs_" → joined "depth" becomes "obs_depth").
     *         Empty = no prefix. */
    QString      fieldPrefix;

    [[nodiscard]] QJsonObject toJson() const;
    static JoinSpec           fromJson(const QJsonObject &j);

    [[nodiscard]] bool operator==(const JoinSpec &other) const;
    [[nodiscard]] bool operator!=(const JoinSpec &other) const
    { return !(*this == other); }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_JOINSPEC_H
