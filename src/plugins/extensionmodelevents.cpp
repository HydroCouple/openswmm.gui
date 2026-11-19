/**
 * @file extensionmodelevents.cpp
 * @brief Canonical model, computed-field, shared-selection and display-unit observations.
 * @details Native signals describe invalidations rather than a complete edit log.
 * Snapshot-backed edits suppress intermediate signals and publish broad rebuilds;
 * coordinate moves can emit only modelEdited. Those paths deliberately invalidate
 * geometry/options/values broadly instead of inventing object or field deltas.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionviewaccess.h"
#include "core/unitsystem.h"
#include "layers/swmmmodellayer.h"
#include "selection/selectionmanager.h"
#include "swmmvisprojectwindow.h"
#include <QSet>

using namespace OpenSWMM::Sdk;
namespace openswmmvis
{
namespace
{
/** @brief Resolve the exact spatial namespace supplied by native rename.
 * @param[in] kind One native namespace bit, never a selection type/category.
 * @return Stable public namespace token, or empty for an unsupported bit. */
QString objectNamespace(int kind)
{
    switch (kind) {
    case SWMMModelLayer::kKindNode:
        return QStringLiteral("node");
    case SWMMModelLayer::kKindLink:
        return QStringLiteral("link");
    case SWMMModelLayer::kKindCatch:
        return QStringLiteral("subcatchment");
    case SWMMModelLayer::kKindGage:
        return QStringLiteral("gage");
    default:
        return {};
    }
}
}

void installExtensionModelViewEvents(SWMMVisProjectWindow *window, QObject *context,
                                     ViewEventSink changed)
{
    if (!window || !context || !changed)
        return;
    const auto publish = [changed](ViewEventKind kind) {
        ViewEvent event;
        event.kind = kind;
        changed(event);
    };
    if (auto *layer = window->modelLayer()) {
        const auto broad = [publish] {
            publish(ViewEventKind::ModelGeometryChanged);
            publish(ViewEventKind::ModelOptionsChanged);
            publish(ViewEventKind::AttributeValuesChanged);
        };
        QObject::connect(layer, &SWMMModelLayer::modelEdited, context, broad);
        QObject::connect(layer, &SWMMModelLayer::geometryChanged, context, [publish] {
            publish(ViewEventKind::ModelGeometryChanged);
            publish(ViewEventKind::AttributeValuesChanged);
        });
        QObject::connect(layer, &SWMMModelLayer::attributeChanged, context,
                         [changed](const QString &name) {
                             ViewEvent event;
                             event.kind = ViewEventKind::AttributeValuesChanged;
                             event.objectName = name;
                             changed(event);
                         });
        QObject::connect(layer, &SWMMModelLayer::optionsChanged, context,
                         [changed](const QStringList &keys) {
                             ViewEvent event;
                             event.kind = ViewEventKind::ModelOptionsChanged;
                             // Empty means broad invalidation. Never return a misleading partial list.
                             if (keys.size() <= 128) {
                                 QSet<QString> seen;
                                 for (const auto &key : keys)
                                     if (!seen.contains(key)) {
                                         seen.insert(key);
                                         event.keys.append(key);
                                     }
                             }
                             changed(event);
                         });
        QObject::connect(layer, &SWMMModelLayer::objectRenamed, context,
                         [changed](const QString &oldName, const QString &newName, int kind) {
                             ViewEvent event;
                             event.kind = ViewEventKind::ObjectRenamed;
                             event.objectNamespace = objectNamespace(kind);
                             event.previousName = oldName;
                             event.objectName = newName;
                             changed(event);
                         });
        QObject::connect(layer, &SWMMModelLayer::extensionAttributesChanged, context,
                         [publish](bool schemaChanged) {
                             publish(schemaChanged ? ViewEventKind::AttributeSchemaChanged
                                                   : ViewEventKind::AttributeValuesChanged);
                         });
        QObject::connect(layer, &SWMMModelLayer::modelLoaded, context, [broad, publish] {
            broad();
            publish(ViewEventKind::AttributeSchemaChanged);
        });
    }
    if (auto *selection = window->selectionManager())
        QObject::connect(selection, &SelectionManager::selectionChanged, context,
                         [publish] { publish(ViewEventKind::ModelSelectionChanged); });
    if (auto *units = window->unitSystem())
        QObject::connect(units, &UnitSystem::unitsChanged, context,
                         [publish] { publish(ViewEventKind::ProjectUnitsChanged); });
}

void populateExtensionModelViewState(SWMMVisProjectWindow *window, ViewSnapshot &snapshot)
{
    if (!window)
        return;
    if (auto *units = window->unitSystem())
        snapshot.projectUnits = units->flowUnitLabel();
    if (auto *selection = window->selectionManager())
        snapshot.selectedObjects = selection->selection().size();
}
}
