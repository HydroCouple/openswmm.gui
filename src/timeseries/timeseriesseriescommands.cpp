/*!
 * \file   timeseriesseriescommands.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "timeseries/timeseriesseriescommands.h"

#include "layers/swmmmodellayer.h"
#include "timeseries/timeseriesregistry.h"

#include <QObject>

#include <openswmm/engine/openswmm_edit.h>
#include <openswmm/engine/openswmm_tables.h>

#include <utility>

namespace openswmmvis::timeseries {

namespace {

SWMMModelLayer *asLayer(void *handle)
{
    return static_cast<SWMMModelLayer *>(handle);
}

TimeseriesRegistry *registryOf(void *handle)
{
    SWMMModelLayer *layer = asLayer(handle);
    if (!layer)
        return nullptr;
    return qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
}

/*! Flush inline providers to the engine — the convention every editor follows
 *  immediately after mutating the registry. */
void flush(void *handle)
{
    SWMMModelLayer *layer = asLayer(handle);
    TimeseriesRegistry *reg = registryOf(handle);
    if (reg && layer)
        reg->saveToEngine(layer->engine());
}

/*! Drop the engine's own table for \p name.
 *
 *  TimeseriesRegistry::remove() only forgets the provider, and saveToEngine()
 *  never deletes rows, so without this an undone create leaves a live table
 *  that reappears in the written INP. */
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

AddTimeseriesCommand::AddTimeseriesCommand(void *layerHandle,
                                           QString name,
                                           QVector<TimeseriesPoint> points,
                                           QString unitsLabel,
                                           QString description,
                                           QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_layer(layerHandle)
    , m_name(std::move(name))
    , m_points(std::move(points))
    , m_unitsLabel(std::move(unitsLabel))
    , m_description(std::move(description))
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
    if (reg->hasName(m_name))
    {
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
    flush(m_layer);
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

SetTimeseriesPointsCommand::SetTimeseriesPointsCommand(void *layerHandle,
                                                       QString name,
                                                       QVector<TimeseriesPoint> newPoints,
                                                       QString newDescription,
                                                       QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_layer(layerHandle)
    , m_name(std::move(name))
    , m_newPoints(std::move(newPoints))
    , m_newDescription(std::move(newDescription))
{
    setText(QObject::tr("Update timeseries %1").arg(m_name));
}

void SetTimeseriesPointsCommand::apply(const QVector<TimeseriesPoint> &pts,
                                       const QString &desc)
{
    TimeseriesRegistry *reg = registryOf(m_layer);
    if (!reg)
        return;
    TimeseriesProvider *p = reg->findByName(m_name);
    if (!p)
        return;   // series gone (e.g. an undone add) — clean no-op
    p->setDescription(desc);
    p->setAllPoints(pts);
    flush(m_layer);
}

void SetTimeseriesPointsCommand::redo()
{
    // Capture once, on the first redo, so the snapshot reflects the state this
    // command actually replaced rather than whatever existed at construction.
    if (!m_captured)
    {
        if (TimeseriesRegistry *reg = registryOf(m_layer))
            if (TimeseriesProvider *p = reg->findByName(m_name))
            {
                m_oldPoints      = p->points();
                m_oldDescription = p->description();
                m_captured       = true;
            }
    }
    apply(m_newPoints, m_newDescription);
}

void SetTimeseriesPointsCommand::undo()
{
    if (m_captured)
        apply(m_oldPoints, m_oldDescription);
}

DeleteTimeseriesCommand::DeleteTimeseriesCommand(void *layerHandle,
                                                 QString name,
                                                 QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_layer(layerHandle)
    , m_name(std::move(name))
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

    if (!m_captured)
    {
        m_points      = p->points();
        m_unitsLabel  = p->unitsLabel();
        m_description = p->description();
        m_captured    = true;
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
    flush(m_layer);
}

} // namespace openswmmvis::timeseries
