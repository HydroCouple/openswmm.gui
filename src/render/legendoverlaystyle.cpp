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
#include <QMetaProperty>

#include <algorithm>

namespace OpenSWMM::Render
{

namespace
{

/** @brief Join a layer key and a class key with '|' (not escaped).
 * @param layerKey Layer identity. @param classKey Row key.
 * @return "layerKey|classKey". */
QString combinedKey(const QString &layerKey, const QString &classKey)
{
    return layerKey + QLatin1Char('|') + classKey;
}

/** @brief JSON for one property value: enums by key name, colors as #AARRGGBB ("" when
 *  invalid), fonts as overlayFontToJson(), other values as JSON numbers, booleans or strings.
 * @param property Property of LegendOverlayStyle.
 * @param value Its current value.
 * @return JSON value. */
QJsonValue propertyToJson(const QMetaProperty &property, const QVariant &value)
{
    if (property.isEnumType())
        return QString::fromLatin1(property.enumerator().valueToKey(value.toInt()));
    switch (value.typeId()) {
    case QMetaType::QColor: {
        const QColor c = value.value<QColor>();
        return c.isValid() ? c.name(QColor::HexArgb) : QString();
    }
    case QMetaType::QFont:
        return overlayFontToJson(value.value<QFont>());
    default:
        return QJsonValue::fromVariant(value);
    }
}

/** @brief Write a saved value into one property through its setter; an unknown enum name or a
 *  value of the wrong JSON type leaves the property unchanged.
 * @param style Style written.
 * @param property Property of LegendOverlayStyle.
 * @param json Saved value. */
void propertyFromJson(LegendOverlayStyle &style, const QMetaProperty &property,
                      const QJsonValue &json)
{
    const QVariant current = property.read(&style);
    if (property.isEnumType()) {
        bool ok = false;
        const int v = property.enumerator().keyToValue(json.toString().toUtf8().constData(), &ok);
        if (ok)
            property.write(&style, v);
        return;
    }
    switch (current.typeId()) {
    case QMetaType::QColor:
        if (json.isString())
            property.write(&style, QColor(json.toString()));
        return;
    case QMetaType::QFont:
        if (json.isObject())
            property.write(&style, overlayFontFromJson(json.toObject(), current.value<QFont>()));
        return;
    case QMetaType::Bool:
        if (json.isBool())
            property.write(&style, json.toBool());
        return;
    case QMetaType::Int:
        if (json.isDouble())
            property.write(&style, json.toInt(current.toInt()));
        return;
    case QMetaType::Double:
        if (json.isDouble())
            property.write(&style, json.toDouble(current.toDouble()));
        return;
    case QMetaType::QString:
        if (json.isString())
            property.write(&style, json.toString());
        return;
    default:
        return;
    }
}

} // namespace

LegendOverlayStyle::LegendOverlayStyle(QObject *parent) : MapBoxStyle(Anchor::BottomRight, parent)
{
    // Default-constructed fonts resolve to the application font, so the legend follows the
    // user's system theme.
}

QString LegendOverlayStyle::itemKey(const QObject *layer, const QString &)
{
    // Only the layer half. A property read keeps this file free of layer classes (the unit
    // test links only Qt); map layers have a LayerId the project file keeps.
    if (!layer)
        return {};
    const QString id = layer->property("LayerId").toString();
    return id.isEmpty() ? layer->objectName() : id;
}

LegendOverlayStyle::ItemOverride LegendOverlayStyle::itemOverride(const QString &layerKey,
                                                                  const QString &classKey) const
{
    if (const LegendNode *node = m_tree.itemNode(layerKey, classKey))
        return ItemOverride{node->visible, node->userLabel};
    return legacyItemOverride(layerKey, classKey);
}

LegendOverlayStyle::ItemOverride
LegendOverlayStyle::legacyItemOverride(const QString &objectName, const QString &classKey) const
{
    return m_legacyOverrides.value(combinedKey(objectName, classKey), ItemOverride{});
}

void LegendOverlayStyle::setItemTree(const LegendItemTree &tree)
{
    LegendItemTree next = tree;
    // A default node still shadows an older file's row setting, so keep it while any exist.
    if (m_autoUpdate && m_legacyOverrides.isEmpty())
        next.prune();
    if (next == m_tree)
        return;
    m_tree = next;
    emit itemTreeChanged();
    emit changed();
}

void LegendOverlayStyle::setItemOverride(const QString &layerKey, const QString &classKey,
                                         const ItemOverride &override)
{
    const ItemOverride current = itemOverride(layerKey, classKey);
    const bool hasNode = m_tree.itemNode(layerKey, classKey) != nullptr;
    // While an older file's row settings exist (keyed by object name, which this key cannot
    // match), an edit always records a node so it wins over them.
    if (current.visible == override.visible && current.userLabel == override.userLabel &&
        (hasNode ||
         (override.visible && override.userLabel.isEmpty() && m_legacyOverrides.isEmpty())))
        return;
    LegendItemTree next = m_tree;
    LegendNode &node = next.ensureItemNode(layerKey, classKey);
    node.visible = override.visible;
    node.userLabel = override.userLabel;
    if (m_autoUpdate && m_legacyOverrides.isEmpty())
        next.prune();
    m_tree = next;
    emit itemOverrideChanged(layerKey, classKey);
    emit itemTreeChanged();
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
    LegendItemTree next = m_tree;
    const auto clear = [](auto &self, QList<LegendNode> &nodes) -> void {
        for (LegendNode &n : nodes) {
            if (n.type == LegendNode::Type::Item) {
                n.visible = true;
                n.userLabel.clear();
            }
            self(self, n.children);
        }
    };
    clear(clear, next.roots());
    if (m_autoUpdate)
        next.prune();
    if (next == m_tree && m_legacyOverrides.isEmpty())
        return;
    m_tree = next;
    m_legacyOverrides.clear();
    emit itemTreeChanged();
    emit changed();
}

void LegendOverlayStyle::resetToDefaults()
{
    // A default-constructed style's values, written through the setters (each emits).
    const LegendOverlayStyle defaults;
    const QMetaObject &meta = staticMetaObject;
    for (int i = meta.propertyOffset(); i < meta.propertyCount(); ++i) {
        const QMetaProperty property = meta.property(i);
        property.write(this, property.read(&defaults));
    }
    resetBoxToDefaults();
}

QString LegendOverlayStyle::displayLabelFor(const QString &propertyName) const
{
    static const QHash<QString, QString> labels = {
        // Title
        {QStringLiteral("showTitle"), QStringLiteral("General — Show title")},
        {QStringLiteral("title"), QStringLiteral("General — Title")},
        {QStringLiteral("titleFont"), QStringLiteral("General — Title font")},
        {QStringLiteral("titleColor"), QStringLiteral("General — Title color")},
        {QStringLiteral("titleAlignment"), QStringLiteral("Title — Alignment")},
        {QStringLiteral("titleSpacing"), QStringLiteral("Title — Space below")},
        // Columns
        {QStringLiteral("columnCount"), QStringLiteral("Columns — Count")},
        {QStringLiteral("equalColumnWidths"), QStringLiteral("Columns — Equal widths")},
        {QStringLiteral("splitLayers"), QStringLiteral("Columns — Split layers")},
        {QStringLiteral("columnSpacing"), QStringLiteral("Columns — Spacing")},
        {QStringLiteral("showColumnSeparators"), QStringLiteral("Columns — Show separators")},
        {QStringLiteral("columnSeparatorColor"), QStringLiteral("Columns — Separator color")},
        {QStringLiteral("columnSeparatorWidth"), QStringLiteral("Columns — Separator width")},
        // Size
        {QStringLiteral("explicitWidth"), QStringLiteral("Size — Width (0 = auto)")},
        {QStringLiteral("explicitHeight"), QStringLiteral("Size — Height (0 = auto)")},
        {QStringLiteral("maxLabelWidth"), QStringLiteral("Size — Max label width")},
        // Text
        {QStringLiteral("layerHeaderFont"), QStringLiteral("General — Layer header font")},
        {QStringLiteral("layerHeaderColor"), QStringLiteral("General — Layer header color")},
        {QStringLiteral("layerHeaderAlignment"), QStringLiteral("Text — Group heading alignment")},
        {QStringLiteral("subheadingFont"), QStringLiteral("Text — Sub-heading font")},
        {QStringLiteral("subheadingColor"), QStringLiteral("Text — Sub-heading color")},
        {QStringLiteral("subheadingAlignment"), QStringLiteral("Text — Sub-heading alignment")},
        {QStringLiteral("itemFont"), QStringLiteral("General — Item font")},
        {QStringLiteral("itemColor"), QStringLiteral("General — Item color")},
        {QStringLiteral("itemAlignment"), QStringLiteral("Text — Item alignment")},
        {QStringLiteral("labelOverflow"), QStringLiteral("Text — Long labels")},
        {QStringLiteral("wrapString"), QStringLiteral("Text — Wrap on")},
        {QStringLiteral("lineSpacing"), QStringLiteral("Text — Line spacing")},
        // Symbols
        {QStringLiteral("swatchSize"), QStringLiteral("General — Swatch size")},
        {QStringLiteral("symbolWidth"), QStringLiteral("Symbols — Width (0 = swatch)")},
        {QStringLiteral("symbolHeight"), QStringLiteral("Symbols — Height (0 = swatch)")},
        {QStringLiteral("symbolSizeMode"), QStringLiteral("Symbols — Size")},
        {QStringLiteral("minSymbolSize"), QStringLiteral("Symbols — Minimum size")},
        {QStringLiteral("maxSymbolSize"), QStringLiteral("Symbols — Maximum size")},
        {QStringLiteral("symbolPosition"), QStringLiteral("Symbols — Position")},
        {QStringLiteral("symbolLabelSpacing"), QStringLiteral("Symbols — Space to label")},
        {QStringLiteral("linePatchShape"), QStringLiteral("Symbols — Line shape")},
        {QStringLiteral("areaPatchShape"), QStringLiteral("Symbols — Fill shape")},
        {QStringLiteral("showPatchOutline"), QStringLiteral("Symbols — Outline fills")},
        {QStringLiteral("patchOutlineColor"), QStringLiteral("Symbols — Outline color")},
        {QStringLiteral("patchOutlineWidth"), QStringLiteral("Symbols — Outline width")},
        // Spacing
        {QStringLiteral("rowSpacing"), QStringLiteral("General — Row spacing")},
        {QStringLiteral("groupSpacing"), QStringLiteral("Spacing — Above groups")},
        {QStringLiteral("headerSpacing"), QStringLiteral("Spacing — Below group headings")},
        {QStringLiteral("subheadingSpacing"), QStringLiteral("Spacing — Above sub-headings")},
        {QStringLiteral("groupIndent"), QStringLiteral("Spacing — Group indent")},
        {QStringLiteral("subheadingIndent"), QStringLiteral("Spacing — Sub-heading indent")},
        {QStringLiteral("itemIndent"), QStringLiteral("Spacing — Item indent")},
        // Content
        {QStringLiteral("autoUpdate"), QStringLiteral("Content — Follow the map")},
    };
    const auto it = labels.constFind(propertyName);
    return it != labels.constEnd() ? it.value() : MapBoxStyle::displayLabelFor(propertyName);
}

QString LegendOverlayStyle::propertyGroupFor(const QString &propertyName) const
{
    static const QHash<QString, QString> groups = {
        {QStringLiteral("showTitle"), QStringLiteral("Title")},
        {QStringLiteral("title"), QStringLiteral("Title")},
        {QStringLiteral("titleFont"), QStringLiteral("Title")},
        {QStringLiteral("titleColor"), QStringLiteral("Title")},
        {QStringLiteral("titleAlignment"), QStringLiteral("Title")},
        {QStringLiteral("titleSpacing"), QStringLiteral("Title")},
        {QStringLiteral("columnCount"), QStringLiteral("Columns")},
        {QStringLiteral("equalColumnWidths"), QStringLiteral("Columns")},
        {QStringLiteral("splitLayers"), QStringLiteral("Columns")},
        {QStringLiteral("columnSpacing"), QStringLiteral("Columns")},
        {QStringLiteral("showColumnSeparators"), QStringLiteral("Columns")},
        {QStringLiteral("columnSeparatorColor"), QStringLiteral("Columns")},
        {QStringLiteral("columnSeparatorWidth"), QStringLiteral("Columns")},
        {QStringLiteral("explicitWidth"), QStringLiteral("Size")},
        {QStringLiteral("explicitHeight"), QStringLiteral("Size")},
        {QStringLiteral("maxLabelWidth"), QStringLiteral("Text")},
        {QStringLiteral("layerHeaderFont"), QStringLiteral("Text")},
        {QStringLiteral("layerHeaderColor"), QStringLiteral("Text")},
        {QStringLiteral("layerHeaderAlignment"), QStringLiteral("Text")},
        {QStringLiteral("subheadingFont"), QStringLiteral("Text")},
        {QStringLiteral("subheadingColor"), QStringLiteral("Text")},
        {QStringLiteral("subheadingAlignment"), QStringLiteral("Text")},
        {QStringLiteral("itemFont"), QStringLiteral("Text")},
        {QStringLiteral("itemColor"), QStringLiteral("Text")},
        {QStringLiteral("itemAlignment"), QStringLiteral("Text")},
        {QStringLiteral("labelOverflow"), QStringLiteral("Text")},
        {QStringLiteral("wrapString"), QStringLiteral("Text")},
        {QStringLiteral("lineSpacing"), QStringLiteral("Text")},
        {QStringLiteral("swatchSize"), QStringLiteral("Symbols")},
        {QStringLiteral("symbolWidth"), QStringLiteral("Symbols")},
        {QStringLiteral("symbolHeight"), QStringLiteral("Symbols")},
        {QStringLiteral("symbolSizeMode"), QStringLiteral("Symbols")},
        {QStringLiteral("minSymbolSize"), QStringLiteral("Symbols")},
        {QStringLiteral("maxSymbolSize"), QStringLiteral("Symbols")},
        {QStringLiteral("symbolPosition"), QStringLiteral("Symbols")},
        {QStringLiteral("symbolLabelSpacing"), QStringLiteral("Symbols")},
        {QStringLiteral("linePatchShape"), QStringLiteral("Symbols")},
        {QStringLiteral("areaPatchShape"), QStringLiteral("Symbols")},
        {QStringLiteral("showPatchOutline"), QStringLiteral("Symbols")},
        {QStringLiteral("patchOutlineColor"), QStringLiteral("Symbols")},
        {QStringLiteral("patchOutlineWidth"), QStringLiteral("Symbols")},
        {QStringLiteral("rowSpacing"), QStringLiteral("Spacing")},
        {QStringLiteral("groupSpacing"), QStringLiteral("Spacing")},
        {QStringLiteral("headerSpacing"), QStringLiteral("Spacing")},
        {QStringLiteral("subheadingSpacing"), QStringLiteral("Spacing")},
        {QStringLiteral("groupIndent"), QStringLiteral("Spacing")},
        {QStringLiteral("subheadingIndent"), QStringLiteral("Spacing")},
        {QStringLiteral("itemIndent"), QStringLiteral("Spacing")},
        {QStringLiteral("autoUpdate"), QStringLiteral("Content")},
    };
    const auto it = groups.constFind(propertyName);
    return it != groups.constEnd() ? it.value() : MapBoxStyle::propertyGroupFor(propertyName);
}

QJsonObject LegendOverlayStyle::toJson() const
{
    QJsonObject j;
    writeBoxJson(j);
    // Every legend property, saved under its property name.
    const QMetaObject &meta = staticMetaObject;
    for (int i = meta.propertyOffset(); i < meta.propertyCount(); ++i) {
        const QMetaProperty property = meta.property(i);
        j[QString::fromLatin1(property.name())] = propertyToJson(property, property.read(this));
    }

    // The legend tree, and row settings read from an older file (kept for it).
    if (!m_tree.roots().isEmpty())
        j[QStringLiteral("items")] = m_tree.toJson();
    if (!m_legacyOverrides.isEmpty()) {
        QJsonObject overrides;
        for (auto it = m_legacyOverrides.constBegin(); it != m_legacyOverrides.constEnd(); ++it) {
            QJsonObject entry;
            if (!it.value().visible)
                entry[QStringLiteral("visible")] = false;
            if (!it.value().userLabel.isEmpty())
                entry[QStringLiteral("userLabel")] = it.value().userLabel;
            overrides[it.key()] = entry;
        }
        j[QStringLiteral("itemOverrides")] = overrides;
    }
    return j;
}

void LegendOverlayStyle::fromJson(const QJsonObject &j)
{
    readBoxJson(j);
    const QMetaObject &meta = staticMetaObject;
    for (int i = meta.propertyOffset(); i < meta.propertyCount(); ++i) {
        const QMetaProperty property = meta.property(i);
        const QString key = QString::fromLatin1(property.name());
        if (j.contains(key))
            propertyFromJson(*this, property, j.value(key));
    }
    // A legend saved before these settings existed drew sub-headings in the item font and
    // color, and spaced the title by the row spacing.
    const auto has = [&j](const char *key) { return j.contains(QLatin1String(key)); };
    if (!has("subheadingFont") && has("itemFont"))
        setSubheadingFont(m_itemFont);
    if (!has("subheadingColor") && has("itemColor"))
        setSubheadingColor(m_itemColor);
    if (!has("titleSpacing") && has("rowSpacing"))
        setTitleSpacing(m_rowSpacing);

    // The tree and the older row settings are replaced as a whole. An older file keyed rows by
    // the layer's object name ("name|classKey"); those apply until the row is edited here.
    m_tree = has("items") ? LegendItemTree::fromJson(j.value(QStringLiteral("items")).toArray())
                          : LegendItemTree{};
    if (m_autoUpdate)
        m_tree.prune();
    m_legacyOverrides.clear();
    const QJsonObject obj = j.value(QStringLiteral("itemOverrides")).toObject();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        ItemOverride o;
        o.visible = entry.value(QStringLiteral("visible")).toBool(true);
        o.userLabel = entry.value(QStringLiteral("userLabel")).toString();
        if (!o.visible || !o.userLabel.isEmpty())
            m_legacyOverrides.insert(it.key(), o);
    }
    // A bulk load: one notification covers it, no per-row signal.
    emit itemTreeChanged();
    emit changed();
}

// Each setter: clamp, ignore an equal value, store, emit its own signal, then changed().
#define LEGEND_SETTER(Type, name, member, signal, clampExpr)                                       \
    void LegendOverlayStyle::set##name(Type v)                                                     \
    {                                                                                              \
        const auto value = clampExpr;                                                              \
        if (member == value)                                                                       \
            return;                                                                                \
        member = value;                                                                            \
        emit signal(member);                                                                       \
        emit changed();                                                                            \
    }

using TextAlignment = LegendOverlayStyle::TextAlignment;

LEGEND_SETTER(bool, ShowTitle, m_showTitle, showTitleChanged, v)
LEGEND_SETTER(const QString &, Title, m_title, titleChanged, v)
LEGEND_SETTER(const QFont &, TitleFont, m_titleFont, titleFontChanged, v)
LEGEND_SETTER(const QColor &, TitleColor, m_titleColor, titleColorChanged, v)
LEGEND_SETTER(TextAlignment, TitleAlignment, m_titleAlignment, titleAlignmentChanged, v)
LEGEND_SETTER(int, TitleSpacing, m_titleSpacing, titleSpacingChanged, std::clamp(v, 0, 200))
LEGEND_SETTER(int, ColumnCount, m_columnCount, columnCountChanged, std::clamp(v, 1, 12))
LEGEND_SETTER(bool, EqualColumnWidths, m_equalColumnWidths, equalColumnWidthsChanged, v)
LEGEND_SETTER(bool, SplitLayers, m_splitLayers, splitLayersChanged, v)
LEGEND_SETTER(int, ColumnSpacing, m_columnSpacing, columnSpacingChanged, std::clamp(v, 0, 200))
LEGEND_SETTER(bool, ShowColumnSeparators, m_showColumnSeparators, showColumnSeparatorsChanged, v)
LEGEND_SETTER(const QColor &, ColumnSeparatorColor, m_columnSeparatorColor,
              columnSeparatorColorChanged, v)
LEGEND_SETTER(qreal, ColumnSeparatorWidth, m_columnSeparatorWidth, columnSeparatorWidthChanged,
              std::clamp<qreal>(v, 0.0, 10.0))
// A fixed size keeps a usable minimum so the box cannot vanish; 0 = fit the content.
LEGEND_SETTER(int, ExplicitWidth, m_explicitWidth, explicitWidthChanged,
              v <= 0 ? 0 : std::max(v, 80))
LEGEND_SETTER(int, ExplicitHeight, m_explicitHeight, explicitHeightChanged,
              v <= 0 ? 0 : std::max(v, 48))
LEGEND_SETTER(const QFont &, LayerHeaderFont, m_layerHeaderFont, layerHeaderFontChanged, v)
LEGEND_SETTER(const QColor &, LayerHeaderColor, m_layerHeaderColor, layerHeaderColorChanged, v)
LEGEND_SETTER(TextAlignment, LayerHeaderAlignment, m_layerHeaderAlignment,
              layerHeaderAlignmentChanged, v)
LEGEND_SETTER(const QFont &, SubheadingFont, m_subheadingFont, subheadingFontChanged, v)
LEGEND_SETTER(const QColor &, SubheadingColor, m_subheadingColor, subheadingColorChanged, v)
LEGEND_SETTER(TextAlignment, SubheadingAlignment, m_subheadingAlignment, subheadingAlignmentChanged,
              v)
LEGEND_SETTER(const QFont &, ItemFont, m_itemFont, itemFontChanged, v)
LEGEND_SETTER(const QColor &, ItemColor, m_itemColor, itemColorChanged, v)
LEGEND_SETTER(TextAlignment, ItemAlignment, m_itemAlignment, itemAlignmentChanged, v)
LEGEND_SETTER(LegendOverlayStyle::LabelOverflow, LabelOverflow, m_labelOverflow,
              labelOverflowChanged, v)
LEGEND_SETTER(int, MaxLabelWidth, m_maxLabelWidth, maxLabelWidthChanged, std::clamp(v, 60, 1200))
LEGEND_SETTER(const QString &, WrapString, m_wrapString, wrapStringChanged, v)
LEGEND_SETTER(int, LineSpacing, m_lineSpacing, lineSpacingChanged, std::clamp(v, 0, 50))
LEGEND_SETTER(int, SwatchSize, m_swatchSize, swatchSizeChanged, std::clamp(v, 1, 200))
LEGEND_SETTER(int, SymbolWidth, m_symbolWidth, symbolWidthChanged, std::clamp(v, 0, 200))
LEGEND_SETTER(int, SymbolHeight, m_symbolHeight, symbolHeightChanged, std::clamp(v, 0, 200))
LEGEND_SETTER(LegendOverlayStyle::SymbolSizeMode, SymbolSizeMode, m_symbolSizeMode,
              symbolSizeModeChanged, v)
LEGEND_SETTER(qreal, MinSymbolSize, m_minSymbolSize, minSymbolSizeChanged,
              std::clamp<qreal>(v, 0.0, 200.0))
LEGEND_SETTER(qreal, MaxSymbolSize, m_maxSymbolSize, maxSymbolSizeChanged,
              std::clamp<qreal>(v, 0.0, 200.0))
LEGEND_SETTER(LegendOverlayStyle::SymbolPosition, SymbolPosition, m_symbolPosition,
              symbolPositionChanged, v)
LEGEND_SETTER(int, SymbolLabelSpacing, m_symbolLabelSpacing, symbolLabelSpacingChanged,
              std::clamp(v, 0, 100))
LEGEND_SETTER(LegendOverlayStyle::LinePatchShape, LinePatchShape, m_linePatchShape,
              linePatchShapeChanged, v)
LEGEND_SETTER(LegendOverlayStyle::AreaPatchShape, AreaPatchShape, m_areaPatchShape,
              areaPatchShapeChanged, v)
LEGEND_SETTER(bool, ShowPatchOutline, m_showPatchOutline, showPatchOutlineChanged, v)
LEGEND_SETTER(const QColor &, PatchOutlineColor, m_patchOutlineColor, patchOutlineColorChanged,
              v.isValid() ? v : QColor())
LEGEND_SETTER(qreal, PatchOutlineWidth, m_patchOutlineWidth, patchOutlineWidthChanged,
              std::clamp<qreal>(v, 0.0, 10.0))
LEGEND_SETTER(int, RowSpacing, m_rowSpacing, rowSpacingChanged, std::clamp(v, 0, 200))
LEGEND_SETTER(int, GroupSpacing, m_groupSpacing, groupSpacingChanged, std::clamp(v, 0, 200))
LEGEND_SETTER(int, HeaderSpacing, m_headerSpacing, headerSpacingChanged, std::clamp(v, 0, 200))
LEGEND_SETTER(int, SubheadingSpacing, m_subheadingSpacing, subheadingSpacingChanged,
              std::clamp(v, 0, 200))
LEGEND_SETTER(int, GroupIndent, m_groupIndent, groupIndentChanged, std::clamp(v, 0, 200))
LEGEND_SETTER(int, SubheadingIndent, m_subheadingIndent, subheadingIndentChanged,
              std::clamp(v, 0, 200))
LEGEND_SETTER(int, ItemIndent, m_itemIndent, itemIndentChanged, std::clamp(v, 0, 200))

void LegendOverlayStyle::setAutoUpdate(bool on)
{
    if (m_autoUpdate == on)
        return;
    m_autoUpdate = on;
    emit autoUpdateChanged(on);
    if (on) {
        // Following the map again: the order, groups and text lines go; settings stay.
        LegendItemTree next = m_tree;
        next.flattenToLayers();
        if (!(next == m_tree)) {
            m_tree = next;
            emit itemTreeChanged();
        }
    }
    emit changed();
}

#undef LEGEND_SETTER

} // namespace OpenSWMM::Render
