/**
 * @file      test_frozengroundeditor.cpp
 * @brief     Frozen-ground rows through the staged model and the dialog: edit, commit, write,
 *            reopen, refusals and rollback.
 * @details   Model files are kept under tests/output/frozen_ground_editor_2026-11-17 for review.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/frozengrounddialog.h"
#include "ui/dialogs/frozengroundrowmodel.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QtTest/QtTest>

using openswmmvis::ui::FrozenGroundDialog;
using openswmmvis::ui::FrozenGroundRowModel;
using Target = FrozenGroundRowModel::Target;

namespace
{

const char *kDeck = R"([OPTIONS]
FLOW_UNITS       CMS
INFILTRATION     HORTON
FLOW_ROUTING     DYNWAVE
START_DATE       01/10/2026
START_TIME       00:00:00
END_DATE         01/10/2026
END_TIME         01:00:00
REPORT_STEP      0:05:00
ROUTING_STEP     5

[RAINGAGES]
RG1  INTENSITY 1:00 1.0 TIMESERIES RAIN

[TEMPERATURE]
TIMESERIES AIR

[SUBCATCHMENTS]
S1  RG1  J1  2.0  0  100  1.0  0
S2  RG1  J1  1.0  20  80  1.0  0

[SUBAREAS]
S1  0.015  0.2  1.0  2.0  0  OUTLET
S2  0.015  0.2  1.0  2.0  0  OUTLET

[INFILTRATION]
S1  80  10  4  7  0
S2  80  10  4  7  0

[TIMESERIES]
RAIN  01/10/2026 00:00 5
AIR   01/10/2026 00:00 -8
AIR   01/10/2026 02:00 -8

[JUNCTIONS]
J1      10    3         0          0         0

[OUTFALLS]
O1      9     FREE  NO

[CONDUITS]
C1      J1    O1  100     0.013      0         0          0

[XSECTIONS]
C1      CIRCULAR  1      0      0      0      1

[2D_OPTIONS]
MAX_TIMESTEP     5
DRY_DEPTH        0.002
REPORT_2D        NO

[2D_VERTICES]
0     0    10
10    0    10.5
10    10   11
0     10   11.5

[2D_TRIANGLES]
0     1   2   0.03   0  PAD
0     2   3   0.045  0  LAWN

[2D_INFILTRATION_DEFAULTS]
*  CONSTANT  10  -  -  -  -

[FROZEN_GROUND]
*   0.2  1.3

[2D_FROZEN_GROUND]
LAWN  0.1  1.5  2.4e6  0.3  8  3  -2
)";

QString outputRoot()
{
    return QStringLiteral(SWMMVIS_FROZEN_GROUND_OUTPUT);
}

struct Engine {
    SWMM_Engine handle = swmm_engine_create();
    Engine() = default;
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    ~Engine()
    {
        if (handle)
            swmm_engine_destroy(handle);
    }
};

bool openModel(Engine &engine, const QString &inp, const QString &outDir)
{
    QDir().mkpath(outDir);
    const QString stem = QDir(outDir).filePath(QFileInfo(inp).completeBaseName());
    return engine.handle &&
           swmm_engine_open(engine.handle, inp.toUtf8().constData(),
                            (stem + ".rpt").toUtf8().constData(),
                            (stem + ".out").toUtf8().constData(), nullptr) == SWMM_OK;
}

QString writeDeck(const QString &name)
{
    const QString dir = QDir(outputRoot()).filePath(name);
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("model.inp"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(kDeck);
    return path;
}

/** Rows of a model as "name|v0|v1|…" in the model's own text. */
QStringList rowsOf(const FrozenGroundRowModel &model)
{
    QStringList rows;
    for (int r = 0; r < model.rowCount(); ++r) {
        QStringList cells;
        for (int c = 0; c < model.columnCount(); ++c)
            cells << model.index(r, c).data().toString();
        rows << cells.join(QLatin1Char('|'));
    }
    return rows;
}

/** Close the next modal message box and report its text. */
void answerNextMessageBox(QString *text)
{
    QTimer::singleShot(0, [text] {
        for (QWidget *w : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(w)) {
                if (text)
                    *text = box->text();
                box->accept();
            }
    });
}

} // namespace

class TestFrozenGroundEditor : public QObject
{
    Q_OBJECT
private slots:
    /** The models read both sections, with the engine's defaults in the omitted columns. */
    void loadsBothSections()
    {
        const QString inp = writeDeck(QStringLiteral("load"));
        Engine engine;
        QVERIFY(openModel(engine, inp, QFileInfo(inp).absolutePath()));
        FrozenGroundRowModel subcatchments(Target::Subcatchments), mesh(Target::Mesh);
        QVERIFY(subcatchments.load(engine.handle).isEmpty());
        QVERIFY(mesh.load(engine.handle).isEmpty());
        QCOMPARE(subcatchments.sectionName(), QStringLiteral("[FROZEN_GROUND]"));
        QCOMPARE(mesh.sectionName(), QStringLiteral("[2D_FROZEN_GROUND]"));
        QCOMPARE(subcatchments.columnCount(), 11);
        QCOMPARE(rowsOf(subcatchments),
                 (QStringList{QStringLiteral("*|0.2|1.3|2400000|0.3|8|3|5|11|0.25|250")}));
        QCOMPARE(rowsOf(mesh),
                 (QStringList{QStringLiteral("LAWN|0.1|1.5|2400000|0.3|8|3|-2|11|0.25|250")}));
        QCOMPARE(subcatchments.names(), (QStringList{QStringLiteral("S1"), QStringLiteral("S2")}));
        QCOMPARE(mesh.names(), (QStringList{QStringLiteral("LAWN"), QStringLiteral("PAD")}));
        QVERIFY(!subcatchments.isDirty());
        // The name column offers * and the known names.
        QCOMPARE(subcatchments.index(0, 0).data(FrozenGroundRowModel::ChoicesRole).toStringList(),
                 (QStringList{QStringLiteral("*"), QStringLiteral("S1"), QStringLiteral("S2")}));
    }

    /** Edit, add and remove rows; commit; write the model; reopen: the rows are the same. */
    void editsWriteAndReopen()
    {
        const QString inp = writeDeck(QStringLiteral("roundtrip"));
        const QString dir = QFileInfo(inp).absolutePath();
        Engine engine;
        QVERIFY(openModel(engine, inp, dir));
        FrozenGroundRowModel subcatchments(Target::Subcatchments), mesh(Target::Mesh);
        QVERIFY(subcatchments.load(engine.handle).isEmpty());
        QVERIFY(mesh.load(engine.handle).isEmpty());

        // A new row takes the first name without one, with the default values.
        const int added = subcatchments.appendRow();
        QCOMPARE(added, 1);
        QCOMPARE(subcatchments.index(added, 0).data().toString(), QStringLiteral("S1"));
        QVERIFY(subcatchments.setData(subcatchments.index(added, 7), QStringLiteral("-4")));
        QVERIFY(subcatchments.setData(subcatchments.index(added, 1),
                                      QStringLiteral("0.123456789012345")));
        QVERIFY(!subcatchments.setData(subcatchments.index(added, 1), QStringLiteral("abc")));
        QVERIFY(subcatchments.isDirty());
        // The mesh keeps its tag row and gains a default row; then the tag row is renamed.
        const int star = mesh.appendRow();
        QCOMPARE(mesh.index(star, 0).data().toString(), QStringLiteral("*"));
        QVERIFY(mesh.setData(mesh.index(0, 0), QStringLiteral("PAD")));

        for (auto *m : {&subcatchments, &mesh}) {
            QVERIFY2(m->validateCommit(engine.handle).isEmpty(), qPrintable(m->sectionName()));
            const QString error = m->commit(engine.handle);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QVERIFY(!m->isDirty());
        }
        // The engine holds what the model holds.
        int count = 0;
        QCOMPARE(swmm_frozen_ground_count(engine.handle, 0, &count), SWMM_OK);
        QCOMPARE(count, 2);
        double values[10];
        char name[64];
        QCOMPARE(swmm_frozen_ground_get(engine.handle, 0, 1, name, sizeof name, values), SWMM_OK);
        QCOMPARE(QString::fromUtf8(name), QStringLiteral("S1"));
        QCOMPARE(values[0], 0.123456789012345);
        QCOMPARE(values[6], -4.0);

        const QString saved = QDir(dir).filePath(QStringLiteral("saved.inp"));
        QCOMPARE(swmm_model_write(engine.handle, saved.toUtf8().constData()), SWMM_OK);
        Engine reopened;
        QVERIFY(openModel(reopened, saved, QDir(dir).filePath(QStringLiteral("reopened"))));
        FrozenGroundRowModel subcatchments2(Target::Subcatchments), mesh2(Target::Mesh);
        QVERIFY(subcatchments2.load(reopened.handle).isEmpty());
        QVERIFY(mesh2.load(reopened.handle).isEmpty());
        QCOMPARE(rowsOf(subcatchments2), rowsOf(subcatchments));
        QCOMPARE(rowsOf(mesh2), rowsOf(mesh));
        QCOMPARE(rowsOf(mesh2).size(), 2);
        QVERIFY(rowsOf(mesh2).first().startsWith(QStringLiteral("PAD|")));

        // The authored row takes effect: S1 starts frozen, S2 uses the unfrozen default row.
        QCOMPARE(swmm_engine_initialize(reopened.handle), SWMM_OK);
        int active = 0;
        double frozen = -1, factor = -1;
        QCOMPARE(swmm_subcatch_get_frozen_ground(reopened.handle, 0, &active, nullptr, &frozen,
                                                 &factor),
                 SWMM_OK);
        QCOMPARE(active, 1);
        QCOMPARE(frozen, 1.0);
        QCOMPARE(factor, 0.0);
        QCOMPARE(swmm_subcatch_get_frozen_ground(reopened.handle, 1, &active, nullptr, &frozen,
                                                 &factor),
                 SWMM_OK);
        QCOMPARE(active, 1);
        QCOMPARE(frozen, 0.0);
    }

    /** Bad drafts are refused with their reason and nothing is written. */
    void refusesBadRows()
    {
        const QString inp = writeDeck(QStringLiteral("refusals"));
        Engine engine;
        QVERIFY(openModel(engine, inp, QFileInfo(inp).absolutePath()));
        FrozenGroundRowModel model(Target::Subcatchments), mesh(Target::Mesh);
        QVERIFY(model.load(engine.handle).isEmpty());
        QVERIFY(mesh.load(engine.handle).isEmpty());
        const QStringList before = rowsOf(model);

        QVERIFY(model.setData(model.index(0, 1), QStringLiteral("-1")));
        QVERIFY(model.validateCommit(engine.handle).contains(QStringLiteral("thickness")));
        QVERIFY(model.setData(model.index(0, 1), QStringLiteral("0.2")));
        QVERIFY(model.setData(model.index(0, 6), QStringLiteral("0.05")));
        QVERIFY(model.validateCommit(engine.handle).contains(QStringLiteral("deep depth")));
        QVERIFY(model.setData(model.index(0, 6), QStringLiteral("3")));
        QVERIFY(!model.isDirty());

        // An unknown name and a duplicate name.
        QVERIFY(model.setData(model.index(0, 0), QStringLiteral("NOPE")));
        QVERIFY(model.validateCommit(engine.handle).contains(QStringLiteral("NOPE")));
        QVERIFY(model.setData(model.index(0, 0), QStringLiteral("*")));
        const int row = model.appendRow();
        QVERIFY(model.setData(model.index(row, 0), QStringLiteral("*")));
        QVERIFY(model.validateCommit(engine.handle).contains(QStringLiteral("more than one")));
        QVERIFY(mesh.setData(mesh.index(0, 0), QStringLiteral("S1")));
        QVERIFY(mesh.validateCommit(engine.handle).contains(QStringLiteral("tag")));

        // The engine was never written.
        FrozenGroundRowModel fresh(Target::Subcatchments);
        QVERIFY(fresh.load(engine.handle).isEmpty());
        QCOMPARE(rowsOf(fresh), before);

        // A second editor that reads stale rows is refused.
        FrozenGroundRowModel first(Target::Subcatchments), second(Target::Subcatchments);
        QVERIFY(first.load(engine.handle).isEmpty());
        QVERIFY(second.load(engine.handle).isEmpty());
        QVERIFY(first.setData(first.index(0, 1), QStringLiteral("0.3")));
        QVERIFY(first.commit(engine.handle).isEmpty());
        QVERIFY(second.setData(second.index(0, 1), QStringLiteral("0.4")));
        QVERIFY(second.validateCommit(engine.handle).contains(QStringLiteral("changed")));
    }

    /** Every name taken: there is nothing left to add. */
    void appendStopsWhenEveryNameHasARow()
    {
        const QString inp = writeDeck(QStringLiteral("append"));
        Engine engine;
        QVERIFY(openModel(engine, inp, QFileInfo(inp).absolutePath()));
        FrozenGroundRowModel model(Target::Subcatchments);
        QVERIFY(model.load(engine.handle).isEmpty());
        QCOMPARE(model.appendRow(), 1); // S1
        QCOMPARE(model.appendRow(), 2); // S2
        QCOMPARE(model.appendRow(), -1);
        QVERIFY(model.removeRows(0, 1)); // the * row
        QCOMPARE(model.appendRow(), 2);
        QCOMPARE(model.index(2, 0).data().toString(), QStringLiteral("*"));
    }

    /** The dialog stages edits on both pages and writes them together on Apply. */
    void dialogAppliesBothPages()
    {
        const QString inp = writeDeck(QStringLiteral("dialog"));
        Engine engine;
        QVERIFY(openModel(engine, inp, QFileInfo(inp).absolutePath()));
        FrozenGroundDialog dialog(engine.handle);
        QSignalSpy may(&dialog, &FrozenGroundDialog::changesMayHaveBeenApplied);
        QSignalSpy applied(&dialog, &FrozenGroundDialog::applied);
        auto *subcatchments = dialog.model(FrozenGroundDialog::SubcatchmentPage);
        auto *mesh = dialog.model(FrozenGroundDialog::MeshPage);
        QVERIFY(subcatchments && mesh);
        QCOMPARE(dialog.findChild<QTabWidget *>()->count(), 2);

        // Nothing changed: Apply succeeds without writing.
        QVERIFY(dialog.applyChanges());
        QCOMPARE(may.count(), 0);

        auto *add = dialog.findChild<QPushButton *>(
            QStringLiteral("frozenGroundAddSubcatchmentRow"));
        QVERIFY(add);
        add->click();
        QCOMPARE(subcatchments->rowCount(), 2);
        QVERIFY(mesh->setData(mesh->index(0, 4), QStringLiteral("0.25")));
        QVERIFY(dialog.applyChanges());
        QCOMPARE(may.count(), 1);
        QCOMPARE(applied.count(), 1);
        QVERIFY(!subcatchments->isDirty());
        QVERIFY(!mesh->isDirty());
        int count = 0;
        QCOMPARE(swmm_frozen_ground_count(engine.handle, 0, &count), SWMM_OK);
        QCOMPARE(count, 2);
        double values[10];
        QCOMPARE(swmm_frozen_ground_get(engine.handle, 1, 0, nullptr, 0, values), SWMM_OK);
        QCOMPARE(values[3], 0.25);

        // A bad draft is refused with a message and neither page is written.
        QVERIFY(subcatchments->setData(subcatchments->index(0, 1), QStringLiteral("-5")));
        QVERIFY(mesh->setData(mesh->index(0, 4), QStringLiteral("0.5")));
        QString message;
        answerNextMessageBox(&message);
        QVERIFY(!dialog.applyChanges());
        QVERIFY(message.contains(QStringLiteral("thickness")));
        QCOMPARE(applied.count(), 1);
        QCOMPARE(swmm_frozen_ground_get(engine.handle, 1, 0, nullptr, 0, values), SWMM_OK);
        QCOMPARE(values[3], 0.25);
        QVERIFY(subcatchments->isDirty()); // the draft stays for correction
    }

    /** A running model opens read-only. */
    void dialogIsReadOnlyWhileRunning()
    {
        const QString inp = writeDeck(QStringLiteral("running"));
        Engine engine;
        QVERIFY(openModel(engine, inp, QFileInfo(inp).absolutePath()));
        QCOMPARE(swmm_engine_initialize(engine.handle), SWMM_OK);
        QCOMPARE(swmm_engine_start(engine.handle, 0), SWMM_OK);
        FrozenGroundDialog dialog(engine.handle);
        QVERIFY(!dialog.applyChanges());
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QVERIFY(!buttons->button(QDialogButtonBox::Apply));
        QVERIFY(buttons->button(QDialogButtonBox::Close));
        for (auto *view : dialog.findChildren<QTableView *>())
            QCOMPARE(view->editTriggers(), QAbstractItemView::NoEditTriggers);
        auto *add = dialog.findChild<QPushButton *>(
            QStringLiteral("frozenGroundAddSubcatchmentRow"));
        QVERIFY(add && !add->isEnabled());
    }
};

QTEST_MAIN(TestFrozenGroundEditor)
#include "test_frozengroundeditor.moc"
