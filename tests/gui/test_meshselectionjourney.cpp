// W0 baseline: drive the real mesh selection tool without an engine or results.
// The fixture has two triangles and a rectangular quad; references must remain
// cell indices, not the display triangles used to paint the quad.

/**
 * @file      test_meshselectionjourney.cpp
 * @brief     Tests for meshselectionjourney.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

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

namespace
{
/**
 * @brief Construct owned mixed triangle/quad mesh for engine-free selection journeys.
 * @return Six planar vertices and cells 0/1 triangles,2 quad, source assigned by caller; heights
 * 0. No native engine/results or global state, allocates local Qt containers.
 */
mesh::MeshResult fixture()
{
    mesh::MeshResult mesh;
    for (const QPointF p : {QPointF(0, 0), QPointF(10, 0), QPointF(10, 10), QPointF(0, 10),
                            QPointF(30, 0), QPointF(30, 10)})
        mesh.vertices.append({p, 0.0});
    mesh.triangles = {{0, 1, 2}, {0, 2, 3}, {1, 4, 5, 2}};
    return mesh;
}

struct Journey {
    SWMM2DMeshLayer mesh{fixture(), QStringLiteral("baseline-mixed.2dm")};
    MapCanvas canvas;
    SelectionManager selection;
    MapToolPick2DCells tool{&canvas, &selection};

    /**
     * @brief Assemble stack-owned canvas/layer/selection/tool and activate the real cell-picking
     * tool.
     * @note 700 x 400 canvas, extent[-5,-5,35,15], nonundo layer association. Qt GUI/QML
     * renderer registrations must already exist; destructor detaches stack layer before canvas
     * destroys it. No engine/results dependency.
     */
    Journey()
    {
        canvas.resize(700, 400);
        canvas.addLayer(&mesh, false);
        canvas.setExtent(MapExtent(-5, -5, 35, 15), false);
        tool.activate();
    }
    /**
     * @brief Deactivate cell tool and detach stack mesh layer before canvas/owner destruction.
     * @note No deletion of stack layer through canvas, no deferred selection action retained;
     * GUI thread only.
     */
    ~Journey()
    {
        tool.deactivate();
        canvas.takeLayer(0, false);
    }
    /**
     * @brief Deliver one synthetic map-coordinate mouse event directly to the active
     * cell-picking tool.
     * @param[in] type Press/move/release selects matching handler; any other supplied type uses
     * double-click.
     * @param[in] x Map x coordinate converted to current canvas pixel, arbitrary fixture units.
     * @param[in] y Map y coordinate in same units.
     * @param[in] modifiers Keyboard modifier flags held for this event, default none.
     * @note Move has no event button, release no held buttons, other cases left-button
     * semantics. Stack event borrowed only through synchronous handler; mutates tool/selection,
     * no event-loop dispatch or engine advance.
     */
    void mouse(QEvent::Type type, double x, double y,
               Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        int px, py;
        tool.toPixelCoords(x, y, px, py);
        const QPointF p(px, py);
        const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event(type, p, p, button, buttons, modifiers);
        if (type == QEvent::MouseButtonPress)
            tool.mousePressEvent(&event);
        else if (type == QEvent::MouseMove)
            tool.mouseMoveEvent(&event);
        else if (type == QEvent::MouseButtonRelease)
            tool.mouseReleaseEvent(&event);
        else
            tool.mouseDoubleClickEvent(&event);
    }
    /**
     * @brief Select Box mode and synchronously simulate one directional rectangle drag.
     * @param[in] x0 Starting map x.
     * @param[in] y0 Starting map y.
     * @param[in] x1 Ending map x, may be less than start to exercise reverse drag.
     * @param[in] y1 Ending map y.
     * @param[in] modifiers Flags passed to press/move/release for add/toggle semantics.
     * @note Mutates owned tool/selection, not fixture geometry; GUI thread and activated
     * initialized journey required.
     */
    void box(double x0, double y0, double x1, double y1,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        tool.setMode(MapToolPick2DCells::Mode::Box);
        mouse(QEvent::MouseButtonPress, x0, y0, modifiers);
        mouse(QEvent::MouseMove, x1, y1, modifiers);
        mouse(QEvent::MouseButtonRelease, x1, y1, modifiers);
    }
    /**
     * @brief Build expected source-qualified mesh cell references for equality assertions.
     * @param[in] cells Initializer list of logical cell indices, copied without range checking.
     * @return Owning QSet of MeshObjectRef::cell(current fixture sourcePath,index); quad remains
     * one cell, no display-triangle conversion or model mutation. Container allocation may fail.
     */
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
    /**
     * @brief Register QML renderers and optionally write live comprehensive-editor registry
     * audit.
     * @note Requires GUI app/resources, asserts mesh QML path; registers production renderers
     * without main.cpp. When SWMMVIS_BASELINE_OUTPUT set, creates directory and writes
     * editor_registry.json from live registry with category/title/create/browse/gap fields. No
     * settings/model writes otherwise; test assertions report IO failure.
     */
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
        if (output.isEmpty())
            return;
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

    /**
     * @brief Verify real rectangle picking selects logical triangle/quad cells without
     * engine/results.
     * @note Full drag selects 0,1,2 and reverse right-half drag selects only quad 2;
     * source-qualified set comparison catches accidental use of display-triangle indices.
     * Journey owns/detaches local layer.
     */
    void rectangleSelectsMixedCellsWithoutResults()
    {
        Journey j;
        QCOMPARE(j.canvas.layerCount(), 1);
        j.box(-1, -1, 31, 11);
        QCOMPARE(j.selection.selection(), j.refs({0, 1, 2}));
        j.box(31, 11, 11, -1); // reverse drag; the quad is one cell
        QCOMPARE(j.selection.selection(), j.refs({2}));
    }

    /**
     * @brief Verify lasso press/double-click completion selects logical quad 2 with no results
     * source.
     * @note Synthetic rectangle polygon around right mesh uses real picker handlers, exact
     * source-qualified selection set asserted; stack journey cleans tool/canvas association.
     */
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

    /**
     * @brief Verify Shift adds cells, Control toggles them and unmodified miss clears selection.
     * @note Starts triangles 0/1, adds quad 2, toggles it off then clicks outside; checks exact
     * sets after every real tool action. No file/engine/results state changes.
     */
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

    /**
     * @brief Verify Escape aborts in-progress lasso and clears prior cell selection.
     * @note Selects quad first, begins left lasso, sends synchronous Escape then attempted
     * completion; selection must remain empty. Stack event/tool/canvas owned by local GUI-thread
     * journey.
     */
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

    /**
     * @brief Verify the active mesh determines source identity for picked cell references.
     * @note Adds second stack layer with different source path, activates it and picker, selects
     * quad 2 then detaches before destruction. Exact active-source reference required, no stale
     * original mesh identity.
     */
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
