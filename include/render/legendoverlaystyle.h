/**
 * @file      legendoverlaystyle.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Model for the map legend's look: title, fonts, spacing, symbol size, box size and
 *            per-row overrides, plus the box settings it shares with the date/time box.
 *
 *         Source-of-truth rule (MVC): the legend is edited from several places (the legend's
 *         context menu, the Legend Properties dialog, the Properties panel, the Legend dock).
 *         All of them write this object and follow its changed() signal; none keeps a copy.
 *         Per-class colors, sizes and symbols live on the layer's renderer, not here.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_LEGENDOVERLAYSTYLE_H
#define OPENSWMM_RENDER_LEGENDOVERLAYSTYLE_H

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
 *        changed() aggregate. Defaults reproduce the original legend: a translucent white
 *        rounded box, 8 px padding, 14 px swatches, anchored bottom-right.
 */
class LegendOverlayStyle : public MapBoxStyle
{
    Q_OBJECT

public:
    // ── Title and text ──────────────────────────────────────────────────
    Q_PROPERTY(bool showTitle READ showTitle WRITE setShowTitle NOTIFY showTitleChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QFont titleFont READ titleFont WRITE setTitleFont NOTIFY titleFontChanged)
    Q_PROPERTY(QColor titleColor READ titleColor WRITE setTitleColor NOTIFY titleColorChanged)
    Q_PROPERTY(QFont itemFont READ itemFont WRITE setItemFont NOTIFY itemFontChanged)
    Q_PROPERTY(QColor itemColor READ itemColor WRITE setItemColor NOTIFY itemColorChanged)
    Q_PROPERTY(QFont layerHeaderFont READ layerHeaderFont WRITE setLayerHeaderFont NOTIFY
                   layerHeaderFontChanged)
    Q_PROPERTY(QColor layerHeaderColor READ layerHeaderColor WRITE setLayerHeaderColor NOTIFY
                   layerHeaderColorChanged)
    Q_PROPERTY(int rowSpacing READ rowSpacing WRITE setRowSpacing NOTIFY rowSpacingChanged)
    Q_PROPERTY(int swatchSize READ swatchSize WRITE setSwatchSize NOTIFY swatchSizeChanged)

    // ── Size ────────────────────────────────────────────────────────────
    Q_PROPERTY(
        int explicitWidth READ explicitWidth WRITE setExplicitWidth NOTIFY explicitWidthChanged)
    Q_PROPERTY(
        int explicitHeight READ explicitHeight WRITE setExplicitHeight NOTIFY explicitHeightChanged)
    Q_PROPERTY(
        int maxLabelWidth READ maxLabelWidth WRITE setMaxLabelWidth NOTIFY maxLabelWidthChanged)

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

    /*! \brief The layer half of a row override key: the layer's QObject::objectName (stable
     *         across renames), so layers need unique object names.
     * @param layer Layer, or null.
     * @param classKey Unused; kept for existing callers.
     * @return The object name, or empty text for null. */
    static QString itemKey(const QObject *layer, const QString &classKey);

    /*! \brief The override stored for a row, or a visible, unrenamed default.
     * @param layerKey Layer identity (see itemKey()).
     * @param classKey Renderer row key.
     * @return Stored override or the default. */
    [[nodiscard]] ItemOverride itemOverride(const QString &layerKey, const QString &classKey) const;

    /*! \brief Replace a row's override; a visible, unrenamed value removes it. Emits
     *         itemOverrideChanged() and changed() only for an effective change.
     * @param layerKey Layer identity, used verbatim.
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

    /** @brief Remove every row override; one changed() when any existed. */
    void clearItemOverrides();

    /*! \brief Group-prefixed row labels for the Properties tree, e.g. "General — Title font".
     * @param propertyName Property name.
     * @return Label, or @p propertyName when unknown. */
    QString displayLabelFor(const QString &propertyName) const override;

    /** @brief Restore every default except the row overrides (each setter emits). */
    void resetToDefaults();

    /** @brief Save every field and the non-default row overrides.
     * @return JSON object; colors #AARRGGBB, enums by name, fonts as overlayFontToJson(), row
     *   overrides as a flat "layerKey|classKey" object. */
    [[nodiscard]] QJsonObject toJson() const;
    /** @brief Apply the fields present in @p j through the setters and replace the row
     *  overrides. Missing fields keep their values; unknown enum names keep the current value.
     * @param j Object written by toJson(). */
    void fromJson(const QJsonObject &j);

    /** @brief Whether the title is shown. Initially false. */
    [[nodiscard]] bool showTitle() const noexcept { return m_showTitle; }
    /** @brief Title text. Initially empty. */
    [[nodiscard]] QString title() const { return m_title; }
    /** @brief Title font. Initially the application font. */
    [[nodiscard]] QFont titleFont() const { return m_titleFont; }
    /** @brief Title color. Initially RGB (20,20,20). */
    [[nodiscard]] QColor titleColor() const noexcept { return m_titleColor; }
    /** @brief Row label font. Initially the application font. */
    [[nodiscard]] QFont itemFont() const { return m_itemFont; }
    /** @brief Row label color. Initially RGB (20,20,20). */
    [[nodiscard]] QColor itemColor() const noexcept { return m_itemColor; }
    /** @brief Layer (group) heading font. Initially the application font. */
    [[nodiscard]] QFont layerHeaderFont() const { return m_layerHeaderFont; }
    /** @brief Layer (group) heading color. Initially RGB (20,20,20). */
    [[nodiscard]] QColor layerHeaderColor() const noexcept { return m_layerHeaderColor; }
    /** @brief Space below each row, logical pixels. Initially 2. */
    [[nodiscard]] int rowSpacing() const noexcept { return m_rowSpacing; }
    /** @brief Smallest symbol slot, logical pixels. Initially 14. */
    [[nodiscard]] int swatchSize() const noexcept { return m_swatchSize; }
    /** @brief Fixed box width, or 0 to fit the content. Initially 0. */
    [[nodiscard]] int explicitWidth() const noexcept { return m_explicitWidth; }
    /** @brief Fixed box height, or 0 to fit the content. Initially 0. */
    [[nodiscard]] int explicitHeight() const noexcept { return m_explicitHeight; }
    /** @brief Widest label when the box width is automatic; longer labels are shortened.
     *  Initially 220, kept within 60..1200. */
    [[nodiscard]] int maxLabelWidth() const noexcept { return m_maxLabelWidth; }

public slots:
    /** @brief Show or hide the title. @param on New value. */
    void setShowTitle(bool on);
    /** @brief Set the title text. @param text New text. */
    void setTitle(const QString &text);
    /** @brief Set the title font. @param f New font. */
    void setTitleFont(const QFont &f);
    /** @brief Set the title color. @param c New color. */
    void setTitleColor(const QColor &c);
    /** @brief Set the row label font. @param f New font. */
    void setItemFont(const QFont &f);
    /** @brief Set the row label color. @param c New color. */
    void setItemColor(const QColor &c);
    /** @brief Set the layer heading font. @param f New font. */
    void setLayerHeaderFont(const QFont &f);
    /** @brief Set the layer heading color. @param c New color. */
    void setLayerHeaderColor(const QColor &c);
    /** @brief Set the space below each row. @param v Logical pixels. */
    void setRowSpacing(int v);
    /** @brief Set the smallest symbol slot. @param v Logical pixels. */
    void setSwatchSize(int v);
    /** @brief Set a fixed box width; negative is 0 (automatic), 1..79 is 80.
     * @param px Logical pixels. */
    void setExplicitWidth(int px);
    /** @brief Set a fixed box height; negative is 0 (automatic), 1..47 is 48.
     * @param px Logical pixels. */
    void setExplicitHeight(int px);
    /** @brief Set the widest label for an automatic width, clamped to 60..1200.
     * @param px Logical pixels. */
    void setMaxLabelWidth(int px);

signals:
    /*! \brief One row's override changed (emitted before changed()); not emitted by
     *         clearItemOverrides() or fromJson().
     * @param layerKey Layer identity. @param classKey Row key. */
    void itemOverrideChanged(const QString &layerKey, const QString &classKey);
    /** @param value New value. */
    void showTitleChanged(bool value);
    /** @param value New value. */
    void titleChanged(const QString &value);
    /** @param value New value. */
    void titleFontChanged(const QFont &value);
    /** @param value New value. */
    void titleColorChanged(const QColor &value);
    /** @param value New value. */
    void itemFontChanged(const QFont &value);
    /** @param value New value. */
    void itemColorChanged(const QColor &value);
    /** @param value New value. */
    void layerHeaderFontChanged(const QFont &value);
    /** @param value New value. */
    void layerHeaderColorChanged(const QColor &value);
    /** @param value New value. */
    void rowSpacingChanged(int value);
    /** @param value New value. */
    void swatchSizeChanged(int value);
    /** @param value New value. */
    void explicitWidthChanged(int value);
    /** @param value New value. */
    void explicitHeightChanged(int value);
    /** @param value New value. */
    void maxLabelWidthChanged(int value);

private:
    bool m_showTitle = false;
    QString m_title;
    QFont m_titleFont;
    QColor m_titleColor = QColor(20, 20, 20);
    QFont m_itemFont;
    QColor m_itemColor = QColor(20, 20, 20);
    QFont m_layerHeaderFont;
    QColor m_layerHeaderColor = QColor(20, 20, 20);
    int m_rowSpacing = 2;
    int m_swatchSize = 14;
    int m_explicitWidth = 0; ///< 0 = fit the content
    int m_explicitHeight = 0;
    int m_maxLabelWidth = 220;

    // Per-(layer, class) visibility and label, keyed "<layerKey>|<classKey>".
    QHash<QString, ItemOverride> m_itemOverrides;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LEGENDOVERLAYSTYLE_H
