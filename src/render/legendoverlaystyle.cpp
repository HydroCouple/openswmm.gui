/**
 * @file      legendoverlaystyle.cpp
 * @brief     The map legend's look and per-row overrides.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "render/legendoverlaystyle.h"

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>

namespace OpenSWMM::Render
{

LegendOverlayStyle::LegendOverlayStyle(QObject *parent) : MapBoxStyle(Anchor::BottomRight, parent)
{
    // Default-constructed fonts resolve to the application font, so the legend follows the
    // user's system theme.
}

QString LegendOverlayStyle::itemKey(const QObject *layer, const QString &)
{
    // Only the layer half; itemOverride() joins it with the class key. Taking a QObject keeps
    // this file free of layer classes (the unit test links only Qt).
    return layer ? layer->objectName() : QString{};
}

namespace
{
/** @brief Join a layer key and a class key with '|' (not escaped).
 * @param layerKey Layer identity. @param classKey Row key.
 * @return "layerKey|classKey". */
QString combinedKey(const QString &layerKey, const QString &classKey)
{
    return layerKey + QLatin1Char('|') + classKey;
}
} // namespace

LegendOverlayStyle::ItemOverride LegendOverlayStyle::itemOverride(const QString &layerKey,
                                                                  const QString &classKey) const
{
    return m_itemOverrides.value(combinedKey(layerKey, classKey), ItemOverride{});
}

void LegendOverlayStyle::setItemOverride(const QString &layerKey, const QString &classKey,
                                         const ItemOverride &override)
{
    const QString k = combinedKey(layerKey, classKey);
    // A default-shaped override (visible, no label) is no override at all.
    if (override.visible && override.userLabel.isEmpty()) {
        if (m_itemOverrides.remove(k) == 0)
            return;
    } else {
        const auto it = m_itemOverrides.constFind(k);
        if (it != m_itemOverrides.constEnd() && it.value().visible == override.visible &&
            it.value().userLabel == override.userLabel)
            return;
        m_itemOverrides.insert(k, override);
    }
    emit itemOverrideChanged(layerKey, classKey);
    emit changed();
}

void LegendOverlayStyle::setItemVisible(const QString &layerKey, const QString &classKey,
                                        bool visible)
{
    ItemOverride o = itemOverride(layerKey, classKey);
    o.visible = visible;
    setItemOverride(layerKey, classKey, o);
}

void LegendOverlayStyle::setItemUserLabel(const QString &layerKey, const QString &classKey,
                                          const QString &label)
{
    ItemOverride o = itemOverride(layerKey, classKey);
    o.userLabel = label;
    setItemOverride(layerKey, classKey, o);
}

void LegendOverlayStyle::clearItemOverrides()
{
    if (m_itemOverrides.isEmpty())
        return;
    m_itemOverrides.clear();
    emit changed();
}

void LegendOverlayStyle::resetToDefaults()
{
    setShowTitle(false);
    setTitle({});
    setTitleFont(QFont{});
    setTitleColor(QColor(20, 20, 20));
    setItemFont(QFont{});
    setItemColor(QColor(20, 20, 20));
    setLayerHeaderFont(QFont{});
    setLayerHeaderColor(QColor(20, 20, 20));
    setRowSpacing(2);
    setSwatchSize(14);
    setExplicitWidth(0);
    setExplicitHeight(0);
    setMaxLabelWidth(220);
    resetBoxToDefaults();
}

QString LegendOverlayStyle::displayLabelFor(const QString &propertyName) const
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("showTitle"), QStringLiteral("General — Show title")},
        {QStringLiteral("title"), QStringLiteral("General — Title")},
        {QStringLiteral("titleFont"), QStringLiteral("General — Title font")},
        {QStringLiteral("titleColor"), QStringLiteral("General — Title color")},
        {QStringLiteral("itemFont"), QStringLiteral("General — Item font")},
        {QStringLiteral("itemColor"), QStringLiteral("General — Item color")},
        {QStringLiteral("layerHeaderFont"), QStringLiteral("General — Layer header font")},
        {QStringLiteral("layerHeaderColor"), QStringLiteral("General — Layer header color")},
        {QStringLiteral("rowSpacing"), QStringLiteral("General — Row spacing")},
        {QStringLiteral("swatchSize"), QStringLiteral("General — Swatch size")},
        {QStringLiteral("explicitWidth"), QStringLiteral("Size — Width (0 = auto)")},
        {QStringLiteral("explicitHeight"), QStringLiteral("Size — Height (0 = auto)")},
        {QStringLiteral("maxLabelWidth"), QStringLiteral("Size — Max label width")},
    };
    const auto it = labels.constFind(propertyName);
    return it != labels.constEnd() ? it.value() : MapBoxStyle::displayLabelFor(propertyName);
}

QJsonObject LegendOverlayStyle::toJson() const
{
    QJsonObject j;
    writeBoxJson(j);
    j[QStringLiteral("showTitle")] = m_showTitle;
    j[QStringLiteral("title")] = m_title;
    j[QStringLiteral("titleFont")] = overlayFontToJson(m_titleFont);
    j[QStringLiteral("titleColor")] = m_titleColor.name(QColor::HexArgb);
    j[QStringLiteral("itemFont")] = overlayFontToJson(m_itemFont);
    j[QStringLiteral("itemColor")] = m_itemColor.name(QColor::HexArgb);
    j[QStringLiteral("layerHeaderFont")] = overlayFontToJson(m_layerHeaderFont);
    j[QStringLiteral("layerHeaderColor")] = m_layerHeaderColor.name(QColor::HexArgb);
    j[QStringLiteral("rowSpacing")] = m_rowSpacing;
    j[QStringLiteral("swatchSize")] = m_swatchSize;
    j[QStringLiteral("explicitWidth")] = m_explicitWidth;
    j[QStringLiteral("explicitHeight")] = m_explicitHeight;
    j[QStringLiteral("maxLabelWidth")] = m_maxLabelWidth;

    // Per-row overrides: { "layerKey|classKey": {visible?, userLabel?}, … }.
    if (!m_itemOverrides.isEmpty()) {
        QJsonObject overrides;
        for (auto it = m_itemOverrides.constBegin(); it != m_itemOverrides.constEnd(); ++it) {
            QJsonObject entry;
            if (!it.value().visible)
                entry[QStringLiteral("visible")] = false;
            if (!it.value().userLabel.isEmpty())
                entry[QStringLiteral("userLabel")] = it.value().userLabel;
            if (!entry.isEmpty())
                overrides[it.key()] = entry;
        }
        if (!overrides.isEmpty())
            j[QStringLiteral("itemOverrides")] = overrides;
    }
    return j;
}

void LegendOverlayStyle::fromJson(const QJsonObject &j)
{
    readBoxJson(j);
    const auto has = [&j](const char *key) { return j.contains(QLatin1String(key)); };
    const auto value = [&j](const char *key) { return j.value(QLatin1String(key)); };
    if (has("showTitle"))
        setShowTitle(value("showTitle").toBool());
    if (has("title"))
        setTitle(value("title").toString());
    if (has("titleFont"))
        setTitleFont(overlayFontFromJson(value("titleFont").toObject(), m_titleFont));
    if (has("titleColor"))
        setTitleColor(QColor(value("titleColor").toString()));
    if (has("itemFont"))
        setItemFont(overlayFontFromJson(value("itemFont").toObject(), m_itemFont));
    if (has("itemColor"))
        setItemColor(QColor(value("itemColor").toString()));
    if (has("layerHeaderFont"))
        setLayerHeaderFont(
            overlayFontFromJson(value("layerHeaderFont").toObject(), m_layerHeaderFont));
    if (has("layerHeaderColor"))
        setLayerHeaderColor(QColor(value("layerHeaderColor").toString()));
    if (has("rowSpacing"))
        setRowSpacing(value("rowSpacing").toInt(m_rowSpacing));
    if (has("swatchSize"))
        setSwatchSize(value("swatchSize").toInt(m_swatchSize));
    if (has("explicitWidth"))
        setExplicitWidth(value("explicitWidth").toInt(m_explicitWidth));
    if (has("explicitHeight"))
        setExplicitHeight(value("explicitHeight").toInt(m_explicitHeight));
    if (has("maxLabelWidth"))
        setMaxLabelWidth(value("maxLabelWidth").toInt(m_maxLabelWidth));

    // Row overrides are replaced as a whole.
    m_itemOverrides.clear();
    if (has("itemOverrides")) {
        const QJsonObject obj = value("itemOverrides").toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            const QJsonObject entry = it.value().toObject();
            ItemOverride o;
            o.visible = entry.value(QStringLiteral("visible")).toBool(true);
            o.userLabel = entry.value(QStringLiteral("userLabel")).toString();
            if (!o.visible || !o.userLabel.isEmpty())
                m_itemOverrides.insert(it.key(), o);
        }
        // A bulk load: one changed() covers it, no per-row signal.
        emit changed();
    }
}

// Each setter: ignore an equal value, store, emit its own signal, then changed().
#define LEGEND_SETTER(Type, name, member, signal)                                                  \
    void LegendOverlayStyle::set##name(Type v)                                                     \
    {                                                                                              \
        if (member == v)                                                                           \
            return;                                                                                \
        member = v;                                                                                \
        emit signal(v);                                                                            \
        emit changed();                                                                            \
    }

LEGEND_SETTER(bool, ShowTitle, m_showTitle, showTitleChanged)
LEGEND_SETTER(const QString &, Title, m_title, titleChanged)
LEGEND_SETTER(const QFont &, TitleFont, m_titleFont, titleFontChanged)
LEGEND_SETTER(const QColor &, TitleColor, m_titleColor, titleColorChanged)
LEGEND_SETTER(const QFont &, ItemFont, m_itemFont, itemFontChanged)
LEGEND_SETTER(const QColor &, ItemColor, m_itemColor, itemColorChanged)
LEGEND_SETTER(const QFont &, LayerHeaderFont, m_layerHeaderFont, layerHeaderFontChanged)
LEGEND_SETTER(const QColor &, LayerHeaderColor, m_layerHeaderColor, layerHeaderColorChanged)
LEGEND_SETTER(int, RowSpacing, m_rowSpacing, rowSpacingChanged)
LEGEND_SETTER(int, SwatchSize, m_swatchSize, swatchSizeChanged)

#undef LEGEND_SETTER

void LegendOverlayStyle::setExplicitWidth(int px)
{
    // 0 = automatic; a fixed size keeps a usable minimum so the box cannot vanish.
    if (px < 0)
        px = 0;
    if (px > 0 && px < 80)
        px = 80;
    if (m_explicitWidth == px)
        return;
    m_explicitWidth = px;
    emit explicitWidthChanged(px);
    emit changed();
}

void LegendOverlayStyle::setExplicitHeight(int px)
{
    if (px < 0)
        px = 0;
    if (px > 0 && px < 48)
        px = 48;
    if (m_explicitHeight == px)
        return;
    m_explicitHeight = px;
    emit explicitHeightChanged(px);
    emit changed();
}

void LegendOverlayStyle::setMaxLabelWidth(int px)
{
    px = std::clamp(px, 60, 1200);
    if (m_maxLabelWidth == px)
        return;
    m_maxLabelWidth = px;
    emit maxLabelWidthChanged(px);
    emit changed();
}

} // namespace OpenSWMM::Render
