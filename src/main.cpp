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
