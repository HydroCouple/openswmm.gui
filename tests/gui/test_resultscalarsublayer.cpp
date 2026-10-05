#include <QtTest>
#include <QSignalSpy>
#include <limits>
#include <cmath>
#include <thread>
#include <atomic>
#include "render/sublayers/resultscalarsublayer.h"

using namespace OpenSWMM::Render;
using namespace openswmmvis::io;
namespace {
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
void bind(ResultScalarSublayer &s, const Mesh2DScalarFrame &f)
{
    s.setVariableKey(f.descriptor.key());
    s.setDescriptor(f.descriptor);
}
QColor legendColor(const LegendSymbolItem &item)
{
    return SymbolProps::readColor(item.symbol.layers.front().props, QStringLiteral("color"));
}
}
class TestResultScalarSublayer : public QObject
{
    Q_OBJECT
private slots:
    void numericRangeAppliesToGroundwaterAndSpecies()
    {
        auto f = frame(); ResultScalarSublayer s({}); bind(s,f);
        f.minimum=0; f.maximum=10;
        f.values={1,2,5,8,10,0};
        f.status={Mesh2DValueStatus::Valid,Mesh2DValueStatus::Valid,Mesh2DValueStatus::Valid,
                  Mesh2DValueStatus::Valid,Mesh2DValueStatus::Valid,Mesh2DValueStatus::Missing};
        for (bool classified : {false,true}) {
            auto scheme=s.fillStyle()->scheme();
            scheme.setUseCustomRange(true); scheme.setRangeMin(2); scheme.setRangeMax(8);
            scheme.setInvertRamp(true); scheme.setClassCount(3);
            scheme.setMode(classified?ClassificationScheme::ClassMode::Classified:ClassificationScheme::ClassMode::Continuous);
            scheme.setColorOverride(2,QColor(11,22,33,128)); s.fillStyle()->setScheme(scheme);
            const auto colors=s.cellColors(f);
            QCOMPARE(colors[0].alpha(),0); QVERIFY(colors[1].alpha()>0);
            QCOMPARE(colors[3],colors[4]);
            if (classified) QCOMPARE(colors[4],QColor(11,22,33,128));
            QCOMPARE(colors[5],s.fillStyle()->missingColor());
        }
    }
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
    void semanticIdentityDoesNotSelectAnotherSpecies()
    {
        auto f = frame(); ResultScalarSublayer s({}); bind(s, f);
        QCOMPARE(s.cellColors(f).size(), 3);
        f.descriptor.species = "Nitrate";
        QVERIFY(s.cellColors(f).isEmpty());
        QVERIFY(s.legendSymbolItems(f).front().label.contains("unavailable"));
        f = frame(); f.error = "Frame unavailable";
        QVERIFY(s.cellColors(f).isEmpty());
    }
    void physicalZeroAndAvailabilityRemainDistinct()
    {
        auto f = frame(); ResultScalarSublayer s({}); bind(s, f);
        f.values = {0, 0, 0, 0, std::numeric_limits<float>::quiet_NaN(), 9};
        f.status = {Mesh2DValueStatus::Valid, Mesh2DValueStatus::Missing,
            Mesh2DValueStatus::Waterless, Mesh2DValueStatus::NotApplicable, Mesh2DValueStatus::Valid};
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
    void opacityVisibilityAndFiniteValues()
    {
        auto f = frame(); ResultScalarSublayer s({}); bind(s, f);
        const auto original = s.cellColors(f);
        s.setOpacity(0.25);
        const auto faded = s.cellColors(f);
        QVERIFY(qAbs(faded[0].alphaF() - original[0].alphaF() * 0.25) < 0.005);
        s.setOpacity(std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(s.opacity(), 0.25);
        s.setVisible(false); QVERIFY(s.cellColors(f).isEmpty());
        QVERIFY(s.legendSymbolItems(f).isEmpty());
        s.setVisible(true); s.setOpacity(0); QVERIFY(s.cellColors(f).isEmpty());
    }
    void classifiedLegendMatchesActualEdges_data()
    {
        QTest::addColumn<int>("method");
        QTest::newRow("manual") << int(BinMethod::Manual);
        QTest::newRow("quantile") << int(BinMethod::Quantile);
        QTest::newRow("equal") << int(BinMethod::EqualInterval);
    }
    void classifiedLegendMatchesActualEdges()
    {
        QFETCH(int, method);
        auto f = frame(); ResultScalarSublayer s({}); bind(s, f);
        f.minimum = 0; f.maximum = 10; f.samples = {0, 1, 1.5, 2, 2.5, 3, 10};
        auto scheme = s.fillStyle()->scheme();
        scheme.setMode(ClassificationScheme::ClassMode::Classified);
        scheme.setMethod(BinMethod(method)); scheme.setClassCount(3);
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
    void constantRangeHasSingleMatchingPhysicalValue()
    {
        auto f = frame(); f.values = {7, 7, 7};
        ResultScalarSublayer s({}); bind(s, f); s.fillStyle()->setClassified(true);
        const auto colors = s.cellColors(f); const auto legend = s.legendSymbolItems(f);
        QCOMPARE(legend.size(), 4);
        QCOMPARE(legend.front().range.first, 7.0);
        QCOMPARE(legend.front().range.second, 7.0);
        QVERIFY(legend.front().label.contains("7"));
        QCOMPARE(colors.front(), legendColor(legend.front()));
    }
    void suppliedWholeRunRangeAndCustomRangeAreHonored()
    {
        auto f = frame(); ResultScalarSublayer s({}); bind(s, f);
        s.fillStyle()->setClassified(false);
        const auto current = s.cellColors(f);
        f.minimum = 0; f.maximum = 100; f.samples = {0, 2, 10, 100};
        const auto wholeRun = s.cellColors(f);
        QVERIFY(wholeRun.back() != current.back());
        auto scheme = s.fillStyle()->scheme(); scheme.setUseCustomRange(true);
        scheme.setRangeMin(0); scheme.setRangeMax(10); s.fillStyle()->setScheme(scheme);
        QCOMPARE(s.cellColors(f), current);
        QCOMPARE(f.values.back(), 10.0f); // concentration never converted to map units
    }
    void invalidClassificationIsExplicit()
    {
        auto f = frame(); ResultScalarSublayer s({}); bind(s, f);
        auto scheme = s.fillStyle()->scheme(); scheme.setUseCustomRange(true);
        scheme.setRangeMin(10); scheme.setRangeMax(1); s.fillStyle()->setScheme(scheme);
        for (const auto &color : s.cellColors(f)) QCOMPARE(color, s.fillStyle()->missingColor());
        QVERIFY(s.legendSymbolItems(f).front().label.contains("classification", Qt::CaseInsensitive));
    }
    void unknownUnitsAreVisible()
    {
        auto f = frame(); f.descriptor.unitsKnown = false;
        ResultScalarSublayer s({}); bind(s, f);
        QVERIFY(s.displayName().contains("units unknown"));
        QVERIFY(s.legendSymbolItems(f).front().label.contains("units unknown"));
    }
    void presentedFrameIsAnImmutableAtomicSnapshot()
    {
        ResultScalarSublayer s({});
        auto first = std::make_shared<const Mesh2DScalarFrame>(frame());
        auto nextData = frame(); nextData.time = 4;
        auto second = std::make_shared<const Mesh2DScalarFrame>(nextData);
        bind(s, *first);
        QSignalSpy changed(&s, &ISublayer::invalidated);
        s.publishPresentedFrame(first);
        QCOMPARE(s.presentedFrame(), first);
        std::atomic<bool> running{true};
        std::thread writer([&] {
            for (int i = 0; i < 1000; ++i) s.publishPresentedFrame(i % 2 ? first : second);
            running.store(false);
        });
        bool valid = true;
        while (running.load()) {
            const auto shown = s.presentedFrame();
            valid = valid && shown && (shown == first || shown == second)
                && (shown->time == 3 || shown->time == 4);
        }
        writer.join();
        QVERIFY(valid);
        QCOMPARE(changed.count(), 0); // publication never calls GUI slots/signals
        s.publishPresentedFrame({});
        QVERIFY(!s.presentedFrame());
    }
    void presentedFrameClearsOnIdentityStyleAndVisibilityChanges()
    {
        auto f = std::make_shared<const Mesh2DScalarFrame>(frame());
        ResultScalarSublayer s({}); bind(s, *f);
        s.publishPresentedFrame(f); s.setDescriptor(f->descriptor);
        QVERIFY(!s.presentedFrame()); // source refresh may reuse semantic identity
        s.publishPresentedFrame(f); s.fillStyle()->setAttribute("surface:another");
        QVERIFY(!s.presentedFrame());
        bind(s, *f); s.publishPresentedFrame(f); s.setVisible(false);
        QVERIFY(!s.presentedFrame());
        s.setVisible(true); s.publishPresentedFrame(f); s.setOpacity(0);
        QVERIFY(!s.presentedFrame());
        s.setOpacity(1); s.publishPresentedFrame(f); s.fillStyle()->setMissingColor(Qt::magenta);
        QVERIFY(!s.presentedFrame());
    }
    void jsonPreservesSemanticIdentityAndStatusColorsAtomically()
    {
        auto f = frame(); ResultScalarSublayer source({}); bind(source, f);
        source.fillStyle()->setMissingColor(QColor(1, 2, 3, 40));
        source.fillStyle()->setWaterlessColor(QColor(10, 20, 30, 90));
        source.fillStyle()->setNotApplicableColor(QColor(40, 50, 60, 200));
        auto scheme = source.fillStyle()->scheme(); scheme.setMethod(BinMethod::Quantile);
        scheme.setClassCount(4); scheme.setRangeMode(RangeMode::FixedOverRun);
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
