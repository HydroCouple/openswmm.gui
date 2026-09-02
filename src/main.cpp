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
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QMutex>
#include <QStandardPaths>
#include <QTranslator>
#include <QScopedPointer>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QSurfaceFormat>
#include <QtQml/qqml.h>

#include <cstdio>

#include "swmmvisapplication.h"
#include "swmmvis.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"

// ── File-tee message handler ─────────────────────────────────────────────
// BULK_DELETE_AND_WINDOWS_OPEN_PERF_PLAN Phase 0.  On Windows the app is a
// WIN32_EXECUTABLE with no console and no message handler, so every
// qDebug/qCInfo — including the openswmm.load.* profiling categories — goes
// only to OutputDebugString and is unreachable without DebugView.  This
// opt-in tee writes every message to a file the user can actually read.
//
// Opt in with SWMM_LOG_FILE=<path>, or SWMM_LOG_FILE=1 for the default
// location under AppDataLocation/logs.  The chosen path is printed to
// stderr and written as the file's first line, so the location is always
// discoverable.  The previous handler is chained so platform behaviour
// (and any handler a test installs LATER, which replaces this one) is
// unchanged.  Messages arrive from worker threads too, hence the mutex.

namespace {

QtMessageHandler g_prevMessageHandler = nullptr;
FILE            *g_logFileHandle      = nullptr;
QBasicMutex      g_logFileMutex;

void fileTeeMessageHandler(QtMsgType type, const QMessageLogContext &ctx,
                           const QString &msg)
{
    {
        QMutexLocker lock(&g_logFileMutex);
        if (g_logFileHandle)
        {
            const QByteArray line =
                QDateTime::currentDateTime()
                    .toString(QStringLiteral("hh:mm:ss.zzz "))
                    .toUtf8()
                + (ctx.category ? QByteArray(ctx.category) + ": "
                                : QByteArray())
                + msg.toUtf8() + '\n';
            std::fwrite(line.constData(), 1, size_t(line.size()),
                        g_logFileHandle);
            std::fflush(g_logFileHandle);
        }
    }
    if (g_prevMessageHandler) g_prevMessageHandler(type, ctx, msg);
}

void installFileTeeIfRequested()
{
    const QByteArray req = qgetenv("SWMM_LOG_FILE");
    if (req.isEmpty()) return;

    QString path = QString::fromLocal8Bit(req);
    if (path == QLatin1String("1") || path.compare(QLatin1String("auto"),
                                                   Qt::CaseInsensitive) == 0)
    {
        const QString dir = QStandardPaths::writableLocation(
                                QStandardPaths::AppDataLocation)
                            + QStringLiteral("/logs");
        QDir().mkpath(dir);
        path = dir + QStringLiteral("/swmmvis-")
             + QDateTime::currentDateTime().toString(
                   QStringLiteral("yyyyMMdd-hhmmss"))
             + QStringLiteral(".log");
    }

    g_logFileHandle = std::fopen(QFile::encodeName(path).constData(), "a");
    if (!g_logFileHandle)
    {
        std::fprintf(stderr, "SWMM_LOG_FILE: cannot open '%s' for append\n",
                     qPrintable(path));
        return;
    }
    std::fprintf(g_logFileHandle, "== SWMMVis log %s ==\n",
                 qPrintable(QDateTime::currentDateTime().toString(Qt::ISODate)));
    std::fflush(g_logFileHandle);
    std::fprintf(stderr, "SWMM_LOG_FILE: logging to %s\n", qPrintable(path));
    g_prevMessageHandler = qInstallMessageHandler(fileTeeMessageHandler);
}

} // namespace

/*!
 * \brief main
 * \param argc The number of arguments passed to the application.
 * \param argv The arguments passed to the application.
 * \return The exit code of the application.
 */
int main(int argc, char *argv[])
    {
    // Perf-plan Phase 0 — opt-in file logging (SWMM_LOG_FILE).  Installed
    // before anything can log so early open phases are captured.
    installFileTeeIfRequested();

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
    //
    // macOS ONLY (perf plan Phase B1, 2026-09-01): applied unconditionally
    // this workaround forced Windows off Qt's D3D11 default onto desktop
    // GL — and onto opengl32sw.dll software rasterization on weak-GL boxes
    // (RDP, VMs, hybrid-GPU laptops), the prime "open is extremely laggy
    // on Windows" suspect. Shaders ship HLSL 5.0 (qsb defaults) and no
    // GL-only scenegraph code exists, so other platforms take Qt's native
    // backend. Respect a pre-set env var so any platform can override for
    // diagnostics (QSG_INFO=1 prints the active backend).
#ifdef Q_OS_MACOS
    if (!qEnvironmentVariableIsSet("QSG_RHI_BACKEND"))
        qputenv("QSG_RHI_BACKEND", "opengl");
    if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP"))
        qputenv("QSG_RENDER_LOOP", "threaded");
#endif
    // QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL); // or Qt::AA_UseOpenGLES
    // Alternatively, for Qt 6, force Metal:
    // qputenv("QSG_RHI_BACKEND", "metal");


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
