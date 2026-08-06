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
#include <QSurfaceFormat>
#include <QtQml/qqml.h>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QMutex>

#include "swmmvisapplication.h"
#include "swmmvis.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"

// Global log file for persistent logging
static QFile *g_logFile = nullptr;
static QMutex g_logMutex;

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QMutexLocker locker(&g_logMutex);
    
    QString level;
    switch (type) {
    case QtDebugMsg:    level = "DEBUG"; break;
    case QtInfoMsg:     level = "INFO"; break;
    case QtWarningMsg:  level = "WARN"; break;
    case QtCriticalMsg: level = "CRITICAL"; break;
    case QtFatalMsg:    level = "FATAL"; break;
    }
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString logLine = QString("[%1] %2: %3\n").arg(timestamp, level, msg);
    
    // Write to file
    if (g_logFile && g_logFile->isOpen()) {
        QTextStream stream(g_logFile);
        stream << logLine;
        stream.flush();
    }
    
    // Also write to stderr for terminal visibility
    fprintf(stderr, "%s", logLine.toLocal8Bit().constData());
    
    if (type == QtFatalMsg)
        abort();
}

void initializeLogging()
{
    // Create log directory: ~/Library/Application Support/SWMMVis/logs on macOS
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/SWMMVis/logs";
    QDir dir(logDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    // Create log file with timestamp
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    QString logPath = logDir + QString("/swmmvis_%1.log").arg(timestamp);
    
    g_logFile = new QFile(logPath);
    if (g_logFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        qInstallMessageHandler(messageHandler);
        qDebug() << "Log file initialized:" << logPath;
        qDebug() << "SWMMVis starting";
    } else {
        delete g_logFile;
        g_logFile = nullptr;
        fprintf(stderr, "Failed to open log file: %s\n", logPath.toLocal8Bit().constData());
    }
}

/*!
 * \brief main
 * \param argc The number of arguments passed to the application.
 * \param argv The arguments passed to the application.
 * \return The exit code of the application.
 */
int main(int argc, char *argv[])
    {
    // §QSG-3 — Force the Qt Scene Graph onto the OpenGL RHI backend on
    // macOS instead of the default Metal. Symptom that drove this:
    // geometry uploaded to certain QSGGeometryNodes (specifically the
    // last few children of a QSGTransformNode) didn't actually paint
    // pixels under Metal, even with correct vertex/material/parent
    // state (verified via diagnostic dump — vertexCount=24, parent==root,
    // mode=DrawTriangles, opaque colour, valid widget coords, but 0
    // matching pixels in the grabbed FBO). The OpenGL backend doesn't
    // exhibit this bug. Must be set BEFORE QGuiApplication is constructed
    // or the env var has no effect.
    qputenv("QSG_RHI_BACKEND", "opengl");
    qputenv("QSG_RENDER_LOOP", "threaded");
    // QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL); // or Qt::AA_UseOpenGLES
    // Alternatively, for Qt 6, force Metal:
    // qputenv("QSG_RHI_BACKEND", "metal");

    // Initialize file logging before anything else
    initializeLogging();

    QCoreApplication *applicationInstance = nullptr;

    // Register the QML types used by MapCanvas's QQuickWidget overlay BEFORE
    // the application object exists. SWMMVisApplication's constructor builds
    // the main window and pumps events for the splash screen, so anything
    // queued during construction (e.g. the SWMMVIS_OPEN_ON_STARTUP hook) can
    // create a MapCanvas — and its swmmlayer.qml `import OpenSWMM 1.0` fails
    // with "module is not installed" if these registrations haven't run yet,
    // silently dropping every GPU renderer for the whole session.
    // (Phase B.RHI of docs/RENDERING_5M_PLAN.md.)
    qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0,
                                           "SWMMLayerQSGRenderer");
    qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0,
                                            "SWMM2DMeshQSGRenderer");
    // VS.8 — GPU renderer for the 2D results layer (Gouraud depth fill,
    // contour bands, isolines, velocity arrows).
    qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0,
                                               "SWMM2DResultsQSGRenderer");

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

    // Get current format, modify it, and set it back
    // §QSG-5 — request 16x multisample anti-aliasing on the default
    // surface format so the QSG-rendered SWMM glyphs (junctions,
    // outfalls, conduits, subcatchment edges) have smooth edges
    // matching the CPU painter's antialiased output. Must be set
    // BEFORE QGuiApplication construction. Drivers downgrade silently
    // when 16x isn't supported (typically to 8x or 4x), so this is
    // safe on weaker GPUs — we just pay zero for the bump where the
    // hardware can't honour it. Combined with QQuickItem::setAntialiasing
    // on the renderers, this is the practical ceiling for AA via the
    // builtin QSGFlatColorMaterial / QSGVertexColorMaterial pipelines;
    // going further would need a custom AA fragment shader.
    {
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        // 6x MSAA: requesting 8x on macOS + OpenGL RHI was observed
        // to leave undefined-content artifacts in the QSG FBO
        // (likely a silent format-negotiation fallback). 6 is the
        // largest sample count that has been verified to render
        // cleanly on Apple Silicon. Driver downgrades to 4x where 6
        // isn't supported.
        fmt.setSamples(6);
        QSurfaceFormat::setDefaultFormat(fmt);
    }
    
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
