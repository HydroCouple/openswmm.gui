/*!
 * \file   legendoverlaystyle.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Shared model for the on-canvas legend's chrome (font / frame /
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
 */
#ifndef OPENSWMM_RENDER_LEGENDOVERLAYSTYLE_H
#define OPENSWMM_RENDER_LEGENDOVERLAYSTYLE_H

#include <QColor>
#include <QFont>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <Qt>

namespace OpenSWMM::Render
{

/*!
 * \class LegendOverlayStyle
 * \brief Model object holding the on-canvas legend's chrome configuration.
 *
 *        Every setter emits the corresponding per-property signal and the
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
        TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, Free
    };
    Q_ENUM(Anchor)

    enum class BackgroundMode {
        None, Solid, Gradient
    };
    Q_ENUM(BackgroundMode)

    // ── General tab ─────────────────────────────────────────────────────
    Q_PROPERTY(bool         showTitle        READ showTitle        WRITE setShowTitle        NOTIFY showTitleChanged)
    Q_PROPERTY(QString      title            READ title            WRITE setTitle            NOTIFY titleChanged)
    Q_PROPERTY(QFont        titleFont        READ titleFont        WRITE setTitleFont        NOTIFY titleFontChanged)
    Q_PROPERTY(QColor       titleColor       READ titleColor       WRITE setTitleColor       NOTIFY titleColorChanged)
    Q_PROPERTY(QFont        itemFont         READ itemFont         WRITE setItemFont         NOTIFY itemFontChanged)
    Q_PROPERTY(QColor       itemColor        READ itemColor        WRITE setItemColor        NOTIFY itemColorChanged)
    Q_PROPERTY(QFont        layerHeaderFont  READ layerHeaderFont  WRITE setLayerHeaderFont  NOTIFY layerHeaderFontChanged)
    Q_PROPERTY(QColor       layerHeaderColor READ layerHeaderColor WRITE setLayerHeaderColor NOTIFY layerHeaderColorChanged)
    Q_PROPERTY(int          rowSpacing       READ rowSpacing       WRITE setRowSpacing       NOTIFY rowSpacingChanged)
    Q_PROPERTY(int          swatchSize       READ swatchSize       WRITE setSwatchSize       NOTIFY swatchSizeChanged)
    Q_PROPERTY(int          padding          READ padding          WRITE setPadding          NOTIFY paddingChanged)
    Q_PROPERTY(Anchor       anchor           READ anchor           WRITE setAnchor           NOTIFY anchorChanged)
    Q_PROPERTY(qreal        opacity          READ opacity          WRITE setOpacity          NOTIFY opacityChanged)

    // ── Frame tab ───────────────────────────────────────────────────────
    Q_PROPERTY(bool         showFrame        READ showFrame        WRITE setShowFrame        NOTIFY showFrameChanged)
    Q_PROPERTY(QColor       frameColor       READ frameColor       WRITE setFrameColor       NOTIFY frameColorChanged)
    Q_PROPERTY(qreal        frameWidth       READ frameWidth       WRITE setFrameWidth       NOTIFY frameWidthChanged)
    Q_PROPERTY(int          cornerRadius     READ cornerRadius     WRITE setCornerRadius     NOTIFY cornerRadiusChanged)

    // ── Background tab ──────────────────────────────────────────────────
    Q_PROPERTY(BackgroundMode backgroundMode      READ backgroundMode      WRITE setBackgroundMode      NOTIFY backgroundModeChanged)
    Q_PROPERTY(QColor         backgroundColor     READ backgroundColor     WRITE setBackgroundColor     NOTIFY backgroundColorChanged)
    Q_PROPERTY(QColor         gradientEndColor    READ gradientEndColor    WRITE setGradientEndColor    NOTIFY gradientEndColorChanged)
    Q_PROPERTY(Qt::Orientation gradientOrientation READ gradientOrientation WRITE setGradientOrientation NOTIFY gradientOrientationChanged)

    /*!
     * \struct ItemOverride
     * \brief Per-(layer, class) overrides for a legend row's visibility +
     *        label. Slice BB Phase 8.6.10 / 8.6.16 — the renderer remains
     *        the source of truth for swatch colour / size; ItemOverride
     *        only carries legend-only presentation overrides.
     */
    struct ItemOverride {
        bool    visible   = true;
        QString userLabel;   /*!< Empty = use renderer-supplied label. */
    };

    explicit LegendOverlayStyle(QObject *parent = nullptr);
    ~LegendOverlayStyle() override = default;

    /*! \brief Build the override hash key from a layer + classKey. Uses
     *         the layer's QObject::objectName (stable across rename) — so
     *         callers must set a unique objectName before relying on the
     *         override hash. Taking const QObject* keeps this header free
     *         of OpenSWMMVisLayer; callers pass their layer directly. */
    static QString itemKey(const QObject *layer, const QString &classKey);

    /*! \brief Read the override for \p layerKey + \p classKey, or a
     *         default-constructed ItemOverride (visible=true, empty label)
     *         when no entry exists. */
    [[nodiscard]] ItemOverride itemOverride(const QString &layerKey,
                                            const QString &classKey) const;

    /*! \brief Replace the override entry. visible == true + empty label
     *         removes the entry (defaults). Emits itemOverrideChanged()
     *         and changed(). */
    void setItemOverride(const QString &layerKey, const QString &classKey,
                         const ItemOverride &override);

    /*! \brief Convenience: set only the visible flag. */
    void setItemVisible(const QString &layerKey, const QString &classKey, bool visible);

    /*! \brief Convenience: set only the userLabel. */
    void setItemUserLabel(const QString &layerKey, const QString &classKey,
                           const QString &label);

    /*! \brief Drop every per-item override. */
    void clearItemOverrides();

    /*! \brief AT-style hook for QPropertyModel — returns human-readable,
     *         group-prefixed display labels (e.g. "General — Title font",
     *         "Frame — Color", "Background — Mode"). */
    Q_INVOKABLE QString displayLabelFor(const QString &propertyName) const;

    // Restore every field to its built-in default.
    void resetToDefaults();

    [[nodiscard]] QJsonObject toJson() const;
    void fromJson(const QJsonObject &j);

    // ── General getters ─────────────────────────────────────────────────
    [[nodiscard]] bool         showTitle()        const noexcept { return m_showTitle; }
    [[nodiscard]] QString      title()            const          { return m_title; }
    [[nodiscard]] QFont        titleFont()        const          { return m_titleFont; }
    [[nodiscard]] QColor       titleColor()       const noexcept { return m_titleColor; }
    [[nodiscard]] QFont        itemFont()         const          { return m_itemFont; }
    [[nodiscard]] QColor       itemColor()        const noexcept { return m_itemColor; }
    [[nodiscard]] QFont        layerHeaderFont()  const          { return m_layerHeaderFont; }
    [[nodiscard]] QColor       layerHeaderColor() const noexcept { return m_layerHeaderColor; }
    [[nodiscard]] int          rowSpacing()       const noexcept { return m_rowSpacing; }
    [[nodiscard]] int          swatchSize()       const noexcept { return m_swatchSize; }
    [[nodiscard]] int          padding()          const noexcept { return m_padding; }
    [[nodiscard]] Anchor       anchor()           const noexcept { return m_anchor; }
    [[nodiscard]] qreal        opacity()          const noexcept { return m_opacity; }

    // ── Frame getters ───────────────────────────────────────────────────
    [[nodiscard]] bool         showFrame()        const noexcept { return m_showFrame; }
    [[nodiscard]] QColor       frameColor()       const noexcept { return m_frameColor; }
    [[nodiscard]] qreal        frameWidth()       const noexcept { return m_frameWidth; }
    [[nodiscard]] int          cornerRadius()     const noexcept { return m_cornerRadius; }

    // ── Background getters ──────────────────────────────────────────────
    [[nodiscard]] BackgroundMode  backgroundMode()      const noexcept { return m_backgroundMode; }
    [[nodiscard]] QColor          backgroundColor()     const noexcept { return m_backgroundColor; }
    [[nodiscard]] QColor          gradientEndColor()    const noexcept { return m_gradientEndColor; }
    [[nodiscard]] Qt::Orientation gradientOrientation() const noexcept { return m_gradientOrientation; }

public slots:
    void setShowTitle(bool on);
    void setTitle(const QString &text);
    void setTitleFont(const QFont &f);
    void setTitleColor(const QColor &c);
    void setItemFont(const QFont &f);
    void setItemColor(const QColor &c);
    void setLayerHeaderFont(const QFont &f);
    void setLayerHeaderColor(const QColor &c);
    void setRowSpacing(int v);
    void setSwatchSize(int v);
    void setPadding(int v);
    void setAnchor(Anchor a);
    void setOpacity(qreal v);

    void setShowFrame(bool on);
    void setFrameColor(const QColor &c);
    void setFrameWidth(qreal w);
    void setCornerRadius(int r);

    void setBackgroundMode(BackgroundMode m);
    void setBackgroundColor(const QColor &c);
    void setGradientEndColor(const QColor &c);
    void setGradientOrientation(Qt::Orientation o);

signals:
    // Canonical aggregate — every per-property setter emits this too so
    // views with one-paint-per-edit semantics can subscribe to just this.
    void changed();

    /*! \brief Fine-grained signal for per-item override edits — lets
     *         views (e.g. the dock tree) update just one row instead of
     *         a full reset. */
    void itemOverrideChanged(const QString &layerKey, const QString &classKey);

    void showTitleChanged(bool);
    void titleChanged(const QString &);
    void titleFontChanged(const QFont &);
    void titleColorChanged(const QColor &);
    void itemFontChanged(const QFont &);
    void itemColorChanged(const QColor &);
    void layerHeaderFontChanged(const QFont &);
    void layerHeaderColorChanged(const QColor &);
    void rowSpacingChanged(int);
    void swatchSizeChanged(int);
    void paddingChanged(int);
    void anchorChanged(Anchor);
    void opacityChanged(qreal);

    void showFrameChanged(bool);
    void frameColorChanged(const QColor &);
    void frameWidthChanged(qreal);
    void cornerRadiusChanged(int);

    void backgroundModeChanged(BackgroundMode);
    void backgroundColorChanged(const QColor &);
    void gradientEndColorChanged(const QColor &);
    void gradientOrientationChanged(Qt::Orientation);

private:
    // General
    bool           m_showTitle        = false;
    QString        m_title;
    QFont          m_titleFont;
    QColor         m_titleColor       = QColor(20, 20, 20);
    QFont          m_itemFont;
    QColor         m_itemColor        = QColor(20, 20, 20);
    QFont          m_layerHeaderFont;
    QColor         m_layerHeaderColor = QColor(20, 20, 20);
    int            m_rowSpacing       = 2;
    int            m_swatchSize       = 14;
    int            m_padding          = 8;
    Anchor         m_anchor           = Anchor::BottomRight;
    qreal          m_opacity          = 1.0;

    // Frame
    bool           m_showFrame        = true;
    QColor         m_frameColor       = QColor(80, 80, 80, 180);
    qreal          m_frameWidth       = 1.0;
    int            m_cornerRadius     = 6;

    // Background
    BackgroundMode  m_backgroundMode      = BackgroundMode::Solid;
    QColor          m_backgroundColor     = QColor(255, 255, 255, 225);
    QColor          m_gradientEndColor    = QColor(240, 240, 240, 225);
    Qt::Orientation m_gradientOrientation = Qt::Vertical;

    // Slice BB Phase 8.6.10 / 8.6.16 — per-(layer, class) overrides for
    // visibility + label. Keyed by "<layerKey>|<classKey>". Persisted in
    // toJson/fromJson alongside chrome.
    QHash<QString, ItemOverride> m_itemOverrides;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LEGENDOVERLAYSTYLE_H
