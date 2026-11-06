/**
 * @file      test_palettedrasterrenderer.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tests for VS.6 — PalettedRasterRenderer (categorical raster).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QColor>
#include <QtTest/QtTest>

#include "render/renderers/palettedrasterrenderer.h"

using namespace OpenSWMM::Render;

class TestPalettedRasterRenderer : public QObject
{
    Q_OBJECT
private slots:

    /**
     *
     * @brief Require listed integer classes and 1.4 rounded to class one to return their explicit
     * colors.
     *
     */
    void colorForValue_listedClassReturnsItsColor()
    {
        PalettedRasterRenderer r;
        r.setClasses({{1, QStringLiteral("Urban"), QColor(200, 0, 0)},
                      {2, QStringLiteral("Forest"), QColor(0, 150, 0)}});
        QCOMPARE(r.colorForValue(1.0), QColor(200, 0, 0));
        QCOMPARE(r.colorForValue(2.0), QColor(0, 150, 0));
        // Rounds to nearest integer class.
        QCOMPARE(r.colorForValue(1.4), QColor(200, 0, 0));
    }

    /**
     *
     * @brief Require explicit NoData to override a listed value with transparent alpha.
     *
     */
    void colorForValue_noDataIsTransparent()
    {
        PalettedRasterRenderer r;
        r.setClasses({{1, QStringLiteral("A"), QColor(10, 20, 30)}});
        QCOMPARE(r.colorForValue(1.0, /*isNoData=*/true).alpha(), 0);
    }

    /**
     *
     * @brief Require an unlisted value to return a valid nontransparent fallback palette color.
     * This asserts positive alpha, not complete opacity.
     *
     */
    void colorForValue_unlistedValueStillDraws()
    {
        PalettedRasterRenderer r;
        r.setClasses({{1, QStringLiteral("A"), QColor(10, 20, 30)}});
        // A value with no explicit class must still get an opaque palette colour.
        const QColor c = r.colorForValue(7.0);
        QVERIFY(c.isValid());
        QVERIFY(c.alpha() > 0);
    }

    /**
     *
     * @brief Require one legend row per class, explicit label/key for class one and numeric
     * fallback label for the empty third label.
     *
     */
    void legendItems_oneRowPerClassWithLabels()
    {
        PalettedRasterRenderer r;
        r.setClasses({{1, QStringLiteral("Urban"), QColor(200, 0, 0)},
                      {2, QStringLiteral("Forest"), QColor(0, 150, 0)},
                      {3, QString(), QColor(0, 0, 200)}});
        const auto items = r.legendSymbolItems();
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.at(0).label, QStringLiteral("Urban"));
        QCOMPARE(items.at(0).classKey, QStringLiteral("1"));
        // Empty label falls back to the value string.
        QCOMPARE(items.at(2).label, QStringLiteral("3"));
    }

    /**
     *
     * @brief Require three Tab10 classes to retain input values, numeric first label and distinct
     * valid first/second colors.
     *
     */
    void buildClassesFromValues_assignsLabelsAndColors()
    {
        PalettedRasterRenderer r;
        r.setPaletteName(QStringLiteral("Tab10"));
        r.buildClassesFromValues({11, 22, 33});
        QCOMPARE(r.classes().size(), 3);
        QCOMPARE(r.classes().at(0).value, 11);
        QCOMPARE(r.classes().at(0).label, QStringLiteral("11"));
        QVERIFY(r.classes().at(0).color.isValid());
        // Distinct palette colours for distinct classes.
        QVERIFY(r.classes().at(0).color != r.classes().at(1).color);
    }

    /**
     *
     * @brief Round-trip D3 palette identity, two classes, first color and second label in memory.
     *
     */
    void jsonRoundTrip()
    {
        PalettedRasterRenderer in;
        in.setPaletteName(QStringLiteral("D3"));
        in.setClasses({{5, QStringLiteral("Water"), QColor(0, 0, 255)},
                       {9, QStringLiteral("Bare"), QColor(180, 160, 120)}});
        PalettedRasterRenderer out;
        out.fromJson(in.toJson());
        QCOMPARE(out.rendererId(), QStringLiteral("palettedraster"));
        QCOMPARE(out.paletteName(), QStringLiteral("D3"));
        QCOMPARE(out.classes().size(), 2);
        QCOMPARE(out.colorForValue(5.0), QColor(0, 0, 255));
        QCOMPARE(out.classes().at(1).label, QStringLiteral("Bare"));
    }

    /**
     *
     * @brief Require a nonnull clone to return the copied listed color.
     * The case does not mutate source/clone separately to prove later edit independence.
     *
     */
    void clone_isIndependentCopy()
    {
        PalettedRasterRenderer in;
        in.setClasses({{1, QStringLiteral("A"), QColor(1, 2, 3)}});
        auto c = in.clone();
        QVERIFY(c != nullptr);
        QCOMPARE(c->colorForValue(1.0), QColor(1, 2, 3));
    }
};

QTEST_MAIN(TestPalettedRasterRenderer)
#include "test_palettedrasterrenderer.moc"
