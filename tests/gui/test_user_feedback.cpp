#include "core/preferencesmanager.h"
#include "connections/basemapconnection.h"
#include "connections/basemapconnectionstore.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include <openswmm/engine/openswmm_engine.h>
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/scalebarsettings.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmmlayeritem.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "render/renderers/categorizedrenderer.h"
#include "render/markersymbollayer.h"
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/dialogs/stylemanagerdialog.h"
#include "ui/util/fileopendialog.h"
#include "ui/dialogs/irendererpanel.h"
#include "plot/comparisonplotmodel.h"
#include "plot/irunlayer.h"
#include "ui/dialogs/comparisonplotdialog.h"
#include "ui/dialogs/preferencesdialog.h"
#include "ui/dialogs/dialoglayoutpersistence.h"
#include "ui/dialogs/dialoglayoutwatcher.h"
#include "ui/dialogs/addbasemapdialog.h"
#include "ui/dialogs/labelstab.h"
#include "ui/dialogs/objectdefaultspage.h"
#include "ui/toolbars/mesheditingtoolbar.h"
#include "ui/models/userflagsmodel.h"
#include "ui/widgets/labelconfigeditor.h"
#include <QApplication>
#include <QAbstractButton>
#include <QQmlEngine>
#include <QClipboard>
#include <QDockWidget>
#include <QFile>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QPainter>
#include <QMdiArea>
#include <QMenuBar>
#include <QMessageBox>
#include <QTimer>
#include <QScopeGuard>
#include <QTableView>
#include <QChart>
#include <QChartView>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QLineSeries>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QPersistentModelIndex>
#include <QSGGeometryNode>
#include <QStackedWidget>
#include <QTest>
#include <QTreeWidget>
#include <memory>

using namespace openswmmvis::plot;
using namespace openswmmvis::ui;

namespace {
class FeedbackRun : public IRunLayer {
public:
    QString scenarioName() const override { return "feedback"; }
    openswmmvis::plot::UnitSystem unitSystem() const override { return openswmmvis::plot::UnitSystem::SI; }
    double startDateJulian() const override { return 46023; }
    int periodCount() const override { return 3; }
    int reportStepSeconds() const override { return 60; }
    void getSeriesAt(const ObjectRef &, PlotAttribute, SeriesData &out) const override {
        out.ok=true; out.periodCount=3;
        out.timesJulian={46023,46023+1./1440,46023+2./1440}; out.values={0,1,2};
    }
};
class FeedbackRenderer : public SWMMLayerQSGRenderer {
public:
    using SWMMLayerQSGRenderer::updatePaintNode;
};
bool hasVertexColor(QSGNode *root, QColor color) {
    if (!root) return false;
    if (root->type()==QSGNode::GeometryNodeType) {
        auto *geometry=static_cast<QSGGeometryNode *>(root)->geometry();
        if (geometry->sizeOfVertex()==sizeof(QSGGeometry::ColoredPoint2D)) {
            const auto *vertices=geometry->vertexDataAsColoredPoint2D();
            for (int i=0;i<geometry->vertexCount();++i)
                if (vertices[i].r==color.red() && vertices[i].g==color.green()
                    && vertices[i].b==color.blue() && vertices[i].a==color.alpha()) return true;
        }
    }
    for (QSGNode *child=root->firstChild();child;child=child->nextSibling())
        if (hasVertexColor(child,color)) return true;
    return false;
}
QString artifactPath(const QString &name) {
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
        .absoluteFilePath("../../../workplans/artifacts/user_feedback/"+name);
}
QString fixture() {
    return artifactPath("model.inp");
}
}

class TestUserFeedback : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("openswmm-feedback-test");
        QCoreApplication::setApplicationName("batch-feedback");
        QSettings().clear();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM",1,0,"SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM",1,0,"SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM",1,0,"SWMM2DResultsQSGRenderer");
        QFile original(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA")).filePath("userflags_fixture.inp"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QVERIFY(QDir().mkpath(QFileInfo(fixture()).absolutePath()));
        QFile model(fixture()); QVERIFY(model.open(QIODevice::WriteOnly));
        QVERIFY(model.write(original.readAll()+"\n[COORDINATES]\nJ1 20 20\nO1 80 80\n[REPORT]\nNODES ALL\nLINKS ALL\n")>0);
    }
    void rapidSeriesToggleKeepsItemAndRestoresLine() {
        ComparisonPlotDialog dialog;
        RunSource source; source.layer=std::make_shared<FeedbackRun>(); source.label="feedback";
        const int run=dialog.model()->addRunSource(source);
        SeriesSpec spec; spec.runIndex=run; spec.objectRef=ObjectRef::forNode("J1");
        spec.attribute=PlotAttribute::NodeDepth;
        dialog.model()->addSeries(spec);
        auto *tree=dialog.findChild<QTreeWidget *>(); QVERIFY(tree);
        auto *item=tree->topLevelItem(0)->child(0); QVERIFY(item);
        const QPersistentModelIndex index=tree->indexFromItem(item);
        for (int i=0;i<100;++i) {
            item->setCheckState(0,Qt::Unchecked);
            QVERIFY(index.isValid());
            QCOMPARE(tree->topLevelItem(0)->child(0),item);
            QVERIFY(!dialog.model()->spec(0).style.showLine);
            item->setCheckState(0,Qt::Checked);
            QVERIFY(dialog.model()->spec(0).style.showLine);
        }
        auto *chart=dialog.findChild<QChartView *>()->chart(); QVERIFY(chart);
        QVERIFY(chart->series().first()->isVisible());
    }
    void explicitPreferencesCategorySurvivesLayoutRestore() {
        DialogLayoutWatcher watcher; qApp->installEventFilter(&watcher);
        {
            PreferencesDialog dialog; dialog.openAtCategory("Canvas & CRS"); dialog.show();
            QCoreApplication::processEvents(); dialog.hide();
        }
        PreferencesDialog dialog; dialog.openAtCategory("Keyboard"); dialog.show();
        QCoreApplication::processEvents();
        auto *list=dialog.findChild<QListWidget *>("categories"); QVERIFY(list);
        QCOMPARE(list->currentItem()->text(),QString("Keyboard"));
        dialog.hide(); qApp->removeEventFilter(&watcher);
    }
    void meshActionsHaveMenuLabels() {
        MeshEditingToolbar toolbar("Mesh");
        QVERIFY(!toolbar.findChild<QAction *>("actionMeshSelectVertex")->text().isEmpty());
        QVERIFY(!toolbar.findChild<QAction *>("actionMeshSelectEdge")->text().isEmpty());
    }
    void savedBuiltinBasemapAppearsOnce() {
        BasemapConnectionStore::instance()->saveXYZ(XYZConnection::builtins().first(),{});
        AddBasemapDialog dialog;
        const QString name=XYZConnection::builtins().first().name;
        int count=0;
        for (auto *combo:dialog.findChildren<QComboBox *>())
            for (int i=0;i<combo->count();++i) if(combo->itemText(i)==name) ++count;
        QCOMPARE(count,1);
    }
    void labelsTabExposesSharedExpressionEditor() {
        SWMMModelLayer layer(""); LabelsTab tab(&layer);
        auto *editor=tab.findChild<LabelConfigEditor *>(); QVERIFY(editor);
        auto cfg=layer.labelConfig(); cfg.enabled=true; cfg.expression="{name}: {maxDepth} m";
        editor->setConfig(cfg);
        QVERIFY(QMetaObject::invokeMethod(editor,"configChanged",Qt::DirectConnection,
            Q_ARG(OpenSWMM::Render::LabelConfig,cfg)));
        QCOMPARE(layer.labelConfig().expression,cfg.expression);
    }
    void objectDefaultsRemembersEditingUnitSet() {
        auto *prefs=PreferencesManager::instance();
        ObjectDefaultsPage page; page.loadFrom(prefs);
        auto *combo=page.findChild<QComboBox *>(); QVERIFY(combo);
        const int target=1-combo->currentIndex(); combo->setCurrentIndex(target); page.applyTo(prefs);
        ObjectDefaultsPage reopened; reopened.loadFrom(prefs);
        QCOMPARE(reopened.findChild<QComboBox *>()->currentIndex(),target);
    }
    void globalConduitPenUpdatesInheritedLayerStyle() {
        auto *prefs=PreferencesManager::instance(); QPen original=prefs->linkPen("conduit");
        const auto restore=qScopeGuard([&]{prefs->setLinkPen("conduit",original);});
        SWMMModelLayer inherited(""); SWMMModelLayer customized("");
        auto symbol=customized.conduitSymbol(); symbol.size=7; customized.setConduitSymbol(symbol);
        QPen next=original; next.setWidthF(original.widthF()+2); prefs->setLinkPen("conduit",next);
        QCOMPARE(inherited.conduitSymbol().size,next.widthF());
        QCOMPARE(customized.conduitSymbol().size,7.);
        prefs->setLinkPen("conduit",original);
    }
    void userFlagsAreThemeableAndChangesInvalidate() {
        SWMMModelLayer layer(fixture()); QList<QString> warnings,errors;
        QVERIFY(layer.loadModel(warnings,errors));
        bool found=false;
        for (const auto &field:layer.availableAttributes(SWMMModelLayer::CatJunctions))
            if (field.name=="uf:PRIORITY") found=true;
        QVERIFY(found);
        QCOMPARE(layer.identifyByName("J1").value("uf:PRIORITY").toInt(),-5);
        auto *flags=layer.ensureUserFlagsModel(); QVERIFY(flags);
        const auto revision=layer.editRevision();
        auto renderer=std::make_unique<OpenSWMM::Render::CategorizedRenderer>();
        renderer->setClassifyAttribute("uf:PRIORITY");
        OpenSWMM::Render::MarkerSymbolLayerSpec marker;
        marker.fillColor=Qt::red;
        OpenSWMM::Render::SymbolStyle red; red.layers.append(marker.toSymbolLayer());
        marker.fillColor=Qt::green;
        OpenSWMM::Render::SymbolStyle green; green.layers.append(marker.toSymbolLayer());
        renderer->addCategory({"-5","Old",red}); renderer->addCategory({"8","New",green});
        layer.setKindRenderer(SWMMModelLayer::CatJunctions,std::move(renderer));
        QCOMPARE(layer.featureColor(SWMMModelLayer::CatJunctions,0),QColor(Qt::red));
        QVERIFY(flags->setValue("NODE","J1","PRIORITY","8"));
        QVERIFY(layer.editRevision()>revision);
        QCOMPARE(layer.identifyByName("J1").value("uf:PRIORITY").toInt(),8);
        QCoreApplication::processEvents();
        QCOMPARE(layer.featureColor(SWMMModelLayer::CatJunctions,0),QColor(Qt::green));
    }
    void categorizedPickerUsesUserFlagKey() {
        SWMMModelLayer layer(fixture()); QList<QString> warnings,errors;
        QVERIFY(layer.loadModel(warnings,errors));
        layer.setKindRenderer(SWMMModelLayer::CatJunctions,
            std::make_unique<OpenSWMM::Render::CategorizedRenderer>());
        const auto context=RendererPanelContext::resolve(&layer,SWMMModelLayer::CatJunctions);
        const auto *entry=RendererPanelRegistry::instance().find("categorized"); QVERIFY(entry);
        std::unique_ptr<IRendererPanel> panel(entry->factory(context,nullptr)); QVERIFY(panel);
        QComboBox *picker=nullptr;
        for(auto *combo:panel->findChildren<QComboBox *>())
            if(combo->findData("uf:PRIORITY")>=0){picker=combo;break;}
        QVERIFY(picker); picker->setCurrentIndex(picker->findData("uf:PRIORITY"));
        auto *renderer=dynamic_cast<OpenSWMM::Render::CategorizedRenderer *>(
            layer.kindRenderer(SWMMModelLayer::CatJunctions)); QVERIFY(renderer);
        QCOMPARE(renderer->classifyAttribute(),QString("uf:PRIORITY"));
        panel->refreshFromModel();
        QCOMPARE(picker->currentData().toString(),QString("uf:PRIORITY"));
    }
    void fieldPickersStoreCanonicalKeys() {
        SWMMModelLayer layer(fixture()); QList<QString> warnings,errors;
        QVERIFY(layer.loadModel(warnings,errors)); LabelsTab tab(&layer);
        auto *editor=tab.findChild<LabelConfigEditor *>(); QVERIFY(editor);
        QComboBox *field=nullptr;
        for(auto *combo:editor->findChildren<QComboBox *>())
            if(combo->findData("maxDepth")>=0){field=combo;break;}
        QVERIFY(field); field->setCurrentIndex(field->findData("maxDepth"));
        QCOMPARE(layer.labelConfig().fieldName,QString("maxDepth"));
        editor->setAvailableFields(layer.availableAttributes(SWMMModelLayer::CatJunctions));
        QCOMPARE(editor->config().fieldName,QString("maxDepth"));
        field->setEditText("legacy_field");
        QCOMPARE(editor->config().fieldName,QString("legacy_field"));
    }
    void resultsLabelsUseFieldAndExpression() {
        const QString out=artifactPath("run.out");
        const QString rpt=artifactPath("run.rpt");
        QCOMPARE(swmm_engine_run(fixture().toUtf8(),rpt.toUtf8(),out.toUtf8(),nullptr),SWMM_OK);
        SWMMModelLayer model(fixture());QList<QString> warnings,errors;
        QVERIFY(model.loadModel(warnings,errors));
        SWMMResultsLayer results(out,&model);QVERIFY(results.openResults(warnings,errors));
        QGraphicsScene scene;
        auto config=results.labelConfig();config.enabled=true;config.fieldName="depth";
        results.setLabelConfig(config);
        results.populateScene(&scene,MapExtent(0,0,100,100),model.srs());
        const auto texts=[&]{QStringList labels;
            for(auto *item:scene.items())
                if(auto *label=dynamic_cast<QGraphicsSimpleTextItem *>(item))
                    if(label->isVisible())labels.append(label->text());
            return labels;
        };
        QVERIFY2(texts().contains("0"),qPrintable(QString("Labels: [%1]; scene items: %2; visible: %3").arg(texts().join(", ")).arg(scene.items().size()).arg(results.isVisible()))); QVERIFY(!texts().contains("J1"));
        config.fieldName="head"; results.setLabelConfig(config);
        results.refreshScene(&scene,MapExtent(0,0,100,100),model.srs());
        QVERIFY(texts().contains("100"));
        config.expression="{name}: {head} ft";results.setLabelConfig(config);
        results.refreshScene(&scene,MapExtent(0,0,100,100),model.srs());
        QVERIFY(texts().contains("J1: 100 ft"));
        results.depopulateScene(&scene);
    }
    void preferredFolderOverridesOnlyModelHistory() {
        auto *prefs=PreferencesManager::instance();
        const QString preferred=QDir::current().absolutePath();
        const QString fallback=QDir::tempPath();
        prefs->setProjectOpenDirectory(preferred);
        QCOMPARE(FileOpenDialog::startDirectory("models",fallback),preferred);
        QCOMPARE(FileOpenDialog::startDirectory("vector",fallback),fallback);
        prefs->setProjectOpenDirectory({});
        QCOMPARE(FileOpenDialog::startDirectory("models",fallback),fallback);
    }
    void scaleBarUsesRoundDisplayUnits() {
        ScaleBarSettings settings; settings.setUnits(ScaleBarSettings::Feet);
        QCOMPARE(settings.niceMetres(400),1000*.3048);
        QCOMPARE(settings.formatLabel(settings.niceMetres(400)),QString("1000 ft"));
        settings.setUnits(ScaleBarSettings::Miles);
        QCOMPARE(settings.niceMetres(4000),2*1609.344);
        QCOMPARE(settings.formatLabel(settings.niceMetres(4000)),QString("2.00 mi"));
        QCOMPARE(settings.niceMetres(4000,true),2000.);
        settings.setUnits(ScaleBarSettings::Meters);
        QCOMPARE(settings.niceMetres(4000),2000.);
        QCOMPARE(settings.niceMetres(0),0.);
    }
    void sceneBufferRenderingHonorsLabelScale() {
        MapCanvas canvas; canvas.resize(800,600); canvas.setExtent(MapExtent(0,0,100,100),false);
        SWMMModelLayer layer(fixture()); QList<QString> warnings,errors;
        QVERIFY(layer.loadModel(warnings,errors));
        QGraphicsScene scene(&canvas); scene.addItem(new SWMMLayerItem(&layer));
        auto config=layer.labelConfig(); config.enabled=true; config.haloEnabled=false;
        config.color=QColor(231,17,109); config.fontSizePt=14;
        const auto render=[&]{
            QImage image(800,600,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
            QPainter painter(&image); scene.render(&painter,QRectF(0,0,800,600),QRectF(0,-100,100,100));
            return image;
        };
        layer.setLabelConfig(config); const QImage visible=render();
        const double scale=canvas.scaleDenominator(); QVERIFY(scale>0);
        config.maxScale=scale*2; layer.setLabelConfig(config); const QImage hidden=render();
        QVERIFY(visible!=hidden);
        config.enabled=false; layer.setLabelConfig(config); QCOMPARE(render(),hidden);
        QVERIFY(visible.save(artifactPath("labels-visible.png")));
        QVERIFY(hidden.save(artifactPath("labels-hidden.png")));
    }
    void menuCopyUsesFloatingTableFocus() {
        SWMMVis window; window.resize(1000,700); window.show();
        SWMMModelLayer layer(fixture()); QList<QString> warnings,errors;
        QVERIFY(layer.loadModel(warnings,errors));
        auto *dock=new QDockWidget("Copy regression",&window);
        auto *panel=new AttributeTablePanel(dock); dock->setWidget(panel);
        auto detach=qScopeGuard([panel]{panel->setProject(nullptr,nullptr);});
        panel->setProject(&layer,nullptr); window.addDockWidget(Qt::BottomDockWidgetArea,dock);
        dock->setFloating(true); dock->show(); dock->activateWindow();
        auto *table=panel->findChild<QTableView *>(); QVERIFY(table); table->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(),table);
        const QString expected=panel->selectionAsTsv(); QVERIFY(!expected.isEmpty());
        auto *restoredMapFocus=new QLineEdit(&window); restoredMapFocus->show();
        window.activateWindow(); restoredMapFocus->setFocus(Qt::ActiveWindowFocusReason);
        QCoreApplication::processEvents();
        window.menuBar()->setFocusPolicy(Qt::StrongFocus);window.menuBar()->setFocus();
        QGuiApplication::clipboard()->setText("sentinel");
        QVERIFY(QMetaObject::invokeMethod(&window,"onCopyActiveView",Qt::DirectConnection));
        QCOMPARE(QGuiApplication::clipboard()->text(),expected);
        QTest::mouseClick(restoredMapFocus,Qt::LeftButton);
        QGuiApplication::clipboard()->setText("map target");
        QVERIFY(QMetaObject::invokeMethod(&window,"onCopyActiveView",Qt::DirectConnection));
        QCOMPARE(QGuiApplication::clipboard()->text(),QString("map target"));
        dock->activateWindow(); table->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(),table);
        dock->hide(); window.activateWindow();
        restoredMapFocus->setFocus(Qt::ActiveWindowFocusReason);
        window.menuBar()->setFocus();
        QGuiApplication::clipboard()->setText("closed table");
        QVERIFY(QMetaObject::invokeMethod(&window,"onCopyActiveView",Qt::DirectConnection));
        QCOMPARE(QGuiApplication::clipboard()->text(),QString("closed table"));
    }
    void importsRequireProjectAndClosingActivatesSurvivor() {
        SWMMVis window; window.resize(1000,700);window.show();
        QTimer discard; discard.setInterval(10);
        connect(&discard,&QTimer::timeout,&window,[]{
            if(auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                if(auto *button=box->button(QMessageBox::Discard))button->click();
        });discard.start();
        auto *import=window.findChild<QAction *>("actionAddSWMMOutput"); QVERIFY(import);
        QVERIFY(!import->isEnabled());
        auto *delimited=window.findChild<QAction *>("actionAddDelimeteredData"); QVERIFY(delimited);
        QVERIFY(!delimited->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(&window,"onNewProject",Qt::DirectConnection));
        auto projects=window.findChildren<SWMMVisProjectWindow *>(); QCOMPARE(projects.size(),1);
        QPointer<SWMMVisProjectWindow> first=projects.first();
        QTRY_VERIFY(first->modelLayer()->engine()); QVERIFY(import->isEnabled());
        QVERIFY(delimited->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(&window,"onNewProject",Qt::DirectConnection));
        projects=window.findChildren<SWMMVisProjectWindow *>(); QCOMPARE(projects.size(),2);
        auto *second=projects[0]==first?projects[1]:projects[0];
        QTRY_VERIFY(second->modelLayer()->engine());second->setHasChanges(false);second->close();
        auto *mdi=window.findChild<QMdiArea *>();QVERIFY(mdi);
        QTRY_COMPARE(mdi->activeSubWindow(),static_cast<QMdiSubWindow *>(first));
        first->setHasChanges(false);first->close();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QTRY_VERIFY(!import->isEnabled());
        QVERIFY(!delimited->isEnabled());
    }
    void styleManagerCanRetargetAndSurviveLayerDeletion() {
        auto *first=new SWMMModelLayer(""); SWMMModelLayer second("");
        StyleManagerDialog dialog(first);dialog.show();QVERIFY(!dialog.isModal());
        dialog.setLayer(&second);delete first;dialog.setLayer(nullptr);
        QVERIFY(dialog.isVisible());dialog.hide();
    }
    void qsgNodesHonorOutlineColor() {
        MapCanvas canvas; canvas.resize(800,600);
        auto *layer=new SWMMModelLayer(fixture()); QList<QString> warnings,errors;
        QVERIFY(layer->loadModel(warnings,errors)); canvas.addLayer(layer,false);
        layer->setGlRenderingEnabled(true);
        auto symbol=layer->junctionSymbol(); symbol.outlineColor=QColor(231,17,109); symbol.outlineWidth=3;
        layer->setJunctionSymbol(symbol);
        FeedbackRenderer renderer; renderer.setWidth(800); renderer.setHeight(600);
        renderer.setLayer(layer); renderer.setMapExtent(canvas.extent());
        std::unique_ptr<QSGNode> root(renderer.updatePaintNode(nullptr,nullptr));
        QVERIFY(root); QVERIFY(hasVertexColor(root.get(),symbol.outlineColor));
        symbol.outlineWidth=0; layer->setJunctionSymbol(symbol);
        root.reset(renderer.updatePaintNode(root.release(),nullptr));
        QVERIFY(!hasVertexColor(root.get(),symbol.outlineColor));
    }
};

int main(int argc,char **argv) {
    QApplication app(argc,argv);
    const QString settingsDir=artifactPath("settings");
    QDir().mkpath(settingsDir); QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsDir);
    TestUserFeedback test; return QTest::qExec(&test,argc,argv);
}
#include "test_user_feedback.moc"
