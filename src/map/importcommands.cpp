/*!
 * \file   importcommands.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "map/importcommands.h"

#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "ui/properties/swmmnodepropertyadapter.h"
#include "ui/properties/swmmlinkpropertyadapter.h"
#include "ui/properties/swmmraingagepropertyadapter.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_gages.h>

// ===========================================================================
// SetAdapterPropertiesCommand
// ===========================================================================

SetAdapterPropertiesCommand::SetAdapterPropertiesCommand(
        SWMMModelLayer *layer, quint8 kind, QString name,
        QVariantMap newValues, QVariantMap oldValues,
        MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Set Attributes \"%1\"").arg(name), canvas, parent),
      m_layer(layer),
      m_kind(kind),
      m_name(std::move(name)),
      m_newValues(std::move(newValues)),
      m_oldValues(std::move(oldValues))
{
}

QObject *SetAdapterPropertiesCommand::createAdapter(SWMMModelLayer *layer,
                                                    quint8 kind,
                                                    const QString &name)
{
    if (!layer) return nullptr;
    SWMM_Engine eng = layer->engine();
    if (!eng) return nullptr;
    const QByteArray utf8 = name.toUtf8();

    QObject *adapter = nullptr;

    if (kind == SWMMModelLayer::kKindNode) {
        const int idx = swmm_node_index(eng, utf8.constData());
        if (idx < 0) return nullptr;
        int type = 0;
        if (swmm_node_get_type(eng, idx, &type) != 0) return nullptr;
        switch (type) {
        case 0:  adapter = new SWMMJunctionPropertyAdapter(eng, name); break;
        case 1:  adapter = new SWMMOutfallPropertyAdapter(eng, name);  break;
        case 2:  adapter = new SWMMStoragePropertyAdapter(eng, name);  break;
        case 3:  adapter = new SWMMDividerPropertyAdapter(eng, name);  break;
        default: return nullptr;
        }
        static_cast<SWMMNodePropertyAdapter *>(adapter)->setModelLayer(layer);
    } else if (kind == SWMMModelLayer::kKindLink) {
        const int idx = swmm_link_index(eng, utf8.constData());
        if (idx < 0) return nullptr;
        int type = 0;
        if (swmm_link_get_type(eng, idx, &type) != 0) return nullptr;
        switch (type) {
        case 0:  adapter = new SWMMConduitPropertyAdapter(eng, name); break;
        case 1:  adapter = new SWMMPumpPropertyAdapter(eng, name);    break;
        case 2:  adapter = new SWMMOrificePropertyAdapter(eng, name); break;
        case 3:  adapter = new SWMMWeirPropertyAdapter(eng, name);    break;
        case 4:  adapter = new SWMMOutletPropertyAdapter(eng, name);  break;
        default: return nullptr;
        }
        static_cast<SWMMLinkPropertyAdapter *>(adapter)->setModelLayer(layer);
    } else if (kind == SWMMModelLayer::kKindGage) {
        const int idx = swmm_gage_index(eng, utf8.constData());
        if (idx < 0) return nullptr;
        auto *gage = new SWMMRainGagePropertyAdapter(eng, name);
        gage->setModelLayer(layer);
        adapter = gage;
    }

    return adapter;
}

void SetAdapterPropertiesCommand::apply(const QVariantMap &values)
{
    if (values.isEmpty()) return;
    QObject *adapter = createAdapter(m_layer, m_kind, m_name);
    if (!adapter) return;   // object vanished (e.g. undone add) — clean no-op

    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        adapter->setProperty(it.key().toUtf8().constData(), it.value());

    delete adapter;

    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Scene,
                             QStringLiteral("import-set-attributes"));
}

void SetAdapterPropertiesCommand::redo() { apply(m_newValues); }
void SetAdapterPropertiesCommand::undo() { apply(m_oldValues); }

// ===========================================================================
// SetLinkVerticesCommand
// ===========================================================================

SetLinkVerticesCommand::SetLinkVerticesCommand(
        SWMMModelLayer *layer, QString linkName,
        QVector<QPointF> oldInterior, QVector<QPointF> newInterior,
        MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Set Link Vertices \"%1\"").arg(linkName),
                 canvas, parent),
      m_layer(layer),
      m_name(std::move(linkName)),
      m_oldInterior(std::move(oldInterior)),
      m_newInterior(std::move(newInterior))
{
}

void SetLinkVerticesCommand::apply(const QVector<QPointF> &interior)
{
    if (!m_layer) return;
    // Resolve by name on every apply — indices shift when other
    // commands add/remove links around this one.
    const int idx = m_layer->linkIndex(m_name);
    if (idx < 0) return;   // link vanished (undone add) — clean no-op
    m_layer->applyLinkInteriorVertices(idx, interior);
}

void SetLinkVerticesCommand::redo() { apply(m_newInterior); }
void SetLinkVerticesCommand::undo() { apply(m_oldInterior); }
