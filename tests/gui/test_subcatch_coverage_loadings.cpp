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

namespace {

SWMM_Engine buildQualityFixture()
{
    SWMM_Engine e = swmm_engine_new();
    if (!e) return nullptr;
    swmm_subcatch_add(e, "S1");
    swmm_pollutant_add(e, "TSS", 0);
    swmm_pollutant_add(e, "Lead", 1);
    swmm_landuse_add(e, "Res");
    swmm_landuse_add(e, "Com");
    swmm_subcatch_set_coverage(e, 0, 0, 60.0);   // S1/Res
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
