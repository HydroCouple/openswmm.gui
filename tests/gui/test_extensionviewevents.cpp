/**
 * @file test_extensionviewevents.cpp
 * @brief Native model, map, selection and attached-results acceptance for SDK view services.
 * @details Real model windows and public host capabilities exercise the same
 * controllers as built-in editors. Every case retains isolated input/output and
 * settings beneath the configured verification directory; no temporary files or
 * network services are used. Rendering payloads are queried separately from events.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionmanager.h"
#include "plugins/extensionmodelaccess.h"
#include "plugins/extensionstateregistry.h"
#include "plugins/guiextensionhost.h"
#include "project/openswmmvisworkspace.h"
#include "sdk/IEventSourceV3.hpp"
#include "sdk/IViewControlsV1.hpp"
#include "sdk/IProject.hpp"
#include "swmmvisprojectwindow.h"
#include "core/unitsystem.h"
#include "layers/featurelayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmresultslayer.h"
#include "map/mapcanvas.h"
#include "map/graphicsitems.h"
#include "map/openswmmvisscene.h"
#include "map/mapundostack.h"
#include "map/spatialreferencesystem.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "selection/selectionmanager.h"
#include "render/labelpainter.h"
#include "ui/panels/swmmattributetablemodel.h"
#include "ui/panels/attributetablepanel.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsEllipseItem>
#include <QGraphicsItemGroup>
#include <QGraphicsSimpleTextItem>
#include <QJsonArray>
#include <QJsonDocument>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetricsF>
#include <QPdfWriter>
#include <QMainWindow>
#include <QMdiArea>
#include <QPointer>
#include <QQmlEngine>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTest>
#include <QTableView>
#include <cpl_vsi.h>
#include <ogrsf_frmts.h>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

using namespace OpenSWMM::Sdk;
using namespace openswmmvis;

namespace
{
/** @brief Keep a real host/window/workspace and destroy them in native ownership order. */
struct ViewFixture {
    std::unique_ptr<OpenSWMMVisWorkspace> workspace; ///< Owns model and imported source layers.
    QMainWindow main;                       ///< Native ancestor used by the private model bridge.
    QSettings settings;                     ///< Isolated host settings, outliving the host.
    std::unique_ptr<GuiExtensionHost> host; ///< Public SDK services under test.
    QPointer<SWMMVisProjectWindow> window;  ///< Guarded native model window.

    /** @brief Construct an unloaded registered native session and its plugin host.
     * @param[in] input Existing retained input model.
     * @param[in] settingsPath Isolated host INI location. */
    ViewFixture(const QString &input, const QString &settingsPath)
        : workspace(OpenSWMMVisWorkspace::newInstance({}, nullptr)),
          settings(settingsPath, QSettings::IniFormat)
    {
        auto *mdi = new QMdiArea(&main);
        main.setCentralWidget(mdi);
        host = std::make_unique<GuiExtensionHost>("org.openswmm.native-view",
                                                  ExtensionManager::buildManifest(), &main, mdi,
                                                  ExtensionUiBridge{}, &settings);
        installExtensionModelAccess(
            host.get(),
            [this] {
                return window ? QList<SWMMVisProjectWindow *>{window.data()}
                              : QList<SWMMVisProjectWindow *>{};
            },
            [this] { return window.data(); });
        window = new SWMMVisProjectWindow(workspace.get(), input, mdi);
        mdi->addSubWindow(window);
    }
    /** @brief Retire the model while its host and workspace remain alive. */
    ~ViewFixture()
    {
        delete window.data();
        host.reset();
    }
    /** @brief Load the retained model through the real synchronous native barrier.
     * @param[out] error Native hard-load diagnostics.
     * @return True after a Ready native session. */
    bool load(QString &error)
    {
        QList<QString> warnings, errors;
        const bool ok = window->loadModel(warnings, errors);
        error = errors.join(';');
        return ok;
    }
    /** @brief Borrow the discoverable V3 service. @return Host-owned capability. */
    IEventSourceV3 *source() const { return qobject_cast<IEventSourceV3 *>(host->lifetime()); }
    /** @brief Borrow validated view controllers. @return Host-owned capability. */
    IViewControlsV1 *controls() const { return qobject_cast<IViewControlsV1 *>(host->lifetime()); }
    /** @brief Copy the current tracked state. @return Owning session metadata. */
    ViewSnapshot snapshot() const { return source()->viewSnapshot(window->extensionSessionId()); }
    /** @brief Resolve the ready model facade. @return Borrowed host model, or null. */
    IModelInstance *instance() const
    {
        return qobject_cast<IEventSourceV2 *>(host->lifetime())
            ->resolveSession(window->extensionSessionId());
    }
};

/** @brief Tiny computed provider whose values change without editing authored model data. */
class ViewAttributes final : public QObject, public IComputedAttributeProvider
{
public:
    double factor = 2.; ///< External calculation input invalidated deliberately by the test.
    /** @brief Describe one static read-only node field.
     * @param[in] category Requested model category.
     * @return One junction field or no descriptors for other categories. */
    QVector<OpenSWMM::Render::AttributeField>
    fields(OpenSWMMVis::SwmmCategory category) const override
    {
        return category == OpenSWMMVis::CatJunctions
                   ? QVector<OpenSWMM::Render::AttributeField>{{"scaled", "Computed invert",
                                                                QMetaType::Double, false, "m"}}
                   : QVector<OpenSWMM::Render::AttributeField>{};
    }
    /** @brief Evaluate one copied node value.
     * @param[in] attributes Copied native attributes; other arguments select an unused key.
     * @return Scaled invert elevation. */
    QVariant value(OpenSWMMVis::SwmmCategory, const QString &, const QString &,
                   const QVariantMap &attributes) const override
    {
        return attributes.value("invertElev").toDouble() * factor;
    }
};

/** @brief Occupy the global async-read pool deterministically, without timed sleeps. */
class ViewWorkerBarrier
{
public:
    /** @brief Reserve the only global-pool worker after prior work settles. */
    ViewWorkerBarrier() : mMaximum(QThreadPool::globalInstance()->maxThreadCount())
    {
        QThreadPool::globalInstance()->waitForDone();
        QThreadPool::globalInstance()->setMaxThreadCount(1);
        mWork = QtConcurrent::run([this] {
            mEntered.release();
            mResume.acquire();
        });
        mEntered.acquire();
    }
    /** @brief Release pending reads and restore the previous worker limit. */
    ~ViewWorkerBarrier()
    {
        release();
        QThreadPool::globalInstance()->waitForDone();
        QThreadPool::globalInstance()->setMaxThreadCount(mMaximum);
    }
    /** @brief Allow the queued read to run once. */
    void release()
    {
        if (!mReleased) {
            mReleased = true;
            mResume.release();
        }
    }

private:
    int mMaximum;                 ///< Previous pool width.
    bool mReleased = false;       ///< Blocker was released.
    QSemaphore mEntered, mResume; ///< Explicit worker/UI handshake.
    QFuture<void> mWork;          ///< Retained occupying worker.
};

/** @brief In-memory two-dimensional source testing metadata without HDF5/network dependencies. */
class ViewMeshSource final : public IMesh2DSource
{
public:
    /** @brief Store the selected period count.
     * @param[in] periods Zero for an attached source that has not published reports. */
    explicit ViewMeshSource(int periods) : mPeriods(periods) {}
    int vertexCount() const override { return 3; }
    int triangleCount() const override { return 1; }
    int timeCount() const override { return mPeriods; }
    bool readMeshGeometry(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                          std::vector<std::array<int, 3>> &triangles) override
    {
        x = {0., 1., 0.};
        y = {0., 0., 1.};
        z = {0., 0., 0.};
        triangles = {{0, 1, 2}};
        return true;
    }
    bool readDepthsAt(int index, std::vector<float> &depths) override
    {
        if (index < 0 || index >= mPeriods)
            return false;
        depths = {0.1f + float(index) * 0.1f};
        return true;
    }

private:
    int mPeriods; ///< Available report ordinals.
};
}

/** @brief Exercise public view observations and commands against native controllers. */
class TestExtensionViewEvents : public QObject
{
    Q_OBJECT
    QString mDirectory;                   ///< Unique retained directory for this case.
    QList<ViewEvent> mEvents;             ///< Public managed notifications received in this case.
    QMetaObject::Connection mObservation; ///< Explicit per-case receiver connection.

    /** @brief Form a retained path and prepare its parent directory.
     * @param[in] name Case-relative path.
     * @return Absolute reviewable filename. */
    QString path(const QString &name) const
    {
        const auto output = QDir(mDirectory).filePath(name);
        if (!QDir().mkpath(QFileInfo(output).absolutePath()))
            qFatal("Cannot prepare view-event evidence: %s", qPrintable(output));
        return output;
    }
    /** @brief Copy the SDK input into this case.
     * @return Retained writable input path. */
    QString model() const
    {
        const auto output = path("model.inp");
        if (!QFile::copy(QStringLiteral(SDK_NATIVE_VIEW_FIXTURE_PATH), output))
            qFatal("Cannot copy view-event input to %s", qPrintable(output));
        return output;
    }
    /** @brief Observe future publications after load/restoration has settled.
     * @param[in] fixture Live native host/window. */
    void observe(ViewFixture &fixture)
    {
        QCoreApplication::processEvents();
        mEvents.clear();
        mObservation = fixture.source()->observeViews(
            this, {{}, fixture.window->extensionSessionId(), {}},
            [this](const ViewEvent &event) { mEvents.append(event); });
        QVERIFY(mObservation);
    }
    /** @brief Count observations of one semantic reason and optional attachment.
     * @param[in] kind Requested reason.
     * @param[in] attachment Optional exact runtime layer identity.
     * @return Delivered count. */
    int count(ViewEventKind kind, const QUuid &attachment = {}) const
    {
        int result = 0;
        for (const auto &event : mEvents)
            result +=
                event.kind == kind && (attachment.isNull() || event.layerSessionId == attachment);
        return result;
    }
    /** @brief Resolve a native layer only through copied snapshot metadata.
     * @param[in] fixture Live native host/window.
     * @param[in] id Native persisted ID in this uniquely named fixture.
     * @return Matching copied attachment metadata, or an empty value. */
    static LayerViewState layerState(const ViewFixture &fixture, const QString &id)
    {
        for (const auto &entry : fixture.snapshot().layers)
            if (entry.layerId == id)
                return entry;
        return {};
    }
    /** @brief Resolve current result metadata for one attachment.
     * @param[in] fixture Live native host/window.
     * @param[in] attachment Current layer runtime identity.
     * @return Matching source metadata, or an empty value. */
    static ResultsViewState resultsState(const ViewFixture &fixture, const QUuid &attachment)
    {
        for (const auto &entry : fixture.snapshot().results)
            if (entry.layerSessionId == attachment)
                return entry;
        return {};
    }
    /** @brief Build a typed action against this fixture's current native session.
     * @param[in] fixture Ready host/window.
     * @param[in] kind Requested controller operation.
     * @param[in] attachment Current runtime layer identity.
     * @return Owning request with a fresh feedback token. */
    static ViewCommand command(const ViewFixture &fixture, ViewCommandKind kind,
                               const QUuid &attachment)
    {
        ViewCommand result;
        result.kind = kind;
        result.sessionId = fixture.window->extensionSessionId();
        result.loadGeneration = fixture.snapshot().session.loadGeneration;
        result.layerSessionId = attachment;
        result.origin = QUuid::createUuid();
        return result;
    }
    /** @brief Write three retained features with stable explicit driver IDs.
     * @return GeoJSON filename. */
    QString vectors() const
    {
        const auto output = path("features.geojson");
        QFile file(output);
        if (!file.open(QIODevice::WriteOnly))
            qFatal("Cannot write vector view-event fixture");
        file.write(R"json({"type":"FeatureCollection","features":[
{"type":"Feature","id":0,"properties":{"grp":1},"geometry":{"type":"Point","coordinates":[-76,40]}},
{"type":"Feature","id":1,"properties":{"grp":2},"geometry":{"type":"Point","coordinates":[-75.99,40]}},
{"type":"Feature","id":2,"properties":{"grp":1},"geometry":{"type":"Point","coordinates":[-76,40.01]}}]})json");
        return output;
    }

    /** @brief Construct two inspectable monitoring locations with scalar and nested metadata.
     * @return Native-layer opt-in collection retaining source attributes and stable feature IDs. */
    static QJsonObject nativePreviewCollection()
    {
        return QJsonDocument::fromJson(R"json({
"type":"FeatureCollection",
"previewStyle":{"asFeatureLayer":true,"title":"USGS sites (preview)","pointLabelProperty":"station_name"},
"features":[
{"type":"Feature","id":0,"properties":{"site_no":"A","station_name":"First station","elevation":12.5,"active":true,"details":{"agency":"USGS","datum":"NAVD88"},"parameters":["00060","00065"],"missing":null},"geometry":{"type":"Point","coordinates":[-76,40]}},
{"type":"Feature","id":1,"properties":{"site_no":"B","station_name":"Second station","elevation":8.0,"active":false},"geometry":{"type":"Point","coordinates":[-75.99,40.01]}}
]})json").object();
    }

    /** @brief Resolve the one temporary native preview through ordinary canvas membership.
     * @param[in] fixture Ready native test host.
     * @return Attached temporary GIS vector layer, or null after cleanup/removal. */
    static GISVectorLayer *nativePreview(const ViewFixture &fixture)
    {
        for (auto *base : fixture.window->canvas()->layers())
            if (auto *layer = qobject_cast<GISVectorLayer *>(base); layer && layer->isTemporary())
                return layer;
        return nullptr;
    }

private slots:
    /** @brief Isolate settings and register the production map renderer types. */
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        const QString directory = QStringLiteral(SDK_NATIVE_VIEW_VERIFICATION_DIRECTORY);
        QVERIFY(QDir().mkpath(directory));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory + "/qt-settings");
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                           directory + "/qt-settings");
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0, "SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DResultsQSGRenderer");
        auto format = QSurfaceFormat::defaultFormat();
        format.setSamples(6);
        QSurfaceFormat::setDefaultFormat(format);
    }
    /** @brief Start independent retained case evidence and discard earlier queued cleanup. */
    void init()
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        mEvents.clear();
        mDirectory = QDir(QStringLiteral(SDK_NATIVE_VIEW_VERIFICATION_DIRECTORY))
                         .filePath(QString::fromLatin1(QTest::currentTestFunction()) + '/' +
                                   QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVERIFY(QDir().mkpath(mDirectory));
    }
    /** @brief Disconnect the managed receiver and finish per-case deferred cleanup. */
    void cleanup()
    {
        QObject::disconnect(mObservation);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }

    /** @brief Keep optional point labels plain and fixed-size without acquiring layer ownership. */
    void pointPreviewLabelsAreOptInAndAtomic()
    {
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        QVERIFY(fixture.instance());
        auto *project = fixture.instance()->project();
        auto *scene = fixture.window->canvas()->mapScene();
        QVERIFY(project);
        QVERIFY(scene);
        const auto layerCount = project->layers().size();
        const bool wasDirty = fixture.window->hasChanges();
        const QString title = QStringLiteral("<b>USGS 01234567</b>\n") + QString(300, 'x');
        QJsonObject collection{
            {"type", "FeatureCollection"},
            {"features", QJsonArray{
                             QJsonObject{{"type", "Feature"},
                                         {"properties", QJsonObject{{"site_label", title}}},
                                         {"geometry", QJsonObject{{"type", "Point"},
                                                                  {"coordinates", QJsonArray{-76., 40.}}}}},
                             QJsonObject{{"type", "Feature"},
                                         {"properties", QJsonObject{{"site_label", 42}}},
                                         {"geometry", QJsonObject{{"type", "Point"},
                                                                  {"coordinates", QJsonArray{-75.99, 40.}}}}}}}};
        const auto overlay = [scene]() -> QGraphicsItemGroup * {
            for (auto *item : scene->items())
                if (auto *group = dynamic_cast<QGraphicsItemGroup *>(item);
                    group && group->zValue() == 1e9)
                    return group;
            return nullptr;
        };
        QVERIFY2(project->showPreview(collection, &error), qPrintable(error));
        QVERIFY(overlay());
        for (auto *point : overlay()->childItems())
            QVERIFY(point->childItems().isEmpty());

        collection["previewStyle"] = QJsonObject{{"pointLabelProperty", "site_label"}};
        QVERIFY2(project->showPreview(collection, &error), qPrintable(error));
        auto *group = overlay();
        QVERIFY(group);
        QCOMPARE(group->childItems().size(), 2);
        QGraphicsSimpleTextItem *label = nullptr;
        QGraphicsEllipseItem *anchor = nullptr;
        int labelCount = 0;
        for (auto *point : group->childItems()) {
            for (auto *child : point->childItems()) {
                label = dynamic_cast<QGraphicsSimpleTextItem *>(child);
                QVERIFY(label);
                anchor = dynamic_cast<QGraphicsEllipseItem *>(point);
                QVERIFY(anchor);
                ++labelCount;
            }
        }
        QCOMPARE(labelCount, 1);
        QVERIFY(label);
        QCOMPARE(label->text(), title.simplified().left(256));
        QVERIFY(label->text().startsWith("<b>USGS 01234567</b> "));
        for (double scale : {0.25, 1., 4.}) {
            const auto view = QTransform::fromScale(scale, scale);
            const auto labelTransform = label->deviceTransform(view);
            const auto pointTransform = anchor->deviceTransform(view);
            QCOMPARE(labelTransform.map(QPointF(1, 0)) - labelTransform.map(QPointF()),
                     QPointF(1, 0));
            QCOMPARE(labelTransform.map(QPointF()) - pointTransform.map(QPointF()),
                     QPointF(6, -16));
        }
        QCOMPARE(project->layers().size(), layerCount);
        QCOMPARE(fixture.window->hasChanges(), wasDirty);

        auto malformed = collection;
        auto features = malformed["features"].toArray();
        features.append(QJsonObject{{"type", "Feature"}, {"geometry", QJsonObject{}}});
        malformed["features"] = features;
        QVERIFY(!project->showPreview(malformed, &error));
        QCOMPARE(overlay(), group);
        QCOMPARE(label->text(), title.simplified().left(256));
        project->clearPreview();
        QVERIFY(!overlay());
        QCOMPARE(project->layers().size(), layerCount);
    }

    /** @brief Native previews expose full attributes and retain user query/style/selection on refresh. */
    void nativeFeaturePreviewRetainsNativeToolsAndIdentity()
    {
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *project = fixture.instance()->project();
        const auto layerCount = project->layers().size();
        const bool wasDirty = fixture.window->hasChanges();
        observe(fixture);
        auto collection = nativePreviewCollection();
        auto features = collection["features"].toArray();
        auto first = features.at(0).toObject();
        auto properties = first["properties"].toObject();
        const QString title = QStringLiteral("<b>Full original station name</b> ") + QString(300, 'x');
        properties["station_name"] = title;
        first["properties"] = properties;
        features[0] = first;
        collection["features"] = features;
        QVERIFY2(project->showPreview(collection, &error), qPrintable(error));
        QPointer<GISVectorLayer> layer = nativePreview(fixture);
        QVERIFY(layer);
        QCOMPARE(project->layers().size(), layerCount + 1);
        QCOMPARE(layer->name(), QStringLiteral("USGS sites (preview)"));
        QCOMPARE(layer->featureCount(), 2);
        QVERIFY(layer->labelConfig().enabled);
        QVERIFY(layer->labelConfig().fieldName.startsWith("__openswmm_preview_label"));
        const auto sourcePath = layer->filePath().toUtf8();
        VSIStatBufL sourceStatus{};
        QCOMPARE(VSIStatL(sourcePath.constData(), &sourceStatus), 0);
        vsi_l_offset sourceSize = 0;
        auto *sourceBytes = VSIGetMemFileBuffer(sourcePath.constData(), &sourceSize, false);
        QVERIFY(sourceBytes);
        QFile retained(path("native-source.geojson"));
        QVERIFY(retained.open(QIODevice::WriteOnly));
        QCOMPARE(retained.write(reinterpret_cast<const char *>(sourceBytes), qint64(sourceSize)),
                 qint64(sourceSize));
        retained.close();
        auto *feature = layer->ogrLayer()->GetFeature(0);
        QVERIFY(feature);
        QCOMPARE(QString::fromUtf8(feature->GetFieldAsString("station_name")), title);
        QCOMPARE(QString::fromUtf8(feature->GetFieldAsString("details")),
                 QStringLiteral("{\"agency\":\"USGS\",\"datum\":\"NAVD88\"}"));
        QCOMPARE(QString::fromUtf8(feature->GetFieldAsString("parameters")),
                 QStringLiteral("[\"00060\",\"00065\"]"));
        QCOMPARE(QString::fromUtf8(feature->GetFieldAsString(
                     layer->labelConfig().fieldName.toUtf8().constData())),
                 title.simplified().left(256));
        OGRFeature::DestroyFeature(feature);

        AttributeTablePanel table;
        table.setProject(fixture.window->modelLayer(), fixture.window->selectionManager(),
                         fixture.window->canvas());
        table.showLayerSource(layer);
        auto *view = table.findChild<QTableView *>();
        QVERIFY(view);
        QCOMPARE(view->model()->rowCount(), 2);
        QVERIFY2(layer->setValidatedFilterExpression("site_no = 'A'", &error), qPrintable(error));
        table.refresh();
        table.showLayerSource(layer);
        QCOMPARE(view->model()->rowCount(), 1);
        layer->setSelectedFeatureIds({0});
        QVERIFY(!fixture.window->selectionManager()->selection().isEmpty());
        auto symbol = layer->symbol();
        symbol.markerFill = Qt::magenta;
        layer->setSymbol(symbol);
        auto labels = layer->labelConfig();
        labels.enabled = false;
        layer->setLabelConfig(labels);
        const auto identity = layerState(fixture, layer->layerId()).layerSessionId;
        QVERIFY(!identity.isNull());
        QCoreApplication::processEvents();
        QCOMPARE(count(ViewEventKind::LayerAdded), 1);
        QVERIFY2(project->showPreview(collection, &error), qPrintable(error));
        QCOMPARE(nativePreview(fixture), layer.data());
        QCOMPARE(layerState(fixture, layer->layerId()).layerSessionId, identity);
        QCOMPARE(layer->filterExpression(), QStringLiteral("site_no = 'A'"));
        QCOMPARE(layer->selectedFeatureIds(), QSet<long long>{0});
        QCOMPARE(layer->symbol().markerFill, QColor(Qt::magenta));
        QVERIFY(!layer->labelConfig().enabled);
        QCOMPARE(fixture.window->hasChanges(), wasDirty);

        auto invalid = collection;
        features.append(QJsonObject{{"type", "Feature"}, {"properties", QJsonObject{}},
                                    {"geometry", QJsonObject{}}});
        invalid["features"] = features;
        QVERIFY(!project->showPreview(invalid, &error));
        QCOMPARE(nativePreview(fixture), layer.data());
        auto oversized = collection;
        QJsonObject tooManyFields;
        for (int i = 0; i < 257; ++i)
            tooManyFields[QStringLiteral("field_%1").arg(i)] = i;
        first["properties"] = tooManyFields;
        oversized["features"] = QJsonArray{first};
        QVERIFY(!project->showPreview(oversized, &error));
        QCOMPARE(nativePreview(fixture), layer.data());
        auto replacement = collection;
        replacement["features"] = QJsonArray{collection["features"].toArray().at(1)};
        QVERIFY2(project->showPreview(replacement, &error), qPrintable(error));
        QVERIFY(!layer);
        QVERIFY(nativePreview(fixture));
        QVERIFY(layerState(fixture, nativePreview(fixture)->layerId()).layerSessionId != identity);
        QVERIFY(VSIStatL(sourcePath.constData(), &sourceStatus) != 0);
        replacement["features"] = QJsonArray{};
        QVERIFY2(project->showPreview(replacement, &error), qPrintable(error));
        QVERIFY(nativePreview(fixture));
        QCOMPARE(nativePreview(fixture)->featureCount(), 0);
        QVERIFY2(project->showPreview(collection, &error), qPrintable(error));
        QVERIFY(nativePreview(fixture));
        QCOMPARE(nativePreview(fixture)->featureCount(), 2);
        project->clearPreview();
        QVERIFY(!nativePreview(fixture));
        QCOMPARE(project->layers().size(), layerCount);
        QCOMPARE(fixture.window->hasChanges(), wasDirty);
    }

    /** @brief Hidden preview geometry stays hidden during direct rendering, style refresh and catalogue replacement. */
    void nativeFeaturePreviewVisibilitySurvivesRefreshAndReplacement()
    {
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *canvas = fixture.window->canvas();
        canvas->resize(320, 240);
        QVERIFY(canvas->setCanvasSRSByCode("EPSG", 4326));
        canvas->setExtent(MapExtent(-76.02, 39.985, -75.98, 40.015), false);
        auto collection = nativePreviewCollection();
        auto *project = fixture.instance()->project();
        QVERIFY2(project->showPreview(collection, &error), qPrintable(error));
        QPointer<GISVectorLayer> layer(nativePreview(fixture));
        QVERIFY(layer);
        const auto render = [&](const QString &name) {
            QImage image(320, 240, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            const auto extent = canvas->extent();
            canvas->mapScene()->renderSlice(
                &painter, QRectF(0, 0, 320, 240),
                QRectF(extent.xMin(), -extent.yMax(), extent.width(), extent.height()),
                layer->layerZValue(), layer->layerZValue() + 1000.);
            painter.end();
            if (!image.save(path(name + ".png")))
                return -1;
            int pixels = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    pixels += image.pixelColor(x, y).alpha() > 0;
            return pixels;
        };
        QVERIFY(render("preview-visible") > 20);
        layer->setSelectedFeatureIds({0});
        layer->refreshScene(canvas->mapScene(), canvas->extent(), canvas->canvasSRS());
        layer->setVisible(false);
        // No canvas timer, event-loop turn or source rebuild is required to hide.
        QCOMPARE(render("preview-hidden-immediately"), 0);
        layer->setOpacity(.35);
        layer->refreshScene(canvas->mapScene(), canvas->extent(), canvas->canvasSRS());
        QCOMPARE(render("preview-hidden-after-opacity"), 0);
        auto symbols = layer->symbol();
        symbols.markerSize = 12.;
        layer->setSymbol(symbols);
        layer->refreshScene(canvas->mapScene(), canvas->extent(), canvas->canvasSRS());
        QCOMPARE(render("preview-hidden-after-rebuild"), 0);
        layer->setVisible(true);
        layer->refreshScene(canvas->mapScene(), canvas->extent(), canvas->canvasSRS());
        QVERIFY(render("preview-visible-again") > 20);
        layer->setVisible(false);
        auto features = collection["features"].toArray();
        features.removeLast(); // The Data catalogue filter publishes a new subset.
        collection["features"] = features;
        QVERIFY2(project->showPreview(collection, &error), qPrintable(error));
        QVERIFY(!layer);
        layer = nativePreview(fixture);
        QVERIFY(layer);
        QVERIFY(!layer->isVisible());
        QCOMPARE(layer->opacity(), .35);
        QCOMPARE(render("preview-filtered-hidden"), 0);
        layer->setVisible(true);
        layer->refreshScene(canvas->mapScene(), canvas->extent(), canvas->canvasSRS());
        QVERIFY(render("preview-filtered-visible") > 20);
    }

    /** @brief Cached glyph rendering is pixel-identical to normal label styling and avoids repeated shaping. */
    void preparedLabelGlyphsPreserveSharedStyling()
    {
        using OpenSWMM::Render::LabelConfig;
        using OpenSWMM::Render::LabelPainter;
        LabelConfig config;
        config.font.setItalic(true);
        config.color = QColor(110, 20, 130);
        config.haloEnabled = true;
        config.haloColor = QColor(250, 230, 20);
        config.haloRadiusPx = 1.5;
        config.backgroundEnabled = true;
        config.backgroundColor = QColor(10, 90, 130, 100);
        const QString text = QStringLiteral("USGS 01646500 — Potomac River");
        for (int ratio : {1, 2})
            for (const auto placement : {LabelConfig::AutoPlacement, LabelConfig::Below,
                                         LabelConfig::Centre}) {
                config.placement = placement;
                const auto font = config.effectiveFont();
                const QFontMetricsF metrics(font);
                const QPointF anchor(200., 100.);
                const auto rectangle = LabelPainter::labelRect(
                    config, anchor, {metrics.horizontalAdvance(text), metrics.height()});
                QPainterPath glyphs;
                glyphs.addText({rectangle.x(), rectangle.y() + metrics.ascent()}, font, text);
                QImage original(800 * ratio, 240 * ratio, QImage::Format_ARGB32_Premultiplied);
                original.setDevicePixelRatio(ratio);
                original.fill(Qt::transparent);
                QImage prepared = original;
                {
                    QPainter painter(&original);
                    LabelPainter::drawLabelAt(painter, anchor, text, config);
                }
                {
                    QPainter painter(&prepared);
                    LabelPainter::drawGlyphPath(painter, glyphs, config);
                }
                QCOMPARE(prepared, original);
                QVERIFY(prepared.save(path(QStringLiteral("prepared-%1-%2.png").arg(ratio).arg(placement))));
            }

        const int iterations = 500;
        const QFont font = config.effectiveFont();
        const QFontMetricsF metrics(font);
        const auto rectangle = LabelPainter::labelRect(
            config, {200., 100.}, {metrics.horizontalAdvance(text), metrics.height()});
        QPainterPath glyphs;
        glyphs.addText({rectangle.x(), rectangle.y() + metrics.ascent()}, font, text);
        QImage image(800, 240, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < iterations; ++i)
            LabelPainter::drawLabelAt(painter, {200., 100.}, text, config);
        const qint64 originalNanoseconds = timer.nsecsElapsed();
        timer.restart();
        for (int i = 0; i < iterations; ++i)
            LabelPainter::drawGlyphPath(painter, glyphs, config);
        const qint64 preparedNanoseconds = timer.nsecsElapsed();
        painter.end();
        QFile evidence(path("prepared-label-benchmark.json"));
        QVERIFY(evidence.open(QIODevice::WriteOnly));
        evidence.write(QJsonDocument(QJsonObject{
            {"labelsDrawn", iterations}, {"pixelsMatch", true},
            {"originalGlyphLayouts", iterations}, {"preparedGlyphLayouts", 1},
            {"originalMs", originalNanoseconds / 1e6},
            {"preparedMs", preparedNanoseconds / 1e6}}).toJson());
    }

    /** @brief Dense labelled previews repaint without rebuilding their projected items or glyphs. */
    void densePreviewLabelsReuseSceneItems()
    {
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *canvas = fixture.window->canvas();
        canvas->resize(640, 480);
        QVERIFY(canvas->setCanvasSRSByCode("EPSG", 4326));
        canvas->setExtent(MapExtent(-76., 40., -75.96, 40.03), false);
        auto collection = nativePreviewCollection();
        QJsonArray features;
        for (int i = 0; i < 1000; ++i)
            features.append(QJsonObject{
                {"type", "Feature"}, {"id", i},
                {"properties", QJsonObject{{"station_name", QStringLiteral("USGS station %1").arg(i)}}},
                {"geometry", QJsonObject{{"type", "Point"},
                    {"coordinates", QJsonArray{-75.9995 + (i % 40) * .001,
                                                40.0005 + (i / 40) * .001}}}}});
        collection["features"] = features;
        QElapsedTimer timer;
        timer.start();
        QVERIFY2(fixture.instance()->project()->showPreview(collection, &error), qPrintable(error));
        const auto populateMs = timer.elapsed();
        auto *layer = nativePreview(fixture);
        QVERIFY(layer);
        const auto items = canvas->mapScene()->items();
        int layerItems = 0;
        for (auto *item : items)
            layerItems += item->zValue() >= layer->layerZValue() &&
                          item->zValue() < layer->layerZValue() + 1000.;
        QCOMPARE(layerItems, 2000); // One point and one prepared label per station.
        QImage image(640, 480, QImage::Format_ARGB32_Premultiplied);
        QList<double> renderMs;
        for (int pass = 0; pass < 8; ++pass) {
            layer->refreshScene(canvas->mapScene(), canvas->extent(), canvas->canvasSRS());
            QCOMPARE(canvas->mapScene()->items(), items);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            timer.restart();
            const auto extent = canvas->extent();
            canvas->mapScene()->renderSlice(
                &painter, QRectF(0, 0, 640, 480),
                QRectF(extent.xMin(), -extent.yMax(), extent.width(), extent.height()),
                layer->layerZValue(), layer->layerZValue() + 1000.);
            renderMs.append(timer.nsecsElapsed() / 1e6);
        }
        QVERIFY(image.save(path("dense-preview.png")));
        QJsonArray timings;
        for (double value : renderMs)
            timings.append(value);
        QFile evidence(path("dense-preview-benchmark.json"));
        QVERIFY(evidence.open(QIODevice::WriteOnly));
        evidence.write(QJsonDocument(QJsonObject{{"features", 1000}, {"sceneItems", layerItems},
            {"populateMs", populateMs}, {"renderMs", timings}, {"itemsReused", true}}).toJson());
    }

    /** @brief Raster label reuse preserves DPR/style/opacity and keeps vector export and oversized-label fallbacks. */
    void nativeLabelRasterCacheRetainsRenderingPolicies()
    {
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *canvas = fixture.window->canvas();
        canvas->resize(320, 240);
        QVERIFY(canvas->setCanvasSRSByCode("EPSG", 4326));
        canvas->setExtent(MapExtent(-76.02, 39.985, -75.98, 40.015), false);
        auto collection = nativePreviewCollection();
        collection["features"] = QJsonArray{collection["features"].toArray().at(0)};
        QVERIFY2(fixture.instance()->project()->showPreview(collection, &error), qPrintable(error));
        auto *layer = nativePreview(fixture);
        QVERIFY(layer);
        auto labels = layer->labelConfig();
        labels.haloEnabled = true;
        labels.haloColor = QColor(250, 220, 20);
        labels.backgroundEnabled = true;
        labels.backgroundColor = QColor(20, 100, 140, 100);
        labels.color = QColor(170, 0, 170);
        layer->setLabelConfig(labels);
        layer->refreshScene(canvas->mapScene(), canvas->extent(), canvas->canvasSRS());
        const auto originalItems = canvas->mapScene()->items();
        auto render = [&](int ratio, const QString &name) {
            QImage image(320 * ratio, 240 * ratio, QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(ratio);
            image.fill(Qt::transparent);
            {
                QPainter painter(&image);
                const auto extent = canvas->extent();
                canvas->mapScene()->renderSlice(
                    &painter, QRectF(0, 0, 320, 240),
                    QRectF(extent.xMin(), -extent.yMax(), extent.width(), extent.height()),
                    layer->layerZValue() + 3., layer->layerZValue() + 4.);
            }
            image.save(path(name + ".png"));
            return image;
        };
        const auto first = render(1, "cache-dpr1-cold");
        QCOMPARE(render(1, "cache-dpr1-warm"), first);
        const auto highDpi = render(2, "cache-dpr2-cold");
        QCOMPARE(render(2, "cache-dpr2-warm"), highDpi);
        QCOMPARE(render(1, "cache-dpr1-return"), first);
        QCOMPARE(canvas->mapScene()->items(), originalItems);

        layer->setOpacity(.35);
        layer->refreshScene(canvas->mapScene(), canvas->extent(), canvas->canvasSRS());
        const auto faded = render(1, "cache-faded-cold");
        QVERIFY(faded != first);
        QCOMPARE(render(1, "cache-faded-warm"), faded);
        layer->setOpacity(1.);
        layer->refreshScene(canvas->mapScene(), canvas->extent(), canvas->canvasSRS());
        QCOMPARE(render(1, "cache-opacity-restored"), first);

        canvas->setExtent(MapExtent(-76.021, 39.985, -75.981, 40.015), false);
        const auto panned = render(1, "cache-pan");
        QVERIFY(panned != first);
        QCOMPARE(canvas->mapScene()->items(), originalItems);
        canvas->setExtent(MapExtent(-76.03, 39.9775, -75.97, 40.0225), false);
        QCOMPARE(render(1, "cache-zoom"), first); // Same anchor, fixed-pixel label size.
        QCOMPARE(canvas->mapScene()->items(), originalItems);

        const auto pdfPath = path("cached-label-vector-export.pdf");
        {
            QPdfWriter pdf(pdfPath);
            pdf.setResolution(96);
            QPainter painter(&pdf);
            const auto extent = canvas->extent();
            canvas->mapScene()->renderSlice(
                &painter, QRectF(0, 0, 320, 240),
                QRectF(extent.xMin(), -extent.yMax(), extent.width(), extent.height()),
                layer->layerZValue() + 3., layer->layerZValue() + 4.);
        }
        QFile pdf(pdfPath);
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        const auto pdfBytes = pdf.readAll();
        QVERIFY(pdfBytes.startsWith("%PDF-"));
        QVERIFY2(!pdfBytes.contains("/Subtype /Image"), "Vector label export embedded a raster cache.");

        labels.color = QColor(0, 150, 20);
        layer->setLabelConfig(labels);
        layer->refreshScene(canvas->mapScene(), canvas->extent(), canvas->canvasSRS());
        const auto restyled = render(1, "cache-restyled");
        QVERIFY(restyled != first);
        QCOMPARE(render(1, "cache-restyled-warm"), restyled);
        labels.fontSizePt = 256.; // Larger than the per-label raster allocation limit.
        labels.placement = OpenSWMM::Render::LabelConfig::Centre;
        layer->setLabelConfig(labels);
        layer->refreshScene(canvas->mapScene(), canvas->extent(), canvas->canvasSRS());
        const auto oversized = render(1, "cache-oversized-vector-fallback");
        int visible = 0;
        for (int y = 0; y < oversized.height(); ++y)
            for (int x = 0; x < oversized.width(); ++x)
                visible += oversized.pixelColor(x, y).alpha() > 0;
        QVERIFY(visible > 100);
    }

    /** @brief Manual removal stays removed, temporary polygons are not boundaries, and close releases bytes. */
    void nativeFeaturePreviewHonoursRemovalAndClosure()
    {
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *project = fixture.instance()->project();
        auto collection = nativePreviewCollection();
        auto first = collection["features"].toArray().at(0).toObject();
        first["geometry"] = QJsonDocument::fromJson(R"json({"type":"Polygon","coordinates":[[[-76,40],[-75.99,40],[-75.99,40.01],[-76,40.01],[-76,40]]]})json").object();
        collection["features"] = QJsonArray{first};
        QVERIFY2(project->showPreview(collection, &error), qPrintable(error));
        auto *layer = nativePreview(fixture);
        QVERIFY(layer);
        for (const auto &boundary : project->polygonLayers())
            QVERIFY(boundary.value("id").toString() != layer->layerId());
        QVERIFY(project->boundaryFeatures(layer->layerId()).isEmpty());
        QVERIFY(project->boundaryPolygon(layer->layerId(), 0, &error).isEmpty());
        const auto sourcePath = layer->filePath().toUtf8();
        const auto countBefore = fixture.window->canvas()->undoStack()->count();
        fixture.window->canvas()->takeLayer(fixture.window->canvas()->layers().indexOf(layer));
        delete layer;
        QCOMPARE(fixture.window->canvas()->undoStack()->count(), countBefore);
        QVERIFY2(project->showPreview(collection, &error), qPrintable(error));
        QVERIFY(!nativePreview(fixture));
        collection["previewStyle"] = QJsonObject{{"asFeatureLayer", true}, {"title", "Changed refresh"}};
        QVERIFY2(project->showPreview(collection, &error), qPrintable(error));
        QVERIFY(!nativePreview(fixture));
        VSIStatBufL status{};
        QVERIFY(VSIStatL(sourcePath.constData(), &status) != 0);
        project->clearPreview();
        QVERIFY2(project->showPreview(collection, &error), qPrintable(error));
        QPointer<GISVectorLayer> restored(nativePreview(fixture));
        QVERIFY(restored);
        const auto restoredPath = restored->filePath().toUtf8();
        delete fixture.window.data();
        QVERIFY(!restored);
        QVERIFY(VSIStatL(restoredPath.constData(), &status) != 0);
        project->clearPreview(); // Retired facade remains safe after its native model has closed.
    }

    /** @brief Unloading the host from a native removal callback cannot publish an orphan replacement. */
    void nativeFeaturePreviewSurvivesReentrantHostDestruction()
    {
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *project = fixture.instance()->project();
        auto collection = nativePreviewCollection();
        QVERIFY2(project->showPreview(collection, &error), qPrintable(error));
        QPointer<GISVectorLayer> old(nativePreview(fixture));
        QVERIFY(old);
        const auto oldPath = old->filePath().toUtf8();
        connect(fixture.window->canvas(), &MapCanvas::layerRemoved, &fixture.main,
                [&fixture] { fixture.host.reset(); });
        collection["features"] = QJsonArray{collection["features"].toArray().at(1)};
        QVERIFY(!project->showPreview(collection, &error));
        QVERIFY(!fixture.host);
        QVERIFY(!old);
        QVERIFY(!nativePreview(fixture));
        VSIStatBufL status{};
        QVERIFY(VSIStatL(oldPath.constData(), &status) != 0);
    }

    /** @brief Cover projected/geographic maps, three zoom levels and standard/retina backing images. */
    void nativeFeaturePointMarkersRenderAtPixelSize_data()
    {
        QTest::addColumn<int>("epsg");
        QTest::addColumn<double>("span");
        QTest::addColumn<int>("pixelRatio");
        for (int epsg : {3857, 4326})
            for (double span : epsg == 3857 ? QList<double>{200., 20000., 2000000.}
                                             : QList<double>{0.0002, 0.02, 2.})
                for (int ratio : {1, 2}) {
                    const auto name = QStringLiteral("epsg%1-span%2-dpr%3")
                                          .arg(epsg).arg(span).arg(ratio).toLatin1();
                    QTest::newRow(name.constData()) << epsg << span << ratio;
                }
    }

    /** @brief Rasterize a real temporary preview through the canvas scene transform and measure visible dots. */
    void nativeFeaturePointMarkersRenderAtPixelSize()
    {
        QFETCH(int, epsg);
        QFETCH(double, span);
        QFETCH(int, pixelRatio);
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *canvas = fixture.window->canvas();
        canvas->resize(320, 240);
        QCOMPARE(canvas->size(), QSize(320, 240));
        QVERIFY(canvas->setCanvasSRSByCode("EPSG", epsg));
        auto collection = nativePreviewCollection();
        collection["previewStyle"] = QJsonObject{{"asFeatureLayer", true}, {"title", "Marker render"}};
        auto feature = collection["features"].toArray().at(0).toObject();
        feature["geometry"] = QJsonObject{{"type", "Point"},
                                           {"coordinates", QJsonArray{-75.995, 40.005}}};
        collection["features"] = QJsonArray{feature};
        QVERIFY2(fixture.instance()->project()->showPreview(collection, &error), qPrintable(error));
        auto *layer = nativePreview(fixture);
        QVERIFY(layer);
        OGRSpatialReference geographic, target(*canvas->canvasSRS()->ogrSpatialReference());
        geographic.importFromEPSG(4326);
        geographic.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        target.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        auto *projection = OGRCreateCoordinateTransformation(&geographic, &target);
        QVERIFY(projection);
        double x = -75.995, y = 40.005;
        const bool transformed = projection->Transform(1, &x, &y);
        OCTDestroyCoordinateTransformation(projection);
        QVERIFY(transformed);
        const double height = span * 0.75;
        canvas->setExtent(MapExtent(x - span / 2., y - height / 2.,
                                    x + span / 2., y + height / 2.), false);
        const auto extent = canvas->extent();
        const QRectF source(extent.xMin(), -extent.yMax(), extent.width(), extent.height());
        QImage image(320 * pixelRatio, 240 * pixelRatio, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(pixelRatio);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            // Match MapCanvas's scene-buffer/slice source rectangle, target
            // transform and image DPR, isolating this native layer's stack band.
            canvas->mapScene()->renderSlice(&painter, QRectF(0, 0, 320, 240), source,
                                             layer->layerZValue(), layer->layerZValue() + 1000.);
        }
        QVERIFY(image.save(path("point-render.png")));
        int minX = image.width(), minY = image.height(), maxX = -1, maxY = -1;
        for (int row = 0; row < image.height(); ++row)
            for (int column = 0; column < image.width(); ++column) {
                const auto color = image.pixelColor(column, row);
                if (color.green() > color.red() + 30 && color.blue() > color.red() + 30) {
                    minX = qMin(minX, column);
                    minY = qMin(minY, row);
                    maxX = qMax(maxX, column);
                    maxY = qMax(maxY, row);
                }
            }
        const QRectF bounds(double(minX) / pixelRatio, double(minY) / pixelRatio,
                             double(maxX - minX + 1) / pixelRatio,
                             double(maxY - minY + 1) / pixelRatio);
        const QJsonObject evidence{{"epsg", epsg}, {"span", span}, {"pixelRatio", pixelRatio},
                                    {"bounds", QJsonArray{bounds.x(), bounds.y(), bounds.width(), bounds.height()}}};
        QFile record(path("point-render.json"));
        QVERIFY(record.open(QIODevice::WriteOnly));
        record.write(QJsonDocument(evidence).toJson());
        QVERIFY2(maxX >= minX && maxY >= minY, "The native GIS point did not paint visible pixels.");
        QVERIFY2(bounds.width() >= 7. && bounds.width() <= 11., qPrintable(QString::number(bounds.width())));
        QVERIFY2(bounds.height() >= 7. && bounds.height() <= 11., qPrintable(QString::number(bounds.height())));
        QVERIFY(std::abs(bounds.center().x() - 160.) <= 1.);
        QVERIFY(std::abs(bounds.center().y() - 120.) <= 1.);
        layer->setSelectedFeatureIds({0});
        layer->refreshScene(canvas->mapScene(), canvas->extent(), canvas->canvasSRS());
        const auto anchors = layer->selectedFeatureAnchors();
        QCOMPARE(anchors.size(), 1);
        QVERIFY(std::abs(anchors[0].x() - x) < 1e-5);
        QVERIFY(std::abs(anchors[0].y() + y) < 1e-5);
    }

    /** @brief Cover ordinary sources and native preview label columns in both map CRSs and DPRs. */
    void nativeGisLabelsPaintConfiguredFields_data()
    {
        QTest::addColumn<bool>("temporary");
        QTest::addColumn<int>("epsg");
        QTest::addColumn<int>("pixelRatio");
        for (bool temporary : {false, true})
            for (int epsg : {3857, 4326})
                for (int ratio : {1, 2}) {
                    const auto name = QStringLiteral("%1-epsg%2-dpr%3")
                                          .arg(temporary ? "preview" : "ordinary")
                                          .arg(epsg).arg(ratio).toLatin1();
                    QTest::newRow(name.constData()) << temporary << epsg << ratio;
                }
    }

    /** @brief Prove enabled labels paint text/halo and update without hiding or blocking gage points. */
    void nativeGisLabelsPaintConfiguredFields()
    {
        QFETCH(bool, temporary);
        QFETCH(int, epsg);
        QFETCH(int, pixelRatio);
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *canvas = fixture.window->canvas();
        canvas->resize(320, 240);
        QVERIFY(canvas->setCanvasSRSByCode("EPSG", epsg));
        auto collection = nativePreviewCollection();
        auto feature = collection["features"].toArray().at(0).toObject();
        auto properties = feature["properties"].toObject();
        properties["monitoring_location_name"] = "Station A";
        properties["short_name"] = "X";
        properties["drainage"] = 12.345;
        feature["properties"] = properties;
        feature["geometry"] = QJsonObject{{"type", "Point"},
                                           {"coordinates", QJsonArray{-75.995, 40.005}}};
        collection["features"] = QJsonArray{feature};
        collection["previewStyle"] = QJsonObject{{"asFeatureLayer", true},
                                                   {"pointLabelProperty", "monitoring_location_name"}};
        GISVectorLayer *layer = nullptr;
        if (temporary) {
            QVERIFY2(fixture.instance()->project()->showPreview(collection, &error), qPrintable(error));
            layer = nativePreview(fixture);
        } else {
            collection.remove("previewStyle");
            const auto sourcePath = path("ordinary-label-source.geojson");
            QFile source(sourcePath);
            QVERIFY(source.open(QIODevice::WriteOnly));
            source.write(QJsonDocument(collection).toJson());
            source.close();
            layer = new GISVectorLayer(sourcePath, {}, fixture.workspace.get());
            canvas->addLayer(layer, false);
        }
        QVERIFY(layer);
        QVERIFY(layer->ogrLayer());
        auto symbol = layer->symbol();
        symbol.markerSize = 8.;
        symbol.markerFill = QColor(0, 145, 170, 150);
        symbol.markerOutline = QColor(0, 145, 170);
        layer->setSymbol(symbol);
        auto labels = layer->labelConfig();
        labels.enabled = true;
        if (temporary)
            QVERIFY(labels.fieldName.startsWith("__openswmm_preview_label"));
        else
            labels.fieldName = "monitoring_location_name";
        labels.color = QColor(170, 0, 170);
        labels.haloEnabled = true;
        labels.haloColor = QColor(255, 200, 0);
        labels.haloRadiusPx = 1.;
        labels.minScale = 0.;
        labels.maxScale = 0.;
        layer->setLabelConfig(labels);
        OGRSpatialReference geographic, target(*canvas->canvasSRS()->ogrSpatialReference());
        geographic.importFromEPSG(4326);
        geographic.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        target.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        auto *projection = OGRCreateCoordinateTransformation(&geographic, &target);
        QVERIFY(projection);
        double x = -75.995, y = 40.005;
        const bool transformed = projection->Transform(1, &x, &y);
        OCTDestroyCoordinateTransformation(projection);
        QVERIFY(transformed);
        struct Pixels {
            QRectF textBounds; ///< Logical-pixel bounds containing the configured purple glyphs.
            int points = 0;    ///< Cyan marker pixels independent of label painting.
            int halo = 0;      ///< Orange halo pixels from the configured shared label painter.
            bool saved = false; ///< Retained image was written for review.
        };
        auto render = [&](const QString &name) {
            layer->refreshScene(canvas->mapScene(), canvas->extent(), canvas->canvasSRS());
            const auto extent = canvas->extent();
            QImage image(320 * pixelRatio, 240 * pixelRatio, QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(pixelRatio);
            image.fill(Qt::white);
            {
                QPainter painter(&image);
                canvas->mapScene()->renderSlice(
                    &painter, QRectF(0, 0, 320, 240),
                    QRectF(extent.xMin(), -extent.yMax(), extent.width(), extent.height()),
                    layer->layerZValue(), layer->layerZValue() + 1000.);
            }
            Pixels result;
            result.saved = image.save(path(name + ".png"));
            int minX = image.width(), minY = image.height(), maxX = -1, maxY = -1;
            for (int row = 0; row < image.height(); ++row)
                for (int column = 0; column < image.width(); ++column) {
                    const auto color = image.pixelColor(column, row);
                    if (color.red() > color.green() + 30 && color.blue() > color.green() + 30) {
                        minX = qMin(minX, column);
                        minY = qMin(minY, row);
                        maxX = qMax(maxX, column);
                        maxY = qMax(maxY, row);
                    }
                    result.points += color.green() > color.red() + 30 && color.blue() > color.red() + 30;
                    result.halo += color.red() > color.green() + 20 && color.green() > color.blue() + 40;
                }
            if (maxX >= minX && maxY >= minY)
                result.textBounds = QRectF(double(minX) / pixelRatio, double(minY) / pixelRatio,
                                           double(maxX - minX + 1) / pixelRatio,
                                           double(maxY - minY + 1) / pixelRatio);
            return result;
        };
        QRectF initialBounds;
        int zoom = 0;
        for (double span : epsg == 3857 ? QList<double>{20000., 2000000.}
                                         : QList<double>{0.02, 2.}) {
            canvas->setExtent(MapExtent(x - span / 2., y - span * 0.375,
                                        x + span / 2., y + span * 0.375), false);
            const auto painted = render(QStringLiteral("labels-on-%1").arg(zoom++));
            QVERIFY(painted.saved);
            QVERIFY2(painted.textBounds.width() > 20., "Enabled native GIS labels painted no station text.");
            QVERIFY(painted.textBounds.height() > 4.);
            QVERIFY(painted.points > 10);
            QVERIFY(painted.halo > 10);
            if (initialBounds.isEmpty())
                initialBounds = painted.textBounds;
            else {
                QVERIFY(std::abs(painted.textBounds.width() - initialBounds.width()) <= 1.);
                QVERIFY(std::abs(painted.textBounds.height() - initialBounds.height()) <= 1.);
            }
        }
        labels.fieldName = "short_name";
        layer->setLabelConfig(labels);
        const auto shortField = render("labels-short-field");
        QVERIFY(shortField.saved);
        QVERIFY(shortField.textBounds.width() > 0.);
        QVERIFY(shortField.textBounds.width() < initialBounds.width() / 2.);
        labels.expression = "{monitoring_location_name}: {drainage}";
        labels.decimals = 1;
        layer->setLabelConfig(labels);
        const auto expression = render("labels-expression");
        QVERIFY(expression.saved);
        QVERIFY(expression.textBounds.width() > initialBounds.width() + 10.);
        labels.minScale = canvas->scaleDenominator() / 2.;
        layer->setLabelConfig(labels);
        const auto hidden = render("labels-scale-hidden");
        QVERIFY(hidden.saved);
        QVERIFY(hidden.textBounds.isEmpty());
        QVERIFY(hidden.points > 10);
        labels.minScale = 0.;
        labels.enabled = false;
        layer->setLabelConfig(labels);
        const auto disabled = render("labels-off");
        QVERIFY(disabled.saved);
        QVERIFY(disabled.textBounds.isEmpty());
        QVERIFY(disabled.points > 10);
        const auto hits = layer->identifyAt(x, y, canvas->canvasSRS(),
                                             canvas->extent().width() / canvas->width() * 6.);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().value("monitoring_location_name").toString(), QStringLiteral("Station A"));
        layer->setSelectedFeatureIds({0});
        layer->refreshScene(canvas->mapScene(), canvas->extent(), canvas->canvasSRS());
        QCOMPARE(layer->selectedFeatureAnchors().size(), 1);
        if (temporary)
            fixture.instance()->project()->clearPreview();
        else {
            canvas->takeLayer(canvas->layers().indexOf(layer), false);
            delete layer;
        }
    }

    /** @brief Re-key selected built-in table identities without touching a colliding link namespace. */
    void builtInRenameKeepsSharedTypedSelection()
    {
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *layer = fixture.window->modelLayer();
        QVERIFY(layer->applyRename("C1", "J1", SWMMModelLayer::kKindLink));
        const SWMMObjectRef unrelated{SWMMObjectRef::Curve, "external-selection"};
        fixture.window->selectionManager()->select(QSet<SWMMObjectRef>{
            {SWMMObjectRef::Node, "J1"}, {SWMMObjectRef::Link, "J1"}, unrelated});
        observe(fixture);
        SWMMAttributeTableModel table;
        table.setSource(layer, SWMMModelLayer::CatJunctions);
        table.setUndoStack(fixture.window->canvas()->undoStack());
        QCOMPARE(table.objectNameAt(0), QStringLiteral("J1"));
        QVERIFY(table.setData(table.index(0, 0), QStringLiteral("NativeRenamed")));
        const auto selected = fixture.window->selectionManager()->selection();
        QVERIFY(selected.contains({SWMMObjectRef::Node, "NativeRenamed"}));
        QVERIFY(!selected.contains({SWMMObjectRef::Node, "J1"}));
        QVERIFY(selected.contains({SWMMObjectRef::Link, "J1"}));
        QVERIFY(selected.contains(unrelated));
        QTRY_COMPARE(count(ViewEventKind::ObjectRenamed), 1);
        const auto rename = *std::find_if(mEvents.cbegin(), mEvents.cend(), [](const auto &event) {
            return event.kind == ViewEventKind::ObjectRenamed;
        });
        QCOMPARE(rename.objectNamespace, QStringLiteral("node"));
        QCOMPARE(rename.previousName, QStringLiteral("J1"));
        QCOMPARE(rename.objectName, QStringLiteral("NativeRenamed"));
        fixture.window->canvas()->undoStack()->undo();
        QVERIFY(
            fixture.window->selectionManager()->selection().contains({SWMMObjectRef::Node, "J1"}));
        QVERIFY(!fixture.window->selectionManager()->selection().contains(
            {SWMMObjectRef::Node, "NativeRenamed"}));
        QTRY_COMPARE(count(ViewEventKind::ObjectRenamed), 2);
    }

    /** @brief Keep blocked SDK edits, rollback and snapshot undo coherent with shared selections. */
    void pluginRenameCommitAbortUndoPreserveSelections()
    {
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *layer = fixture.window->modelLayer();
        QVERIFY(layer->applyRename("C1", "J1", SWMMModelLayer::kKindLink));
        auto *selection = fixture.window->selectionManager();
        const SWMMObjectRef unrelated{SWMMObjectRef::Curve, "external-selection"};
        selection->select(QSet<SWMMObjectRef>{
            {SWMMObjectRef::Node, "J1"}, {SWMMObjectRef::Link, "J1"}, unrelated});
        observe(fixture);
        auto *instance = fixture.instance();
        QVERIFY(instance);
        auto edit = instance->edit("Rename selected node", &error);
        QVERIFY2(edit, qPrintable(error));
        QVERIFY(edit->rename("node", "J1", "PluginRenamed"));
        QVERIFY2(edit->commit(&error), qPrintable(error));
        edit.reset();
        QVERIFY(selection->selection().contains({SWMMObjectRef::Node, "PluginRenamed"}));
        QVERIFY(selection->selection().contains({SWMMObjectRef::Link, "J1"}));
        QVERIFY(selection->selection().contains(unrelated));
        QTRY_COMPARE(count(ViewEventKind::ModelGeometryChanged), 1);
        QCOMPARE(count(ViewEventKind::ObjectRenamed),
                 0); // Native intermediate signals were blocked.
        const SWMMObjectRef laterSelection{SWMMObjectRef::TimeSeries, "selected-after-edit"};
        selection->select(laterSelection, SelectionManager::Add);
        fixture.window->canvas()->undoStack()->undo();
        QVERIFY(selection->selection().contains({SWMMObjectRef::Node, "J1"}));
        QVERIFY(!selection->selection().contains({SWMMObjectRef::Node, "PluginRenamed"}));
        QVERIFY(selection->selection().contains(laterSelection));
        fixture.window->canvas()->undoStack()->redo();
        QVERIFY(selection->selection().contains({SWMMObjectRef::Node, "PluginRenamed"}));
        const int undoCount = fixture.window->canvas()->undoStack()->count();
        edit = instance->edit("Aborted selected rename", &error);
        QVERIFY2(edit, qPrintable(error));
        QVERIFY(edit->rename("node", "PluginRenamed", "AbortedName"));
        QVERIFY2(edit->abort(&error), qPrintable(error));
        edit.reset();
        QVERIFY(selection->selection().contains({SWMMObjectRef::Node, "PluginRenamed"}));
        QVERIFY(!selection->selection().contains({SWMMObjectRef::Node, "AbortedName"}));
        QVERIFY(selection->selection().contains({SWMMObjectRef::Link, "J1"}));
        QVERIFY(selection->selection().contains(unrelated));
        QCOMPARE(fixture.window->canvas()->undoStack()->count(), undoCount);
    }

    /** @brief Publish bounded broad geometry/options and distinct computed schema/value invalidations. */
    void modelChangesComputedFieldsAndUnits()
    {
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        observe(fixture);
        auto *layer = fixture.window->modelLayer();
        const auto before = fixture.snapshot();
        for (int index = 0; index < 100; ++index)
            QVERIFY(layer->applyNodeMove(0, -76. + double(index) / 10000., 40.));
        QTRY_COMPARE(count(ViewEventKind::ModelGeometryChanged), 1);
        QVERIFY(fixture.snapshot().session.stateRevision > before.session.stateRevision);
        double x = 0., y = 0.;
        QCOMPARE(swmm_spatial_get_node_coord(layer->engine(), 0, &x, &y), SWMM_OK);
        QVERIFY(qAbs(x - (-76. + 99. / 10000.)) < 1e-9);
        mEvents.clear();
        QVERIFY(layer->setOption("LINK_OFFSETS", "ELEVATION"));
        QTRY_COMPARE(count(ViewEventKind::ModelOptionsChanged), 1);
        const auto options = *std::find_if(mEvents.cbegin(), mEvents.cend(), [](const auto &event) {
            return event.kind == ViewEventKind::ModelOptionsChanged;
        });
        QVERIFY(options.keys.contains("LINK_OFFSETS"));
        mEvents.clear();
        ViewAttributes attributes;
        QVERIFY2(fixture.instance()->registerAttributeProvider("computed", &attributes, &attributes,
                                                               &error),
                 qPrintable(error));
        QTRY_COMPARE(count(ViewEventKind::AttributeSchemaChanged), 1);
        mEvents.clear();
        attributes.factor = 3.;
        fixture.instance()->refreshAttributeProvider("computed");
        QTRY_COMPARE(count(ViewEventKind::AttributeValuesChanged), 1);
        QCOMPARE(count(ViewEventKind::AttributeSchemaChanged), 0);
        fixture.instance()->unregisterAttributeProvider("computed");
        QTRY_COMPARE(count(ViewEventKind::AttributeSchemaChanged), 1);
        mEvents.clear();
        fixture.window->unitSystem()->setFlowUnits(swmm_CFS);
        QTRY_COMPARE(count(ViewEventKind::ProjectUnitsChanged), 1);
        QCOMPARE(fixture.snapshot().projectUnits, QStringLiteral("CFS"));
        const auto stable = fixture.snapshot();
        for (int index = 0; index < 20; ++index) {
            QCOMPARE(fixture.snapshot().sequence, stable.sequence);
            QCOMPARE(fixture.snapshot().layers.size(), stable.layers.size());
        }
    }

    /** @brief Route native and plugin layer property changes to one shared tracked state. */
    void layerPropertiesOriginsAndNoops()
    {
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *vector = new GISVectorLayer(vectors(), {}, fixture.workspace.get());
        fixture.window->canvas()->addLayer(vector, false);
        const auto attachment = layerState(fixture, vector->layerId()).layerSessionId;
        QVERIFY(!attachment.isNull());
        observe(fixture);
        vector->setName("Native title");
        vector->setVisible(false);
        vector->setOpacity(.4);
        QTRY_COMPARE(count(ViewEventKind::LayerMetadataChanged, attachment), 1);
        const auto native = layerState(fixture, vector->layerId());
        QCOMPARE(native.title, QStringLiteral("Native title"));
        QVERIFY(!native.visible);
        QCOMPARE(native.opacity, .4);
        auto change = command(fixture, ViewCommandKind::RenameLayer, attachment);
        change.text = "Plugin title";
        QVERIFY2(fixture.controls()->executeViewCommand(change, &error), qPrintable(error));
        change.kind = ViewCommandKind::SetLayerVisible;
        change.enabled = true;
        QVERIFY2(fixture.controls()->executeViewCommand(change, &error), qPrintable(error));
        change.kind = ViewCommandKind::SetLayerOpacity;
        change.number = .65;
        QVERIFY2(fixture.controls()->executeViewCommand(change, &error), qPrintable(error));
        QTRY_COMPARE(count(ViewEventKind::LayerMetadataChanged, attachment), 2);
        const auto plugin = layerState(fixture, vector->layerId());
        QCOMPARE(plugin.title, QStringLiteral("Plugin title"));
        QVERIFY(plugin.visible);
        QCOMPARE(plugin.opacity, .65);
        QCOMPARE(mEvents.last().origin, change.origin);
        const auto watermark = fixture.snapshot().sequence;
        QVERIFY(fixture.controls()->executeViewCommand(change, &error));
        QCOMPARE(fixture.snapshot().sequence, watermark);
        change.number = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!fixture.controls()->executeViewCommand(change, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(vector->opacity(), .65);
        mEvents.clear();
        fixture.window->modelLayer()->setCategoryOpacity(SWMMModelLayer::CatJunctions, .3);
        QTRY_COMPARE(count(ViewEventKind::LayerStyleChanged), 1);
        mEvents.clear();
        emit vector->repaintRequested();
        QCoreApplication::processEvents();
        QCOMPARE(count(ViewEventKind::LayerStyleChanged), 0);
    }

    /** @brief Validate vector filters/selections and prevent removed attachment IDs from being reused. */
    void layerFilterSelectionRemovalAndReattach()
    {
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *vector = new GISVectorLayer(vectors(), {}, fixture.workspace.get());
        fixture.window->canvas()->addLayer(vector, false);
        const auto attachment = layerState(fixture, vector->layerId()).layerSessionId;
        observe(fixture);
        auto change = command(fixture, ViewCommandKind::SetLayerFilter, attachment);
        change.text = "grp = 1";
        QVERIFY2(fixture.controls()->executeViewCommand(change, &error), qPrintable(error));
        QCOMPARE(vector->filterExpression(), change.text);
        // Fast metadata counts may be unknown for filtered GeoJSON. Verify the
        // applied predicate explicitly without adding scans to public snapshots.
        QCOMPARE(vector->ogrLayer()->GetFeatureCount(TRUE), GIntBig(2));
        QTRY_COMPARE(count(ViewEventKind::LayerFilterChanged, attachment), 1);
        const auto stable = fixture.snapshot().sequence;
        QVERIFY(fixture.controls()->executeViewCommand(change, &error));
        QCOMPARE(fixture.snapshot().sequence, stable);
        change.text = "this is not valid SQL (";
        QVERIFY(!fixture.controls()->executeViewCommand(change, &error));
        QCOMPARE(vector->filterExpression(), QStringLiteral("grp = 1"));
        QCOMPARE(vector->ogrLayer()->GetFeatureCount(TRUE), GIntBig(2));
        change.text.clear();
        QVERIFY(fixture.controls()->executeViewCommand(change, &error));
        change.kind = ViewCommandKind::SelectLayerFeatures;
        change.featureIds = {2, 0, 2};
        QVERIFY2(fixture.controls()->executeViewCommand(change, &error), qPrintable(error));
        QCOMPARE(fixture.controls()->selectedLayerFeatures(change.sessionId, attachment, 0, 1),
                 QList<qint64>{0});
        QCOMPARE(fixture.controls()->selectedLayerFeatures(change.sessionId, attachment, 1, 10),
                 QList<qint64>{2});
        QTRY_COMPARE(count(ViewEventKind::LayerSelectionChanged, attachment), 1);
        QCOMPARE(layerState(fixture, vector->layerId()).selectionCount, qint64{2});
        const auto selectedWatermark = fixture.snapshot().sequence;
        QVERIFY(fixture.controls()->executeViewCommand(change, &error));
        QCOMPARE(fixture.snapshot().sequence, selectedWatermark);
        change.featureIds = {-1};
        QVERIFY(!fixture.controls()->executeViewCommand(change, &error));
        change.kind = ViewCommandKind::RemoveLayer;
        QVERIFY2(fixture.controls()->executeViewCommand(change, &error), qPrintable(error));
        QVERIFY(layerState(fixture, vector->layerId()).layerSessionId.isNull());
        QTRY_COMPARE(count(ViewEventKind::LayerRemoved, attachment), 1);
        change.kind = ViewCommandKind::SetLayerVisible;
        QVERIFY(!fixture.controls()->executeViewCommand(change, &error));
        fixture.window->canvas()->undoStack()->undo();
        const auto attachedAgain = layerState(fixture, vector->layerId()).layerSessionId;
        QVERIFY(!attachedAgain.isNull());
        QVERIFY(attachedAgain != attachment);
        QVERIFY(!fixture.controls()->executeViewCommand(change, &error));
    }

    /** @brief Use the same category-constrained stack controller from native and plugin routes. */
    void layerOrderAndStaleGenerationCommands()
    {
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        const auto input = vectors();
        auto *first = new GISVectorLayer(input, {}, fixture.workspace.get());
        auto *second = new GISVectorLayer(input, {}, fixture.workspace.get());
        auto *canvas = fixture.window->canvas();
        canvas->addLayer(first, false);
        canvas->addLayer(second, false);
        const auto attachment = layerState(fixture, first->layerId()).layerSessionId;
        const auto original = canvas->layers();
        const int from = original.indexOf(first), to = original.indexOf(second);
        QVERIFY(from >= 0 && to >= 0 && from != to);
        observe(fixture);
        canvas->moveLayer(from, to, true);
        QCOMPARE(canvas->layers().indexOf(first), to);
        QTRY_COMPARE(count(ViewEventKind::LayerOrderChanged), 1);
        const auto nativeOrder = canvas->layers();
        canvas->undoStack()->undo();
        QCOMPARE(canvas->layers(), original);
        auto move = command(fixture, ViewCommandKind::MoveLayer, attachment);
        move.index = to;
        QVERIFY2(fixture.controls()->executeViewCommand(move, &error), qPrintable(error));
        QCOMPARE(canvas->layers(), nativeOrder);
        const auto stable = fixture.snapshot().sequence;
        QVERIFY(fixture.controls()->executeViewCommand(move, &error));
        QCOMPARE(fixture.snapshot().sequence, stable);
        move.index = -1;
        QVERIFY(!fixture.controls()->executeViewCommand(move, &error));
        auto stale = command(fixture, ViewCommandKind::SetLayerVisible, attachment);
        stale.enabled = !first->isVisible();
        const auto generation = stale.loadGeneration;
        QVERIFY2(fixture.load(error), qPrintable(error));
        QVERIFY(fixture.snapshot().session.loadGeneration > generation);
        const bool wasVisible = first->isVisible();
        QVERIFY(!fixture.controls()->executeViewCommand(stale, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(first->isVisible(), wasVisible);
    }

    /** @brief Keep legacy boundary invalidation scoped to each current shared-source attachment. */
    void sharedBoundaryLegacyObserversDetachAndRebind()
    {
        const auto input = model();
        auto shared = std::make_unique<GISVectorLayer>(vectors());
        shared->setVisible(false);
        QList<QVariantMap> firstEvents, secondEvents;
        ViewFixture first(input, path("first-host.ini"));
        ViewFixture second(input, path("second-host.ini"));
        QString error;
        QVERIFY2(first.load(error), qPrintable(error));
        QVERIFY2(second.load(error), qPrintable(error));
        first.window->canvas()->addLayer(shared.get(), false);
        second.window->canvas()->addLayer(shared.get(), false);
        first.host->observe(this, [&](const QString &name, const QVariantMap &data) {
            if (name == "boundaryChanged")
                firstEvents.append(data);
        });
        second.host->observe(this, [&](const QString &name, const QVariantMap &data) {
            if (name == "boundaryChanged")
                secondEvents.append(data);
        });
        shared->setSelectedFeatureIds({0});
        QCOMPARE(firstEvents.size(), 1);
        QCOMPARE(secondEvents.size(), 1);
        QCOMPARE(firstEvents.last()["id"].toUuid(), first.instance()->id());
        QCOMPARE(secondEvents.last()["id"].toUuid(), second.instance()->id());
        shared->restoreLayerId("restored-shared-source");
        shared->setSelectedFeatureIds({1});
        QCOMPARE(firstEvents.last()["layerId"].toString(), shared->layerId());
        QCOMPARE(secondEvents.last()["layerId"].toString(), shared->layerId());
        auto *canvas = first.window->canvas();
        canvas->takeLayer(canvas->layers().indexOf(shared.get()), true);
        const auto detachedCount = firstEvents.size(), remainingCount = secondEvents.size();
        shared->setSelectedFeatureIds({2});
        QCOMPARE(firstEvents.size(), detachedCount);
        QCOMPARE(secondEvents.size(), remainingCount + 1);
        canvas->undoStack()->undo();
        shared->setSelectedFeatureIds({0});
        QCOMPARE(firstEvents.size(), detachedCount + 1);
        const auto closedCount = firstEvents.size();
        delete first.window.data();
        shared->setSelectedFeatureIds({1});
        QCOMPARE(firstEvents.size(), closedCount);
        second.window->canvas()->takeLayer(second.window->canvas()->layers().indexOf(shared.get()),
                                           false);
    }

    /** @brief Refuse success when a synchronous native observer replaces the command's generation. */
    void reentrantReloadInvalidatesViewCommandOutcome()
    {
        bool attempted = false, loadedAgain = false;
        QString loadError;
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *vector = new GISVectorLayer(vectors(), {}, fixture.workspace.get());
        fixture.window->canvas()->addLayer(vector, false);
        const auto attachment = layerState(fixture, vector->layerId()).layerSessionId;
        auto rename = command(fixture, ViewCommandKind::RenameLayer, attachment);
        rename.text = "Reload-triggering title";
        connect(vector, &OpenSWMMVisLayer::nameChanged, &fixture.main, [&] {
            if (!attempted) {
                attempted = true;
                loadedAgain = fixture.load(loadError);
            }
        });
        QVERIFY(!fixture.controls()->executeViewCommand(rename, &error));
        QVERIFY(attempted);
        QVERIFY2(loadedAgain, qPrintable(loadError));
        QVERIFY(!error.isEmpty());
        QVERIFY(fixture.snapshot().session.loadGeneration > rename.loadGeneration);
    }

    /** @brief Observe authored feature schema/content without treating every redraw as a data edit. */
    void editableFeatureSchemaAndGeometry()
    {
        using namespace openswmmvis::feature;
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        std::unique_ptr<SpatialReferenceSystem> crs(
            SpatialReferenceSystem::fromWktOrProj("EPSG:4326"));
        QVERIFY(crs);
        auto *editable = FeatureLayer::create(
            path("authored.gpkg"), "authored", GeometryType::Point, ZPolicy{}, {}, crs->toWkt(),
            FeatureLayerRole::General, &error, fixture.workspace.get());
        QVERIFY2(editable, qPrintable(error));
        fixture.window->canvas()->addLayer(editable, false);
        const auto attachment = layerState(fixture, editable->layerId()).layerSessionId;
        observe(fixture);
        QVERIFY2(editable->addField({"value", FieldType::Real, 0.}, &error), qPrintable(error));
        QTRY_COMPARE(count(ViewEventKind::LayerSchemaChanged, attachment), 1);
        Feature feature;
        feature.attributes["value"] = 2.;
        Ring ring;
        ring.pts = {{-76., 40.}};
        Part part;
        part.exterior = ring;
        feature.geometry = FeatureGeometry(GeometryType::Point);
        feature.geometry.addPart(part);
        const auto id = editable->addFeature(feature, &error);
        QVERIFY2(id >= 0, qPrintable(error));
        QTRY_VERIFY(count(ViewEventKind::LayerFeaturesChanged, attachment) >= 1);
        QCOMPARE(layerState(fixture, editable->layerId()).featureCount, qint64{1});
        mEvents.clear();
        editable->setSRS(SpatialReferenceSystem::fromWktOrProj("EPSG:3857", editable));
        QTRY_COMPARE(count(ViewEventKind::LayerCrsChanged, attachment), 1);
    }

    /** @brief Distinguish 1D empty/failure/ready epochs and publish only current cursor state. */
    void resultsLifecycleVariablesAndTime()
    {
        bool closeDuringSettlement = true;
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *missing = new SWMMResultsLayer(path("missing.out"), fixture.window->modelLayer(),
                                             fixture.workspace.get());
        fixture.window->canvas()->addLayer(missing, false);
        const auto missingId = layerState(fixture, missing->layerId()).layerSessionId;
        QCOMPARE(resultsState(fixture, missingId).availability, ResultAvailability::Empty);
        QList<QString> warnings, errors;
        QVERIFY(!missing->openResults(warnings, errors));
        const auto failed = resultsState(fixture, missingId);
        QCOMPARE(failed.availability, ResultAvailability::Failed);
        QVERIFY(!failed.error.isEmpty());
        QVERIFY(failed.sourceGeneration > 0);
        const auto output = path("run.out");
        QCOMPARE(swmm_engine_run(fixture.window->modelFilePath().toUtf8(), path("run.rpt").toUtf8(),
                                 output.toUtf8(), nullptr),
                 SWMM_OK);
        auto *results =
            new SWMMResultsLayer(output, fixture.window->modelLayer(), fixture.workspace.get());
        fixture.window->canvas()->addLayer(results, false);
        const auto attachment = layerState(fixture, results->layerId()).layerSessionId;
        warnings.clear();
        errors.clear();
        QVERIFY2(results->openResults(warnings, errors), qPrintable(errors.join(';')));
        auto ready = resultsState(fixture, attachment);
        QCOMPARE(ready.availability, ResultAvailability::Ready);
        QVERIFY(ready.periodCount > 1);
        observe(fixture);
        auto change = command(fixture, ViewCommandKind::ActivateResults, attachment);
        QVERIFY2(fixture.controls()->executeViewCommand(change, &error), qPrintable(error));
        QVERIFY(resultsState(fixture, attachment).active);
        change.kind = ViewCommandKind::SetResultsVariable;
        change.text = "NodeHead";
        QVERIFY2(fixture.controls()->executeViewCommand(change, &error), qPrintable(error));
        QTRY_COMPARE(count(ViewEventKind::ResultsVariableChanged, attachment), 1);
        ready = resultsState(fixture, attachment);
        QCOMPARE(ready.variables.first().variableKey, QStringLiteral("NodeHead"));
        const auto nativeUnits = ready.variables.first().units;
        QVERIFY(!nativeUnits.isEmpty());
        fixture.window->unitSystem()->setFlowUnits(swmm_CFS);
        QCOMPARE(resultsState(fixture, attachment).variables.first().units, nativeUnits);
        change.kind = ViewCommandKind::SetResultsTime;
        for (int ordinal = 0; ordinal < 30; ++ordinal) {
            change.index = ordinal % ready.periodCount;
            QVERIFY2(fixture.controls()->executeViewCommand(change, &error), qPrintable(error));
        }
        QTRY_COMPARE(count(ViewEventKind::ResultsTimeChanged, attachment), 1);
        QCOMPARE(resultsState(fixture, attachment).currentIndex, change.index);
        const auto stable = fixture.snapshot().sequence;
        QVERIFY(fixture.controls()->executeViewCommand(change, &error));
        QCOMPARE(fixture.snapshot().sequence, stable);
        change.index = ready.periodCount;
        QVERIFY(!fixture.controls()->executeViewCommand(change, &error));
        const auto generation = results->sourceGeneration();
        results->closeResults();
        QCOMPARE(resultsState(fixture, attachment).availability, ResultAvailability::Empty);
        QVERIFY(resultsState(fixture, attachment).sourceGeneration > generation);
        // A native observer may close the newly adopted reader during the final
        // source publication. The interrupted open must not return successful readiness.
        connect(results, &SWMMResultsLayer::sourceStateChanged, &fixture.main, [&] {
            if (closeDuringSettlement && !results->sourceLoading() && results->hasOpenResults()) {
                closeDuringSettlement = false;
                results->closeResults();
            }
        });
        warnings.clear();
        errors.clear();
        QVERIFY(!results->openResults(warnings, errors));
        QVERIFY(!closeDuringSettlement);
        QVERIFY(!results->sourceLoading());
        QVERIFY(!results->hasOpenResults());
        QCOMPARE(resultsState(fixture, attachment).availability, ResultAvailability::Empty);
        QCOMPARE(resultsState(fixture, attachment).currentIndex, qint64{-1});
        QList<ViewEvent> activeEvents;
        QObject activeObserver;
        QVERIFY(fixture.source()->observeViews(
            &activeObserver,
            {{ViewEventKind::ResultsActiveChanged}, fixture.window->extensionSessionId(), {}},
            [&](const ViewEvent &event) { activeEvents.append(event); }));
        QCOMPARE(fixture.window->activeResultsLayer(), results);
        auto *canvas = fixture.window->canvas();
        canvas->takeLayer(canvas->layers().indexOf(results), false);
        QTRY_COMPARE(activeEvents.size(), 1);
        QVERIFY(activeEvents.first().layerSessionId.isNull());
        QCOMPARE(activeEvents.first().sourceGeneration, quint64{0});
        QVERIFY(resultsState(fixture, attachment).layerSessionId.isNull());
    }

    /** @brief Prevent a queued 1D open from adopting a source after explicit close. */
    void closingQueuedResultsOpenSuppressesStaleReady()
    {
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        const auto output = path("run.out");
        QCOMPARE(swmm_engine_run(fixture.window->modelFilePath().toUtf8(), path("run.rpt").toUtf8(),
                                 output.toUtf8(), nullptr),
                 SWMM_OK);
        auto *results =
            new SWMMResultsLayer(output, fixture.window->modelLayer(), fixture.workspace.get());
        fixture.window->canvas()->addLayer(results, false);
        const auto attachment = layerState(fixture, results->layerId()).layerSessionId;
        observe(fixture);
        ViewWorkerBarrier barrier;
        results->openResultsAsync();
        QVERIFY(results->sourceLoading());
        const auto opening = results->sourceGeneration();
        results->closeResults();
        QVERIFY(results->sourceGeneration() > opening);
        barrier.release();
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        const auto state = resultsState(fixture, attachment);
        QCOMPARE(state.availability, ResultAvailability::Empty);
        QVERIFY(!results->hasOpenResults());
        QCOMPARE(state.currentIndex, qint64{-1});
    }

    /** @brief Keep an attached zero-period 2D source distinct from no source and guard detach epochs. */
    void zeroPeriodMeshSourceIsReady()
    {
        bool interruptAdoption = true;
        QList<ResultAvailability> delivered;
        ViewFixture fixture(model(), path("host.ini"));
        QString error;
        QVERIFY2(fixture.load(error), qPrintable(error));
        auto *results = new SWMM2DResultsLayer("Metadata source", fixture.workspace.get());
        fixture.window->canvas()->addLayer(results, false);
        const auto attachment = layerState(fixture, results->layerId()).layerSessionId;
        QCOMPARE(resultsState(fixture, attachment).availability, ResultAvailability::Empty);
        results->setSource(std::make_unique<ViewMeshSource>(0));
        const auto ready = resultsState(fixture, attachment);
        QCOMPARE(ready.availability, ResultAvailability::Ready);
        QCOMPARE(ready.periodCount, qint64{0});
        QCOMPARE(ready.currentIndex, qint64{-1});
        auto seek = command(fixture, ViewCommandKind::SetResultsTime, attachment);
        QVERIFY(!fixture.controls()->executeViewCommand(seek, &error));
        results->closeSource();
        const auto empty = resultsState(fixture, attachment);
        QCOMPARE(empty.availability, ResultAvailability::Empty);
        QVERIFY(empty.sourceGeneration > ready.sourceGeneration);
        observe(fixture);
        QVERIFY(fixture.source()->observeViews(
            &fixture.main,
            {{ViewEventKind::ResultsSourceChanged},
             fixture.window->extensionSessionId(),
             attachment},
            [&](const ViewEvent &) {
                delivered.append(resultsState(fixture, attachment).availability);
            }));
        connect(results, &SWMM2DResultsLayer::timeRangeChanged, &fixture.main, [&] {
            if (interruptAdoption) {
                interruptAdoption = false; // closeSource publishes the range again.
                results->closeSource();
            }
        });
        const auto beforeInterruptedOpen = results->sourceRevision();
        results->setSource(std::make_unique<ViewMeshSource>(0));
        QVERIFY(!interruptAdoption);
        QVERIFY(!results->source());
        QVERIFY(!results->sourceLoading());
        const auto interrupted = resultsState(fixture, attachment);
        QCOMPARE(interrupted.availability, ResultAvailability::Empty);
        QCOMPARE(interrupted.currentIndex, qint64{-1});
        QVERIFY(interrupted.sourceGeneration > beforeInterruptedOpen + 1);
        QCOMPARE(interrupted.sourceGeneration, results->sourceRevision());
        QTRY_COMPARE(delivered.size(), 1);
        QCOMPARE(delivered.first(), ResultAvailability::Empty);
    }
};

QTEST_MAIN(TestExtensionViewEvents)
#include "test_extensionviewevents.moc"
