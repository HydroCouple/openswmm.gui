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
#include "swmmvisprojectwindow.h"
#include "core/unitsystem.h"
#include "layers/featurelayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmresultslayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/spatialreferencesystem.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "selection/selectionmanager.h"
#include "ui/panels/swmmattributetablemodel.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
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
