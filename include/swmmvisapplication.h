/*!
 * \file   swmmapplication.h
 * \author Caleb Buahin <buahin.caleb@epa.gov>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2024
 * \pre
 * \bug
 * \warning
 * \todo
 */
#ifndef SWMMAPPLICATION_H
#define SWMMAPPLICATION_H

#include <QApplication>

class SWMMVis;
class SWMMVisSplashScreen;

/*! \class SWMMVisCoreApplication
 * \brief The SWMMVisCoreApplication class is a subclass of QCoreApplication.
 *
 * The SWMMVisCoreApplication class is a subclass of QCoreApplication.
 * It is used to handle the core functionality of the SWMM application.
 */
class SWMMVisCoreApplication : public QCoreApplication
{
	Q_OBJECT

    public:
        SWMMVisCoreApplication(int &argc, char *argv[]);

		virtual ~SWMMVisCoreApplication();
};

/*! \class SWMMVisApplication
 * \brief The SWMMVisApplication class is a subclass of QApplication.
 *
 * The SWMMVisApplication class is a subclass of QApplication.
 * It is used to handle the core functionality of the SWMM application.
 */
class SWMMVisApplication: public QApplication
{
    Q_OBJECT

public:
    /*!
     * \brief SWMMVisApplication
     * \param argc
     * \param argv
     */
    SWMMVisApplication(int &argc, char *argv[]);


    virtual ~SWMMVisApplication();

private:
    SWMMVis *mSWMMVisGUI;
    SWMMVisSplashScreen *mSWMMVisSplashScreen;
};

#endif // SWMMAPPLICATION_H
