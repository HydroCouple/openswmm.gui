/**
 * @file      test_gwledgermodel.cpp
 * @brief     V4 gate — the saved run's aquifer water and species ledgers.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * workplans/GW2D_AND_PIPE_REACTION_RESULTS_VISUALIZATION_PLAN_2026-10-14.md V4:
 *   - terms are identified by NAME from the `terms` attribute: the same
 *     physical values stored in a different column order read identically;
 *   - final values and cumulative series are the stored values on `/time`;
 *   - species rows take their names (and concentration units) from the
 *     aquifer concentration datasets, as the engine writes them;
 *   - a file without ledgers, or with unresolvable identities, shows
 *     "unavailable" rows with the reason — never zeros, never a series.
 *
 * Synthetic fixtures are written to GW_RESULTS_TEST_OUTPUT. Set GW_PROBE_H5
 * to a real engine `.2d.h5` to also run the read-only probe check.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "gwresults_fixture.h"
#include "results/gwledgermodel.h"

#include <QFileInfo>
#include <QObject>
#include <QTest>

#include <cmath>
#include <vector>

using openswmmvis::results::GwLedgerModel;

namespace
{

constexpr double kDay0 = 41089.0;          // 2012-06-30 00:00
constexpr double kStep = 300.0 / 86400.0;  // five-minute reports
const std::vector<double> kTimes = {kDay0, kDay0 + kStep, kDay0 + 2 * kStep};

// Water ledger values by term name, one per frame.
const QStringList kWaterTerms = {"recharge", "node", "storage", "continuity_residual"};
const std::vector<std::vector<double>> kWaterByTerm = {
    {0.0, 5.0, 10.0}, {0.0, 2.0, 4.0}, {100.0, 103.0, 106.0}, {0.0, 1e-3, -2e-3}};

/**
 * @brief Write a fixture with the water ledger columns in \p order (indices into kWaterTerms)
 * plus a two-species ledger whose names live on Mesh2_face_gw_sat_conc.
 * @param[in] name Fixture file name.
 * @param[in] order Column order of the stored water ledger.
 * @param[in] species True also writes the species ledger and its name source.
 * @return Fixture path.
 */
QString writeLedgers(const QString &name, const QVector<int> &order, bool species = true)
{
    hid_t f = gwfixture::create(name);
    gwfixture::times(f, kTimes);
    std::vector<double> water;
    QStringList terms;
    for (int t : order)
        terms.append(kWaterTerms[t]);
    for (std::size_t frame = 0; frame < kTimes.size(); ++frame)
        for (int t : order)
            water.push_back(kWaterByTerm[std::size_t(t)][frame]);
    gwfixture::doubles(f, "groundwater_ledger", {kTimes.size(), hsize_t(order.size())}, water);
    gwfixture::stringAttr(f, "groundwater_ledger", "terms", terms.join(','));
    gwfixture::stringAttr(f, "groundwater_ledger", "units", "m3");
    gwfixture::stringAttr(f, "groundwater_ledger", "long_name",
                          "two-zone aquifer domain ledger, cumulative");
    if (species) {
        // value = 100*species + 10*frame + term; GWTracer's final node_out is NaN.
        std::vector<double> mass;
        for (int frame = 0; frame < 3; ++frame)
            for (int s = 0; s < 2; ++s)
                for (int k = 0; k < 3; ++k)
                    mass.push_back(frame == 2 && s == 1 && k == 2 ? std::nan("")
                                                                  : 100.0 * s + 10.0 * frame + k);
        gwfixture::doubles(f, "groundwater_species_ledger", {3, 2, 3}, mass);
        gwfixture::stringAttr(f, "groundwater_species_ledger", "terms", "init,storage,node_out");
        gwfixture::stringAttr(f, "groundwater_species_ledger", "units", "1");
        gwfixture::doubles(f, "Mesh2_face_gw_sat_conc", {3, 2, 1}, std::vector<double>(6, 0.0));
        gwfixture::stringAttr(f, "Mesh2_face_gw_sat_conc", "species_names", "SurfPoll,GWTracer");
        gwfixture::stringAttr(f, "Mesh2_face_gw_sat_conc", "species_units", "MG/L,MG/L");
    }
    H5Fclose(f);
    return gwfixture::path(name);
}

} // namespace

class TestGwLedgerModel : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief Water then species rows, keyed by name, with stored final values and units.
     */
    void readsTermsByNameWithFinalValues()
    {
        GwLedgerModel model;
        QVERIFY(model.load(writeLedgers("ledger_full.h5", {0, 1, 2, 3})));
        QCOMPARE(model.rowCount(), 4 + 2 * 3);
        QCOMPARE(model.key(0), QStringLiteral("gwledger:water:recharge"));
        QCOMPARE(model.key(4), QStringLiteral("gwledger:species:SurfPoll:init"));
        QCOMPARE(model.key(9), QStringLiteral("gwledger:species:GWTracer:node_out"));
        const int storage = model.rowForKey("gwledger:water:storage");
        QCOMPARE(storage, 2);
        QCOMPARE(model.finalValue(storage), 106.0);
        QCOMPARE(model.finalValue(model.rowForKey("gwledger:water:continuity_residual")), -2e-3);
        QCOMPARE(model.data(model.index(storage, GwLedgerModel::ColUnits)).toString(), "m3");
        QCOMPARE(model.data(model.index(storage, GwLedgerModel::ColFinal)).toString(), "106");

        const int tracerStorage = model.rowForKey("gwledger:species:GWTracer:storage");
        QCOMPARE(model.finalValue(tracerStorage), 121.0);
        QCOMPARE(model.data(model.index(tracerStorage, GwLedgerModel::ColLedger)).toString(),
                 "GWTracer");
        QCOMPARE(model.data(model.index(tracerStorage, GwLedgerModel::ColUnits)).toString(),
                 QStringLiteral("MG/L·m3"));
        // A non-finite stored value is missing, not zero.
        const int tracerOut = model.rowForKey("gwledger:species:GWTracer:node_out");
        QVERIFY(std::isnan(model.finalValue(tracerOut)));
        QCOMPARE(model.data(model.index(tracerOut, GwLedgerModel::ColFinal)).toString(), "missing");

        const auto d = model.descriptor(storage);
        QVERIFY(d.isMeshVariable());
        QCOMPARE(d.variableKey, QStringLiteral("gwledger:water:storage"));
        QCOMPARE(d.variableUnits, QStringLiteral("m3"));
        QVERIFY(d.variableLabel.contains("storage") && d.variableLabel.contains("cumulative"));
        QCOMPARE(model.descriptors().size(), 10);
        QVERIFY(model.status().contains("ledger_full.h5"));
    }

    /**
     * @brief The same physical values stored in another column order read identically.
     */
    void termIdentityIsTheNameNotTheColumn()
    {
        GwLedgerModel engineOrder, reordered;
        QVERIFY(engineOrder.load(writeLedgers("ledger_order_a.h5", {0, 1, 2, 3}, false)));
        QVERIFY(reordered.load(writeLedgers("ledger_order_b.h5", {2, 3, 0, 1}, false)));
        for (const QString &term : kWaterTerms) {
            const QString key = "gwledger:water:" + term;
            QVERIFY(engineOrder.rowForKey(key) >= 0);
            QVERIFY(reordered.rowForKey(key) >= 0);
            QCOMPARE(reordered.finalValue(reordered.rowForKey(key)),
                     engineOrder.finalValue(engineOrder.rowForKey(key)));
        }
        QCOMPARE(reordered.finalValue(reordered.rowForKey("gwledger:water:recharge")), 10.0);
    }

    /**
     * @brief A term's series is every stored cumulative value on the file's own time axis.
     */
    void seriesAreCumulativeOnTheFileTimeAxis()
    {
        GwLedgerModel model;
        QVERIFY(model.load(writeLedgers("ledger_series.h5", {0, 1, 2, 3})));
        openswmmvis::plot::SeriesData out;
        QVERIFY(model.series(model.rowForKey("gwledger:water:node"), out));
        QVERIFY(out.ok);
        QCOMPARE(out.periodCount, 3);
        QCOMPARE(out.timesJulian, kTimes);
        QCOMPARE(out.values, (std::vector<double>{0.0, 2.0, 4.0}));

        openswmmvis::plot::SeriesData tail;
        tail.firstPeriod = 2;
        QVERIFY(model.series(model.rowForKey("gwledger:species:SurfPoll:storage"), tail));
        QCOMPARE(tail.timesJulian, (std::vector<double>{kTimes[2]}));
        QCOMPARE(tail.values, (std::vector<double>{21.0}));
    }

    /**
     * @brief An older file without either ledger has unavailable rows and no series.
     */
    void absentLedgersAreUnavailableNotZero()
    {
        hid_t f = gwfixture::create("ledger_absent.h5");
        gwfixture::times(f, kTimes);
        H5Fclose(f);
        GwLedgerModel model;
        QVERIFY(!model.load(gwfixture::path("ledger_absent.h5")));
        QCOMPARE(model.rowCount(), 2);
        for (int row = 0; row < 2; ++row) {
            QCOMPARE(model.data(model.index(row, GwLedgerModel::ColFinal)).toString(),
                     "unavailable");
            QVERIFY(model.key(row).isEmpty());
            QVERIFY(!model.descriptor(row).isValid());
            QVERIFY(std::isnan(model.finalValue(row)));
            QVERIFY(!model.data(model.index(row, 0), Qt::ToolTipRole).toString().isEmpty());
            openswmmvis::plot::SeriesData out;
            QVERIFY(!model.series(row, out));
            QVERIFY(!out.ok && out.values.empty() && !out.errorMessage.isEmpty());
        }
        QVERIFY(model.descriptors().isEmpty());
        QVERIFY(model.status().contains("no groundwater ledger"));
    }

    /**
     * @brief Species without resolvable names, or terms that do not match the columns,
     * are refused rather than guessed.
     */
    void unresolvedIdentitiesAreRefused()
    {
        hid_t f = gwfixture::create("ledger_unresolved.h5");
        gwfixture::times(f, kTimes);
        gwfixture::doubles(f, "groundwater_ledger", {3, 2}, std::vector<double>(6, 1.0));
        gwfixture::stringAttr(f, "groundwater_ledger", "terms", "recharge,node,storage");
        gwfixture::doubles(f, "groundwater_species_ledger", {3, 2, 1},
                           std::vector<double>(6, 1.0));
        gwfixture::stringAttr(f, "groundwater_species_ledger", "terms", "storage");
        H5Fclose(f);
        GwLedgerModel model;
        QVERIFY(!model.load(gwfixture::path("ledger_unresolved.h5")));
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(model.data(model.index(0, 0), Qt::ToolTipRole).toString().contains("term"));
        QVERIFY(model.data(model.index(1, 0), Qt::ToolTipRole).toString().contains("species"));
        QVERIFY(model.descriptors().isEmpty());
    }

    /**
     * @brief No file (no run yet, or a live run) explains itself and has no rows.
     */
    void noFileExplains()
    {
        GwLedgerModel model;
        QVERIFY(!model.load(QString()));
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.status().isEmpty());
    }

    /**
     * @brief Read-only check against a real engine file (GW_PROBE_H5); skipped by default.
     */
    void realEngineFileWhenProvided()
    {
        const QString probe = qEnvironmentVariable("GW_PROBE_H5");
        if (probe.isEmpty() || !QFileInfo::exists(probe))
            QSKIP("Set GW_PROBE_H5 to a real .2d.h5 to run this check.");
        GwLedgerModel model;
        QVERIFY(model.load(probe));
        qInfo().noquote() << model.status();
        for (int row = 0; row < model.rowCount(); ++row)
            qInfo().noquote() << model.data(model.index(row, 0)).toString()
                              << model.data(model.index(row, 1)).toString()
                              << model.data(model.index(row, 2)).toString()
                              << model.data(model.index(row, 3)).toString();
        QVERIFY(model.rowForKey("gwledger:water:continuity_residual") >= 0);
    }
};

QTEST_GUILESS_MAIN(TestGwLedgerModel)
#include "test_gwledgermodel.moc"
