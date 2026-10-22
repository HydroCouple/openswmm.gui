/**
 * @file      test_symbollevels.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tests for Slice Z.11 — Symbol Levels paint-order algorithm.
 *
 *         Success criterion (RENDERING_RULE_MODEL_PLAN.md §16, Z.11):
 *           - Default (disabled) order matches the legacy per-feature
 *             bottom-up scheme.
 *           - Enabled order is level-major; within a level, features
 *             paint in input order; within a feature, symbol layers
 *             paint in input order.
 *           - Missing level prop defaults to 0.
 *           - Negative levels work and sort before 0.
 *           - Stable: equal-keyed entries preserve input order.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QtTest/QtTest>

#include "render/symbollayer.h"
#include "render/symbollevels.h"
#include "render/symbolstyle.h"

using namespace OpenSWMM::Render;

class TestSymbolLevels : public QObject
{
    Q_OBJECT
private slots:
    // Level prop helpers
    /**
     *
     * @brief Require absent SymbolLayer level property to read as zero.
     *
     */
    void level_defaultZeroWhenMissing();
    /**
     *
     * @brief Require a written positive level 7 to read back exactly.
     *
     */
    void level_setterRoundTrips();
    /**
     *
     * @brief Require a written negative level -3 to remain negative.
     *
     */
    void level_negativeValuesPreserved();

    // Disabled (legacy) order
    /**
     *
     * @brief Require legacy disabled ordering to traverse all layers of each feature before the
     * next feature.
     *
     */
    void disabled_emitsPerFeatureBottomUp();
    /**
     *
     * @brief Require exactly one paint step for one feature with one layer.
     *
     */
    void disabled_singleFeatureSingleLayerIsOneStep();
    /**
     *
     * @brief Require no paint steps for an empty feature list.
     *
     */
    void disabled_emptyFeaturesEmptySteps();

    // Enabled (level-major) order
    /**
     *
     * @brief Require enabled ordering to paint both level-zero outlines before both level-one
     * fills.
     *
     */
    void enabled_groupsByLevelAcrossFeatures();
    /**
     *
     * @brief Require equal-level paint steps to preserve three-feature input order.
     *
     */
    void enabled_withinLevelKeepsFeatureOrder();
    /**
     *
     * @brief Require same-feature equal-level steps to preserve symbol-layer input order.
     *
     */
    void enabled_withinFeatureKeepsLayerOrder();
    /**
     *
     * @brief Require absent and explicit zero levels to share stable input ordering.
     *
     */
    void enabled_missingLevelTreatedAsZero();
    /**
     *
     * @brief Require -1,0,5 levels to reorder layer indices in ascending level order.
     *
     */
    void enabled_negativeLevelsSortFirst();
    /**
     *
     * @brief Require four equal-level features to preserve their original order.
     *
     */
    void enabled_isStable();
    /**
     *
     * @brief Require enabled/disabled complete step sequences to agree when all levels are zero.
     *
     */
    void enabled_identicalToLegacyWhenAllLevelsAreZero();

    // Sanity
    /**
     *
     * @brief Require both ordering modes to emit five steps for two-plus-three symbol layers.
     *
     */
    void totalStepCountMatchesSumOfLayers();
};

// ── Level prop helpers ─────────────────────────────────────────────

void TestSymbolLevels::level_defaultZeroWhenMissing()
{
    SymbolLayer l;
    QCOMPARE(symbolLayerLevel(l), 0);
}

void TestSymbolLevels::level_setterRoundTrips()
{
    SymbolLayer l;
    setSymbolLayerLevel(l, 7);
    QCOMPARE(symbolLayerLevel(l), 7);
}

void TestSymbolLevels::level_negativeValuesPreserved()
{
    SymbolLayer l;
    setSymbolLayerLevel(l, -3);
    QCOMPARE(symbolLayerLevel(l), -3);
}

// ── Disabled order ─────────────────────────────────────────────────

void TestSymbolLevels::disabled_emitsPerFeatureBottomUp()
{
    SymbolStyle s1, s2;
    s1.layers.append(SymbolLayer{}); // f0, sl0
    s1.layers.append(SymbolLayer{}); // f0, sl1
    s2.layers.append(SymbolLayer{}); // f1, sl0

    QVector<SymbolStyle> features{s1, s2};
    const auto steps = computeSymbolLevelOrder(features, false);
    QCOMPARE(steps.size(), 3);
    QCOMPARE(steps[0], PaintStep({0, 0}));
    QCOMPARE(steps[1], PaintStep({0, 1}));
    QCOMPARE(steps[2], PaintStep({1, 0}));
}

void TestSymbolLevels::disabled_singleFeatureSingleLayerIsOneStep()
{
    SymbolStyle s;
    s.layers.append(SymbolLayer{});
    const auto steps = computeSymbolLevelOrder({s}, false);
    QCOMPARE(steps.size(), 1);
    QCOMPARE(steps[0], PaintStep({0, 0}));
}

void TestSymbolLevels::disabled_emptyFeaturesEmptySteps()
{
    QCOMPARE(computeSymbolLevelOrder({}, false).size(), 0);
}

// ── Enabled order ──────────────────────────────────────────────────

void TestSymbolLevels::enabled_groupsByLevelAcrossFeatures()
{
    // Two features, each with an "outline" (level 0) and a "fill"
    // (level 1) symbol layer. Enabled order: f0.outline, f1.outline,
    // f0.fill, f1.fill — all outlines first, then all fills.
    SymbolStyle f0, f1;
    SymbolLayer outline0, fill0, outline1, fill1;
    setSymbolLayerLevel(outline0, 0);
    setSymbolLayerLevel(fill0, 1);
    setSymbolLayerLevel(outline1, 0);
    setSymbolLayerLevel(fill1, 1);
    f0.layers.append(outline0);
    f0.layers.append(fill0);
    f1.layers.append(outline1);
    f1.layers.append(fill1);

    const auto steps = computeSymbolLevelOrder({f0, f1}, true);
    QCOMPARE(steps.size(), 4);
    QCOMPARE(steps[0], PaintStep({0, 0})); // f0 outline
    QCOMPARE(steps[1], PaintStep({1, 0})); // f1 outline
    QCOMPARE(steps[2], PaintStep({0, 1})); // f0 fill
    QCOMPARE(steps[3], PaintStep({1, 1})); // f1 fill
}

void TestSymbolLevels::enabled_withinLevelKeepsFeatureOrder()
{
    // 3 features, all single-layer at level 0. Enabled order should be
    // f0, f1, f2 (input order).
    SymbolStyle a, b, c;
    a.layers.append(SymbolLayer{});
    b.layers.append(SymbolLayer{});
    c.layers.append(SymbolLayer{});
    const auto steps = computeSymbolLevelOrder({a, b, c}, true);
    QCOMPARE(steps.size(), 3);
    QCOMPARE(steps[0].featureIndex, 0);
    QCOMPARE(steps[1].featureIndex, 1);
    QCOMPARE(steps[2].featureIndex, 2);
}

void TestSymbolLevels::enabled_withinFeatureKeepsLayerOrder()
{
    // One feature, three layers all at level 0. Layer-input order
    // preserved.
    SymbolStyle s;
    s.layers.append(SymbolLayer{});
    s.layers.append(SymbolLayer{});
    s.layers.append(SymbolLayer{});
    const auto steps = computeSymbolLevelOrder({s}, true);
    QCOMPARE(steps.size(), 3);
    QCOMPARE(steps[0].symbolLayerIndex, 0);
    QCOMPARE(steps[1].symbolLayerIndex, 1);
    QCOMPARE(steps[2].symbolLayerIndex, 2);
}

void TestSymbolLevels::enabled_missingLevelTreatedAsZero()
{
    SymbolStyle a, b;
    SymbolLayer noLevel;
    SymbolLayer levelZero;
    setSymbolLayerLevel(levelZero, 0);
    a.layers.append(noLevel);
    b.layers.append(levelZero);
    const auto steps = computeSymbolLevelOrder({a, b}, true);
    QCOMPARE(steps.size(), 2);
    // Both effectively level 0 → input order.
    QCOMPARE(steps[0].featureIndex, 0);
    QCOMPARE(steps[1].featureIndex, 1);
}

void TestSymbolLevels::enabled_negativeLevelsSortFirst()
{
    SymbolStyle s;
    SymbolLayer lneg, l0, lpos;
    setSymbolLayerLevel(lneg, -1);
    setSymbolLayerLevel(l0, 0);
    setSymbolLayerLevel(lpos, 5);
    s.layers.append(lpos); // sl 0 → level 5
    s.layers.append(l0);   // sl 1 → level 0
    s.layers.append(lneg); // sl 2 → level -1
    const auto steps = computeSymbolLevelOrder({s}, true);
    QCOMPARE(steps.size(), 3);
    QCOMPARE(steps[0].symbolLayerIndex, 2); // level -1 first
    QCOMPARE(steps[1].symbolLayerIndex, 1); // level 0 next
    QCOMPARE(steps[2].symbolLayerIndex, 0); // level 5 last
}

void TestSymbolLevels::enabled_isStable()
{
    // 4 features at level 0, single layer each — order must be f0..f3.
    QVector<SymbolStyle> features;
    for (int i = 0; i < 4; ++i) {
        SymbolStyle s;
        SymbolLayer l;
        s.layers.append(l);
        features.append(s);
    }
    const auto steps = computeSymbolLevelOrder(features, true);
    for (int i = 0; i < 4; ++i)
        QCOMPARE(steps[i].featureIndex, i);
}

void TestSymbolLevels::enabled_identicalToLegacyWhenAllLevelsAreZero()
{
    // When every layer is at level 0, enabled and disabled orders
    // should match exactly.
    QVector<SymbolStyle> features;
    for (int i = 0; i < 3; ++i) {
        SymbolStyle s;
        s.layers.append(SymbolLayer{});
        s.layers.append(SymbolLayer{});
        features.append(s);
    }
    const auto a = computeSymbolLevelOrder(features, false);
    const auto b = computeSymbolLevelOrder(features, true);
    QCOMPARE(a, b);
}

void TestSymbolLevels::totalStepCountMatchesSumOfLayers()
{
    SymbolStyle a, b;
    a.layers.append(SymbolLayer{});
    a.layers.append(SymbolLayer{});
    b.layers.append(SymbolLayer{});
    b.layers.append(SymbolLayer{});
    b.layers.append(SymbolLayer{});

    QCOMPARE(computeSymbolLevelOrder({a, b}, false).size(), 5);
    QCOMPARE(computeSymbolLevelOrder({a, b}, true).size(), 5);
}

QTEST_MAIN(TestSymbolLevels)
#include "test_symbollevels.moc"
