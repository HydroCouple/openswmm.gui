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

#include <algorithm>

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
        {QStringLiteral("anchor"), QStringLiteral("General — Anchor")},
        {QStringLiteral("anchorMarginX"), QStringLiteral("General — Anchor margin X")},
        {QStringLiteral("anchorMarginY"), QStringLiteral("General — Anchor margin Y")},
        {QStringLiteral("opacity"), QStringLiteral("General — Opacity")},
        {QStringLiteral("marginLeft"), QStringLiteral("Margins — Left")},
        {QStringLiteral("marginTop"), QStringLiteral("Margins — Top")},
        {QStringLiteral("marginRight"), QStringLiteral("Margins — Right")},
        {QStringLiteral("marginBottom"), QStringLiteral("Margins — Bottom")},
        {QStringLiteral("showFrame"), QStringLiteral("Frame — Show frame")},
        {QStringLiteral("frameColor"), QStringLiteral("Frame — Color")},
        {QStringLiteral("frameWidth"), QStringLiteral("Frame — Width")},
        {QStringLiteral("frameStyle"), QStringLiteral("Frame — Line style")},
        {QStringLiteral("cornerRadius"), QStringLiteral("Frame — Corner radius")},
        {QStringLiteral("backgroundMode"), QStringLiteral("Background — Mode")},
        {QStringLiteral("backgroundColor"), QStringLiteral("Background — Color")},
        {QStringLiteral("gradientEndColor"), QStringLiteral("Background — Gradient end color")},
        {QStringLiteral("gradientOrientation"),
         QStringLiteral("Background — Gradient orientation")},
        {QStringLiteral("showShadow"), QStringLiteral("Shadow — Show shadow")},
        {QStringLiteral("shadowColor"), QStringLiteral("Shadow — Color")},
        {QStringLiteral("shadowOffsetX"), QStringLiteral("Shadow — Offset X")},
        {QStringLiteral("shadowOffsetY"), QStringLiteral("Shadow — Offset Y")},
    };
    return labels.value(propertyName, propertyName);
}

QString MapBoxStyle::propertyGroupFor(const QString &propertyName) const
{
    // The group is the part of the display label before " — ".
    const QString label = displayLabelFor(propertyName);
    const int dash = label.indexOf(QStringLiteral(" — "));
    return dash > 0 ? label.left(dash) : QString();
}

void MapBoxStyle::resetBoxToDefaults()
{
    setAnchor(m_defaultAnchor);
    setAnchorMarginX(12);
    setAnchorMarginY(12);
    setOpacity(1.0);
    setFreePosition(QPoint());
    setPadding(8);
    setShowFrame(true);
    setFrameColor(QColor(80, 80, 80, 180));
    setFrameWidth(1.0);
    setFrameStyle(FrameStyle::Solid);
    setCornerRadius(6);
    setBackgroundMode(BackgroundMode::Solid);
    setBackgroundColor(QColor(255, 255, 255, 225));
    setGradientEndColor(QColor(240, 240, 240, 225));
    setGradientOrientation(Qt::Vertical);
    setShowShadow(false);
    setShadowColor(QColor(0, 0, 0, 90));
    setShadowOffsetX(3);
    setShadowOffsetY(3);
}

void MapBoxStyle::writeBoxJson(QJsonObject &j) const
{
    j[QStringLiteral("anchor")] = mapBoxEnumToString(m_anchor);
    j[QStringLiteral("anchorMarginX")] = m_anchorMarginX;
    j[QStringLiteral("anchorMarginY")] = m_anchorMarginY;
    j[QStringLiteral("opacity")] = m_opacity;
    j[QStringLiteral("freeX")] = m_freePosition.x();
    j[QStringLiteral("freeY")] = m_freePosition.y();
    j[QStringLiteral("padding")] = m_marginLeft; // read by builds before the four margins
    j[QStringLiteral("marginLeft")] = m_marginLeft;
    j[QStringLiteral("marginTop")] = m_marginTop;
    j[QStringLiteral("marginRight")] = m_marginRight;
    j[QStringLiteral("marginBottom")] = m_marginBottom;
    j[QStringLiteral("showFrame")] = m_showFrame;
    j[QStringLiteral("frameColor")] = m_frameColor.name(QColor::HexArgb);
    j[QStringLiteral("frameWidth")] = m_frameWidth;
    j[QStringLiteral("frameStyle")] = mapBoxEnumToString(m_frameStyle);
    j[QStringLiteral("cornerRadius")] = m_cornerRadius;
    j[QStringLiteral("backgroundMode")] = mapBoxEnumToString(m_backgroundMode);
    j[QStringLiteral("backgroundColor")] = m_backgroundColor.name(QColor::HexArgb);
    j[QStringLiteral("gradientEndColor")] = m_gradientEndColor.name(QColor::HexArgb);
    j[QStringLiteral("gradientOrientation")] = m_gradientOrientation == Qt::Horizontal
                                                   ? QStringLiteral("Horizontal")
                                                   : QStringLiteral("Vertical");
    j[QStringLiteral("showShadow")] = m_showShadow;
    j[QStringLiteral("shadowColor")] = m_shadowColor.name(QColor::HexArgb);
    j[QStringLiteral("shadowOffsetX")] = m_shadowOffsetX;
    j[QStringLiteral("shadowOffsetY")] = m_shadowOffsetY;
}

void MapBoxStyle::readBoxJson(const QJsonObject &j)
{
    const auto has = [&j](const char *key) { return j.contains(QLatin1String(key)); };
    const auto value = [&j](const char *key) { return j.value(QLatin1String(key)); };
    if (has("anchor"))
        setAnchor(mapBoxEnumFromString<Anchor>(value("anchor").toString(), m_anchor));
    if (has("anchorMarginX"))
        setAnchorMarginX(value("anchorMarginX").toInt(m_anchorMarginX));
    if (has("anchorMarginY"))
        setAnchorMarginY(value("anchorMarginY").toInt(m_anchorMarginY));
    if (has("opacity"))
        setOpacity(value("opacity").toDouble(m_opacity));
    if (has("freeX") || has("freeY"))
        setFreePosition(QPoint(value("freeX").toInt(m_freePosition.x()),
                               value("freeY").toInt(m_freePosition.y())));
    // An older file has one padding for all sides; the four margins win when present.
    if (has("padding"))
        setPadding(value("padding").toInt(m_marginLeft));
    if (has("marginLeft"))
        setMarginLeft(value("marginLeft").toInt(m_marginLeft));
    if (has("marginTop"))
        setMarginTop(value("marginTop").toInt(m_marginTop));
    if (has("marginRight"))
        setMarginRight(value("marginRight").toInt(m_marginRight));
    if (has("marginBottom"))
        setMarginBottom(value("marginBottom").toInt(m_marginBottom));
    if (has("showFrame"))
        setShowFrame(value("showFrame").toBool());
    if (has("frameColor"))
        setFrameColor(QColor(value("frameColor").toString()));
    if (has("frameWidth"))
        setFrameWidth(value("frameWidth").toDouble(m_frameWidth));
    if (has("frameStyle"))
        setFrameStyle(
            mapBoxEnumFromString<FrameStyle>(value("frameStyle").toString(), m_frameStyle));
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
    if (has("showShadow"))
        setShowShadow(value("showShadow").toBool());
    if (has("shadowColor"))
        setShadowColor(QColor(value("shadowColor").toString()));
    if (has("shadowOffsetX"))
        setShadowOffsetX(value("shadowOffsetX").toInt(m_shadowOffsetX));
    if (has("shadowOffsetY"))
        setShadowOffsetY(value("shadowOffsetY").toInt(m_shadowOffsetY));
}

void MapBoxStyle::setPadding(int v)
{
    setMarginLeft(v);
    setMarginTop(v);
    setMarginRight(v);
    setMarginBottom(v);
}

// Each setter: clamp, ignore an equal value, store, emit its own signal, then changed().
#define MAPBOX_SETTER(Type, name, member, signal, clampExpr)                                       \
    void MapBoxStyle::set##name(Type v)                                                            \
    {                                                                                              \
        const auto value = clampExpr;                                                              \
        if (member == value)                                                                       \
            return;                                                                                \
        member = value;                                                                            \
        emit signal(member);                                                                       \
        emit changed();                                                                            \
    }

MAPBOX_SETTER(MapBoxStyle::Anchor, Anchor, m_anchor, anchorChanged, v)
MAPBOX_SETTER(int, AnchorMarginX, m_anchorMarginX, anchorMarginXChanged, std::clamp(v, 0, 200))
MAPBOX_SETTER(int, AnchorMarginY, m_anchorMarginY, anchorMarginYChanged, std::clamp(v, 0, 200))
MAPBOX_SETTER(qreal, Opacity, m_opacity, opacityChanged, std::clamp<qreal>(v, 0.0, 1.0))
MAPBOX_SETTER(const QPoint &, FreePosition, m_freePosition, freePositionChanged, v)
MAPBOX_SETTER(int, MarginLeft, m_marginLeft, marginLeftChanged, std::clamp(v, 0, 200))
MAPBOX_SETTER(int, MarginTop, m_marginTop, marginTopChanged, std::clamp(v, 0, 200))
MAPBOX_SETTER(int, MarginRight, m_marginRight, marginRightChanged, std::clamp(v, 0, 200))
MAPBOX_SETTER(int, MarginBottom, m_marginBottom, marginBottomChanged, std::clamp(v, 0, 200))
MAPBOX_SETTER(bool, ShowFrame, m_showFrame, showFrameChanged, v)
MAPBOX_SETTER(const QColor &, FrameColor, m_frameColor, frameColorChanged, v)
MAPBOX_SETTER(qreal, FrameWidth, m_frameWidth, frameWidthChanged, std::clamp<qreal>(v, 0.0, 20.0))
MAPBOX_SETTER(MapBoxStyle::FrameStyle, FrameStyle, m_frameStyle, frameStyleChanged, v)
MAPBOX_SETTER(int, CornerRadius, m_cornerRadius, cornerRadiusChanged, std::clamp(v, 0, 100))
MAPBOX_SETTER(MapBoxStyle::BackgroundMode, BackgroundMode, m_backgroundMode, backgroundModeChanged,
              v)
MAPBOX_SETTER(const QColor &, BackgroundColor, m_backgroundColor, backgroundColorChanged, v)
MAPBOX_SETTER(const QColor &, GradientEndColor, m_gradientEndColor, gradientEndColorChanged, v)
MAPBOX_SETTER(Qt::Orientation, GradientOrientation, m_gradientOrientation,
              gradientOrientationChanged, v)
MAPBOX_SETTER(bool, ShowShadow, m_showShadow, showShadowChanged, v)
MAPBOX_SETTER(const QColor &, ShadowColor, m_shadowColor, shadowColorChanged, v)
MAPBOX_SETTER(int, ShadowOffsetX, m_shadowOffsetX, shadowOffsetXChanged, std::clamp(v, -20, 20))
MAPBOX_SETTER(int, ShadowOffsetY, m_shadowOffsetY, shadowOffsetYChanged, std::clamp(v, -20, 20))

#undef MAPBOX_SETTER

} // namespace OpenSWMM::Render
