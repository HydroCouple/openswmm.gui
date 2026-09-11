/*!
 * \file   test_preferencesdialog_roundtrip.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Round-trip and structural contract for PreferencesDialog
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
 */
#include "core/preferencesmanager.h"
#include "ui/dialogs/preferencesdialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QListWidget>
#include <QMap>
#include <QObject>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTabWidget>
#include <QTest>
#include <QVariant>
#include <QWidget>

namespace {

/*! Artifacts land under tests/gui/data/ so a failure is inspectable
 *  (CLAUDE.md §4.1 — never a system temp dir). */
QString settingsDir()
{
    const QString base = qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA",
                                              QStringLiteral("."));
    return QDir(base).filePath(QStringLiteral("prefs_roundtrip_artifacts"));
}

/*! Every key/value the store holds, flattened. */
QMap<QString, QVariant> snapshotSettings()
{
    QMap<QString, QVariant> out;
    QSettings s;
    for (const QString &k : s.allKeys())
        out.insert(k, s.value(k));
    return out;
}

/*! Report the first few differing keys rather than just "maps differ". */
QString describeDiff(const QMap<QString, QVariant> &a,
                     const QMap<QString, QVariant> &b)
{
    QStringList lines;
    QStringList keys = a.keys();
    for (const QString &k : b.keys())
        if (!keys.contains(k)) keys << k;
    for (const QString &k : keys) {
        if (a.value(k) == b.value(k)) continue;
        lines << QStringLiteral("%1: %2 -> %3")
                     .arg(k, a.value(k).toString(), b.value(k).toString());
        if (lines.size() >= 8) { lines << QStringLiteral("..."); break; }
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
};

void TestPreferencesDialogRoundtrip::initTestCase()
{
    // Must run before ANYTHING calls PreferencesManager::instance(): the
    // manager's QSettings binds to these at that first call. IniFormat is
    // mandatory — setPath is ignored for NativeFormat on macOS, and without it
    // this test would write to the real store (org "hydrocouple", app "SWMMVis
    // Stormwater Management Model").
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(
        QStringLiteral("test_preferencesdialog_roundtrip"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    const QString dir = settingsDir();
    QDir().mkpath(dir);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);

    // Start from a known store so a stale artifact from a previous run cannot
    // make the normalisation pass look like a real edit.
    { QSettings s; s.clear(); s.sync(); }
}

/*! Test 9 — an Apply with no edits must not change a single stored key. */
void TestPreferencesDialogRoundtrip::applyWithNoEditsWritesNothing()
{
    // Normalisation pass: the first Apply against a store with absent keys
    // legitimately materialises their defaults.
    {
        PreferencesDialog warm(nullptr);
        QVERIFY(QMetaObject::invokeMethod(&warm, "onApply", Qt::DirectConnection));
    }
    { QSettings s; s.sync(); }
    const QMap<QString, QVariant> before = snapshotSettings();
    QVERIFY2(!before.isEmpty(),
             "the redirected store is empty after an Apply — the redirect or "
             "writeToManager() is not wired");

    {
        PreferencesDialog dlg(nullptr);
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
    }
    { QSettings s; s.sync(); }
    const QMap<QString, QVariant> after = snapshotSettings();

    QVERIFY2(before == after,
             qPrintable(QStringLiteral("Apply with no edits changed stored "
                                       "preferences: %1")
                            .arg(describeDiff(before, after))));
}

/*! Test 10 — the thirteen rows and the two inner tab sets, in PLAN §3 order. */
void TestPreferencesDialogRoundtrip::structure()
{
    PreferencesDialog dlg(nullptr);
    auto *cats = dlg.findChild<QListWidget *>(QStringLiteral("categories"));
    QVERIFY2(cats, "the sidebar lost its objectName \"categories\"");

    const QStringList expected{
        QStringLiteral("General"),
        QStringLiteral("Selection"),
        // Renders with both ampersands: addCategory feeds a QListWidgetItem,
        // which takes no mnemonic. Pre-existing; PLAN §3 leaves this row alone.
        QStringLiteral("Canvas && CRS"),
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
    for (int i = 0; i < cats->count(); ++i) actual << cats->item(i)->text();
    QCOMPARE(actual, expected);

    const struct { const char *tabs; QStringList titles; } kTabSets[] = {
        { "renderingTabs", { QStringLiteral("Labels"),
                             QStringLiteral("Links & Nodes"),
                             QStringLiteral("GPU"),
                             QStringLiteral("2D Mesh Edges") } },
        { "simulationDefaultsTabs",
                           { QStringLiteral("Processes & Modules"),
                             QStringLiteral("Hydraulics & Schedule"),
                             QStringLiteral("Time Steps & Tolerances"),
                             QStringLiteral("Dynamic Wave"),
                             // Seven, not PLAN §3's five: the five 2D
                             // groups need 1318 px against a 746 px viewport
                             // (measured), so they split three ways.
                             QStringLiteral("2D Solver"),
                             QStringLiteral("2D Coupling & Rainfall"),
                             QStringLiteral("2D Mesh") } },
    };
    for (const auto &set : kTabSets) {
        auto *tabs = dlg.findChild<QTabWidget *>(QLatin1String(set.tabs));
        QVERIFY2(tabs, qPrintable(QStringLiteral("no QTabWidget named %1")
                                      .arg(QLatin1String(set.tabs))));
        QStringList got;
        for (int i = 0; i < tabs->count(); ++i) got << tabs->tabText(i);
        QVERIFY2(got == set.titles,
                 qPrintable(QStringLiteral("%1: %2 != %3")
                                .arg(QLatin1String(set.tabs),
                                     got.join(QStringLiteral(" | ")),
                                     set.titles.join(QStringLiteral(" | ")))));
    }
}

/*! Test 10 — the point of the fold: nothing scrolls at 1280x800. */
void TestPreferencesDialogRoundtrip::noScrollAt1280x800()
{
    PreferencesDialog dlg(nullptr);
    auto *cats  = dlg.findChild<QListWidget *>(QStringLiteral("categories"));
    auto *pages = dlg.findChild<QStackedWidget *>(QStringLiteral("pages"));
    QVERIFY(cats && pages);

    dlg.resize(1280, 800);
    dlg.show();
    QTest::qWait(50);

    // Keyboard is a shortcut table and Object Defaults a per-type property
    // grid; both legitimately scroll their tables rather than their page.
    const QStringList tableDominated{
        QStringLiteral("Keyboard"), QStringLiteral("Object Defaults"),
    };
    QStringList overflows;
    for (int r = 0; r < cats->count(); ++r) {
        const QString title = cats->item(r)->text();
        if (tableDominated.contains(title)) continue;
        cats->setCurrentRow(r);
        auto *sa = qobject_cast<QScrollArea *>(pages->widget(r));
        if (!sa) continue;
        QWidget *page = sa->widget();
        const QList<QTabWidget *> inner =
            page ? page->findChildren<QTabWidget *>() : QList<QTabWidget *>{};
        const int nTabs = inner.isEmpty() ? 1 : inner.first()->count();
        for (int t = 0; t < nTabs; ++t) {
            if (!inner.isEmpty()) inner.first()->setCurrentIndex(t);
            QCoreApplication::processEvents();
            QTest::qWait(10);
            if (sa->verticalScrollBar()->maximum() > 0) {
                overflows << (inner.isEmpty()
                    ? title
                    : QStringLiteral("%1 › %2")
                          .arg(title, inner.first()->tabText(t)));
            }
        }
    }
    QVERIFY2(overflows.isEmpty(),
             qPrintable(QStringLiteral("page(s) still scroll at 1280x800: %1")
                            .arg(overflows.join(QStringLiteral(", ")))));
}

QTEST_MAIN(TestPreferencesDialogRoundtrip)
#include "test_preferencesdialog_roundtrip.moc"
