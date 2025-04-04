 /*!
 * \file   main.cpp
 * \author Caleb Buahin <buahin.caleb@epa.gov>
 * \version see
 * \description
 * \license
 * \copyright
 * \date 2024
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

#include "swmmvisapplication.h"
#include "swmmvis.h"

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
