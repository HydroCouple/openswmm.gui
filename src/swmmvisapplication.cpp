/*!
 * \file   swmmapplication.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 * \pre
 * \bug
 * \warning
 * \todo
 */

#include <QStyleFactory>
#include <QPalette>
#include <QScreen>
#include <QTimer>
#include <QDialog>
#include <QEvent>
// #ifdef Q_OS_WIN
// #include <windows.h> // for Sleep
// #endif

#include "version.h"
#include "swmmvisapplication.h"
#include "swmmvis.h"
#include "swmmvissplashscreen.h"
#include "core/gisdatapaths.h"
#include "ui/dialogs/licenseagreementdialog.h"
#include "platform/macoswindowutils.h"


/*! \class SWMMVisCoreApplication
 * \brief The SWMMVisCoreApplication class is a subclass of QCoreApplication.
 *
 * The SWMMVisCoreApplication class is a subclass of QCoreApplication.
 * It is used to handle the core functionality of the SWMM application.
 */
SWMMVisCoreApplication::SWMMVisCoreApplication(int& argc, char* argv[])
    : QCoreApplication(argc, argv)
{
    setupBundledGisDataPaths();

    setOrganizationName("hydrocouple");
    setOrganizationDomain("calebbuahin.github.io");

    QString version = QString("%1").arg(SWMM_VERSION);
    setApplicationVersion(version);
    setApplicationName("OpenSWMM Stormwater Management Model");
}

/*!
 * \brief SWMMVisCoreApplication::~SWMMVisCoreApplication
 * The destructor for the SWMMVisCoreApplication class.
 */
SWMMVisCoreApplication::~SWMMVisCoreApplication()
{

}

/*!
 * \brief SWMMVisApplication::SWMMVisApplication
 * \param argc The number of arguments passed to the application.
 * \param argv The arguments passed to the application.
 */
SWMMVisApplication::SWMMVisApplication(int &argc, char *argv[])
    : QApplication(argc, argv),
    mSWMMVisGUI(new SWMMVis()),
    mSWMMVisSplashScreen(nullptr)
{
    setupBundledGisDataPaths();

    setOrganizationName("hydrocouple");
    setOrganizationDomain("calebbuahin.github.io");

    QString version = QString("%1").arg(SWMM_VERSION);
    setApplicationVersion(version);
    setApplicationName("OpenSWMM Stormwater Management Model");
    setApplicationDisplayName("SWMM");


    this->setStyle(QStyleFactory::create("Fusion"));

    // Keep every QDialog stacked above the main window. The filter reparents
    // orphaned dialogs to the main window and raises them on show so a click
    // on the main window cannot bury them.
    installEventFilter(this);

    //set up splash screen
    QPixmap pixmap(":/swmmvis/splashscreen");

    if (QScreen* screen = QGuiApplication::primaryScreen())
    {
        pixmap.setDevicePixelRatio(screen->devicePixelRatio());
    }

    int w = 600 * pixmap.devicePixelRatioF();
    int h = 400 * pixmap.devicePixelRatioF();

    mSWMMVisSplashScreen = new SWMMVisSplashScreen(pixmap.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // Force splash screen to start on primary screen
    if (QScreen* screen = QGuiApplication::primaryScreen())
    {
        const QPoint currentDesktopsCenter = screen->availableGeometry().center();
        mSWMMVisSplashScreen->move(currentDesktopsCenter - mSWMMVisSplashScreen->rect().center());
    }

    mSWMMVisSplashScreen->show();


    //!Read settings
    mSWMMVisSplashScreen->onShowMessage("Reading Application Settings");
    printf("Reading Application Settings\n");

    //!Load components
    mSWMMVisSplashScreen->onShowMessage("Loading Component Libraries");
    printf("Loading Component Libraries\n");

//     int ms = 5000;

// #ifdef Q_OS_WIN
//     Sleep(uint(ms));
// #else

//     struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
//     nanosleep(&ts, NULL);
// #endif

    mSWMMVisSplashScreen->finish(mSWMMVisGUI);
    mSWMMVisGUI->show();

    // Defer license check until after the event loop starts so macOS has
    // activated the app and the dialog reliably appears in the foreground.
    QTimer::singleShot(0, this, [this]() {
        if (LicenseAgreementDialog::shouldShowOnStartup())
        {
            LicenseAgreementDialog dlg(mSWMMVisGUI);
            if (dlg.exec() != QDialog::Accepted)
                quit();
        }
    });
}

/*!
 * \brief SWMMVisApplication::~SWMMVisApplication
 * The destructor for the SWMMVisApplication class.
 */
SWMMVisApplication::~SWMMVisApplication()
{

}

bool SWMMVisApplication::eventFilter(QObject *watched, QEvent *event)
{
#ifdef Q_OS_MACOS
    // Balance attachAsChildWindow: AppKit requires removeChildWindow before a
    // child window is ordered out or closed, and Qt's cocoa backend doesn't
    // know about the attachment. Close fires before the platform hide (clean
    // detach); Hide is the fallback for dialogs hidden without a Close event
    // (QDialog::done() paths) — detachFromParentWindow re-orders-out in that
    // case so the window can't linger glued above the main window.
    if (event->type() == QEvent::Close || event->type() == QEvent::Hide)
    {
        if (auto *dlg = qobject_cast<QDialog *>(watched))
        {
            if (dlg->isWindow())
                openswmmvis::platform::detachFromParentWindow(dlg);
        }
    }
#endif
    if (event->type() == QEvent::Show)
    {
        if (auto *dlg = qobject_cast<QDialog *>(watched))
        {
            // Only act on top-level dialogs — embedded QDialog widgets in a
            // layout are not what we're trying to keep on top.
            if (dlg->isWindow())
            {
                // Defer until after the Show is processed so we operate on the
                // realised platform window. raise()+activateWindow() brings the
                // dialog above the main window without the always-on-top hint
                // (which would also stay above other apps).
                QTimer::singleShot(0, dlg, [dlg]() {
                    dlg->raise();
                    dlg->activateWindow();
                    // NOTE: this used to also attach non-modal dialogs as
                    // native NSWindow child windows of the main window
                    // (attachAsChildWindow) to keep them stacked above it.
                    // Qt's cocoa backend knows nothing about AppKit child-
                    // window links, and the attachment wedged key-window
                    // routing: after the first non-modal dialog opened, the
                    // whole app stopped receiving mouse events while the
                    // event loop sat idle. Plain raise-on-show only; dialogs
                    // now stack like ordinary macOS windows.
                });
            }
        }
    }
    return QApplication::eventFilter(watched, event);
}
