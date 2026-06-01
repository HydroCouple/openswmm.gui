/*!
 * \file   graduatedclass.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "render/graduatedclass.h"

namespace OpenSWMM::Render
{

GraduatedClass::GraduatedClass(QObject *parent)
    : QObject(parent)
{
}

GraduatedClass::GraduatedClass(double minValue, double maxValue, QColor color,
                                QString label, QObject *parent)
    : QObject(parent)
    , m_color(std::move(color))
    , m_minValue(minValue)
    , m_maxValue(maxValue)
    , m_label(std::move(label))
{
}

void GraduatedClass::setColor(const QColor &c)
{
    if (!c.isValid() || m_color == c) return;
    m_color = c;
    emit colorChanged(m_color);
    emit changed();
}

void GraduatedClass::setMinValue(double v)
{
    if (m_minValue == v) return;
    m_minValue = v;
    emit minValueChanged(m_minValue);
    emit changed();
}

void GraduatedClass::setMaxValue(double v)
{
    if (m_maxValue == v) return;
    m_maxValue = v;
    emit maxValueChanged(m_maxValue);
    emit changed();
}

void GraduatedClass::setLabel(const QString &l)
{
    if (m_label == l) return;
    m_label = l;
    emit labelChanged(m_label);
    emit changed();
}

void GraduatedClass::setVisible(bool v)
{
    if (m_visible == v) return;
    m_visible = v;
    emit visibleChanged(m_visible);
    emit changed();
}

bool GraduatedClass::contains(double v, bool inclusiveUpper) const
{
    if (v < m_minValue) return false;
    return inclusiveUpper ? (v <= m_maxValue) : (v < m_maxValue);
}

QJsonObject GraduatedClass::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("color"),    m_color.name(QColor::HexArgb));
    o.insert(QStringLiteral("min"),      m_minValue);
    o.insert(QStringLiteral("max"),      m_maxValue);
    o.insert(QStringLiteral("label"),    m_label);
    o.insert(QStringLiteral("visible"),  m_visible);
    return o;
}

GraduatedClass *GraduatedClass::fromJson(const QJsonObject &j, QObject *parent)
{
    auto *gc = new GraduatedClass(parent);
    gc->m_color    = QColor(j.value(QStringLiteral("color")).toString(QStringLiteral("#808080")));
    if (!gc->m_color.isValid()) gc->m_color = Qt::gray;
    gc->m_minValue = j.value(QStringLiteral("min")).toDouble(0.0);
    gc->m_maxValue = j.value(QStringLiteral("max")).toDouble(1.0);
    gc->m_label    = j.value(QStringLiteral("label")).toString();
    gc->m_visible  = j.value(QStringLiteral("visible")).toBool(true);
    return gc;
}

} // namespace OpenSWMM::Render
