/*!
 * \file   test_landuse_unified_editor.cpp
 * \brief  Iteration 4 — the unified Land Use editor: tabbed detail pane,
 *         Buildup/Washoff table models bound to the engine, pollutant-set
 *         re-dimensioning, impact-aware delete, and engine-backed rename.
 *
 * Fixture models are written to a persistent, reviewable directory under
 * the GUI test data dir (SWMMVIS_GUI_TEST_DATA/landuse_editor/), never to
 * temp dirs.
 */

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QTabWidget>
#include <QTextStream>

#include "landuse/landuseregistry.h"
#include "ui/dialogs/landuseeditordialog.h"
#include "ui/models/qualityfunctiontablemodels.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>

using openswmmvis::landuse::LandUseRegistry;
using openswmmvis::ui::BuildupTableModel;
using openswmmvis::ui::LandUseEditorDialog;
using openswmmvis::ui::WashoffTableModel;

namespace {

QString fixtureDir()
{
    const QString base = QString::fromUtf8(qgetenv("SWMMVIS_GUI_TEST_DATA"));
    return base + QStringLiteral("/landuse_editor");
}

QString writeFixture(const QString &tag)
{
    QDir().mkpath(fixtureDir());
    const QString path = fixtureDir() + QStringLiteral("/%1.inp").arg(tag);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    QTextStream out(&f);
    out << "[OPTIONS]\n"
           "FLOW_UNITS           CFS\n"
           "FLOW_ROUTING         KINWAVE\n"
           "INFILTRATION         HORTON\n"
           "START_DATE           01/01/2026\n"
           "START_TIME           00:00:00\n"
           "END_DATE             01/01/2026\n"
           "END_TIME             01:00:00\n"
           "REPORT_STEP          00:05:00\n"
           "ROUTING_STEP         0:00:30\n"
           "\n[RAINGAGES]\n"
           "RG1  INTENSITY 0:05 1.0 TIMESERIES TS1\n"
           "\n[SUBCATCHMENTS]\n"
           "S1  RG1  J1  10.0  50  500  0.5  0\n"
           "\n[SUBAREAS]\n"
           "S1  0.01  0.1  0.05  0.05  25  OUTLET\n"
           "\n[INFILTRATION]\n"
           "S1  3.0  0.5  4.0  7  0\n"
           "\n[TIMESERIES]\n"
           "TS1  01/01/2026 00:00 1.0\n"
           "\n[JUNCTIONS]\n"
           "J1  100.0  10.0  0.0  0.0  0.0\n"
           "\n[OUTFALLS]\n"
           "O1  95.0  FREE\n"
           "\n[CONDUITS]\n"
           "C1  J1  O1  400.0  0.013  0  0\n"
           "\n[XSECTIONS]\n"
           "C1  CIRCULAR  1.5  0  0  0  1\n"
           "\n[POLLUTANTS]\n"
           "TSS   MG/L  0  0  0  0  NO  *  0.0  0  0\n"
           "Lead  UG/L  0  0  0  0  NO  *  0.0  0  0\n"
           "\n[LANDUSES]\n"
           "Res  7   0.5  0\n"
           "Com  14  0.3  0\n"
           "\n[COVERAGES]\n"
           "S1  Res  60\n"
           "\n[BUILDUP]\n"
           "Res  TSS  POW  100  2  1.5  AREA\n"
           "\n[WASHOFF]\n"
           "Res  TSS  EXP  0.1  1.2  30  15\n";
    return path;
}

struct EngineFixture {
    SWMM_Engine engine = nullptr;

    explicit EngineFixture(const QString &tag)
    {
        const QString inp = writeFixture(tag);
        if (inp.isEmpty()) return;
        engine = swmm_engine_create();
        const QString rpt = fixtureDir() + QStringLiteral("/%1.rpt").arg(tag);
        const QString out = fixtureDir() + QStringLiteral("/%1.out").arg(tag);
        if (swmm_engine_open(engine, inp.toUtf8().constData(),
                             rpt.toUtf8().constData(),
                             out.toUtf8().constData(), nullptr) != SWMM_OK) {
            swmm_engine_destroy(engine);
            engine = nullptr;
        }
    }
    ~EngineFixture()
    {
        if (engine) swmm_engine_destroy(engine);
    }
};

} // namespace

class TestLandUseUnifiedEditor : public QObject
{
    Q_OBJECT

private slots:
    void tabsAndModelsPresent();
    void buildupEditsRoundTripThroughEngine();
    void extBuildupC3IsReadOnly();
    void pollutantAddRedimensionsRows();
    void impactAwareDeleteRemovesEngineObject();
    void renameKeepsEngineMatrices();
};

void TestLandUseUnifiedEditor::tabsAndModelsPresent()
{
    EngineFixture fx(QStringLiteral("tabs"));
    QVERIFY(fx.engine);
    LandUseRegistry reg;
    reg.loadFromEngine(fx.engine);
    QCOMPARE(reg.providerCount(), 2);

    LandUseEditorDialog dlg(&reg, nullptr);
    auto *tabs = dlg.findChild<QTabWidget *>();
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 3);
    QVERIFY(dlg.buildupModel());
    QVERIFY(dlg.washoffModel());
    // The first land use (Res) is auto-selected; both grids show one row
    // per pollutant.
    QCOMPARE(dlg.buildupModel()->rowCount(), 2);
    QCOMPARE(dlg.washoffModel()->rowCount(), 2);
    QVERIFY(dlg.buildupModel()->landUseIndex() >= 0);
}

void TestLandUseUnifiedEditor::buildupEditsRoundTripThroughEngine()
{
    EngineFixture fx(QStringLiteral("roundtrip"));
    QVERIFY(fx.engine);
    LandUseRegistry reg;
    reg.loadFromEngine(fx.engine);
    LandUseEditorDialog dlg(&reg, nullptr);

    auto *model = dlg.buildupModel();
    const int lead = swmm_pollutant_index(fx.engine, "Lead");
    QVERIFY(lead >= 0);

    // Set Lead's buildup on Res through the model.
    QVERIFY(model->setData(model->index(lead, BuildupTableModel::ColFunction),
                           QStringLiteral("EXP"), Qt::EditRole));
    QVERIFY(model->setData(model->index(lead, BuildupTableModel::ColC1),
                           42.0, Qt::EditRole));
    QVERIFY(model->setData(model->index(lead, BuildupTableModel::ColNormalizer),
                           QStringLiteral("CURB"), Qt::EditRole));

    int ft = 0, norm = 0;
    double c1 = 0, c2 = 0, c3 = 0;
    const int res = swmm_landuse_index(fx.engine, "Res");
    QCOMPARE(swmm_buildup_get(fx.engine, res, lead, &ft, &c1, &c2, &c3, &norm),
             SWMM_OK);
    QCOMPARE(ft, 2);              // EXP
    QCOMPARE(c1, 42.0);
    QCOMPARE(norm, 1);            // CURB

    // Washoff too.
    auto *wo = dlg.washoffModel();
    QVERIFY(wo->setData(wo->index(lead, WashoffTableModel::ColFunction),
                        QStringLiteral("EMC"), Qt::EditRole));
    QVERIFY(wo->setData(wo->index(lead, WashoffTableModel::ColSweepEffic),
                        55.0, Qt::EditRole));
    double coeff = 0, expon = 0, sweep = 0, bmp = 0;
    QCOMPARE(swmm_washoff_get(fx.engine, res, lead, &ft, &coeff, &expon,
                              &sweep, &bmp), SWMM_OK);
    QCOMPARE(ft, 3);              // EMC
    QCOMPARE(sweep, 55.0);
}

void TestLandUseUnifiedEditor::extBuildupC3IsReadOnly()
{
    EngineFixture fx(QStringLiteral("extguard"));
    QVERIFY(fx.engine);
    const int res = swmm_landuse_index(fx.engine, "Res");
    const int tss = swmm_pollutant_index(fx.engine, "TSS");
    // Make Res/TSS an EXT buildup (C3 = table index in disguise).
    QCOMPARE(swmm_buildup_set(fx.engine, res, tss, 4, 1.0, 2.0, 0.0, 0),
             SWMM_OK);

    LandUseRegistry reg;
    reg.loadFromEngine(fx.engine);
    LandUseEditorDialog dlg(&reg, nullptr);
    auto *model = dlg.buildupModel();

    const QModelIndex c3 = model->index(tss, BuildupTableModel::ColC3);
    QVERIFY(!(model->flags(c3) & Qt::ItemIsEditable));
    QVERIFY(!model->setData(c3, 99.0, Qt::EditRole));
}

void TestLandUseUnifiedEditor::pollutantAddRedimensionsRows()
{
    EngineFixture fx(QStringLiteral("poladd"));
    QVERIFY(fx.engine);
    LandUseRegistry reg;
    reg.loadFromEngine(fx.engine);
    LandUseEditorDialog dlg(&reg, nullptr);

    QCOMPARE(dlg.buildupModel()->rowCount(), 2);
    QCOMPARE(swmm_pollutant_add(fx.engine, "BOD", 0), SWMM_OK);
    dlg.refreshPollutants();
    QCOMPARE(dlg.buildupModel()->rowCount(), 3);
    QCOMPARE(dlg.washoffModel()->rowCount(), 3);

    // Existing data survived the stride change (engine grow-preserving add).
    const int res = swmm_landuse_index(fx.engine, "Res");
    const int tss = swmm_pollutant_index(fx.engine, "TSS");
    int ft = 0, norm = 0;
    double c1 = 0, c2 = 0, c3 = 0;
    QCOMPARE(swmm_buildup_get(fx.engine, res, tss, &ft, &c1, &c2, &c3, &norm),
             SWMM_OK);
    QCOMPARE(ft, 1);
    QCOMPARE(c1, 100.0);
}

void TestLandUseUnifiedEditor::impactAwareDeleteRemovesEngineObject()
{
    EngineFixture fx(QStringLiteral("impactdel"));
    QVERIFY(fx.engine);
    LandUseRegistry reg;
    reg.loadFromEngine(fx.engine);

    auto *res = reg.findByName(QStringLiteral("Res"));
    QVERIFY(res);
    // Res carries buildup + washoff rows and one S1 coverage.
    const QString impact = reg.impactSummary(res);
    QVERIFY2(!impact.isEmpty(), "expected a non-empty impact summary");
    QVERIFY(impact.contains(QStringLiteral("buildup")));
    QVERIFY(impact.contains(QStringLiteral("coverage")));

    reg.remove(res);
    QCOMPARE(swmm_landuse_count(fx.engine), 1);
    QCOMPARE(swmm_landuse_index(fx.engine, "Res"), -1);
    QCOMPARE(reg.providerCount(), 1);
}

void TestLandUseUnifiedEditor::renameKeepsEngineMatrices()
{
    EngineFixture fx(QStringLiteral("rename"));
    QVERIFY(fx.engine);
    LandUseRegistry reg;
    reg.loadFromEngine(fx.engine);

    auto *res = reg.findByName(QStringLiteral("Res"));
    QVERIFY(res);
    QVERIFY(reg.rename(res, QStringLiteral("Residential")));

    // Engine followed — no duplicate object, matrices intact.
    QCOMPARE(swmm_landuse_count(fx.engine), 2);
    const int idx = swmm_landuse_index(fx.engine, "Residential");
    QVERIFY(idx >= 0);
    QCOMPARE(swmm_landuse_index(fx.engine, "Res"), -1);
    const int tss = swmm_pollutant_index(fx.engine, "TSS");
    int ft = 0, norm = 0;
    double c1 = 0, c2 = 0, c3 = 0;
    QCOMPARE(swmm_buildup_get(fx.engine, idx, tss, &ft, &c1, &c2, &c3, &norm),
             SWMM_OK);
    QCOMPARE(ft, 1);

    // saveToEngine no longer duplicates after a rename.
    reg.saveToEngine(fx.engine);
    QCOMPARE(swmm_landuse_count(fx.engine), 2);

    // Duplicate name rejected.
    QVERIFY(!reg.rename(res, QStringLiteral("Com")));
}

QTEST_MAIN(TestLandUseUnifiedEditor)
#include "test_landuse_unified_editor.moc"
