/**
 * @file      timeoverlaystyle.cpp
 * @brief     Model for the map's animation date/time box.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "render/timeoverlaystyle.h"

#include <QDateTime>
#include <QHash>
#include <QJsonValue>

namespace OpenSWMM::Render
{

TimeOverlayStyle::TimeOverlayStyle(QObject *parent) : MapBoxStyle(Anchor::TopLeft, parent) {}

QString TimeOverlayStyle::displayLabelFor(const QString &propertyName) const
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("format"), QStringLiteral("General — Date/time format")},
        {QStringLiteral("font"), QStringLiteral("General — Font")},
        {QStringLiteral("textColor"), QStringLiteral("General — Text color")},
    };
    const auto it = labels.constFind(propertyName);
    return it != labels.constEnd() ? it.value() : MapBoxStyle::displayLabelFor(propertyName);
}

void TimeOverlayStyle::resetToDefaults()
{
    const TimeOverlayStyle defaults;
    QJsonObject j = defaults.toJson();
    j.remove(QStringLiteral("shown")); // Reset restyles the box; it does not hide it.
    fromJson(j);
}

QJsonObject TimeOverlayStyle::toJson() const
{
    QJsonObject j;
    writeBoxJson(j);
    j[QStringLiteral("shown")] = m_shown;
    j[QStringLiteral("format")] = m_format;
    j[QStringLiteral("font")] = overlayFontToJson(m_font);
    j[QStringLiteral("textColor")] = m_textColor.name(QColor::HexArgb);
    return j;
}

void TimeOverlayStyle::fromJson(const QJsonObject &j)
{
    auto has = [&j](const char *key) { return j.contains(QLatin1String(key)); };
    auto value = [&j](const char *key) { return j.value(QLatin1String(key)); };
    readBoxJson(j);
    if (has("format"))
        setFormat(value("format").toString());
    if (has("font"))
        setFont(overlayFontFromJson(value("font").toObject(), m_font));
    if (has("textColor"))
        setTextColor(QColor(value("textColor").toString()));
    // Last, so a restored box appears already styled and placed.
    if (has("shown"))
        setShown(value("shown").toBool());
}

QString TimeOverlayStyle::textFor(const QDateTime &dt) const
{
    if (!dt.isValid())
        return {};
    return dt.toUTC().toString(m_format.isEmpty() ? QString::fromLatin1(kDefaultFormat) : m_format);
}

// Each setter: no-op when equal; otherwise store, emit the property's signal, then changed().
#define SETTER(Type, name, member, signal)                                                         \
    void TimeOverlayStyle::set##name(Type v)                                                       \
    {                                                                                              \
        if (member == v)                                                                           \
            return;                                                                                \
        member = v;                                                                                \
        emit signal(v);                                                                            \
        emit changed();                                                                            \
    }

SETTER(bool, Shown, m_shown, shownChanged)
SETTER(const QString &, Format, m_format, formatChanged)
SETTER(const QFont &, Font, m_font, fontChanged)
SETTER(const QColor &, TextColor, m_textColor, textColorChanged)

#undef SETTER

} // namespace OpenSWMM::Render
