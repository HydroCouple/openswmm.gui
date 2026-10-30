// UI redesign P1 — ActionRegistry behavior: adopt-in-place identity,
// shortcut application from catalog defaults, user-override persistence
// through QSettings, tag-driven enable/disable, conflict lookup, and a
// real QTest::keyClick firing an adopted action's shortcut (offscreen).

/**
 * @file      test_action_registry.cpp
 * @brief     Tests for action registry.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest/QtTest>

#include <QAction>
#include <QMainWindow>
#include <QSettings>
#include <QSignalSpy>

#include "ui/actioncatalog.h"
#include "ui/actionregistry.h"

using openswmmvis::ui::ActionRegistry;
using openswmmvis::ui::RequiresProject;

class TestActionRegistry : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void adoptInPlaceKeepsIdentity();
    void userOverridePersistsAndApplies();
    void clearedShortcutPersistsAsEmpty();
    void enableByTag();
    void conflictLookup();
    void shortcutFiresViaKeyClick();

private:
    void wipeShortcutSettings();

    QMainWindow *mWindow = nullptr;
    QAction *mNew = nullptr;         // adopted as file.new
    QAction *mAddJunction = nullptr; // adopted as model.addJunction (RequiresProject)
    QAction *mRun = nullptr;         // adopted as sim.run (Ctrl+R)
};

/**
 *
 * @brief Remove the complete SWMMVis::Shortcuts group from the current test application QSettings
 * namespace.
 * Does not reset the in-memory registry or check settings sync status.
 *
 */
void TestActionRegistry::wipeShortcutSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("SWMMVis::Shortcuts"));
    settings.remove(QString());
    settings.endGroup();
}

/**
 *
 * @brief Select the isolated openswmm-test/actionregistry-test settings namespace, clear shortcut
 * overrides and adopt three window-owned actions.
 * Requires exactly three catalog registrations; retains pointers for later cases
 * and changes process-global application identity.
 *
 */
void TestActionRegistry::initTestCase()
{
    // Isolate persistence from the real app (and from other tests).
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("actionregistry-test"));
    wipeShortcutSettings();

    mWindow = new QMainWindow;
    mNew = new QAction(QStringLiteral("New"), mWindow);
    mNew->setObjectName(QStringLiteral("actionNew"));
    mAddJunction = new QAction(QStringLiteral("Add Junction"), mWindow);
    mAddJunction->setObjectName(QStringLiteral("actionAddJunction"));
    mRun = new QAction(QStringLiteral("Execute"), mWindow);
    mRun->setObjectName(QStringLiteral("actionExecute"));

    const int adopted = ActionRegistry::instance()->registerFromCatalog(mWindow);
    QCOMPARE(adopted, 3);
}

/**
 *
 * @brief Clear test shortcut settings, destroy the window-owned actions and null the window
 * pointer.
 *
 */
void TestActionRegistry::cleanupTestCase()
{
    wipeShortcutSettings();
    delete mWindow;
    mWindow = nullptr;
}

/**
 *
 * @brief Require catalog registration to retain action pointers, apply New/Run defaults and
 * return null for an unknown ID.
 *
 */
void TestActionRegistry::adoptInPlaceKeepsIdentity()
{
    auto *registry = ActionRegistry::instance();
    // Same pointer — the registry adopts, it never re-creates.
    QCOMPARE(registry->action(QStringLiteral("file.new")), mNew);
    QCOMPARE(registry->action(QStringLiteral("model.addJunction")), mAddJunction);
    // Catalog default applied on adoption: file.new carries std:New.
    QVERIFY(mNew->shortcuts().contains(QKeySequence(QKeySequence::New)));
    // sim.run carries the literal Ctrl+R.
    QCOMPARE(mRun->shortcut(), QKeySequence(QStringLiteral("Ctrl+R")));
    // Unregistered ids resolve to nothing.
    QVERIFY(!registry->action(QStringLiteral("no.such.id")));
}

/**
 *
 * @brief Require a Ctrl+9 override to emit once, update the live action and persist portable
 * text, then reset to Ctrl+R.
 * Inspects current QSettings rather than launching a fresh process.
 *
 */
void TestActionRegistry::userOverridePersistsAndApplies()
{
    auto *registry = ActionRegistry::instance();
    const QString id = QStringLiteral("sim.run");
    const QKeySequence custom(QStringLiteral("Ctrl+9"));

    QSignalSpy spy(registry, &ActionRegistry::shortcutChanged);
    registry->setUserShortcut(id, custom);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(mRun->shortcuts(), QList<QKeySequence>{custom});
    QVERIFY(registry->hasUserShortcut(id));
    // Persisted portable text under the app group — what a fresh process
    // would read back.
    QSettings settings;
    settings.beginGroup(QStringLiteral("SWMMVis::Shortcuts"));
    QCOMPARE(settings.value(id).toString(), custom.toString(QKeySequence::PortableText));
    settings.endGroup();

    registry->resetShortcut(id);
    QVERIFY(!registry->hasUserShortcut(id));
    QCOMPARE(mRun->shortcut(), QKeySequence(QStringLiteral("Ctrl+R")));
}

/**
 *
 * @brief Require an explicit empty user binding to remain an override with no effective
 * shortcuts, then reset to the default.
 *
 */
void TestActionRegistry::clearedShortcutPersistsAsEmpty()
{
    auto *registry = ActionRegistry::instance();
    const QString id = QStringLiteral("sim.run");

    registry->setUserShortcut(id, QKeySequence());
    QVERIFY(registry->hasUserShortcut(id)); // stored as ""
    QVERIFY(mRun->shortcuts().isEmpty());   // deliberately unbound
    QVERIFY(registry->effectiveShortcuts(id).isEmpty());

    registry->resetShortcut(id);
    QCOMPARE(mRun->shortcut(), QKeySequence(QStringLiteral("Ctrl+R")));
}

/**
 *
 * @brief Require project-tag disabling/enabling to affect Add Junction while leaving the untagged
 * New action enabled.
 *
 */
void TestActionRegistry::enableByTag()
{
    auto *registry = ActionRegistry::instance();
    QVERIFY(mAddJunction->isEnabled());
    registry->setEnabledByTag(RequiresProject, false);
    QVERIFY(!mAddJunction->isEnabled());
    QVERIFY(mNew->isEnabled()); // untagged actions untouched
    registry->setEnabledByTag(RequiresProject, true);
    QVERIFY(mAddJunction->isEnabled());
}

/**
 *
 * @brief Require conflict lookup to identify Run at Ctrl+R, ignore an excluded holder and return
 * empty for an unused chord.
 * Assumes prior cases have restored the default Run binding.
 *
 */
void TestActionRegistry::conflictLookup()
{
    auto *registry = ActionRegistry::instance();
    // sim.run currently holds Ctrl+R (reset in the previous tests).
    const QKeySequence runSeq(QStringLiteral("Ctrl+R"));
    QCOMPARE(registry->conflictingActionId(runSeq, QStringLiteral("file.new")),
             QStringLiteral("sim.run"));
    // Excluding the holder itself reports no conflict.
    QVERIFY(registry->conflictingActionId(runSeq, QStringLiteral("sim.run")).isEmpty());
    QVERIFY(registry->conflictingActionId(QKeySequence(QStringLiteral("Ctrl+Alt+0"))).isEmpty());
}

/**
 *
 * @brief Require the adopted Run action to trigger once through a platform-correct Ctrl+R key
 * sequence on its active shown window.
 *
 */
void TestActionRegistry::shortcutFiresViaKeyClick()
{
    // The adopted action participates in normal shortcut dispatch once it
    // sits on a widget — exactly how the real menus/toolbars hold them.
    // WindowShortcut matching requires the window to be active, so show +
    // activate before synthesizing the chord (QTest::keySequence sends the
    // platform-correct modifier for "Ctrl" on every OS).
    mWindow->addAction(mRun);
    mWindow->show();
    mWindow->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(mWindow));

    QSignalSpy spy(mRun, &QAction::triggered);
    QTest::keySequence(mWindow, QKeySequence(QStringLiteral("Ctrl+R")));
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TestActionRegistry)
#include "test_action_registry.moc"
