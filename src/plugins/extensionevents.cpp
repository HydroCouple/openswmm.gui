/**
 * @file extensionevents.cpp
 * @brief Canonical value translation for the existing extension event catalogue.
 * @details This bridge types existing observations only. Model readiness, state revisions,
 * run-attempt identities and transaction ordering require their later lifecycle contracts.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionevents.h"

using namespace OpenSWMM::Sdk;
namespace openswmmvis
{
std::optional<Event> decodeExtensionEvent(const QString &name, const QVariantMap &data)
{
    const auto id = data.value("id").toUuid();
    const auto model = [&](EventKind kind) -> Event {
        return {kind, 0,
                ModelEvent{id, kind == EventKind::AttributesChanged ? data.value("name").toString()
                                                                    : QString{}}};
    };
    if (name == "instanceOpened")
        return model(EventKind::InstanceDiscovered);
    if (name == "instanceClosed")
        return model(EventKind::InstanceClosing);
    if (name == "instanceActivated")
        return model(EventKind::InstanceActivated);
    if (name == "selectionChanged")
        return model(EventKind::SelectionChanged);
    if (name == "modelEdited")
        return model(EventKind::ModelEdited);
    if (name == "attributesChanged")
        return model(EventKind::AttributesChanged);
    if (name == "viewportChanged")
        return Event{EventKind::ViewportChanged, 0, SpatialEvent{id, {}}};
    if (name == "layersChanged")
        return Event{EventKind::LayersChanged, 0, SpatialEvent{id, {}}};
    if (name == "boundaryChanged")
        return Event{EventKind::BoundaryChanged, 0,
                     SpatialEvent{id, data.value("layerId").toString()}};
    if (name == "resultsChanged")
        return Event{EventKind::ActiveResultsChanged, 0, ResultsEvent{id}};
    if (name == "runsChanged")
        return Event{EventKind::RunsChanged, 0, RunsEvent{}};
    if (name == "settingsChanged")
        return Event{EventKind::SettingsChanged, 0, SettingsEvent{data.value("key").toString()}};
    if (name == "themeChanged")
        return Event{EventKind::ThemeChanged, 0, ThemeEvent{}};
    if (name == "jobProgress")
        return Event{EventKind::JobProgress, 0,
                     JobProgressEvent{data.value("title").toString(),
                                      data.value("progress").toDouble(),
                                      data.value("text").toString()}};
    if (name == "jobFinished")
        return Event{EventKind::JobFinished, 0,
                     JobFinishedEvent{data.value("title").toString(), data.value("ok").toBool(),
                                      data.value("error").toString()}};
    return std::nullopt;
}

std::pair<QString, QVariantMap> legacyExtensionEvent(const Event &event)
{
    const auto model = [&](const QString &name) -> std::pair<QString, QVariantMap> {
        const auto &value = std::get<ModelEvent>(event.payload);
        QVariantMap data{{"id", value.instanceId}};
        if (event.kind == EventKind::AttributesChanged)
            data.insert("name", value.objectName);
        return {name, data};
    };
    switch (event.kind) {
    case EventKind::InstanceDiscovered:
        return model("instanceOpened");
    case EventKind::InstanceClosing:
        return model("instanceClosed");
    case EventKind::InstanceActivated:
        return model("instanceActivated");
    case EventKind::SelectionChanged:
        return model("selectionChanged");
    case EventKind::ModelEdited:
        return model("modelEdited");
    case EventKind::AttributesChanged:
        return model("attributesChanged");
    case EventKind::ViewportChanged:
        return {"viewportChanged", {{"id", std::get<SpatialEvent>(event.payload).instanceId}}};
    case EventKind::LayersChanged:
        return {"layersChanged", {{"id", std::get<SpatialEvent>(event.payload).instanceId}}};
    case EventKind::BoundaryChanged: {
        const auto &value = std::get<SpatialEvent>(event.payload);
        return {"boundaryChanged", {{"id", value.instanceId}, {"layerId", value.layerId}}};
    }
    case EventKind::ActiveResultsChanged:
        return {"resultsChanged", {{"id", std::get<ResultsEvent>(event.payload).instanceId}}};
    case EventKind::RunsChanged:
        return {"runsChanged", {}};
    case EventKind::SettingsChanged:
        return {"settingsChanged", {{"key", std::get<SettingsEvent>(event.payload).key}}};
    case EventKind::ThemeChanged:
        return {"themeChanged", {}};
    case EventKind::JobProgress: {
        const auto &value = std::get<JobProgressEvent>(event.payload);
        return {"jobProgress",
                {{"title", value.title}, {"progress", value.progress}, {"text", value.text}}};
    }
    case EventKind::JobFinished: {
        const auto &value = std::get<JobFinishedEvent>(event.payload);
        return {"jobFinished", {{"title", value.title}, {"ok", value.ok}, {"error", value.error}}};
    }
    }
    return {};
}
}
