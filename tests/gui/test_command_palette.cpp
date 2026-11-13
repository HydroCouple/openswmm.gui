// UI redesign P7 — command palette: fuzzy-score ranking invariants,
// model filtering over registry-registered actions, end-to-end
// type-filter-Enter triggering (offscreen keyClicks), Esc dismissal,
// and disabled actions not triggerable.

/**
 * @file      test_command_palette.cpp
 * @brief     Tests for command palette.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest/QtTest>

#include <QAccessible>
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMainWindow>
#include <QSignalSpy>

#include "ui/actionregistry.h"
#include "ui/widgets/commandpalette.h"
#include "ui/widgets/commandpalettemodel.h"

using openswmmvis::ui::ActionRegistry;
using openswmmvis::ui::CommandPalette;
using openswmmvis::ui::CommandPaletteModel;
using openswmmvis::ui::fuzzyScore;

class TestCommandPalette : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void fuzzyScoreRanking();
    void modelFiltersAndSorts();
    void enterTriggersFilteredAction();
    void escapeCloses();
    void disabledActionNotTriggerable();
    void disabledReasonIsShownAndAnnounced();

private:
    QMainWindow *mWindow = nullptr;
    QAction *mRun = nullptr;    // "Execute" (sim.run)
    QAction *mSaveAs = nullptr; // "Save As…" (file.saveAs), disabled
};

/**
 *
 * @brief Select the commandpalette-test application identity and register window-owned Run plus
 * disabled Save As actions.
 * Retains the parentless main window for subsequent cases; this fixture has no
 * explicit cleanup slot. No shortcut-settings group is cleared here.
 *
 */
void TestCommandPalette::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("commandpalette-test"));

    mWindow = new QMainWindow;
    mRun = new QAction(QStringLiteral("&Execute"), mWindow);
    mRun->setObjectName(QStringLiteral("actionExecute"));
    mSaveAs = new QAction(QStringLiteral("Save &As…"), mWindow);
    mSaveAs->setObjectName(QStringLiteral("actionSaveAs"));
    mSaveAs->setEnabled(false);
    ActionRegistry::instance()->registerFromCatalog(mWindow);
    QVERIFY(ActionRegistry::instance()->action(QStringLiteral("sim.run")) == mRun);
}

/**
 *
 * @brief Require prefix scoring above word-start above scattered matching, case-insensitive
 * matches, -1 nonmatches and neutral empty-pattern score.
 *
 */
void TestCommandPalette::fuzzyScoreRanking()
{
    // Prefix beats word-boundary beats scattered subsequence.
    const int prefix = fuzzyScore(QStringLiteral("exe"), QStringLiteral("Execute"));
    const int wordStart = fuzzyScore(QStringLiteral("exe"), QStringLiteral("Run Executable"));
    const int scattered = fuzzyScore(QStringLiteral("exe"), QStringLiteral("Trend Explorer Zone"));
    QVERIFY(prefix > wordStart);
    QVERIFY(wordStart > scattered);

    // Case-insensitive; non-matches are rejected.
    QVERIFY(fuzzyScore(QStringLiteral("EXE"), QStringLiteral("execute")) > 0);
    QCOMPARE(fuzzyScore(QStringLiteral("xyz"), QStringLiteral("Execute")), -1);
    // Empty pattern matches everything neutrally.
    QCOMPARE(fuzzyScore(QString(), QStringLiteral("anything")), 0);
}

/**
 *
 * @brief Require registry-backed model reload to expose both actions, rank Execute first for its
 * exact filter and return zero rows for a nonmatch.
 *
 */
void TestCommandPalette::modelFiltersAndSorts()
{
    CommandPaletteModel model;
    model.reload();
    QVERIFY(model.rowCount() >= 2); // both registered actions, ampersands stripped

    model.setFilterPattern(QStringLiteral("execute"));
    QVERIFY(model.rowCount() >= 1);
    QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("Execute"));
    QCOMPARE(model.actionAt(model.index(0, 0)), mRun);

    model.setFilterPattern(QStringLiteral("no such command zz"));
    QCOMPARE(model.rowCount(), 0);
}

/**
 *
 * @brief Require typing Execute then Enter to trigger Run once and hide the palette.
 *
 */
void TestCommandPalette::enterTriggersFilteredAction()
{
    CommandPalette palette(mWindow);
    palette.popup();
    QVERIFY(QTest::qWaitForWindowExposed(&palette));

    QSignalSpy spy(mRun, &QAction::triggered);
    QTest::keyClicks(palette.filterEdit(), QStringLiteral("execute"));
    QTest::keyClick(palette.filterEdit(), Qt::Key_Return);
    QCOMPARE(spy.count(), 1);
    QVERIFY(palette.isHidden());
}

/**
 *
 * @brief Require Escape in the palette filter to hide the exposed palette.
 *
 */
void TestCommandPalette::escapeCloses()
{
    CommandPalette palette(mWindow);
    palette.popup();
    QVERIFY(QTest::qWaitForWindowExposed(&palette));
    QTest::keyClick(palette.filterEdit(), Qt::Key_Escape);
    QVERIFY(palette.isHidden());
}

/**
 *
 * @brief Require Enter on filtered disabled Save As to emit no action trigger and keep the
 * palette available.
 *
 */
void TestCommandPalette::disabledActionNotTriggerable()
{
    CommandPalette palette(mWindow);
    palette.popup();
    QVERIFY(QTest::qWaitForWindowExposed(&palette));

    QSignalSpy spy(mSaveAs, &QAction::triggered);
    QTest::keyClicks(palette.filterEdit(), QStringLiteral("save as"));
    QTest::keyClick(palette.filterEdit(), Qt::Key_Return);
    QCOMPARE(spy.count(), 0);     // grayed row, Enter refuses
    QVERIFY(!palette.isHidden()); // palette stays up for another pick
}

namespace
{
QStringList g_announcements;

/*! Accessibility update handler recording what would be announced to a screen reader. */
void recordAnnouncement(QAccessibleEvent *event)
{
    if (event->type() == QAccessible::Announcement)
        g_announcements.append(static_cast<QAccessibleAnnouncementEvent *>(event)->message());
}
} // namespace

/*! A-15: the palette says why a command is unavailable. The highlighted disabled command's
 *  reason shows under the list, its tooltip carries it, and Enter on it keeps the palette open
 *  and announces the reason instead of doing nothing. */
void TestCommandPalette::disabledReasonIsShownAndAnnounced()
{
    auto *registry = ActionRegistry::instance();
    const QString reason = QStringLiteral("Open a model first.");
    registry->setDisabledReason(QStringLiteral("file.saveAs"), reason);

    CommandPaletteModel model;
    model.reload();
    model.setFilterPattern(QStringLiteral("save as"));
    QVERIFY(model.rowCount() >= 1);
    QCOMPARE(model.actionAt(model.index(0, 0)), mSaveAs);
    QCOMPARE(model.data(model.index(0, 0), CommandPaletteModel::DisabledReasonRole).toString(),
             reason);
    QCOMPARE(model.data(model.index(0, 0), Qt::ToolTipRole).toString(), reason);
    model.setFilterPattern(QStringLiteral("execute"));
    QCOMPARE(model.actionAt(model.index(0, 0)), mRun);
    QVERIFY(model.data(model.index(0, 0), CommandPaletteModel::DisabledReasonRole)
                .toString()
                .isEmpty());

    CommandPalette palette(mWindow);
    palette.popup();
    QVERIFY(QTest::qWaitForWindowExposed(&palette));
    auto *status = palette.findChild<QLabel *>(QStringLiteral("commandPaletteStatus"));
    QVERIFY(status);
    QVERIFY(!status->accessibleName().isEmpty());

    // Highlighting the disabled command says why, before anything is pressed.
    QTest::keyClicks(palette.filterEdit(), QStringLiteral("save as"));
    QVERIFY(status->isVisible());
    QVERIFY2(status->text().contains(reason), qPrintable(status->text()));

    // Enter on it is refused out loud.
    g_announcements.clear();
    const auto previous = QAccessible::installUpdateHandler(recordAnnouncement);
    QSignalSpy spy(mSaveAs, &QAction::triggered);
    QTest::keyClick(palette.filterEdit(), Qt::Key_Return);
    QAccessible::installUpdateHandler(previous);
    QCOMPARE(spy.count(), 0);
    QVERIFY(!palette.isHidden());
    QCOMPARE(g_announcements.size(), 1);
    QVERIFY2(g_announcements.first().contains(reason), qPrintable(g_announcements.first()));

    // An available command clears the line.
    palette.filterEdit()->clear();
    QTest::keyClicks(palette.filterEdit(), QStringLiteral("execute"));
    QVERIFY(status->isHidden());

    // A disabled command with no recorded reason still says it is unavailable.
    registry->setDisabledReason(QStringLiteral("file.saveAs"), QString());
    palette.filterEdit()->clear();
    QTest::keyClicks(palette.filterEdit(), QStringLiteral("save as"));
    QVERIFY(status->isVisible());
    QVERIFY(!status->text().isEmpty());
}

QTEST_MAIN(TestCommandPalette)
#include "test_command_palette.moc"
