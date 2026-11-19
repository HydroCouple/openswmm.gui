/**
 * @file      test_gwqualityroweditors.cpp
 * @brief     Phase 3a (2026-10-30): model-level round trips for the mesh surface-quality and
 *            groundwater-transport row editors.
 *
 * Each test opens a small deck in the engine, loads the editor model, edits, commits, writes the
 * model with swmm_model_write, checks the section rows in the written file, then reopens that
 * file in a fresh engine and checks the model again. Every file lands under
 * SWMMVIS_QUALITY_EDITORS_OUTPUT (tests/output/gw_quality_editors_2026-10-30) for review. The
 * Bellinge groundwater-quality example is opened read-only (report/output files go to the same
 * output folder).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/mesh2daquifermodel.h"
#include "ui/dialogs/mesh2dgwsourcemodel.h"
#include "ui/dialogs/mesh2dsectionrowmodel.h"
#include "ui/dialogs/mesh2dsurfacequalitydialog.h"
#include "ui/dialogs/mesh2dboundaryqualitymodel.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_gw_transport.h>
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_sq2d.h>

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QTimer>
#include <QtTest/QtTest>

using openswmmvis::ui::Mesh2DAquiferNodeModel;
using openswmmvis::ui::Mesh2DGwSourceModel;
using openswmmvis::ui::Mesh2DGwSourceTermModel;
using openswmmvis::ui::Mesh2DSectionRowModel;
using openswmmvis::ui::Mesh2DSurfaceQualityDialog;
using openswmmvis::ui::Mesh2DBoundaryQualityModel;
using Section = Mesh2DSectionRowModel::Section;

namespace
{

const char *kDeck = R"([OPTIONS]
FLOW_UNITS       CMS
INFILTRATION     HORTON
FLOW_ROUTING     DYNWAVE
START_DATE       01/01/2026
START_TIME       00:00:00
END_DATE         01/01/2026
END_TIME         01:00:00
REPORT_STEP      0:05:00
ROUTING_STEP     5

[JUNCTIONS]
J1      10    3         0          0         0

[OUTFALLS]
O1      9     FREE  NO

[CONDUITS]
C1      J1    O1  100     0.013      0         0          0

[XSECTIONS]
C1      CIRCULAR  1      0      0      0      1

[POLLUTANTS]
TSS     MG/L   0  0  0  0  NO  *  0  0  0
Tracer  MG/L   0  0  0  0  NO  *  0  0  0

[LANDUSES]
Urban   0  0  0
Grass   0  0  0

[BUILDUP]
Urban   TSS  NONE  0  0  0  AREA
Grass   TSS  NONE  0  0  0  AREA

[WASHOFF]
Urban   TSS  EXP  0.1  1.0  0  0
Grass   TSS  EXP  0.1  1.0  0  0

[TIMESERIES]
Pump    0:00  0.001
Pump    1:00  0.002

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

[2D_COVERAGES]
*         Grass  100
TAG PAD   Urban  60  Grass  40

[2D_LOADINGS]
TAG PAD   TSS  50

[2D_CURB_LENGTH]
TAG PAD   12.5

[2D_INITIAL_QUALITY]
*         Tracer  1.5

[2D_AQUIFER_OPTIONS]
NODE_ENROLMENT ROWS

[2D_AQUIFER]
*   10  5  0.40  0.05  2

[2D_AQUIFER_LINKS]
C1  KC 50 DC 2.5

[GW_TRANSPORT_PARAMS]
*   2600  - - - 2

[GW_SORPTION]
*   TSS  0.5

[GW_INITIAL_QUALITY]
*   SAT  Tracer  2

[GW_SOURCES]
Well1  CELL 2  FLOW 0.0005  Tracer CONC 100
Spot   XY 2 8  FLOW Pump  SCALE 2  TSS MASS 3
)";

QString outputRoot()
{
    return QStringLiteral(SWMMVIS_QUALITY_EDITORS_OUTPUT);
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

/** Open @p inp with report/output beside it (never next to an input we must not touch). */
bool openModel(Engine &engine, const QString &inp, const QString &outDir)
{
    QDir().mkpath(outDir);
    const QString stem = QDir(outDir).filePath(QFileInfo(inp).completeBaseName());
    return engine.handle &&
           swmm_engine_open(engine.handle, inp.toUtf8().constData(),
                            (stem + ".rpt").toUtf8().constData(),
                            (stem + ".out").toUtf8().constData(), nullptr) == SWMM_OK;
}

/** Write the fixture deck into its own reviewable folder and return its path. */
QString writeDeck(const QString &name, const QByteArray &deck = kDeck)
{
    const QString dir = QDir(outputRoot()).filePath(name);
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("model.inp"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(deck);
    return path;
}

/** Non-comment rows of one section in a written file, as whitespace-separated tokens. */
QList<QStringList> sectionRows(const QString &path, const QString &section)
{
    QList<QStringList> rows;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return rows;
    bool inside = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).simplified();
        if (line.startsWith(QLatin1Char('['))) {
            inside = line.compare(section, Qt::CaseInsensitive) == 0;
            continue;
        }
        if (inside && !line.isEmpty() && !line.startsWith(QLatin1Char(';')))
            rows.append(line.split(QLatin1Char(' ')));
    }
    return rows;
}

/** Token-wise match; numeric tokens compare as exact doubles (the writer prints 17 digits). */
bool sameTokens(const QStringList &actual, const QStringList &expected)
{
    if (actual.size() != expected.size())
        return false;
    for (int i = 0; i < actual.size(); ++i) {
        bool a = false, b = false;
        const double x = actual[i].toDouble(&a), y = expected[i].toDouble(&b);
        if (a && b ? x != y : actual[i].compare(expected[i], Qt::CaseInsensitive) != 0)
            return false;
    }
    return true;
}

/** Fail with both row lists printed when the section's rows differ. */
bool sectionIs(const QString &path, const QString &section, const QList<QStringList> &expected)
{
    const auto rows = sectionRows(path, section);
    bool same = rows.size() == expected.size();
    for (int i = 0; same && i < rows.size(); ++i)
        same = sameTokens(rows[i], expected[i]);
    if (!same) {
        qWarning().noquote() << section << "in" << path;
        for (const auto &row : rows)
            qWarning().noquote() << "  actual  :" << row.join(QLatin1Char(' '));
        for (const auto &row : expected)
            qWarning().noquote() << "  expected:" << row.join(QLatin1Char(' '));
    }
    return same;
}

/** Display text of a model cell. */
QString cell(const QAbstractItemModel &model, int row, int column)
{
    return model.data(model.index(row, column), Qt::EditRole).toString();
}

/** Every displayed row of a section model as one space-joined string per row. */
QStringList rowsOf(const QAbstractItemModel &model)
{
    QStringList rows;
    for (int r = 0; r < model.rowCount(); ++r) {
        QStringList cells;
        for (int c = 0; c < model.columnCount(); ++c)
            cells << model.data(model.index(r, c), Qt::EditRole).toString();
        rows << cells.join(QLatin1Char('|'));
    }
    return rows;
}

/** Column index by header text. */
int column(const QAbstractItemModel &model, const QString &header)
{
    for (int c = 0; c < model.columnCount(); ++c)
        if (model.headerData(c, Qt::Horizontal).toString().startsWith(header))
            return c;
    return -1;
}

bool set(QAbstractItemModel &model, int row, int col, const QVariant &value)
{
    return model.setData(model.index(row, col), value, Qt::EditRole);
}

} // namespace

class TestGwQualityRowEditors : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief [2D_COVERAGES], [2D_LOADINGS], [2D_CURB_LENGTH] and [2D_INITIAL_QUALITY]: load the
     * authored rows, edit/add/remove, commit, write, check the file, reopen and check again.
     */
    void surfaceSectionsWriteAndReopen()
    {
        const QString inp = writeDeck(QStringLiteral("surface"));
        const QString dir = QFileInfo(inp).absolutePath();
        Engine engine;
        QVERIFY(openModel(engine, inp, dir));

        Mesh2DSectionRowModel coverages(Section::Coverages), loadings(Section::Loadings),
            curb(Section::CurbLength), quality(Section::SurfaceInitialQuality);
        for (auto *m : {&coverages, &loadings, &curb, &quality})
            QVERIFY2(m->load(engine.handle).isEmpty(), qPrintable(m->sectionName()));

        // Coverages: one percent column per land use, in [LANDUSES] order.
        const int urban = column(coverages, QStringLiteral("Urban"));
        const int grass = column(coverages, QStringLiteral("Grass"));
        QCOMPARE(coverages.columnCount(), 4);
        QCOMPARE(rowsOf(coverages), (QStringList{QStringLiteral("All cells|||100"),
                                                 QStringLiteral("Tag|PAD|60|40")}));
        QCOMPARE(rowsOf(loadings), QStringList{QStringLiteral("Tag|PAD|TSS|50")});
        QCOMPARE(rowsOf(curb), QStringList{QStringLiteral("Tag|PAD|12.5")});
        QCOMPARE(rowsOf(quality), QStringList{QStringLiteral("All cells||Tracer|1.5")});
        QVERIFY(!coverages.isDirty());

        // A row's percents may not exceed 100, and two rows may not share a scope.
        QVERIFY(set(coverages, 0, urban, QStringLiteral("30")));
        QVERIFY(coverages.validateCommit(engine.handle).contains(QStringLiteral("130")));
        QVERIFY(set(coverages, 0, grass, QStringLiteral("70")));
        int added = coverages.appendRow();
        QVERIFY(coverages.validateCommit(engine.handle).contains(QStringLiteral("same scope")));
        QVERIFY(set(coverages, added, 0, 2));
        QVERIFY(set(coverages, added, 1, QStringLiteral("2")));
        QVERIFY(set(coverages, added, grass, QString())); // blank = not in this row
        QVERIFY(set(coverages, added, urban, QStringLiteral("100")));
        QVERIFY(!set(coverages, added, urban, QStringLiteral("101")));
        QVERIFY(!set(coverages, added, 1, QStringLiteral("0"))); // cells are 1-based

        QVERIFY(set(loadings, 0, 3, QStringLiteral("75")));
        added = loadings.appendRow();
        QVERIFY(set(loadings, added, 2, QStringLiteral("Tracer")));
        QVERIFY(set(loadings, added, 3, QStringLiteral("5")));
        QVERIFY(!set(loadings, added, 3, QStringLiteral("-1")));

        QVERIFY(curb.removeRow(0));
        added = curb.appendRow();
        QVERIFY(set(curb, added, 0, 2));
        QVERIFY(set(curb, added, 1, QStringLiteral("1")));
        QVERIFY(set(curb, added, 2, QStringLiteral("3.25")));

        added = quality.appendRow();
        QVERIFY(set(quality, added, 0, 1));
        QVERIFY(set(quality, added, 1, QStringLiteral("LAWN")));
        QVERIFY(set(quality, added, 2, QStringLiteral("TSS")));
        QVERIFY(set(quality, added, 3, QStringLiteral("0.25")));

        for (auto *m : {&coverages, &loadings, &curb, &quality}) {
            QVERIFY(m->isDirty());
            const QString error = m->commit(engine.handle);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QVERIFY(!m->isDirty());
        }
        const QString saved = QDir(dir).filePath(QStringLiteral("saved.inp"));
        QCOMPARE(swmm_model_write(engine.handle, saved.toUtf8().constData()), SWMM_OK);
        QVERIFY(sectionIs(saved, QStringLiteral("[2D_COVERAGES]"),
                          {{"*", "Urban", "30", "Grass", "70"},
                           {"TAG", "PAD", "Urban", "60", "Grass", "40"},
                           {"CELL", "2", "Urban", "100"}}));
        QVERIFY(sectionIs(saved, QStringLiteral("[2D_LOADINGS]"),
                          {{"TAG", "PAD", "TSS", "75"}, {"*", "Tracer", "5"}}));
        QVERIFY(sectionIs(saved, QStringLiteral("[2D_CURB_LENGTH]"), {{"CELL", "1", "3.25"}}));
        QVERIFY(sectionIs(saved, QStringLiteral("[2D_INITIAL_QUALITY]"),
                          {{"*", "Tracer", "1.5"}, {"TAG", "LAWN", "TSS", "0.25"}}));

        Engine reopened;
        QVERIFY(openModel(reopened, saved, QDir(dir).filePath(QStringLiteral("reopened"))));
        Mesh2DSectionRowModel coverages2(Section::Coverages), loadings2(Section::Loadings),
            curb2(Section::CurbLength), quality2(Section::SurfaceInitialQuality);
        for (auto *m : {&coverages2, &loadings2, &curb2, &quality2})
            QVERIFY2(m->load(reopened.handle).isEmpty(), qPrintable(m->sectionName()));
        QCOMPARE(rowsOf(coverages2), rowsOf(coverages));
        QCOMPARE(rowsOf(loadings2), rowsOf(loadings));
        QCOMPARE(rowsOf(curb2), (QStringList{QStringLiteral("Cell|1|3.25")}));
        QCOMPARE(rowsOf(quality2), rowsOf(quality));
    }

    /**
     * @brief [GW_TRANSPORT_PARAMS], [GW_SORPTION], [GW_INITIAL_QUALITY] and [2D_AQUIFER_LINKS]
     * through the same staged model: edit, commit, write, reopen.
     */
    void groundwaterRowSectionsWriteAndReopen()
    {
        const QString inp = writeDeck(QStringLiteral("groundwater_rows"));
        const QString dir = QFileInfo(inp).absolutePath();
        Engine engine;
        QVERIFY(openModel(engine, inp, dir));
        Mesh2DSectionRowModel params(Section::GwParams), sorption(Section::GwSorption),
            quality(Section::GwInitialQuality), links(Section::AquiferLinks);
        for (auto *m : {&params, &sorption, &quality, &links})
            QVERIFY2(m->load(engine.handle).isEmpty(), qPrintable(m->sectionName()));

        // Omitted parameter columns hold the engine defaults.
        QCOMPARE(rowsOf(params),
                 QStringList{QStringLiteral("All cells||2600|880|2|0|2|0.1|1e-09|1e-09|0.065")});
        QCOMPARE(rowsOf(sorption), QStringList{QStringLiteral("All cells||TSS|0.5|")});
        QCOMPARE(sorption.data(sorption.index(0, 4)).toString(),
                 QStringLiteral("(pollutant decay)"));
        QCOMPARE(rowsOf(quality), QStringList{QStringLiteral("All cells||SAT|Tracer|2")});
        QCOMPARE(rowsOf(links), QStringList{QStringLiteral("C1|50|2.5|")});
        QCOMPARE(links.data(links.index(0, 3), Qt::CheckStateRole).toInt(), int(Qt::Checked));
        QVERIFY(links.data(links.index(0, 0), Mesh2DSectionRowModel::ChoicesRole)
                    .toStringList()
                    .contains(QStringLiteral("C1")));

        QVERIFY(set(params, 0, 6, QStringLiteral("3")));
        int added = params.appendRow();
        QVERIFY(set(params, added, 0, 1));
        QVERIFY(set(params, added, 1, QStringLiteral("PAD")));
        QVERIFY(set(params, added, 10, QStringLiteral("-1.5"))); // GeoFlux is signed
        QVERIFY(!set(params, added, 9, QStringLiteral("-1")));

        added = sorption.appendRow();
        QVERIFY(set(sorption, added, 0, 2));
        QVERIFY(set(sorption, added, 1, QStringLiteral("1")));
        QVERIFY(set(sorption, added, 2, QStringLiteral("Tracer")));
        QVERIFY(set(sorption, added, 3, QStringLiteral("1")));
        QVERIFY(set(sorption, added, 4, QStringLiteral("0.1")));

        added = quality.appendRow();
        QVERIFY(set(quality, added, 0, 1));
        QVERIFY(set(quality, added, 1, QStringLiteral("PAD")));
        QVERIFY(set(quality, added, 2, QStringLiteral("UNSAT")));
        QVERIFY(set(quality, added, 3, QStringLiteral("TSS")));
        QVERIFY(set(quality, added, 4, QStringLiteral("-4")));
        QVERIFY(quality.validateCommit(engine.handle).contains(QStringLiteral("negative")));
        QVERIFY(set(quality, added, 4, QStringLiteral("4")));

        QVERIFY(set(links, 0, 1, QStringLiteral("10")));
        QVERIFY(links.setData(links.index(0, 3), Qt::Unchecked, Qt::CheckStateRole));

        for (auto *m : {&params, &sorption, &quality, &links}) {
            const QString error = m->commit(engine.handle);
            QVERIFY2(error.isEmpty(), qPrintable(error));
        }
        const QString saved = QDir(dir).filePath(QStringLiteral("saved.inp"));
        QCOMPARE(swmm_model_write(engine.handle, saved.toUtf8().constData()), SWMM_OK);
        QVERIFY(sectionIs(saved, QStringLiteral("[GW_TRANSPORT_PARAMS]"),
                          {{"*", "2600", "880", "2", "0", "3", "0.1", "1e-9", "1e-9", "0.065"},
                           {"TAG", "PAD", "2650", "880", "2", "0", "1", "0.1", "1e-9", "1e-9",
                            "-1.5"}}));
        QVERIFY(sectionIs(saved, QStringLiteral("[GW_SORPTION]"),
                          {{"*", "TSS", "0.5", "-"}, {"CELL", "1", "Tracer", "1", "0.1"}}));
        QVERIFY(sectionIs(saved, QStringLiteral("[GW_INITIAL_QUALITY]"),
                          {{"*", "SAT", "Tracer", "2"}, {"TAG", "PAD", "UNSAT", "TSS", "4"}}));
        const auto linkRows = sectionRows(saved, QStringLiteral("[2D_AQUIFER_LINKS]"));
        QCOMPARE(linkRows.size(), 1);
        QCOMPARE(linkRows[0].value(0), QStringLiteral("C1"));
        QVERIFY(linkRows[0].join(' ').contains(QStringLiteral("EXCHANGE NO")));

        Engine reopened;
        QVERIFY(openModel(reopened, saved, QDir(dir).filePath(QStringLiteral("reopened"))));
        Mesh2DSectionRowModel params2(Section::GwParams), sorption2(Section::GwSorption),
            quality2(Section::GwInitialQuality), links2(Section::AquiferLinks);
        for (auto *m : {&params2, &sorption2, &quality2, &links2})
            QVERIFY2(m->load(reopened.handle).isEmpty(), qPrintable(m->sectionName()));
        QCOMPARE(rowsOf(params2), rowsOf(params));
        QCOMPARE(rowsOf(sorption2), rowsOf(sorption));
        QCOMPARE(rowsOf(quality2), rowsOf(quality));
        QCOMPARE(rowsOf(links2), (QStringList{QStringLiteral("C1|10|2.5|")}));
        QCOMPARE(links2.data(links2.index(0, 3), Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
    }

    /**
     * @brief [GW_SOURCES]: an XY source keeps `XY x y` through edits of its scale, terms and
     * coordinates, sits in the cell with the nearest centroid, and switches to CELL (and a CELL
     * source to XY) through the Scope column; sources can be added, renamed and removed, with
     * species terms edited through the term model.
     */
    void sourcesKeepXyThroughEditsAndReopen()
    {
        const QString inp = writeDeck(QStringLiteral("sources"));
        const QString dir = QFileInfo(inp).absolutePath();
        Engine engine;
        QVERIFY(openModel(engine, inp, dir));
        Mesh2DGwSourceModel sources;
        QVERIFY(sources.load(engine.handle).isEmpty());
        Mesh2DGwSourceTermModel terms(&sources);
        using C = Mesh2DGwSourceModel;
        QCOMPARE(sources.rowCount(), 2);
        QCOMPARE(cell(sources, 0, C::ColName), QStringLiteral("Well1"));
        QCOMPARE(cell(sources, 0, C::ColTarget), QStringLiteral("2"));
        QCOMPARE(cell(sources, 0, C::ColX), QString());
        QCOMPARE(cell(sources, 0, C::ColTerms), QStringLiteral("Tracer CONC 100"));
        QVERIFY(!sources.isXy(0));
        QVERIFY(sources.isXy(1));
        QCOMPARE(cell(sources, 1, C::ColScope), QStringLiteral("XY"));
        QCOMPARE(cell(sources, 1, C::ColX), QStringLiteral("2"));
        QCOMPARE(cell(sources, 1, C::ColY), QStringLiteral("8"));
        // (2, 8) is nearest the centroid of cell 2 (3.33, 6.67), not cell 1 (6.67, 3.33).
        QCOMPARE(sources.data(sources.index(1, C::ColTarget)).toString(),
                 QStringLiteral("cell 2"));
        QCOMPARE(cell(sources, 1, C::ColFlowSeries), QStringLiteral("Pump"));
        QCOMPARE(cell(sources, 1, C::ColScale), QStringLiteral("2"));
        // XY rows edit X and Y; the cell they resolve to is read-only. CELL rows have no X/Y.
        QVERIFY(!(sources.flags(sources.index(1, C::ColTarget)) & Qt::ItemIsEditable));
        QVERIFY(sources.flags(sources.index(1, C::ColX)) & Qt::ItemIsEditable);
        QVERIFY(!(sources.flags(sources.index(0, C::ColX)) & Qt::ItemIsEditable));

        // Spot: new scale and coordinates; it stays XY.
        QVERIFY(set(sources, 1, C::ColScale, QStringLiteral("3")));
        QVERIFY(set(sources, 1, C::ColX, QStringLiteral("9")));
        QVERIFY(set(sources, 1, C::ColY, QStringLiteral("1")));
        QVERIFY(!set(sources, 1, C::ColY, QStringLiteral("nan")));
        QCOMPARE(sources.data(sources.index(1, C::ColTarget)).toString(),
                 QStringLiteral("(placed on Apply)"));

        // Well1: new flow and an added MASS term through the term model.
        QVERIFY(set(sources, 0, C::ColFlow, QStringLiteral("0.001")));
        terms.setSourceRow(0);
        QCOMPARE(terms.rowCount(), 1);
        const int term = terms.appendTerm();
        QCOMPARE(term, 1);
        QCOMPARE(cell(terms, term, Mesh2DGwSourceTermModel::ColSpecies), QStringLiteral("TSS"));
        QVERIFY(set(terms, term, Mesh2DGwSourceTermModel::ColKind, QStringLiteral("MASS")));
        QVERIFY(set(terms, term, Mesh2DGwSourceTermModel::ColValue, QStringLiteral("2")));
        QVERIFY(!set(terms, term, Mesh2DGwSourceTermModel::ColSeries, QStringLiteral("Nope")));

        // A new TAG source; a duplicate name is refused at validation.
        const int ring = sources.appendSource();
        QVERIFY(set(sources, ring, C::ColName, QStringLiteral("well1")));
        QVERIFY(sources.validateCommit(engine.handle).contains(QStringLiteral("unique")));
        QVERIFY(set(sources, ring, C::ColName, QStringLiteral("Ring")));
        QVERIFY(set(sources, ring, C::ColScope, 1));
        QVERIFY(set(sources, ring, C::ColTarget, QStringLiteral("PAD")));
        QVERIFY(set(sources, ring, C::ColFlow, QStringLiteral("-0.0001")));

        QString error = sources.commit(engine.handle);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(sources.isXy(1));
        // (9, 1) is nearest cell 1's centroid; the engine placed it on Apply.
        QCOMPARE(sources.data(sources.index(1, C::ColTarget)).toString(),
                 QStringLiteral("cell 1"));
        const QString first = QDir(dir).filePath(QStringLiteral("saved_keep_xy.inp"));
        QCOMPARE(swmm_model_write(engine.handle, first.toUtf8().constData()), SWMM_OK);
        QVERIFY(sectionIs(first, QStringLiteral("[GW_SOURCES]"),
                          {{"Well1", "CELL", "2", "FLOW", "0.001", "Tracer", "CONC", "100", "TSS",
                            "MASS", "2"},
                           {"Spot", "XY", "9", "1", "FLOW", "Pump", "SCALE", "3", "TSS", "MASS",
                            "3"},
                           {"Ring", "TAG", "PAD", "FLOW", "-0.0001"}}));

        // Scope switches: Spot XY -> CELL at the cell it sat in; Well1 renamed and made XY at
        // (2, 8); Ring dropped.
        QVERIFY(set(sources, 1, C::ColScope, QStringLiteral("Cell")));
        QVERIFY(!sources.isXy(1));
        QCOMPARE(cell(sources, 1, C::ColTarget), QStringLiteral("1"));
        QVERIFY(set(sources, 0, C::ColName, QStringLiteral("WellA")));
        QVERIFY(set(sources, 0, C::ColScope, QStringLiteral("XY")));
        QVERIFY(set(sources, 0, C::ColX, QStringLiteral("2")));
        QVERIFY(set(sources, 0, C::ColY, QStringLiteral("8")));
        QVERIFY(sources.removeRow(2));
        error = sources.commit(engine.handle);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        // The renamed source is appended by the engine, and the model adopts that order.
        QCOMPARE(cell(sources, 0, C::ColName), QStringLiteral("Spot"));
        QCOMPARE(cell(sources, 1, C::ColName), QStringLiteral("WellA"));
        QVERIFY(sources.isXy(1));
        QCOMPARE(sources.data(sources.index(1, C::ColTarget)).toString(),
                 QStringLiteral("cell 2"));
        const QString second = QDir(dir).filePath(QStringLiteral("saved_scopes.inp"));
        QCOMPARE(swmm_model_write(engine.handle, second.toUtf8().constData()), SWMM_OK);
        QVERIFY(sectionIs(second, QStringLiteral("[GW_SOURCES]"),
                          {{"Spot", "CELL", "1", "FLOW", "Pump", "SCALE", "3", "TSS", "MASS", "3"},
                           {"WellA", "XY", "2", "8", "FLOW", "0.001", "Tracer", "CONC", "100",
                            "TSS", "MASS", "2"}}));

        Engine reopened;
        QVERIFY(openModel(reopened, second, QDir(dir).filePath(QStringLiteral("reopened"))));
        Mesh2DGwSourceModel sources2;
        QVERIFY(sources2.load(reopened.handle).isEmpty());
        QCOMPARE(rowsOf(sources2), rowsOf(sources));
        QVERIFY(!sources2.isXy(0));
        QVERIFY(sources2.isXy(1));
        QCOMPARE(sources2.data(sources2.index(1, C::ColTarget)).toString(),
                 QStringLiteral("cell 2"));
    }

    /**
     * @brief XY comes from the engine, not the input file: a model opened from a file deleted
     * afterwards, and a source made XY through the API in this session, both read as XY.
     */
    void xySourcesDetectedWithoutInputFile()
    {
        const QString inp = writeDeck(QStringLiteral("xy_without_file"));
        const QString dir = QFileInfo(inp).absolutePath();
        const QString copy = QDir(dir).filePath(QStringLiteral("opened_then_deleted.inp"));
        QFile::remove(copy);
        QVERIFY(QFile::copy(inp, copy));
        Engine engine;
        QVERIFY(openModel(engine, copy, dir));
        QVERIFY(QFile::remove(copy));
        QCOMPARE(swmm_gw_source_xy_set(engine.handle, 0, 9.0, 1.0), SWMM_OK); // Well1, in session
        Mesh2DGwSourceModel sources;
        QVERIFY(sources.load(engine.handle).isEmpty());
        QVERIFY(sources.isXy(0));
        QVERIFY(sources.isXy(1));
        QCOMPARE(cell(sources, 0, Mesh2DGwSourceModel::ColX), QStringLiteral("9"));
        QCOMPARE(cell(sources, 0, Mesh2DGwSourceModel::ColY), QStringLiteral("1"));
        QCOMPARE(sources.data(sources.index(0, Mesh2DGwSourceModel::ColTarget)).toString(),
                 QStringLiteral("cell 1"));
        QCOMPARE(sources.data(sources.index(1, Mesh2DGwSourceModel::ColTarget)).toString(),
                 QStringLiteral("cell 2"));
    }

    /**
     * @brief Restoring the loaded sources after a commit puts an XY source back as XY (its flow
     * rewrite clears XY in the engine, so the restore must place it again).
     */
    void restoreKeepsXySources()
    {
        const QString inp = writeDeck(QStringLiteral("xy_restore"));
        Engine engine;
        QVERIFY(openModel(engine, inp, QFileInfo(inp).absolutePath()));
        Mesh2DGwSourceModel sources;
        QVERIFY(sources.load(engine.handle).isEmpty());
        QVERIFY(set(sources, 1, Mesh2DGwSourceModel::ColFlowSeries, QStringLiteral("(none)")));
        QVERIFY(set(sources, 1, Mesh2DGwSourceModel::ColFlow, QStringLiteral("0.002")));
        const QString error = sources.commit(engine.handle, false);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        int byXy = 0;
        double x = 0.0, y = 0.0, flow = 0.0;
        char series[64]{};
        QCOMPARE(swmm_gw_source_xy_get(engine.handle, 1, &byXy, &x, &y), SWMM_OK);
        QCOMPARE(byXy, 1);
        QCOMPARE(swmm_gw_source_get(engine.handle, 1, nullptr, 0, nullptr, nullptr, 0, nullptr,
                                    &flow, series, sizeof series),
                 SWMM_OK);
        QCOMPARE(flow, 0.002);
        QVERIFY(sources.restoreLoaded(engine.handle).isEmpty());
        byXy = 0;
        QCOMPARE(swmm_gw_source_xy_get(engine.handle, 1, &byXy, &x, &y), SWMM_OK);
        QCOMPARE(byXy, 1);
        QCOMPARE(x, 2.0);
        QCOMPARE(y, 8.0);
        QCOMPARE(swmm_gw_source_get(engine.handle, 1, nullptr, 0, nullptr, nullptr, 0, nullptr,
                                    &flow, series, sizeof series),
                 SWMM_OK);
        QCOMPARE(QString::fromUtf8(series), QStringLiteral("Pump"));
    }

    /**
     * @brief [GW_INITIAL_QUALITY] FILE rows belong to the file: the editor stages only the inline
     * rows, and a commit neither drops the FILE reference nor copies its rows inline.
     */
    void groundwaterInitialQualityLeavesFileRowsAlone()
    {
        QByteArray deck(kDeck);
        deck.replace("*   SAT  Tracer  2", "*   SAT  Tracer  2\nFILE  initial_quality.csv");
        const QString inp = writeDeck(QStringLiteral("gw_initial_quality_file"), deck);
        const QString dir = QFileInfo(inp).absolutePath();
        QFile csv(QDir(dir).filePath(QStringLiteral("initial_quality.csv")));
        QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Truncate));
        csv.write("scope,zone,species,value\nCELL 1,SAT,TSS,7\n");
        csv.close();
        Engine engine;
        QVERIFY(openModel(engine, inp, dir));
        Mesh2DSectionRowModel quality(Section::GwInitialQuality);
        QVERIFY(quality.load(engine.handle).isEmpty());
        QCOMPARE(rowsOf(quality), QStringList{QStringLiteral("All cells||SAT|Tracer|2")});
        QCOMPARE(quality.fileReference(), QStringLiteral("initial_quality.csv"));
        qInfo() << "FILE rows loaded at open:" << quality.fileRowCount();
        QVERIFY(set(quality, 0, 4, QStringLiteral("3")));
        const QString error = quality.commit(engine.handle);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(swmm_gw_init_quality_count(engine.handle), 1 + quality.fileRowCount());
        const QString saved = QDir(dir).filePath(QStringLiteral("saved.inp"));
        QCOMPARE(swmm_model_write(engine.handle, saved.toUtf8().constData()), SWMM_OK);
        const auto rows = sectionRows(saved, QStringLiteral("[GW_INITIAL_QUALITY]"));
        QStringList lines;
        for (const auto &row : rows)
            lines << row.join(QLatin1Char(' '));
        const int file = int(lines.indexOf(QStringLiteral("FILE initial_quality.csv")));
        QVERIFY2(file >= 0 && lines.size() == 2, qPrintable(lines.join('/')));
        QVERIFY(sameTokens(rows.value(file == 0 ? 1 : 0), {"*", "SAT", "Tracer", "3"}));

        Engine reopened;
        QVERIFY(openModel(reopened, saved, QDir(dir).filePath(QStringLiteral("reopened"))));
        Mesh2DSectionRowModel quality2(Section::GwInitialQuality);
        QVERIFY(quality2.load(reopened.handle).isEmpty());
        QCOMPARE(rowsOf(quality2), rowsOf(quality));
        QCOMPARE(quality2.fileRowCount(), quality.fileRowCount());
    }

    /** A row edited outside the editor after load blocks the commit; nothing is overwritten. */
    void outsideEditBlocksCommit()
    {
        const QString inp = writeDeck(QStringLiteral("concurrency"));
        Engine engine;
        QVERIFY(openModel(engine, inp, QFileInfo(inp).absolutePath()));
        Mesh2DSectionRowModel loadings(Section::Loadings);
        QVERIFY(loadings.load(engine.handle).isEmpty());
        QVERIFY(set(loadings, 0, 3, QStringLiteral("80")));
        QCOMPARE(swmm_2d_loading_set(engine.handle, SWMM_SQ2D_SCOPE_GLOBAL, nullptr, 0, "TSS", 1.0),
                 SWMM_OK);
        QVERIFY(loadings.commit(engine.handle).contains(QStringLiteral("outside this editor")));
        int count = 0;
        QCOMPARE(swmm_2d_loading_count(engine.handle, &count), SWMM_OK);
        QCOMPARE(count, 2);
    }

    /** The Surface Quality dialog stages its four tabs and Apply writes a changed section. */
    void surfaceQualityDialogAppliesChangedTabs()
    {
        const QString inp = writeDeck(QStringLiteral("dialog"));
        Engine engine;
        QVERIFY(openModel(engine, inp, QFileInfo(inp).absolutePath()));
        Mesh2DSurfaceQualityDialog dialog(engine.handle);
        auto *tabs = dialog.findChild<QTabWidget *>();
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 5); // the four section tabs and Boundary inflow (Phase 4d)
        auto *curb = dialog.findChild<Mesh2DSectionRowModel *>(QStringLiteral("sqCurbLength"));
        QVERIFY(curb);
        QVERIFY(set(*curb, 0, 2, QStringLiteral("20")));
        QSignalSpy applied(&dialog, &Mesh2DSurfaceQualityDialog::applied);
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(applied.count(), 1);
        double length = 0.0;
        QCOMPARE(swmm_2d_curb_length_get(engine.handle, 0, nullptr, nullptr, 0, nullptr, &length),
                 SWMM_OK);
        QCOMPARE(length, 20.0);
        // Nothing changed: Apply succeeds without writing again.
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(applied.count(), 1);
    }

    /** Process lock (plan §5, Phase 4): with water quality off the dialog opens read-only with
     *  the reason, and switching it off after a draft refuses Apply and writes nothing. */
    void switchedOffQualityIsReadOnlyAndRefusesStaleApply()
    {
        const QString inp = writeDeck(QStringLiteral("lock"));
        Engine engine;
        QVERIFY(openModel(engine, inp, QFileInfo(inp).absolutePath()));
        const QString reason = QStringLiteral("Water quality is off.");
        const auto bannerSays = [&reason](const QDialog &dialog) {
            for (auto *label : dialog.findChildren<QLabel *>())
                if (label->text().startsWith(reason))
                    return true;
            return false;
        };
        QCOMPARE(swmm_options_set(engine.handle, "IGNORE_QUALITY", "YES"), SWMM_OK);
        {
            Mesh2DSurfaceQualityDialog dialog(engine.handle);
            QCOMPARE(dialog.findChild<QDialogButtonBox *>()->standardButtons(),
                     QDialogButtonBox::Close);
            QVERIFY(bannerSays(dialog));
        }
        QCOMPARE(swmm_options_set(engine.handle, "IGNORE_QUALITY", "NO"), SWMM_OK);
        Mesh2DSurfaceQualityDialog dialog(engine.handle);
        QVERIFY(!bannerSays(dialog));
        auto *curb = dialog.findChild<Mesh2DSectionRowModel *>(QStringLiteral("sqCurbLength"));
        QVERIFY(curb);
        QVERIFY(set(*curb, 0, 2, QStringLiteral("20")));
        QSignalSpy applied(&dialog, &Mesh2DSurfaceQualityDialog::applied);
        QCOMPARE(swmm_options_set(engine.handle, "IGNORE_QUALITY", "YES"), SWMM_OK);
        dialog.show();
        QString warning;
        QTimer::singleShot(0, &dialog, [&dialog, &warning] {
            if (auto *message = dialog.findChild<QMessageBox *>()) {
                warning = message->text();
                message->accept();
            }
        });
        auto *apply = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply);
        apply->click();
        QCOMPARE(applied.count(), 0);
        QVERIFY2(warning.startsWith(reason), qPrintable(warning));
        double length = 0.0;
        QCOMPARE(swmm_2d_curb_length_get(engine.handle, 0, nullptr, nullptr, 0, nullptr, &length),
                 SWMM_OK);
        QVERIFY(length != 20.0);
        QCOMPARE(swmm_options_set(engine.handle, "IGNORE_QUALITY", "NO"), SWMM_OK);
        apply->click();
        QCOMPARE(applied.count(), 1);
        QCOMPARE(swmm_2d_curb_length_get(engine.handle, 0, nullptr, nullptr, 0, nullptr, &length),
                 SWMM_OK);
        QCOMPARE(length, 20.0);
    }

    /**
     * @brief The Bellinge groundwater-quality example shows its authored rows in each editor.
     * Opened read-only: report/output go to the output folder and nothing is written back.
     */
    /** Phase 4d (review G-03): [2D_BOUNDARY_QUALITY] rows load, are refused on interior or
     *  unknown edges, unknown species, negative values and duplicate keys, are set on exterior
     *  edges, apply, survive save and reopen, and are restored after a failed apply. The fixture's
     *  two triangles share one diagonal: exterior edges (0,0) (0,2) (1,0) (1,1), interior (0,1)
     *  (1,2). */
    void boundaryQualityRowsValidateApplyAndReopen()
    {
        QByteArray deck(kDeck);
        deck += "\n[2D_BOUNDARY_QUALITY]\n0  0  TSS  25\n";
        const QString inp = writeDeck(QStringLiteral("boundary_quality"), deck);
        const QString dir = QFileInfo(inp).absolutePath();
        Engine engine;
        QVERIFY(openModel(engine, inp, dir));
        Mesh2DBoundaryQualityModel model;
        QCOMPARE(model.load(engine.handle), QString());
        QCOMPARE(model.rows().size(), 1);
        QCOMPARE(model.rows()[0].cell, 0);
        QCOMPARE(model.rows()[0].edge, 0);
        QCOMPARE(model.rows()[0].species, QStringLiteral("TSS"));
        QCOMPARE(model.rows()[0].concentration, 25.0);
        QVERIFY(!model.isDirty());
        QVERIFY(model.species().contains(QStringLiteral("Tracer")));

        const auto scope = Mesh2DBoundaryQualityModel::classifyEdges(
            engine.handle, {{0, 0}, {0, 1}, {0, 2}, {1, 2}, {1, 0}, {9, 0}, {0, 0}});
        QCOMPARE(scope.exterior, (QList<QPair<int, int>>{{0, 0}, {0, 2}, {1, 0}}));
        QCOMPARE(scope.interior, 2);
        QCOMPARE(scope.invalid, 1);

        const auto refused = [&](Mesh2DBoundaryQualityModel::Row row, const QString &why) {
            model.addRow(row);
            const QString error = model.validateCommit(engine.handle);
            model.removeRowAt(int(model.rows().size()) - 1);
            if (error.isEmpty())
                return QStringLiteral("accepted");
            return error.contains(why) ? QString() : error;
        };
        QCOMPARE(refused({0, 1, QStringLiteral("Tracer"), 1.0}, QStringLiteral("interior")),
                 QString());
        QCOMPARE(refused({9, 0, QStringLiteral("Tracer"), 1.0}, QStringLiteral("not an edge")),
                 QString());
        QCOMPARE(refused({1, 0, QStringLiteral("Lead"), 1.0}, QStringLiteral("not a species")),
                 QString());
        QCOMPARE(refused({1, 0, QStringLiteral("Tracer"), -1.0}, QStringLiteral("not negative")),
                 QString());
        QCOMPARE(refused({0, 0, QStringLiteral("TSS"), 30.0}, QStringLiteral("already has")),
                 QString());
        QVERIFY(!model.isDirty());

        QCOMPARE(model.setForEdges(scope.exterior, QStringLiteral("Tracer"), 2.5), 3);
        QCOMPARE(model.setForEdges({{0, 0}}, QStringLiteral("TSS"), 40.0), 1); // replaces 25
        QCOMPARE(model.setForEdges({{0, 0}}, QStringLiteral("TSS"), 40.0), 0); // unchanged
        QCOMPARE(model.rows().size(), 4);
        QVERIFY(model.isDirty());
        QCOMPARE(model.validateCommit(engine.handle), QString());
        QCOMPARE(model.commit(engine.handle), QString());
        model.acceptChanges();
        QVERIFY(!model.isDirty());

        const QString saved = QDir(dir).filePath(QStringLiteral("saved.inp"));
        QCOMPARE(swmm_model_write(engine.handle, saved.toUtf8().constData()), SWMM_OK);
        QCOMPARE(sectionRows(saved, QStringLiteral("[2D_BOUNDARY_QUALITY]")).size(), 4);
        Engine reopened;
        QVERIFY(openModel(reopened, saved, QDir(dir).filePath(QStringLiteral("reopened"))));
        Mesh2DBoundaryQualityModel back;
        QCOMPARE(back.load(reopened.handle), QString());
        QCOMPARE(back.rows(), model.rows());

        // A failed apply puts back what was loaded.
        Mesh2DBoundaryQualityModel again;
        QCOMPARE(again.load(engine.handle), QString());
        again.addRow({0, 2, QStringLiteral("TSS"), 5.0});
        QCOMPARE(again.commit(engine.handle), QString());
        int count = 0;
        QCOMPARE(swmm_2d_boundary_quality_count(engine.handle, &count), SWMM_OK);
        QCOMPARE(count, 5);
        QCOMPARE(again.restoreLoaded(engine.handle), QString());
        QCOMPARE(swmm_2d_boundary_quality_count(engine.handle, &count), SWMM_OK);
        QCOMPARE(count, 4);
    }

    /** Phase 4d: opened on selected edges, the Boundary inflow tab counts them, sets a species
     *  on the exterior ones only and applies with the other tabs; switched-off quality leaves it
     *  read-only, still showing the counts. */
    void surfaceQualityDialogTargetsBoundaryInflow()
    {
        const QString inp = writeDeck(QStringLiteral("dialog_boundary"));
        Engine engine;
        QVERIFY(openModel(engine, inp, QFileInfo(inp).absolutePath()));
        const QList<QPair<int, int>> target{{0, 0}, {0, 1}, {1, 0}, {7, 0}};
        {
            Mesh2DSurfaceQualityDialog dialog(engine.handle);
            dialog.setTargetEdges(target);
            auto *tabs = dialog.findChild<QTabWidget *>();
            QVERIFY(tabs);
            QCOMPARE(tabs->currentWidget()->objectName(), QStringLiteral("sqBoundaryInflowPage"));
            auto *summary = dialog.findChild<QLabel *>(QStringLiteral("sqBoundaryTargetSummary"));
            QVERIFY(summary && !summary->isHidden());
            QVERIFY2(summary->text().contains(
                         QStringLiteral("4 selected edges: 2 exterior, 1 interior, 1 not on the mesh")),
                     qPrintable(summary->text()));
            auto *species = dialog.findChild<QComboBox *>(QStringLiteral("sqBoundaryTargetSpecies"));
            auto *value = dialog.findChild<QLineEdit *>(QStringLiteral("sqBoundaryTargetValue"));
            auto *set = dialog.findChild<QPushButton *>(QStringLiteral("sqBoundaryTargetSet"));
            QVERIFY(species && value && set);
            QVERIFY(set->isEnabled());
            species->setCurrentText(QStringLiteral("Tracer"));
            value->setText(QStringLiteral("4"));
            set->click();
            auto *model =
                dialog.findChild<Mesh2DBoundaryQualityModel *>(QStringLiteral("sqBoundaryInflow"));
            QVERIFY(model);
            QCOMPARE(model->rows().size(), 2);
            QSignalSpy applied(&dialog, &Mesh2DSurfaceQualityDialog::applied);
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
            QCOMPARE(applied.count(), 1);
            int count = 0;
            QCOMPARE(swmm_2d_boundary_quality_count(engine.handle, &count), SWMM_OK);
            QCOMPARE(count, 2);
            int cell = -1, edge = -1;
            char name[64]{};
            double conc = 0.0;
            QCOMPARE(swmm_2d_get_boundary_quality(engine.handle, 1, &cell, &edge, name, 64, &conc),
                     SWMM_OK);
            QCOMPARE(cell, 1);
            QCOMPARE(edge, 0);
            QCOMPARE(QString::fromUtf8(name), QStringLiteral("Tracer"));
            QCOMPARE(conc, 4.0);
        }
        QCOMPARE(swmm_options_set(engine.handle, "IGNORE_QUALITY", "YES"), SWMM_OK);
        Mesh2DSurfaceQualityDialog locked(engine.handle);
        locked.setTargetEdges(target);
        auto *set = locked.findChild<QPushButton *>(QStringLiteral("sqBoundaryTargetSet"));
        QVERIFY(set && !set->isEnabled());
        QVERIFY(!locked.findChild<QLabel *>(QStringLiteral("sqBoundaryTargetSummary"))->isHidden());
    }

    void bellingeModelShowsItsRows()
    {
        const QString inp = QStringLiteral(SWMMVIS_BELLINGE_GW_QUALITY_INP);
        if (!QFileInfo::exists(inp))
            QSKIP("Bellinge groundwater-quality example not present");
        const QString dir = QDir(outputRoot()).filePath(QStringLiteral("bellinge_readonly"));
        QStringList before;
        for (const QFileInfo &info : QDir(QFileInfo(inp).absolutePath()).entryInfoList(QDir::Files))
            before << info.fileName() + QLatin1Char('|') +
                          QString::number(info.lastModified().toMSecsSinceEpoch()) +
                          QLatin1Char('|') + QString::number(info.size());
        {
            Engine engine;
            QVERIFY(openModel(engine, inp, dir));
            Mesh2DGwSourceModel sources;
            QVERIFY(sources.load(engine.handle).isEmpty());
            QCOMPARE(sources.rowCount(), 1);
            QCOMPARE(cell(sources, 0, Mesh2DGwSourceModel::ColName), QStringLiteral("Well1"));
            QCOMPARE(cell(sources, 0, Mesh2DGwSourceModel::ColScope), QStringLiteral("Cell"));
            QCOMPARE(cell(sources, 0, Mesh2DGwSourceModel::ColTarget), QStringLiteral("3077"));
            QCOMPARE(cell(sources, 0, Mesh2DGwSourceModel::ColTerms),
                     QStringLiteral("GWTracer CONC 100"));
            QVERIFY(!sources.isXy(0));

            Mesh2DSectionRowModel coverages(Section::Coverages), loadings(Section::Loadings);
            QVERIFY(coverages.load(engine.handle).isEmpty());
            QVERIFY(loadings.load(engine.handle).isEmpty());
            QCOMPARE(rowsOf(coverages), QStringList{QStringLiteral("Tag|SPILLPAD|100")});
            QCOMPARE(coverages.headerData(2, Qt::Horizontal).toString(),
                     QStringLiteral("SpillPad (%)"));
            QCOMPARE(rowsOf(loadings), QStringList{QStringLiteral("Tag|SPILLPAD|SurfPoll|50")});

            Mesh2DAquiferNodeModel nodes;
            nodes.load(engine.handle);
            QCOMPARE(nodes.rowCount(), 9);
            QStringList names;
            for (int r = 0; r < nodes.rowCount(); ++r) {
                names << cell(nodes, r, Mesh2DAquiferNodeModel::ColNode);
                QCOMPARE(cell(nodes, r, Mesh2DAquiferNodeModel::ColCell), QStringLiteral("AUTO"));
                QCOMPARE(nodes.data(nodes.index(r, Mesh2DAquiferNodeModel::ColExchange),
                                    Qt::CheckStateRole)
                             .toInt(),
                         int(Qt::Unchecked));
            }
            QCOMPARE(names, (QStringList{"F74F360", "G60K220", "G60U02F", "G70U01F", "G71U02F",
                                         "G71U10F", "G72U07R", "G75U03F", "G80U03F"}));
            for (auto *m : {&coverages, &loadings})
                QVERIFY(!m->isDirty());
            QVERIFY(!sources.isDirty());
        }
        QStringList after;
        for (const QFileInfo &info : QDir(QFileInfo(inp).absolutePath()).entryInfoList(QDir::Files))
            after << info.fileName() + QLatin1Char('|') +
                         QString::number(info.lastModified().toMSecsSinceEpoch()) +
                         QLatin1Char('|') + QString::number(info.size());
        QCOMPARE(after, before);
    }
};

QTEST_MAIN(TestGwQualityRowEditors)
#include "test_gwqualityroweditors.moc"
