/*!
 * \file   importplan.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * FEATURE_LAYER_TO_SWMM_IMPORT — dry-run plan PODs shared by the
 * planner (pure), the preview model, and the executor. Pure Qt Core.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTPLAN_H
#define OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTPLAN_H

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace openswmmvis::import {

/*! Per-feature planned outcome. */
struct PlannedItem {
    enum class Action { Create, Update, Skip, Error };

    long long   fid = -1;          ///< source OGR FID
    QString     name;              ///< resolved unique identifier
    Action      action = Action::Skip;
    QStringList messages;          ///< human-readable notes/errors

    // Resolved payload, all coordinates in the MODEL layer CRS ---------
    double            x = 0.0, y = 0.0;      ///< point kinds
    QString           fromNode, toNode;      ///< link kinds
    QVector<QPointF>  interiorVertices;      ///< link kinds
    QVector<QPointF>  autoNodePos;           ///< junctions to create first
    QStringList       autoNodeNames;         ///< parallel to autoNodePos
    QVariantMap       attributeValues;       ///< targetKey → coerced value

    // Update-only extras ----------------------------------------------
    bool geometryDiffers = false;  ///< incoming geometry ≠ existing
    bool endpointsDiffer = false;  ///< links: resolved from/to ≠ existing
};

struct ImportPlan {
    QVector<PlannedItem> items;
    int createCount = 0, updateCount = 0, skipCount = 0, errorCount = 0;

    void recount()
    {
        createCount = updateCount = skipCount = errorCount = 0;
        for (const PlannedItem &it : items) {
            switch (it.action) {
            case PlannedItem::Action::Create: ++createCount; break;
            case PlannedItem::Action::Update: ++updateCount; break;
            case PlannedItem::Action::Skip:   ++skipCount;   break;
            case PlannedItem::Action::Error:  ++errorCount;  break;
            }
        }
    }
};

} // namespace openswmmvis::import

#endif // OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTPLAN_H
