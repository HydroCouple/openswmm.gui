/**
 * @file      legendoverlaystyle.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Model for the map legend's look: title, columns, fonts, text wrapping, symbols,
 *            spacing, box size and per-row overrides, plus the box settings it shares with the
 *            date/time box.
 *
 *         Source-of-truth rule (MVC): the legend is edited from several places (the legend's
 *         context menu, the Legend Properties dialog, the Properties panel, the Legend dock).
 *         All of them write this object and follow its changed() signal; none keeps a copy.
 *         Per-class colors, sizes and symbols live on the layer's renderer, not here.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_LEGENDOVERLAYSTYLE_H
#define OPENSWMM_RENDER_LEGENDOVERLAYSTYLE_H

#include "render/legenditemtree.h"
#include "render/mapboxstyle.h"

#include <QColor>
#include <QFont>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace OpenSWMM::Render
{

/*!
 * \class LegendOverlayStyle
 * \brief What the map legend shows and how it looks.
 *
 *        Effective property changes emit the corresponding per-property signal and then the
 *        changed() aggregate. Setters clamp to the documented ranges. Defaults reproduce the
 *        original legend: one column, a translucent white rounded box, 8 px margins, 14 px
 *        swatches, anchored bottom-right.
 */
class LegendOverlayStyle : public MapBoxStyle
{
    Q_OBJECT

public:
    /** @brief Horizontal text alignment. */
    enum class TextAlignment { Left, Center, Right };
    Q_ENUM(TextAlignment)
    /** @brief What happens to a label wider than its space. */
    enum class LabelOverflow { Elide, Wrap };
    Q_ENUM(LabelOverflow)
    /** @brief How symbols are sized: as on the map (the slot grows), or fitted to the slot. */
    enum class SymbolSizeMode { MapSize, Fit };
    Q_ENUM(SymbolSizeMode)
    /** @brief Which side of the label the symbol is on. */
    enum class SymbolPosition { Left, Right };
    Q_ENUM(SymbolPosition)
    /** @brief Shape of a line symbol's sample. */
    enum class LinePatchShape { Horizontal, ZigZag, SCurve };
    Q_ENUM(LinePatchShape)
    /** @brief Shape of a fill symbol's sample. */
    enum class AreaPatchShape { Rectangle, RoundedRectangle, Ellipse };
    Q_ENUM(AreaPatchShape)
    /** @brief How a continuous color ramp is shown: one bar, or sampled swatch rows. */
    enum class RampDisplay { Bar, Swatches };
    Q_ENUM(RampDisplay)
    /** @brief Direction of the color bar. */
    enum class RampOrientation { Vertical, Horizontal };
    Q_ENUM(RampOrientation)
    /** @brief How the color bar's values are written. */
    enum class NumberFormat { Auto, Fixed, Scientific };
    Q_ENUM(NumberFormat)

    // ── Title ───────────────────────────────────────────────────────────
    Q_PROPERTY(bool showTitle READ showTitle WRITE setShowTitle NOTIFY showTitleChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QFont titleFont READ titleFont WRITE setTitleFont NOTIFY titleFontChanged)
    Q_PROPERTY(QColor titleColor READ titleColor WRITE setTitleColor NOTIFY titleColorChanged)
    Q_PROPERTY(TextAlignment titleAlignment READ titleAlignment WRITE setTitleAlignment NOTIFY
                   titleAlignmentChanged)
    Q_PROPERTY(int titleSpacing READ titleSpacing WRITE setTitleSpacing NOTIFY titleSpacingChanged)

    // ── Columns ─────────────────────────────────────────────────────────
    Q_PROPERTY(int columnCount READ columnCount WRITE setColumnCount NOTIFY columnCountChanged)
    Q_PROPERTY(bool equalColumnWidths READ equalColumnWidths WRITE setEqualColumnWidths NOTIFY
                   equalColumnWidthsChanged)
    Q_PROPERTY(bool splitLayers READ splitLayers WRITE setSplitLayers NOTIFY splitLayersChanged)
    Q_PROPERTY(
        int columnSpacing READ columnSpacing WRITE setColumnSpacing NOTIFY columnSpacingChanged)
    Q_PROPERTY(bool showColumnSeparators READ showColumnSeparators WRITE setShowColumnSeparators
                   NOTIFY showColumnSeparatorsChanged)
    Q_PROPERTY(QColor columnSeparatorColor READ columnSeparatorColor WRITE setColumnSeparatorColor
                   NOTIFY columnSeparatorColorChanged)
    Q_PROPERTY(qreal columnSeparatorWidth READ columnSeparatorWidth WRITE setColumnSeparatorWidth
                   NOTIFY columnSeparatorWidthChanged)

    // ── Size ────────────────────────────────────────────────────────────
    Q_PROPERTY(
        int explicitWidth READ explicitWidth WRITE setExplicitWidth NOTIFY explicitWidthChanged)
    Q_PROPERTY(
        int explicitHeight READ explicitHeight WRITE setExplicitHeight NOTIFY explicitHeightChanged)

    // ── Text ────────────────────────────────────────────────────────────
    Q_PROPERTY(QFont layerHeaderFont READ layerHeaderFont WRITE setLayerHeaderFont NOTIFY
                   layerHeaderFontChanged)
    Q_PROPERTY(QColor layerHeaderColor READ layerHeaderColor WRITE setLayerHeaderColor NOTIFY
                   layerHeaderColorChanged)
    Q_PROPERTY(TextAlignment layerHeaderAlignment READ layerHeaderAlignment WRITE
                   setLayerHeaderAlignment NOTIFY layerHeaderAlignmentChanged)
    Q_PROPERTY(QFont subheadingFont READ subheadingFont WRITE setSubheadingFont NOTIFY
                   subheadingFontChanged)
    Q_PROPERTY(QColor subheadingColor READ subheadingColor WRITE setSubheadingColor NOTIFY
                   subheadingColorChanged)
    Q_PROPERTY(TextAlignment subheadingAlignment READ subheadingAlignment WRITE
                   setSubheadingAlignment NOTIFY subheadingAlignmentChanged)
    Q_PROPERTY(QFont itemFont READ itemFont WRITE setItemFont NOTIFY itemFontChanged)
    Q_PROPERTY(QColor itemColor READ itemColor WRITE setItemColor NOTIFY itemColorChanged)
    Q_PROPERTY(TextAlignment itemAlignment READ itemAlignment WRITE setItemAlignment NOTIFY
                   itemAlignmentChanged)
    Q_PROPERTY(LabelOverflow labelOverflow READ labelOverflow WRITE setLabelOverflow NOTIFY
                   labelOverflowChanged)
    Q_PROPERTY(
        int maxLabelWidth READ maxLabelWidth WRITE setMaxLabelWidth NOTIFY maxLabelWidthChanged)
    Q_PROPERTY(QString wrapString READ wrapString WRITE setWrapString NOTIFY wrapStringChanged)
    Q_PROPERTY(int lineSpacing READ lineSpacing WRITE setLineSpacing NOTIFY lineSpacingChanged)

    // ── Symbols ─────────────────────────────────────────────────────────
    Q_PROPERTY(int swatchSize READ swatchSize WRITE setSwatchSize NOTIFY swatchSizeChanged)
    Q_PROPERTY(int symbolWidth READ symbolWidth WRITE setSymbolWidth NOTIFY symbolWidthChanged)
    Q_PROPERTY(int symbolHeight READ symbolHeight WRITE setSymbolHeight NOTIFY symbolHeightChanged)
    Q_PROPERTY(SymbolSizeMode symbolSizeMode READ symbolSizeMode WRITE setSymbolSizeMode NOTIFY
                   symbolSizeModeChanged)
    Q_PROPERTY(
        qreal minSymbolSize READ minSymbolSize WRITE setMinSymbolSize NOTIFY minSymbolSizeChanged)
    Q_PROPERTY(
        qreal maxSymbolSize READ maxSymbolSize WRITE setMaxSymbolSize NOTIFY maxSymbolSizeChanged)
    Q_PROPERTY(SymbolPosition symbolPosition READ symbolPosition WRITE setSymbolPosition NOTIFY
                   symbolPositionChanged)
    Q_PROPERTY(int symbolLabelSpacing READ symbolLabelSpacing WRITE setSymbolLabelSpacing NOTIFY
                   symbolLabelSpacingChanged)
    Q_PROPERTY(LinePatchShape linePatchShape READ linePatchShape WRITE setLinePatchShape NOTIFY
                   linePatchShapeChanged)
    Q_PROPERTY(AreaPatchShape areaPatchShape READ areaPatchShape WRITE setAreaPatchShape NOTIFY
                   areaPatchShapeChanged)
    Q_PROPERTY(bool showPatchOutline READ showPatchOutline WRITE setShowPatchOutline NOTIFY
                   showPatchOutlineChanged)
    Q_PROPERTY(QColor patchOutlineColor READ patchOutlineColor WRITE setPatchOutlineColor NOTIFY
                   patchOutlineColorChanged)
    Q_PROPERTY(qreal patchOutlineWidth READ patchOutlineWidth WRITE setPatchOutlineWidth NOTIFY
                   patchOutlineWidthChanged)

    // ── Spacing ─────────────────────────────────────────────────────────
    Q_PROPERTY(int rowSpacing READ rowSpacing WRITE setRowSpacing NOTIFY rowSpacingChanged)
    Q_PROPERTY(int groupSpacing READ groupSpacing WRITE setGroupSpacing NOTIFY groupSpacingChanged)
    Q_PROPERTY(
        int headerSpacing READ headerSpacing WRITE setHeaderSpacing NOTIFY headerSpacingChanged)
    Q_PROPERTY(int subheadingSpacing READ subheadingSpacing WRITE setSubheadingSpacing NOTIFY
                   subheadingSpacingChanged)
    Q_PROPERTY(int groupIndent READ groupIndent WRITE setGroupIndent NOTIFY groupIndentChanged)
    Q_PROPERTY(int subheadingIndent READ subheadingIndent WRITE setSubheadingIndent NOTIFY
                   subheadingIndentChanged)
    Q_PROPERTY(int itemIndent READ itemIndent WRITE setItemIndent NOTIFY itemIndentChanged)

    // ── Content ─────────────────────────────────────────────────────────
    Q_PROPERTY(bool autoUpdate READ autoUpdate WRITE setAutoUpdate NOTIFY autoUpdateChanged)

    // ── Color ramps ─────────────────────────────────────────────────────
    Q_PROPERTY(
        RampDisplay rampDisplay READ rampDisplay WRITE setRampDisplay NOTIFY rampDisplayChanged)
    Q_PROPERTY(RampOrientation rampOrientation READ rampOrientation WRITE setRampOrientation NOTIFY
                   rampOrientationChanged)
    Q_PROPERTY(int rampLength READ rampLength WRITE setRampLength NOTIFY rampLengthChanged)
    Q_PROPERTY(
        int rampThickness READ rampThickness WRITE setRampThickness NOTIFY rampThicknessChanged)
    Q_PROPERTY(
        int rampTickCount READ rampTickCount WRITE setRampTickCount NOTIFY rampTickCountChanged)
    Q_PROPERTY(
        bool rampHighAtTop READ rampHighAtTop WRITE setRampHighAtTop NOTIFY rampHighAtTopChanged)
    Q_PROPERTY(NumberFormat rampNumberFormat READ rampNumberFormat WRITE setRampNumberFormat NOTIFY
                   rampNumberFormatChanged)
    Q_PROPERTY(int rampDecimals READ rampDecimals WRITE setRampDecimals NOTIFY rampDecimalsChanged)
    Q_PROPERTY(bool rampShowOutline READ rampShowOutline WRITE setRampShowOutline NOTIFY
                   rampShowOutlineChanged)
    Q_PROPERTY(QColor rampOutlineColor READ rampOutlineColor WRITE setRampOutlineColor NOTIFY
                   rampOutlineColorChanged)
    Q_PROPERTY(bool rampShowTickMarks READ rampShowTickMarks WRITE setRampShowTickMarks NOTIFY
                   rampShowTickMarksChanged)
    Q_PROPERTY(bool rampShowEndCaps READ rampShowEndCaps WRITE setRampShowEndCaps NOTIFY
                   rampShowEndCapsChanged)
    Q_PROPERTY(int rampLabelSpacing READ rampLabelSpacing WRITE setRampLabelSpacing NOTIFY
                   rampLabelSpacingChanged)

    /*!
     * \struct ItemOverride
     * \brief Legend-only visibility and label of one row. The renderer stays the source of
     *        truth for the row's symbol.
     */
    struct ItemOverride {
        bool visible = true;
        QString userLabel; /*!< Empty = use the renderer's label. */
    };

    /** @brief Construct the default legend style with no row overrides.
     * @param parent Optional QObject owner. Default fonts follow the application font. */
    explicit LegendOverlayStyle(QObject *parent = nullptr);
    /** @brief Destroy the style. */
    ~LegendOverlayStyle() override = default;

    /*! \brief A layer's key in the legend tree: its "LayerId" property (the id the project
     *         file keeps), or its QObject::objectName when it has no id.
     * @param layer Layer, or null.
     * @param classKey Unused; kept for existing callers.
     * @return The key, or empty text for null. */
    static QString itemKey(const QObject *layer, const QString &classKey);

    /*! \brief A row's legend visibility and label from the tree (or, when the tree has no
     *         node, from row settings read from an older file), or a visible, unrenamed
     *         default.
     * @param layerKey Layer key (see itemKey()).
     * @param classKey Renderer row key.
     * @return Stored settings or the default. */
    [[nodiscard]] ItemOverride itemOverride(const QString &layerKey, const QString &classKey) const;

    /*! \brief A row setting read from an older file, which keyed rows by the layer's object
     *         name; used when the tree has no node for the row.
     * @param objectName Layer object name. @param classKey Renderer row key.
     * @return The setting, or a visible, unrenamed default. */
    [[nodiscard]] ItemOverride legacyItemOverride(const QString &objectName,
                                                  const QString &classKey) const;

    /** @brief The legend tree (per-layer and per-row settings, and the order when the legend
     *  does not follow the map). */
    [[nodiscard]] const LegendItemTree &itemTree() const { return m_tree; }
    /** @brief Replace the legend tree; while the legend follows the map, nodes without a
     *  setting are dropped. Emits itemTreeChanged() and changed() for an effective change.
     * @param tree New tree. */
    void setItemTree(const LegendItemTree &tree);

    /*! \brief Replace a row's visibility and label; a visible, unrenamed value clears them.
     *         Emits itemOverrideChanged(), itemTreeChanged() and changed() only for an
     *         effective change.
     * @param layerKey Layer key, used verbatim.
     * @param classKey Renderer row key, used verbatim.
     * @param override New value; labels are not trimmed. */
    void setItemOverride(const QString &layerKey, const QString &classKey,
                         const ItemOverride &override);

    /** @brief Show or hide a row, keeping its label.
     * @param layerKey Layer identity. @param classKey Row key. @param visible New value. */
    void setItemVisible(const QString &layerKey, const QString &classKey, bool visible);

    /** @brief Rename a row, keeping its visibility; empty text restores the renderer's label.
     * @param layerKey Layer identity. @param classKey Row key. @param label New label. */
    void setItemUserLabel(const QString &layerKey, const QString &classKey, const QString &label);

    /** @brief Show every row and restore every row's label (other settings stay); one
     *  changed() when anything was set. */
    void clearItemOverrides();

    /*! \brief Group-prefixed row labels for the Properties tree, e.g. "Title — Font".
     * @param propertyName Property name.
     * @return Label, or @p propertyName when unknown. */
    QString displayLabelFor(const QString &propertyName) const override;

    /*! \brief Properties panel group of a property, e.g. "Columns".
     * @param propertyName Property name.
     * @return Group name, or empty text when unknown. */
    QString propertyGroupFor(const QString &propertyName) const override;

    /** @brief Restore every default except the row overrides (each setter emits). */
    void resetToDefaults();

    /** @brief Save every field and the non-default row overrides.
     * @return JSON object; colors #AARRGGBB (an automatic color as ""), enums by name, fonts as
     *   overlayFontToJson(), row overrides as a flat "layerKey|classKey" object. */
    [[nodiscard]] QJsonObject toJson() const;
    /** @brief Apply the fields present in @p j through the setters and replace the row
     *  overrides. Missing fields keep their values; unknown enum names keep the current value.
     *  An older file without the sub-heading font, sub-heading color or title spacing takes
     *  them from the item font, item color and row spacing, as that legend drew them.
     * @param j Object written by toJson(). */
    void fromJson(const QJsonObject &j);

    // ── Title ───────────────────────────────────────────────────────────
    /** @brief Whether the title is shown. Initially false. */
    [[nodiscard]] bool showTitle() const noexcept { return m_showTitle; }
    /** @brief Title text. Initially empty. */
    [[nodiscard]] QString title() const { return m_title; }
    /** @brief Title font. Initially the application font. */
    [[nodiscard]] QFont titleFont() const { return m_titleFont; }
    /** @brief Title color. Initially RGB (20,20,20). */
    [[nodiscard]] QColor titleColor() const noexcept { return m_titleColor; }
    /** @brief Title alignment across the legend. Initially Left. */
    [[nodiscard]] TextAlignment titleAlignment() const noexcept { return m_titleAlignment; }
    /** @brief Space below the title, logical pixels. Initially 2, 0..200. */
    [[nodiscard]] int titleSpacing() const noexcept { return m_titleSpacing; }

    // ── Columns ─────────────────────────────────────────────────────────
    /** @brief Number of columns, 1..12. Initially 1. */
    [[nodiscard]] int columnCount() const noexcept { return m_columnCount; }
    /** @brief Whether every column takes the widest column's width. Initially false. */
    [[nodiscard]] bool equalColumnWidths() const noexcept { return m_equalColumnWidths; }
    /** @brief Whether a layer's rows may continue in the next column. Initially false. */
    [[nodiscard]] bool splitLayers() const noexcept { return m_splitLayers; }
    /** @brief Space between columns, logical pixels. Initially 16, 0..200. */
    [[nodiscard]] int columnSpacing() const noexcept { return m_columnSpacing; }
    /** @brief Whether a line is drawn between columns. Initially false. */
    [[nodiscard]] bool showColumnSeparators() const noexcept { return m_showColumnSeparators; }
    /** @brief Column separator color. Initially RGBA (80,80,80,120). */
    [[nodiscard]] QColor columnSeparatorColor() const noexcept { return m_columnSeparatorColor; }
    /** @brief Column separator width, logical pixels. Initially 1, 0..10. */
    [[nodiscard]] qreal columnSeparatorWidth() const noexcept { return m_columnSeparatorWidth; }

    // ── Size ────────────────────────────────────────────────────────────
    /** @brief Fixed box width, or 0 to fit the content. Initially 0. */
    [[nodiscard]] int explicitWidth() const noexcept { return m_explicitWidth; }
    /** @brief Fixed box height, or 0 to fit the content. Initially 0. */
    [[nodiscard]] int explicitHeight() const noexcept { return m_explicitHeight; }

    // ── Text ────────────────────────────────────────────────────────────
    /** @brief Layer (group) heading font. Initially the application font. */
    [[nodiscard]] QFont layerHeaderFont() const { return m_layerHeaderFont; }
    /** @brief Layer (group) heading color. Initially RGB (20,20,20). */
    [[nodiscard]] QColor layerHeaderColor() const noexcept { return m_layerHeaderColor; }
    /** @brief Layer heading alignment in its column. Initially Left. */
    [[nodiscard]] TextAlignment layerHeaderAlignment() const noexcept
    {
        return m_layerHeaderAlignment;
    }
    /** @brief Sub-heading (row without a symbol) font. Initially the application font. */
    [[nodiscard]] QFont subheadingFont() const { return m_subheadingFont; }
    /** @brief Sub-heading color. Initially RGB (20,20,20). */
    [[nodiscard]] QColor subheadingColor() const noexcept { return m_subheadingColor; }
    /** @brief Sub-heading alignment. Initially Left. */
    [[nodiscard]] TextAlignment subheadingAlignment() const noexcept
    {
        return m_subheadingAlignment;
    }
    /** @brief Row label font. Initially the application font. */
    [[nodiscard]] QFont itemFont() const { return m_itemFont; }
    /** @brief Row label color. Initially RGB (20,20,20). */
    [[nodiscard]] QColor itemColor() const noexcept { return m_itemColor; }
    /** @brief Row label alignment in its text space. Initially Left. */
    [[nodiscard]] TextAlignment itemAlignment() const noexcept { return m_itemAlignment; }
    /** @brief Shorten (Elide) or wrap labels wider than their space. Initially Elide. */
    [[nodiscard]] LabelOverflow labelOverflow() const noexcept { return m_labelOverflow; }
    /** @brief Widest label when the box width is automatic. Initially 220, 60..1200. */
    [[nodiscard]] int maxLabelWidth() const noexcept { return m_maxLabelWidth; }
    /** @brief Text at which labels also break when wrapping (e.g. "|"); empty for none. */
    [[nodiscard]] QString wrapString() const { return m_wrapString; }
    /** @brief Extra space between wrapped lines, logical pixels. Initially 0, 0..50. */
    [[nodiscard]] int lineSpacing() const noexcept { return m_lineSpacing; }

    // ── Symbols ─────────────────────────────────────────────────────────
    /** @brief Smallest symbol slot, logical pixels; also the fill patch size. Initially 14,
     *  1..200. */
    [[nodiscard]] int swatchSize() const noexcept { return m_swatchSize; }
    /** @brief Symbol slot width, or 0 to use swatchSize(). Initially 0, 0..200. */
    [[nodiscard]] int symbolWidth() const noexcept { return m_symbolWidth; }
    /** @brief Symbol slot height, or 0 to use swatchSize(). Initially 0, 0..200. */
    [[nodiscard]] int symbolHeight() const noexcept { return m_symbolHeight; }
    /** @brief MapSize: the slot grows to the symbol's map size; Fit: symbols shrink to the
     *  slot. Initially MapSize. */
    [[nodiscard]] SymbolSizeMode symbolSizeMode() const noexcept { return m_symbolSizeMode; }
    /** @brief Smallest marker diameter or line width drawn, or 0 for none. Initially 0. */
    [[nodiscard]] qreal minSymbolSize() const noexcept { return m_minSymbolSize; }
    /** @brief Largest marker diameter or line width drawn, or 0 for none. Initially 0. */
    [[nodiscard]] qreal maxSymbolSize() const noexcept { return m_maxSymbolSize; }
    /** @brief Symbol before (Left) or after (Right) the label. Initially Left. */
    [[nodiscard]] SymbolPosition symbolPosition() const noexcept { return m_symbolPosition; }
    /** @brief Space between a symbol and its label. Initially 6, 0..100. */
    [[nodiscard]] int symbolLabelSpacing() const noexcept { return m_symbolLabelSpacing; }
    /** @brief Line sample shape. Initially Horizontal. */
    [[nodiscard]] LinePatchShape linePatchShape() const noexcept { return m_linePatchShape; }
    /** @brief Fill sample shape. Initially Rectangle. */
    [[nodiscard]] AreaPatchShape areaPatchShape() const noexcept { return m_areaPatchShape; }
    /** @brief Whether fill samples are outlined. Initially true. */
    [[nodiscard]] bool showPatchOutline() const noexcept { return m_showPatchOutline; }
    /** @brief Fill sample outline color; invalid (the default) draws a darker shade of the
     *  fill. */
    [[nodiscard]] QColor patchOutlineColor() const noexcept { return m_patchOutlineColor; }
    /** @brief Fill sample outline width. Initially 1, 0..10. */
    [[nodiscard]] qreal patchOutlineWidth() const noexcept { return m_patchOutlineWidth; }

    // ── Spacing ─────────────────────────────────────────────────────────
    /** @brief Space below each row. Initially 2, 0..200. */
    [[nodiscard]] int rowSpacing() const noexcept { return m_rowSpacing; }
    /** @brief Space above each group heading except at the top of a column. Initially 6. */
    [[nodiscard]] int groupSpacing() const noexcept { return m_groupSpacing; }
    /** @brief Space below a group heading. Initially 0. */
    [[nodiscard]] int headerSpacing() const noexcept { return m_headerSpacing; }
    /** @brief Space above a sub-heading. Initially 0. */
    [[nodiscard]] int subheadingSpacing() const noexcept { return m_subheadingSpacing; }
    /** @brief Left indent of group headings. Initially 0. */
    [[nodiscard]] int groupIndent() const noexcept { return m_groupIndent; }
    /** @brief Left indent of sub-headings. Initially 0. */
    [[nodiscard]] int subheadingIndent() const noexcept { return m_subheadingIndent; }
    /** @brief Left indent of symbol rows. Initially 0. */
    [[nodiscard]] int itemIndent() const noexcept { return m_itemIndent; }

    // ── Content ─────────────────────────────────────────────────────────
    /** @brief Whether the legend follows the map: its layers in map order, new layers added.
     *  Off, the legend tree's order, groups and text are used. Initially true. */
    [[nodiscard]] bool autoUpdate() const noexcept { return m_autoUpdate; }

    // ── Color ramps ─────────────────────────────────────────────────────
    /** @brief Continuous ramps as one bar or as sampled swatches. Initially Bar. */
    [[nodiscard]] RampDisplay rampDisplay() const noexcept { return m_rampDisplay; }
    /** @brief Bar direction. Initially Vertical. */
    [[nodiscard]] RampOrientation rampOrientation() const noexcept { return m_rampOrientation; }
    /** @brief Bar length along the values, logical pixels. Initially 120, 40..600. */
    [[nodiscard]] int rampLength() const noexcept { return m_rampLength; }
    /** @brief Bar thickness, logical pixels. Initially 14, 4..80. */
    [[nodiscard]] int rampThickness() const noexcept { return m_rampThickness; }
    /** @brief Values written along the bar, ends included. Initially 5, 2..15. */
    [[nodiscard]] int rampTickCount() const noexcept { return m_rampTickCount; }
    /** @brief Vertical bars: high values at the top. Initially true. Horizontal bars run low to
     *  high left to right. */
    [[nodiscard]] bool rampHighAtTop() const noexcept { return m_rampHighAtTop; }
    /** @brief How values are written. Initially Auto (up to 4 significant digits). */
    [[nodiscard]] NumberFormat rampNumberFormat() const noexcept { return m_rampNumberFormat; }
    /** @brief Decimals for the Fixed and Scientific formats. Initially 2, 0..10. */
    [[nodiscard]] int rampDecimals() const noexcept { return m_rampDecimals; }
    /** @brief Whether the bar is outlined. Initially true. */
    [[nodiscard]] bool rampShowOutline() const noexcept { return m_rampShowOutline; }
    /** @brief Bar outline and tick mark color. Initially RGBA (80,80,80,180). */
    [[nodiscard]] QColor rampOutlineColor() const noexcept { return m_rampOutlineColor; }
    /** @brief Whether tick marks are drawn at the values. Initially true. */
    [[nodiscard]] bool rampShowTickMarks() const noexcept { return m_rampShowTickMarks; }
    /** @brief Whether triangles at the bar's ends show the below/above colors, when the ramp has
     *  them. Initially true. */
    [[nodiscard]] bool rampShowEndCaps() const noexcept { return m_rampShowEndCaps; }
    /** @brief Space between the bar (or its tick marks) and the values. Initially 4, 0..50. */
    [[nodiscard]] int rampLabelSpacing() const noexcept { return m_rampLabelSpacing; }
    /** @brief A value written the way the color bar writes it.
     * @param value Value. @return Text, without units. */
    [[nodiscard]] QString formatRampValue(double value) const;

public slots:
    /** @brief Show or hide the title. @param on New value. */
    void setShowTitle(bool on);
    /** @brief Set the title text. @param text New text. */
    void setTitle(const QString &text);
    /** @brief Set the title font. @param f New font. */
    void setTitleFont(const QFont &f);
    /** @brief Set the title color. @param c New color. */
    void setTitleColor(const QColor &c);
    /** @brief Set the title alignment. @param a New value. */
    void setTitleAlignment(OpenSWMM::Render::LegendOverlayStyle::TextAlignment a);
    /** @brief Set the space below the title. @param v Logical pixels, 0..200. */
    void setTitleSpacing(int v);
    /** @brief Set the number of columns. @param v Clamped to 1..12. */
    void setColumnCount(int v);
    /** @brief Give every column the widest column's width or not. @param on New value. */
    void setEqualColumnWidths(bool on);
    /** @brief Let a layer's rows continue in the next column or not. @param on New value. */
    void setSplitLayers(bool on);
    /** @brief Set the space between columns. @param v Logical pixels, 0..200. */
    void setColumnSpacing(int v);
    /** @brief Draw lines between columns or not. @param on New value. */
    void setShowColumnSeparators(bool on);
    /** @brief Set the column separator color. @param c New color. */
    void setColumnSeparatorColor(const QColor &c);
    /** @brief Set the column separator width. @param w Logical pixels, 0..10. */
    void setColumnSeparatorWidth(qreal w);
    /** @brief Set a fixed box width; negative is 0 (automatic), 1..79 is 80.
     * @param px Logical pixels. */
    void setExplicitWidth(int px);
    /** @brief Set a fixed box height; negative is 0 (automatic), 1..47 is 48.
     * @param px Logical pixels. */
    void setExplicitHeight(int px);
    /** @brief Set the layer heading font. @param f New font. */
    void setLayerHeaderFont(const QFont &f);
    /** @brief Set the layer heading color. @param c New color. */
    void setLayerHeaderColor(const QColor &c);
    /** @brief Set the layer heading alignment. @param a New value. */
    void setLayerHeaderAlignment(OpenSWMM::Render::LegendOverlayStyle::TextAlignment a);
    /** @brief Set the sub-heading font. @param f New font. */
    void setSubheadingFont(const QFont &f);
    /** @brief Set the sub-heading color. @param c New color. */
    void setSubheadingColor(const QColor &c);
    /** @brief Set the sub-heading alignment. @param a New value. */
    void setSubheadingAlignment(OpenSWMM::Render::LegendOverlayStyle::TextAlignment a);
    /** @brief Set the row label font. @param f New font. */
    void setItemFont(const QFont &f);
    /** @brief Set the row label color. @param c New color. */
    void setItemColor(const QColor &c);
    /** @brief Set the row label alignment. @param a New value. */
    void setItemAlignment(OpenSWMM::Render::LegendOverlayStyle::TextAlignment a);
    /** @brief Shorten or wrap long labels. @param o New value. */
    void setLabelOverflow(OpenSWMM::Render::LegendOverlayStyle::LabelOverflow o);
    /** @brief Set the widest label for an automatic width. @param px Clamped to 60..1200. */
    void setMaxLabelWidth(int px);
    /** @brief Set the text labels also break at. @param s Text; empty for none. */
    void setWrapString(const QString &s);
    /** @brief Set the extra space between wrapped lines. @param v Logical pixels, 0..50. */
    void setLineSpacing(int v);
    /** @brief Set the smallest symbol slot. @param v Logical pixels, 1..200. */
    void setSwatchSize(int v);
    /** @brief Set the symbol slot width. @param v Logical pixels, 0 (swatch size)..200. */
    void setSymbolWidth(int v);
    /** @brief Set the symbol slot height. @param v Logical pixels, 0 (swatch size)..200. */
    void setSymbolHeight(int v);
    /** @brief Set how symbols are sized. @param m New value. */
    void setSymbolSizeMode(OpenSWMM::Render::LegendOverlayStyle::SymbolSizeMode m);
    /** @brief Set the smallest drawn symbol size. @param v Logical pixels, 0 (none)..200. */
    void setMinSymbolSize(qreal v);
    /** @brief Set the largest drawn symbol size. @param v Logical pixels, 0 (none)..200. */
    void setMaxSymbolSize(qreal v);
    /** @brief Put symbols left or right of labels. @param p New value. */
    void setSymbolPosition(OpenSWMM::Render::LegendOverlayStyle::SymbolPosition p);
    /** @brief Set the space between a symbol and its label. @param v Logical pixels, 0..100. */
    void setSymbolLabelSpacing(int v);
    /** @brief Set the line sample shape. @param s New value. */
    void setLinePatchShape(OpenSWMM::Render::LegendOverlayStyle::LinePatchShape s);
    /** @brief Set the fill sample shape. @param s New value. */
    void setAreaPatchShape(OpenSWMM::Render::LegendOverlayStyle::AreaPatchShape s);
    /** @brief Outline fill samples or not. @param on New value. */
    void setShowPatchOutline(bool on);
    /** @brief Set the fill sample outline color; invalid = darker shade of the fill.
     * @param c New color. */
    void setPatchOutlineColor(const QColor &c);
    /** @brief Set the fill sample outline width. @param w Logical pixels, 0..10. */
    void setPatchOutlineWidth(qreal w);
    /** @brief Set the space below each row. @param v Logical pixels, 0..200. */
    void setRowSpacing(int v);
    /** @brief Set the space above group headings. @param v Logical pixels, 0..200. */
    void setGroupSpacing(int v);
    /** @brief Set the space below group headings. @param v Logical pixels, 0..200. */
    void setHeaderSpacing(int v);
    /** @brief Set the space above sub-headings. @param v Logical pixels, 0..200. */
    void setSubheadingSpacing(int v);
    /** @brief Set the group heading indent. @param v Logical pixels, 0..200. */
    void setGroupIndent(int v);
    /** @brief Set the sub-heading indent. @param v Logical pixels, 0..200. */
    void setSubheadingIndent(int v);
    /** @brief Set the symbol row indent. @param v Logical pixels, 0..200. */
    void setItemIndent(int v);
    /** @brief Follow the map or not. Turning it on keeps the per-layer and per-row settings but
     *  drops the tree's order, groups and text lines. @param on New value. */
    void setAutoUpdate(bool on);
    /** @brief Show ramps as a bar or as swatches. @param d New value. */
    void setRampDisplay(OpenSWMM::Render::LegendOverlayStyle::RampDisplay d);
    /** @brief Set the bar direction. @param o New value. */
    void setRampOrientation(OpenSWMM::Render::LegendOverlayStyle::RampOrientation o);
    /** @brief Set the bar length. @param v Logical pixels, 40..600. */
    void setRampLength(int v);
    /** @brief Set the bar thickness. @param v Logical pixels, 4..80. */
    void setRampThickness(int v);
    /** @brief Set how many values are written. @param v 2..15. */
    void setRampTickCount(int v);
    /** @brief Put high values at the top of vertical bars or not. @param on New value. */
    void setRampHighAtTop(bool on);
    /** @brief Set how values are written. @param f New value. */
    void setRampNumberFormat(OpenSWMM::Render::LegendOverlayStyle::NumberFormat f);
    /** @brief Set the decimals of the Fixed and Scientific formats. @param v 0..10. */
    void setRampDecimals(int v);
    /** @brief Outline the bar or not. @param on New value. */
    void setRampShowOutline(bool on);
    /** @brief Set the bar outline color. @param c New color. */
    void setRampOutlineColor(const QColor &c);
    /** @brief Draw tick marks or not. @param on New value. */
    void setRampShowTickMarks(bool on);
    /** @brief Draw end caps or not. @param on New value. */
    void setRampShowEndCaps(bool on);
    /** @brief Set the space between the bar and its values. @param v Logical pixels, 0..50. */
    void setRampLabelSpacing(int v);

signals:
    /*! \brief One row's override changed (emitted before changed()); not emitted by
     *         clearItemOverrides() or fromJson().
     * @param layerKey Layer identity. @param classKey Row key. */
    void itemOverrideChanged(const QString &layerKey, const QString &classKey);
    // Per-property notifications; each carries the stored (clamped) value.
    void showTitleChanged(bool value);           ///< @param value New value.
    void titleChanged(const QString &value);     ///< @param value New value.
    void titleFontChanged(const QFont &value);   ///< @param value New value.
    void titleColorChanged(const QColor &value); ///< @param value New value.
    void titleAlignmentChanged(OpenSWMM::Render::LegendOverlayStyle::TextAlignment value);
    void titleSpacingChanged(int value);                   ///< @param value New value.
    void columnCountChanged(int value);                    ///< @param value New value.
    void equalColumnWidthsChanged(bool value);             ///< @param value New value.
    void splitLayersChanged(bool value);                   ///< @param value New value.
    void columnSpacingChanged(int value);                  ///< @param value New value.
    void showColumnSeparatorsChanged(bool value);          ///< @param value New value.
    void columnSeparatorColorChanged(const QColor &value); ///< @param value New value.
    void columnSeparatorWidthChanged(qreal value);         ///< @param value New value.
    void explicitWidthChanged(int value);                  ///< @param value New value.
    void explicitHeightChanged(int value);                 ///< @param value New value.
    void layerHeaderFontChanged(const QFont &value);       ///< @param value New value.
    void layerHeaderColorChanged(const QColor &value);     ///< @param value New value.
    void layerHeaderAlignmentChanged(OpenSWMM::Render::LegendOverlayStyle::TextAlignment value);
    void subheadingFontChanged(const QFont &value);   ///< @param value New value.
    void subheadingColorChanged(const QColor &value); ///< @param value New value.
    void subheadingAlignmentChanged(OpenSWMM::Render::LegendOverlayStyle::TextAlignment value);
    void itemFontChanged(const QFont &value);   ///< @param value New value.
    void itemColorChanged(const QColor &value); ///< @param value New value.
    void itemAlignmentChanged(OpenSWMM::Render::LegendOverlayStyle::TextAlignment value);
    void labelOverflowChanged(OpenSWMM::Render::LegendOverlayStyle::LabelOverflow value);
    void maxLabelWidthChanged(int value);         ///< @param value New value.
    void wrapStringChanged(const QString &value); ///< @param value New value.
    void lineSpacingChanged(int value);           ///< @param value New value.
    void swatchSizeChanged(int value);            ///< @param value New value.
    void symbolWidthChanged(int value);           ///< @param value New value.
    void symbolHeightChanged(int value);          ///< @param value New value.
    void symbolSizeModeChanged(OpenSWMM::Render::LegendOverlayStyle::SymbolSizeMode value);
    void minSymbolSizeChanged(qreal value); ///< @param value New value.
    void maxSymbolSizeChanged(qreal value); ///< @param value New value.
    void symbolPositionChanged(OpenSWMM::Render::LegendOverlayStyle::SymbolPosition value);
    void symbolLabelSpacingChanged(int value); ///< @param value New value.
    void linePatchShapeChanged(OpenSWMM::Render::LegendOverlayStyle::LinePatchShape value);
    void areaPatchShapeChanged(OpenSWMM::Render::LegendOverlayStyle::AreaPatchShape value);
    void showPatchOutlineChanged(bool value);           ///< @param value New value.
    void patchOutlineColorChanged(const QColor &value); ///< @param value New value.
    void patchOutlineWidthChanged(qreal value);         ///< @param value New value.
    void rowSpacingChanged(int value);                  ///< @param value New value.
    void groupSpacingChanged(int value);                ///< @param value New value.
    void headerSpacingChanged(int value);               ///< @param value New value.
    void subheadingSpacingChanged(int value);           ///< @param value New value.
    void groupIndentChanged(int value);                 ///< @param value New value.
    void subheadingIndentChanged(int value);            ///< @param value New value.
    void itemIndentChanged(int value);                  ///< @param value New value.
    void autoUpdateChanged(bool value);                 ///< @param value New value.
    /** @brief The legend tree changed (emitted before changed()). */
    void itemTreeChanged();
    void rampDisplayChanged(OpenSWMM::Render::LegendOverlayStyle::RampDisplay value);
    void rampOrientationChanged(OpenSWMM::Render::LegendOverlayStyle::RampOrientation value);
    void rampLengthChanged(int value);     ///< @param value New value.
    void rampThicknessChanged(int value);  ///< @param value New value.
    void rampTickCountChanged(int value);  ///< @param value New value.
    void rampHighAtTopChanged(bool value); ///< @param value New value.
    void rampNumberFormatChanged(OpenSWMM::Render::LegendOverlayStyle::NumberFormat value);
    void rampDecimalsChanged(int value);               ///< @param value New value.
    void rampShowOutlineChanged(bool value);           ///< @param value New value.
    void rampOutlineColorChanged(const QColor &value); ///< @param value New value.
    void rampShowTickMarksChanged(bool value);         ///< @param value New value.
    void rampShowEndCapsChanged(bool value);           ///< @param value New value.
    void rampLabelSpacingChanged(int value);           ///< @param value New value.

private:
    bool m_showTitle = false;
    QString m_title;
    QFont m_titleFont;
    QColor m_titleColor = QColor(20, 20, 20);
    TextAlignment m_titleAlignment = TextAlignment::Left;
    int m_titleSpacing = 2;

    int m_columnCount = 1;
    bool m_equalColumnWidths = false;
    bool m_splitLayers = false;
    int m_columnSpacing = 16;
    bool m_showColumnSeparators = false;
    QColor m_columnSeparatorColor = QColor(80, 80, 80, 120);
    qreal m_columnSeparatorWidth = 1.0;

    int m_explicitWidth = 0; ///< 0 = fit the content
    int m_explicitHeight = 0;

    QFont m_layerHeaderFont;
    QColor m_layerHeaderColor = QColor(20, 20, 20);
    TextAlignment m_layerHeaderAlignment = TextAlignment::Left;
    QFont m_subheadingFont;
    QColor m_subheadingColor = QColor(20, 20, 20);
    TextAlignment m_subheadingAlignment = TextAlignment::Left;
    QFont m_itemFont;
    QColor m_itemColor = QColor(20, 20, 20);
    TextAlignment m_itemAlignment = TextAlignment::Left;
    LabelOverflow m_labelOverflow = LabelOverflow::Elide;
    int m_maxLabelWidth = 220;
    QString m_wrapString;
    int m_lineSpacing = 0;

    int m_swatchSize = 14;
    int m_symbolWidth = 0;
    int m_symbolHeight = 0;
    SymbolSizeMode m_symbolSizeMode = SymbolSizeMode::MapSize;
    qreal m_minSymbolSize = 0.0;
    qreal m_maxSymbolSize = 0.0;
    SymbolPosition m_symbolPosition = SymbolPosition::Left;
    int m_symbolLabelSpacing = 6;
    LinePatchShape m_linePatchShape = LinePatchShape::Horizontal;
    AreaPatchShape m_areaPatchShape = AreaPatchShape::Rectangle;
    bool m_showPatchOutline = true;
    QColor m_patchOutlineColor; ///< invalid = darker shade of the fill
    qreal m_patchOutlineWidth = 1.0;

    int m_rowSpacing = 2;
    int m_groupSpacing = 6;
    int m_headerSpacing = 0;
    int m_subheadingSpacing = 0;
    int m_groupIndent = 0;
    int m_subheadingIndent = 0;
    int m_itemIndent = 0;

    bool m_autoUpdate = true;

    RampDisplay m_rampDisplay = RampDisplay::Bar;
    RampOrientation m_rampOrientation = RampOrientation::Vertical;
    int m_rampLength = 120;
    int m_rampThickness = 14;
    int m_rampTickCount = 5;
    bool m_rampHighAtTop = true;
    NumberFormat m_rampNumberFormat = NumberFormat::Auto;
    int m_rampDecimals = 2;
    bool m_rampShowOutline = true;
    QColor m_rampOutlineColor = QColor(80, 80, 80, 180);
    bool m_rampShowTickMarks = true;
    bool m_rampShowEndCaps = true;
    int m_rampLabelSpacing = 4;
    LegendItemTree m_tree;
    // Row settings from an older file, keyed "<objectName>|<classKey>"; read-only.
    QHash<QString, ItemOverride> m_legacyOverrides;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LEGENDOVERLAYSTYLE_H
