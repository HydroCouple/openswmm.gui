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
#include "project/examplesseeder.h"
#include "swmmvissplashscreen.h"
#include "core/gisdatapaths.h"
#include "core/preferencesmanager.h"
#include "ui/dialogs/licenseagreementdialog.h"
#include "ui/dialogs/dialoglayoutwatcher.h"
#include "ui/dialogs/dialogregistry.h"
#include "ui/theme/thememanager.h"
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
    mSWMMVisGUI(nullptr),
    mSWMMVisSplashScreen(nullptr)
{
    // MUST run before the main window is built. PROJ caches its data search
    // paths when a PJ_CONTEXT is first created, so GDAL_DATA/PROJ_DATA have to
    // be in the environment before ANY GDAL/PROJ call. The SWMMVis constructor
    // pumps events (see main.cpp) and can create a MapCanvas, which creates a
    // PROJ context — so constructing it in the member-initialiser list, as this
    // did previously, published the paths too late. On macOS/Linux a
    // system-installed PROJ masked the mistake; on Windows there is no system
    // proj.db and every CRS lookup came back empty.
    setupBundledGisDataPaths();

    // Identity MUST precede the main-window construction: SWMMVis's ctor
    // builds QSettings members and (via the Welcome screen) resolves
    // QStandardPaths::AppLocalDataLocation, both of which key off the
    // org/app names. Setting them after `new SWMMVis()` (as this did
    // previously) made those resolve against Qt defaults, splitting the
    // app's settings across two files.
    setOrganizationName("hydrocouple");
    setOrganizationDomain("calebbuahin.github.io");

    QString version = QString("%1").arg(SWMM_VERSION);
    setApplicationVersion(version);
    setApplicationName("OpenSWMM Stormwater Management Model");
    setApplicationDisplayName("SWMM");

    // Seed bundled examples into the per-user data dir before the Welcome
    // screen (built inside the SWMMVis ctor) scans for them.
    openswmmvis::project::examples::preferredExamplesDir(version);

    mSWMMVisGUI = new SWMMVis();


    this->setStyle(QStyleFactory::create("Fusion"));

    // UI redesign P2 — install the token-driven chrome theme on top of
    // Fusion. Mode comes from the persisted appearance preference;
    // "System" follows the OS light/dark appearance live.
    {
        auto *theme = openswmmvis::ui::ThemeManager::instance();
        theme->setMode(openswmmvis::ui::ThemeManager::modeFromString(
            PreferencesManager::instance()->appearanceMode()));
        theme->apply();
    }

    // Keep every QDialog stacked above the main window. The filter reparents
    // orphaned dialogs to the main window and raises them on show so a click
    // on the main window cannot bury them.
    installEventFilter(this);

    // Iteration 2 (D1) — automatic dialog layout persistence: restore on
    // first Show / save on Hide-Close for every NAMED top-level QDialog
    // (see dialoglayoutwatcher.h). A separate filter so the macOS
    // stacking logic above stays untangled from persistence.
    installEventFilter(new openswmmvis::ui::DialogLayoutWatcher(this));

    // Register of open modeless dialogs in most-recently-used order. Feeds
    // the Window menu (so a dialog dragged onto a since-disconnected monitor
    // is still reachable) and, when the Qt stacking mode is selected, the
    // raise-on-activate pass that replaces the AppKit child-window
    // attachment. Mode is resolved once here — see dialogregistry.h.
    {
        auto *registry = openswmmvis::ui::DialogRegistry::instance();
        registry->setStackingMode(
            openswmmvis::ui::DialogRegistry::configuredStackingMode());
        installEventFilter(registry);
    }

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
#ifdef Q_OS_MACOS
                    // Keep non-modal dialogs stacked above the main window in
                    // open order by attaching them as native NSWindow child
                    // windows (idempotent; balanced by the detach on Close/Hide
                    // above). The attachment was once removed (5d43e28) blaming
                    // an app-wide input freeze, but the actual freeze was the
                    // modal-exec-during-mousepress QNSView button latch, fixed
                    // separately by firing pickers on mouse RELEASE (ddca63d).
                    // If a freeze reappears, first look for a dialog shown from
                    // inside a mousePressEvent before suspecting this.
                    //
                    // Skipped entirely in QtRaiseOnActivate mode, where
                    // DialogRegistry re-raises dialogs on activation instead —
                    // no native gluing, so a dialog can never be dragged
                    // off-screen by the window it happens to be attached to.
                    using openswmmvis::ui::DialogRegistry;
                    const bool nativeStacking =
                        DialogRegistry::instance()->stackingMode()
                            == DialogRegistry::StackingMode::NativeChildWindow;
                    if (nativeStacking && dlg->windowModality() == Qt::NonModal)
                        openswmmvis::platform::attachAsChildWindow(dlg);
#endif
                });
            }
        }
    }
    return QApplication::eventFilter(watched, event);
}
