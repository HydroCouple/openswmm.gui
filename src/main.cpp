 /*!
 * \file   main.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version see
 * \description
 * \license
 * \copyright
 * \date 2026
 * \pre
 * \bug
 * \warning
 * \todo
 */
#include <QLocale>
#include <QTranslator>
#include <QScopedPointer>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QtQml/qqml.h>

#include "swmmvisapplication.h"
#include "swmmvis.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"

/*!
 * \brief main
 * \param argc The number of arguments passed to the application.
 * \param argv The arguments passed to the application.
 * \return The exit code of the application.
 */
int main(int argc, char *argv[])
    {  
    QCoreApplication *applicationInstance = nullptr;

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "SWMMVis"));
    QCommandLineOption helpOption = parser.addHelpOption();
    QCommandLineOption versionOption = parser.addVersionOption();

    parser.addOption(QCommandLineOption(QStringList() << "c" << "cli", QCoreApplication::translate("main", "Run in command line mode.")));

    QStringList opts;
    for (int i = 0; i < argc; ++i)
        opts << QString(argv[i]);

    parser.parse(opts);
    if (parser.isSet("cli"))
        applicationInstance = new SWMMVisCoreApplication(argc, argv);
    else
        applicationInstance = new SWMMVisApplication(argc, argv);

    // Register the QML types used by MapCanvas's QQuickWidget overlay
    // (Phase B.RHI of docs/RENDERING_5M_PLAN.md).
    qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0,
                                           "SWMMLayerQSGRenderer");
    qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0,
                                            "SWMM2DMeshQSGRenderer");
       
    QScopedPointer<QCoreApplication> application(applicationInstance);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "SWMMVis_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            application->installTranslator(&translator);
            break;
        }
    }
  
    return application->exec();
}
