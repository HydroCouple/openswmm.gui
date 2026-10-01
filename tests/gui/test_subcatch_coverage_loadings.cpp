/*!
 * \file   test_subcatch_coverage_loadings.cpp
 * \brief  Iteration 4 — SubcatchCompoundEditDialog's rebuilt Land Use
 *         Coverage page (editable full matrix, live re-list, sum warning)
 *         and the new Initial Loadings page ([LOADINGS]).
 */

#include "ui/dialogs/subcatchcompoundeditdialog.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <QLabel>
#include <QObject>
#include <QTableWidget>
#include <QTest>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <cmath>

namespace {

SWMM_Engine buildQualityFixture()
{
    // The current engine's programmatic add path fails to size initial-loading
    // storage (including in the preexisting loadingsPageRoundTrips test). Use
    // the normal INP import path for GUI tests; preserve that engine finding
    // separately rather than dereferencing unallocated storage in this suite.
    const QString data = qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA");
    QFile base(QDir(data).filePath(QStringLiteral("gw_exchange_fixture.inp")));
    if (!base.open(QIODevice::ReadOnly)) return nullptr;
    QByteArray text = base.readAll();
    text += "\n[POLLUTANTS]\nTSS MG/L 0 0 0 0 NO * 0 0 0\nLead UG/L 0 0 0 0 NO * 0 0 0\n"
            "[LANDUSES]\nRes 7 0.5 0\nCom 14 0.3 0\n[COVERAGES]\nS1 Res 60\n[LOADINGS]\nS1 TSS 0\nS1 Lead 0\n";
    const QString folder = QDir::cleanPath(QDir(data).absoluteFilePath(
        QStringLiteral("../../../workplans/artifacts/phase_33_dialogs_safety/model_editors")));
    if (!QDir().mkpath(folder)) return nullptr;
    const QString path = QDir(folder).filePath(QStringLiteral("subcatch_quality.inp"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(text) != text.size() || !file.flush()) return nullptr;
    file.close();
    SWMM_Engine e = swmm_engine_create();
    if (!e) return nullptr;
    const QString report = QDir(folder).filePath(QStringLiteral("subcatch_quality.rpt"));
    const QString output = QDir(folder).filePath(QStringLiteral("subcatch_quality.out"));
    const int opened = swmm_engine_open(e, path.toUtf8().constData(), report.toUtf8().constData(), output.toUtf8().constData(), nullptr);
    if (opened != SWMM_OK) {
        qWarning("Model editor fixture open failed (%d): %s", opened, swmm_error_message(opened));
        swmm_engine_destroy(e); return nullptr;
    }
    return e;
}

SubcatchCompoundEditRef makeRef(SWMM_Engine e,
                                SubcatchCompoundEditRef::Kind kind)
{
    SubcatchCompoundEditRef r;
    r.engine  = e;
    r.subName = QStringLiteral("S1");
    r.kind    = kind;
    return r;
}

/// The dialog hosts one QTableWidget per page — pick by first-column header.
QTableWidget *tableByHeader(QDialog &dlg, const QString &firstColumn)
{
    const auto tables = dlg.findChildren<QTableWidget *>();
    for (auto *t : tables) {
        if (auto *h = t->horizontalHeaderItem(0);
            h && h->text() == firstColumn)
            return t;
    }
    return nullptr;
}

} // namespace

class TestSubcatchCoverageLoadings : public QObject
{
    Q_OBJECT

private slots:
    void invalidatedCompoundCannotUseDestroyedEngine()
    {
        SWMM_Engine e = buildQualityFixture(); QVERIFY(e);
        SubcatchCompoundEditDialog dlg(makeRef(e, SubcatchCompoundEditRef::LandUse));
        auto *table = tableByHeader(dlg, QStringLiteral("Land Use")); QVERIFY(table);
        const QString summary = dlg.updatedSummary();
        const bool invoked = QMetaObject::invokeMethod(&dlg, "invalidateContext", Qt::DirectConnection);
        swmm_engine_destroy(e);
        QVERIFY2(invoked, "Compound dialogs require a pre-engine-close invalidation slot.");
        QVERIFY(!dlg.isEnabled());
        // A queued stale edit after engine destruction must not dereference it.
        table->item(0, 1)->setText(QStringLiteral("12"));
        QCOMPARE(dlg.updatedSummary(), summary);
    }
    void editableValuesPreserveFullPrecision()
    {
        SWMM_Engine e = buildQualityFixture();
        QVERIFY(e);
        const double coverage = 25.1234567890123, loading = 1.23456789012345;
        QCOMPARE(swmm_subcatch_set_coverage(e, 0, 0, coverage), SWMM_OK);
        QCOMPARE(swmm_subcatch_set_initial_loading(e, 0, 0, loading), SWMM_OK);
        SubcatchCompoundEditDialog dlg(makeRef(e, SubcatchCompoundEditRef::LandUse));
        auto *coverageTable = tableByHeader(dlg, QStringLiteral("Land Use"));
        QVERIFY(coverageTable);
        QVERIFY(coverageTable->item(0, 1));
        const double shownCoverage = coverageTable->item(0, 1)->text().toDouble();
        SubcatchCompoundEditDialog loadings(makeRef(e, SubcatchCompoundEditRef::Loadings));
        auto *loadingTable = tableByHeader(loadings, QStringLiteral("Pollutant"));
        QVERIFY(loadingTable);
        QVERIFY(loadingTable->item(0, 1));
        const double shownLoading = loadingTable->item(0, 1)->text().toDouble();
        swmm_engine_destroy(e);
        QCOMPARE(shownCoverage, coverage);
        QCOMPARE(shownLoading, loading);
    }

    void nonfiniteEditsNeverReachEngine_data()
    {
        QTest::addColumn<bool>("loading"); QTest::addColumn<QString>("text");
        QTest::newRow("coverage-nan") << false << QStringLiteral("nan");
        QTest::newRow("loading-nan") << true << QStringLiteral("nan");
        QTest::newRow("loading-infinity") << true << QStringLiteral("inf");
    }

    void nonfiniteEditsNeverReachEngine()
    {
        QFETCH(bool, loading); QFETCH(QString, text);
        SWMM_Engine e = buildQualityFixture(); QVERIFY(e);
        QCOMPARE(swmm_subcatch_set_initial_loading(e, 0, 0, 2.5), SWMM_OK);
        SubcatchCompoundEditDialog dlg(makeRef(e, loading ? SubcatchCompoundEditRef::Loadings : SubcatchCompoundEditRef::LandUse));
        auto *table = tableByHeader(dlg, loading ? QStringLiteral("Pollutant") : QStringLiteral("Land Use"));
        QVERIFY(table);
        QTimer::singleShot(0, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->accept();
        });
        table->item(0, 1)->setText(text);
        QCoreApplication::processEvents();
        double value = -1;
        const int rc = loading ? swmm_subcatch_get_initial_loading(e, 0, 0, &value)
                               : swmm_subcatch_get_coverage(e, 0, 0, &value);
        swmm_engine_destroy(e);
        QCOMPARE(rc, SWMM_OK); QVERIFY(std::isfinite(value));
        QCOMPARE(value, loading ? 2.5 : 60.0);
    }

    void editableTablesHaveAccessiblePurpose()
    {
        SWMM_Engine e = buildQualityFixture(); QVERIFY(e);
        SubcatchCompoundEditDialog dlg(makeRef(e, SubcatchCompoundEditRef::LandUse));
        const auto tables = dlg.findChildren<QTableWidget *>();
        bool named = !tables.isEmpty();
        for (const auto *table : tables) named &= !table->accessibleName().isEmpty();
        swmm_engine_destroy(e);
        QVERIFY2(named, "Each compound-page table needs a distinct accessible purpose.");
        QVERIFY(!dlg.accessibleDescription().isEmpty());
    }

    void coverageMatrixListsAllLandUses()
    {
        SWMM_Engine e = buildQualityFixture();
        QVERIFY(e);
        SubcatchCompoundEditDialog dlg(
            makeRef(e, SubcatchCompoundEditRef::LandUse));

        auto *table = tableByHeader(dlg, QStringLiteral("Land Use"));
        QVERIFY(table);
        // Every DEFINED land use has a row — including the 0% one.
        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("Res"));
        QCOMPARE(table->item(0, 1)->text().toDouble(), 60.0);
        QCOMPARE(table->item(1, 0)->text(), QStringLiteral("Com"));
        QCOMPARE(table->item(1, 1)->text().toDouble(), 0.0);
        // Name column is not editable; percent column is.
        QVERIFY(!(table->item(0, 0)->flags() & Qt::ItemIsEditable));
        QVERIFY(table->item(0, 1)->flags() & Qt::ItemIsEditable);

        swmm_engine_destroy(e);
    }

    void editingCellWritesEngineAndRelists()
    {
        SWMM_Engine e = buildQualityFixture();
        QVERIFY(e);
        SubcatchCompoundEditDialog dlg(
            makeRef(e, SubcatchCompoundEditRef::LandUse));
        auto *table = tableByHeader(dlg, QStringLiteral("Land Use"));
        QVERIFY(table);

        // A land use added while the dialog is open appears after the next
        // refresh (the old populate-once combo never noticed).
        QCOMPARE(swmm_landuse_add(e, "Ind"), SWMM_OK);

        // Editing Com's percent writes the engine and re-lists the matrix.
        table->item(1, 1)->setText(QStringLiteral("40"));
        double pct = 0.0;
        QCOMPARE(swmm_subcatch_get_coverage(e, 0, 1, &pct), SWMM_OK);
        QCOMPARE(pct, 40.0);
        QCOMPARE(table->rowCount(), 3);   // Ind is listed now

        // Editing to 0 removes the coverage.
        table->item(0, 1)->setText(QStringLiteral("0"));
        QCOMPARE(swmm_subcatch_get_coverage(e, 0, 0, &pct), SWMM_OK);
        QCOMPARE(pct, 0.0);

        swmm_engine_destroy(e);
    }

    void sumWarningWhenOver100()
    {
        SWMM_Engine e = buildQualityFixture();
        QVERIFY(e);
        swmm_subcatch_set_coverage(e, 0, 1, 70.0);   // 60 + 70 = 130%
        SubcatchCompoundEditDialog dlg(
            makeRef(e, SubcatchCompoundEditRef::LandUse));
        bool warned = false;
        for (auto *lbl : dlg.findChildren<QLabel *>())
            if (lbl->text().contains(QStringLiteral("Warning")))
                warned = true;
        QVERIFY(warned);
        swmm_engine_destroy(e);
    }

    void loadingsPageRoundTrips()
    {
        SWMM_Engine e = buildQualityFixture();
        QVERIFY(e);
        QCOMPARE(swmm_subcatch_set_initial_loading(e, 0, 0, 1.5), SWMM_OK);

        SubcatchCompoundEditDialog dlg(
            makeRef(e, SubcatchCompoundEditRef::Loadings));
        auto *table = tableByHeader(dlg, QStringLiteral("Pollutant"));
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("TSS"));
        QCOMPARE(table->item(0, 1)->text().toDouble(), 1.5);
        QCOMPARE(table->item(1, 0)->text(), QStringLiteral("Lead"));
        QCOMPARE(table->item(1, 1)->text().toDouble(), 0.0);

        // Edit Lead's loading in place → engine follows.
        table->item(1, 1)->setText(QStringLiteral("2.25"));
        double w = 0.0;
        QCOMPARE(swmm_subcatch_get_initial_loading(e, 0, 1, &w), SWMM_OK);
        QCOMPARE(w, 2.25);

        swmm_engine_destroy(e);
    }
};

QTEST_MAIN(TestSubcatchCoverageLoadings)
#include "test_subcatch_coverage_loadings.moc"
