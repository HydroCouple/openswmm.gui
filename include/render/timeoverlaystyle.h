/**
 * @file      timeoverlaystyle.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Model for the map's animation date/time box: its text format, font and color,
 *            whether it is shown, and the box settings it shares with the legend.
 *
 *         Views (the box on the map, its Properties dialog, the animation toolbar's checkbox)
 *         all read and write this object and follow its changed() signal; none keeps a copy.
 *         Saved per project window in the .oswp session.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_TIMEOVERLAYSTYLE_H
#define OPENSWMM_RENDER_TIMEOVERLAYSTYLE_H

#include "render/mapboxstyle.h"

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace OpenSWMM::Render
{

/*!
 * \class TimeOverlayStyle
 * \brief What the map's date/time box shows and how it looks.
 *
 *        Box properties (anchor, padding, frame, background, opacity) come from MapBoxStyle,
 *        under the legend's names, defaults and saved keys. Each effective change emits its own
 *        signal and then changed().
 */
class TimeOverlayStyle : public MapBoxStyle
{
    Q_OBJECT

public:
    /// Default date/time format (Qt QDateTime::toString format), e.g. 03/14/2026 06:30:00.
    static constexpr const char *kDefaultFormat = "MM/dd/yyyy hh:mm:ss";

    Q_PROPERTY(QString format READ format WRITE setFormat NOTIFY formatChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY textColorChanged)

    /** @brief Construct the default style: hidden, top-left, legend-like box.
     * @param parent Optional QObject owner. */
    explicit TimeOverlayStyle(QObject *parent = nullptr);
    /** @brief Destroy the style. */
    ~TimeOverlayStyle() override = default;

    /** @brief Group-prefixed row labels for the Properties tree (QPropertyModel hook).
     * @param propertyName Property name.
     * @return Label such as "General — Date/time format", or the box label from MapBoxStyle. */
    QString displayLabelFor(const QString &propertyName) const override;

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

public slots:
    /** @brief Show or hide the box on the map. @param on New value. */
    void setShown(bool on);
    /** @brief Set the date/time format. @param format Qt date/time format. */
    void setFormat(const QString &format);
    /** @brief Set the text font. @param f New font. */
    void setFont(const QFont &f);
    /** @brief Set the text color. @param c New color. */
    void setTextColor(const QColor &c);

signals:
    /** @brief The box was shown or hidden. @param on New value. */
    void shownChanged(bool on);
    /** @param value New format. */
    void formatChanged(const QString &value);
    /** @param value New font. */
    void fontChanged(const QFont &value);
    /** @param value New text color. */
    void textColorChanged(const QColor &value);

private:
    bool m_shown = false;
    QString m_format = QString::fromLatin1(kDefaultFormat);
    QFont m_font;
    QColor m_textColor = QColor(20, 20, 20);
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_TIMEOVERLAYSTYLE_H
