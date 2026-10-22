/**
 * @file      test_preferencesdialog_roundtrip.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Round-trip and structural contract for PreferencesDialog
 *         (workplans/OPTIONS_DIALOG_TABBED_RESTRUCTURE_PLAN_2026-09-07.md §6,
 *         HANDOFF_OPTIONS_DIALOG_TABS §5.2).
 *
 * The dialog had no automated test at all before T7, and T7 folds two of its
 * fifteen sidebar rows into a third as tabs. These three tests pin what the
 * fold must not change:
 *
 *   applyWithNoEditsWritesNothing  Apply with no edits is the identity on
 *                                  PreferencesManager's stored keys. A control
 *                                  that a page move left unread would show up
 *                                  here as its key reverting to a default.
 *   structure                      the twelve sidebar rows and the two inner
 *                                  tab sets, in PLAN §3 order.
 *   noScrollAt1280x800             the reason for the fold: no page needs a
 *                                  scrollbar at 1280x800, on any of its tabs.
 *
 * NORMALISATION (test 9): PreferencesManager writes back defaults for keys a
 * fresh settings store does not carry, so the FIRST Apply against an empty
 * store legitimately differs. The test therefore snapshots AFTER one Apply and
 * compares against a second — from then on Apply must be the identity.
 *
 * QSettings is redirected to a scratch store in initTestCase(), BEFORE anything
 * can call PreferencesManager::instance(): the manager holds a single
 * default-constructed QSettings inside a function-local static, so it binds to
 * the org/app names and format at that first call and never reloads. Without
 * the redirect this test would edit the developer's real preferences.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "core/preferencesmanager.h"
#include "ui/dialogs/preferencesdialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QMap>
#include <QObject>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QCheckBox>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTest>
#include <QVariant>
#include <QWidget>

namespace
{

/*! Artifacts land under tests/gui/data/ so a failure is inspectable
 *  (CLAUDE.md §4.1 — never a system temp dir). */
/** @brief Resolve retained preferences-test settings/artifact storage.
 * @return prefs_roundtrip_artifacts under SWMMVIS_GUI_TEST_DATA or "."; no directory creation here. */
QString settingsDir()
{
    const QString base = qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
    return QDir(base).filePath(QStringLiteral("prefs_roundtrip_artifacts"));
}

/*! Every key/value the store holds, flattened. */
/** @brief Copy every key/value from the current test QSettings namespace.
 * @return Flattened independent settings map, without explicitly synchronizing storage first. */
QMap<QString, QVariant> snapshotSettings()
{
    QMap<QString, QVariant> out;
    QSettings s;
    for (const QString &k : s.allKeys())
        out.insert(k, s.value(k));
    return out;
}

/*! Report the first few differing keys rather than just "maps differ". */
/** @brief Describe up to eight differing settings values.
 * @param[in] a Before snapshot.
 * @param[in] b After snapshot.
 * @return Semicolon-separated key/value transitions and an ellipsis after the eighth difference, or empty when compared values match. Missing keys compare as default invalid QVariant values. */
QString describeDiff(const QMap<QString, QVariant> &a, const QMap<QString, QVariant> &b)
{
    QStringList lines;
    QStringList keys = a.keys();
    for (const QString &k : b.keys())
        if (!keys.contains(k))
            keys << k;
    for (const QString &k : keys) {
        if (a.value(k) == b.value(k))
            continue;
        lines
            << QStringLiteral("%1: %2 -> %3").arg(k, a.value(k).toString(), b.value(k).toString());
        if (lines.size() >= 8) {
            lines << QStringLiteral("...");
            break;
        }
    }
    return lines.join(QStringLiteral("; "));
}

} // namespace

class TestPreferencesDialogRoundtrip : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void applyWithNoEditsWritesNothing();
    void structure();
    void noScrollAt1280x800();
    void meshDefaultsShareIndependentControls();
};

/** @brief Redirect INI settings to retained artifacts before preference singleton creation and clear/synchronize the test store. */
void TestPreferencesDialogRoundtrip::initTestCase()
{
    // Must run before ANYTHING calls PreferencesManager::instance(): the
    // manager's QSettings binds to these at that first call. IniFormat is
    // mandatory — setPath is ignored for NativeFormat on macOS, and without it
    // this test would write to the real store (org "hydrocouple", app "SWMMVis
    // Stormwater Management Model").
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("test_preferencesdialog_roundtrip"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    const QString dir = settingsDir();
    QDir().mkpath(dir);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);

    // Start from a known store so a stale artifact from a previous run cannot
    // make the normalisation pass look like a real edit.
    {
        QSettings s;
        s.clear();
        s.sync();
    }
}

/*! Test 9 — an Apply with no edits must not change a single stored key. */
/** @brief Verify a second untouched Apply preserves every stored key after the first Apply materializes defaults. */
void TestPreferencesDialogRoundtrip::applyWithNoEditsWritesNothing()
{
    // Normalisation pass: the first Apply against a store with absent keys
    // legitimately materialises their defaults.
    {
        PreferencesDialog warm(nullptr);
        QVERIFY(QMetaObject::invokeMethod(&warm, "onApply", Qt::DirectConnection));
    }
    {
        QSettings s;
        s.sync();
    }
    const QMap<QString, QVariant> before = snapshotSettings();
    QVERIFY2(!before.isEmpty(), "the redirected store is empty after an Apply — the redirect or "
                                "writeToManager() is not wired");

    {
        PreferencesDialog dlg(nullptr);
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
    }
    {
        QSettings s;
        s.sync();
    }
    const QMap<QString, QVariant> after = snapshotSettings();

    QVERIFY2(before == after, qPrintable(QStringLiteral("Apply with no edits changed stored "
                                                        "preferences: %1")
                                             .arg(describeDiff(before, after))));
}

/*! Test 10 — the thirteen rows and the two inner tab sets, in PLAN §3 order. */
/** @brief Verify all thirteen sidebar labels and the exact rendering/simulation-default tab labels/order. */
void TestPreferencesDialogRoundtrip::structure()
{
    PreferencesDialog dlg(nullptr);
    auto *cats = dlg.findChild<QListWidget *>(QStringLiteral("categories"));
    QVERIFY2(cats, "the sidebar lost its objectName \"categories\"");

    const QStringList expected{
        QStringLiteral("General"),
        QStringLiteral("Selection"),
        // A single "&": addCategory feeds a QListWidgetItem, which takes no
        // mnemonic, so "&&" would render both ampersands (fixed in 30e1c839).
        QStringLiteral("Canvas & CRS"),
        QStringLiteral("Rendering"),
        QStringLiteral("Simulation"),
        QStringLiteral("Simulation Defaults"),
        QStringLiteral("Object Defaults"),
        QStringLiteral("Map Display"),
        QStringLiteral("Measure Tool"),
        QStringLiteral("Plots"),
        QStringLiteral("Naming"),
        QStringLiteral("Appearance"),
        QStringLiteral("Keyboard"),
    };
    QStringList actual;
    for (int i = 0; i < cats->count(); ++i)
        actual << cats->item(i)->text();
    QCOMPARE(actual, expected);

    // tabText() is the source text: a literal ampersand in a tab label is
    // "&&", since QTabBar reads a single "&" as a mnemonic (30e1c839).
    const struct {
        const char *tabs;
        QStringList titles;
    } kTabSets[] = {
        {"renderingTabs",
         {QStringLiteral("Labels"),
          QStringLiteral("Network Symbols"), // feedback C14 — was "Links && Nodes"
          QStringLiteral("GPU"), QStringLiteral("2D Mesh Edges")}},
        {"simulationDefaultsTabs",
         {QStringLiteral("Processes && Modules"), QStringLiteral("Hydraulics && Schedule"),
          QStringLiteral("Time Steps && Tolerances"), QStringLiteral("Dynamic Wave"),
          // Seven, not PLAN §3's five: the five 2D
          // groups need 1318 px against a 746 px viewport
          // (measured), so they split three ways.
          QStringLiteral("2D Solver"), QStringLiteral("2D Coupling && Rainfall"),
          QStringLiteral("2D Mesh")}},
    };
    for (const auto &set : kTabSets) {
        auto *tabs = dlg.findChild<QTabWidget *>(QLatin1String(set.tabs));
        QVERIFY2(tabs,
                 qPrintable(QStringLiteral("no QTabWidget named %1").arg(QLatin1String(set.tabs))));
        QStringList got;
        for (int i = 0; i < tabs->count(); ++i)
            got << tabs->tabText(i);
        QVERIFY2(got == set.titles,
                 qPrintable(QStringLiteral("%1: %2 != %3")
                                .arg(QLatin1String(set.tabs), got.join(QStringLiteral(" | ")),
                                     set.titles.join(QStringLiteral(" | ")))));
    }
}

/*! Test 10 — the point of the fold: nothing scrolls at 1280x800. */
/** @brief Verify non-table-dominated preference pages and their first inner tab set require no vertical page scrolling at 1280 by 800 pixels.
 * Keyboard/Object Defaults are intentionally excluded; the body does not assert every nested table has no scrolling. */
void TestPreferencesDialogRoundtrip::noScrollAt1280x800()
{
    PreferencesDialog dlg(nullptr);
    auto *cats = dlg.findChild<QListWidget *>(QStringLiteral("categories"));
    auto *pages = dlg.findChild<QStackedWidget *>(QStringLiteral("pages"));
    QVERIFY(cats && pages);

    dlg.resize(1280, 800);
    dlg.show();
    QTest::qWait(50);

    // Keyboard is a shortcut table and Object Defaults a per-type property
    // grid; both legitimately scroll their tables rather than their page.
    const QStringList tableDominated{
        QStringLiteral("Keyboard"),
        QStringLiteral("Object Defaults"),
    };
    QStringList overflows;
    for (int r = 0; r < cats->count(); ++r) {
        const QString title = cats->item(r)->text();
        if (tableDominated.contains(title))
            continue;
        cats->setCurrentRow(r);
        auto *sa = qobject_cast<QScrollArea *>(pages->widget(r));
        if (!sa)
            continue;
        QWidget *page = sa->widget();
        const QList<QTabWidget *> inner =
            page ? page->findChildren<QTabWidget *>() : QList<QTabWidget *>{};
        const int nTabs = inner.isEmpty() ? 1 : inner.first()->count();
        for (int t = 0; t < nTabs; ++t) {
            if (!inner.isEmpty())
                inner.first()->setCurrentIndex(t);
            QCoreApplication::processEvents();
            QTest::qWait(10);
            if (sa->verticalScrollBar()->maximum() > 0) {
                overflows << (inner.isEmpty() ? title
                                              : QStringLiteral("%1 › %2").arg(
                                                    title, inner.first()->tabText(t)));
            }
        }
    }
    QVERIFY2(overflows.isEmpty(), qPrintable(QStringLiteral("page(s) still scroll at 1280x800: %1")
                                                 .arg(overflows.join(QStringLiteral(", ")))));
}

/** @brief Verify coarsening, optional maximum size and terrain tolerance persist independently across Apply/reopen.
 * Checks every mesh settings page fits without either scrollbar, retains screenshots and resets 2D preferences to defaults. */
void TestPreferencesDialogRoundtrip::meshDefaultsShareIndependentControls()
{
    PreferencesDialog dialog(nullptr);
    auto *maximumMode = dialog.findChild<QComboBox *>("meshMaximumMode");
    auto *maximum = dialog.findChild<QDoubleSpinBox *>("meshMaxCellSizeSpin");
    auto *toleranceMode = dialog.findChild<QComboBox *>("meshToleranceMode");
    auto *tolerance = dialog.findChild<QDoubleSpinBox *>("meshTerrainTolSpin");
    QVERIFY(maximumMode && maximum && toleranceMode && tolerance);
    auto *coarsen = dialog.findChild<QCheckBox *>("meshCoarsenEnabled");
    auto *passes = dialog.findChild<QSpinBox *>("meshCoarsenPassesSpin");
    QVERIFY(coarsen && passes);
    coarsen->setChecked(true);
    passes->setValue(4);
    maximumMode->setCurrentIndex(1);
    maximum->setValue(50);
    toleranceMode->setCurrentIndex(1);
    tolerance->setValue(.1524);
    QVERIFY(QMetaObject::invokeMethod(&dialog, "onApply", Qt::DirectConnection));
    auto settings = PreferencesManager::instance()->twoDDefaults().resolutionSettings();
    QCOMPARE(settings.coarsenPasses, 4);
    QCOMPARE(settings.maximumM.value(), 50.0);
    QCOMPARE(settings.toleranceM.value(), .1524);
    QVERIFY(!settings.breaklineToleranceM);
    maximumMode->setCurrentIndex(0);
    QVERIFY(QMetaObject::invokeMethod(&dialog, "onApply", Qt::DirectConnection));
    PreferencesDialog reopened(nullptr);
    QVERIFY(reopened.findChild<QCheckBox *>("meshCoarsenEnabled")->isChecked());
    QCOMPARE(reopened.findChild<QSpinBox *>("meshCoarsenPassesSpin")->value(), 4);
    QCOMPARE(reopened.findChild<QComboBox *>("meshMaximumMode")->currentIndex(), 0);
    QCOMPARE(reopened.findChild<QDoubleSpinBox *>("meshTerrainTolSpin")->value(), .1524);
    auto *cats = reopened.findChild<QListWidget *>("categories");
    for (int i = 0; i < cats->count(); ++i)
        if (cats->item(i)->text() == "Simulation Defaults")
            cats->setCurrentRow(i);
    auto *tabs = reopened.findChild<QTabWidget *>("simulationDefaultsTabs");
    QVERIFY(tabs);
    for (int i = 0; i < tabs->count(); ++i)
        if (tabs->tabText(i).contains("Mesh"))
            tabs->setCurrentIndex(i);
    reopened.resize(1280, 800);
    reopened.show();
    QTest::qWait(50);
    QVERIFY(reopened.grab().save(QDir(settingsDir()).filePath("mesh-preferences.png")));
    auto *meshCategories = reopened.findChild<QTreeWidget *>("meshSettingsCategories");
    auto *meshPages = reopened.findChild<QStackedWidget *>("meshSettingsPages");
    QVERIFY(meshCategories && meshPages);
    for (int i = 0; i < meshCategories->topLevelItemCount(); ++i) {
        meshCategories->setCurrentItem(meshCategories->topLevelItem(i));
        QCoreApplication::processEvents();
        auto *scroll = qobject_cast<QScrollArea *>(meshPages->currentWidget());
        QVERIFY(scroll);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
        QVERIFY(reopened.grab().save(
            QDir(settingsDir()).filePath(QString("mesh-preferences-%1.png").arg(i))));
    }
    PreferencesManager::instance()->setTwoDDefaults(PreferencesManager::TwoDDefaults{});
}

QTEST_MAIN(TestPreferencesDialogRoundtrip)
#include "test_preferencesdialog_roundtrip.moc"
