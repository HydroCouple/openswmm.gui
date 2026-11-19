// UI redesign P2 — ThemeManager behavior: mode switching applies the
// token palette to the application, themeChanged fires exactly on
// effective-scheme changes, and System mode under the offscreen QPA
// (which reports Unknown) falls back to Light.

/**
 * @file      test_theme_manager.cpp
 * @brief     Tests for theme manager.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest/QtTest>

#include <QApplication>
#include <functional>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QSignalSpy>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui/theme/thememanager.h"
#include "ui/theme/reducedmotion.h"
#include "ui/theme/themetokens.h"

using openswmmvis::ui::ThemeManager;
using openswmmvis::ui::darkColors;
using openswmmvis::ui::highContrastDarkColors;
using openswmmvis::ui::highContrastLightColors;
using openswmmvis::ui::lightColors;

class TestThemeManager : public QObject
{
    Q_OBJECT

private slots:
    /** @brief Replace the system reader with one reporting both settings off (deterministic). */
    void initTestCase();
    /**
     *
     * @brief Require fresh System mode under an offscreen/Unknown scheme to use Light tokens for
     * Window, PlaceholderText and Mid.
     * Depends on a fresh process/default settings and an offscreen platform; applies
     * the palette globally without restoring a previous palette.
     *
     */
    void systemModeFallsBackToLightOffscreen();
    /**
     *
     * @brief Require Dark mode/token identity and selected palette roles, with distinct disabled
     * text, then return the global mode to Light.
     *
     */
    void darkModeAppliesDarkPalette();
    /**
     *
     * @brief Require no signal on repeated Light/apply and one signal for each Dark/System
     * effective-scheme change.
     * Assumes offscreen System resolves to Light; leaves global mode System.
     *
     */
    void themeChangedFiresOnSchemeChangeOnly();
    /**
     *
     * @brief Require all three theme modes to round-trip their string forms and unknown Sepia to
     * fall back to System.
     *
     */
    void modeStringRoundTrip();
    void focusedItemViewsShowTheFocusRing();
    void contrastFollowsTheSystemUnlessOverridden();
    void highContrastAppliesStrongerChrome();
    void motionFollowsTheSystemUnlessOverridden();
    void preferenceStringRoundTrip();
};

namespace
{
openswmmvis::platform::SystemAccessibility g_system;

void resetAccessibility()
{
    auto *theme = ThemeManager::instance();
    g_system = {};
    theme->setContrastPreference(ThemeManager::Preference::Automatic);
    theme->setMotionPreference(ThemeManager::Preference::Automatic);
    theme->setSystemSettingsReader([] { return g_system; });
}
} // namespace

void TestThemeManager::initTestCase()
{
    resetAccessibility();
}

void TestThemeManager::systemModeFallsBackToLightOffscreen()
{
    auto *theme = ThemeManager::instance();
    QCOMPARE(theme->mode(), ThemeManager::Mode::System);
    QCOMPARE(theme->effectiveScheme(), Qt::ColorScheme::Light);

    theme->apply();
    QCOMPARE(qApp->palette().color(QPalette::Window), lightColors().surfaceWindow);
    QCOMPARE(qApp->palette().color(QPalette::PlaceholderText), lightColors().hintText);
    // The `palette(mid)` hint sites ride on the Mid role.
    QCOMPARE(qApp->palette().color(QPalette::Mid), lightColors().hintText);
}

void TestThemeManager::darkModeAppliesDarkPalette()
{
    auto *theme = ThemeManager::instance();
    theme->setMode(ThemeManager::Mode::Dark);
    QCOMPARE(theme->effectiveScheme(), Qt::ColorScheme::Dark);
    QCOMPARE(&theme->colors(), &darkColors());

    QCOMPARE(qApp->palette().color(QPalette::Window), darkColors().surfaceWindow);
    QCOMPARE(qApp->palette().color(QPalette::Base), darkColors().surfaceRaised);
    QCOMPARE(qApp->palette().color(QPalette::Highlight), darkColors().selectionFill);
    // Disabled group derived, distinct from enabled text.
    QVERIFY(qApp->palette().color(QPalette::Disabled, QPalette::Text) !=
            qApp->palette().color(QPalette::Active, QPalette::Text));

    theme->setMode(ThemeManager::Mode::Light);
    QCOMPARE(qApp->palette().color(QPalette::Window), lightColors().surfaceWindow);
}

void TestThemeManager::themeChangedFiresOnSchemeChangeOnly()
{
    auto *theme = ThemeManager::instance();
    theme->setMode(ThemeManager::Mode::Light);

    QSignalSpy spy(theme, &ThemeManager::themeChanged);
    theme->setMode(ThemeManager::Mode::Light); // no-op: same mode
    QCOMPARE(spy.count(), 0);
    theme->apply(); // same scheme re-applied
    QCOMPARE(spy.count(), 0);
    theme->setMode(ThemeManager::Mode::Dark); // scheme flips
    QCOMPARE(spy.count(), 1);
    theme->setMode(ThemeManager::Mode::System); // offscreen system == light
    QCOMPARE(spy.count(), 2);
}

void TestThemeManager::modeStringRoundTrip()
{
    QCOMPARE(ThemeManager::modeFromString(ThemeManager::modeToString(ThemeManager::Mode::System)),
             ThemeManager::Mode::System);
    QCOMPARE(ThemeManager::modeFromString(ThemeManager::modeToString(ThemeManager::Mode::Light)),
             ThemeManager::Mode::Light);
    QCOMPARE(ThemeManager::modeFromString(ThemeManager::modeToString(ThemeManager::Mode::Dark)),
             ThemeManager::Mode::Dark);
    // Unknown input degrades to System, never crashes or picks Dark.
    QCOMPARE(ThemeManager::modeFromString(QStringLiteral("Sepia")), ThemeManager::Mode::System);
}

/*! A-11: with the theme applied, a table, list or tree reached by Tab draws the theme's focus
 *  ring around its current cell. Fusion alone marks it with a faint fill only. */
void TestThemeManager::focusedItemViewsShowTheFocusRing()
{
    qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    auto *theme = ThemeManager::instance();
    theme->setMode(ThemeManager::Mode::Light);
    theme->apply();
    const QColor ring = theme->colors().focusRing;
    const auto ringPixels = [ring](const QImage &img) {
        int n = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QColor c = img.pixelColor(x, y);
                if (std::abs(c.red() - ring.red()) + std::abs(c.green() - ring.green()) +
                        std::abs(c.blue() - ring.blue()) < 60)
                    ++n;
            }
        return n;
    };
    const QList<std::function<QAbstractItemView *()>> views = {
        [] { return new QTableWidget(3, 2); },
        [] {
            auto *list = new QListWidget;
            list->addItems({QStringLiteral("a"), QStringLiteral("b")});
            return list;
        },
        [] {
            auto *tree = new QTreeWidget;
            tree->addTopLevelItem(new QTreeWidgetItem({QStringLiteral("a")}));
            return tree;
        }};
    for (const auto &make : views) {
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        auto *before = new QLineEdit(&window);
        QAbstractItemView *view = make();
        layout->addWidget(before);
        layout->addWidget(view);
        window.resize(260, 200);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        before->setFocus();
        QTest::keyClick(before, Qt::Key_Tab);
        QTRY_VERIFY(view->hasFocus());
        const QImage img = view->grab().toImage().convertToFormat(QImage::Format_RGB32);
        QVERIFY2(ringPixels(img) > 100, view->metaObject()->className());
    }
}

void TestThemeManager::contrastFollowsTheSystemUnlessOverridden()
{
    auto *theme = ThemeManager::instance();
    theme->setMode(ThemeManager::Mode::Light);
    resetAccessibility();
    QSignalSpy changed(theme, &ThemeManager::themeChanged);
    QVERIFY(!theme->highContrast());
    QCOMPARE(&theme->colors(), &lightColors());

    g_system.highContrast = true; // the user turns on Increase contrast and comes back
    theme->refreshSystemSettings();
    QVERIFY(theme->highContrast());
    QCOMPARE(&theme->colors(), &highContrastLightColors());
    QCOMPARE(changed.count(), 1);

    theme->setContrastPreference(ThemeManager::Preference::Off);
    QVERIFY(!theme->highContrast());
    QCOMPARE(changed.count(), 2);
    g_system.highContrast = false;
    theme->refreshSystemSettings();
    QCOMPARE(changed.count(), 2); // overridden: the system change does nothing
    theme->setContrastPreference(ThemeManager::Preference::On);
    QVERIFY(theme->highContrast());
    QCOMPARE(changed.count(), 3);
    resetAccessibility();
    QVERIFY(!theme->highContrast());
}

void TestThemeManager::highContrastAppliesStrongerChrome()
{
    auto *theme = ThemeManager::instance();
    resetAccessibility();
    theme->setMode(ThemeManager::Mode::Light);
    theme->setContrastPreference(ThemeManager::Preference::On);
    QCOMPARE(qApp->palette().color(QPalette::WindowText), highContrastLightColors().text);
    QCOMPARE(qApp->palette().color(QPalette::Highlight), highContrastLightColors().selectionFill);
    QVERIFY2(qApp->styleSheet().contains(QStringLiteral("outline: 3px")), qPrintable(qApp->styleSheet()));
    theme->setMode(ThemeManager::Mode::Dark);
    QCOMPARE(&theme->colors(), &highContrastDarkColors());
    QCOMPARE(qApp->palette().color(QPalette::Window), highContrastDarkColors().surfaceWindow);
    QVERIFY(qApp->styleSheet().contains(highContrastDarkColors().focusRing.name()));
    resetAccessibility();
    theme->setMode(ThemeManager::Mode::Light);
    QVERIFY(qApp->styleSheet().contains(QStringLiteral("outline: 2px")));
    QCOMPARE(qApp->palette().color(QPalette::WindowText), lightColors().text);
}

void TestThemeManager::motionFollowsTheSystemUnlessOverridden()
{
    auto *theme = ThemeManager::instance();
    resetAccessibility();
    QSignalSpy changed(theme, &ThemeManager::motionChanged);
    QVERIFY(!theme->reduceMotion());
    QVERIFY(!openswmmvis::ui::motionReduced());

    g_system.reduceMotion = true;
    theme->refreshSystemSettings();
    QVERIFY(theme->reduceMotion());
    QVERIFY(openswmmvis::ui::motionReduced()); // what the map and the streaks read
    QCOMPARE(changed.count(), 1);

    theme->setMotionPreference(ThemeManager::Preference::Off);
    QVERIFY(!openswmmvis::ui::motionReduced());
    QCOMPARE(changed.count(), 2);
    theme->setMotionPreference(ThemeManager::Preference::On);
    g_system.reduceMotion = false;
    theme->refreshSystemSettings();
    QVERIFY(openswmmvis::ui::motionReduced()); // On holds whatever the system says
    QCOMPARE(changed.count(), 3);
    resetAccessibility();
    QVERIFY(!openswmmvis::ui::motionReduced());
}

void TestThemeManager::preferenceStringRoundTrip()
{
    using P = ThemeManager::Preference;
    for (P p : {P::Automatic, P::On, P::Off})
        QCOMPARE(ThemeManager::preferenceFromString(ThemeManager::preferenceToString(p)), p);
    QCOMPARE(ThemeManager::preferenceFromString(QStringLiteral("on")), P::On);
    QCOMPARE(ThemeManager::preferenceFromString(QStringLiteral("sometimes")), P::Automatic);
}

QTEST_MAIN(TestThemeManager)
#include "test_theme_manager.moc"
