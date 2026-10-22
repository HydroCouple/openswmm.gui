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
#include "ui/panels/swmmattributetablemodel.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/properties/attributegroups.h"
#include <QComboBox>
#include <QTableView>
#include <QUndoStack>
#include <QClipboard>
#include <QApplication>

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTest>

namespace
{

int columnFor(SWMMAttributeTableModel &table, const QString &tag)
{
    const auto specs = table.columnSpecs();
    for (int c = 0; c < specs.size(); ++c)
        if (specs[c].setter == tag) return c;
    return -1;
}

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
    bool open(const QString &path = fixturePath())
    {
        ws = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        window = new SWMMVisProjectWindow(ws, path, nullptr);
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
    void attributeParametersValidateAndUndo()
    {
        Harness h;
        QVERIFY(h.open());
        SWMMAttributeTableModel table;
        table.setSource(h.layer, SWMMModelLayer::CatJunctions);
        table.setUndoStack(h.canvas->undoStack());
        auto cell = [&](const QString &tag) { return table.index(0, columnFor(table, tag)); };
        const auto initial = h.state("J1");
        for (const auto &key : nodecoupling2d::paramKeys()) {
            if (key == "SPREAD" || key == "TAILWATER_RELAX") continue;
            const auto idx = cell("node_2d_param_" + key);
            QVERIFY2(idx.isValid(), qPrintable(key));
            QVERIFY(table.flags(idx) & Qt::ItemIsEditable);
            QVERIFY(!table.data(idx).isValid() || table.data(idx).toString().isEmpty());
            QVERIFY(table.headerData(idx.column(), Qt::Horizontal,
                                     openswmmvis::AttributeGroupRole).toString().startsWith("2D Coupling"));
        }
        QVERIFY(table.setData(cell("node_2d_param_D"), "0.24"));
        QCOMPARE(h.state("J1").params.value("D"), QString("0.24"));
        QCOMPARE(table.data(cell("node_2d_param_D")).toDouble(), 0.24);
        h.canvas->undoStack()->undo();
        QCOMPARE(h.state("J1"), initial); // blank/inherited state is restored exactly
        h.canvas->undoStack()->redo();
        QVERIFY(table.setData(cell("node_2d_param_LAW"), "weir_orifice"));
        QVERIFY(table.setData(cell("node_2d_param_COEFFS"), "RUBINATO2017"));
        const auto geometry = h.state("J1");
        const int depth = h.canvas->undoStack()->count();
        QVERIFY(!table.setData(cell("node_2d_param_D"), "-2"));
        QVERIFY(!table.setData(cell("node_2d_param_D"), "nan"));
        QVERIFY(!table.setData(cell("node_2d_param_LID_OPEN_FRACTION"), "1.1"));
        QVERIFY(!table.setData(cell("node_2d_param_LAW"), "bad"));
        QVERIFY(!table.setData(cell("node_2d_coupling_type"), "OUTFALL"));
        QVERIFY(!table.setData(cell("node_2d_coupleable"), "perhaps"));
        QCOMPARE(h.canvas->undoStack()->count(), depth);
        QCOMPARE(h.state("J1"), geometry);
        QVERIFY(table.setData(cell("node_2d_coupleable"), "no"));
        QCOMPARE(table.data(cell("node_2d_coupleable")).toString(), QString("NO"));
        QVERIFY(h.state("J1").params.isEmpty());
        h.canvas->undoStack()->undo();
        QCOMPARE(h.state("J1"), geometry);
        QVERIFY(table.setData(cell("node_2d_coupling_type"), "GRATE"));
        QVERIFY(h.state("J1").params.isEmpty());
        h.canvas->undoStack()->undo();
        QCOMPARE(h.state("J1"), geometry);
        QVERIFY(table.setData(cell("node_2d_param_D"), ""));
        QVERIFY(!h.state("J1").params.contains("D"));
        h.canvas->undoStack()->undo();
        QCOMPARE(h.state("J1"), geometry);

        table.setSource(h.layer, SWMMModelLayer::CatOutfalls);
        QVERIFY(cell("node_2d_param_SPREAD").isValid());
        QVERIFY(cell("node_2d_param_TAILWATER_RELAX").isValid());
        QVERIFY(!cell("node_2d_param_D").isValid());
        QVERIFY(table.setData(cell("node_2d_param_SPREAD"), "0"));
        QVERIFY(table.setData(cell("node_2d_param_TAILWATER_RELAX"), "10"));
        QCOMPARE(h.state("O1").params.value("TAILWATER_RELAX"), QString("10"));
        for (const auto category : {SWMMModelLayer::CatStorage, SWMMModelLayer::CatDividers}) {
            table.setSource(h.layer, category);
            for (const auto &key : nodecoupling2d::paramKeys()) {
                if (key == "SPREAD" || key == "TAILWATER_RELAX") continue;
                QVERIFY2(columnFor(table, "node_2d_param_" + key) >= 0, qPrintable(key));
            }
        }
    }

    void pasteVisibleRowsAndBooleans()
    {
        Harness h;
        QVERIFY(h.open(QDir(QFileInfo(fixturePath()).absolutePath())
                           .filePath("typed_selection_fixture.inp")));
        AttributeTablePanel panel;
        panel.setProject(h.layer, nullptr, h.canvas);
        auto *combo = panel.findChild<QComboBox *>();
        QVERIFY(combo);
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemText(i).startsWith("Junctions")) combo->setCurrentIndex(i);
        auto *view = panel.findChild<QTableView *>();
        auto *table = panel.findChild<SWMMAttributeTableModel *>();
        QVERIFY(view && table);
        QVERIFY(view->model()->rowCount() >= 2);
        // Sorting must not redirect subsequent writes in the same paste.
        const int flag = columnFor(*table, "node_2d_coupleable");
        const int diameter = columnFor(*table, "node_2d_param_D");
        view->sortByColumn(0, Qt::DescendingOrder);
        const QString first = view->model()->index(0, 0).data().toString();
        const QString second = view->model()->index(1, 0).data().toString();
        view->setCurrentIndex(view->model()->index(0, flag));
        view->selectionModel()->select(view->model()->index(1, flag),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
        const int depth = h.canvas->undoStack()->count();
        QString error;
        QVERIFY2(panel.pasteText("no\r\n", &error), qPrintable(error));
        QCOMPARE(h.state(first).type, int(nodecoupling2d::None));
        QCOMPARE(h.state(second).type, int(nodecoupling2d::None));
        QCOMPARE(h.canvas->undoStack()->count(), depth + 1);
        h.canvas->undoStack()->undo();
        QVERIFY(!h.state(first).authored);
        QVERIFY(!h.state(second).authored);
        view->clearSelection();
        view->setCurrentIndex(view->model()->index(0, diameter));
        QVERIFY2(panel.pasteText("0.24\t0.75\n0.3\t0.9\n", &error), qPrintable(error));
        QCOMPARE(h.state(first).params.value("D"), QString("0.24"));
        QCOMPARE(h.state(second).params.value("D"), QString("0.3"));
        QCOMPARE(h.state(first).params.value("PERIMETER"), QString("0.75"));
        h.canvas->undoStack()->undo();
        QVERIFY(!h.state(first).authored);
        QVERIFY(!h.state(second).authored);
        // A rejected node-specific value is reported; accepted cells remain undoable.
        QVERIFY(!panel.pasteText("0.24\n-1", &error));
        QVERIFY(error.contains("rejected"));
        QCOMPARE(h.state(first).params.value("D"), QString("0.24"));
        QVERIFY(!h.state(second).authored);
        h.canvas->undoStack()->undo();
        QVERIFY(!h.state(first).authored);
        // The actual keyboard path handles one value and leaves other rows alone.
        view->clearSelection();
        view->setCurrentIndex(view->model()->index(0, flag));
        QApplication::clipboard()->setText("No");
        QTest::keyClick(view, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(h.state(first).type, int(nodecoupling2d::None));
        QVERIFY(h.state(second).type != nodecoupling2d::None);
        QVERIFY(!panel.pasteText("perhaps", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(panel.pasteText("YES", &error));
        QVERIFY(!h.state(first).authored);
        const QString evidence = qEnvironmentVariable("SWMMVIS_COUPLING_QA");
        if (!evidence.isEmpty()) {
            for (int c = 1; c < table->columnCount(); ++c)
                view->setColumnHidden(c, !table->columnSpecs()[c].setter.startsWith("node_2d_"));
            panel.resize(1600, 520);
            panel.show();
            QTest::qWait(50);
            QVERIFY(panel.grab().save(QDir(evidence).filePath("node-coupling-table.png")));
        }
    }

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
            SWMMAttributeTableModel table;
            table.setSource(h.layer, SWMMModelLayer::CatJunctions);
            table.setUndoStack(h.canvas->undoStack());
            const QMap<QString, QString> values = {
                {"D", "0.24"}, {"CHAMBER_AREA", "0.05"}, {"LID_OPEN_FRACTION", "0.8"},
                {"CREST", "MAX"}, {"LAW", "WEIR_ORIFICE"}, {"COEFFS", "RUBINATO2017_LOWER"},
                {"CW", "0.5"}, {"CSW", "0.055"}, {"COI", "0.16"}, {"COO", "0.159"}};
            for (auto it = values.cbegin(); it != values.cend(); ++it)
                QVERIFY2(table.setData(table.index(0, columnFor(table, "node_2d_param_" + it.key())),
                                       it.value()), qPrintable(it.key()));
            j1 = h.state("J1");
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
