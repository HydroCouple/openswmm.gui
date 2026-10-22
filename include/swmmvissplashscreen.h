/**
 * @file      swmmvissplashscreen.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Application startup splash screen with configurable message colour
 *         and text alignment.
 *
 * @details SWMMVisSplashScreen extends QSplashScreen to allow custom text
 *          colour and alignment, matching the SWMMVis branding.  The
 *          `onShowMessage` slot stores text then invokes the base showMessage with an empty string
 *          to repaint multi-line startup progress messages in
 *          the correct colour and position.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SPLASHSCREEN_H
#define SPLASHSCREEN_H

#include <QColor>
#include <QSplashScreen>

/*!
 * \class SWMMVisSplashScreen
 * \brief Startup splash screen with configurable message text colour and
 *        alignment.
 *
 * \details Overrides QSplashScreen::drawContents() to paint the status
 *          message using the colour and alignment set via setColor() /
 *          setAlignment().  The default colour is white and the default
 *          alignment is Qt::AlignBottom | Qt::AlignHCenter, matching the
 *          standard QSplashScreen layout.
 */
class SWMMVisSplashScreen : public QSplashScreen
{
    Q_OBJECT

public:
    /*!
     * \brief Constructs a splash screen from a pixmap.
     * \param pixmap  Splash image copied by QSplashScreen; widget size follows pixmap, not desktop
     *   full screen.
     * \param f       Qt window flags.
     * @note Parentless top-level widget, caller manages lifetime. White bottom/center text with
     *   system general font 20 pt DemiBold; does not show itself or start event loop. GUI thread
     *   only.
     */
    SWMMVisSplashScreen(const QPixmap &pixmap = QPixmap(), Qt::WindowFlags f = Qt::WindowFlags());

    /*!
     * \brief Constructs another top-level splash; the parent parameter is currently ignored.
     * \param parent  Ignored compatibility argument; constructor does not pass it to QSplashScreen
     *   or adopt ownership.
     * \param pixmap  Splash image.
     * \param f       Qt window flags.
     * @note Copies pixmap/flags to base and sets white bottom/center alignment. Unlike pixmap-only
     *   overload, does not override default font. Caller manages parentless lifetime even when
     *   parent supplied.
     */
    SWMMVisSplashScreen(QWidget *parent, const QPixmap &pixmap = QPixmap(),
                        Qt::WindowFlags f = Qt::WindowFlags());

    /*!
     * \brief Destructor.
     * @note Empty destructor body; Qt base releases splash resources, no application/window
     *   teardown.
     */
    virtual ~SWMMVisSplashScreen();

    /*!
     * \brief Sets the colour used to draw the status message text.
     * \param color  Message text colour (default: white).
     * @note Copies color only, no validation or immediate repaint request; next custom draw uses
     *   it.
     */
    void setColor(const QColor &color);

    /*!
     * \brief Sets the alignment flags used when drawing the status message.
     * \param alignment  Qt::Alignment flags, e.g. Qt::AlignBottom | Qt::AlignLeft.
     * @note Copies flags only, no validation/repaint request. Draw adds word wrap and forced
     *   justification.
     */
    void setAlignment(Qt::Alignment alignment);

    /*!
     * \brief Overridden to draw the status message in the configured colour
     *        and at the configured alignment.
     * \param painter  Active painter for the splash widget.
     * @note Painter must be nonnull/active; pen/font state is changed without save/restore. Draw
     *   stored message in widget rect inset 25 logical pixels on all sides, then invoke base
     *   drawContents. No text layout outside that rectangle.
     */
    void drawContents(QPainter *painter) override;

public slots:

    /*!
     * \brief Store \p message for custom painting and request repaint through an empty base
     *   showMessage().
     * \details Connected to the application's startup progress chain.
     * \param message  Human-readable startup status text.
     * @note Copies text (not translation/HTML), then calls base with EMPTY message plus stored
     *   alignment/color. Custom draw uses stored text; base messageChanged does not carry the
     *   supplied text. Qt may process events while updating splash, so use GUI thread and retain
     *   its lifetime.
     */
    void onShowMessage(const QString &message);

private:
    QColor mColor;            ///< Text colour for the status message.
    Qt::Alignment mAlignment; ///< Alignment flags for the status message.
    QString mMessage;         ///< Current message text displayed on the splash.
};

#endif // SPLASHSCREEN_H
