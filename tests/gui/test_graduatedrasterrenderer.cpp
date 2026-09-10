/*!
 * \file   test_graduatedrasterrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Raster symbology model tests — GraduatedRasterRenderer (the
 *         ClassificationScheme-driven continuous / classified single-band
 *         renderer), MultiBandColorRenderer, and makeRasterRenderer().
 *
 *         Contracts pinned here:
 *           - Continuous mode colours a value exactly as the scheme's ramp
 *             would at the LUT texel position (ScalarRampLut contract),
 *             inversion included; out-of-range clamps to the end colours
 *             unless clipOutOfRange, which makes them transparent.
 *           - Classified mode colours by class edge interval; data-driven
 *             methods use the sample handed to reclassify(); Manual breaks
 *             and per-class colour overrides are honoured.
 *           - NoData / non-finite → transparent in both modes.
 *           - Legend: 6 sampled rows (Continuous) or one row per class with
 *             range / classKey / label override (Classified).
 *           - JSON round-trip reproduces colours without the sample (edges
 *             persist); missing edges are re-derived.
 *           - clone() is independent; the factory dispatches on "id".
 *
 *         Self-contained: render sources only (no widgets, no GDAL, no engine).
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest/QtTest>

#include <cmath>
#include <limits>
#include <memory>

#include "render/classificationscheme.h"
#include "render/irasterrenderer.h"
#include "render/legendsymbolitem.h"
#include "render/rasterrendererfactory.h"
#include "render/renderers/graduatedrasterrenderer.h"
#include "render/renderers/multibandcolorrenderer.h"
#include "render/renderers/palettedrasterrenderer.h"
#include "render/renderers/singlebandpseudocolorrenderer.h"
#include "render/scalarramplut.h"
#include "render/symbollayer.h"

using namespace OpenSWMM::Render;

namespace
{

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

QColor swatchColor(const LegendSymbolItem &item)
{
    return SymbolProps::readColor(item.symbol.layers.first().props,
                                  QStringLiteral("color"));
}

/*! The renderer quantises to the 8-bit QRgb the tile pixel actually holds;
 *  scheme colours built via fromRgbF carry float components that do not
 *  round-trip through QColor::operator==, so compare on the packed value. */
QRgb px(const QColor &c) { return c.rgba(); }

/*! Classified, 4 equal-interval classes of viridis over [0, 100]. */
GraduatedRasterRenderer makeClassified4()
{
    GraduatedRasterRenderer r;
    ClassificationScheme s = r.scheme();
    s.setMode(ClassificationScheme::ClassMode::Classified);
    s.setMethod(BinMethod::EqualInterval);
    s.setClassCount(4);
    s.setRampName(QStringLiteral("viridis"));
    r.setDataRange(0.0, 100.0);
    r.setScheme(s);
    return r;
}

} // namespace

class TestGraduatedRasterRenderer : public QObject
{
    Q_OBJECT

private slots:
    void rendererId_isStable();
    void defaults_continuousGrayscale();
    void continuous_lutMatchesSchemeAtTexels();
    void continuous_invertFlipsEnds();
    void continuous_outOfRangeClampsUnlessClipped();
    void continuous_customRangeDrivesStretch();
    void classified_equalIntervalEdgesAndColours();
    void classified_quantileUsesSamples();
    void classified_manualBreaks();
    void classified_colorOverrideWins();
    void noData_andNonFinite_transparent();
    void legend_continuousSampledRows();
    void legend_classifiedOnePerClass();
    void json_roundTripReproducesColours();
    void json_missingEdgesAreRederived();
    void clone_isIndependent();
    void multiband_bandsAndJson();
    void factory_dispatchesOnId();
};

void TestGraduatedRasterRenderer::rendererId_isStable()
{
    GraduatedRasterRenderer r;
    QCOMPARE(r.rendererId(), QStringLiteral("graduatedraster"));
    MultiBandColorRenderer m;
    QCOMPARE(m.rendererId(), QStringLiteral("multibandcolor"));
}

void TestGraduatedRasterRenderer::defaults_continuousGrayscale()
{
    GraduatedRasterRenderer r;
    QCOMPARE(r.scheme().mode(), ClassificationScheme::ClassMode::Continuous);
    QCOMPARE(r.scheme().rampName(), QStringLiteral("grayscale"));
    QCOMPARE(r.dataMin(), 0.0);
    QCOMPARE(r.dataMax(), 1.0);
    QVERIFY(!r.clipOutOfRange());
    QCOMPARE(r.colorForValue(0.0), QColor(Qt::black));
    QCOMPARE(r.colorForValue(1.0), QColor(Qt::white));
}

void TestGraduatedRasterRenderer::continuous_lutMatchesSchemeAtTexels()
{
    GraduatedRasterRenderer r;
    ClassificationScheme s = r.scheme();
    s.setRampName(QStringLiteral("viridis"));
    r.setScheme(s);
    r.setDataRange(0.0, 100.0);

    // Values that land exactly on LUT texels must reproduce the scheme's
    // colour at that texel's position.
    for (int i : { 0, 1, 64, 128, 200, 255 })
    {
        const double f = ScalarRampLut::positionForTexel(i);
        const double v = f * 100.0;
        QCOMPARE(px(r.colorForValue(v)), px(r.scheme().colorAtF(f)));
    }
    // A value between texels rounds to the nearest texel (never more than
    // one texel away from the exact ramp colour).
    const QColor mid = r.colorForValue(50.0);
    const QColor ref = r.scheme().colorAtF(0.5);
    QVERIFY(std::abs(mid.red()   - ref.red())   <= 2);
    QVERIFY(std::abs(mid.green() - ref.green()) <= 2);
    QVERIFY(std::abs(mid.blue()  - ref.blue())  <= 2);
}

void TestGraduatedRasterRenderer::continuous_invertFlipsEnds()
{
    GraduatedRasterRenderer r;
    ClassificationScheme s = r.scheme();
    s.setInvertRamp(true);
    r.setScheme(s);
    QCOMPARE(r.colorForValue(0.0), QColor(Qt::white));
    QCOMPARE(r.colorForValue(1.0), QColor(Qt::black));
}

void TestGraduatedRasterRenderer::continuous_outOfRangeClampsUnlessClipped()
{
    GraduatedRasterRenderer r;
    r.setDataRange(10.0, 20.0);
    // Default policy: clamp to the end colours (QGIS default).
    QCOMPARE(r.colorForValue(-5.0),  QColor(Qt::black));
    QCOMPARE(r.colorForValue(500.0), QColor(Qt::white));
    // Clip: out-of-range → transparent, in-range unchanged.
    r.setClipOutOfRange(true);
    QCOMPARE(r.colorForValue(-5.0),  QColor(Qt::transparent));
    QCOMPARE(r.colorForValue(500.0), QColor(Qt::transparent));
    QCOMPARE(r.colorForValue(10.0),  QColor(Qt::black));
    QCOMPARE(r.colorForValue(20.0),  QColor(Qt::white));
}

void TestGraduatedRasterRenderer::continuous_customRangeDrivesStretch()
{
    GraduatedRasterRenderer r;
    r.setDataRange(0.0, 1000.0);
    ClassificationScheme s = r.scheme();
    s.setUseCustomRange(true);
    s.setRangeMin(0.0);
    s.setRangeMax(10.0);
    r.setScheme(s);
    QCOMPARE(r.effectiveRange(), qMakePair(0.0, 10.0));
    // 10 is the top of the custom range → white, even though dataMax is 1000.
    QCOMPARE(r.colorForValue(10.0), QColor(Qt::white));
    QCOMPARE(r.colorForValue(500.0), QColor(Qt::white));
}

void TestGraduatedRasterRenderer::classified_equalIntervalEdgesAndColours()
{
    const GraduatedRasterRenderer r = makeClassified4();
    QCOMPARE(r.classCount(), 4);
    QCOMPARE(r.edges(), (QVector<double>{ 0.0, 25.0, 50.0, 75.0, 100.0 }));

    const auto colorOfClass = [&](int i) { return r.scheme().colorForClass(i, 4); };
    QCOMPARE(px(r.colorForValue(10.0)),  px(colorOfClass(0)));
    QCOMPARE(px(r.colorForValue(24.9)),  px(colorOfClass(0)));
    QCOMPARE(px(r.colorForValue(25.0)),  px(colorOfClass(1)));   // edge belongs to the upper class
    QCOMPARE(px(r.colorForValue(60.0)),  px(colorOfClass(2)));
    QCOMPARE(px(r.colorForValue(90.0)),  px(colorOfClass(3)));
    QCOMPARE(px(r.colorForValue(100.0)), px(colorOfClass(3)));
    // Out of range clamps to the end classes unless clipped.
    QCOMPARE(px(r.colorForValue(-1.0)),  px(colorOfClass(0)));
    QCOMPARE(px(r.colorForValue(101.0)), px(colorOfClass(3)));
    // Adjacent classes are distinct colours.
    QVERIFY(px(colorOfClass(0)) != px(colorOfClass(1)));
}

void TestGraduatedRasterRenderer::classified_quantileUsesSamples()
{
    GraduatedRasterRenderer r;
    ClassificationScheme s = r.scheme();
    s.setMode(ClassificationScheme::ClassMode::Classified);
    s.setMethod(BinMethod::Quantile);
    s.setClassCount(2);
    r.setDataRange(0.0, 100.0);
    r.setScheme(s);
    // Without samples the data-driven method degrades to equal spacing.
    QCOMPARE(r.edges(), (QVector<double>{ 0.0, 50.0, 100.0 }));

    // A skewed sample moves the median break well below 50.
    const QVector<double> samples{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 90 };
    r.reclassify(samples);
    QCOMPARE(r.edges().size(), 3);
    QVERIFY(r.edges().at(1) > 0.0);
    QVERIFY(r.edges().at(1) < 20.0);
    // A value above the break but far below 50 now lands in the upper class.
    QCOMPARE(px(r.colorForValue(30.0)), px(r.scheme().colorForClass(1, 2)));
    QCOMPARE(px(r.colorForValue(1.0)),  px(r.scheme().colorForClass(0, 2)));
}

void TestGraduatedRasterRenderer::classified_manualBreaks()
{
    GraduatedRasterRenderer r;
    ClassificationScheme s = r.scheme();
    s.setMode(ClassificationScheme::ClassMode::Classified);
    s.setMethod(BinMethod::Manual);
    s.setManualBreaks({ 30.0, 70.0 });
    r.setDataRange(0.0, 100.0);
    r.setScheme(s);
    QCOMPARE(r.edges(), (QVector<double>{ 0.0, 30.0, 70.0, 100.0 }));
    QCOMPARE(r.classCount(), 3);
    QCOMPARE(px(r.colorForValue(29.0)), px(r.scheme().colorForClass(0, 3)));
    QCOMPARE(px(r.colorForValue(50.0)), px(r.scheme().colorForClass(1, 3)));
    QCOMPARE(px(r.colorForValue(99.0)), px(r.scheme().colorForClass(2, 3)));
}

void TestGraduatedRasterRenderer::classified_colorOverrideWins()
{
    GraduatedRasterRenderer r = makeClassified4();
    ClassificationScheme s = r.scheme();
    s.setColorOverride(1, QColor(Qt::red));
    r.setScheme(s);
    QCOMPARE(r.colorForValue(30.0), QColor(Qt::red));
    QCOMPARE(px(r.colorForValue(10.0)), px(r.scheme().colorForClass(0, 4)));
}

void TestGraduatedRasterRenderer::noData_andNonFinite_transparent()
{
    GraduatedRasterRenderer cont;
    QCOMPARE(cont.colorForValue(0.5, /*isNoData=*/true), QColor(Qt::transparent));
    QCOMPARE(cont.colorForValue(kNaN), QColor(Qt::transparent));
    QCOMPARE(cont.colorForValue(std::numeric_limits<double>::infinity()),
             QColor(Qt::transparent));

    const GraduatedRasterRenderer cls = makeClassified4();
    QCOMPARE(cls.colorForValue(50.0, /*isNoData=*/true), QColor(Qt::transparent));
    QCOMPARE(cls.colorForValue(kNaN), QColor(Qt::transparent));
}

void TestGraduatedRasterRenderer::legend_continuousSampledRows()
{
    GraduatedRasterRenderer r;
    r.setDataRange(0.0, 100.0);
    const auto items = r.legendSymbolItems();
    QCOMPARE(items.size(), GraduatedRasterRenderer::kContinuousLegendRows);
    QCOMPARE(items.first().label, r.scheme().formatValue(0.0));
    QCOMPARE(items.last().label,  r.scheme().formatValue(100.0));
    QCOMPARE(swatchColor(items.first()), QColor(Qt::black));
    QCOMPARE(swatchColor(items.last()),  QColor(Qt::white));
    QVERIFY(items.first().classKey.isEmpty());   // no per-class editing
    for (int i = 0; i < items.size(); ++i)
        QCOMPARE(items.at(i).sortIndex, i);
}

void TestGraduatedRasterRenderer::legend_classifiedOnePerClass()
{
    GraduatedRasterRenderer r = makeClassified4();
    ClassificationScheme s = r.scheme();
    s.setLabelOverride(2, QStringLiteral("Mid-high"));
    r.setScheme(s);

    const auto items = r.legendSymbolItems();
    QCOMPARE(items.size(), 4);
    for (int i = 0; i < 4; ++i)
    {
        QCOMPARE(items.at(i).classKey, QString::number(i));
        QCOMPARE(items.at(i).range.first,  r.edges().at(i));
        QCOMPARE(items.at(i).range.second, r.edges().at(i + 1));
        QCOMPARE(px(swatchColor(items.at(i))), px(r.scheme().colorForClass(i, 4)));
    }
    QCOMPARE(items.at(0).label, QStringLiteral("0 – 25"));
    QVERIFY(items.at(0).userLabel.isEmpty());
    QCOMPARE(items.at(2).userLabel, QStringLiteral("Mid-high"));
    QCOMPARE(items.at(2).effectiveLabel(), QStringLiteral("Mid-high"));
}

void TestGraduatedRasterRenderer::json_roundTripReproducesColours()
{
    // Quantile classification derived from a sample the loader never sees:
    // the persisted edges must carry the classification.
    GraduatedRasterRenderer in;
    ClassificationScheme s = in.scheme();
    s.setMode(ClassificationScheme::ClassMode::Classified);
    s.setMethod(BinMethod::Quantile);
    s.setClassCount(3);
    s.setRampName(QStringLiteral("plasma"));
    s.setInvertRamp(true);
    s.setColorOverride(0, QColor(Qt::cyan));
    s.setLabelOverride(1, QStringLiteral("middle"));
    in.setDataRange(-5.0, 250.0);
    in.setScheme(s);
    in.reclassify({ 0, 1, 2, 3, 4, 5, 100, 200, 240 });
    in.setClipOutOfRange(true);

    const QJsonObject j = in.toJson();
    QCOMPARE(j.value(QStringLiteral("id")).toString(), QStringLiteral("graduatedraster"));

    GraduatedRasterRenderer out;
    out.fromJson(j);
    QCOMPARE(out.toJson(), j);                     // deterministic + lossless
    QCOMPARE(out.edges(), in.edges());
    QCOMPARE(out.dataMin(), -5.0);
    QCOMPARE(out.dataMax(), 250.0);
    QVERIFY(out.clipOutOfRange());
    for (double v : { -10.0, 0.5, 2.5, 50.0, 150.0, 249.0, 300.0 })
        QCOMPARE(out.colorForValue(v), in.colorForValue(v));
    QCOMPARE(out.legendSymbolItems().size(), 3);
    QCOMPARE(out.legendSymbolItems().at(1).userLabel, QStringLiteral("middle"));
}

void TestGraduatedRasterRenderer::json_missingEdgesAreRederived()
{
    const GraduatedRasterRenderer in = makeClassified4();
    QJsonObject j = in.toJson();
    j.remove(QStringLiteral("edges"));

    GraduatedRasterRenderer out;
    out.fromJson(j);
    QCOMPARE(out.edges(), in.edges());   // equal-interval is range-derived

    // Malformed edges are rejected the same way.
    j.insert(QStringLiteral("edges"), QJsonArray{ 5.0, 1.0 });
    GraduatedRasterRenderer out2;
    out2.fromJson(j);
    QCOMPARE(out2.edges(), in.edges());
}

void TestGraduatedRasterRenderer::clone_isIndependent()
{
    GraduatedRasterRenderer original = makeClassified4();
    std::unique_ptr<IRasterRenderer> copy = original.clone();
    QVERIFY(copy != nullptr);
    auto *typed = dynamic_cast<GraduatedRasterRenderer *>(copy.get());
    QVERIFY(typed != nullptr);

    original.setDataRange(0.0, 1.0);   // re-derives the original's edges
    QCOMPARE(typed->dataMax(), 100.0);
    QCOMPARE(typed->edges(), (QVector<double>{ 0.0, 25.0, 50.0, 75.0, 100.0 }));
    QCOMPARE(px(typed->colorForValue(60.0)), px(typed->scheme().colorForClass(2, 4)));
}

void TestGraduatedRasterRenderer::multiband_bandsAndJson()
{
    MultiBandColorRenderer m;
    QCOMPARE(m.redBand(), 1);
    QCOMPARE(m.greenBand(), 2);
    QCOMPARE(m.blueBand(), 3);
    QCOMPARE(m.alphaBand(), 0);

    m.setBands(3, 2, 1, 4);
    MultiBandColorRenderer out;
    out.fromJson(m.toJson());
    QCOMPARE(out.redBand(), 3);
    QCOMPARE(out.greenBand(), 2);
    QCOMPARE(out.blueBand(), 1);
    QCOMPARE(out.alphaBand(), 4);
    QCOMPARE(out.toJson(), m.toJson());
    QCOMPARE(out.colorForValue(42.0), QColor(Qt::transparent));
    QCOMPARE(out.legendSymbolItems().size(), 1);
}

void TestGraduatedRasterRenderer::factory_dispatchesOnId()
{
    {
        const GraduatedRasterRenderer src = makeClassified4();
        auto r = makeRasterRenderer(src.toJson());
        QVERIFY(dynamic_cast<GraduatedRasterRenderer *>(r.get()) != nullptr);
        QCOMPARE(r->toJson(), src.toJson());
    }
    {
        PalettedRasterRenderer src;
        src.buildClassesFromValues({ 1, 2, 3 });
        auto r = makeRasterRenderer(src.toJson());
        QVERIFY(dynamic_cast<PalettedRasterRenderer *>(r.get()) != nullptr);
        QCOMPARE(r->toJson(), src.toJson());
    }
    {
        MultiBandColorRenderer src(2, 3, 1);
        auto r = makeRasterRenderer(src.toJson());
        QVERIFY(dynamic_cast<MultiBandColorRenderer *>(r.get()) != nullptr);
        QCOMPARE(r->toJson(), src.toJson());
    }
    {
        SingleBandPseudoColorRenderer src;
        src.setRange(0.0, 9.0);
        src.setStops({ { 0.0, QColor(Qt::red) }, { 1.0, QColor(Qt::blue) } });
        auto r = makeRasterRenderer(src.toJson());
        QVERIFY(dynamic_cast<SingleBandPseudoColorRenderer *>(r.get()) != nullptr);
        QCOMPARE(r->toJson(), src.toJson());
    }
    QVERIFY(makeRasterRenderer(QJsonObject{}) == nullptr);
    QJsonObject bogus;
    bogus.insert(QStringLiteral("id"), QStringLiteral("no-such-renderer"));
    QVERIFY(makeRasterRenderer(bogus) == nullptr);
}

QTEST_MAIN(TestGraduatedRasterRenderer)
#include "test_graduatedrasterrenderer.moc"
