/*!
 * \file   nodepicksession.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "map/nodepicksession.h"

#include "map/mapcanvas.h"
#include "map/tools/maptoolpicknode.h"

NodePickSession::NodePickSession(MapCanvas *canvas, QObject *parent)
    : QObject(parent), m_canvas(canvas)
{
    if (!canvas) {
        m_finished = true;
        return;
    }
    m_previous = canvas->activeTool();
    m_tool = new OpenSWMMVisMapToolPickNode(canvas, this);
    connect(m_tool, &OpenSWMMVisMapToolPickNode::nodePicked,
            this, &NodePickSession::nodePicked);
    connect(m_tool, &OpenSWMMVisMapToolPickNode::cancelled, this, [this]() {
        finish();
        emit cancelled();
    });
    // The user reached for another tool mid-pick: nothing to restore, the
    // wait is over.
    connect(canvas, &MapCanvas::activeToolChanged, this,
            [this](OpenSWMMVisMapTool *tool) {
                if (m_finished || tool == m_tool) return;
                m_finished = true;
                if (m_tool) m_tool->deleteLater();
                emit cancelled();
            });
    canvas->setActiveTool(m_tool);
}

NodePickSession::~NodePickSession()
{
    finish();
}

bool NodePickSession::isActive() const
{
    return !m_finished && m_tool && m_canvas && m_canvas->activeTool() == m_tool;
}

void NodePickSession::finish()
{
    if (m_finished) return;
    m_finished = true;   // before setActiveTool: its activeToolChanged must not read as a cancel
    if (m_canvas && m_tool && m_canvas->activeTool() == m_tool)
        m_canvas->setActiveTool(m_previous ? m_previous.data() : nullptr);
    if (m_tool) m_tool->deleteLater();
}
