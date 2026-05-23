/*!
 * \file   swmmvisapplication.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QApplication subclass for the OpenSWMM GUI — sets up GDAL, static
 *         Qt resources, and the main window during application startup.
 *
 * \details Two application objects are provided:
 *  - SWMMVisCoreApplication: non-GUI variant (extends QCoreApplication),
 *    used for headless batch runs.
 *  - SWMMVisApplication: full GUI application (extends QApplication),
 *    creates the SWMMVis main window and the startup splash screen.
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
 * \details Initialises GDAL and Qt infrastructure without opening a display.
 *          Used in the future for command-line simulation tools that share
 *          the same engine library as the GUI but require no graphics context.
 */
class SWMMVisCoreApplication : public QCoreApplication
{
    Q_OBJECT

public:
    /*!
     * \brief Constructs the core application and initialises GDAL.
     * \param argc  Command-line argument count (from main).
     * \param argv  Command-line argument vector (from main).
     */
    SWMMVisCoreApplication(int &argc, char *argv[]);

    /*!
     * \brief Destructor. Releases GDAL resources.
     */
    virtual ~SWMMVisCoreApplication();
};

/*!
 * \class SWMMVisApplication
 * \brief Full GUI application object — creates the main window and splash screen.
 *
 * \details On construction, SWMMVisApplication:
 *  1. Calls QApplication's constructor (sets up the event loop and UI platform).
 *  2. Initialises GDAL/PROJ and loads Qt static resources.
 *  3. Shows SWMMVisSplashScreen while the main SWMMVis window initialises.
 *  4. Creates and shows the SWMMVis main window.
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
     */
    SWMMVisApplication(int &argc, char *argv[]);

    /*!
     * \brief Destructor. Closes the main window if still open and cleans up
     *        GDAL resources.
     */
    virtual ~SWMMVisApplication();

private:
    SWMMVis             *mSWMMVisGUI;           ///< Owned main window.
    SWMMVisSplashScreen *mSWMMVisSplashScreen;  ///< Owned splash screen.
};

#endif // SWMMAPPLICATION_H
