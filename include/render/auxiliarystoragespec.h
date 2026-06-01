/*!
 * \file   auxiliarystoragespec.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Typed config for the Layer Properties → Auxiliary Storage tab
 *         (Slice Z.15).
 *
 *         RENDERING_RULE_MODEL_PLAN.md §11.4 — per-feature manual style
 *         overrides persisted to a sidecar SQLite DB. Use cases:
 *           - "This one junction is highlighted red because I'm
 *             investigating it"
 *           - "These three conduits are dashed because they're proposed
 *             not built"
 *
 *         Overrides survive Rule edits — they're a layer above the
 *         Rule List. The DB stores (feature_id, property_path,
 *         value_or_expression) tuples; render path consults it after
 *         Data-defined overrides.
 *
 *         The spec itself is small: a toggle plus the sidecar DB path.
 *         The override rows themselves don't live in this spec —
 *         they're managed at runtime by the auxiliary-storage tab UI
 *         and persisted through SQLite. The spec just tells the layer
 *         "here's where to find the DB" and "is it active right now."
 *
 *         Slice Z.15-data ships the value type + JSON round-trip. The
 *         SQLite schema + tab UI + runtime override application are
 *         separate slices (Z.15-db, Z.15-ui, Z.15-paint).
 */

#ifndef OPENSWMM_RENDER_AUXILIARYSTORAGESPEC_H
#define OPENSWMM_RENDER_AUXILIARYSTORAGESPEC_H

#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

/*!
 * \struct AuxiliaryStorageSpec
 * \brief Per-layer auxiliary-storage configuration.
 */
struct AuxiliaryStorageSpec
{
    /*! \brief Master switch. When false, the DB is not consulted at
     *         paint time (overrides are inert but preserved). */
    bool      enabled = false;

    /*! \brief Filesystem path to the sidecar SQLite DB. Relative paths
     *         are resolved against the project directory at runtime.
     *         Empty path falls back to the default location
     *         `<projectDir>/.auxstorage/<layerId>.db`. */
    QString   dbPath;

    [[nodiscard]] QJsonObject toJson() const;
    static AuxiliaryStorageSpec fromJson(const QJsonObject &j);

    [[nodiscard]] bool operator==(const AuxiliaryStorageSpec &other) const;
    [[nodiscard]] bool operator!=(const AuxiliaryStorageSpec &other) const
    { return !(*this == other); }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_AUXILIARYSTORAGESPEC_H
