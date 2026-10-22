/**
 * @file      importmapping.h
 * @brief     FEATURE_LAYER_TO_SWMM_IMPORT — the mapping value object the dialog edits, the planner
 *            consumes, and named presets serialize.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * FEATURE_LAYER_TO_SWMM_IMPORT — the mapping value object the dialog
 * edits, the planner consumes, and named presets serialize. Pure Qt
 * Core.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTMAPPING_H
#define OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTMAPPING_H

#include "ui/dialogs/import/importtargetregistry.h"

#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>

#include <optional>

namespace openswmmvis::import
{

/*! Binds one target-attribute key to a source column and/or a
 *  constant default. */
struct AttributeBinding {
    QString targetKey;     ///< TargetAttribute::key
    QString sourceField;   ///< OGR field name; empty = unmapped
    QVariant defaultValue; ///< used when sourceField is empty or the cell is null

    /**
     * @return Source field string NONEMPTY or default QVariant isValid, including whitespace-only
     *   field name and valid null default. Does not verify column exists, default converts or value
     *   is nonnull. Pure predicate, no mutation.
     */
    [[nodiscard]] bool isBound() const { return !sourceField.isEmpty() || defaultValue.isValid(); }
};

/*! Everything the import needs besides the source layer itself. */
struct ImportMapping {
    TargetKind kind = TargetKind::Junction;
    QVector<AttributeBinding> bindings;

    // ---- link endpoint resolution (priority order: fields → snap →
    //      auto-create; see the planner) --------------------------------
    bool endpointsFromFields = false;
    bool endpointsSnap = true;
    double snapToleranceMapUnits = 1.0; ///< model-layer CRS units
    bool autoCreateJunctions = false;
    QString autoNodePrefix = QStringLiteral("J_");

    // ---- conflict policy against existing objects ---------------------
    enum class Conflict { Skip = 0, Update = 1 };
    Conflict conflict = Conflict::Skip;
    bool updateAttributes = true; ///< meaningful when conflict == Update
    bool updateGeometry = false;  ///< meaningful when conflict == Update

    // ---- source-side filter -------------------------------------------
    bool selectedFeaturesOnly = false;

    /**
     * @param[in] targetKey Exact case-sensitive key, no trimming/registry validation.
     * @return Borrowed FIRST matching vector entry or null; pointer valid only while mapping/vector
     *   entry remains alive and unmodified, append/reallocation can invalidate. Duplicate bindings
     *   not reconciled. No mutation.
     */
    [[nodiscard]] const AttributeBinding *binding(const QString &targetKey) const;
    /*! Returns the existing binding for \p targetKey, creating an
     *  unbound one if absent.
     * @param[in] targetKey Exact raw key, including empty/unknown, copied on new entry.
     * @return Borrowed first existing entry or appended unbound entry reference. May
     *   grow/reallocate bindings, invalidating earlier references/pointers. Does not validate known
     *   target, source schema or preserve uniqueness of pre-existing duplicates.
     */
    AttributeBinding &ensureBinding(const QString &targetKey);

    /*! Serialize for presets. Field bindings are stored by source-field
     *  *name* so a preset survives re-application to a different layer
     *  with the same schema.
     * @return New version 1 preset object with numeric kind, bound entries only,
     *   endpoint/conflict/selected options. Source field names preserved raw, default via
     *   QJsonValue conversion; non-JSON QVariant fidelity follows Qt. Unbound entries omitted,
     *   ordering/duplicates otherwise retained. No validation, file write or input mutation.
     */
    [[nodiscard]] QJsonObject toJson() const;

    /*! Tolerant parse — unknown keys ignored, missing keys defaulted.
     *  Returns std::nullopt (with \p errorOut set) only when the JSON
     *  is structurally unusable (no valid "kind").
     * @param[in] j Borrowed preset JSON; version/unknown keys ignored, kind must be numeric and
     *   toInt within Junction..Outlet.
     * @param[out] errorOut Optional localized-independent error string set ONLY invalid/missing
     *   kind; not cleared on success.
     * @return Parsed value or nullopt for kind error. Missing/wrong-typed sections default per Qt
     *   conversions; empty binding target skipped, unknown/duplicate targets preserved. No
     *   schema/source-field/default/tolerance/prefix validation; exact lowercase update policy
     *   selects Update, all others Skip. No file/source write.
     */
    [[nodiscard]] static std::optional<ImportMapping> fromJson(const QJsonObject &j,
                                                               QString *errorOut = nullptr);
};

} // namespace openswmmvis::import

#endif // OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTMAPPING_H
