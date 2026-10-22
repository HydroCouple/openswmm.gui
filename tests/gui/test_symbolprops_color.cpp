/**
 * @file      test_symbolprops_color.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Gap A1.2 — canonical colour prop convention.
 *
 *         Verifies that SymbolProps reads tolerate BOTH encodings (QColor
 *         variant and legacy hex string), that writes emit the canonical
 *         QColor variant, that every typed spec reader survives a hex-
 *         encoded prop bag (the historic X1 failure mode), and that the
 *         SymbolLayer JSON boundary rehydrates ALL colour-suffixed keys —
 *         including the raster grammar keys (noDataColor / lineColor /
 *         wideColor) the old fixed key list missed.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QJsonObject>
#include <QtTest/QtTest>

#include "render/fillsymbollayer.h"
#include "render/linesymbollayer.h"
#include "render/markersymbollayer.h"
#include "render/rastersymbollayers.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"

using namespace OpenSWMM::Render;

namespace
{
const QColor kRed(255, 0, 0);
const QColor kSemiBlue(0, 0, 255, 128);

/**
 *
 * @brief Encode a fixture color using Qt ARGB hex notation.
 * @param c Borrowed QColor, normally valid; invalid-color behavior is delegated to Qt.
 * @return Owned ARGB hexadecimal string with alpha; does not mutate the input.
 *
 */
QString hexOf(const QColor &c)
{
    return c.name(QColor::HexArgb);
}
} // namespace

class TestSymbolPropsColor : public QObject
{
    Q_OBJECT

private slots:
    // ── SymbolProps helpers ────────────────────────────────────────────
    /**
     *
     * @brief Require a QColor-valued property to preserve semi-transparent blue exactly.
     *
     */
    void readColor_acceptsVariant();
    /**
     *
     * @brief Require legacy ARGB text to decode the same semi-transparent QColor.
     *
     */
    void readColor_acceptsHexString();
    /**
     *
     * @brief Require the named red string to decode to Qt red.
     *
     */
    void readColor_acceptsNamedString();
    /**
     *
     * @brief Require missing and invalid-color properties to return the explicit red fallback.
     *
     */
    void readColor_fallbackOnMissingOrJunk();
    /**
     *
     * @brief Require property writes to use the QColor metatype and retain the exact alpha/color.
     *
     */
    void writeColor_storesVariant();
    /**
     *
     * @brief Require fillColor, color, outlineColor precedence and explicit fallback for an empty
     * style.
     *
     */
    void firstColor_prefersFillThenColorThenOutline();
    /**
     *
     * @brief Require firstColor to decode a legacy ARGB text property.
     *
     */
    void firstColor_toleratesHex();
    /**
     *
     * @brief Require override to update existing fill/color slots without inserting unrelated
     * keys or coloring a bare layer.
     *
     */
    void overrideColorInPlace_writesOnlyAdvertisedSlots();

    // ── Typed spec readers tolerate both encodings ─────────────────────
    /**
     *
     * @brief Check legacy marker fill/outline ARGB and default typed-spec round-trip colors.
     *
     */
    void markerSpec_readsHexAndVariant();
    /**
     *
     * @brief Check legacy line/arrow colors and the default line color typed round trip.
     *
     */
    void lineSpec_readsHexAndVariant();
    /**
     *
     * @brief Check legacy fill/outline colors and the default fill color typed round trip.
     *
     */
    void fillSpec_readsHexAndVariant();
    /**
     *
     * @brief Check legacy NoData ARGB and default NoData typed round-trip color.
     *
     */
    void rasterRampSpec_readsHexAndVariant();
    /**
     *
     * @brief Check legacy contour line ARGB and default typed round-trip line color.
     *
     */
    void contourSpec_readsHexAndVariant();
    /**
     *
     * @brief Require legacy mesh-edge color/wideColor ARGB to retain exact QColors.
     * This case does not separately exercise a QColor-variant input despite its name.
     *
     */
    void meshEdgeSpec_readsHexAndVariant();
    /**
     *
     * @brief Require a legacy velocity-vector ARGB color to decode exactly.
     * This case does not separately exercise a QColor-variant input despite its name.
     *
     */
    void velocityVectorSpec_readsHexAndVariant();

    // ── JSON boundary ──────────────────────────────────────────────────
    /**
     *
     * @brief Require five tested color-suffixed properties to rehydrate as QColor after JSON.
     * Pins NoData hex serialization and selected NoData/wide/line exact colors, including
     * semi-transparent line blue; no filesystem IO.
     *
     */
    void symbolLayerJson_rehydratesAllColorKeys();
};

// ── SymbolProps helpers ────────────────────────────────────────────────

void TestSymbolPropsColor::readColor_acceptsVariant()
{
    QVariantMap p;
    p.insert(QStringLiteral("color"), QVariant::fromValue(kSemiBlue));
    QCOMPARE(SymbolProps::readColor(p, QStringLiteral("color")), kSemiBlue);
}

void TestSymbolPropsColor::readColor_acceptsHexString()
{
    QVariantMap p;
    p.insert(QStringLiteral("color"), hexOf(kSemiBlue));
    QCOMPARE(SymbolProps::readColor(p, QStringLiteral("color")), kSemiBlue);
}

void TestSymbolPropsColor::readColor_acceptsNamedString()
{
    QVariantMap p;
    p.insert(QStringLiteral("color"), QStringLiteral("red"));
    QCOMPARE(SymbolProps::readColor(p, QStringLiteral("color")), QColor(Qt::red));
}

void TestSymbolPropsColor::readColor_fallbackOnMissingOrJunk()
{
    QVariantMap p;
    QCOMPARE(SymbolProps::readColor(p, QStringLiteral("color"), kRed), kRed);
    p.insert(QStringLiteral("color"), QStringLiteral("not-a-colour"));
    QCOMPARE(SymbolProps::readColor(p, QStringLiteral("color"), kRed), kRed);
}

void TestSymbolPropsColor::writeColor_storesVariant()
{
    QVariantMap p;
    SymbolProps::writeColor(p, QStringLiteral("color"), kSemiBlue);
    QCOMPARE(p.value(QStringLiteral("color")).userType(), static_cast<int>(QMetaType::QColor));
    QCOMPARE(p.value(QStringLiteral("color")).value<QColor>(), kSemiBlue);
}

void TestSymbolPropsColor::firstColor_prefersFillThenColorThenOutline()
{
    SymbolStyle s;
    SymbolLayer sl;
    SymbolProps::writeColor(sl.props, QStringLiteral("outlineColor"), kRed);
    SymbolProps::writeColor(sl.props, QStringLiteral("color"), QColor(Qt::green));
    SymbolProps::writeColor(sl.props, QStringLiteral("fillColor"), kSemiBlue);
    s.layers.append(sl);
    QCOMPARE(SymbolProps::firstColor(s), kSemiBlue); // fillColor wins

    SymbolStyle s2;
    SymbolLayer sl2;
    SymbolProps::writeColor(sl2.props, QStringLiteral("outlineColor"), kRed);
    SymbolProps::writeColor(sl2.props, QStringLiteral("color"), QColor(Qt::green));
    s2.layers.append(sl2);
    QCOMPARE(SymbolProps::firstColor(s2), QColor(Qt::green)); // then color

    SymbolStyle s3;
    SymbolLayer sl3;
    SymbolProps::writeColor(sl3.props, QStringLiteral("outlineColor"), kRed);
    s3.layers.append(sl3);
    QCOMPARE(SymbolProps::firstColor(s3), kRed); // then outlineColor

    QCOMPARE(SymbolProps::firstColor(SymbolStyle{}, kRed), kRed); // fallback
}

void TestSymbolPropsColor::firstColor_toleratesHex()
{
    SymbolStyle s;
    SymbolLayer sl;
    sl.props.insert(QStringLiteral("color"), hexOf(kSemiBlue)); // legacy hex
    s.layers.append(sl);
    QCOMPARE(SymbolProps::firstColor(s), kSemiBlue);
}

void TestSymbolPropsColor::overrideColorInPlace_writesOnlyAdvertisedSlots()
{
    SymbolStyle s;
    SymbolLayer marker; // advertises fillColor only
    SymbolProps::writeColor(marker.props, QStringLiteral("fillColor"), kRed);
    SymbolLayer line; // advertises color only
    SymbolProps::writeColor(line.props, QStringLiteral("color"), kRed);
    SymbolLayer bare; // advertises no colour slot
    s.layers << marker << line << bare;

    SymbolProps::overrideColorInPlace(s, kSemiBlue);

    QCOMPARE(SymbolProps::readColor(s.layers[0].props, QStringLiteral("fillColor")), kSemiBlue);
    QVERIFY(!s.layers[0].props.contains(QStringLiteral("color")));
    QCOMPARE(SymbolProps::readColor(s.layers[1].props, QStringLiteral("color")), kSemiBlue);
    QVERIFY(!s.layers[1].props.contains(QStringLiteral("fillColor")));
    QVERIFY(s.layers[2].props.isEmpty());
}

// ── Typed spec readers ─────────────────────────────────────────────────

void TestSymbolPropsColor::markerSpec_readsHexAndVariant()
{
    SymbolLayer hexed;
    hexed.kind = SymbolLayerKind::SimpleMarker;
    hexed.props.insert(QStringLiteral("fillColor"), hexOf(kSemiBlue));
    hexed.props.insert(QStringLiteral("outlineColor"), hexOf(kRed));
    const auto fromHex = MarkerSymbolLayerSpec::fromSymbolLayer(hexed);
    QCOMPARE(fromHex.fillColor, kSemiBlue);
    QCOMPARE(fromHex.outlineColor, kRed);

    const auto roundTrip =
        MarkerSymbolLayerSpec::fromSymbolLayer(MarkerSymbolLayerSpec{}.toSymbolLayer());
    QCOMPARE(roundTrip.fillColor, MarkerSymbolLayerSpec{}.fillColor);
    QCOMPARE(roundTrip.outlineColor, MarkerSymbolLayerSpec{}.outlineColor);
}

void TestSymbolPropsColor::lineSpec_readsHexAndVariant()
{
    SymbolLayer hexed;
    hexed.kind = SymbolLayerKind::SimpleLine;
    hexed.props.insert(QStringLiteral("color"), hexOf(kSemiBlue));
    hexed.props.insert(QStringLiteral("arrowColor"), hexOf(kRed));
    const auto fromHex = LineSymbolLayerSpec::fromSymbolLayer(hexed);
    QCOMPARE(fromHex.color, kSemiBlue);
    QCOMPARE(fromHex.arrows.color, kRed);

    const auto roundTrip =
        LineSymbolLayerSpec::fromSymbolLayer(LineSymbolLayerSpec{}.toSymbolLayer());
    QCOMPARE(roundTrip.color, LineSymbolLayerSpec{}.color);
}

void TestSymbolPropsColor::fillSpec_readsHexAndVariant()
{
    SymbolLayer hexed;
    hexed.kind = SymbolLayerKind::SimpleFill;
    hexed.props.insert(QStringLiteral("fillColor"), hexOf(kSemiBlue));
    hexed.props.insert(QStringLiteral("outlineColor"), hexOf(kRed));
    const auto fromHex = FillSymbolLayerSpec::fromSymbolLayer(hexed);
    QCOMPARE(fromHex.fillColor, kSemiBlue);
    QCOMPARE(fromHex.outlineColor, kRed);

    const auto roundTrip =
        FillSymbolLayerSpec::fromSymbolLayer(FillSymbolLayerSpec{}.toSymbolLayer());
    QCOMPARE(roundTrip.fillColor, FillSymbolLayerSpec{}.fillColor);
}

void TestSymbolPropsColor::rasterRampSpec_readsHexAndVariant()
{
    SymbolLayer hexed;
    hexed.kind = SymbolLayerKind::RasterColorRamp;
    hexed.props.insert(QStringLiteral("noDataColor"), hexOf(kSemiBlue));
    QCOMPARE(RasterColorRampSymbolLayerSpec::fromSymbolLayer(hexed).noDataColor, kSemiBlue);

    const auto roundTrip = RasterColorRampSymbolLayerSpec::fromSymbolLayer(
        RasterColorRampSymbolLayerSpec{}.toSymbolLayer());
    QCOMPARE(roundTrip.noDataColor, RasterColorRampSymbolLayerSpec{}.noDataColor);
}

void TestSymbolPropsColor::contourSpec_readsHexAndVariant()
{
    SymbolLayer hexed;
    hexed.kind = SymbolLayerKind::Contour;
    hexed.props.insert(QStringLiteral("lineColor"), hexOf(kRed));
    QCOMPARE(ContourSymbolLayerSpec::fromSymbolLayer(hexed).lineColor, kRed);

    const auto roundTrip =
        ContourSymbolLayerSpec::fromSymbolLayer(ContourSymbolLayerSpec{}.toSymbolLayer());
    QCOMPARE(roundTrip.lineColor, ContourSymbolLayerSpec{}.lineColor);
}

void TestSymbolPropsColor::meshEdgeSpec_readsHexAndVariant()
{
    SymbolLayer hexed;
    hexed.kind = SymbolLayerKind::MeshEdge;
    hexed.props.insert(QStringLiteral("color"), hexOf(kRed));
    hexed.props.insert(QStringLiteral("wideColor"), hexOf(kSemiBlue));
    const auto fromHex = MeshEdgeSymbolLayerSpec::fromSymbolLayer(hexed);
    QCOMPARE(fromHex.color, kRed);
    QCOMPARE(fromHex.wideColor, kSemiBlue);
}

void TestSymbolPropsColor::velocityVectorSpec_readsHexAndVariant()
{
    SymbolLayer hexed;
    hexed.kind = SymbolLayerKind::VectorGlyph;
    hexed.props.insert(QStringLiteral("color"), hexOf(kRed));
    QCOMPARE(VelocityVectorSymbolLayerSpec::fromSymbolLayer(hexed).color, kRed);
}

// ── JSON boundary ──────────────────────────────────────────────────────

void TestSymbolPropsColor::symbolLayerJson_rehydratesAllColorKeys()
{
    SymbolLayer original;
    original.kind = SymbolLayerKind::MeshEdge;
    SymbolProps::writeColor(original.props, QStringLiteral("color"), kRed);
    SymbolProps::writeColor(original.props, QStringLiteral("fillColor"), kSemiBlue);
    // Raster grammar keys the old fixed rehydration list missed:
    SymbolProps::writeColor(original.props, QStringLiteral("noDataColor"), kRed);
    SymbolProps::writeColor(original.props, QStringLiteral("lineColor"), kSemiBlue);
    SymbolProps::writeColor(original.props, QStringLiteral("wideColor"), kRed);

    const QJsonObject j = original.toJson();
    // JSON boundary stores hex strings (QColor variants have no JSON form).
    const QJsonObject jsonProps = j.value(QStringLiteral("props")).toObject();
    QCOMPARE(jsonProps.value(QStringLiteral("noDataColor")).toString(), hexOf(kRed));

    SymbolLayer restored;
    restored.fromJson(j);
    const QStringList keys = {QStringLiteral("color"), QStringLiteral("fillColor"),
                              QStringLiteral("noDataColor"), QStringLiteral("lineColor"),
                              QStringLiteral("wideColor")};
    for (const QString &key : keys) {
        QCOMPARE(restored.props.value(key).userType(), static_cast<int>(QMetaType::QColor));
    }
    QCOMPARE(restored.props.value(QStringLiteral("noDataColor")).value<QColor>(), kRed);
    QCOMPARE(restored.props.value(QStringLiteral("wideColor")).value<QColor>(), kRed);
    QCOMPARE(restored.props.value(QStringLiteral("lineColor")).value<QColor>(), kSemiBlue);
}

QTEST_MAIN(TestSymbolPropsColor)
#include "test_symbolprops_color.moc"
