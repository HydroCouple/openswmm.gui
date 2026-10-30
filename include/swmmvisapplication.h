/**
 * @file      swmmvisapplication.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Qt application variants configuring bundled GIS paths, application identity
 *         and (for GUI mode) windows, theme and dialog event filters at startup.
 *
 * @details Two application objects are provided:
 *  - SWMMVisCoreApplication: non-GUI variant (extends QCoreApplication),
 *    used for headless batch runs.
 *  - SWMMVisApplication: full GUI application (extends QApplication),
 *    creates the SWMMVis main window and the startup splash screen.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMAPPLICATION_H
#define SWMMAPPLICATION_H

#include <QApplication>

class SWMMVis;
class SWMMVisSplashScreen;

/*!
 * \class SWMMVisCoreApplication
 * \brief Headless (non-GUI) application object for batch/scripted use.
 *
 * \details Configures GIS data search paths and Qt application identity without opening a display.
 *          Used in the future for command-line simulation tools that share
 *          the same engine library as the GUI but require no graphics context.
 */
class SWMMVisCoreApplication : public QCoreApplication
{
    Q_OBJECT

public:
    /*!
     * \brief Construct the core event loop, configure bundled GIS data paths and set
     *   application/settings identity.
     * \param argc  Command-line argument count (from main).
     * \param argv  Command-line argument vector (from main).
     * @note Qt may modify argc and argv contents; provide the main argument storage and keep it
     *   valid for application lifetime. Construct only one Qt application on the main thread. No
     *   ownership transfer of caller argument storage.
     * @note Does not register GDAL drivers, run a simulation, install GUI filters or parse
     *   commands. Call exec externally; core mode alone has no automatic work/quit path here.
     */
    SWMMVisCoreApplication(int &argc, char *argv[]);

    /*!
     * \brief Destructor with no explicit GDAL cleanup; base QCoreApplication performs its normal
     *   teardown.
     */
    virtual ~SWMMVisCoreApplication();
};

/*!
 * \class SWMMVisApplication
 * \brief Full GUI application object — creates the main window and splash screen.
 *
 * \details On construction, SWMMVisApplication:
 *  1. Calls QApplication's constructor and configures bundled GIS data paths/settings identity.
 *  2. Seeds examples and constructs the main window before showing the splash.
 *  3. Applies Fusion/theme, installs dialog filters and displays/finishes the splash.
 *  4. Shows the main window and schedules the startup licence prompt.
 *
 * The application object remains alive for the full run of exec().
 */
class SWMMVisApplication : public QApplication
{
    Q_OBJECT

public:
    /*!
     * \brief Constructs the application, creates the main window, and shows
     *        the startup splash screen.
     * \param argc  Command-line argument count (from main).
     * \param argv  Command-line argument vector (from main).
     * @note Qt may modify argc and argv contents; provide the main argument storage and keep it
     *   valid for application lifetime. Construct only one Qt application on the main thread. No
     *   ownership transfer of caller argument storage.
     * @note Configure GIS paths and hydrocouple/application identity before constructing main
     *   window. Capture-manifest mode redirects INI settings beside manifest/default outDir and
     *   CLEARS that scope, forces light theme and skips startup licence; directory creation/read
     *   failures are not reported here. Seeds bundled examples in user data directory.
     * @note Main/splash are allocated as parentless top-level widgets and held raw; destructor body
     *   does not explicitly delete them. Main window exists before splash is created. Deferred
     *   declined licence calls quit after event loop starts. Constructor can show windows/process
     *   UI; do not call from worker or before required QML registration.
     */
    SWMMVisApplication(int &argc, char *argv[]);

    /*!
     * \brief Destructor with empty body; no explicit main-window/splash deletion or GDAL teardown
     *   here.
     */
    virtual ~SWMMVisApplication();

protected:
    /*!
     * \brief Application-wide event filter that keeps every QDialog stacked
     *        above the main window. When a top-level QDialog is shown it is
     *        raised and activated after Show processing so it
     *        cannot be hidden by clicks on the main window.
     * @param[in] watched Borrowed target, inspected as top-level QDialog; non-dialog/embedded
     *   targets pass through.
     * @param[in] event Required borrowed Qt event, inspected synchronously; not retained.
     * @return QApplication base filter result; this filter never intentionally consumes event.
     * @note Show queues receiver-guarded raise/activate then registry stack raise. On macOS,
     *   native-child mode attaches only nonmodal dialogs; Close/Hide detaches top-level dialogs. No
     *   QObject reparenting or always-on-top flag is applied here despite historical comments.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

    /*!
     * \brief Last-resort guard around event delivery: an exception thrown by
     *        a slot or event handler is logged (qCritical → Message Log tee)
     *        and reported once through a throttled message box instead of
     *        propagating out of exec() and terminating the application.
     * @param[in] receiver Borrowed event receiver passed to QApplication and used for diagnostic
     *   class name (null prints question mark).
     * @param[in] event Borrowed event passed unchanged to Qt; valid according to QApplication
     *   notify contract.
     * @return Base delivery result on success; false after catching std::exception or any other
     *   exception. Does not retry/rollback partially executed handler.
     * @note Logs every caught exception and schedules throttled critical dialog. Runs on Qt
     *   delivery thread; throttle/report UI assume main-thread event delivery.
     */
    bool notify(QObject *receiver, QEvent *event) override;

private:
    /**
     * @param[in] receiver Optional borrowed receiver used only for class name, not retained by
     *   deferred lambda.
     * @param[in] what Diagnostic string copied into deferred notification; not interpreted as a
     *   command.
     * @brief Emit qCritical for every handler exception, then queue at most one modal warning per
     *   10-second burst using static elapsed timer.
     * @note Timer starts when warning is scheduled, not dismissed; notification owned by
     *   application timer context, canceled if application destroyed. No source recovery or
     *   application exit.
     */
    void reportHandlerException(QObject *receiver, const QString &what);

    SWMMVis *mSWMMVisGUI;                      ///< Owned main window.
    SWMMVisSplashScreen *mSWMMVisSplashScreen; ///< Owned splash screen.
};

#endif // SWMMAPPLICATION_H
