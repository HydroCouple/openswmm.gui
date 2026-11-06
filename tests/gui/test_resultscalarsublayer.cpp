/**
 * @file      test_resultscalarsublayer.cpp
 * @brief     Tests for resultscalarsublayer.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest>
#include <QSignalSpy>
#include <limits>
#include <cmath>
#include <thread>
#include <atomic>
#include "render/sublayers/resultscalarsublayer.h"

using namespace OpenSWMM::Render;
using namespace openswmmvis::io;
namespace
{
/**
 *
 * @brief Build a native-unit groundwater chloride scalar fixture.
 * @return Owned three-cell values 0,2,10 mg/L, all Valid, semantic dataset/species/
 * domain metadata and synthetic time 3. Leaves derived range/sample metadata at
 * its defaults; no geometry/source handles, file IO or unit conversion.
 *
 */
Mesh2DScalarFrame frame()
{
    Mesh2DScalarFrame f;
    f.descriptor.dataset = "gw/concentration";
    f.descriptor.species = "Chloride";
    f.descriptor.label = "Groundwater chloride";
    f.descriptor.domain = Mesh2DResultVariable::Domain::Groundwater;
    f.descriptor.units = "mg/L";
    f.descriptor.unitsKnown = true;
    f.values = {0, 2, 10};
    f.status = {Mesh2DValueStatus::Valid, Mesh2DValueStatus::Valid, Mesh2DValueStatus::Valid};
    f.time = 3;
    return f;
}
/**
 *
 * @brief Assign the fixture sublayer's semantic variable key and descriptor.
 * @param s Borrowed mutable sublayer receiving the metadata; style/identity-related
 * invalidation follows the production setters.
 * @param f Borrowed scalar frame; metadata is copied, values/ownership not bound.
 * Does not publish a presented frame, compute colors or retain f.
 *
 */
void bind(ResultScalarSublayer &s, const Mesh2DScalarFrame &f)
{
    s.setVariableKey(f.descriptor.key());
    s.setDescriptor(f.descriptor);
}
/**
 *
 * @brief Read the canonical color of one fixture legend symbol.
 * @param item Borrowed legend row with a nonempty symbol layer list.
 * @return Color read from the first layer's color property through SymbolProps,
 * using that reader's fallback for malformed/missing property. No empty-list guard,
 * layer traversal, input mutation or opacity composition.
 *
 */
QColor legendColor(const LegendSymbolItem &item)
{
    return SymbolProps::readColor(item.symbol.layers.front().props, QStringLiteral("color"));
}
}
class TestResultScalarSublayer : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require custom scalar range to hide underflow, retain endpoints, saturate overflow
     * and preserve missing status in continuous/classified modes.
     *
     */
    void numericRangeAppliesToGroundwaterAndSpecies()
    {
        auto f = frame();
        ResultScalarSublayer s({});
        bind(s, f);
        f.minimum = 0;
        f.maximum = 10;
        f.values = {1, 2, 5, 8, 10, 0};
        f.status = {Mesh2DValueStatus::Valid, Mesh2DValueStatus::Valid, Mesh2DValueStatus::Valid,
                    Mesh2DValueStatus::Valid, Mesh2DValueStatus::Valid, Mesh2DValueStatus::Missing};
        for (bool classified : {false, true}) {
            auto scheme = s.fillStyle()->scheme();
            scheme.setUseCustomRange(true);
            scheme.setRangeMin(2);
            scheme.setRangeMax(8);
            scheme.setInvertRamp(true);
            scheme.setClassCount(3);
            scheme.setMode(classified ? ClassificationScheme::ClassMode::Classified
                                      : ClassificationScheme::ClassMode::Continuous);
            scheme.setColorOverride(2, QColor(11, 22, 33, 128));
            s.fillStyle()->setScheme(scheme);
            const auto colors = s.cellColors(f);
            QCOMPARE(colors[0].alpha(), 0);
            QVERIFY(colors[1].alpha() > 0);
            QCOMPARE(colors[3], colors[4]);
            if (classified)
                QCOMPARE(colors[4], QColor(11, 22, 33, 128));
            QCOMPARE(colors[5], s.fillStyle()->missingColor());
        }
    }
    /**
     *
     * @brief Round-trip contour presentation/line/label settings, check fixed-interval levels
     * under custom range and propagate nested style changes.
     *
     */
    void contourAndLabelStylesPersistIndependently()
    {
        ResultScalarSublayer s({});
        bind(s, frame());
        auto *style = s.fillStyle();
        style->setPresentation(ResultScalarStyle::Presentation::Contours);
        style->contourStyle()->setLabels(true);
        style->contourStyle()->setColor(Qt::magenta);
        style->contourStyle()->setDashPattern(Qt::DashLine);
        style->contourStyle()->setLevelMode(IsolineStyle::LevelMode::FixedInterval);
        style->contourStyle()->setLevelInterval(2);
        ResultScalarStyle restored;
        restored.fromJson(style->toJson());
        QCOMPARE(restored.presentation(), ResultScalarStyle::Presentation::Contours);
        QCOMPARE(restored.contourStyle()->color(), QColor(Qt::magenta));
        QCOMPARE(restored.contourStyle()->dashPattern(), Qt::DashLine);
        QVERIFY(restored.contourStyle()->labels());
        QCOMPARE(restored.contourLevels(0, 10, {}), std::vector<double>({2, 4, 6, 8}));
        auto scheme = restored.contourStyle()->scheme();
        scheme.setUseCustomRange(true);
        scheme.setRangeMin(3);
        scheme.setRangeMax(7);
        restored.contourStyle()->setScheme(scheme);
        QCOMPARE(restored.contourLevels(0, 10, {}), std::vector<double>({4, 6}));
        restored.setPresentation(ResultScalarStyle::Presentation::Labels);
        QCOMPARE(restored.toJson()["presentation"].toInt(), 2);
        QSignalSpy changes(style, &SublayerStyle::styleChanged);
        style->contourStyle()->setLabelDecimals(5);
        QVERIFY(changes.count() > 0);
    }
    /**
     *
     * @brief Require unique generated scalar IDs, preserved explicit ID, per-frame default range
     * and independent missing colors.
     *
     */
    void uniqueIdsAndIndependentStyles()
    {
        ResultScalarSublayer a({}), b({}), restored("results2d.variable.saved");
        QVERIFY(a.id().startsWith("results2d.variable."));
        QVERIFY(a.id() != b.id());
        QCOMPARE(a.fillStyle()->scheme().rangeMode(), RangeMode::PerFrameAutoStretch);
        QCOMPARE(restored.id(), QString("results2d.variable.saved"));
        a.fillStyle()->setMissingColor(Qt::magenta);
        QVERIFY(b.fillStyle()->missingColor() != a.fillStyle()->missingColor());
    }
    /**
     *
     * @brief Require species mismatch or frame error to refuse colors and expose unavailability
     * rather than reuse another species.
     *
     */
    void semanticIdentityDoesNotSelectAnotherSpecies()
    {
        auto f = frame();
        ResultScalarSublayer s({});
        bind(s, f);
        QCOMPARE(s.cellColors(f).size(), 3);
        f.descriptor.species = "Nitrate";
        QVERIFY(s.cellColors(f).isEmpty());
        QVERIFY(s.legendSymbolItems(f).front().label.contains("unavailable"));
        f = frame();
        f.error = "Frame unavailable";
        QVERIFY(s.cellColors(f).isEmpty());
    }
    /**
     *
     * @brief Require valid zero, missing, waterless and not-applicable colors to remain distinct,
     * with NaN/absent statuses handled as missing.
     *
     */
    void physicalZeroAndAvailabilityRemainDistinct()
    {
        auto f = frame();
        ResultScalarSublayer s({});
        bind(s, f);
        f.values = {0, 0, 0, 0, std::numeric_limits<float>::quiet_NaN(), 9};
        f.status = {Mesh2DValueStatus::Valid, Mesh2DValueStatus::Missing,
                    Mesh2DValueStatus::Waterless, Mesh2DValueStatus::NotApplicable,
                    Mesh2DValueStatus::Valid};
        const auto colors = s.cellColors(f);
        QCOMPARE(colors.size(), 6);
        QVERIFY(colors[0] != colors[1]);
        QCOMPARE(colors[1], s.fillStyle()->missingColor());
        QCOMPARE(colors[2], s.fillStyle()->waterlessColor());
        QCOMPARE(colors[3], s.fillStyle()->notApplicableColor());
        QCOMPARE(colors[4], colors[1]);
        QCOMPARE(colors[5], colors[1]);
        QCOMPARE(f.values.front(), 0.0f);
        QVERIFY(std::isnan(f.values[4]));
    }
    /**
     *
     * @brief Require scalar alpha scaling, rejection of NaN opacity and empty colors/legend for
     * hidden or zero-opacity sublayers.
     *
     */
    void opacityVisibilityAndFiniteValues()
    {
        auto f = frame();
        ResultScalarSublayer s({});
        bind(s, f);
        const auto original = s.cellColors(f);
        s.setOpacity(0.25);
        const auto faded = s.cellColors(f);
        QVERIFY(qAbs(faded[0].alphaF() - original[0].alphaF() * 0.25) < 0.005);
        s.setOpacity(std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(s.opacity(), 0.25);
        s.setVisible(false);
        QVERIFY(s.cellColors(f).isEmpty());
        QVERIFY(s.legendSymbolItems(f).isEmpty());
        s.setVisible(true);
        s.setOpacity(0);
        QVERIFY(s.cellColors(f).isEmpty());
    }
    /**
     *
     * @brief Supply manual, quantile and equal-interval classification variants.
     *
     */
    void classifiedLegendMatchesActualEdges_data()
    {
        QTest::addColumn<int>("method");
        QTest::newRow("manual") << int(BinMethod::Manual);
        QTest::newRow("quantile") << int(BinMethod::Quantile);
        QTest::newRow("equal") << int(BinMethod::EqualInterval);
    }
    /**
     *
     * @brief Require rendered scalar colors to match exact legend class edges, native units and
     * explicit color/user-label overrides.
     *
     */
    void classifiedLegendMatchesActualEdges()
    {
        QFETCH(int, method);
        auto f = frame();
        ResultScalarSublayer s({});
        bind(s, f);
        f.minimum = 0;
        f.maximum = 10;
        f.samples = {0, 1, 1.5, 2, 2.5, 3, 10};
        auto scheme = s.fillStyle()->scheme();
        scheme.setMode(ClassificationScheme::ClassMode::Classified);
        scheme.setMethod(BinMethod(method));
        scheme.setClassCount(3);
        scheme.setManualBreaks({1, 5});
        scheme.setColorOverride(1, QColor("#C02040"));
        scheme.setLabelOverride(1, "Selected concentration band");
        s.fillStyle()->setScheme(scheme);
        const auto edges = scheme.levelEdges(f.minimum, f.maximum, f.samples);
        const auto colors = s.cellColors(f);
        const auto legend = s.legendSymbolItems(f);
        QCOMPARE(legend.size(), edges.size() - 1 + 3);
        for (int i = 0; i < colors.size(); ++i) {
            const int index = ClassificationScheme::classIndexFor(f.values[size_t(i)], edges);
            QCOMPARE(colors[i], legendColor(legend[index]));
            QVERIFY(legend[index].label.contains("mg/L"));
        }
        QCOMPARE(legend[1].userLabel, QString("Selected concentration band"));
        QCOMPARE(legendColor(legend[1]), QColor("#C02040"));
    }
    /**
     *
     * @brief Require constant values 7 to produce one matching physical class plus status legend
     * rows.
     *
     */
    void constantRangeHasSingleMatchingPhysicalValue()
    {
        auto f = frame();
        f.values = {7, 7, 7};
        ResultScalarSublayer s({});
        bind(s, f);
        s.fillStyle()->setClassified(true);
        const auto colors = s.cellColors(f);
        const auto legend = s.legendSymbolItems(f);
        QCOMPARE(legend.size(), 4);
        QCOMPARE(legend.front().range.first, 7.0);
        QCOMPARE(legend.front().range.second, 7.0);
        QVERIFY(legend.front().label.contains("7"));
        QCOMPARE(colors.front(), legendColor(legend.front()));
    }
    /**
     *
     * @brief Require supplied wider range to change colors and explicit custom range to restore
     * original colors without converting concentrations.
     *
     */
    void suppliedWholeRunRangeAndCustomRangeAreHonored()
    {
        auto f = frame();
        ResultScalarSublayer s({});
        bind(s, f);
        s.fillStyle()->setClassified(false);
        const auto current = s.cellColors(f);
        f.minimum = 0;
        f.maximum = 100;
        f.samples = {0, 2, 10, 100};
        const auto wholeRun = s.cellColors(f);
        QVERIFY(wholeRun.back() != current.back());
        auto scheme = s.fillStyle()->scheme();
        scheme.setUseCustomRange(true);
        scheme.setRangeMin(0);
        scheme.setRangeMax(10);
        s.fillStyle()->setScheme(scheme);
        QCOMPARE(s.cellColors(f), current);
        QCOMPARE(f.values.back(), 10.0f); // concentration never converted to map units
    }
    /**
     *
     * @brief Require reversed custom range to produce missing colors and an explanatory
     * classification legend.
     *
     */
    void invalidClassificationIsExplicit()
    {
        auto f = frame();
        ResultScalarSublayer s({});
        bind(s, f);
        auto scheme = s.fillStyle()->scheme();
        scheme.setUseCustomRange(true);
        scheme.setRangeMin(10);
        scheme.setRangeMax(1);
        s.fillStyle()->setScheme(scheme);
        for (const auto &color : s.cellColors(f))
            QCOMPARE(color, s.fillStyle()->missingColor());
        QVERIFY(
            s.legendSymbolItems(f).front().label.contains("classification", Qt::CaseInsensitive));
    }
    /**
     *
     * @brief Require unknown-unit metadata to appear in both display name and legend.
     *
     */
    void unknownUnitsAreVisible()
    {
        auto f = frame();
        f.descriptor.unitsKnown = false;
        ResultScalarSublayer s({});
        bind(s, f);
        QVERIFY(s.displayName().contains("units unknown"));
        QVERIFY(s.legendSymbolItems(f).front().label.contains("units unknown"));
    }
    /**
     *
     * @brief Publish immutable frames from one worker while reading snapshots and require only
     * complete known pointers/times with no invalidation signals.
     * Joins the worker before checking success, then clears publication; does not mutate
     * QObject style/descriptor state concurrently or establish behavior for mutable frames.
     *
     */
    void presentedFrameIsAnImmutableAtomicSnapshot()
    {
        ResultScalarSublayer s({});
        auto first = std::make_shared<const Mesh2DScalarFrame>(frame());
        auto nextData = frame();
        nextData.time = 4;
        auto second = std::make_shared<const Mesh2DScalarFrame>(nextData);
        bind(s, *first);
        QSignalSpy changed(&s, &ISublayer::invalidated);
        s.publishPresentedFrame(first);
        QCOMPARE(s.presentedFrame(), first);
        std::atomic<bool> running{true};
        std::thread writer([&] {
            for (int i = 0; i < 1000; ++i)
                s.publishPresentedFrame(i % 2 ? first : second);
            running.store(false);
        });
        bool valid = true;
        while (running.load()) {
            const auto shown = s.presentedFrame();
            valid = valid && shown && (shown == first || shown == second) &&
                    (shown->time == 3 || shown->time == 4);
        }
        writer.join();
        QVERIFY(valid);
        QCOMPARE(changed.count(), 0); // publication never calls GUI slots/signals
        s.publishPresentedFrame({});
        QVERIFY(!s.presentedFrame());
    }
    /**
     *
     * @brief Require descriptor refresh, variable/style changes, hiding and zero opacity to clear
     * the presented-frame snapshot.
     *
     */
    void presentedFrameClearsOnIdentityStyleAndVisibilityChanges()
    {
        auto f = std::make_shared<const Mesh2DScalarFrame>(frame());
        ResultScalarSublayer s({});
        bind(s, *f);
        s.publishPresentedFrame(f);
        s.setDescriptor(f->descriptor);
        QVERIFY(!s.presentedFrame()); // source refresh may reuse semantic identity
        s.publishPresentedFrame(f);
        s.fillStyle()->setAttribute("surface:another");
        QVERIFY(!s.presentedFrame());
        bind(s, *f);
        s.publishPresentedFrame(f);
        s.setVisible(false);
        QVERIFY(!s.presentedFrame());
        s.setVisible(true);
        s.publishPresentedFrame(f);
        s.setOpacity(0);
        QVERIFY(!s.presentedFrame());
        s.setOpacity(1);
        s.publishPresentedFrame(f);
        s.fillStyle()->setMissingColor(Qt::magenta);
        QVERIFY(!s.presentedFrame());
    }
    /**
     *
     * @brief Require one style notification, semantic key/status-color preservation, exact JSON
     * and matching colors after style restore.
     *
     */
    void jsonPreservesSemanticIdentityAndStatusColorsAtomically()
    {
        auto f = frame();
        ResultScalarSublayer source({});
        bind(source, f);
        source.fillStyle()->setMissingColor(QColor(1, 2, 3, 40));
        source.fillStyle()->setWaterlessColor(QColor(10, 20, 30, 90));
        source.fillStyle()->setNotApplicableColor(QColor(40, 50, 60, 200));
        auto scheme = source.fillStyle()->scheme();
        scheme.setMethod(BinMethod::Quantile);
        scheme.setClassCount(4);
        scheme.setRangeMode(RangeMode::FixedOverRun);
        source.fillStyle()->setScheme(scheme);
        ResultScalarSublayer restored({});
        QSignalSpy changed(restored.fillStyle(), &SublayerStyle::styleChanged);
        restored.fillStyle()->fromJson(source.fillStyle()->toJson());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(restored.variableKey(), f.descriptor.key());
        QCOMPARE(restored.fillStyle()->toJson(), source.fillStyle()->toJson());
        QCOMPARE(restored.cellColors(f), source.cellColors(f));
    }
};
QTEST_MAIN(TestResultScalarSublayer)
#include "test_resultscalarsublayer.moc"
