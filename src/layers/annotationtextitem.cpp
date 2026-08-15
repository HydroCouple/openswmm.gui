/*!
 * \file   annotationtextitem.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "layers/annotationtextitem.h"

#include <QJsonObject>

namespace {

QString colorToString(const QColor &c)
{
    return c.name(QColor::HexArgb);
}

QColor colorFromString(const QString &s, const QColor &fallback)
{
    if (s.isEmpty()) return fallback;
    QColor c;
    c.setNamedColor(s);
    return c.isValid() ? c : fallback;
}

} // anonymous

AnnotationTextItem::AnnotationTextItem(QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_font.setPointSizeF(10.0);
}

QJsonObject AnnotationTextItem::toJson() const
{
    QJsonObject o;
    o["id"]       = m_id;
    o["text"]     = m_text;
    o["x"]        = m_x;
    o["y"]        = m_y;
    o["rotation"] = m_rotation;

    o["font"]     = m_font.toString();
    o["fill"]     = colorToString(m_fillColor);

    o["outlineEnabled"] = m_outlineEnabled;
    o["outlineColor"]   = colorToString(m_outlineColor);
    o["outlineWidth"]   = m_outlineWidth;

    o["haloEnabled"] = m_haloEnabled;
    o["haloColor"]   = colorToString(m_haloColor);
    o["haloRadius"]  = m_haloRadius;

    o["bgEnabled"]       = m_bgEnabled;
    o["bgFill"]          = colorToString(m_bgFill);
    o["bgOutline"]       = colorToString(m_bgOutline);
    o["bgOutlineWidth"]  = m_bgOutlineWidth;
    o["bgPadding"]       = m_bgPadding;
    o["bgCornerRadius"]  = m_bgCornerRadius;
    return o;
}

void AnnotationTextItem::fromJson(const QJsonObject &o)
{
    if (o.contains("id"))       m_id       = o.value("id").toString(m_id);
    if (o.contains("text"))     m_text     = o.value("text").toString(m_text);
    if (o.contains("x"))        m_x        = o.value("x").toDouble(m_x);
    if (o.contains("y"))        m_y        = o.value("y").toDouble(m_y);
    if (o.contains("rotation")) m_rotation = o.value("rotation").toDouble(m_rotation);

    if (o.contains("font")) {
        QFont f;
        if (f.fromString(o.value("font").toString()))
            m_font = f;
    }
    m_fillColor = colorFromString(o.value("fill").toString(), m_fillColor);

    m_outlineEnabled = o.value("outlineEnabled").toBool(m_outlineEnabled);
    m_outlineColor   = colorFromString(o.value("outlineColor").toString(), m_outlineColor);
    m_outlineWidth   = o.value("outlineWidth").toDouble(m_outlineWidth);

    m_haloEnabled = o.value("haloEnabled").toBool(m_haloEnabled);
    m_haloColor   = colorFromString(o.value("haloColor").toString(), m_haloColor);
    m_haloRadius  = o.value("haloRadius").toDouble(m_haloRadius);

    m_bgEnabled      = o.value("bgEnabled").toBool(m_bgEnabled);
    m_bgFill         = colorFromString(o.value("bgFill").toString(), m_bgFill);
    m_bgOutline      = colorFromString(o.value("bgOutline").toString(), m_bgOutline);
    m_bgOutlineWidth = o.value("bgOutlineWidth").toDouble(m_bgOutlineWidth);
    m_bgPadding      = o.value("bgPadding").toDouble(m_bgPadding);
    m_bgCornerRadius = o.value("bgCornerRadius").toDouble(m_bgCornerRadius);

    emit changed();
}

// ---------------------------------------------------------------------------
// Setters — every mutator emits a typed NOTIFY (for the QPropertyModel) and
// the aggregate changed() (for the layer's repaint hook). Identity-check
// guards avoid spurious repaints from non-mutating writes.
// ---------------------------------------------------------------------------

void AnnotationTextItem::setText(const QString &t)
{
    if (m_text == t) return;
    m_text = t;
    emit textChanged(m_text);
    emit changed();
}

void AnnotationTextItem::setX(double v)
{
    if (m_x == v) return;
    m_x = v;
    emit positionChanged();
    emit changed();
}

void AnnotationTextItem::setY(double v)
{
    if (m_y == v) return;
    m_y = v;
    emit positionChanged();
    emit changed();
}

void AnnotationTextItem::setPosition(double x, double y)
{
    if (m_x == x && m_y == y) return;
    m_x = x;
    m_y = y;
    emit positionChanged();
    emit changed();
}

void AnnotationTextItem::setRotation(double deg)
{
    if (m_rotation == deg) return;
    m_rotation = deg;
    emit rotationChanged(deg);
    emit changed();
}

void AnnotationTextItem::setFont(const QFont &f)
{
    if (m_font == f) return;
    m_font = f;
    emit fontChanged(f);
    emit changed();
}

void AnnotationTextItem::setFillColor(const QColor &c)
{
    if (m_fillColor == c) return;
    m_fillColor = c;
    emit fillColorChanged(c);
    emit changed();
}

void AnnotationTextItem::setOutlineEnabled(bool on)
{
    if (m_outlineEnabled == on) return;
    m_outlineEnabled = on;
    emit outlineEnabledChanged(on);
    emit changed();
}

void AnnotationTextItem::setOutlineColor(const QColor &c)
{
    if (m_outlineColor == c) return;
    m_outlineColor = c;
    emit outlineColorChanged(c);
    emit changed();
}

void AnnotationTextItem::setOutlineWidth(double w)
{
    if (m_outlineWidth == w) return;
    m_outlineWidth = w;
    emit outlineWidthChanged(w);
    emit changed();
}

void AnnotationTextItem::setHaloEnabled(bool on)
{
    if (m_haloEnabled == on) return;
    m_haloEnabled = on;
    emit haloEnabledChanged(on);
    emit changed();
}

void AnnotationTextItem::setHaloColor(const QColor &c)
{
    if (m_haloColor == c) return;
    m_haloColor = c;
    emit haloColorChanged(c);
    emit changed();
}

void AnnotationTextItem::setHaloRadius(double r)
{
    if (m_haloRadius == r) return;
    m_haloRadius = r;
    emit haloRadiusChanged(r);
    emit changed();
}

void AnnotationTextItem::setBackgroundEnabled(bool on)
{
    if (m_bgEnabled == on) return;
    m_bgEnabled = on;
    emit backgroundEnabledChanged(on);
    emit changed();
}

void AnnotationTextItem::setBackgroundFillColor(const QColor &c)
{
    if (m_bgFill == c) return;
    m_bgFill = c;
    emit backgroundFillColorChanged(c);
    emit changed();
}

void AnnotationTextItem::setBackgroundOutlineColor(const QColor &c)
{
    if (m_bgOutline == c) return;
    m_bgOutline = c;
    emit backgroundOutlineColorChanged(c);
    emit changed();
}

void AnnotationTextItem::setBackgroundOutlineWidth(double w)
{
    if (m_bgOutlineWidth == w) return;
    m_bgOutlineWidth = w;
    emit backgroundOutlineWidthChanged(w);
    emit changed();
}

void AnnotationTextItem::setBackgroundPadding(double p)
{
    if (m_bgPadding == p) return;
    m_bgPadding = p;
    emit backgroundPaddingChanged(p);
    emit changed();
}

void AnnotationTextItem::setBackgroundCornerRadius(double r)
{
    if (m_bgCornerRadius == r) return;
    m_bgCornerRadius = r;
    emit backgroundCornerRadiusChanged(r);
    emit changed();
}
