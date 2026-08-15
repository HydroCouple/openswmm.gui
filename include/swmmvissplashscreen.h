/*!
 * \file   swmmvissplashscreen.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Application startup splash screen with configurable message colour
 *         and text alignment.
 *
 * \details SWMMVisSplashScreen extends QSplashScreen to allow custom text
 *          colour and alignment, matching the OpenSWMM branding.  The
 *          `onShowMessage` slot is connected to the splash's showMessage
 *          signal so multi-line startup progress messages are displayed in
 *          the correct colour and position.
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
     * \param pixmap  Splash image (shown full-screen on the splash widget).
     * \param f       Qt window flags.
     */
    SWMMVisSplashScreen(const QPixmap &pixmap = QPixmap(),
                        Qt::WindowFlags f     = Qt::WindowFlags());

    /*!
     * \brief Constructs a splash screen with an explicit parent widget.
     * \param parent  Qt parent widget.
     * \param pixmap  Splash image.
     * \param f       Qt window flags.
     */
    SWMMVisSplashScreen(QWidget *parent,
                        const QPixmap &pixmap = QPixmap(),
                        Qt::WindowFlags f     = Qt::WindowFlags());

    /*!
     * \brief Destructor.
     */
    virtual ~SWMMVisSplashScreen();

    /*!
     * \brief Sets the colour used to draw the status message text.
     * \param color  Message text colour (default: white).
     */
    void setColor(const QColor &color);

    /*!
     * \brief Sets the alignment flags used when drawing the status message.
     * \param alignment  Qt::Alignment flags, e.g. Qt::AlignBottom | Qt::AlignLeft.
     */
    void setAlignment(Qt::Alignment alignment);

    /*!
     * \brief Overridden to draw the status message in the configured colour
     *        and at the configured alignment.
     * \param painter  Active painter for the splash widget.
     */
    void drawContents(QPainter *painter) override;

public slots:

    /*!
     * \brief Displays \p message on the splash screen using showMessage().
     * \details Connected to the application's startup progress chain.
     * \param message  Human-readable startup status text.
     */
    void onShowMessage(const QString &message);

private:

    QColor        mColor;      ///< Text colour for the status message.
    Qt::Alignment mAlignment;  ///< Alignment flags for the status message.
    QString       mMessage;    ///< Current message text displayed on the splash.
};

#endif // SPLASHSCREEN_H
