// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      test_traceanalysisstore.cpp
 * @brief     Tests for traceanalysisstore.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "output/traceanalysisstore.h"
#include <QDir>
#include <QUuid>
#include <QtTest>
#include <cmath>
using namespace openswmmvis::trace;
class TraceAnalysisStoreTest : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Persist/reload a synthetic SI trace analysis and require travel time, ratio,
     * geometry, identity and style to survive without native output.
     * Creates a uniquely named persistent GeoPackage under SWMMVIS_TRACE_TEST_OUTPUT.
     * Checks incompatible run identity refusal, style-only update and pre-cancelled save
     * without extra rows. Native trace handle closes on the success path before persistence;
     * early Qt assertions can terminate before that explicit cleanup.
     *
     */
    void savedResultsSurviveWithoutOutput()
    {
        QString directory = QStringLiteral(SWMMVIS_TRACE_TEST_OUTPUT);
        QVERIFY(QDir().mkpath(directory));
        QString path =
            directory + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".gpkg";
        auto d = std::make_shared<Dataset>();
        d->snapshot.nodes = {{"S", 0, 0, {0, 0}, true}, {"O", 1, 0, {100, 0}, true}};
        d->snapshot.links = {{"pipe", 0, 1, 0, 100, {{0, 0}, {100, 0}}}};
        d->snapshot.modelFingerprint = "fixture";
        d->runId = "run-a";
        d->sourceId = "source-a";
        d->label = "Baseline · Run 1";
        d->fingerprint = "content-a";
        d->packagePath = path;
        d->nodes.resize(2);
        d->nodes[0].lateral_in_m3s = 2;
        d->links.resize(1);
        d->links[0].net_flow_m3s = d->links[0].absolute_flow_m3s = 2;
        d->links[0].absolute_velocity_mps = 1;
        QString error;
        auto h = createHandle(d->snapshot, d->options, &error);
        QVERIFY2(h, qPrintable(error));
        QCOMPARE(swmm_trace_set_averages(h, d->nodes.data(), 2, d->links.data(), 1, nullptr), 0);
        swmm_trace_get_info(h, &d->info);
        swmm_trace_get_averages(h, d->nodes.data(), 2, d->links.data(), 1);
        Result r;
        r.dataset = d;
        r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        r.nodes.resize(2);
        r.links.resize(1);
        r.style = {{"colorField", "time"}};
        QCOMPARE(swmm_trace_estimate(h, 0, 0, r.nodes.data(), 2, r.links.data(), 1, &r.summary,
                                     nullptr, nullptr),
                 0);
        swmm_trace_close(h);
        QVERIFY2(AnalysisStore::prepare(path, *d, &error), qPrintable(error));
        QVERIFY2(AnalysisStore::save(path, r, &error), qPrintable(error));
        auto loaded = AnalysisStore::read(path, r.id, &error);
        QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->dataset->runId, d->runId);
        QCOMPARE(loaded->links[0].time_s, 100.0);
        QCOMPARE(loaded->links[0].ratio, 1.0);
        QCOMPARE(loaded->dataset->snapshot.links[0].points, d->snapshot.links[0].points);
        QCOMPARE(loaded->style, r.style);
        auto replacement = std::make_shared<Dataset>(*d);
        replacement->runId = "run-b";
        r.dataset = replacement;
        QVERIFY(!AnalysisStore::save(path, r, &error));
        QCOMPARE(AnalysisStore::analyses(path, &error).size(), 1);
        QVERIFY(AnalysisStore::saveStyle(path, loaded->id, {{"colorField", "ratio"}}, &error));
        QCOMPARE(AnalysisStore::read(path, loaded->id, &error)->style["colorField"].toString(),
                 QString("ratio"));
        std::atomic_bool cancel{true};
        r.dataset = d;
        r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(!AnalysisStore::save(path, r, &error, &cancel));
        QCOMPARE(AnalysisStore::analyses(path, &error).size(), 1);
    }
};
QTEST_GUILESS_MAIN(TraceAnalysisStoreTest)
#include "test_traceanalysisstore.moc"
