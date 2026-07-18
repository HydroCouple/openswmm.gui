/*!
 * \file   importmapping.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * FEATURE_LAYER_TO_SWMM_IMPORT — the mapping value object the dialog
 * edits, the planner consumes, and named presets serialize. Pure Qt
 * Core.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTMAPPING_H
#define OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTMAPPING_H

#include "ui/dialogs/import/importtargetregistry.h"

#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>

#include <optional>

namespace openswmmvis::import {

/*! Binds one target-attribute key to a source column and/or a
 *  constant default. */
struct AttributeBinding {
    QString  targetKey;     ///< TargetAttribute::key
    QString  sourceField;   ///< OGR field name; empty = unmapped
    QVariant defaultValue;  ///< used when sourceField is empty or the cell is null

    [[nodiscard]] bool isBound() const
    { return !sourceField.isEmpty() || defaultValue.isValid(); }
};

/*! Everything the import needs besides the source layer itself. */
struct ImportMapping {
    TargetKind                kind = TargetKind::Junction;
    QVector<AttributeBinding> bindings;

    // ---- link endpoint resolution (priority order: fields → snap →
    //      auto-create; see the planner) --------------------------------
    bool    endpointsFromFields   = false;
    bool    endpointsSnap         = true;
    double  snapToleranceMapUnits = 1.0;   ///< model-layer CRS units
    bool    autoCreateJunctions   = false;
    QString autoNodePrefix        = QStringLiteral("J_");

    // ---- conflict policy against existing objects ---------------------
    enum class Conflict { Skip = 0, Update = 1 };
    Conflict conflict         = Conflict::Skip;
    bool     updateAttributes = true;   ///< meaningful when conflict == Update
    bool     updateGeometry   = false;  ///< meaningful when conflict == Update

    // ---- source-side filter -------------------------------------------
    bool selectedFeaturesOnly = false;

    [[nodiscard]] const AttributeBinding *binding(const QString &targetKey) const;
    /*! Returns the existing binding for \p targetKey, creating an
     *  unbound one if absent. */
    AttributeBinding &ensureBinding(const QString &targetKey);

    /*! Serialize for presets. Field bindings are stored by source-field
     *  *name* so a preset survives re-application to a different layer
     *  with the same schema. */
    [[nodiscard]] QJsonObject toJson() const;

    /*! Tolerant parse — unknown keys ignored, missing keys defaulted.
     *  Returns std::nullopt (with \p errorOut set) only when the JSON
     *  is structurally unusable (no valid "kind"). */
    [[nodiscard]] static std::optional<ImportMapping>
        fromJson(const QJsonObject &j, QString *errorOut = nullptr);
};

} // namespace openswmmvis::import

#endif // OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTMAPPING_H
