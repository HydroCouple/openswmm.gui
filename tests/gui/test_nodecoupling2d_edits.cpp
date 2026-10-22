/**
 * @file      test_nodecoupling2d_edits.cpp
 * @brief     1D↔2D node coupling edits (fine-scale coupling plan §7.1–7.2):
 *         the coupling selector's staged bulk edits and their Apply as one
 *         undoable step through SWMMModelLayer, read back from the engine.
 *
 * Fixture: tests/gui/data/mini_2d.inp (junction J1, an outfall, a 2D mesh).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "ui/widgets/nodecouplingselector.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTest>

namespace
{

/** @brief Locate the node-coupling fixture.
 * @return mini_2d.inp under SWMMVIS_GUI_TEST_DATA or ".". */
QString fixturePath()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .filePath(QStringLiteral("mini_2d.inp"));
}

struct Harness {
    OpenSWMMVisWorkspace *ws = nullptr;
    SWMMVisProjectWindow *window = nullptr;
    MapCanvas *canvas = nullptr;
    SWMMModelLayer *layer = nullptr;

    /** @brief Allocate/load the owned coupling project and borrow its canvas/layer.
     * @return True when load succeeds and canvas/layer/undo stack exist, otherwise false; destructor cleans allocated owners. */
    bool open()
    {
        ws = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        window = new SWMMVisProjectWindow(ws, fixturePath(), nullptr);
        QList<QString> warnings, errors;
        if (!window->loadModel(warnings, errors))
            return false;
        canvas = window->canvas();
        layer = window->modelLayer();
        return canvas && layer && canvas->undoStack();
    }
    /** @brief Destroy the owned project before its workspace; canvas and model pointers are borrowed children. */
    ~Harness()
    {
        delete window;
        delete ws;
    }
    /** @brief Read a node coupling state for assertions.
     * @param[in] node Node name expected to resolve in the opened layer.
     * @return State populated by the unchecked layer getter, or default/partially populated state on failure. */
    nodecoupling2d::State state(const QString &node) const
    {
        nodecoupling2d::State s;
        layer->nodeCoupling2D(node, &s);
        return s;
    }
};

} // namespace

class TestNodeCoupling2DEdits : public QObject
{
    Q_OBJECT
private slots:
    // Staged bulk edits change nothing until applied; Apply is one undo step
    // and undo restores the class defaults exactly.
    /** @brief Verify staged coupleable edits leave engine state untouched until one undoable Apply, then undo restores unauthored class defaults. */
    void selectorAppliesAsOneUndoStep()
    {
        Harness h;
        QVERIFY(h.open());
        NodeCouplingTableModel model;
        model.load(h.layer);
        const int j1 = model.rowOf(QStringLiteral("J1"));
        QVERIFY(j1 >= 0);
        const auto before = h.state(QStringLiteral("J1"));
        QVERIFY(!before.authored);
        QCOMPARE(before.type, int(nodecoupling2d::Manhole));
        const int all = model.rowCount();
        QCOMPARE(model.coupleableCount(), all); // no inlet / virtual junctions

        model.setCoupleable({j1}, false);
        QCOMPARE(model.coupleableCount(), all - 1);
        QCOMPARE(h.state(QStringLiteral("J1")), before); // staged only

        const int depth = h.canvas->undoStack()->count();
        QString error;
        QVERIFY2(
            h.layer->pushNodeCoupling2DEdits(model.changedStates(), QStringLiteral("t"), &error),
            qPrintable(error));
        QCOMPARE(h.canvas->undoStack()->count(), depth + 1);
        QCOMPARE(h.state(QStringLiteral("J1")).type, int(nodecoupling2d::None));
        QVERIFY(h.state(QStringLiteral("J1")).authored);

        h.canvas->undoStack()->undo();
        QCOMPARE(h.state(QStringLiteral("J1")), before);
        h.canvas->undoStack()->redo();
        QCOMPARE(h.state(QStringLiteral("J1")).type, int(nodecoupling2d::None));
    }

    // A type the node class may not take is refused before anything is
    // written; a structure type change drops the old structure's parameters.
    /** @brief Verify illegal class/type changes refuse before history writes and a valid structure switch discards parameters belonging to the previous type. */
    void illegalTypesAreRefusedAndParametersFollowTheType()
    {
        Harness h;
        QVERIFY(h.open());
        nodecoupling2d::State s = h.state(QStringLiteral("J1"));
        s.authored = true;
        s.type = nodecoupling2d::Outfall;
        const int depth = h.canvas->undoStack()->count();
        QString error;
        QVERIFY(!h.layer->pushNodeCoupling2DEdits({s}, QStringLiteral("t"), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(h.canvas->undoStack()->count(), depth);

        NodeCouplingTableModel model;
        model.load(h.layer);
        const int j1 = model.rowOf(QStringLiteral("J1"));
        model.setType({j1}, nodecoupling2d::Outfall); // not allowed: ignored
        QVERIFY(model.changedStates().isEmpty());
        model.setType({j1}, nodecoupling2d::Grate);
        auto staged = model.changedStates();
        QCOMPARE(staged.size(), 1);
        staged[0].params.insert(QStringLiteral("AREA"), QStringLiteral("0.18"));
        QVERIFY(h.layer->pushNodeCoupling2DEdits(staged, QStringLiteral("t"), &error));
        QCOMPARE(h.state(QStringLiteral("J1")).params.value(QStringLiteral("AREA")),
                 QStringLiteral("0.18"));
        model.load(h.layer);
        model.setType({j1}, nodecoupling2d::Manhole);
        QVERIFY(model.changedStates().value(0).params.isEmpty());
    }

    // The GUI save path keeps [2D_NODE_COUPLING] (the engine writes it in
    // the main file; the mesh writer must not strip it) — Save As, reopen,
    // read the same states back. Files land in tests/output/nodecoupling2d_edits.
    /** @brief Verify authored grate area/perimeter and the 2D_NODE_COUPLING section survive real Save As and project reload in retained output. */
    void couplingSurvivesSaveAndReopen()
    {
        const QString dir = QStringLiteral(SWMMVIS_NODECOUPLING_OUTPUT_DIR);
        QVERIFY(QDir().mkpath(dir));
        const QString saved = QDir(dir).filePath(QStringLiteral("mini_2d_coupling.inp"));
        QFile::remove(saved);
        nodecoupling2d::State j1;
        {
            Harness h;
            QVERIFY(h.open());
            j1 = h.state(QStringLiteral("J1"));
            j1.authored = true;
            j1.type = nodecoupling2d::Grate;
            j1.params = {{QStringLiteral("AREA"), QStringLiteral("0.18")},
                         {QStringLiteral("PERIMETER"), QStringLiteral("1.8")}};
            QString error;
            QVERIFY2(h.layer->pushNodeCoupling2DEdits({j1}, QStringLiteral("t"), &error),
                     qPrintable(error));
            QVERIFY2(h.window->saveAs(saved, &error), qPrintable(error));
        }
        QFile f(saved);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY2(text.contains(QStringLiteral("[2D_NODE_COUPLING]")), qPrintable(saved));

        OpenSWMMVisWorkspace *ws = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        auto *window = new SWMMVisProjectWindow(ws, saved, nullptr);
        QList<QString> warnings, errors;
        QVERIFY(window->loadModel(warnings, errors));
        nodecoupling2d::State back;
        QVERIFY(window->modelLayer()->nodeCoupling2D(QStringLiteral("J1"), &back));
        QCOMPARE(back, j1);
        delete window;
        delete ws;
    }
};

QTEST_MAIN(TestNodeCoupling2DEdits)
#include "test_nodecoupling2d_edits.moc"
