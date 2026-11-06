/**
 * @file      legendoverlaystyle.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Shared model for the on-canvas legend's chrome (font / frame /
 *         background / anchor / opacity).
 *
 *         Slice BB Phase 8.6.16 — interim wiring on the QWidget-based
 *         LegendOverlay; same object is carried to the QGraphicsItem
 *         OnCanvasLegendOverlay in Phase 8.6.12 without re-implementation.
 *
 *         Source-of-truth rule (GUI_IMPLEMENTATION_PLAN.md §L.BB Phase
 *         8.6.16, MVC layering): the legend chrome is editable from
 *         multiple surfaces (right-click context menu, LegendPropertiesDialog,
 *         and eventually the LegendDock). All of them subscribe to this
 *         object's `changed()` signal; none of them keep a shadow copy.
 *         Per-class theming (colors / sizes / symbols) lives on the
 *         layer's IFeatureRenderer, not here.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_LEGENDOVERLAYSTYLE_H
#define OPENSWMM_RENDER_LEGENDOVERLAYSTYLE_H

#include <QColor>
#include <QFont>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <QString>
#include <Qt>

namespace OpenSWMM::Render
{

/*!
 * \class LegendOverlayStyle
 * \brief Model object holding the on-canvas legend's chrome configuration.
 *
 *        Effective property changes emit the corresponding per-property signal and the
 *        canonical `changed()` aggregate, so views can choose between
 *        fine-grained updates and a single repaint trigger.
 *
 *        Defaults reproduce the legacy hard-coded `LegendOverlay`
 *        appearance (translucent white rounded box, 8 px padding,
 *        14 px swatch, bottom-right anchor) so existing projects look
 *        unchanged until the user opens the Properties dialog.
 */
class LegendOverlayStyle : public QObject
{
    Q_OBJECT

public:
    enum class Anchor {
        TopLeft,
        Top,
        TopRight,
        Right,
        BottomRight,
        Bottom,
        BottomLeft,
        Left,
        Free
    };
    Q_ENUM(Anchor)

    enum class BackgroundMode { None, Solid, Gradient };
    Q_ENUM(BackgroundMode)

    // ── General tab ─────────────────────────────────────────────────────
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
    Q_PROPERTY(int padding READ padding WRITE setPadding NOTIFY paddingChanged)
    Q_PROPERTY(Anchor anchor READ anchor WRITE setAnchor NOTIFY anchorChanged)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)
    // Slice US.B1 — explicit sizing (0 = auto-fit), free position (used when
    // anchor == Free), and the per-row label wrap width.
    Q_PROPERTY(
        int explicitWidth READ explicitWidth WRITE setExplicitWidth NOTIFY explicitWidthChanged)
    Q_PROPERTY(
        int explicitHeight READ explicitHeight WRITE setExplicitHeight NOTIFY explicitHeightChanged)
    Q_PROPERTY(
        int maxLabelWidth READ maxLabelWidth WRITE setMaxLabelWidth NOTIFY maxLabelWidthChanged)

    // ── Frame tab ───────────────────────────────────────────────────────
    Q_PROPERTY(bool showFrame READ showFrame WRITE setShowFrame NOTIFY showFrameChanged)
    Q_PROPERTY(QColor frameColor READ frameColor WRITE setFrameColor NOTIFY frameColorChanged)
    Q_PROPERTY(qreal frameWidth READ frameWidth WRITE setFrameWidth NOTIFY frameWidthChanged)
    Q_PROPERTY(int cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY cornerRadiusChanged)

    // ── Background tab ──────────────────────────────────────────────────
    Q_PROPERTY(BackgroundMode backgroundMode READ backgroundMode WRITE setBackgroundMode NOTIFY
                   backgroundModeChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY
                   backgroundColorChanged)
    Q_PROPERTY(QColor gradientEndColor READ gradientEndColor WRITE setGradientEndColor NOTIFY
                   gradientEndColorChanged)
    Q_PROPERTY(Qt::Orientation gradientOrientation READ gradientOrientation WRITE
                   setGradientOrientation NOTIFY gradientOrientationChanged)

    /*!
     * \struct ItemOverride
     * \brief Per-(layer, class) overrides for a legend row's visibility +
     *        label. Slice BB Phase 8.6.10 / 8.6.16 — the renderer remains
     *        the source of truth for swatch colour / size; ItemOverride
     *        only carries legend-only presentation overrides.
     */
    struct ItemOverride {
        bool visible = true;
        QString userLabel; /*!< Empty = use renderer-supplied label. */
    };

    /** @brief Construct the default legend chrome and an empty row-override map.
     * @param parent Optional QObject owner. Default QFonts resolve through Qt's
     * application font settings; all mutations occur on the owning Qt thread.
     * Construction emits no change signals. */
    explicit LegendOverlayStyle(QObject *parent = nullptr);
    /** @brief Destroy stored chrome and override values along with QObject-owned children. */
    ~LegendOverlayStyle() override = default;

    /*! \brief Build the override hash key from a layer + classKey. Uses
     *         the layer's QObject::objectName (stable across rename) — so
     *         callers must set a unique objectName before relying on the
     *         override hash. Taking const QObject* keeps this header free
     *         of OpenSWMMVisLayer; callers pass their layer directly.
     * @param layer Optional borrowed QObject whose objectName supplies the layer key.
     * @param classKey Unused compatibility argument; it is not included in this helper's result.
     * @return Exact objectName, or empty for null; no combined class key is produced.
     * Callers maintain stable unique nonempty layer names separately. */
    static QString itemKey(const QObject *layer, const QString &classKey);

    /*! \brief Read the override for \p layerKey + \p classKey, or a
     *         default-constructed ItemOverride (visible=true, empty label)
     *         when no entry exists.
     * @param layerKey Exact stable layer identity text.
     * @param classKey Exact renderer class/row key.
     * @return Owned stored override or visible/empty-label defaults. Keys are
     * concatenated with an unescaped |, so callers must avoid ambiguous key pairs. */
    [[nodiscard]] ItemOverride itemOverride(const QString &layerKey, const QString &classKey) const;

    /*! \brief Replace the override entry. visible == true + empty label
     *         removes the entry (defaults). Emits itemOverrideChanged()
     *         and changed() only for an effective stored-map change.
     * @param layerKey Stable layer identity, used verbatim without validation.
     * @param classKey Renderer row key, used verbatim without validation.
     * @param override Value copied into the map, or removed for visible/empty-label
     * defaults. Equal stored entries and absent-default removal are no-ops.
     * Labels are not trimmed; whitespace-only labels remain explicit overrides. */
    void setItemOverride(const QString &layerKey, const QString &classKey,
                         const ItemOverride &override);

    /** @brief Edit row visibility while retaining its current user label.
     * @param layerKey Stable layer identity for the unescaped combined key.
     * @param classKey Exact renderer row key.
     * @param visible New row visibility. Delegates default removal, equality and
     * notification behavior to setItemOverride(). */
    void setItemVisible(const QString &layerKey, const QString &classKey, bool visible);

    /** @brief Edit a row label while retaining its current visibility.
     * @param layerKey Stable layer identity for the unescaped combined key.
     * @param classKey Exact renderer row key.
     * @param label Verbatim user text; empty restores the renderer label when the
     * row is shown. Delegates equality/default removal and signals to setItemOverride(). */
    void setItemUserLabel(const QString &layerKey, const QString &classKey, const QString &label);

    /** @brief Clear every row override and emit one aggregate changed() when nonempty.
     * Does not emit per-row itemOverrideChanged(); an already empty map is a no-op. */
    void clearItemOverrides();

    /*! \brief AT-style hook for QPropertyModel — returns human-readable,
     *         group-prefixed display labels (e.g. "General — Title font",
     *         "Frame — Color", "Background — Mode").
     * @param propertyName Exact case-sensitive property name to look up.
     * @return Hardcoded English display text, or the unchanged input for an
     * unknown key; no translation or normalization is performed here. */
    Q_INVOKABLE QString displayLabelFor(const QString &propertyName) const;

    /** @brief Restore chrome defaults through each ordinary setter.
     * Retains all per-row overrides. Each effective property reset emits its
     * own fine-grained/aggregate pair; this is not a single atomic notification. */
    void resetToDefaults();

    /** @brief Serialize all chrome values and nondefault row overrides.
     * @return Owned object with ARGB colors, enum-name tokens, structured font
     * fields and flat layerKey|classKey override keys. Pixel-sized font requests
     * are not represented separately; font persistence records pointSizeF().
     * Invalid/nonstandard values are serialized without model-level validation. */
    [[nodiscard]] QJsonObject toJson() const;
    /** @brief Apply present chrome fields through setters and replace the override map.
     * @param j Legend object; missing chrome keys retain current values. Unknown
     * anchor/background tokens retain current enum values; any orientation text
     * other than exact Horizontal selects Vertical. Colors and numeric values
     * follow their setters, including accepted invalid/negative/nonfinite values.
     * Existing overrides are always cleared, even when itemOverrides is absent.
     * Present override metadata emits one changed() after rebuilding it; absent
     * metadata clears the map without a dedicated signal. Chrome setter signals
     * can occur earlier; no per-row notifications or transactional rollback occur. */
    void fromJson(const QJsonObject &j);

    // ── General getters ─────────────────────────────────────────────────
    /** @brief Read whether the legend title is enabled.
     * @return Stored value copied without mutation. Initially false; hiding the title retains its
     * text and font. */
    [[nodiscard]] bool showTitle() const noexcept { return m_showTitle; }
    /** @brief Read the legend title text.
     * @return Stored value copied without mutation. Initially empty; text is stored without
     * trimming. */
    [[nodiscard]] QString title() const { return m_title; }
    /** @brief Read the legend title font.
     * @return Stored value copied without mutation. Initially a default QFont, resolved by
     * Qt/application font settings. */
    [[nodiscard]] QFont titleFont() const { return m_titleFont; }
    /** @brief Read the legend title color.
     * @return Stored value copied without mutation. Initially RGB (20,20,20); invalid colors can
     * be stored. */
    [[nodiscard]] QColor titleColor() const noexcept { return m_titleColor; }
    /** @brief Read the legend item-label font.
     * @return Stored value copied without mutation. Initially a default QFont; no font
     * availability validation occurs. */
    [[nodiscard]] QFont itemFont() const { return m_itemFont; }
    /** @brief Read the legend item-label color.
     * @return Stored value copied without mutation. Initially RGB (20,20,20); invalid colors can
     * be stored. */
    [[nodiscard]] QColor itemColor() const noexcept { return m_itemColor; }
    /** @brief Read the layer-heading font.
     * @return Stored value copied without mutation. Initially a default QFont; separate from item
     * and title fonts. */
    [[nodiscard]] QFont layerHeaderFont() const { return m_layerHeaderFont; }
    /** @brief Read the layer-heading color.
     * @return Stored value copied without mutation. Initially RGB (20,20,20); invalid colors can
     * be stored. */
    [[nodiscard]] QColor layerHeaderColor() const noexcept { return m_layerHeaderColor; }
    /** @brief Read the space between legend rows.
     * @return Stored value copied without mutation. Logical pixels, initially two. Negative values
     * are accepted by the model. */
    [[nodiscard]] int rowSpacing() const noexcept { return m_rowSpacing; }
    /** @brief Read the legend swatch extent.
     * @return Stored value copied without mutation. Logical pixels, initially fourteen; no
     * positivity check occurs. */
    [[nodiscard]] int swatchSize() const noexcept { return m_swatchSize; }
    /** @brief Read the legend interior padding.
     * @return Stored value copied without mutation. Logical pixels, initially eight; negative
     * values are accepted. */
    [[nodiscard]] int padding() const noexcept { return m_padding; }
    /** @brief Read the legend placement anchor.
     * @return Stored value copied without mutation. Initially BottomRight; unknown enum values are
     * stored without validation. */
    [[nodiscard]] Anchor anchor() const noexcept { return m_anchor; }
    /** @brief Read the overall legend opacity.
     * @return Stored value copied without mutation. Dimensionless, initially one; the model does
     * not clamp to [0,1] or reject nonfinite values. */
    [[nodiscard]] qreal opacity() const noexcept { return m_opacity; }

    // ── Slice US.B1 — size / position ───────────────────────────────────
    /** @brief Read the requested legend width.
     * @return Stored value copied without mutation. Logical pixels, initially zero for automatic
     * fit. */
    [[nodiscard]] int explicitWidth() const noexcept { return m_explicitWidth; }
    /** @brief Read the requested legend height.
     * @return Stored value copied without mutation. Logical pixels, initially zero for automatic
     * fit. */
    [[nodiscard]] int explicitHeight() const noexcept { return m_explicitHeight; }
    /** @brief Read the label wrapping width.
     * @return Stored value copied without mutation. Logical pixels, initially 220 and constrained
     * to [60,1200]. */
    [[nodiscard]] int maxLabelWidth() const noexcept { return m_maxLabelWidth; }
    /*! Top-left position in canvas pixels, honoured when anchor == Free.
     *  Not exposed in the Q_PROPERTY grid — set by interactive drag. */
    /** @brief Read the free-position anchor point.
     * @return Stored value copied without mutation. Top-left canvas position in logical pixels,
     * initially (0,0), used when anchor is Free. */
    [[nodiscard]] QPoint freePosition() const noexcept { return m_freePosition; }

    // ── Frame getters ───────────────────────────────────────────────────
    /** @brief Read whether the legend frame is enabled.
     * @return Stored value copied without mutation. Initially true; disabling retains frame
     * settings. */
    [[nodiscard]] bool showFrame() const noexcept { return m_showFrame; }
    /** @brief Read the legend frame color.
     * @return Stored value copied without mutation. Initially RGBA (80,80,80,180); invalid colors
     * can be stored. */
    [[nodiscard]] QColor frameColor() const noexcept { return m_frameColor; }
    /** @brief Read the legend frame width.
     * @return Stored value copied without mutation. Logical pixels, initially one; negative and
     * nonfinite values are not rejected. */
    [[nodiscard]] qreal frameWidth() const noexcept { return m_frameWidth; }
    /** @brief Read the rounded frame corner radius.
     * @return Stored value copied without mutation. Logical pixels, initially six; negative values
     * are accepted by the model. */
    [[nodiscard]] int cornerRadius() const noexcept { return m_cornerRadius; }

    // ── Background getters ──────────────────────────────────────────────
    /** @brief Read the legend background mode.
     * @return Stored value copied without mutation. Initially Solid; unknown enum values are
     * stored without validation. */
    [[nodiscard]] BackgroundMode backgroundMode() const noexcept { return m_backgroundMode; }
    /** @brief Read the solid or starting background color.
     * @return Stored value copied without mutation. Initially RGBA (255,255,255,225); invalid
     * colors can be stored. */
    [[nodiscard]] QColor backgroundColor() const noexcept { return m_backgroundColor; }
    /** @brief Read the ending gradient color.
     * @return Stored value copied without mutation. Initially RGBA (240,240,240,225); invalid
     * colors can be stored. */
    [[nodiscard]] QColor gradientEndColor() const noexcept { return m_gradientEndColor; }
    /** @brief Read the background gradient direction.
     * @return Stored value copied without mutation. Initially Vertical; the setter accepts any
     * Qt::Orientation value without validation. */
    [[nodiscard]] Qt::Orientation gradientOrientation() const noexcept
    {
        return m_gradientOrientation;
    }

public slots:
    /** @brief Set whether the legend title is enabled.
     * @param on New value for whether the legend title is enabled. Initially false; hiding the
     * title retains its text and font.
     * After effective equality comparison, a change stores the value, emits showTitleChanged(),
     * then changed(). Equal values emit neither signal. Uses ordinary equality without
     * normalization; NaN numeric values compare unequal. */
    void setShowTitle(bool on);
    /** @brief Set the legend title text.
     * @param text New value for the legend title text. Initially empty; text is stored without
     * trimming.
     * After effective equality comparison, a change stores the value, emits titleChanged(), then
     * changed(). Equal values emit neither signal. Uses ordinary equality without normalization;
     * NaN numeric values compare unequal. */
    void setTitle(const QString &text);
    /** @brief Set the legend title font.
     * @param f New value for the legend title font. Initially a default QFont, resolved by
     * Qt/application font settings.
     * After effective equality comparison, a change stores the value, emits titleFontChanged(),
     * then changed(). Equal values emit neither signal. Uses ordinary equality without
     * normalization; NaN numeric values compare unequal. */
    void setTitleFont(const QFont &f);
    /** @brief Set the legend title color.
     * @param c New value for the legend title color. Initially RGB (20,20,20); invalid colors can
     * be stored.
     * After effective equality comparison, a change stores the value, emits titleColorChanged(),
     * then changed(). Equal values emit neither signal. Uses ordinary equality without
     * normalization; NaN numeric values compare unequal. */
    void setTitleColor(const QColor &c);
    /** @brief Set the legend item-label font.
     * @param f New value for the legend item-label font. Initially a default QFont; no font
     * availability validation occurs.
     * After effective equality comparison, a change stores the value, emits itemFontChanged(),
     * then changed(). Equal values emit neither signal. Uses ordinary equality without
     * normalization; NaN numeric values compare unequal. */
    void setItemFont(const QFont &f);
    /** @brief Set the legend item-label color.
     * @param c New value for the legend item-label color. Initially RGB (20,20,20); invalid colors
     * can be stored.
     * After effective equality comparison, a change stores the value, emits itemColorChanged(),
     * then changed(). Equal values emit neither signal. Uses ordinary equality without
     * normalization; NaN numeric values compare unequal. */
    void setItemColor(const QColor &c);
    /** @brief Set the layer-heading font.
     * @param f New value for the layer-heading font. Initially a default QFont; separate from item
     * and title fonts.
     * After effective equality comparison, a change stores the value, emits
     * layerHeaderFontChanged(), then changed(). Equal values emit neither signal. Uses ordinary
     * equality without normalization; NaN numeric values compare unequal. */
    void setLayerHeaderFont(const QFont &f);
    /** @brief Set the layer-heading color.
     * @param c New value for the layer-heading color. Initially RGB (20,20,20); invalid colors can
     * be stored.
     * After effective equality comparison, a change stores the value, emits
     * layerHeaderColorChanged(), then changed(). Equal values emit neither signal. Uses ordinary
     * equality without normalization; NaN numeric values compare unequal. */
    void setLayerHeaderColor(const QColor &c);
    /** @brief Set the space between legend rows.
     * @param v New value for the space between legend rows. Logical pixels, initially two.
     * Negative values are accepted by the model.
     * After effective equality comparison, a change stores the value, emits rowSpacingChanged(),
     * then changed(). Equal values emit neither signal. Uses ordinary equality without
     * normalization; NaN numeric values compare unequal. */
    void setRowSpacing(int v);
    /** @brief Set the legend swatch extent.
     * @param v New value for the legend swatch extent. Logical pixels, initially fourteen; no
     * positivity check occurs.
     * After effective equality comparison, a change stores the value, emits swatchSizeChanged(),
     * then changed(). Equal values emit neither signal. Uses ordinary equality without
     * normalization; NaN numeric values compare unequal. */
    void setSwatchSize(int v);
    /** @brief Set the legend interior padding.
     * @param v New value for the legend interior padding. Logical pixels, initially eight;
     * negative values are accepted.
     * After effective equality comparison, a change stores the value, emits paddingChanged(), then
     * changed(). Equal values emit neither signal. Uses ordinary equality without normalization;
     * NaN numeric values compare unequal. */
    void setPadding(int v);
    /** @brief Set the legend placement anchor.
     * @param a New value for the legend placement anchor. Initially BottomRight; unknown enum
     * values are stored without validation.
     * After effective equality comparison, a change stores the value, emits anchorChanged(), then
     * changed(). Equal values emit neither signal. Uses ordinary equality without normalization;
     * NaN numeric values compare unequal. */
    void setAnchor(Anchor a);
    /** @brief Set the overall legend opacity.
     * @param v New value for the overall legend opacity. Dimensionless, initially one; the model
     * does not clamp to [0,1] or reject nonfinite values.
     * After effective equality comparison, a change stores the value, emits opacityChanged(), then
     * changed(). Equal values emit neither signal. Uses ordinary equality without normalization;
     * NaN numeric values compare unequal. */
    void setOpacity(qreal v);
    /** @brief Set the requested legend width.
     * @param px New value for the requested legend width. Logical pixels, initially zero for
     * automatic fit. Negative input becomes zero (auto); positive values below eighty become
     * eighty.
     * After effective equality comparison, a change stores the value, emits
     * explicitWidthChanged(), then changed(). Equal values emit neither signal. */
    void setExplicitWidth(int px);
    /** @brief Set the requested legend height.
     * @param px New value for the requested legend height. Logical pixels, initially zero for
     * automatic fit. Negative input becomes zero (auto); positive values below forty-eight become
     * forty-eight.
     * After effective equality comparison, a change stores the value, emits
     * explicitHeightChanged(), then changed(). Equal values emit neither signal. */
    void setExplicitHeight(int px);
    /** @brief Set the label wrapping width.
     * @param px New value for the label wrapping width. Logical pixels, initially 220 and
     * constrained to [60,1200]. Input clamps to [60,1200] before equality comparison.
     * After effective equality comparison, a change stores the value, emits
     * maxLabelWidthChanged(), then changed(). Equal values emit neither signal. */
    void setMaxLabelWidth(int px);
    /** @brief Set the free-position anchor point.
     * @param p New value for the free-position anchor point. Top-left canvas position in logical
     * pixels, initially (0,0), used when anchor is Free.
     * After effective equality comparison, a change stores the value, emits freePositionChanged(),
     * then changed(). Equal values emit neither signal. Uses ordinary equality without
     * normalization; NaN numeric values compare unequal. */
    void setFreePosition(const QPoint &p);

    /** @brief Set whether the legend frame is enabled.
     * @param on New value for whether the legend frame is enabled. Initially true; disabling
     * retains frame settings.
     * After effective equality comparison, a change stores the value, emits showFrameChanged(),
     * then changed(). Equal values emit neither signal. Uses ordinary equality without
     * normalization; NaN numeric values compare unequal. */
    void setShowFrame(bool on);
    /** @brief Set the legend frame color.
     * @param c New value for the legend frame color. Initially RGBA (80,80,80,180); invalid colors
     * can be stored.
     * After effective equality comparison, a change stores the value, emits frameColorChanged(),
     * then changed(). Equal values emit neither signal. Uses ordinary equality without
     * normalization; NaN numeric values compare unequal. */
    void setFrameColor(const QColor &c);
    /** @brief Set the legend frame width.
     * @param w New value for the legend frame width. Logical pixels, initially one; negative and
     * nonfinite values are not rejected.
     * After effective equality comparison, a change stores the value, emits frameWidthChanged(),
     * then changed(). Equal values emit neither signal. Uses ordinary equality without
     * normalization; NaN numeric values compare unequal. */
    void setFrameWidth(qreal w);
    /** @brief Set the rounded frame corner radius.
     * @param r New value for the rounded frame corner radius. Logical pixels, initially six;
     * negative values are accepted by the model.
     * After effective equality comparison, a change stores the value, emits cornerRadiusChanged(),
     * then changed(). Equal values emit neither signal. Uses ordinary equality without
     * normalization; NaN numeric values compare unequal. */
    void setCornerRadius(int r);

    /** @brief Set the legend background mode.
     * @param m New value for the legend background mode. Initially Solid; unknown enum values are
     * stored without validation.
     * After effective equality comparison, a change stores the value, emits
     * backgroundModeChanged(), then changed(). Equal values emit neither signal. Uses ordinary
     * equality without normalization; NaN numeric values compare unequal. */
    void setBackgroundMode(BackgroundMode m);
    /** @brief Set the solid or starting background color.
     * @param c New value for the solid or starting background color. Initially RGBA
     * (255,255,255,225); invalid colors can be stored.
     * After effective equality comparison, a change stores the value, emits
     * backgroundColorChanged(), then changed(). Equal values emit neither signal. Uses ordinary
     * equality without normalization; NaN numeric values compare unequal. */
    void setBackgroundColor(const QColor &c);
    /** @brief Set the ending gradient color.
     * @param c New value for the ending gradient color. Initially RGBA (240,240,240,225); invalid
     * colors can be stored.
     * After effective equality comparison, a change stores the value, emits
     * gradientEndColorChanged(), then changed(). Equal values emit neither signal. Uses ordinary
     * equality without normalization; NaN numeric values compare unequal. */
    void setGradientEndColor(const QColor &c);
    /** @brief Set the background gradient direction.
     * @param o New value for the background gradient direction. Initially Vertical; the setter
     * accepts any Qt::Orientation value without validation.
     * After effective equality comparison, a change stores the value, emits
     * gradientOrientationChanged(), then changed(). Equal values emit neither signal. Uses
     * ordinary equality without normalization; NaN numeric values compare unequal. */
    void setGradientOrientation(Qt::Orientation o);

signals:
    // Canonical aggregate — every per-property setter emits this too so
    // views with one-paint-per-edit semantics can subscribe to just this.
    /** @brief Notify an effective chrome edit, row-override edit/clear or bulk override load.
     * A multi-property load/reset can emit this repeatedly; it is not a transaction boundary. */
    void changed();

    /*! \brief Fine-grained signal for per-item override edits — lets
     *         views (e.g. the dock tree) update just one row instead of
     *         a full reset.
     * @param layerKey Exact layer identity whose override changed.
     * @param classKey Exact renderer row key whose override changed.
     * Emitted before changed() for setItemOverride() changes/removals; not emitted
     * by clearItemOverrides() or bulk fromJson() restoration. */
    void itemOverrideChanged(const QString &layerKey, const QString &classKey);

    /** @brief Notify a change to whether the legend title is enabled.
     * @param value Newly stored value, after any setter clamping. Initially false; hiding the
     * title retains its text and font.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void showTitleChanged(bool value);
    /** @brief Notify a change to the legend title text.
     * @param value Newly stored value, after any setter clamping. Initially empty; text is stored
     * without trimming.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void titleChanged(const QString &value);
    /** @brief Notify a change to the legend title font.
     * @param value Newly stored value, after any setter clamping. Initially a default QFont,
     * resolved by Qt/application font settings.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void titleFontChanged(const QFont &value);
    /** @brief Notify a change to the legend title color.
     * @param value Newly stored value, after any setter clamping. Initially RGB (20,20,20);
     * invalid colors can be stored.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void titleColorChanged(const QColor &value);
    /** @brief Notify a change to the legend item-label font.
     * @param value Newly stored value, after any setter clamping. Initially a default QFont; no
     * font availability validation occurs.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void itemFontChanged(const QFont &value);
    /** @brief Notify a change to the legend item-label color.
     * @param value Newly stored value, after any setter clamping. Initially RGB (20,20,20);
     * invalid colors can be stored.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void itemColorChanged(const QColor &value);
    /** @brief Notify a change to the layer-heading font.
     * @param value Newly stored value, after any setter clamping. Initially a default QFont;
     * separate from item and title fonts.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void layerHeaderFontChanged(const QFont &value);
    /** @brief Notify a change to the layer-heading color.
     * @param value Newly stored value, after any setter clamping. Initially RGB (20,20,20);
     * invalid colors can be stored.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void layerHeaderColorChanged(const QColor &value);
    /** @brief Notify a change to the space between legend rows.
     * @param value Newly stored value, after any setter clamping. Logical pixels, initially two.
     * Negative values are accepted by the model.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void rowSpacingChanged(int value);
    /** @brief Notify a change to the legend swatch extent.
     * @param value Newly stored value, after any setter clamping. Logical pixels, initially
     * fourteen; no positivity check occurs.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void swatchSizeChanged(int value);
    /** @brief Notify a change to the legend interior padding.
     * @param value Newly stored value, after any setter clamping. Logical pixels, initially eight;
     * negative values are accepted.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void paddingChanged(int value);
    /** @brief Notify a change to the legend placement anchor.
     * @param value Newly stored value, after any setter clamping. Initially BottomRight; unknown
     * enum values are stored without validation.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void anchorChanged(Anchor value);
    /** @brief Notify a change to the overall legend opacity.
     * @param value Newly stored value, after any setter clamping. Dimensionless, initially one;
     * the model does not clamp to [0,1] or reject nonfinite values.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void opacityChanged(qreal value);
    /** @brief Notify a change to the requested legend width.
     * @param value Newly stored value, after any setter clamping. Logical pixels, initially zero
     * for automatic fit.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void explicitWidthChanged(int value);
    /** @brief Notify a change to the requested legend height.
     * @param value Newly stored value, after any setter clamping. Logical pixels, initially zero
     * for automatic fit.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void explicitHeightChanged(int value);
    /** @brief Notify a change to the label wrapping width.
     * @param value Newly stored value, after any setter clamping. Logical pixels, initially 220
     * and constrained to [60,1200].
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void maxLabelWidthChanged(int value);
    /** @brief Notify a change to the free-position anchor point.
     * @param value Newly stored value, after any setter clamping. Top-left canvas position in
     * logical pixels, initially (0,0), used when anchor is Free.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void freePositionChanged(const QPoint &value);

    /** @brief Notify a change to whether the legend frame is enabled.
     * @param value Newly stored value, after any setter clamping. Initially true; disabling
     * retains frame settings.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void showFrameChanged(bool value);
    /** @brief Notify a change to the legend frame color.
     * @param value Newly stored value, after any setter clamping. Initially RGBA (80,80,80,180);
     * invalid colors can be stored.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void frameColorChanged(const QColor &value);
    /** @brief Notify a change to the legend frame width.
     * @param value Newly stored value, after any setter clamping. Logical pixels, initially one;
     * negative and nonfinite values are not rejected.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void frameWidthChanged(qreal value);
    /** @brief Notify a change to the rounded frame corner radius.
     * @param value Newly stored value, after any setter clamping. Logical pixels, initially six;
     * negative values are accepted by the model.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void cornerRadiusChanged(int value);

    /** @brief Notify a change to the legend background mode.
     * @param value Newly stored value, after any setter clamping. Initially Solid; unknown enum
     * values are stored without validation.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void backgroundModeChanged(BackgroundMode value);
    /** @brief Notify a change to the solid or starting background color.
     * @param value Newly stored value, after any setter clamping. Initially RGBA
     * (255,255,255,225); invalid colors can be stored.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void backgroundColorChanged(const QColor &value);
    /** @brief Notify a change to the ending gradient color.
     * @param value Newly stored value, after any setter clamping. Initially RGBA
     * (240,240,240,225); invalid colors can be stored.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void gradientEndColorChanged(const QColor &value);
    /** @brief Notify a change to the background gradient direction.
     * @param value Newly stored value, after any setter clamping. Initially Vertical; the setter
     * accepts any Qt::Orientation value without validation.
     * Emitted before the aggregate changed() signal; the model value is already updated. */
    void gradientOrientationChanged(Qt::Orientation value);

private:
    // General
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
    int m_padding = 8;
    Anchor m_anchor = Anchor::BottomRight;
    qreal m_opacity = 1.0;

    // Slice US.B1 — size / position. 0 = auto-fit on that axis.
    int m_explicitWidth = 0;
    int m_explicitHeight = 0;
    int m_maxLabelWidth = 220;
    QPoint m_freePosition;

    // Frame
    bool m_showFrame = true;
    QColor m_frameColor = QColor(80, 80, 80, 180);
    qreal m_frameWidth = 1.0;
    int m_cornerRadius = 6;

    // Background
    BackgroundMode m_backgroundMode = BackgroundMode::Solid;
    QColor m_backgroundColor = QColor(255, 255, 255, 225);
    QColor m_gradientEndColor = QColor(240, 240, 240, 225);
    Qt::Orientation m_gradientOrientation = Qt::Vertical;

    // Slice BB Phase 8.6.10 / 8.6.16 — per-(layer, class) overrides for
    // visibility + label. Keyed by "<layerKey>|<classKey>". Persisted in
    // toJson/fromJson alongside chrome.
    QHash<QString, ItemOverride> m_itemOverrides;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LEGENDOVERLAYSTYLE_H
