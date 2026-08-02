// UI redesign P2 — WCAG AA contrast gate over the design tokens
// (include/ui/theme/themetokens.h). Pure math on the two schemes: any
// token edit that drops a text/surface pairing below 4.5:1 (or a
// non-text UI glyph below 3:1) fails here before it ships.
#include <QtTest/QtTest>

#include <QColor>

#include "ui/theme/themetokens.h"

using openswmmvis::ui::ThemeColors;
using openswmmvis::ui::darkColors;
using openswmmvis::ui::lightColors;

namespace {

// WCAG 2.x relative luminance.
double channel(double srgb)
{
    return srgb <= 0.04045 ? srgb / 12.92
                           : std::pow((srgb + 0.055) / 1.055, 2.4);
}

double luminance(const QColor &c)
{
    return 0.2126 * channel(c.redF())
         + 0.7152 * channel(c.greenF())
         + 0.0722 * channel(c.blueF());
}

double ratio(const QColor &a, const QColor &b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    const double lighter = std::max(la, lb);
    const double darker  = std::min(la, lb);
    return (lighter + 0.05) / (darker + 0.05);
}

}   // namespace

class TestThemeContrast : public QObject
{
    Q_OBJECT

private slots:
    void schemes_data();
    void schemes();

private:
    void verifyScheme(const ThemeColors &c);
};

void TestThemeContrast::schemes_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("light") << false;
    QTest::newRow("dark")  << true;
}

#define CHECK_RATIO(fg, bg, minimum)                                          \
    do {                                                                      \
        const double r = ratio(fg, bg);                                       \
        QVERIFY2(r >= (minimum),                                              \
                 qPrintable(QStringLiteral("%1 vs %2: %3 < %4")               \
                                .arg(QStringLiteral(#fg),                     \
                                     QStringLiteral(#bg))                     \
                                .arg(r, 0, 'f', 2)                            \
                                .arg(minimum)));                              \
    } while (false)

void TestThemeContrast::verifyScheme(const ThemeColors &c)
{
    // Text on surfaces — AA normal text.
    CHECK_RATIO(c.text,     c.surfaceWindow, 4.5);
    CHECK_RATIO(c.text,     c.surfaceRaised, 4.5);
    CHECK_RATIO(c.text,     c.surfaceSunken, 4.5);
    CHECK_RATIO(c.hintText, c.surfaceWindow, 4.5);
    CHECK_RATIO(c.hintText, c.surfaceRaised, 4.5);

    // Interaction pairs.
    CHECK_RATIO(c.accentText,    c.accent,        4.5);
    CHECK_RATIO(c.selectionText, c.selectionFill, 4.5);
    CHECK_RATIO(c.focusRing,     c.surfaceWindow, 3.0);
    CHECK_RATIO(c.focusRing,     c.surfaceRaised, 3.0);

    // Semantic status used as text.
    CHECK_RATIO(c.error,   c.surfaceWindow, 4.5);
    CHECK_RATIO(c.error,   c.surfaceRaised, 4.5);
    CHECK_RATIO(c.warning, c.surfaceWindow, 4.5);
    CHECK_RATIO(c.success, c.surfaceWindow, 4.5);
    CHECK_RATIO(c.info,    c.surfaceWindow, 4.5);

    // Banner strips.
    CHECK_RATIO(c.bannerErrorFg,   c.bannerErrorBg,   4.5);
    CHECK_RATIO(c.bannerSuccessFg, c.bannerSuccessBg, 4.5);
    CHECK_RATIO(c.bannerInfoFg,    c.bannerInfoBg,    4.5);

    // Plot chrome that carries information.
    CHECK_RATIO(c.plotAxis,      c.plotBackground, 4.5);
    CHECK_RATIO(c.plotNodeLabel, c.plotBackground, 4.5);
}

void TestThemeContrast::schemes()
{
    QFETCH(bool, dark);
    verifyScheme(dark ? darkColors() : lightColors());
}

QTEST_APPLESS_MAIN(TestThemeContrast)
#include "test_theme_contrast.moc"
