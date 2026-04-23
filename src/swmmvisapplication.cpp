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
// #ifdef Q_OS_WIN
// #include <windows.h> // for Sleep
// #endif

#include "version.h"
#include "swmmvisapplication.h"
#include "swmmvis.h"
#include "swmmvissplashscreen.h"


/*! \class SWMMVisCoreApplication
 * \brief The SWMMVisCoreApplication class is a subclass of QCoreApplication.
 *
 * The SWMMVisCoreApplication class is a subclass of QCoreApplication.
 * It is used to handle the core functionality of the SWMM application.
 */
SWMMVisCoreApplication::SWMMVisCoreApplication(int& argc, char* argv[])
    : QCoreApplication(argc, argv)
{
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
    setOrganizationName("hydrocouple");
    setOrganizationDomain("calebbuahin.github.io");

    QString version = QString("%1").arg(SWMM_VERSION);
    setApplicationVersion(version);
    setApplicationName("OpenSWMM Stormwater Management Model");
    setApplicationDisplayName("SWMM");


    this->setStyle(QStyleFactory::create("Fusion"));

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

//     int ms = 50000;

// #ifdef Q_OS_WIN
//     Sleep(uint(ms));
// #else

//     struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
//     nanosleep(&ts, NULL);
// #endif

    mSWMMVisSplashScreen->finish(mSWMMVisGUI);
    mSWMMVisGUI->show();
}

/*!
 * \brief SWMMVisApplication::~SWMMVisApplication
 * The destructor for the SWMMVisApplication class.
 */
SWMMVisApplication::~SWMMVisApplication()
{

}
