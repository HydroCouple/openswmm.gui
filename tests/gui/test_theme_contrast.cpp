// UI redesign P2 — WCAG AA contrast gate over the design tokens
// (include/ui/theme/themetokens.h). Pure math on the two schemes: any
// token edit that drops a text/surface pairing below 4.5:1 (or a
// non-text UI glyph below 3:1) fails here before it ships.

/**
 * @file      test_theme_contrast.cpp
 * @brief     Tests for theme contrast.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest/QtTest>

#include <QColor>

#include "ui/theme/themetokens.h"

using openswmmvis::ui::ThemeColors;
using openswmmvis::ui::darkColors;
using openswmmvis::ui::lightColors;

namespace
{

// WCAG 2.x relative luminance.
/**
 *
 * @brief Convert one normalized sRGB fixture channel through the encoded luminance transfer
 * curve.
 * @param srgb Channel intended in [0,1]; no clamping or finite validation.
 * @return srgb/12.92 through 0.04045, otherwise ((srgb+0.055)/1.055)^2.4.
 * No state mutation; nonphysical inputs follow ordinary double arithmetic.
 *
 */
double channel(double srgb)
{
    return srgb <= 0.04045 ? srgb / 12.92 : std::pow((srgb + 0.055) / 1.055, 2.4);
}

/**
 *
 * @brief Compute relative luminance from fixture QColor RGB channels.
 * @param c Borrowed valid opaque color in the intended sRGB interpretation.
 * @return Weighted linearized RGB (0.2126,0.7152,0.0722); alpha is ignored, so
 * transparency compositing must be resolved before calling for a physical comparison.
 *
 */
double luminance(const QColor &c)
{
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

/**
 *
 * @brief Compute an order-independent fixture luminance contrast ratio.
 * @param a Borrowed first valid opaque sRGB color.
 * @param b Borrowed second valid opaque sRGB color.
 * @return (max(luminance)+0.05)/(min(luminance)+0.05); no alpha compositing,
 * validity checks or color-profile transformation is performed.
 *
 */
double ratio(const QColor &a, const QColor &b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    const double lighter = std::max(la, lb);
    const double darker = std::min(la, lb);
    return (lighter + 0.05) / (darker + 0.05);
}

} // namespace

class TestThemeContrast : public QObject
{
    Q_OBJECT

private slots:
    /**
     *
     * @brief Register bool dark rows selecting the shipped Light and Dark token schemes.
     *
     */
    void schemes_data();
    /**
     *
     * @brief Run the contrast assertions for the bool dark data row without modifying the global
     * theme.
     *
     */
    void schemes();

private:
    /**
     *
     * @brief Assert the listed palette text/surface and focus-ring contrast thresholds.
     * @param c Borrowed complete opaque token palette, unchanged by this helper.
     * Checks specified text/banner/plot pairs at ratio 4.5 and focus rings at three;
     * this is a mathematical token gate, not a rendered-font/accessibility audit.
     * Each failed macro assertion returns from this helper with a QtTest failure.
     *
     */
    void verifyScheme(const ThemeColors &c);
};

void TestThemeContrast::schemes_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("light") << false;
    QTest::newRow("dark") << true;
}

#define CHECK_RATIO(fg, bg, minimum)                                                               \
    do {                                                                                           \
        const double r = ratio(fg, bg);                                                            \
        QVERIFY2(r >= (minimum), qPrintable(QStringLiteral("%1 vs %2: %3 < %4")                    \
                                                .arg(QStringLiteral(#fg), QStringLiteral(#bg))     \
                                                .arg(r, 0, 'f', 2)                                 \
                                                .arg(minimum)));                                   \
    } while (false)

void TestThemeContrast::verifyScheme(const ThemeColors &c)
{
    // Text on surfaces — AA normal text.
    CHECK_RATIO(c.text, c.surfaceWindow, 4.5);
    CHECK_RATIO(c.text, c.surfaceRaised, 4.5);
    CHECK_RATIO(c.text, c.surfaceSunken, 4.5);
    CHECK_RATIO(c.hintText, c.surfaceWindow, 4.5);
    CHECK_RATIO(c.hintText, c.surfaceRaised, 4.5);

    // Interaction pairs.
    CHECK_RATIO(c.accentText, c.accent, 4.5);
    CHECK_RATIO(c.selectionText, c.selectionFill, 4.5);
    CHECK_RATIO(c.focusRing, c.surfaceWindow, 3.0);
    CHECK_RATIO(c.focusRing, c.surfaceRaised, 3.0);

    // Semantic status used as text.
    CHECK_RATIO(c.error, c.surfaceWindow, 4.5);
    CHECK_RATIO(c.error, c.surfaceRaised, 4.5);
    CHECK_RATIO(c.warning, c.surfaceWindow, 4.5);
    CHECK_RATIO(c.success, c.surfaceWindow, 4.5);
    CHECK_RATIO(c.info, c.surfaceWindow, 4.5);

    // Banner strips.
    CHECK_RATIO(c.bannerErrorFg, c.bannerErrorBg, 4.5);
    CHECK_RATIO(c.bannerSuccessFg, c.bannerSuccessBg, 4.5);
    CHECK_RATIO(c.bannerInfoFg, c.bannerInfoBg, 4.5);

    // Plot chrome that carries information.
    CHECK_RATIO(c.plotAxis, c.plotBackground, 4.5);
    CHECK_RATIO(c.plotNodeLabel, c.plotBackground, 4.5);
}

void TestThemeContrast::schemes()
{
    QFETCH(bool, dark);
    verifyScheme(dark ? darkColors() : lightColors());
}

QTEST_APPLESS_MAIN(TestThemeContrast)
#include "test_theme_contrast.moc"
