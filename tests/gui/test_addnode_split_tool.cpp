/*!
 * \file   test_addnode_split_tool.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  The generic add-node tools split a conduit when the click lands on
 *         one (ADDNODE_SPLIT_REDESIGN_PLAN_2026-09-10.md step 3), through the
 *         real canvas event path (press + release on a MapCanvas inside a
 *         SWMMVisProjectWindow):
 *           - Junction on a conduit → the conduit is split, the junction sits
 *             at the break, `<name>_B` carries the downstream half
 *           - Outfall on a conduit → refused, nothing changes
 *           - Storage on a conduit → split + converted to STORAGE, undoable
 *             from the canvas undo stack
 *           - Junction on empty canvas → placed freely, no split
 */

#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/tools/maptooladdnode.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"

#include <openswmm/engine/openswmm_nodes.h>

#include <QDir>
#include <QObject>
#include <QPoint>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <cmath>

namespace {

QString fixturePath()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .filePath(QStringLiteral("selection_trace_fixture.inp"));
}

/*! A loaded project window with its canvas zoomed to the model. The fixture
 *  is in a Local CRS, so canvas coordinates ARE layer coordinates:
 *  J1(0,0) —C1— J2(1000,0) —C2— J3(2000,0) —C3— O1(3000,0), J4(2000,-800)
 *  with pump P1 J4→J3. */
struct Harness
{
    OpenSWMMVisWorkspace  *ws     = nullptr;
    SWMMVisProjectWindow  *window = nullptr;
    MapCanvas             *canvas = nullptr;
    SWMMModelLayer        *layer  = nullptr;

    bool open()
    {
        ws = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        window = new SWMMVisProjectWindow(ws, fixturePath(), nullptr);
        QList<QString> warnings, errors;
        if (!window->loadModel(warnings, errors)) return false;
        window->resize(900, 700);
        window->show();
        if (!QTest::qWaitForWindowExposed(window)) return false;
        QTest::qWait(200);
        canvas = window->canvas();
        layer  = window->modelLayer();
        if (!canvas || !layer) return false;
        canvas->zoomToFullExtent();
        QTest::qWait(100);
        return true;
    }
    ~Harness()
    {
        delete window;
        delete ws;
    }
    QPoint pixelOf(double mapX, double mapY) const
    {
        int px = 0, py = 0;
        canvas->toPixelCoords(mapX, mapY, px, py);
        return { px, py };
    }
    void click(OpenSWMMVisMapToolAddNode &tool, const QPoint &at)
    {
        canvas->setActiveTool(&tool);
        QTest::mouseMove(canvas, at);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, at);   // press + release
        canvas->setActiveTool(nullptr);
    }
};

int engineNodeType(SWMMModelLayer *layer, const QString &name)
{
    const int idx = swmm_node_index(layer->engine(), name.toUtf8().constData());
    int type = -1;
    if (idx >= 0) swmm_node_get_type(layer->engine(), idx, &type);
    return type;
}

} // namespace

class TestAddNodeSplitTool : public QObject
{
    Q_OBJECT

private slots:
    void junctionOnConduitSplits()
    {
        Harness h;
        QVERIFY(h.open());
        const int links = h.layer->cachedLinkCount();
        const int nodes = h.layer->cachedNodeCount();

        OpenSWMMVisMapToolAddNode tool(h.canvas, 0, QStringLiteral("junction"));
        QSignalSpy split(&tool, &OpenSWMMVisMapToolAddNode::nodeInsertedOnConduit);
        h.click(tool, h.pixelOf(500.0, 0.0));            // middle of C1

        QCOMPARE(h.layer->cachedLinkCount(), links + 1);
        QCOMPARE(h.layer->cachedNodeCount(), nodes + 1);
        QCOMPARE(h.layer->renderLinkCount(), h.layer->cachedLinkCount());
        QCOMPARE(split.count(), 1);
        const QString nodeName = split.first().at(0).toString();
        QCOMPARE(split.first().at(1).toString(), QStringLiteral("C1"));
        QCOMPARE(split.first().at(2).toString(), QStringLiteral("C1_B"));
        QVERIFY(h.layer->linkIndex(QStringLiteral("C1_B")) >= 0);
        QCOMPARE(engineNodeType(h.layer, nodeName), 0);
        // The junction sits on the conduit, near the click.
        double x = 0, y = 0;
        QVERIFY(h.layer->cachedNodeCoord(h.layer->nodeIndex(nodeName), &x, &y));
        QVERIFY2(std::abs(y) < 1e-6 && std::abs(x - 500.0) < 60.0,
                 qPrintable(QStringLiteral("junction at (%1,%2)").arg(x).arg(y)));
        QVERIFY(h.layer->isObjectVisible(QStringLiteral("C1_B"), SWMMModelLayer::CatConduits));
    }

    void outfallOnConduitRefused()
    {
        Harness h;
        QVERIFY(h.open());
        const int links = h.layer->cachedLinkCount();
        const int nodes = h.layer->cachedNodeCount();

        OpenSWMMVisMapToolAddNode tool(h.canvas, 1, QStringLiteral("outfall"));
        QVERIFY(!tool.kindCanSplitConduit());
        QSignalSpy status(&tool, &OpenSWMMVisMapToolAddNode::statusMessageChanged);
        h.click(tool, h.pixelOf(1500.0, 0.0));           // middle of C2

        QCOMPARE(h.layer->cachedLinkCount(), links);
        QCOMPARE(h.layer->cachedNodeCount(), nodes);
        // The refusal hint was emitted (deactivate() then clears the status
        // bar with an empty message, so look through every emission).
        bool refused = false;
        for (const auto &args : status)
            if (args.at(0).toString().contains(QStringLiteral("must end the network")))
                refused = true;
        QVERIFY2(refused, "no outfall-on-conduit refusal message was emitted");
    }

    void storageOnConduitSplitsConvertsAndUndoes()
    {
        Harness h;
        QVERIFY(h.open());
        const int links = h.layer->cachedLinkCount();
        const int nodes = h.layer->cachedNodeCount();

        OpenSWMMVisMapToolAddNode tool(h.canvas, 2, QStringLiteral("storage"));
        QSignalSpy split(&tool, &OpenSWMMVisMapToolAddNode::nodeInsertedOnConduit);
        h.click(tool, h.pixelOf(2500.0, 0.0));           // middle of C3

        QCOMPARE(split.count(), 1);
        const QString nodeName = split.first().at(0).toString();
        QCOMPARE(h.layer->cachedLinkCount(), links + 1);
        QVERIFY(h.layer->linkIndex(QStringLiteral("C3_B")) >= 0);
        QCOMPARE(engineNodeType(h.layer, nodeName), 2);
        QCOMPARE(h.layer->objectNameAt(SWMMModelLayer::CatStorage, 0), nodeName);

        // One undoable step on the canvas stack.
        QVERIFY(h.canvas->undoStack());
        h.canvas->undoStack()->undo();
        QCOMPARE(h.layer->cachedLinkCount(), links);
        QCOMPARE(h.layer->cachedNodeCount(), nodes);
        QVERIFY(h.layer->linkIndex(QStringLiteral("C3_B")) < 0);
        QVERIFY(h.layer->nodeIndex(nodeName) < 0);
        QCOMPARE(h.layer->renderLinkCount(), h.layer->cachedLinkCount());

        h.canvas->undoStack()->redo();
        QCOMPARE(h.layer->cachedLinkCount(), links + 1);
        QCOMPARE(engineNodeType(h.layer, nodeName), 2);
    }

    void junctionOffConduitPlacesFreely()
    {
        Harness h;
        QVERIFY(h.open());
        const int links = h.layer->cachedLinkCount();
        const int nodes = h.layer->cachedNodeCount();

        OpenSWMMVisMapToolAddNode tool(h.canvas, 0, QStringLiteral("junction"));
        QSignalSpy split(&tool, &OpenSWMMVisMapToolAddNode::nodeInsertedOnConduit);
        QSignalSpy added(&tool, &OpenSWMMVisMapToolAddNode::nodeAdded);
        h.click(tool, h.pixelOf(500.0, -500.0));         // nowhere near a link

        QCOMPARE(split.count(), 0);
        QCOMPARE(added.count(), 1);
        QCOMPARE(h.layer->cachedLinkCount(), links);
        QCOMPARE(h.layer->cachedNodeCount(), nodes + 1);
    }
};

QTEST_MAIN(TestAddNodeSplitTool)
#include "test_addnode_split_tool.moc"
