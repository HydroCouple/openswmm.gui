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
#include <QPalette>
#include <QSignalSpy>

#include "ui/theme/thememanager.h"
#include "ui/theme/themetokens.h"

using openswmmvis::ui::ThemeManager;
using openswmmvis::ui::darkColors;
using openswmmvis::ui::lightColors;

class TestThemeManager : public QObject
{
    Q_OBJECT

private slots:
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
};

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

QTEST_MAIN(TestThemeManager)
#include "test_theme_manager.moc"
