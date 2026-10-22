/**
 * @file      timeseriesseriescommands.cpp
 * @brief     Separate from timeseriesundocommands.h on purpose.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "timeseries/timeseriesseriescommands.h"

#include "layers/swmmmodellayer.h"
#include "timeseries/timeseriesregistry.h"

#include <QObject>

#include <openswmm/engine/openswmm_edit.h>
#include <openswmm/engine/openswmm_tables.h>

#include <utility>

namespace openswmmvis::timeseries
{

namespace
{

/**
 * @brief Recover the borrowed model-layer pointer used by whole-series commands.
 * @param[in] handle Actual SWMMModelLayer pointer, or null; no runtime type check is performed.
 * @return Same borrowed pointer cast to its concrete type.
 */
SWMMModelLayer *asLayer(void *handle)
{
    return static_cast<SWMMModelLayer *>(handle);
}

/**
 * @brief Lazily resolve the layer-owned series registry.
 * @param[in] handle Borrowed model-layer pointer erased as void*, valid on the caller thread.
 * @return Borrowed registry from ensureTimeseriesRegistry(), or null for null layer/failed cast; lazy initialization can load engine providers.
 */
TimeseriesRegistry *registryOf(void *handle)
{
    SWMMModelLayer *layer = asLayer(handle);
    if (!layer)
        return nullptr;
    return qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
}

/*! Flush the one provider a command changed to the engine — the convention
 *  every editor follows immediately after mutating the registry. Only that
 *  series is rewritten, so a macro of N series commands stays O(N).
 * @param[in] handle Borrowed model-layer pointer erased as void*, supplying the current engine.
 * @param[in] p Borrowed provider expected to be registered in that layer.
 * @details No-op for missing layer/registry/provider; save failure is ignored.
 */
void flush(void *handle, TimeseriesProvider *p)
{
    SWMMModelLayer *layer = asLayer(handle);
    TimeseriesRegistry *reg = registryOf(handle);
    if (reg && layer && p)
        reg->saveProviderToEngine(p, layer->engine());
}

/*! Drop the engine's own table for \p name.
 *
 *  The registry now also requests deletion; this explicit pre-removal path
 *  remains part of the command implementation.
 * @param[in] handle Borrowed model-layer pointer erased as void*.
 * @param[in] name Table identifier passed to generic swmm_table_index().
 * @details No-op without an engine/matching table; delete result is ignored. Generic lookup can resolve a same-named curve when no timeseries exists.
 */
void dropEngineTable(void *handle, const QString &name)
{
    SWMMModelLayer *layer = asLayer(handle);
    if (!layer || !layer->engine())
        return;
    const int idx = swmm_table_index(layer->engine(), name.toUtf8().constData());
    if (idx >= 0)
        swmm_table_delete(layer->engine(), idx, nullptr);
}

} // namespace

AddTimeseriesCommand::AddTimeseriesCommand(void *layerHandle, QString name,
                                           QVector<TimeseriesPoint> points, QString unitsLabel,
                                           QString description, QUndoCommand *parent)
    : QUndoCommand(parent), m_layer(layerHandle), m_name(std::move(name)),
      m_points(std::move(points)), m_unitsLabel(std::move(unitsLabel)),
      m_description(std::move(description))
{
    setText(QObject::tr("Add timeseries %1").arg(m_name));
}

void AddTimeseriesCommand::redo()
{
    TimeseriesRegistry *reg = registryOf(m_layer);
    if (!reg)
        return;

    // Someone else already owns this name — leave it strictly alone, including
    // on undo. Also makes redo() safe to re-enter.
    if (reg->hasName(m_name)) {
        m_preexisting = true;
        return;
    }
    m_preexisting = false;

    TimeseriesProvider *p = reg->create(m_name);
    if (!p)
        return;
    p->setSourceMode(TimeseriesProvider::SourceMode::Inline);
    p->setUnitsLabel(m_unitsLabel);
    p->setDescription(m_description);
    p->setAllPoints(m_points);
    flush(m_layer, p);
}

void AddTimeseriesCommand::undo()
{
    if (m_preexisting)
        return;
    TimeseriesRegistry *reg = registryOf(m_layer);
    if (!reg)
        return;
    // Engine row first: once the provider is gone the registry can no longer
    // tell us anything about it.
    dropEngineTable(m_layer, m_name);
    if (TimeseriesProvider *p = reg->findByName(m_name))
        reg->remove(p);
}

SetTimeseriesPointsCommand::SetTimeseriesPointsCommand(void *layerHandle, QString name,
                                                       QVector<TimeseriesPoint> newPoints,
                                                       QString newDescription, QUndoCommand *parent)
    : QUndoCommand(parent), m_layer(layerHandle), m_name(std::move(name)),
      m_newPoints(std::move(newPoints)), m_newDescription(std::move(newDescription))
{
    setText(QObject::tr("Update timeseries %1").arg(m_name));
}

void SetTimeseriesPointsCommand::apply(const QVector<TimeseriesPoint> &pts, const QString &desc)
{
    TimeseriesRegistry *reg = registryOf(m_layer);
    if (!reg)
        return;
    TimeseriesProvider *p = reg->findByName(m_name);
    if (!p)
        return; // series gone (e.g. an undone add) — clean no-op
    p->setDescription(desc);
    p->setAllPoints(pts);
    flush(m_layer, p);
}

void SetTimeseriesPointsCommand::redo()
{
    // Capture once, on the first redo, so the snapshot reflects the state this
    // command actually replaced rather than whatever existed at construction.
    if (!m_captured) {
        if (TimeseriesRegistry *reg = registryOf(m_layer))
            if (TimeseriesProvider *p = reg->findByName(m_name)) {
                m_oldPoints = p->points();
                m_oldDescription = p->description();
                m_captured = true;
            }
    }
    apply(m_newPoints, m_newDescription);
}

void SetTimeseriesPointsCommand::undo()
{
    if (m_captured)
        apply(m_oldPoints, m_oldDescription);
}

DeleteTimeseriesCommand::DeleteTimeseriesCommand(void *layerHandle, QString name,
                                                 QUndoCommand *parent)
    : QUndoCommand(parent), m_layer(layerHandle), m_name(std::move(name))
{
    setText(QObject::tr("Delete timeseries %1").arg(m_name));
}

void DeleteTimeseriesCommand::redo()
{
    TimeseriesRegistry *reg = registryOf(m_layer);
    if (!reg)
        return;
    TimeseriesProvider *p = reg->findByName(m_name);
    if (!p)
        return;

    if (!m_captured) {
        m_points = p->points();
        m_unitsLabel = p->unitsLabel();
        m_description = p->description();
        m_captured = true;
    }
    dropEngineTable(m_layer, m_name);
    reg->remove(p);
}

void DeleteTimeseriesCommand::undo()
{
    if (!m_captured)
        return;
    TimeseriesRegistry *reg = registryOf(m_layer);
    if (!reg || reg->hasName(m_name))
        return;
    TimeseriesProvider *p = reg->create(m_name);
    if (!p)
        return;
    p->setSourceMode(TimeseriesProvider::SourceMode::Inline);
    p->setUnitsLabel(m_unitsLabel);
    p->setDescription(m_description);
    p->setAllPoints(m_points);
    flush(m_layer, p);
}

} // namespace openswmmvis::timeseries
