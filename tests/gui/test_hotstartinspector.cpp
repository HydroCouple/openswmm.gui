// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      test_hotstartinspector.cpp
 * @brief     Hot-start inspector (review G-09): OpenSWMM and SWMM 5 files, units, edits written
 *            and read back, and the window.
 * @details   The fixture model (tests/gui/data/hotstart_inspector/) is run once; its two
 *            hot-start files and every edited copy are kept under
 *            tests/output/hotstart_inspector_g09/ for review.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "simulation/hotstartdocument.h"
#include "ui/dialogs/hotstartinspectordialog.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>

#include <cmath>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_hotstart.h>

using openswmmvis::simulation::HotStartDocument;
using openswmmvis::simulation::HotStartUnits;
using openswmmvis::ui::HotStartInspectorDialog;
using openswmmvis::ui::HotStartRecordModel;
using Kind = HotStartRecordModel::Kind;

namespace
{
QString outputRoot()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .absoluteFilePath(QStringLiteral("../../output/hotstart_inspector_g09"));
}

QString fixture()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .absoluteFilePath(QStringLiteral("hotstart_inspector/hotstart_model.inp"));
}

QByteArray bytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/** The model as the GUI holds it (lenient open, not run). */
struct LiveEngine {
    SWMM_Engine engine = swmm_engine_create();
    explicit LiveEngine(const QString &inp)
    {
        swmm_engine_set_lenient_open(engine, 1);
        const QString stem = inp.left(inp.size() - 4);
        swmm_engine_open(engine, inp.toUtf8().constData(), (stem + ".live.rpt").toUtf8().constData(),
                         (stem + ".live.out").toUtf8().constData(), nullptr);
    }
    ~LiveEngine()
    {
        swmm_engine_close(engine);
        swmm_engine_destroy(engine);
    }
};

/** Fresh copy of a run file under output/<name>/. */
QString copyOf(const QString &file, const QString &name)
{
    const QString dir = outputRoot() + QLatin1Char('/') + name;
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const QString copy = dir + QLatin1Char('/') + QFileInfo(file).fileName();
    return QFile::copy(file, copy) ? copy : QString();
}
} // namespace

class TestHotStartInspector : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void unitsFollowTheModelsFlowUnits();
    void readsOpenSwmmAndSwmm5Files();
    void editsAreWrittenAndReadBack();
    void refusedOrInvalidEditsChangeNothing();
    void windowShowsTheFileAndGoesToObjects();

private:
    QString m_run;    ///< the run's model copy
    QString m_native; ///< OpenSWMM hot-start file the run saved
    QString m_legacy; ///< SWMM 5 hot-start file the run saved
};

void TestHotStartInspector::initTestCase()
{
    const QString dir = outputRoot() + QStringLiteral("/run");
    QDir(dir).removeRecursively();
    QVERIFY(QDir().mkpath(dir));
    m_run = dir + QStringLiteral("/hotstart_model.inp");
    QVERIFY(QFile::copy(fixture(), m_run));
    SWMM_Engine e = swmm_engine_create();
    QCOMPARE(swmm_engine_open(e, m_run.toUtf8().constData(),
                              (dir + QStringLiteral("/hotstart_model.rpt")).toUtf8().constData(),
                              (dir + QStringLiteral("/hotstart_model.out")).toUtf8().constData(),
                              nullptr),
             SWMM_OK);
    QCOMPARE(swmm_engine_initialize(e), SWMM_OK);
    QCOMPARE(swmm_engine_start(e, 1), SWMM_OK);
    double elapsed = 0;
    int rc = SWMM_OK;
    do
        rc = swmm_engine_step(e, &elapsed);
    while (rc == SWMM_OK && elapsed > 0);
    QCOMPARE(rc, SWMM_OK);
    QCOMPARE(swmm_engine_end(e), SWMM_OK);
    swmm_engine_close(e);
    swmm_engine_destroy(e);
    m_native = dir + QStringLiteral("/hs_native.hsf");
    m_legacy = dir + QStringLiteral("/hs_legacy.hsf");
    QVERIFY(bytes(m_native).startsWith("OPENSWMM_HS_V1"));
    QVERIFY(bytes(m_legacy).startsWith("SWMM5-HOTSTART"));
}

void TestHotStartInspector::unitsFollowTheModelsFlowUnits()
{
    const auto us = HotStartUnits::forEngine(nullptr);
    QCOMPARE(us.flowLabel, QStringLiteral("CFS"));
    QCOMPARE(us.length, 1.0);

    const QString dir = outputRoot() + QStringLiteral("/si");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    QByteArray text = bytes(fixture());
    text.replace("FLOW_UNITS           CFS", "FLOW_UNITS           LPS");
    QFile si(dir + QStringLiteral("/si_model.inp"));
    QVERIFY(si.open(QIODevice::WriteOnly) && si.write(text) == text.size());
    si.close();
    LiveEngine live(si.fileName());
    const auto units = HotStartUnits::forEngine(live.engine);
    QCOMPARE(units.flowLabel, QStringLiteral("LPS"));
    QCOMPARE(units.lengthLabel, QStringLiteral("m"));
    QCOMPARE(units.length, 0.3048);
    QCOMPARE(units.flow, 28.31685);

    // Shown in meters, edited in meters, stored in feet.
    const QString copy = copyOf(m_native, QStringLiteral("si_edit"));
    HotStartDocument document;
    QString error;
    QVERIFY2(document.open(copy, live.engine, &error), qPrintable(error));
    HotStartRecordModel nodes(Kind::Nodes);
    nodes.setDocument(&document, units);
    const int depth = nodes.columnOf(QStringLiteral("depth"));
    QCOMPARE(nodes.headerData(depth, Qt::Horizontal, Qt::DisplayRole).toString(),
             QStringLiteral("Depth (m)"));
    const double feet = document.nodes().first().depth;
    QCOMPARE(nodes.index(0, depth).data().toString(),
             QLocale::c().toString(feet * 0.3048, 'g', 10));
    QVERIFY(nodes.setData(nodes.index(0, depth), QStringLiteral("0.6096"), Qt::EditRole));
    QCOMPARE(nodes.nodeEdits().value(document.nodes().first().id), 0.6096 / 0.3048);
}

void TestHotStartInspector::readsOpenSwmmAndSwmm5Files()
{
    LiveEngine live(m_run);
    HotStartDocument native, legacy;
    QString error;
    QVERIFY2(native.open(m_native, live.engine, &error), qPrintable(error));
    QVERIFY2(legacy.open(m_legacy, live.engine, &error), qPrintable(error));

    QCOMPARE(native.info().format, HotStartDocument::Format::Native);
    QVERIFY(native.info().version >= 4);
    QVERIFY(native.info().savedAt > 0);
    QVERIFY(native.info().elapsedSeconds > 7100.0); // saved at the 2 h end of the run
    QVERIFY(native.isEditable());
    QCOMPARE(legacy.info().format, HotStartDocument::Format::Legacy);
    QCOMPARE(legacy.info().version, 4);
    QVERIFY(legacy.info().contents & SWMM_HOTSTART_LEGACY);
    QVERIFY(!legacy.isEditable());

    for (const auto *doc : {&native, &legacy}) {
        QCOMPARE(doc->nodes().size(), 3);
        QCOMPARE(doc->links().size(), 2);
        QCOMPARE(doc->species(), QStringList{QStringLiteral("TSS")});
        QCOMPARE(doc->modelNodesMissing(), 0);
        QCOMPARE(doc->modelLinksMissing(), 0);
        for (const auto &node : doc->nodes())
            QVERIFY2(node.inModel, qPrintable(node.id));
    }
    // Same end state: the SWMM 5 file holds it in single precision.
    for (int i = 0; i < 3; ++i) {
        const auto &a = native.nodes().at(i);
        const auto &b = legacy.nodes().at(i);
        QCOMPARE(a.id, b.id);
        QVERIFY2(std::abs(a.depth - b.depth) <= 1e-6 * std::max(1.0, a.depth), qPrintable(a.id));
        QVERIFY(std::isnan(b.head) && std::isnan(b.volume)); // not stored in SWMM 5 files
        QVERIFY(std::isfinite(a.head));
        QVERIFY(std::abs(a.species.first() - b.species.first()) <= 1e-4 * a.species.first());
    }
    const auto &c1 = native.links().first();
    QCOMPARE(c1.id, QStringLiteral("C1"));
    QVERIFY2(std::abs(c1.flow - 1.5) < 0.01, qPrintable(QString::number(c1.flow)));
    QVERIFY(std::abs(c1.flow - legacy.links().first().flow) <= 1e-6);
    QVERIFY(native.links().first().species.first() > 24.0);

    // A SWMM 5 file needs the model it was written for.
    HotStartDocument orphan;
    QVERIFY(!orphan.open(m_legacy, nullptr, &error));
    QVERIFY2(error.contains(QStringLiteral("Open that model first")), qPrintable(error));
    QVERIFY(!orphan.open(fixture(), live.engine, &error));
    QCOMPARE(error, QStringLiteral("This is not a hot-start file."));
}

void TestHotStartInspector::editsAreWrittenAndReadBack()
{
    LiveEngine live(m_run);
    const QString copy = copyOf(m_native, QStringLiteral("edits"));
    HotStartInspectorDialog dialog([&] { return live.engine; });
    QStringList questions;
    dialog.setConfirm([&](const QString &question) {
        questions << question;
        return true;
    });
    QVERIFY(dialog.openFile(copy));
    auto *nodes = dialog.model(Kind::Nodes);
    auto *links = dialog.model(Kind::Links);
    QAbstractItemModelTester nodeTester(nodes, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QAbstractItemModelTester linkTester(links, QAbstractItemModelTester::FailureReportingMode::QtTest);
    auto *save = dialog.findChild<QPushButton *>(QStringLiteral("hotstartSave"));
    QVERIFY(save && !save->isEnabled());

    const int depth = nodes->columnOf(QStringLiteral("depth"));
    const int head = nodes->columnOf(QStringLiteral("head"));
    const int flow = links->columnOf(QStringLiteral("flow"));
    QVERIFY(nodes->flags(nodes->index(0, depth)) & Qt::ItemIsEditable);
    QVERIFY(!(nodes->flags(nodes->index(0, head)) & Qt::ItemIsEditable));
    QVERIFY(nodes->setData(nodes->index(0, depth), QStringLiteral("1.25"), Qt::EditRole));
    QCOMPARE(nodes->index(0, head).data().toString(), QStringLiteral("set at restart"));
    QVERIFY(nodes->index(0, depth).data(Qt::FontRole).value<QFont>().bold());
    QVERIFY(links->setData(links->index(0, flow), QStringLiteral("2"), Qt::EditRole));
    QVERIFY(save->isEnabled());

    QVERIFY(dialog.save());
    QCOMPARE(questions.size(), 1);
    QVERIFY2(questions.first().contains(QStringLiteral("2 objects")), qPrintable(questions.first()));
    QVERIFY(!save->isEnabled()); // nothing staged after the save
    QVERIFY(dialog.noticeText().startsWith(QStringLiteral("Saved.")));
    QVERIFY(!QFile::exists(copy + QStringLiteral(".inspector-edit")));

    HotStartDocument reread;
    QString error;
    QVERIFY2(reread.open(copy, live.engine, &error), qPrintable(error));
    const auto &j1 = reread.nodes().first();
    QCOMPARE(j1.depth, 1.25);
    QVERIFY(std::isnan(j1.head) && std::isnan(j1.volume)); // derived when a run applies the file
    QVERIFY(std::isfinite(reread.nodes().at(1).head));      // untouched record
    const auto &c1 = reread.links().first();
    QCOMPARE(c1.flow, 2.0);
    QVERIFY(std::isnan(c1.volume));
}

void TestHotStartInspector::refusedOrInvalidEditsChangeNothing()
{
    LiveEngine live(m_run);
    const QString copy = copyOf(m_native, QStringLiteral("refused"));
    const QByteArray before = bytes(copy);
    HotStartInspectorDialog dialog([&] { return live.engine; });
    dialog.setConfirm([](const QString &) { return false; });
    QVERIFY(dialog.openFile(copy));
    auto *nodes = dialog.model(Kind::Nodes);
    const int depth = nodes->columnOf(QStringLiteral("depth"));
    QVERIFY(!nodes->setData(nodes->index(0, depth), QStringLiteral("-1"), Qt::EditRole));
    QVERIFY(!nodes->setData(nodes->index(0, depth), QStringLiteral("deep"), Qt::EditRole));
    QVERIFY(!nodes->isDirty());
    QVERIFY(nodes->setData(nodes->index(0, depth), QStringLiteral("3"), Qt::EditRole));
    QVERIFY(!dialog.save()); // the user said no
    QCOMPARE(bytes(copy), before);
    dialog.revert();
    QVERIFY(!nodes->isDirty());
    // Setting a value back to the stored one drops the edit.
    const QString stored = nodes->index(0, depth).data(Qt::EditRole).toString();
    QVERIFY(nodes->setData(nodes->index(0, depth), QStringLiteral("3"), Qt::EditRole));
    QVERIFY(nodes->setData(nodes->index(0, depth), stored, Qt::EditRole));
    QVERIFY(!nodes->isDirty());

    // A SWMM 5 file is read-only.
    QVERIFY(dialog.openFile(m_legacy));
    QVERIFY(!(nodes->flags(nodes->index(0, depth)) & Qt::ItemIsEditable));
    QVERIFY(!nodes->setData(nodes->index(0, depth), QStringLiteral("3"), Qt::EditRole));
    QVERIFY2(dialog.noticeText().contains(QStringLiteral("read-only")),
             qPrintable(dialog.noticeText()));
    HotStartDocument legacy;
    QString error;
    QVERIFY(legacy.open(m_legacy, live.engine, &error));
    QVERIFY(!legacy.save({{QStringLiteral("J1"), 1.0}}, {}, live.engine, &error));
    QVERIFY(error.contains(QStringLiteral("read-only")));

    // A file that cannot be read keeps the previous one.
    QVERIFY(!dialog.openFile(outputRoot() + QStringLiteral("/no_such_file.hsf")));
    QVERIFY(dialog.noticeText().contains(QStringLiteral("could not be opened")));
    QCOMPARE(dialog.document().path(), m_legacy);
}

void TestHotStartInspector::windowShowsTheFileAndGoesToObjects()
{
    LiveEngine live(m_run);
    HotStartInspectorDialog dialog([&] { return live.engine; });
    QVERIFY(dialog.openFile(m_native));
    QVERIFY2(dialog.infoText(QStringLiteral("format")).startsWith(QStringLiteral("OpenSWMM")),
             qPrintable(dialog.infoText(QStringLiteral("format"))));
    QCOMPARE(dialog.infoText(QStringLiteral("start")), QStringLiteral("2024-01-01 00:00:00"));
    QVERIFY2(dialog.infoText(QStringLiteral("time")).startsWith(QStringLiteral("2024-01-01 02:00")),
             qPrintable(dialog.infoText(QStringLiteral("time"))));
    QVERIFY(dialog.infoText(QStringLiteral("contents")).contains(QStringLiteral("TSS")));
    auto *nodes = dialog.model(Kind::Nodes);
    QCOMPARE(nodes->headerData(nodes->columnCount() - 1, Qt::Horizontal, Qt::DisplayRole).toString(),
             QStringLiteral("TSS"));

    QSignalSpy activated(&dialog, &HotStartInspectorDialog::objectActivated);
    auto *view = dialog.findChild<QTableView *>(QStringLiteral("hotstartTable_nodes"));
    QVERIFY(view && !view->accessibleName().isEmpty());
    view->setCurrentIndex(nodes->index(1, HotStartRecordModel::IdColumn));
    QTest::keyClick(view, Qt::Key_Return);
    QCOMPARE(activated.size(), 1);
    const auto ref = activated.first().at(0).value<SWMMObjectRef>();
    QCOMPARE(ref.objectType, SWMMObjectRef::Node);
    QCOMPARE(ref.name, nodes->index(1, 0).data().toString());
}

QTEST_MAIN(TestHotStartInspector)
#include "test_hotstartinspector.moc"
