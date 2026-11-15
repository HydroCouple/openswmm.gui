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
#include <QMetaEnum>

namespace OpenSWMM::Render
{

namespace
{

/** @brief Enum value for a saved key name, or @p fallback when the name is unknown. */
template <typename Enum> Enum enumFromString(const QString &name, Enum fallback)
{
    bool ok = false;
    const int v = QMetaEnum::fromType<Enum>().keyToValue(name.toUtf8().constData(), &ok);
    return ok ? static_cast<Enum>(v) : fallback;
}

/** @brief Key name saved for an enum value. */
template <typename Enum> QString enumToString(Enum v)
{
    return QString::fromLatin1(QMetaEnum::fromType<Enum>().valueToKey(static_cast<int>(v)));
}

} // namespace

TimeOverlayStyle::TimeOverlayStyle(QObject *parent) : QObject(parent) {}

QString TimeOverlayStyle::displayLabelFor(const QString &propertyName) const
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("format"), QStringLiteral("General — Date/time format")},
        {QStringLiteral("font"), QStringLiteral("General — Font")},
        {QStringLiteral("textColor"), QStringLiteral("General — Text color")},
        {QStringLiteral("padding"), QStringLiteral("General — Padding")},
        {QStringLiteral("anchor"), QStringLiteral("General — Anchor")},
        {QStringLiteral("opacity"), QStringLiteral("General — Opacity")},
        {QStringLiteral("showFrame"), QStringLiteral("Frame — Show frame")},
        {QStringLiteral("frameColor"), QStringLiteral("Frame — Color")},
        {QStringLiteral("frameWidth"), QStringLiteral("Frame — Width")},
        {QStringLiteral("cornerRadius"), QStringLiteral("Frame — Corner radius")},
        {QStringLiteral("backgroundMode"), QStringLiteral("Background — Mode")},
        {QStringLiteral("backgroundColor"), QStringLiteral("Background — Color")},
        {QStringLiteral("gradientEndColor"), QStringLiteral("Background — Gradient end color")},
        {QStringLiteral("gradientOrientation"),
         QStringLiteral("Background — Gradient orientation")},
    };
    const auto it = labels.constFind(propertyName);
    return it != labels.constEnd() ? it.value() : propertyName;
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
    j[QStringLiteral("shown")] = m_shown;
    j[QStringLiteral("format")] = m_format;
    j[QStringLiteral("font")] = overlayFontToJson(m_font);
    j[QStringLiteral("textColor")] = m_textColor.name(QColor::HexArgb);
    j[QStringLiteral("padding")] = m_padding;
    j[QStringLiteral("anchor")] = enumToString(m_anchor);
    j[QStringLiteral("opacity")] = m_opacity;
    j[QStringLiteral("freeX")] = m_freePosition.x();
    j[QStringLiteral("freeY")] = m_freePosition.y();
    j[QStringLiteral("showFrame")] = m_showFrame;
    j[QStringLiteral("frameColor")] = m_frameColor.name(QColor::HexArgb);
    j[QStringLiteral("frameWidth")] = m_frameWidth;
    j[QStringLiteral("cornerRadius")] = m_cornerRadius;
    j[QStringLiteral("backgroundMode")] = enumToString(m_backgroundMode);
    j[QStringLiteral("backgroundColor")] = m_backgroundColor.name(QColor::HexArgb);
    j[QStringLiteral("gradientEndColor")] = m_gradientEndColor.name(QColor::HexArgb);
    j[QStringLiteral("gradientOrientation")] = m_gradientOrientation == Qt::Horizontal
                                                   ? QStringLiteral("Horizontal")
                                                   : QStringLiteral("Vertical");
    return j;
}

void TimeOverlayStyle::fromJson(const QJsonObject &j)
{
    auto has = [&j](const char *key) { return j.contains(QLatin1String(key)); };
    auto value = [&j](const char *key) { return j.value(QLatin1String(key)); };
    if (has("format"))
        setFormat(value("format").toString());
    if (has("font"))
        setFont(overlayFontFromJson(value("font").toObject(), m_font));
    if (has("textColor"))
        setTextColor(QColor(value("textColor").toString()));
    if (has("padding"))
        setPadding(value("padding").toInt(m_padding));
    if (has("anchor"))
        setAnchor(enumFromString<Anchor>(value("anchor").toString(), m_anchor));
    if (has("opacity"))
        setOpacity(value("opacity").toDouble(m_opacity));
    if (has("freeX") || has("freeY"))
        setFreePosition(QPoint(value("freeX").toInt(m_freePosition.x()),
                               value("freeY").toInt(m_freePosition.y())));
    if (has("showFrame"))
        setShowFrame(value("showFrame").toBool());
    if (has("frameColor"))
        setFrameColor(QColor(value("frameColor").toString()));
    if (has("frameWidth"))
        setFrameWidth(value("frameWidth").toDouble(m_frameWidth));
    if (has("cornerRadius"))
        setCornerRadius(value("cornerRadius").toInt(m_cornerRadius));
    if (has("backgroundMode"))
        setBackgroundMode(
            enumFromString<BackgroundMode>(value("backgroundMode").toString(), m_backgroundMode));
    if (has("backgroundColor"))
        setBackgroundColor(QColor(value("backgroundColor").toString()));
    if (has("gradientEndColor"))
        setGradientEndColor(QColor(value("gradientEndColor").toString()));
    if (has("gradientOrientation"))
        setGradientOrientation(
            value("gradientOrientation").toString() == QLatin1String("Horizontal") ? Qt::Horizontal
                                                                                   : Qt::Vertical);
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
SETTER(int, Padding, m_padding, paddingChanged)
SETTER(LegendOverlayStyle::Anchor, Anchor, m_anchor, anchorChanged)
SETTER(qreal, Opacity, m_opacity, opacityChanged)
SETTER(const QPoint &, FreePosition, m_freePosition, freePositionChanged)
SETTER(bool, ShowFrame, m_showFrame, showFrameChanged)
SETTER(const QColor &, FrameColor, m_frameColor, frameColorChanged)
SETTER(qreal, FrameWidth, m_frameWidth, frameWidthChanged)
SETTER(int, CornerRadius, m_cornerRadius, cornerRadiusChanged)
SETTER(LegendOverlayStyle::BackgroundMode, BackgroundMode, m_backgroundMode, backgroundModeChanged)
SETTER(const QColor &, BackgroundColor, m_backgroundColor, backgroundColorChanged)
SETTER(const QColor &, GradientEndColor, m_gradientEndColor, gradientEndColorChanged)
SETTER(Qt::Orientation, GradientOrientation, m_gradientOrientation, gradientOrientationChanged)

#undef SETTER

} // namespace OpenSWMM::Render
