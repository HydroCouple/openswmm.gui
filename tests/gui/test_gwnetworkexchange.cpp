/**
 * @file      test_gwnetworkexchange.cpp
 * @brief     V3 gate — per-node aquifer ↔ 1D node exchange from a saved run.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * workplans/GW2D_AND_PIPE_REACTION_RESULTS_VISUALIZATION_PLAN_2026-10-14.md V3
 * and D-V5, on a two-node `/groundwater_node_exchange_cum` fixture:
 *   - the cumulative series is the stored column (+ out of the aquifer);
 *   - the rate is Δcumulative / Δt over each report interval, at the
 *     interval's end, with the sign kept (an inflowing node is negative)
 *     and uneven report intervals honoured; the first report has no rate;
 *   - a node not in `node_names` is missing, never zero;
 *   - a missing value is a gap in both series; absent or malformed datasets
 *     are unavailable.
 *
 * Synthetic fixtures are written to GW_RESULTS_TEST_OUTPUT. Set GW_PROBE_H5
 * to a real engine `.2d.h5` to also run the read-only probe check.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "gwresults_fixture.h"
#include "plot/gwnetworkexchangesource.h"

#include <QFileInfo>
#include <QObject>
#include <QTest>

#include <cmath>
#include <vector>

using openswmmvis::plot::GwNetworkExchangeSource;
using openswmmvis::plot::ResultDescriptor;
using openswmmvis::plot::SeriesData;

namespace
{

constexpr double kDay0 = 41089.0;
constexpr double kSecond = 1.0 / 86400.0;
// Reports at 0, 300, 600 and 1200 s: the last interval is twice as long.
const std::vector<double> kTimes = {kDay0, kDay0 + 300 * kSecond, kDay0 + 600 * kSecond,
                                    kDay0 + 1200 * kSecond};

/**
 * @brief Write `/groundwater_node_exchange_cum` for nodes N1 (out of the aquifer), N2 (into it)
 * and N3 (a missing value at report 2).
 * @param[in] name Fixture file name.
 * @param[in] names `node_names` attribute text.
 * @return Fixture path.
 */
QString writeExchange(const QString &name, const QString &names = "N1,N2,N3")
{
    hid_t f = gwfixture::create(name);
    gwfixture::times(f, kTimes);
    const double nan = std::nan("");
    // [time][node]
    const std::vector<double> cum = {0.0,  0.0,   0.0, //
                                     30.0, -6.0,  1.0, //
                                     90.0, -12.0, nan, //
                                     90.0, -30.0, 4.0};
    gwfixture::doubles(f, "groundwater_node_exchange_cum", {4, 3}, cum);
    gwfixture::stringAttr(f, "groundwater_node_exchange_cum", "node_names", names);
    gwfixture::stringAttr(f, "groundwater_node_exchange_cum", "units", "m3");
    H5Fclose(f);
    return gwfixture::path(name);
}

/** @return \p d's series for \p node from \p first on. */
SeriesData seriesFor(const GwNetworkExchangeSource &source, const QString &node,
                     const char *key, int first = 0)
{
    SeriesData out;
    out.firstPeriod = first;
    for (const ResultDescriptor &d : source.nodeDescriptors())
        if (d.variableKey == QLatin1String(key))
            source.series(node, d, out);
    return out;
}

} // namespace

class TestGwNetworkExchange : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief Two descriptors keyed and united as the plan names them, sign stated.
     */
    void offersRateAndCumulativeDescriptors()
    {
        GwNetworkExchangeSource source;
        QVERIFY(source.open(writeExchange("exchange_nodes.h5")));
        QVERIFY(source.isAvailable());
        QCOMPARE(source.nodeNames(), (QStringList{"N1", "N2", "N3"}));
        QVERIFY(source.hasNode("N2") && !source.hasNode("N4") && !source.hasNode("n1"));
        const auto descriptors = source.nodeDescriptors();
        QCOMPARE(descriptors.size(), 2);
        QCOMPARE(descriptors[0].variableKey, QStringLiteral("gw_node_exchange"));
        QCOMPARE(descriptors[0].variableUnits, QStringLiteral("m3/s"));
        QCOMPARE(descriptors[1].variableKey, QStringLiteral("gw_node_exchange_cum"));
        QCOMPARE(descriptors[1].variableUnits, QStringLiteral("m3"));
        for (const auto &d : descriptors) {
            QVERIFY(d.isMeshVariable());
            QVERIFY(GwNetworkExchangeSource::isExchangeDescriptor(d));
            QVERIFY2(d.variableLabel.contains("out of aquifer"), qPrintable(d.variableLabel));
        }
    }

    /**
     * @brief Cumulative values as stored; rate = Δcum/Δt at the interval end, sign kept.
     */
    void rateIsDeltaCumulativeOverDeltaTWithSignKept()
    {
        GwNetworkExchangeSource source;
        QVERIFY(source.open(writeExchange("exchange_rate.h5")));

        const SeriesData cum = seriesFor(source, "N1", GwNetworkExchangeSource::kCumulativeKey);
        QVERIFY(cum.ok);
        QCOMPARE(cum.periodCount, 4);
        QCOMPARE(cum.timesJulian, kTimes);
        QCOMPARE(cum.values, (std::vector<double>{0.0, 30.0, 90.0, 90.0}));

        const SeriesData out = seriesFor(source, "N1", GwNetworkExchangeSource::kRateKey);
        QVERIFY(out.ok);
        QCOMPARE(out.timesJulian, (std::vector<double>{kTimes[1], kTimes[2], kTimes[3]}));
        const std::vector<double> outExpected = {30.0 / 300.0, 60.0 / 300.0, 0.0};
        QCOMPARE(out.values.size(), outExpected.size());
        for (std::size_t i = 0; i < outExpected.size(); ++i)
            QVERIFY2(std::abs(out.values[i] - outExpected[i]) < 1e-9,
                     qPrintable(QString::number(out.values[i], 'g', 17)));

        // Into the aquifer is negative, as stored; the 600 s interval halves the rate.
        const SeriesData in = seriesFor(source, "N2", GwNetworkExchangeSource::kRateKey);
        const std::vector<double> inExpected = {-6.0 / 300.0, -6.0 / 300.0, -18.0 / 600.0};
        QCOMPARE(in.values.size(), inExpected.size());
        for (std::size_t i = 0; i < inExpected.size(); ++i)
            QVERIFY(std::abs(in.values[i] - inExpected[i]) < 1e-9);
    }

    /**
     * @brief A live tail asks from a later period; the rate still uses the previous report.
     */
    void tailUsesThePreviousReport()
    {
        GwNetworkExchangeSource source;
        QVERIFY(source.open(writeExchange("exchange_tail.h5")));
        const SeriesData tail = seriesFor(source, "N1", GwNetworkExchangeSource::kRateKey, 2);
        QVERIFY(tail.ok);
        QCOMPARE(tail.periodCount, 4);
        QCOMPARE(tail.timesJulian, (std::vector<double>{kTimes[2], kTimes[3]}));
        QVERIFY(std::abs(tail.values[0] - 0.2) < 1e-9);
        const SeriesData done = seriesFor(source, "N1", GwNetworkExchangeSource::kRateKey, 4);
        QVERIFY(done.ok && done.values.empty());
    }

    /**
     * @brief A node without a bed is missing (refused), never a zero series.
     */
    void unknownNodeIsMissingNotZero()
    {
        GwNetworkExchangeSource source;
        QVERIFY(source.open(writeExchange("exchange_unknown.h5")));
        for (const char *key :
             {GwNetworkExchangeSource::kRateKey, GwNetworkExchangeSource::kCumulativeKey}) {
            const SeriesData out = seriesFor(source, "J99", key);
            QVERIFY(!out.ok);
            QVERIFY(out.values.empty() && out.timesJulian.empty());
            QVERIFY2(out.errorMessage.contains("missing"), qPrintable(out.errorMessage));
        }
    }

    /**
     * @brief A missing cumulative value is a gap in the cumulative series and in both rates
     * that use it.
     */
    void missingValueIsAGap()
    {
        GwNetworkExchangeSource source;
        QVERIFY(source.open(writeExchange("exchange_gap.h5")));
        const SeriesData cum = seriesFor(source, "N3", GwNetworkExchangeSource::kCumulativeKey);
        QCOMPARE(cum.values.size(), std::size_t(4));
        QVERIFY(std::isnan(cum.values[2]));
        const SeriesData out = seriesFor(source, "N3", GwNetworkExchangeSource::kRateKey);
        QCOMPARE(out.values.size(), std::size_t(3));
        QVERIFY(std::abs(out.values[0] - 1.0 / 300.0) < 1e-9);
        QVERIFY(std::isnan(out.values[1]) && std::isnan(out.values[2]));
    }

    /**
     * @brief Absent dataset or names that do not match the columns: unavailable.
     */
    void absentOrMalformedIsUnavailable()
    {
        hid_t f = gwfixture::create("exchange_absent.h5");
        gwfixture::times(f, kTimes);
        H5Fclose(f);
        GwNetworkExchangeSource absent;
        QVERIFY(!absent.open(gwfixture::path("exchange_absent.h5")));
        QVERIFY(!absent.isAvailable() && !absent.error().isEmpty());
        SeriesData out;
        absent.series("N1", ResultDescriptor::forMeshVariable("gw_node_exchange", {}, {}), out);
        QVERIFY(!out.ok && out.values.empty() && !out.errorMessage.isEmpty());

        GwNetworkExchangeSource malformed;
        QVERIFY(!malformed.open(writeExchange("exchange_names_mismatch.h5", "N1,N2")));
        QVERIFY(malformed.error().contains("node names"));
        GwNetworkExchangeSource duplicate;
        QVERIFY(!duplicate.open(writeExchange("exchange_names_duplicate.h5", "N1,N1,N3")));
    }

    /**
     * @brief Read-only check against a real engine file (GW_PROBE_H5); skipped by default.
     */
    void realEngineFileWhenProvided()
    {
        const QString probe = qEnvironmentVariable("GW_PROBE_H5");
        if (probe.isEmpty() || !QFileInfo::exists(probe))
            QSKIP("Set GW_PROBE_H5 to a real .2d.h5 to run this check.");
        GwNetworkExchangeSource source;
        QVERIFY2(source.open(probe), qPrintable(source.error()));
        QVERIFY(!source.nodeNames().isEmpty());
        const QString node = qEnvironmentVariable("GW_PROBE_NODE", source.nodeNames().first());
        const SeriesData cum = seriesFor(source, node, GwNetworkExchangeSource::kCumulativeKey);
        const SeriesData rate = seriesFor(source, node, GwNetworkExchangeSource::kRateKey);
        QVERIFY(cum.ok && rate.ok);
        qInfo().noquote() << "nodes" << source.nodeNames().size() << "frames" << cum.periodCount
                          << "node" << node << "final cumulative" << cum.values.back()
                          << "last rate" << rate.values.back();
        QVERIFY(!seriesFor(source, "__no_such_node__", GwNetworkExchangeSource::kRateKey).ok);
    }
};

QTEST_GUILESS_MAIN(TestGwNetworkExchange)
#include "test_gwnetworkexchange.moc"
