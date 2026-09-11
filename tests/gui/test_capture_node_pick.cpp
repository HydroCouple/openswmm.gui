/*!
 * \file   test_capture_node_pick.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Picking an inlet junction's capture node on the map, through the
 *         real canvas press/release path in a project window:
 *           - NodePickSession swaps in the pick tool, reports the clicked
 *             node and restores the previous tool; Escape cancels
 *           - InletJunctionSetupDialog rejects an excluded node and adopts an
 *             eligible one
 *           - the inlet-junction tool opens the NON-MODAL dialog on release,
 *             the pick fills its capture field, and OK inserts the node with
 *             that capture
 *           - the Properties-panel DataObjectPickerEditor writes a map pick
 *             through the layer (host index carried by the DataObjectRef)
 *
 * Fixture: tests/gui/data/street_inlet_local.inp (tests/data/inlets/
 * street_inlet.inp plus a [MAP] section) — J1(0,0) —C1 STREET— J2(400,0),
 * SEWER(200,-100) —D1— OUT1(400,-100), inlet design GRATE1. Local CRS, so
 * canvas coordinates are layer coordinates.
 */

#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/nodepicksession.h"
#include "map/tools/maptool.h"
#include "map/tools/maptooladdinletnode.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/inletjunctionsetupdialog.h"
#include "ui/properties/dataobjectpickereditor.h"
#include "ui/widgets/labeledcontrols.h"

#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <QDir>
#include <QObject>
#include <QPoint>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QToolButton>

namespace {

QString fixturePath()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .filePath(QStringLiteral("street_inlet_local.inp"));
}

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
    void clickAt(const QPoint &at)
    {
        QTest::mouseMove(canvas, at);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, at);   // press + release
        QTest::qWait(20);
    }
    QPoint sewer() const { return pixelOf(200.0, -100.0); }
    QPoint out1()  const { return pixelOf(400.0, -100.0); }
    QPoint j1()    const { return pixelOf(0.0, 0.0); }
};

/*! An inert stand-in for "whatever tool was active before the pick". */
class DummyTool : public OpenSWMMVisMapTool
{
public:
    explicit DummyTool(MapCanvas *c) : OpenSWMMVisMapTool(QStringLiteral("Dummy"), c) {}
};

} // namespace

class TestCaptureNodePick : public QObject
{
    Q_OBJECT

private slots:
    void sessionPicksNodeAndRestoresTool()
    {
        Harness h;
        QVERIFY(h.open());
        DummyTool previous(h.canvas);
        h.canvas->setActiveTool(&previous);

        auto *session = new NodePickSession(h.canvas, h.layer);
        QVERIFY(session->isActive());
        QVERIFY(h.canvas->activeTool() != &previous);
        QSignalSpy picked(session, &NodePickSession::nodePicked);
        QSignalSpy cancelled(session, &NodePickSession::cancelled);

        h.clickAt(h.pixelOf(200.0, 60.0));               // empty canvas: keep waiting
        QCOMPARE(picked.count(), 0);
        QVERIFY(session->isActive());

        h.clickAt(h.sewer());
        QCOMPARE(picked.count(), 1);
        QCOMPARE(picked.first().at(0).value<SWMMModelLayer *>(), h.layer);
        QCOMPARE(picked.first().at(1).toString(), QStringLiteral("SEWER"));
        QCOMPARE(picked.first().at(2).toInt(), h.layer->nodeIndex(QStringLiteral("SEWER")));
        QVERIFY(session->isActive());                    // consumer decides when it ends

        session->finish();
        QVERIFY(!session->isActive());
        QCOMPARE(h.canvas->activeTool(), &previous);
        QCOMPARE(cancelled.count(), 0);
        delete session;
        h.canvas->setActiveTool(nullptr);
    }

    void sessionEscapeCancelsAndRestoresTool()
    {
        Harness h;
        QVERIFY(h.open());
        DummyTool previous(h.canvas);
        h.canvas->setActiveTool(&previous);

        auto *session = new NodePickSession(h.canvas, h.layer);
        QSignalSpy cancelled(session, &NodePickSession::cancelled);
        QVERIFY(session->isActive());
        QTest::keyClick(h.canvas, Qt::Key_Escape);
        QCOMPARE(cancelled.count(), 1);
        QVERIFY(!session->isActive());
        QCOMPARE(h.canvas->activeTool(), &previous);
        delete session;
        h.canvas->setActiveTool(nullptr);
    }

    void dialogRejectsExcludedThenAdoptsPick()
    {
        Harness h;
        QVERIFY(h.open());
        const int j1 = h.layer->nodeIndex(QStringLiteral("J1"));
        const int j2 = h.layer->nodeIndex(QStringLiteral("J2"));
        QVERIFY(j1 >= 0 && j2 >= 0);

        openswmmvis::ui::InletJunctionSetupDialog dlg(h.layer, {j1, j2}, h.window);
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        // Preselection = first eligible node in sorted order (J1/J2 excluded).
        QCOMPARE(dlg.captureNode(), QStringLiteral("OUT1"));
        QVERIFY(!dlg.isPickingCaptureNode());

        auto *picker = dlg.findChild<LabeledPickerCombo *>(QStringLiteral("capturePicker"));
        QVERIFY(picker);
        QVERIFY(picker->button()->isEnabled());
        OpenSWMMVisMapTool *previous = h.canvas->activeTool();   // the window's Select tool
        picker->button()->click();                       // the "…" button
        QVERIFY(dlg.isPickingCaptureNode());
        QVERIFY(h.canvas->activeTool() != previous);

        h.clickAt(h.j1());                                // excluded host end: refused
        QVERIFY(dlg.isPickingCaptureNode());
        QCOMPARE(dlg.captureNode(), QStringLiteral("OUT1"));

        h.clickAt(h.sewer());
        QCOMPARE(dlg.captureNode(), QStringLiteral("SEWER"));
        QVERIFY(!dlg.isPickingCaptureNode());
        QCOMPARE(h.canvas->activeTool(), previous);      // previous tool restored
    }

    void inletToolOpensNonModalDialogAndInsertsWithPickedCapture()
    {
        Harness h;
        QVERIFY(h.open());
        const int links = h.layer->cachedLinkCount();
        const int nodes = h.layer->cachedNodeCount();

        OpenSWMMVisMapToolAddInletNode tool(h.canvas);
        QSignalSpy added(&tool, &OpenSWMMVisMapToolAddInletNode::inletJunctionAdded);
        h.canvas->setActiveTool(&tool);
        h.clickAt(h.pixelOf(200.0, 0.0));                // middle of C1 (STREET)

        // Nothing is inserted yet: the dialog is up, non-modal, waiting.
        QCOMPARE(h.layer->cachedLinkCount(), links);
        openswmmvis::ui::InletJunctionSetupDialog *dlg = tool.pendingDialog();
        QVERIFY(dlg);
        QVERIFY(dlg->isVisible());
        QVERIFY(!dlg->isModal());
        QCOMPARE(h.canvas->activeTool(), &tool);

        auto *design = dlg->findChild<LabeledPickerCombo *>(QStringLiteral("designPicker"));
        QVERIFY(design);
        design->setCurrentText(QStringLiteral("GRATE1"));
        QCOMPARE(dlg->inletDesign(), QStringLiteral("GRATE1"));

        dlg->startCaptureNodePick();
        QVERIFY(dlg->isPickingCaptureNode());
        QVERIFY(h.canvas->activeTool() != &tool);
        h.clickAt(h.sewer());
        QCOMPARE(dlg->captureNode(), QStringLiteral("SEWER"));
        QCOMPARE(h.canvas->activeTool(), &tool);         // inlet tool is back

        dlg->accept();
        QTest::qWait(50);                                 // WA_DeleteOnClose
        QVERIFY(!tool.pendingDialog());

        QCOMPARE(added.count(), 1);
        const QString nodeName = added.first().at(0).toString();
        QCOMPARE(added.first().at(1).toString(), QStringLiteral("C1"));
        QCOMPARE(h.layer->cachedLinkCount(), links + 1);
        QCOMPARE(h.layer->cachedNodeCount(), nodes + 1);
        const int ni = h.layer->nodeIndex(nodeName);
        QVERIFY(ni >= 0);
        int isInlet = 0;
        swmm_node_is_inlet(h.layer->engine(), ni, &isInlet);
        QVERIFY(isInlet);
        SWMM_InletUsage u{};
        QVERIFY(h.layer->inletUsageFor(SWMM_INLET_HOST_NODE, ni, &u));
        QCOMPARE(u.capture_node_idx, h.layer->nodeIndex(QStringLiteral("SEWER")));
        QCOMPARE(QString::fromUtf8(swmm_inlet_id(h.layer->engine(), u.design_idx)),
                 QStringLiteral("GRATE1"));
        h.canvas->setActiveTool(nullptr);
    }

    void propertyEditorPickWritesThroughLayer()
    {
        Harness h;
        QVERIFY(h.open());

        // Host: an inlet junction inserted through the tool with SEWER.
        OpenSWMMVisMapToolAddInletNode tool(h.canvas);
        QSignalSpy added(&tool, &OpenSWMMVisMapToolAddInletNode::inletJunctionAdded);
        h.canvas->setActiveTool(&tool);
        h.clickAt(h.pixelOf(200.0, 0.0));
        openswmmvis::ui::InletJunctionSetupDialog *dlg = tool.pendingDialog();
        QVERIFY(dlg);
        dlg->findChild<LabeledPickerCombo *>(QStringLiteral("designPicker"))
            ->setCurrentText(QStringLiteral("GRATE1"));
        auto *capture = dlg->findChild<LabeledPickerCombo *>(QStringLiteral("capturePicker"));
        capture->setCurrentText(QStringLiteral("SEWER"));
        dlg->accept();
        QTest::qWait(50);
        QCOMPARE(added.count(), 1);
        h.canvas->setActiveTool(nullptr);
        const int host = h.layer->nodeIndex(added.first().at(0).toString());
        QVERIFY(host >= 0);

        // The Properties-panel cell editor for that node's capture-node row.
        DataObjectRef ref;
        ref.engine      = h.layer->engine();
        ref.layer       = h.layer;
        ref.kind        = DataObjectRef::CaptureNode;
        ref.currentName = QStringLiteral("SEWER");
        ref.hostNodeIdx = host;
        DataObjectPickerEditor editor(h.window);
        editor.setValue(ref);
        editor.show();
        QSignalSpy changed(&editor, &DataObjectPickerEditor::valueChanged);

        auto *combo = editor.findChild<LabeledPickerCombo *>();
        QVERIFY(combo);
        combo->button()->click();                        // "…" → map pick
        QVERIFY(h.canvas->activeTool());                 // pick tool installed
        h.clickAt(h.pixelOf(200.0, 0.0));                // the inlet itself: refused
        QVERIFY(h.canvas->activeTool());
        h.clickAt(h.out1());
        QTest::qWait(50);

        SWMM_InletUsage u{};
        QVERIFY(h.layer->inletUsageFor(SWMM_INLET_HOST_NODE, host, &u));
        QCOMPARE(u.capture_node_idx, h.layer->nodeIndex(QStringLiteral("OUT1")));
        QCOMPARE(editor.value().currentName, QStringLiteral("OUT1"));
        QVERIFY(changed.count() >= 1);
        QCOMPARE(h.canvas->activeTool(), nullptr);       // previous (none) restored

        // The layer write is undoable from the canvas stack.
        QVERIFY(h.canvas->undoStack());
        h.canvas->undoStack()->undo();
        QVERIFY(h.layer->inletUsageFor(SWMM_INLET_HOST_NODE, host, &u));
        QCOMPARE(u.capture_node_idx, h.layer->nodeIndex(QStringLiteral("SEWER")));
    }
};

QTEST_MAIN(TestCaptureNodePick)
#include "test_capture_node_pick.moc"
