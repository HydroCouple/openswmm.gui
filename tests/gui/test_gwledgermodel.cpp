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
 *     "unavailable" rows with the reason — never zeros, never a series;
 *   - review G-07: the layered-transport ledger and the aquifer boundary
 *     water and species totals (summed over boundary edges) are rows too,
 *     present only when the run wrote them.
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

/**
 * @brief Add the layered-transport ledger and the aquifer boundary totals (review G-07) to a
 * writeLedgers() fixture, in the engine's shapes and attributes.
 * @details Transport [time, species, term]: 1000 + 100*species + 10*frame + term.
 * Boundary water [time, boundary, term] over two edges: (boundary+1)*(frame+1)*(term+1), so the
 * summed final accepted_in (term 2) is 27. Boundary species [time, species, boundary, term]:
 * 1000*species + 100*boundary + 10*frame + term, except SurfPoll's frame-2 advective_out on
 * boundary 1, which is NaN.
 * @param[in] name Fixture file name.
 * @return Fixture path.
 */
QString writeTransportAndBoundaries(const QString &name)
{
    writeLedgers(name, {0, 1, 2, 3});
    hid_t f = H5Fopen(gwfixture::path(name).toUtf8().constData(), H5F_ACC_RDWR, H5P_DEFAULT);
    std::vector<double> transport;
    for (int frame = 0; frame < 3; ++frame)
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < 3; ++k)
                transport.push_back(1000.0 + 100.0 * s + 10.0 * frame + k);
    gwfixture::doubles(f, "groundwater_transport_ledger", {3, 2, 3}, transport);
    gwfixture::stringAttr(f, "groundwater_transport_ledger", "terms", "init,storage,boundary_in");
    gwfixture::stringAttr(f, "groundwater_transport_ledger", "layout",
                          "[time,species,term]; native concentration x m3");

    std::vector<double> water;
    for (int frame = 0; frame < 3; ++frame)
        for (int b = 0; b < 2; ++b)
            for (int k = 0; k < 4; ++k)
                water.push_back(double((b + 1) * (frame + 1) * (k + 1)));
    gwfixture::doubles(f, "groundwater_boundary_water", {3, 2, 4}, water);
    gwfixture::stringAttr(f, "groundwater_boundary_water", "terms",
                          "requested_in,requested_out,accepted_in,accepted_out");
    gwfixture::stringAttr(f, "groundwater_boundary_water", "layout",
                          "[time,boundary,term]; gross cumulative m3");
    gwfixture::stringAttr(f, "groundwater_boundary_water", "cell_local_edge", "4:0,7:2");

    std::vector<double> mass;
    for (int frame = 0; frame < 3; ++frame)
        for (int s = 0; s < 2; ++s)
            for (int b = 0; b < 2; ++b)
                for (int k = 0; k < 4; ++k)
                    mass.push_back(frame == 2 && s == 0 && b == 1 && k == 1
                                       ? std::nan("")
                                       : 1000.0 * s + 100.0 * b + 10.0 * frame + k);
    gwfixture::doubles(f, "groundwater_boundary_species", {3, 2, 2, 4}, mass);
    gwfixture::stringAttr(f, "groundwater_boundary_species", "terms",
                          "advective_in,advective_out,imposed_in,imposed_out");
    gwfixture::stringAttr(f, "groundwater_boundary_species", "layout",
                          "[time,species,boundary,term]; gross cumulative native concentration x m3");
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
     * @brief G-07: the layered-transport ledger and the aquifer boundary totals follow the
     * water and species ledgers, keyed by name, with boundaries summed and NaN kept missing.
     */
    void transportAndBoundaryTotalsAreRows()
    {
        GwLedgerModel model;
        QVERIFY(model.load(writeTransportAndBoundaries("ledger_transport_boundary.h5")));
        // water 4 + species 2x3 + transport 2x3 + boundary water 4 + boundary species 2x4
        QCOMPARE(model.rowCount(), 4 + 6 + 6 + 4 + 8);
        QCOMPARE(model.descriptors().size(), 28);
        QCOMPARE(model.key(10), QStringLiteral("gwledger:transport:SurfPoll:init"));
        QCOMPARE(model.key(16), QStringLiteral("gwledger:boundary:water:requested_in"));
        QCOMPARE(model.key(20), QStringLiteral("gwledger:boundary:species:SurfPoll:advective_in"));
        // The earlier ledgers keep their keys and values.
        QCOMPARE(model.finalValue(model.rowForKey("gwledger:species:GWTracer:storage")), 121.0);

        const int transport = model.rowForKey("gwledger:transport:GWTracer:boundary_in");
        QCOMPARE(model.finalValue(transport), 1122.0);
        QCOMPARE(model.data(model.index(transport, GwLedgerModel::ColLedger)).toString(),
                 QStringLiteral("Transport: GWTracer"));
        QCOMPARE(model.data(model.index(transport, GwLedgerModel::ColUnits)).toString(),
                 QStringLiteral("MG/L·m3"));

        const int accepted = model.rowForKey("gwledger:boundary:water:accepted_in");
        QCOMPARE(model.finalValue(accepted), 27.0);
        QCOMPARE(model.data(model.index(accepted, GwLedgerModel::ColLedger)).toString(),
                 QStringLiteral("Boundary water"));
        QCOMPARE(model.data(model.index(accepted, GwLedgerModel::ColUnits)).toString(), "m3");
        QVERIFY(model.data(model.index(accepted, 0), Qt::ToolTipRole)
                    .toString()
                    .contains("2 aquifer boundary edges"));
        openswmmvis::plot::SeriesData out;
        QVERIFY(model.series(accepted, out));
        QCOMPARE(out.timesJulian, kTimes);
        QCOMPARE(out.values, (std::vector<double>{9.0, 18.0, 27.0}));

        // 2 x (1000 + 10*2 + 2) + 100: both edges of GWTracer's imposed_in at the last report.
        const int imposed = model.rowForKey("gwledger:boundary:species:GWTracer:imposed_in");
        QCOMPARE(model.finalValue(imposed), 2144.0);
        QCOMPARE(model.data(model.index(imposed, GwLedgerModel::ColLedger)).toString(),
                 QStringLiteral("Boundary: GWTracer"));
        QCOMPARE(model.data(model.index(imposed, GwLedgerModel::ColUnits)).toString(),
                 QStringLiteral("MG/L·m3"));
        // One edge's missing value makes the total missing, not a partial sum.
        const int partial = model.rowForKey("gwledger:boundary:species:SurfPoll:advective_out");
        QVERIFY(std::isnan(model.finalValue(partial)));
        QCOMPARE(model.data(model.index(partial, GwLedgerModel::ColFinal)).toString(), "missing");
        openswmmvis::plot::SeriesData earlier;
        QVERIFY(model.series(partial, earlier));
        QCOMPARE(earlier.values[1], 2.0 * (10.0 + 1.0) + 100.0);

        const auto d = model.descriptor(imposed);
        QVERIFY(d.isMeshVariable());
        QCOMPARE(d.variableKey, QStringLiteral("gwledger:boundary:species:GWTracer:imposed_in"));
        QVERIFY(d.variableLabel.contains("Boundary: GWTracer") &&
                d.variableLabel.contains("imposed_in"));
    }

    /**
     * @brief A transport or boundary dataset that is present but unreadable is one unavailable
     * row with the reason; an absent one adds no row (readsTermsByNameWithFinalValues).
     */
    void unreadableOptionalLedgersAreUnavailable()
    {
        const QString name = "ledger_optional_bad.h5";
        writeLedgers(name, {0, 1, 2, 3});
        hid_t f = H5Fopen(gwfixture::path(name).toUtf8().constData(), H5F_ACC_RDWR, H5P_DEFAULT);
        // Three species rows, but the files name two.
        gwfixture::doubles(f, "groundwater_transport_ledger", {3, 3, 1},
                           std::vector<double>(9, 1.0));
        gwfixture::stringAttr(f, "groundwater_transport_ledger", "terms", "storage");
        // Two term names for four columns.
        gwfixture::doubles(f, "groundwater_boundary_water", {3, 1, 4},
                           std::vector<double>(12, 1.0));
        gwfixture::stringAttr(f, "groundwater_boundary_water", "terms", "accepted_in,accepted_out");
        // No boundary edges to sum (the buffer is only non-null for the empty write).
        gwfixture::doubles(f, "groundwater_boundary_species", {3, 2, 0, 4},
                           std::vector<double>(1, 0.0));
        gwfixture::stringAttr(f, "groundwater_boundary_species", "terms",
                              "advective_in,advective_out,imposed_in,imposed_out");
        H5Fclose(f);

        GwLedgerModel model;
        QVERIFY(model.load(gwfixture::path(name)));
        QCOMPARE(model.rowCount(), 10 + 3);
        QCOMPARE(model.descriptors().size(), 10);
        const QStringList ledgers = {"Transport", "Boundary water", "Boundary species"};
        const QStringList reasons = {"species", "term", "no boundary edges"};
        for (int i = 0; i < 3; ++i) {
            const int row = 10 + i;
            QCOMPARE(model.data(model.index(row, GwLedgerModel::ColLedger)).toString(),
                     ledgers[i]);
            QCOMPARE(model.data(model.index(row, GwLedgerModel::ColFinal)).toString(),
                     "unavailable");
            QVERIFY(model.key(row).isEmpty());
            QVERIFY2(model.data(model.index(row, 0), Qt::ToolTipRole)
                         .toString()
                         .contains(reasons[i]),
                     qPrintable(model.data(model.index(row, 0), Qt::ToolTipRole).toString()));
        }
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
