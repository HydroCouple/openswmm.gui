// W0 baseline: drive the real mesh selection tool without an engine or results.
// The fixture has two triangles and a rectangular quad; references must remain
// cell indices, not the display triangles used to paint the quad.
#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QQmlEngine>

#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "map/tools/maptoolpick2dcells.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"
#include "ui/editors/comprehensiveeditorregistry.h"

namespace {
mesh::MeshResult fixture()
{
    mesh::MeshResult mesh;
    for (const QPointF p : {QPointF(0, 0), QPointF(10, 0), QPointF(10, 10),
                            QPointF(0, 10), QPointF(30, 0), QPointF(30, 10)})
        mesh.vertices.append({p, 0.0});
    mesh.triangles = {{0, 1, 2}, {0, 2, 3}, {1, 4, 5, 2}};
    return mesh;
}

struct Journey {
    SWMM2DMeshLayer mesh{fixture(), QStringLiteral("baseline-mixed.2dm")};
    MapCanvas canvas;
    SelectionManager selection;
    MapToolPick2DCells tool{&canvas, &selection};

    Journey()
    {
        canvas.resize(700, 400);
        canvas.addLayer(&mesh, false);
        canvas.setExtent(MapExtent(-5, -5, 35, 15), false);
        tool.activate();
    }
    ~Journey()
    {
        tool.deactivate();
        canvas.takeLayer(0, false);
    }
    void mouse(QEvent::Type type, double x, double y,
               Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        int px, py;
        tool.toPixelCoords(x, y, px, py);
        const QPointF p(px, py);
        const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event(type, p, p, button, buttons, modifiers);
        if (type == QEvent::MouseButtonPress) tool.mousePressEvent(&event);
        else if (type == QEvent::MouseMove) tool.mouseMoveEvent(&event);
        else if (type == QEvent::MouseButtonRelease) tool.mouseReleaseEvent(&event);
        else tool.mouseDoubleClickEvent(&event);
    }
    void box(double x0, double y0, double x1, double y1,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        tool.setMode(MapToolPick2DCells::Mode::Box);
        mouse(QEvent::MouseButtonPress, x0, y0, modifiers);
        mouse(QEvent::MouseMove, x1, y1, modifiers);
        mouse(QEvent::MouseButtonRelease, x1, y1, modifiers);
    }
    QSet<SWMMObjectRef> refs(std::initializer_list<int> cells) const
    {
        QSet<SWMMObjectRef> result;
        for (int cell : cells)
            result.insert(mesh::MeshObjectRef::cell(mesh.sourcePath(), cell));
        return result;
    }
};
}

class TestMeshSelectionJourney : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // The test has no main.cpp: mirror its QML registration before a canvas
        // is constructed. Selection must not rely on an incomplete QML scene.
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0, "SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DResultsQSGRenderer");
        QVERIFY(QFile::exists(QStringLiteral(":/openswmm/qml/swmmlayer.qml")));
        // Optional audit output comes from the live registry, not a copied list
        // of editor names. No user settings or model files are touched.
        const QString output = qEnvironmentVariable("SWMMVIS_BASELINE_OUTPUT");
        if (output.isEmpty()) return;
        QVERIFY(QDir().mkpath(output));
        QJsonArray entries;
        auto &registry = ComprehensiveEditorRegistry::instance();
        for (int i = 0; i < SWMMModelLayer::NumDataCategories; ++i) {
            const auto category = static_cast<SWMMModelLayer::DataCategory>(i);
            const auto *entry = registry.find(category);
            QJsonObject row{{"category", i}, {"registered", entry != nullptr}};
            if (entry) {
                row["title"] = entry->editorTitle;
                row["createAvailable"] = bool(entry->openCreateNew);
                row["browseAvailable"] = bool(entry->openBrowse);
                row["gap"] = entry->gapSliceLabel;
            }
            entries.append(row);
        }
        QFile file(QDir(output).filePath(QStringLiteral("editor_registry.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray bytes = QJsonDocument(entries).toJson();
        QCOMPARE(file.write(bytes), bytes.size());
    }

    void rectangleSelectsMixedCellsWithoutResults()
    {
        Journey j;
        QCOMPARE(j.canvas.layerCount(), 1);
        j.box(-1, -1, 31, 11);
        QCOMPARE(j.selection.selection(), j.refs({0, 1, 2}));
        j.box(31, 11, 11, -1); // reverse drag; the quad is one cell
        QCOMPARE(j.selection.selection(), j.refs({2}));
    }

    void polygonSelectsQuadWithoutResults()
    {
        Journey j;
        j.tool.setMode(MapToolPick2DCells::Mode::Lasso);
        j.mouse(QEvent::MouseButtonPress, 11, -1);
        j.mouse(QEvent::MouseButtonPress, 31, -1);
        j.mouse(QEvent::MouseButtonPress, 31, 11);
        j.mouse(QEvent::MouseButtonDblClick, 11, 11);
        QCOMPARE(j.selection.selection(), j.refs({2}));
    }

    void modifiersAddToggleAndMissClears()
    {
        Journey j;
        j.box(-1, -1, 9, 11);
        QCOMPARE(j.selection.selection(), j.refs({0, 1}));
        j.box(11, -1, 31, 11, Qt::ShiftModifier);
        QCOMPARE(j.selection.selection(), j.refs({0, 1, 2}));
        j.box(11, -1, 31, 11, Qt::ControlModifier);
        QCOMPARE(j.selection.selection(), j.refs({0, 1}));
        j.mouse(QEvent::MouseButtonPress, 33, 13);
        j.mouse(QEvent::MouseButtonRelease, 33, 13);
        QVERIFY(j.selection.selection().isEmpty());
    }

    void escapeCancelsPolygonAndClearsSelection()
    {
        Journey j;
        j.box(11, -1, 31, 11);
        j.tool.setMode(MapToolPick2DCells::Mode::Lasso);
        j.mouse(QEvent::MouseButtonPress, -1, -1);
        j.mouse(QEvent::MouseButtonPress, 9, -1);
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        j.tool.keyPressEvent(&escape);
        j.mouse(QEvent::MouseButtonDblClick, 9, 11);
        QVERIFY(j.selection.selection().isEmpty());
    }

    void activeMeshOwnsSelectionReferences()
    {
        Journey j;
        SWMM2DMeshLayer active(fixture(), QStringLiteral("active-mixed.2dm"));
        j.canvas.addLayer(&active, false);
        active.setActiveMesh(true);
        j.tool.activate();
        j.box(11, -1, 31, 11);
        const QSet<SWMMObjectRef> expected{mesh::MeshObjectRef::cell(active.sourcePath(), 2)};
        QCOMPARE(j.selection.selection(), expected);
        j.canvas.takeLayer(j.canvas.layers().indexOf(&active), false);
    }
};

QTEST_MAIN(TestMeshSelectionJourney)
#include "test_meshselectionjourney.moc"
