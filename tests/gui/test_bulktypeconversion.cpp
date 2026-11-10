// SPDX-License-Identifier: GPL-3.0-or-later
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "map/spatialreferencesystem.h"
#include <QQmlEngine>
#include "map/tools/maptoolselect.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "ui/panels/attributetablepanel.h"
#include <openswmm/engine/openswmm_edit.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <QAbstractButton>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace
{
// Drive the real context menus and dialogs. The timer closes any unexpected menu
// so a changed label or missing action produces an assertion instead of a hang.
struct Interaction {
    QString prompt, report, details;
    int target;
    bool accept;
    void schedule()
    {
        QTimer::singleShot(0, qApp, [this] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) {
                for (auto *w : QApplication::topLevelWidgets())
                    if (w->isVisible() && qobject_cast<QMenu *>(w))
                        menu = qobject_cast<QMenu *>(w);
            }
            if (!menu)
                return;
            for (auto *parentAction : menu->actions()) {
                auto *choices = parentAction->menu();
                if (!choices || (!parentAction->text().startsWith("Convert") &&
                                 !parentAction->text().startsWith("Change Type")))
                    continue;
                QMetaObject::invokeMethod(choices, "aboutToShow", Qt::DirectConnection);
                for (auto *a : choices->actions()) {
                    if (!a->data().isValid() || a->data().toInt() != target || !a->isEnabled())
                        continue;
                    QTimer::singleShot(0, qApp, [this] {
                        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                        if (!box)
                            return;
                        prompt = box->text();
                        if (accept)
                            QTimer::singleShot(0, qApp, [this] {
                                auto *result =
                                    qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                                if (result) {
                                    report = result->text();
                                    details = result->detailedText();
                                    result->accept();
                                }
                            });
                        if (auto *button = box->button(accept ? QMessageBox::Yes : QMessageBox::No))
                            button->click();
                        else
                            box->reject();
                    });
                    a->trigger();
                    break;
                }
                break;
            }
            menu->close();
        });
    }
};

int kind(SWMM_Engine e, bool nodes, const char *name)
{
    int value = -1;
    if (nodes)
        swmm_node_get_type(e, swmm_node_index(e, name), &value);
    else
        swmm_link_get_type(e, swmm_link_index(e, name), &value);
    return value;
}
}

class BulkTypeConversionTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0, "SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DResultsQSGRenderer");
    }

    void realSelectionActions()
    {
        QTemporaryDir dir;
        QFile deck(dir.filePath("selection.inp"));
        QVERIFY(deck.open(QIODevice::WriteOnly));
        deck.write("[OPTIONS]\nFLOW_UNITS CMS\nFLOW_ROUTING DYNWAVE\n"
                   "[JUNCTIONS]\nA 1 2\nB 1 2\nC 1 2\nD 1 2\n"
                   "[OUTFALLS]\nO 0 FREE NO\n"
                   "[CONDUITS]\nAB A B 100 .013 0 0\nBC B C 100 .013 0 0\nD D O 100 .013 0 0\n"
                   "[XSECTIONS]\nAB CIRCULAR 1\nBC CIRCULAR 1\nD CIRCULAR 1\n"
                   "[COORDINATES]\nA 0 10\nB 100 10\nC 200 10\nD 0 100\nO 100 100\n"
                   "[VERTICES]\nD 50 102\n");
        deck.close();
        std::unique_ptr<OpenSWMMVisWorkspace> ws(OpenSWMMVisWorkspace::newInstance({}, nullptr));
        SWMMVisProjectWindow window(ws.get(), deck.fileName());
        QStringList warnings, errors;
        QVERIFY2(window.loadModel(warnings, errors), qPrintable(errors.join(" ")));
        window.resize(800, 600);
        window.show();
        auto *layer = window.modelLayer();
        auto *canvas = window.canvas();
        layer->setSRS(new SpatialReferenceSystem("EPSG", 3857), true);
        QVERIFY(canvas->setCanvasSRSByCode("EPSG", 3857));
        auto *bus = window.selectionManager();
        auto eng = layer->engine();
        QVERIFY(layer->applySetVirtual("B", true));
        window.setHasChanges(false);
        const auto geomRevision = layer->geomRevision();
        QSignalSpy types(layer, &SWMMModelLayer::typesChanged);
        QSignalSpy refresh(layer, &SWMMModelLayer::geometryChanged);
        QSignalSpy edited(layer, &SWMMModelLayer::modelEdited);

        // A refusal, a stale ID, a duplicate and the node sharing a link ID
        // must never change the model or create a dirty notification.
        TypeConversionRequest blocked;
        blocked.family = SWMMObjectRef::Link;
        blocked.target = SWMM_LINK_PUMP;
        blocked.objects = {{SWMMObjectRef::Link, "AB"},
                           {SWMMObjectRef::Link, "missing"},
                           {SWMMObjectRef::Link, "AB"},
                           {SWMMObjectRef::Node, "D"}};
        const auto skipped = layer->convertTypes(blocked, true);
        QCOMPARE(skipped.size(), 3);
        for (const auto &row : skipped) {
            QCOMPARE(row.state, TypeConversionOutcome::Skipped);
            QVERIFY(!row.reason.isEmpty());
        }
        QVERIFY(!window.hasChanges());
        QCOMPARE(types.size(), 0);
        QCOMPARE(refresh.size(), 0);
        QCOMPARE(edited.size(), 0);
        bus->select(QSet<SWMMObjectRef>{
            {SWMMObjectRef::Link, "AB"}, {SWMMObjectRef::Link, "D"}, {SWMMObjectRef::Node, "D"}});
        canvas->setExtent(MapExtent(-20, -20, 230, 150), false);
        QApplication::processEvents();
        int x = 0, y = 0;
        QCOMPARE(layer->pickAt(50, 10, 2).name, QString("AB"));
        canvas->toPixelCoords(50, 10, x, y);
        // Right-click the blocked conduit first. An independent selected link enables the target.
        QMouseEvent click(QEvent::MouseButtonPress, QPointF(x, y),
                          QPointF(canvas->mapToGlobal(QPoint(x, y))), Qt::RightButton,
                          Qt::RightButton, Qt::NoModifier);
        Interaction map{{}, {}, {}, SWMM_LINK_WEIR, true};
        map.schedule();
        window.selectTool()->mousePressEvent(&click);
        QVERIFY2(map.report.contains("1 converted") && map.report.contains("2 skipped"),
                 qPrintable(map.report));
        QVERIFY(map.details.contains("AB") && map.details.contains("609") &&
                map.details.contains("B"));
        QCOMPARE(kind(eng, false, "AB"), int(SWMM_LINK_CONDUIT));
        QCOMPARE(kind(eng, false, "D"), int(SWMM_LINK_WEIR));
        QCOMPARE(kind(eng, true, "D"), int(SWMM_NODE_JUNCTION));
        QVERIFY(window.hasChanges());
        QCOMPARE(layer->geomRevision(), geomRevision);
        QCOMPARE(types.size(), 1);
        QCOMPARE(refresh.size(), 1);
        QCOMPARE(edited.size(), 1);

        AttributeTablePanel table;
        table.setProject(layer, bus, canvas);
        table.showModelCategory(layer, SWMMModelLayer::CatJunctions);
        table.resize(640, 480);
        table.show();
        auto *view = table.findChild<QTableView *>();
        QVERIFY(view);
        view->sortByColumn(0, Qt::DescendingOrder);
        bus->select(QSet<SWMMObjectRef>{{SWMMObjectRef::Node, "A"}, {SWMMObjectRef::Node, "D"}});
        auto *edit = table.findChild<QLineEdit *>("attributeQuery");
        QVERIFY(edit);
        edit->setText("Name = 'D'");
        QVERIFY(QMetaObject::invokeMethod(&table, "onQueryApplyClicked"));
        QApplication::processEvents();
        QCOMPARE(view->model()->rowCount(), 1);
        // Replace-mode query Apply selects matches. Add the hidden A from the map,
        // which leaves one visible row and two selected rows in this category.
        bus->select({SWMMObjectRef::Node, "A"}, SelectionManager::Add);
        QCOMPARE(bus->size(), 2);
        const QPoint selected = view->visualRect(view->model()->index(0, 0)).center();
        auto tableAction = [&](bool accept) {
            Interaction interaction{{}, {}, {}, SWMM_NODE_STORAGE, accept};
            interaction.schedule();
            QMetaObject::invokeMethod(&table, "onContextMenuRequested", Q_ARG(QPoint, selected));
            return interaction;
        };
        auto cancelled = tableAction(false);
        QVERIFY2(cancelled.prompt.contains("2 eligible") &&
                     cancelled.prompt.contains("hidden by the filter: 1"),
                 qPrintable(cancelled.prompt));
        QCOMPARE(types.size(), 1);
        auto applied = tableAction(true);
        QVERIFY2(applied.report.contains("2 converted"), qPrintable(applied.report));
        QCOMPARE(kind(eng, true, "A"), int(SWMM_NODE_STORAGE));
        QCOMPARE(kind(eng, true, "D"), int(SWMM_NODE_STORAGE));
        QCOMPARE(kind(eng, true, "C"), int(SWMM_NODE_JUNCTION));
        QCOMPARE(types.size(), 2);
        QCOMPARE(refresh.size(), 2);
        QCOMPARE(edited.size(), 2);

        // A click outside the selection has single-object scope.
        canvas->toPixelCoords(200, 10, x, y);
        QMouseEvent outside(QEvent::MouseButtonPress, QPointF(x, y),
                            QPointF(canvas->mapToGlobal(QPoint(x, y))), Qt::RightButton,
                            Qt::RightButton, Qt::NoModifier);
        Interaction single{{}, {}, {}, SWMM_NODE_STORAGE, true};
        single.schedule();
        window.selectTool()->mousePressEvent(&outside);
        QVERIFY2(single.report.contains("1 converted"), qPrintable(single.report));
        QCOMPARE(kind(eng, true, "A"), int(SWMM_NODE_STORAGE));
        QCOMPARE(kind(eng, true, "D"), int(SWMM_NODE_STORAGE));
        QCOMPARE(kind(eng, true, "C"), int(SWMM_NODE_STORAGE));

        QString path = dir.filePath("saved.inp");
        QCOMPARE(swmm_model_write(eng, path.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(path);
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(" ")));
        QCOMPARE(kind(reopened.engine(), true, "D"), int(SWMM_NODE_STORAGE));
        QCOMPARE(kind(reopened.engine(), false, "D"), int(SWMM_LINK_WEIR));
        int rule = -1, vj = 0;
        QCOMPARE(swmm_node_virtual_eligible(reopened.engine(), reopened.nodeIndex("B"), &rule),
                 SWMM_OK);
        QCOMPARE(rule, 0);
        QCOMPARE(swmm_node_is_virtual(reopened.engine(), reopened.nodeIndex("B"), &vj), SWMM_OK);
        QCOMPARE(vj, 1);
    }

    void largeSelection_data()
    {
        QTest::addColumn<int>("size");
        for (int count : {100, 1000, 10000})
            QTest::newRow(qPrintable(QString::number(count))) << count;
    }
    void largeSelection()
    {
        QFETCH(int, size);
        SWMMModelLayer layer(QString{});
        auto eng = swmm_engine_new();
        QVERIFY(eng);
        TypeConversionRequest request;
        request.family = SWMMObjectRef::Node;
        request.target = SWMM_NODE_STORAGE;
        for (int n = 0; n < size; ++n) {
            auto name = QString("J%1").arg(n);
            QCOMPARE(swmm_node_add(eng, name.toUtf8().constData(), SWMM_NODE_JUNCTION), SWMM_OK);
            request.objects.append({SWMMObjectRef::Node, name});
        }
        layer.buildFromEngine(eng);
        QStringList warnings, errors;
        QVERIFY(layer.adoptOpenEngine(eng, warnings, errors));
        QSignalSpy refreshed(&layer, &SWMMModelLayer::geometryChanged);
        QSignalSpy repaint(&layer, &SWMMModelLayer::repaintRequested);
        auto revision = layer.geomRevision();
        QElapsedTimer timer;
        timer.start();
        auto preview = layer.convertTypes(request, false);
        auto previewNs = timer.nsecsElapsed();
        timer.restart();
        auto applied = layer.convertTypes(request, true);
        auto applyNs = timer.nsecsElapsed();
        QCOMPARE(preview.size(), size);
        QCOMPARE(applied.size(), size);
        for (int i = 0; i < size; ++i) {
            QCOMPARE(preview[i].state, TypeConversionOutcome::Eligible);
            QCOMPARE(applied[i].state, TypeConversionOutcome::Converted);
        }
        QCOMPARE(layer.categoryCount(SWMMModelLayer::CatStorage), size);
        QCOMPARE(refreshed.size(), 1);
        auto noop = layer.convertTypes(request, true);
        QCOMPARE(noop.last().state, TypeConversionOutcome::AlreadyTarget);
        QCOMPARE(refreshed.size(), 1);
        QCOMPARE(layer.geomRevision(), revision);
        QCOMPARE(repaint.size(), 1);
        qInfo("Nodes %d, preview %.2f ms, apply and refresh %.2f ms", size, previewNs / 1e6,
              applyNs / 1e6);
    }
};
QTEST_MAIN(BulkTypeConversionTest)
#include "test_bulktypeconversion.moc"
