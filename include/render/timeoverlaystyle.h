/**
 * @file      timeoverlaystyle.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Model for the map's animation date/time box: its text format, font and the same
 *            box styling as the legend (anchor, frame, background, opacity), plus whether it
 *            is shown.
 *
 *         Views (the box on the map, its Properties dialog, the animation toolbar's checkbox)
 *         all read and write this object and follow its changed() signal; none keeps a copy.
 *         Saved per project window in the .oswp session.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_TIMEOVERLAYSTYLE_H
#define OPENSWMM_RENDER_TIMEOVERLAYSTYLE_H

#include "render/legendoverlaystyle.h"

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <QString>
#include <Qt>

namespace OpenSWMM::Render
{

/*!
 * \class TimeOverlayStyle
 * \brief What the map's date/time box shows and how it looks.
 *
 *        Box properties use the legend's names, defaults and saved tokens. Each effective
 *        change emits its own signal and then changed().
 */
class TimeOverlayStyle : public QObject
{
    Q_OBJECT

public:
    using Anchor = LegendOverlayStyle::Anchor;
    using BackgroundMode = LegendOverlayStyle::BackgroundMode;

    /// Default date/time format (Qt QDateTime::toString format), e.g. 03/14/2026 06:30:00.
    static constexpr const char *kDefaultFormat = "MM/dd/yyyy hh:mm:ss";

    // ── General ─────────────────────────────────────────────────────────
    Q_PROPERTY(QString format READ format WRITE setFormat NOTIFY formatChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY textColorChanged)
    Q_PROPERTY(int padding READ padding WRITE setPadding NOTIFY paddingChanged)
    Q_PROPERTY(OpenSWMM::Render::LegendOverlayStyle::Anchor anchor READ anchor WRITE setAnchor
                   NOTIFY anchorChanged)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)

    // ── Frame ───────────────────────────────────────────────────────────
    Q_PROPERTY(bool showFrame READ showFrame WRITE setShowFrame NOTIFY showFrameChanged)
    Q_PROPERTY(QColor frameColor READ frameColor WRITE setFrameColor NOTIFY frameColorChanged)
    Q_PROPERTY(qreal frameWidth READ frameWidth WRITE setFrameWidth NOTIFY frameWidthChanged)
    Q_PROPERTY(int cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY cornerRadiusChanged)

    // ── Background ──────────────────────────────────────────────────────
    Q_PROPERTY(OpenSWMM::Render::LegendOverlayStyle::BackgroundMode backgroundMode READ
                   backgroundMode WRITE setBackgroundMode NOTIFY backgroundModeChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY
                   backgroundColorChanged)
    Q_PROPERTY(QColor gradientEndColor READ gradientEndColor WRITE setGradientEndColor NOTIFY
                   gradientEndColorChanged)
    Q_PROPERTY(Qt::Orientation gradientOrientation READ gradientOrientation WRITE
                   setGradientOrientation NOTIFY gradientOrientationChanged)

    /** @brief Construct the default style: hidden, top-left, legend-like box.
     * @param parent Optional QObject owner. */
    explicit TimeOverlayStyle(QObject *parent = nullptr);
    /** @brief Destroy the style. */
    ~TimeOverlayStyle() override = default;

    /** @brief Group-prefixed row labels for the Properties tree (QPropertyModel hook).
     * @param propertyName Property name.
     * @return Label such as "Frame — Color", or @p propertyName when unknown. */
    Q_INVOKABLE QString displayLabelFor(const QString &propertyName) const;

    /** @brief Restore every default except whether the box is shown. */
    void resetToDefaults();

    /** @brief Save every field, including whether the box is shown.
     * @return JSON object; colors as #AARRGGBB, enums by name, fonts as overlayFontToJson(). */
    [[nodiscard]] QJsonObject toJson() const;
    /** @brief Apply the fields present in @p j through the setters; missing fields keep their
     * current values and unknown enum names keep the current value.
     * @param j Object written by toJson(). */
    void fromJson(const QJsonObject &j);

    /** @brief Text the box shows for an animation time.
     * @param dt Animation time (UTC wall clock); converted to UTC as the toolbar's time box does.
     * @return The time in format() (the default format when format() is empty), or an empty
     *   string when @p dt is invalid. */
    [[nodiscard]] QString textFor(const QDateTime &dt) const;

    /** @brief Whether the box is shown on the map. Initially false. */
    [[nodiscard]] bool isShown() const noexcept { return m_shown; }
    /** @brief Date/time format (Qt QDateTime::toString format; quoted text is literal). */
    [[nodiscard]] QString format() const { return m_format; }
    /** @brief Text font. Initially the application font. */
    [[nodiscard]] QFont font() const { return m_font; }
    /** @brief Text color. Initially RGB (20,20,20). */
    [[nodiscard]] QColor textColor() const noexcept { return m_textColor; }
    /** @brief Space between the text and the box edge, logical pixels. Initially 8. */
    [[nodiscard]] int padding() const noexcept { return m_padding; }
    /** @brief Placement on the map. Initially TopLeft; Free uses freePosition(). */
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
    /** @brief Show or hide the box on the map. @param on New value. */
    void setShown(bool on);
    /** @brief Set the date/time format. @param format Qt date/time format. */
    void setFormat(const QString &format);
    /** @brief Set the text font. @param f New font. */
    void setFont(const QFont &f);
    /** @brief Set the text color. @param c New color. */
    void setTextColor(const QColor &c);
    /** @brief Set the padding. @param v Logical pixels. */
    void setPadding(int v);
    /** @brief Set the placement. @param a New anchor. */
    void setAnchor(OpenSWMM::Render::LegendOverlayStyle::Anchor a);
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
    void setBackgroundMode(OpenSWMM::Render::LegendOverlayStyle::BackgroundMode m);
    /** @brief Set the solid or gradient start color. @param c New color. */
    void setBackgroundColor(const QColor &c);
    /** @brief Set the gradient end color. @param c New color. */
    void setGradientEndColor(const QColor &c);
    /** @brief Set the gradient direction. @param o New orientation. */
    void setGradientOrientation(Qt::Orientation o);

signals:
    /** @brief Any effective change; emitted after the per-property signal. */
    void changed();
    /** @brief The box was shown or hidden. @param on New value. */
    void shownChanged(bool on);
    /** @param value New format. */
    void formatChanged(const QString &value);
    /** @param value New font. */
    void fontChanged(const QFont &value);
    /** @param value New text color. */
    void textColorChanged(const QColor &value);
    /** @param value New padding. */
    void paddingChanged(int value);
    /** @param value New anchor. */
    void anchorChanged(OpenSWMM::Render::LegendOverlayStyle::Anchor value);
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
    void backgroundModeChanged(OpenSWMM::Render::LegendOverlayStyle::BackgroundMode value);
    /** @param value New background color. */
    void backgroundColorChanged(const QColor &value);
    /** @param value New gradient end color. */
    void gradientEndColorChanged(const QColor &value);
    /** @param value New gradient direction. */
    void gradientOrientationChanged(Qt::Orientation value);

private:
    bool m_shown = false;
    QString m_format = QString::fromLatin1(kDefaultFormat);
    QFont m_font;
    QColor m_textColor = QColor(20, 20, 20);
    int m_padding = 8;
    Anchor m_anchor = Anchor::TopLeft;
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

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_TIMEOVERLAYSTYLE_H
