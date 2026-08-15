/*!
 * \file   profilesourcestyleadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/profilesourcestyleadapter.h"

#include "layers/swmmresultslayer.h"

ProfileSourceStyleAdapter::ProfileSourceStyleAdapter(QObject *parent)
    : QObject(parent)
{}

void ProfileSourceStyleAdapter::setLayer(SWMMResultsLayer *layer)
{
    if (m_layer.data() == layer) return;
    if (m_layer)
        disconnect(m_layer.data(), nullptr, this, nullptr);
    m_layer = layer;
    if (m_layer) {
        // Forward both signals as a single `changed()` so QPropertyModel can
        // refresh in one pass.  No payload — the model re-reads all rows.
        connect(m_layer.data(), &SWMMResultsLayer::profileStyleChanged,
                this, &ProfileSourceStyleAdapter::changed);
        connect(m_layer.data(), &SWMMResultsLayer::profileLineColorChanged,
                this, &ProfileSourceStyleAdapter::changed);
    }
    emit changed();
}

// ── Getters ────────────────────────────────────────────────────────────────

QColor ProfileSourceStyleAdapter::lineColor() const
{
    return m_layer ? m_layer->profileLineColor() : QColor();
}
QPen ProfileSourceStyleAdapter::hglLinePen() const
{
    return m_layer ? m_layer->profileHglLinePen() : QPen();
}
QBrush ProfileSourceStyleAdapter::hglFillBrush() const
{
    return m_layer ? m_layer->profileHglFillBrush() : QBrush();
}
QPen ProfileSourceStyleAdapter::eglLinePen() const
{
    return m_layer ? m_layer->profileEglLinePen() : QPen();
}
QPen ProfileSourceStyleAdapter::maxHglLinePen() const
{
    return m_layer ? m_layer->profileMaxHglLinePen() : QPen();
}
QBrush ProfileSourceStyleAdapter::maxHglFillBrush() const
{
    return m_layer ? m_layer->profileMaxHglFillBrush() : QBrush();
}
QPen ProfileSourceStyleAdapter::maxEglLinePen() const
{
    return m_layer ? m_layer->profileMaxEglLinePen() : QPen();
}

// ── Setters ────────────────────────────────────────────────────────────────

void ProfileSourceStyleAdapter::setLineColor(const QColor &v)
{
    if (m_layer) m_layer->setProfileLineColor(v);
}
void ProfileSourceStyleAdapter::setHglLinePen(const QPen &v)
{
    if (m_layer) m_layer->setProfileHglLinePen(v);
}
void ProfileSourceStyleAdapter::setHglFillBrush(const QBrush &v)
{
    if (m_layer) m_layer->setProfileHglFillBrush(v);
}
void ProfileSourceStyleAdapter::setEglLinePen(const QPen &v)
{
    if (m_layer) m_layer->setProfileEglLinePen(v);
}
void ProfileSourceStyleAdapter::setMaxHglLinePen(const QPen &v)
{
    if (m_layer) m_layer->setProfileMaxHglLinePen(v);
}
void ProfileSourceStyleAdapter::setMaxHglFillBrush(const QBrush &v)
{
    if (m_layer) m_layer->setProfileMaxHglFillBrush(v);
}
void ProfileSourceStyleAdapter::setMaxEglLinePen(const QPen &v)
{
    if (m_layer) m_layer->setProfileMaxEglLinePen(v);
}
