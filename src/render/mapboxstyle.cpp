/**
 * @file      mapboxstyle.cpp
 * @brief     Box settings shared by the map legend and the animation date/time box.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "render/mapboxstyle.h"

#include <QHash>
#include <QJsonValue>

namespace OpenSWMM::Render
{

// Structured font fields: QFont::toString/fromString is brittle when the family is empty (the
// default application font before QGuiApplication starts on macOS).
QJsonObject overlayFontToJson(const QFont &f)
{
    QJsonObject j;
    j[QStringLiteral("family")] = f.family();
    j[QStringLiteral("pointSize")] = f.pointSizeF();
    j[QStringLiteral("weight")] = int(f.weight());
    j[QStringLiteral("italic")] = f.italic();
    j[QStringLiteral("underline")] = f.underline();
    j[QStringLiteral("strikeOut")] = f.strikeOut();
    return j;
}

QFont overlayFontFromJson(const QJsonObject &j, const QFont &fallback)
{
    QFont f = fallback;
    if (j.contains(QStringLiteral("family")))
        f.setFamily(j.value(QStringLiteral("family")).toString());
    if (j.contains(QStringLiteral("pointSize"))) {
        const double ps = j.value(QStringLiteral("pointSize")).toDouble(-1.0);
        if (ps > 0.0)
            f.setPointSizeF(ps);
    }
    if (j.contains(QStringLiteral("weight")))
        f.setWeight(QFont::Weight(j.value(QStringLiteral("weight")).toInt(int(f.weight()))));
    if (j.contains(QStringLiteral("italic")))
        f.setItalic(j.value(QStringLiteral("italic")).toBool());
    if (j.contains(QStringLiteral("underline")))
        f.setUnderline(j.value(QStringLiteral("underline")).toBool());
    if (j.contains(QStringLiteral("strikeOut")))
        f.setStrikeOut(j.value(QStringLiteral("strikeOut")).toBool());
    return f;
}

MapBoxStyle::MapBoxStyle(Anchor defaultAnchor, QObject *parent)
    : QObject(parent), m_defaultAnchor(defaultAnchor), m_anchor(defaultAnchor)
{
}

QString MapBoxStyle::displayLabelFor(const QString &propertyName) const
{
    static const QHash<QString, QString> labels = {
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
    return labels.value(propertyName, propertyName);
}

void MapBoxStyle::resetBoxToDefaults()
{
    setPadding(8);
    setAnchor(m_defaultAnchor);
    setOpacity(1.0);
    setFreePosition(QPoint());
    setShowFrame(true);
    setFrameColor(QColor(80, 80, 80, 180));
    setFrameWidth(1.0);
    setCornerRadius(6);
    setBackgroundMode(BackgroundMode::Solid);
    setBackgroundColor(QColor(255, 255, 255, 225));
    setGradientEndColor(QColor(240, 240, 240, 225));
    setGradientOrientation(Qt::Vertical);
}

void MapBoxStyle::writeBoxJson(QJsonObject &j) const
{
    j[QStringLiteral("padding")] = m_padding;
    j[QStringLiteral("anchor")] = mapBoxEnumToString(m_anchor);
    j[QStringLiteral("opacity")] = m_opacity;
    j[QStringLiteral("freeX")] = m_freePosition.x();
    j[QStringLiteral("freeY")] = m_freePosition.y();
    j[QStringLiteral("showFrame")] = m_showFrame;
    j[QStringLiteral("frameColor")] = m_frameColor.name(QColor::HexArgb);
    j[QStringLiteral("frameWidth")] = m_frameWidth;
    j[QStringLiteral("cornerRadius")] = m_cornerRadius;
    j[QStringLiteral("backgroundMode")] = mapBoxEnumToString(m_backgroundMode);
    j[QStringLiteral("backgroundColor")] = m_backgroundColor.name(QColor::HexArgb);
    j[QStringLiteral("gradientEndColor")] = m_gradientEndColor.name(QColor::HexArgb);
    j[QStringLiteral("gradientOrientation")] = m_gradientOrientation == Qt::Horizontal
                                                   ? QStringLiteral("Horizontal")
                                                   : QStringLiteral("Vertical");
}

void MapBoxStyle::readBoxJson(const QJsonObject &j)
{
    const auto has = [&j](const char *key) { return j.contains(QLatin1String(key)); };
    const auto value = [&j](const char *key) { return j.value(QLatin1String(key)); };
    if (has("padding"))
        setPadding(value("padding").toInt(m_padding));
    if (has("anchor"))
        setAnchor(mapBoxEnumFromString<Anchor>(value("anchor").toString(), m_anchor));
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
        setBackgroundMode(mapBoxEnumFromString<BackgroundMode>(value("backgroundMode").toString(),
                                                               m_backgroundMode));
    if (has("backgroundColor"))
        setBackgroundColor(QColor(value("backgroundColor").toString()));
    if (has("gradientEndColor"))
        setGradientEndColor(QColor(value("gradientEndColor").toString()));
    if (has("gradientOrientation"))
        setGradientOrientation(
            value("gradientOrientation").toString() == QLatin1String("Horizontal") ? Qt::Horizontal
                                                                                   : Qt::Vertical);
}

// Each setter: ignore an equal value, store, emit its own signal, then changed().
#define MAPBOX_SETTER(Type, name, member, signal)                                                  \
    void MapBoxStyle::set##name(Type v)                                                            \
    {                                                                                              \
        if (member == v)                                                                           \
            return;                                                                                \
        member = v;                                                                                \
        emit signal(v);                                                                            \
        emit changed();                                                                            \
    }

MAPBOX_SETTER(int, Padding, m_padding, paddingChanged)
MAPBOX_SETTER(MapBoxStyle::Anchor, Anchor, m_anchor, anchorChanged)
MAPBOX_SETTER(qreal, Opacity, m_opacity, opacityChanged)
MAPBOX_SETTER(const QPoint &, FreePosition, m_freePosition, freePositionChanged)
MAPBOX_SETTER(bool, ShowFrame, m_showFrame, showFrameChanged)
MAPBOX_SETTER(const QColor &, FrameColor, m_frameColor, frameColorChanged)
MAPBOX_SETTER(qreal, FrameWidth, m_frameWidth, frameWidthChanged)
MAPBOX_SETTER(int, CornerRadius, m_cornerRadius, cornerRadiusChanged)
MAPBOX_SETTER(MapBoxStyle::BackgroundMode, BackgroundMode, m_backgroundMode, backgroundModeChanged)
MAPBOX_SETTER(const QColor &, BackgroundColor, m_backgroundColor, backgroundColorChanged)
MAPBOX_SETTER(const QColor &, GradientEndColor, m_gradientEndColor, gradientEndColorChanged)
MAPBOX_SETTER(Qt::Orientation, GradientOrientation, m_gradientOrientation,
              gradientOrientationChanged)

#undef MAPBOX_SETTER

} // namespace OpenSWMM::Render
