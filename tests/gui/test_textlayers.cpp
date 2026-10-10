#include <QtTest>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QPainter>
#include <QTimer>
#include "layers/annotationfeatures.h"
#include "layers/annotationlayer.h"
#include "layers/annotationtextitem.h"
#include "layers/swmmmodellayer.h"
#include "layers/gisvectorlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/textlayercommands.h"
#include "map/tools/maptooladdtext.h"
#include "map/tools/maptoolselect.h"
#include "map/spatialreferencesystem.h"
#include "project/projectserializer.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/annotationstyledialog.h"
#include "ui/dialogs/textlayersdialog.h"

class TestTextLayers : public QObject
{
    Q_OBJECT
    QString output() const {
        return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "tests/gui/data")).absoluteFilePath("../../output/text_layers");
    }
    static AnnotationTextItem *add(OpenSWMMVisAnnotationLayer &layer, const QString &text, const QPointF &pos) {
        auto *item = new AnnotationTextItem; item->setText(text); item->setPosition(pos.x(),pos.y()); layer.addAnnotation(item); return item;
    }
    QImage render(const QList<OpenSWMMVisLayer *> &layers, const MapExtent &extent = MapExtent(0,0,100,100), const QSize &size = QSize(800,600)) {
        QImage image(size, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white); QPainter p(&image);
        OpenSWMMVisAnnotationLayer::paintLayers(p,layers,extent,nullptr,size); return image;
    }
    static void mouse(MapCanvas *canvas, QEvent::Type type, const QPointF &pixel) {
        const bool move = type == QEvent::MouseMove;
        QMouseEvent event(type, pixel, pixel, move ? Qt::NoButton : Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(canvas, &event);
    }
private slots:
    void initTestCase() { QVERIFY(QDir().mkpath(output())); }
    void layeredPersistenceAndLegacy() {
        SWMMVisProjectWindow original(nullptr);
        auto *a = original.ensureAnnotationLayer(); a->setName("Hydraulic notes"); a->setAutoLayout(true); a->setOpacity(0.6);
        auto *item = add(*a,"Critical location\nInspect inlet",{40,45}); item->setOffset({30,-20}); item->setPinned(true); item->setHaloEnabled(false);
        item->setFeatureReference({{"layerId",original.modelLayer()->layerId()},{"name","J1"},{"category",0}});
        item->setExpression("{name}: {invertElev}"); item->setCalloutEnabled(true);
        auto *b = new OpenSWMMVisAnnotationLayer("Design notes"); b->setVisible(false); original.canvas()->addLayer(b,false);
        QVERIFY(ProjectSerializer::saveToFile(output()+"/project.oswp", &original));
        QFile file(output()+"/project.oswp"); QVERIFY(file.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        const auto saved = root.value("sessions").toArray().first().toObject();
        QCOMPARE(saved.value("textLayers").toArray().size(),2);
        SWMMVisProjectWindow restored(nullptr);
        QVERIFY(ProjectSerializer::applyFromFile(output()+"/project.oswp", &restored));
        QVector<OpenSWMMVisAnnotationLayer *> layers;
        for (auto *base : restored.canvas()->layers()) if (auto *text=qobject_cast<OpenSWMMVisAnnotationLayer *>(base)) layers.append(text);
        QCOMPARE(layers.size(),2);
        QCOMPARE(layers[0]->layerJson(),a->layerJson()); QCOMPARE(layers[1]->layerJson(),b->layerJson());
        QCOMPARE(restored.modelLayer()->layerId(), original.modelLayer()->layerId());
        QCOMPARE(AnnotationFeatures::source(restored.canvas()->layers(), layers[0]->annotations()[0]->featureReference()), restored.modelLayer());
        SWMMVisProjectWindow legacy(nullptr);
        root["sessions"] = QJsonArray{QJsonObject{{"annotations", a->toJson()}}};
        QFile legacyFile(output()+"/legacy.oswp"); QVERIFY(legacyFile.open(QIODevice::WriteOnly));
        legacyFile.write(QJsonDocument(root).toJson()); legacyFile.close();
        QVERIFY(ProjectSerializer::applyFromFile(output()+"/legacy.oswp", &legacy));
        QCOMPARE(legacy.ensureAnnotationLayer()->annotations().size(),1);
    }
    void collisionPlacementAcrossLayers() {
        OpenSWMMVisAnnotationLayer a("Notes"), b("Generated");
        a.setAutoLayout(true); b.setAutoLayout(true);
        auto *fixed = add(a,"Pinned text",{50,50}); fixed->setPinned(true);
        QVector<AnnotationTextItem *> items{fixed};
        for (int i=0;i<14;++i) items.append(add(i%2?a:b,QString("Label %1").arg(i),{50,50}));
        const auto beforeA=a.toJson(), beforeB=b.toJson();
        auto image=render({&a,&b}); QVERIFY(image.save(output()+"/collision_layout.png"));
        QVector<QRectF> rects;
        for (auto *item:items) {
            auto *layer=qobject_cast<OpenSWMMVisAnnotationLayer *>(item->parent());
            const auto rect=layer->renderedBounds(item->id()); QVERIFY(!rect.isEmpty());
            for (const auto &other:rects) QVERIFY(!rect.intersects(other));
            rects.append(rect);
            QCOMPARE(layer->annotationAtPixel(rect.center()),item);
        }
        QCOMPARE(a.toJson(),beforeA); QCOMPARE(b.toJson(),beforeB);
        const auto fixedRect=a.renderedBounds(fixed->id());
        a.setAutoLayout(false); b.setVisible(false); render({&a,&b});
        QCOMPARE(a.renderedBounds(fixed->id()),fixedRect); QVERIFY(b.renderedBounds(items[1]->id()).isEmpty());
    }
    void hitTestAtDifferentZoomsAndHalos() {
        OpenSWMMVisAnnotationLayer a;
        auto *item=add(a,"Readable\nmultiline",{50,50}); item->setRotation(20); item->setBackgroundEnabled(true); item->setHaloColor(QColor(220,150,80));
        auto first=render({&a}); const auto r=a.renderedBounds(item->id()); QVERIFY(!r.isEmpty());
        QCOMPARE(a.annotationAtScenePos({r.center().x()/8, -(100-r.center().y()/6)}),item);
        auto second=render({&a},MapExtent(25,25,75,75)); QCOMPARE(a.renderedBounds(item->id()).size(),r.size());
        QCOMPARE(a.annotationAtPixel(a.renderedBounds(item->id()).center()),item);
        item->setHaloEnabled(false); auto plain=render({&a}); QVERIFY(plain!=first);
        QVERIFY(first.save(output()+"/halo_on.png")); QVERIFY(plain.save(output()+"/halo_off.png"));
        a.setOpacity(0); render({&a}); QVERIFY(!a.annotationAtPixel(r.center()));
    }
    void stagedEditsAndUndo() {
        OpenSWMMVisAnnotationLayer layer;
        auto *item=add(layer,"Original",{40,40}); const QString id=item->id();
        const auto before=layer.layerJson();
        {
            AnnotationStyleDialog dialog(item); auto *text=dialog.findChild<QPlainTextEdit *>("annotationText"); QVERIFY(text);
            dialog.show(); QApplication::processEvents(); QVERIFY(dialog.grab().save(output()+"/text_editor.png"));
            text->setPlainText("Cancelled"); dialog.reject(); QCOMPARE(item->text(),QString("Original"));
        }
        {
            AnnotationStyleDialog dialog(item); dialog.findChild<QPlainTextEdit *>("annotationText")->setPlainText("Applied\nSecond line");
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QCOMPARE(item->text(),QString("Applied\nSecond line"));
        }
        QUndoStack stack; stack.push(new EditTextLayerCommand(&layer,before,layer.layerJson(),"Edit"));
        stack.undo(); QCOMPARE(layer.annotation(id)->text(),QString("Original")); stack.redo(); QCOMPARE(layer.annotation(id)->text(),QString("Applied\nSecond line"));
    }
    void typedModelFeatureLinks() {
        const auto path=QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA","tests/gui/data")).filePath("typed_selection_fixture.inp");
        SWMMModelLayer model(path,nullptr); QList<QString> warnings, errors;
        QVERIFY2(model.loadModel(warnings,errors),qPrintable(errors.join('\n')));
        auto features=AnnotationFeatures::enumerate(&model); QVERIFY(!features.isEmpty());
        AnnotationFeatures::Feature node,link,gage,catchment;
        for (const auto &f:features) {
            int c=f.reference.value("category").toInt();
            if(f.name=="X1"&&c==SWMMModelLayer::CatJunctions) node=f;
            if(f.name=="X1"&&c==SWMMModelLayer::CatConduits) link=f;
            if(f.name=="S1"&&c==SWMMModelLayer::CatRainGages) gage=f;
            if(f.name=="S1"&&c==SWMMModelLayer::CatSubcatchments) catchment=f;
        }
        QVERIFY(!node.reference.isEmpty()); QVERIFY(!link.reference.isEmpty()); QVERIFY(node.position!=link.position);
        QVERIFY(!gage.reference.isEmpty()); QVERIFY(!catchment.reference.isEmpty()); QVERIFY(gage.position!=catchment.position);
        QVERIFY(AnnotationFeatures::evaluate("{name}: {invertElev}",node).startsWith("X1: 95"));
        OpenSWMMVisAnnotationLayer text;
        auto *item=add(text,"X1",node.position); item->setFeatureReference(node.reference); item->setExpression("{name}"); item->setCalloutEnabled(true); item->setOffset({35,-20});
        render({&model,&text},MapExtent(-100,-100,4000,4000)); auto rect=text.renderedBounds(item->id()); QVERIFY(!rect.isEmpty());
        QVERIFY(model.applyNodeMove(model.nodeIndex("X1"),node.position.x()+200,node.position.y()+100));
        render({&model,&text},MapExtent(-100,-100,4000,4000)); QVERIFY(text.renderedBounds(item->id()).center()!=rect.center());
        // Stable layer identity survives a display-name change and prevents wrong rebinding.
        const auto key=AnnotationFeatures::key(node.reference);
        model.setName("Renamed source"); AnnotationFeatures::Feature renamed; QVERIFY(AnnotationFeatures::resolve(&model,node.reference,renamed)); QCOMPARE(AnnotationFeatures::key(renamed.reference),key);
        QCOMPARE(AnnotationFeatures::source({&model},node.reference),&model);
        SWMMModelLayer other(QString(),nullptr); other.setName(node.reference.value("layerName").toString());
        QVERIFY(!AnnotationFeatures::source({&other},node.reference));
        auto withLine=render({&model,&text},MapExtent(-100,-100,4000,4000)); item->setCalloutEnabled(false);
        auto withoutLine=render({&model,&text},MapExtent(-100,-100,4000,4000)); QVERIFY(withLine!=withoutLine);
        QVERIFY(withLine.save(output()+"/feature_callout.png"));
    }
    void gisLinksAndMissingFeatures() {
        const auto path=output()+"/features.geojson"; QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"type":"FeatureCollection","features":[{"type":"Feature","id":42,"properties":{"label":"Inlet A","depth":1.25},"geometry":{"type":"Point","coordinates":[50,50]}}]})"); file.close();
        GISVectorLayer gis(path); const auto features=AnnotationFeatures::enumerate(&gis); QCOMPARE(features.size(),1);
        auto f=features.first(); QCOMPARE(f.reference.value("fid").toString(),QString("42")); QCOMPARE(AnnotationFeatures::evaluate("{label}: {depth}",f),QString("Inlet A: 1.25"));
        AnnotationFeatures::Feature resolved; QVERIFY(AnnotationFeatures::resolve(&gis,f.reference,resolved)); QCOMPARE(resolved.position,QPointF(50,50));
        OpenSWMMVisAnnotationLayer text; auto *item=add(text,"Fallback",{50,50}); item->setFeatureReference(f.reference); item->setExpression("{label}"); item->setOffset({20,-20});
        render({&gis,&text}); QVERIFY(!text.renderedBounds(item->id()).isEmpty()); render({&text}); QVERIFY(text.renderedBounds(item->id()).isEmpty());
        item->setExpression({}); render({&text}); QVERIFY(!text.renderedBounds(item->id()).isEmpty());
        SWMMVisProjectWindow project(nullptr); project.canvas()->addLayer(&gis,false);
        auto *savedText=project.ensureAnnotationLayer();
        auto *savedItem=add(*savedText,"Saved GIS",{50,50}); savedItem->setFeatureReference(f.reference); savedItem->setExpression("{label}");
        QVERIFY(ProjectSerializer::saveToFile(output()+"/gis.oswp", &project));
        QFile saved(output()+"/gis.oswp"); QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto root=QJsonDocument::fromJson(saved.readAll()).object();
        QCOMPARE(root.value("gisLayers").toArray().first().toObject().value("id").toString(),gis.layerId());
        SWMMVisProjectWindow reopened(nullptr); QVERIFY(ProjectSerializer::applyFromFile(output()+"/gis.oswp", &reopened));
        QTRY_VERIFY_WITH_TIMEOUT(AnnotationFeatures::source(reopened.canvas()->layers(),f.reference),5000);
        auto *reopenedText=reopened.ensureAnnotationLayer();
        QCOMPARE(reopenedText->annotations().first()->featureReference(),f.reference);
        project.canvas()->takeLayer(project.canvas()->layers().indexOf(&gis),false);
    }
    void generateThroughManager() {
        const auto path=output()+"/generate.geojson"; QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"type":"FeatureCollection","features":[{"type":"Feature","id":8,"properties":{"name":"Pump station"},"geometry":{"type":"Point","coordinates":[50,50]}}]})"); file.close();
        MapCanvas canvas;
        GISVectorLayer gis(path); auto cfg=gis.labelConfig(); cfg.enabled=true; gis.setLabelConfig(cfg);
        OpenSWMMVisAnnotationLayer text("Generated notes"); canvas.addLayer(&gis,false); canvas.addLayer(&text,false);
        TextLayersDialog manager(&canvas,&text);
        QTimer::singleShot(0, &manager, [] {
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (dialog) dialog->accept();
        });
        manager.findChild<QPushButton *>("generateTextLabels")->click();
        QCOMPARE(text.annotations().size(),1); QCOMPARE(text.annotations().first()->text(),QString("Pump station")); QVERIFY(text.autoLayout());
        QVERIFY(!text.annotations().first()->featureReference().isEmpty()); QVERIFY(text.annotations().first()->calloutEnabled());
        QVERIFY(!gis.labelConfig().enabled);
        canvas.undoStack()->undo(); QVERIFY(text.annotations().isEmpty()); QVERIFY(gis.labelConfig().enabled);
        canvas.undoStack()->redo(); QCOMPARE(text.annotations().size(),1); QVERIFY(!gis.labelConfig().enabled);
        canvas.takeLayer(canvas.layers().indexOf(&text),false); canvas.takeLayer(canvas.layers().indexOf(&gis),false);
    }
    void reprojectionAndUnresolvedEdit() {
        OpenSWMMVisAnnotationLayer text;
        text.setSRS(new SpatialReferenceSystem("EPSG",4326),true);
        SpatialReferenceSystem mercator("EPSG",3857);
        auto *item=add(text,"Projected note",{1,1});
        QImage image(800,600,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white); QPainter p(&image);
        OpenSWMMVisAnnotationLayer::paintLayers(p,{&text},MapExtent(0,0,222638.981586,222650.285733),&mercator,{800,600});
        QVERIFY(qAbs(text.renderedBounds(item->id()).left()-400)<8);
        QVERIFY(qAbs(text.renderedBounds(item->id()).bottom()-300)<8);
        p.end(); QVERIFY(image.save(output()+"/reprojected_text.png"));
        MapCanvas canvas;
        const auto path=QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA","tests/gui/data")).filePath("typed_selection_fixture.inp");
        SWMMModelLayer model(path,nullptr); QList<QString> warnings,errors; QVERIFY(model.loadModel(warnings,errors));
        canvas.addLayer(&model,false);
        auto ref=AnnotationFeatures::enumerate(&model).first().reference; ref["name"]="Deleted feature";
        item->setFeatureReference(ref); item->setExpression("{name}");
        AnnotationStyleDialog dialog(item,nullptr,&canvas);
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(item->featureReference(),ref); // Opening/applying must not bind to the first remaining feature.
        canvas.takeLayer(canvas.layers().indexOf(&model),false);
    }
    void selectClickEditing_data() {
        QTest::addColumn<bool>("generated");
        QTest::newRow("custom text") << false;
        QTest::newRow("generated label") << true;
    }
    void selectClickEditing() {
        QFETCH(bool, generated);
        SWMMVisProjectWindow project(nullptr);
        auto *canvas = project.canvas(); canvas->resize(800,600); canvas->setExtent(MapExtent(0,0,100,100),false);
        auto *lower = project.ensureAnnotationLayer();
        const auto lowerId = add(*lower,"Label",{50,50})->id();
        auto *upper = new OpenSWMMVisAnnotationLayer("Upper text"); upper->setParent(canvas); canvas->addLayer(upper,false);
        auto *item = add(*upper,"Label",{50,50}); const auto id = item->id();
        if (generated) {
            const auto path = output()+"/click_features.geojson"; QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(R"({"type":"FeatureCollection","features":[{"type":"Feature","id":1,"properties":{"name":"Label"},"geometry":{"type":"Point","coordinates":[50,50]}}]})"); file.close();
            auto *gis = new GISVectorLayer(path); gis->setParent(canvas); canvas->addLayer(gis,false);
            const auto features = AnnotationFeatures::enumerate(gis); QCOMPARE(features.size(),1);
            item->setFeatureReference(features.first().reference); item->setExpression("{name}");
        }
        const auto reference = item->featureReference();
        project.activateSelectTool();
        auto *select = canvas->activeTool();
        const auto refresh = [&] { render(canvas->layers(),canvas->extent(),canvas->size()); };
        refresh(); const auto pixel = upper->renderedBounds(id).center(); QVERIFY(!upper->renderedBounds(id).isEmpty());
        bool opened = false;
        bool apply = false;
        QTimer closeEditor;
        connect(&closeEditor, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<AnnotationStyleDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            opened = true;
            dialog->findChild<QPlainTextEdit *>("annotationText")->setPlainText("Edited on map");
            dialog->findChild<QLineEdit *>("annotationExpression")->clear();
            if (apply) dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            else dialog->reject();
        });
        closeEditor.start(0);
        const auto click = [&] {
            mouse(canvas,QEvent::MouseButtonPress,pixel); mouse(canvas,QEvent::MouseButtonRelease,pixel);
        };
        click(); QVERIFY(!opened); QCOMPARE(upper->annotation(id)->text(),QString("Label"));
        canvas->undoStack()->clear();
        project.setEditSessionActive(true);
        click(); QVERIFY(opened); QCOMPARE(upper->annotation(id)->text(),QString("Label")); QCOMPARE(canvas->undoStack()->count(),0);
        opened = false; apply = true;
        click(); QVERIFY(opened); QCOMPARE(upper->annotation(id)->text(),QString("Edited on map"));
        QCOMPARE(upper->annotation(id)->featureReference(),reference); QVERIFY(upper->annotation(id)->expression().isEmpty());
        QCOMPARE(lower->annotation(lowerId)->text(),QString("Label")); QCOMPARE(canvas->activeTool(),select);
        QCOMPARE(canvas->undoStack()->count(),1); QVERIFY(project.hasChanges());
        canvas->undoStack()->undo(); QCOMPARE(upper->annotation(id)->text(),QString("Label"));
        QCOMPARE(upper->annotation(id)->expression(),generated ? QString("{name}") : QString());
        canvas->undoStack()->redo(); QCOMPARE(upper->annotation(id)->text(),QString("Edited on map"));
        upper->setVisible(false); refresh(); opened = false;
        click(); QVERIFY(opened); QCOMPARE(lower->annotation(lowerId)->text(),QString("Edited on map"));
        lower->setOpacity(0); refresh(); opened = false;
        click(); QVERIFY(!opened);
    }
    void selectDragAndCancel() {
        SWMMVisProjectWindow project(nullptr);
        auto *canvas = project.canvas(); canvas->resize(800,600); canvas->setExtent(MapExtent(0,0,100,100),false);
        auto *layer = project.ensureAnnotationLayer(); const auto id = add(*layer,"Drag me",{50,50})->id();
        project.activateSelectTool(); project.setEditSessionActive(true);
        auto *select = canvas->activeTool();
        render(canvas->layers(),canvas->extent(),canvas->size());
        const auto start = layer->renderedBounds(id).center(), end = start+QPointF(70,30);
        const auto beginDrag = [&] {
            render(canvas->layers(),canvas->extent(),canvas->size());
            mouse(canvas,QEvent::MouseButtonPress,start); mouse(canvas,QEvent::MouseMove,end);
            QCOMPARE(layer->annotation(id)->offset(),QPointF(70,30));
        };
        beginDrag(); mouse(canvas,QEvent::MouseButtonRelease,end);
        QVERIFY(layer->annotation(id)->pinned()); QCOMPARE(canvas->undoStack()->count(),1);
        canvas->undoStack()->undo(); QVERIFY(!layer->annotation(id)->pinned()); QCOMPARE(layer->annotation(id)->offset(),QPointF());
        canvas->undoStack()->redo(); QCOMPARE(layer->annotation(id)->offset(),QPointF(70,30));
        canvas->undoStack()->undo(); canvas->undoStack()->clear();
        beginDrag();
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); QApplication::sendEvent(canvas,&escape);
        mouse(canvas,QEvent::MouseButtonRelease,end);
        QCOMPARE(layer->annotation(id)->offset(),QPointF()); QCOMPARE(canvas->undoStack()->count(),0); QCOMPARE(canvas->activeTool(),select);
        beginDrag(); project.setEditSessionActive(false); mouse(canvas,QEvent::MouseButtonRelease,end);
        QCOMPARE(layer->annotation(id)->offset(),QPointF()); QCOMPARE(canvas->undoStack()->count(),0);
        project.setEditSessionActive(true); beginDrag(); project.activatePanTool();
        QCOMPARE(layer->annotation(id)->offset(),QPointF()); QCOMPARE(canvas->undoStack()->count(),0);
    }
    void managerAndMapEditing() {
        MapCanvas canvas; canvas.resize(800,600); canvas.setExtent(MapExtent(0,0,100,100),false);
        OpenSWMMVisAnnotationLayer layer("Review notes"); canvas.addLayer(&layer,false);
        auto *item=add(layer,"Drag me",{50,50}); const auto id=item->id();
        render({&layer});
        TextLayersDialog manager(&canvas,&layer); manager.show(); QApplication::processEvents();
        QCOMPARE(manager.selectedLayer(),&layer); QVERIFY(manager.grab().save(output()+"/text_layers_manager.png")); manager.reject();
        OpenSWMMVisMapToolAddText tool(&canvas,&layer); canvas.setActiveTool(&tool);
        const auto start=layer.renderedBounds(id).center();
        QMouseEvent press(QEvent::MouseButtonPress,start,start,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier); tool.mousePressEvent(&press);
        QMouseEvent move(QEvent::MouseMove,start+QPointF(70,30),start+QPointF(70,30),Qt::NoButton,Qt::LeftButton,Qt::NoModifier); tool.mouseMoveEvent(&move);
        QMouseEvent release(QEvent::MouseButtonRelease,start+QPointF(70,30),start+QPointF(70,30),Qt::LeftButton,Qt::NoButton,Qt::NoModifier); tool.mouseReleaseEvent(&release);
        QVERIFY(layer.annotation(id)->pinned()); QCOMPARE(layer.annotation(id)->offset(),QPointF(70,30));
        canvas.undoStack()->undo(); QVERIFY(!layer.annotation(id)->pinned()); QCOMPARE(layer.annotation(id)->offset(),QPointF());
        canvas.undoStack()->redo(); QCOMPARE(layer.annotation(id)->offset(),QPointF(70,30));
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); tool.keyPressEvent(&escape); QVERIFY(!canvas.activeTool());
        canvas.takeLayer(canvas.layers().indexOf(&layer),false);
    }
};
QTEST_MAIN(TestTextLayers)
#include "test_textlayers.moc"
