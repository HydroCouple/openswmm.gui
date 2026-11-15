/**
 * @file      mapboxstyle.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Model for the look and placement of a box drawn on the map (the legend, the
 *            animation date/time box): anchor, opacity, padding, frame and background.
 *
 *         LegendOverlayStyle and TimeOverlayStyle derive from this class, so both boxes offer
 *         the same box settings under the same property names and saved keys. Views follow the
 *         changed() signal; none keeps a copy.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_MAPBOXSTYLE_H
#define OPENSWMM_RENDER_MAPBOXSTYLE_H

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <QString>
#include <Qt>

namespace OpenSWMM::Render
{

/*!
 * \class MapBoxStyle
 * \brief Box settings shared by every box drawn on the map.
 *
 *        Each effective change emits its own signal and then changed(). Setters ignore a value
 *        equal to the stored one.
 */
class MapBoxStyle : public QObject
{
    Q_OBJECT

public:
    /** @brief Where the box sits on the map. Free uses freePosition(). */
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

    /** @brief How the box background is filled. */
    enum class BackgroundMode { None, Solid, Gradient };
    Q_ENUM(BackgroundMode)

    // ── General ─────────────────────────────────────────────────────────
    Q_PROPERTY(int padding READ padding WRITE setPadding NOTIFY paddingChanged)
    Q_PROPERTY(Anchor anchor READ anchor WRITE setAnchor NOTIFY anchorChanged)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)

    // ── Frame ───────────────────────────────────────────────────────────
    Q_PROPERTY(bool showFrame READ showFrame WRITE setShowFrame NOTIFY showFrameChanged)
    Q_PROPERTY(QColor frameColor READ frameColor WRITE setFrameColor NOTIFY frameColorChanged)
    Q_PROPERTY(qreal frameWidth READ frameWidth WRITE setFrameWidth NOTIFY frameWidthChanged)
    Q_PROPERTY(int cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY cornerRadiusChanged)

    // ── Background ──────────────────────────────────────────────────────
    Q_PROPERTY(BackgroundMode backgroundMode READ backgroundMode WRITE setBackgroundMode NOTIFY
                   backgroundModeChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY
                   backgroundColorChanged)
    Q_PROPERTY(QColor gradientEndColor READ gradientEndColor WRITE setGradientEndColor NOTIFY
                   gradientEndColorChanged)
    Q_PROPERTY(Qt::Orientation gradientOrientation READ gradientOrientation WRITE
                   setGradientOrientation NOTIFY gradientOrientationChanged)

    /** @brief Destroy the style. */
    ~MapBoxStyle() override = default;

    /** @brief Group-prefixed row labels for the box properties (QPropertyModel hook).
     * @param propertyName Property name.
     * @return Label such as "Frame — Color", or @p propertyName when it is not a box property.
     *   Derived classes add their own names and call this for the rest. */
    Q_INVOKABLE virtual QString displayLabelFor(const QString &propertyName) const;

    /** @brief Space between the box edge and its content, logical pixels. Initially 8. */
    [[nodiscard]] int padding() const noexcept { return m_padding; }
    /** @brief Placement on the map. Free uses freePosition(). */
    [[nodiscard]] Anchor anchor() const noexcept { return m_anchor; }
    /** @brief Opacity of the whole box, 0..1. Initially 1. */
    [[nodiscard]] qreal opacity() const noexcept { return m_opacity; }
    /** @brief Top-left position in canvas pixels, used when anchor() is Free. Set by dragging. */
    [[nodiscard]] QPoint freePosition() const noexcept { return m_freePosition; }
    /** @brief Whether the frame is drawn. Initially true. */
    [[nodiscard]] bool showFrame() const noexcept { return m_showFrame; }
    /** @brief Frame color. Initially RGBA (80,80,80,180). */
    [[nodiscard]] QColor frameColor() const noexcept { return m_frameColor; }
    /** @brief Frame width, logical pixels. Initially 1. */
    [[nodiscard]] qreal frameWidth() const noexcept { return m_frameWidth; }
    /** @brief Corner radius, logical pixels. Initially 6. */
    [[nodiscard]] int cornerRadius() const noexcept { return m_cornerRadius; }
    /** @brief Background fill: None, Solid or Gradient. Initially Solid. */
    [[nodiscard]] BackgroundMode backgroundMode() const noexcept { return m_backgroundMode; }
    /** @brief Solid color, or the gradient's start color. Initially RGBA (255,255,255,225). */
    [[nodiscard]] QColor backgroundColor() const noexcept { return m_backgroundColor; }
    /** @brief Gradient end color. Initially RGBA (240,240,240,225). */
    [[nodiscard]] QColor gradientEndColor() const noexcept { return m_gradientEndColor; }
    /** @brief Gradient direction. Initially Vertical. */
    [[nodiscard]] Qt::Orientation gradientOrientation() const noexcept
    {
        return m_gradientOrientation;
    }

public slots:
    /** @brief Set the padding. @param v Logical pixels. */
    void setPadding(int v);
    /** @brief Set the placement. @param a New anchor. */
    void setAnchor(OpenSWMM::Render::MapBoxStyle::Anchor a);
    /** @brief Set the opacity. @param v 0..1. */
    void setOpacity(qreal v);
    /** @brief Set the free position. @param p Top-left in canvas pixels. */
    void setFreePosition(const QPoint &p);
    /** @brief Draw the frame or not. @param on New value. */
    void setShowFrame(bool on);
    /** @brief Set the frame color. @param c New color. */
    void setFrameColor(const QColor &c);
    /** @brief Set the frame width. @param w Logical pixels. */
    void setFrameWidth(qreal w);
    /** @brief Set the corner radius. @param r Logical pixels. */
    void setCornerRadius(int r);
    /** @brief Set the background fill. @param m New mode. */
    void setBackgroundMode(OpenSWMM::Render::MapBoxStyle::BackgroundMode m);
    /** @brief Set the solid or gradient start color. @param c New color. */
    void setBackgroundColor(const QColor &c);
    /** @brief Set the gradient end color. @param c New color. */
    void setGradientEndColor(const QColor &c);
    /** @brief Set the gradient direction. @param o New orientation. */
    void setGradientOrientation(Qt::Orientation o);

signals:
    /** @brief Any effective change; emitted after the per-property signal. */
    void changed();
    /** @param value New padding. */
    void paddingChanged(int value);
    /** @param value New anchor. */
    void anchorChanged(OpenSWMM::Render::MapBoxStyle::Anchor value);
    /** @param value New opacity. */
    void opacityChanged(qreal value);
    /** @param value New free position. */
    void freePositionChanged(const QPoint &value);
    /** @param value New frame visibility. */
    void showFrameChanged(bool value);
    /** @param value New frame color. */
    void frameColorChanged(const QColor &value);
    /** @param value New frame width. */
    void frameWidthChanged(qreal value);
    /** @param value New corner radius. */
    void cornerRadiusChanged(int value);
    /** @param value New background mode. */
    void backgroundModeChanged(OpenSWMM::Render::MapBoxStyle::BackgroundMode value);
    /** @param value New background color. */
    void backgroundColorChanged(const QColor &value);
    /** @param value New gradient end color. */
    void gradientEndColorChanged(const QColor &value);
    /** @param value New gradient direction. */
    void gradientOrientationChanged(Qt::Orientation value);

protected:
    /** @brief Construct the default box settings.
     * @param defaultAnchor Anchor this kind of box starts at and resets to.
     * @param parent Optional QObject owner. */
    explicit MapBoxStyle(Anchor defaultAnchor, QObject *parent = nullptr);

    /** @brief Restore the default box settings through the setters (each emits). */
    void resetBoxToDefaults();
    /** @brief Write every box field into @p j (colors #AARRGGBB, enums by name).
     * @param j Object the derived class is building. */
    void writeBoxJson(QJsonObject &j) const;
    /** @brief Apply the box fields present in @p j; missing fields and unknown enum names keep
     *  the current value.
     * @param j Object written by writeBoxJson(). */
    void readBoxJson(const QJsonObject &j);

private:
    Anchor m_defaultAnchor;
    int m_padding = 8;
    Anchor m_anchor;
    qreal m_opacity = 1.0;
    QPoint m_freePosition;

    bool m_showFrame = true;
    QColor m_frameColor = QColor(80, 80, 80, 180);
    qreal m_frameWidth = 1.0;
    int m_cornerRadius = 6;

    BackgroundMode m_backgroundMode = BackgroundMode::Solid;
    QColor m_backgroundColor = QColor(255, 255, 255, 225);
    QColor m_gradientEndColor = QColor(240, 240, 240, 225);
    Qt::Orientation m_gradientOrientation = Qt::Vertical;
};

/** @brief Save a font the way the map box styles do (family, point size, weight, italic,
 * underline, strikeOut); shared by the legend and the date/time box.
 * @param f Font to save.
 * @return JSON object; a pixel-sized font is saved by its pointSizeF(). */
[[nodiscard]] QJsonObject overlayFontToJson(const QFont &f);
/** @brief Read a font saved by overlayFontToJson().
 * @param j Saved object; missing fields, and a point size that is not positive, keep
 *   @p fallback's value.
 * @param fallback Font the saved fields are applied over.
 * @return The resulting font. */
[[nodiscard]] QFont overlayFontFromJson(const QJsonObject &j, const QFont &fallback);

/** @brief Decode a Qt meta-enum key exactly (no trimming or case folding).
 * @tparam Enum Enum registered with Q_ENUM.
 * @param name Key text.
 * @param fallback Value returned when @p name is not a key.
 * @return The matching value, or @p fallback. */
template <typename Enum> Enum mapBoxEnumFromString(const QString &name, Enum fallback);
/** @brief Encode an enum value by its registered key name.
 * @tparam Enum Enum registered with Q_ENUM.
 * @param v Value.
 * @return Key name, or empty text for an unregistered value. */
template <typename Enum> QString mapBoxEnumToString(Enum v);

} // namespace OpenSWMM::Render

#include <QMetaEnum>

namespace OpenSWMM::Render
{
template <typename Enum> Enum mapBoxEnumFromString(const QString &name, Enum fallback)
{
    bool ok = false;
    const int v = QMetaEnum::fromType<Enum>().keyToValue(name.toUtf8().constData(), &ok);
    return ok ? static_cast<Enum>(v) : fallback;
}

template <typename Enum> QString mapBoxEnumToString(Enum v)
{
    return QString::fromLatin1(QMetaEnum::fromType<Enum>().valueToKey(static_cast<int>(v)));
}
} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MAPBOXSTYLE_H
