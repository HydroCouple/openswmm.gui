/*!
 * \file   test_binsampler.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for Slice Z.7 — animation bin sampler.
 *
 *         Success criterion (RENDERING_RULE_MODEL_PLAN.md §16, Z.7):
 *           - sampleBreaksAcrossFrames is deterministic per
 *             (frameCount, sampleRate).
 *           - sampleRate ∈ {0.0, 0.2, 0.5, 1.0} all pick at least one
 *             frame (no zero-size samples).
 *           - Selected frames span [0, frameCount-1] inclusive (first
 *             and last frames sampled at sampleRate ≥ 0.2 when
 *             frameCount ≥ 5).
 *           - Breaks computed via the binner match what a direct
 *             IntervalBinner::computeBreaks() call yields on the
 *             flattened sample.
 *           - Empty input returns empty breaks.
 *           - Manual binner round-trips its breaks regardless of frames.
 */

#include <QtTest/QtTest>

#include "render/binsampler.h"
#include "render/intervalbinner.h"

using namespace OpenSWMM::Render;

class TestBinSampler : public QObject
{
    Q_OBJECT
private slots:
    // sampledFrameIndices
    void indices_emptyForZeroFrames();
    void indices_singleFrameAlwaysReturnsZero();
    void indices_sampleRateOneReturnsAllFrames();
    void indices_sampleRate20pickAtLeastOne();
    void indices_spreadsAcrossRange();
    void indices_areDeterministic();
    void indices_sampleRateClampsAtBounds();

    // sampleBreaksAcrossFrames
    void breaks_emptyFramesReturnsEmpty();
    void breaks_matchDirectBinnerComputeOnFlattenedSamples();
    void breaks_manualBinnerReturnsItsBreaksVerbatim();
    void breaks_quantileSamplerProducesMonotonicBreaks();
    void breaks_equalIntervalSpansMinToMax();
    void breaks_singleFrameInputWorks();
};

// ── sampledFrameIndices ─────────────────────────────────────────────

void TestBinSampler::indices_emptyForZeroFrames()
{
    QCOMPARE(sampledFrameIndices(0, 0.5).size(), 0);
}

void TestBinSampler::indices_singleFrameAlwaysReturnsZero()
{
    QCOMPARE(sampledFrameIndices(1, 0.2), QVector<int>{0});
    QCOMPARE(sampledFrameIndices(1, 1.0), QVector<int>{0});
    QCOMPARE(sampledFrameIndices(1, 0.0), QVector<int>{0});
}

void TestBinSampler::indices_sampleRateOneReturnsAllFrames()
{
    const auto idx = sampledFrameIndices(5, 1.0);
    QCOMPARE(idx.size(), 5);
    QCOMPARE(idx, QVector<int>({0, 1, 2, 3, 4}));
}

void TestBinSampler::indices_sampleRate20pickAtLeastOne()
{
    // 10 frames × 0.2 = 2 frames.
    const auto idx = sampledFrameIndices(10, 0.2);
    QVERIFY(idx.size() >= 1);
    QCOMPARE(idx.first(), 0);
    QCOMPARE(idx.last(),  9);
}

void TestBinSampler::indices_spreadsAcrossRange()
{
    // 100 frames × 0.5 = 50 frames, evenly spread.
    const auto idx = sampledFrameIndices(100, 0.5);
    QCOMPARE(idx.first(), 0);
    QCOMPARE(idx.last(),  99);
    // No duplicate indices.
    QSet<int> seen;
    for (int i : idx) {
        QVERIFY(!seen.contains(i));
        seen.insert(i);
    }
}

void TestBinSampler::indices_areDeterministic()
{
    const auto a = sampledFrameIndices(47, 0.31);
    const auto b = sampledFrameIndices(47, 0.31);
    QCOMPARE(a, b);
}

void TestBinSampler::indices_sampleRateClampsAtBounds()
{
    // Negative → at least one frame.
    QCOMPARE(sampledFrameIndices(10, -0.5).size(), 1);
    // > 1 → clamped to 1 → every frame.
    QCOMPARE(sampledFrameIndices(10, 2.0).size(), 10);
}

// ── sampleBreaksAcrossFrames ────────────────────────────────────────

void TestBinSampler::breaks_emptyFramesReturnsEmpty()
{
    IntervalBinner binner;
    binner.setMethod(BinMethod::EqualInterval);
    binner.setBinCount(5);
    QCOMPARE(sampleBreaksAcrossFrames(binner, {}).size(), 0);
}

void TestBinSampler::breaks_matchDirectBinnerComputeOnFlattenedSamples()
{
    QVector<QVector<double>> frames;
    frames.append({1.0, 2.0, 3.0});
    frames.append({4.0, 5.0, 6.0});

    IntervalBinner binner;
    binner.setMethod(BinMethod::EqualInterval);
    binner.setBinCount(3);

    const auto fromSampler = sampleBreaksAcrossFrames(binner, frames, 1.0);
    QVector<double> flat = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0 };
    const auto fromDirect = binner.computeBreaks(flat);
    QCOMPARE(fromSampler, fromDirect);
}

void TestBinSampler::breaks_manualBinnerReturnsItsBreaksVerbatim()
{
    IntervalBinner binner;
    binner.setMethod(BinMethod::Manual);
    binner.setManualBreaks({ 0.5, 1.5, 2.5 });
    binner.setBinCount(4);

    QVector<QVector<double>> frames;
    frames.append({0.0, 1.0, 2.0, 3.0});

    const auto breaks = sampleBreaksAcrossFrames(binner, frames, 1.0);
    QCOMPARE(breaks, QVector<double>({0.5, 1.5, 2.5}));
}

void TestBinSampler::breaks_quantileSamplerProducesMonotonicBreaks()
{
    QVector<QVector<double>> frames;
    for (int f = 0; f < 5; ++f) {
        QVector<double> values;
        for (int i = 0; i < 100; ++i)
            values.append(static_cast<double>(i + f));
        frames.append(values);
    }

    IntervalBinner binner;
    binner.setMethod(BinMethod::Quantile);
    binner.setBinCount(5);

    const auto breaks = sampleBreaksAcrossFrames(binner, frames, 1.0);
    QCOMPARE(breaks.size(), 4);
    for (int i = 1; i < breaks.size(); ++i)
        QVERIFY2(breaks[i] >= breaks[i - 1],
                 "Quantile breaks must be non-decreasing");
}

void TestBinSampler::breaks_equalIntervalSpansMinToMax()
{
    QVector<QVector<double>> frames;
    frames.append({0.0, 10.0});
    frames.append({-5.0, 20.0});  // min -5, max 20

    IntervalBinner binner;
    binner.setMethod(BinMethod::EqualInterval);
    binner.setBinCount(5);

    const auto breaks = sampleBreaksAcrossFrames(binner, frames, 1.0);
    QCOMPARE(breaks.size(), 4);
    // First break > min, last break < max.
    QVERIFY(breaks.first() > -5.0);
    QVERIFY(breaks.last()  <  20.0);
}

void TestBinSampler::breaks_singleFrameInputWorks()
{
    QVector<QVector<double>> frames;
    frames.append({1.0, 2.0, 3.0, 4.0, 5.0});

    IntervalBinner binner;
    binner.setMethod(BinMethod::EqualInterval);
    binner.setBinCount(2);

    const auto breaks = sampleBreaksAcrossFrames(binner, frames, 1.0);
    QCOMPARE(breaks.size(), 1);
}

QTEST_MAIN(TestBinSampler)
#include "test_binsampler.moc"
