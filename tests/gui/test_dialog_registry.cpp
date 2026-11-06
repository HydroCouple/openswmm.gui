// Dialog window management — the app-wide register of open modeless dialogs
// that backs the Window menu's recovery entries and the pure-Qt stacking
// mode (see include/ui/dialogs/dialogregistry.h).
//
// The contract under test:
//   - modeless top-level dialogs are tracked; modal and embedded ones are not
//   - order is most-recently-USED (activation moves an entry to the end), so
//     re-raising restores the z-order the user arranged by clicking rather
//     than shuffling it back to open order
//   - hiding/closing drops the entry; destruction can never leave a dangling
//     pointer in the list
//   - the stacking mode honours the OPENSWMM_DIALOG_STACKING override
//
// Runs on the offscreen QPA platform, so this exercises bookkeeping, not
// real window-server stacking. Actual z-order behaviour on a multi-monitor
// Mac is covered by the manual checklist in the verification document.

/**
 * @file      test_dialog_registry.cpp
 * @brief     Tests for dialog registry.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest/QtTest>

#include <QCloseEvent>
#include <QDialog>
#include <QSettings>
#include <QShowEvent>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "ui/dialogs/dialogregistry.h"

using openswmmvis::ui::DialogRegistry;

namespace
{

class RaiseRecorder : public QObject
{
public:
    QList<QObject *> raised;

protected:
    /**
     *
     * @brief Record observed ZOrderChange events without consuming event delivery.
     * @param watched Borrowed event target appended by raw pointer for later identity
     * comparison; this recorder does not extend its lifetime.
     * @param event Borrowed non-null Qt event, valid only during this call.
     * @return Always false so normal event processing continues; other event types
     * leave the recorded list unchanged.
     *
     */
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::ZOrderChange)
            raised.append(watched);
        return false;
    }
};

// The registry is installed as a filter on the individual dialog rather than
// on qApp: that drives the REAL eventFilter() path (calling it directly is
// not possible — it is protected) without leaking tracking between test
// functions. WindowActivate in particular is not synthesised by the
// offscreen platform from an ordinary show(), so it has to be sent by hand.
//
// The concrete QShowEvent/QCloseEvent subclasses matter: QWidget::event()
// downcasts by type, so handing it a bare QEvent with those type tags is
// formally undefined behaviour even though the subclasses add no members.

/**
 *
 * @brief Install the borrowed registry filter and synchronously send a concrete show event.
 * @param reg Borrowed non-null registry, retained by Qt as an event filter until
 * either object is destroyed or the filter is removed.
 * @param dlg Borrowed non-null dialog receiving the event.
 * Qt deduplicates repeated filter installation; this exercises registry bookkeeping
 * without requiring an actual platform-generated show or transferring ownership.
 *
 */
void sendShow(DialogRegistry *reg, QDialog *dlg)
{
    dlg->installEventFilter(reg); // idempotent: Qt de-dupes filters
    QShowEvent e;
    QCoreApplication::sendEvent(dlg, &e);
}

/**
 *
 * @brief Install the registry filter and synchronously send WindowActivate.
 * @param reg Borrowed non-null registry, retained as a Qt filter.
 * @param dlg Borrowed non-null dialog target.
 * Uses plain QEvent because this type has no dedicated subclass; synthetic
 * activation exercises MRU bookkeeping without proving native window-server order.
 *
 */
void sendActivate(DialogRegistry *reg, QDialog *dlg)
{
    dlg->installEventFilter(reg);
    QEvent e(QEvent::WindowActivate); // no dedicated subclass exists
    QCoreApplication::sendEvent(dlg, &e);
}

/**
 *
 * @brief Install the registry filter and synchronously send a concrete close event.
 * @param reg Borrowed non-null registry, retained as a Qt filter.
 * @param dlg Borrowed non-null target dialog, ownership unchanged.
 * Exercises the close-event path rather than calling close() or deleting the dialog.
 *
 */
void sendClose(DialogRegistry *reg, QDialog *dlg)
{
    dlg->installEventFilter(reg);
    QCloseEvent e;
    QCoreApplication::sendEvent(dlg, &e);
}

} // namespace

class TestDialogRegistry : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void tracksModelessTopLevelDialogs();
    void ignoresModalDialogs();
    void ignoresEmbeddedDialogs();
    void newestShownEndsUpLast();
    void activationPromotesToMostRecent();
    void closeRemovesEntry();
    void destroyedDialogNeverDangles();
    void emitsChangeSignal();
    void environmentOverridesStackingMode();
    void siblingDialogsStayAboveMainWindow_data();
    void siblingDialogsStayAboveMainWindow();

private:
    /// The registry is a process-wide singleton, so every test starts by
    /// closing any dialog the previous one left registered. Dialogs that
    /// were stack-allocated are already destroyed by now and read back as
    /// null QPointers, which openDialogs() skips — so this only has to
    /// handle live leftovers.
    void drain();

    QTemporaryDir mSettingsDir; ///< keeps configuredStackingMode() off the
                                ///< developer's real preferences
};

/**
 *
 * @brief Send synthetic close events to every surviving singleton entry and require an empty
 * register.
 * Null guarded pointers are skipped; helper assertion failures return locally.
 * Does not delete surviving dialogs or reset the configured stacking mode.
 *
 */
void TestDialogRegistry::drain()
{
    auto *reg = DialogRegistry::instance();
    const QList<QPointer<QDialog>> open = reg->openDialogs();
    for (const QPointer<QDialog> &d : open) {
        if (d)
            sendClose(reg, d.data());
    }
    QCOMPARE(reg->openDialogs().size(), 0);
}

/**
 *
 * @brief Redirect registry INI settings to a valid temporary directory and isolated
 * dialogregistry-test application identity.
 * Changes process-global identity/default format/path; no corresponding restoration
 * is performed by this suite.
 *
 */
void TestDialogRegistry::initTestCase()
{
    // configuredStackingMode() falls back to a QSettings lookup when the
    // environment override is absent; point that at a throwaway directory so
    // the test can never read (or write) the real preference file.
    QVERIFY(mSettingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("dialogregistry-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, mSettingsDir.path());
}

/**
 *
 * @brief Drain singleton dialog entries before each case to prevent stale tracking from an
 * earlier case.
 *
 */
void TestDialogRegistry::init()
{
    drain();
}

/**
 *
 * @brief Register QtRaiseOnActivate and NativeChildWindow stacking modes for sibling-dialog
 * bookkeeping.
 *
 */
void TestDialogRegistry::siblingDialogsStayAboveMainWindow_data()
{
    QTest::addColumn<DialogRegistry::StackingMode>("mode");
    QTest::newRow("qt") << DialogRegistry::StackingMode::QtRaiseOnActivate;
    QTest::newRow("native") << DialogRegistry::StackingMode::NativeChildWindow;
}

/**
 *
 * @brief Require sibling restacks to raise the most recent dialog last, preserve both visible
 * siblings and defer to an active modal prompt.
 * Consumes mode and records Qt ZOrderChange events; these assertions do not prove
 * actual native multi-monitor stacking. Closing one sibling must leave the other visible.
 *
 */
void TestDialogRegistry::siblingDialogsStayAboveMainWindow()
{
    QFETCH(DialogRegistry::StackingMode, mode);
    auto *reg = DialogRegistry::instance();
    reg->setStackingMode(mode);
    QWidget mainWindow;
    QDialog comparison(&mainWindow), rainfall(&mainWindow);
    RaiseRecorder recorder;
    comparison.installEventFilter(&recorder);
    rainfall.installEventFilter(&recorder);
    comparison.installEventFilter(reg);
    rainfall.installEventFilter(reg);
    mainWindow.show();
    comparison.show();
    QCoreApplication::processEvents();

    // A new plot must bring the existing plot back above the main window,
    // with the new plot last. Visibility alone cannot detect a buried window.
    mainWindow.raise();
    recorder.raised.clear();
    rainfall.show();
    QCoreApplication::processEvents();
    QVERIFY(comparison.isVisible());
    QVERIFY(rainfall.isVisible());
    QVERIFY(recorder.raised.contains(&comparison));
    QCOMPARE(recorder.raised.last(), &rainfall);

    // Switching between plots keeps both in front, in the user's MRU order.
    recorder.raised.clear();
    sendActivate(reg, &comparison);
    QCoreApplication::processEvents();
    QVERIFY(recorder.raised.contains(&rainfall));
    QCOMPARE(recorder.raised.last(), &comparison);

    // A modal prompt opened before the queued restack runs stays on top.
    sendActivate(reg, &comparison);
    QDialog prompt(&comparison);
    prompt.setModal(true);
    prompt.show();
    recorder.raised.clear();
    QCoreApplication::processEvents();
    QCOMPARE(QApplication::activeModalWidget(), &prompt);
    QVERIFY(recorder.raised.isEmpty());
    prompt.close();

    // Explicitly closing one plot must leave the other alone.
    rainfall.close();
    QCoreApplication::processEvents();
    QVERIFY(comparison.isVisible());
    QVERIFY(!rainfall.isVisible());
}

/**
 *
 * @brief Require a synthetic modeless top-level show to register exactly its live pointer.
 *
 */
void TestDialogRegistry::tracksModelessTopLevelDialogs()
{
    auto *reg = DialogRegistry::instance();
    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("Plain"));
    sendShow(reg, &dlg);

    QCOMPARE(reg->openDialogs().size(), 1);
    QCOMPARE(reg->openDialogs().first().data(), &dlg);
}

/**
 *
 * @brief Require a modal dialog show to leave the modeless registry empty.
 *
 */
void TestDialogRegistry::ignoresModalDialogs()
{
    auto *reg = DialogRegistry::instance();
    QDialog dlg;
    dlg.setModal(true); // blocks interaction; not user-managed
    sendShow(reg, &dlg);

    QCOMPARE(reg->openDialogs().size(), 0);
}

/**
 *
 * @brief Require a QWidget-flag embedded dialog to be excluded from top-level tracking.
 *
 */
void TestDialogRegistry::ignoresEmbeddedDialogs()
{
    auto *reg = DialogRegistry::instance();
    QWidget host;
    QDialog embedded(&host);
    embedded.setWindowFlags(Qt::Widget); // laid out inside host, not a window
    QVERIFY(!embedded.isWindow());
    sendShow(reg, &embedded);

    QCOMPARE(reg->openDialogs().size(), 0);
}

/**
 *
 * @brief Require shown-dialog registration order to retain the newest entry last for subsequent
 * raising.
 *
 */
void TestDialogRegistry::newestShownEndsUpLast()
{
    auto *reg = DialogRegistry::instance();
    QDialog first, second, third;
    sendShow(reg, &first);
    sendShow(reg, &second);
    sendShow(reg, &third);

    // Least-recently-used first: raiseAllInOrder() walks this order, so the
    // last entry is the one that finishes on top.
    const QList<QPointer<QDialog>> open = reg->openDialogs();
    QCOMPARE(open.size(), 3);
    QCOMPARE(open.at(0).data(), &first);
    QCOMPARE(open.at(1).data(), &second);
    QCOMPARE(open.at(2).data(), &third);
}

/**
 *
 * @brief Require activating the oldest dialog to move it last while retaining relative order of
 * the other entries.
 *
 */
void TestDialogRegistry::activationPromotesToMostRecent()
{
    auto *reg = DialogRegistry::instance();
    QDialog first, second, third;
    sendShow(reg, &first);
    sendShow(reg, &second);
    sendShow(reg, &third);

    // The user clicks the OLDEST dialog to bring it forward. Without MRU
    // tracking, the next app re-activation would re-raise in open order and
    // shove it back under the other two.
    sendActivate(reg, &first);

    const QList<QPointer<QDialog>> open = reg->openDialogs();
    QCOMPARE(open.size(), 3);
    QCOMPARE(open.at(2).data(), &first);  // now the top of the stack
    QCOMPARE(open.at(0).data(), &second); // relative order of the rest kept
    QCOMPARE(open.at(1).data(), &third);
}

/**
 *
 * @brief Require a synthetic close to remove only its dialog and retain the other registered
 * pointer.
 *
 */
void TestDialogRegistry::closeRemovesEntry()
{
    auto *reg = DialogRegistry::instance();
    QDialog keep, drop;
    sendShow(reg, &keep);
    sendShow(reg, &drop);
    QCOMPARE(reg->openDialogs().size(), 2);

    sendClose(reg, &drop);
    const QList<QPointer<QDialog>> open = reg->openDialogs();
    QCOMPARE(open.size(), 1);
    QCOMPARE(open.first().data(), &keep);
}

/**
 *
 * @brief Require direct deletion of a registered dialog to prune its guarded pointer while
 * preserving and safely raising the survivor.
 *
 */
void TestDialogRegistry::destroyedDialogNeverDangles()
{
    auto *reg = DialogRegistry::instance();
    QDialog survivor;
    sendShow(reg, &survivor);

    // WA_DeleteOnClose teardown destroys the dialog without the registry
    // seeing a usable Close (the QDialog sub-object is gone by then). The
    // QPointer entry must read back null and be pruned, not dangle.
    {
        auto *doomed = new QDialog;
        sendShow(reg, doomed);
        QCOMPARE(reg->openDialogs().size(), 2);
        delete doomed;
    }

    const QList<QPointer<QDialog>> open = reg->openDialogs();
    QCOMPARE(open.size(), 1);
    QCOMPARE(open.first().data(), &survivor);

    // And raising must not touch the freed entry.
    reg->raiseAllInOrder();
}

/**
 *
 * @brief Require one openDialogsChanged signal for synthetic show and one additional signal for
 * close.
 *
 */
void TestDialogRegistry::emitsChangeSignal()
{
    auto *reg = DialogRegistry::instance();
    QSignalSpy spy(reg, &DialogRegistry::openDialogsChanged);

    QDialog dlg;
    sendShow(reg, &dlg);
    QCOMPARE(spy.count(), 1); // Window menu rebuilds on this

    sendClose(reg, &dlg);
    QCOMPARE(spy.count(), 2);
}

/**
 *
 * @brief Require qt/native environment values to select their matching configured modes.
 * Temporarily overwrites OPENSWMM_DIALOG_STACKING then unsets it; an originally
 * present environment value is not restored.
 *
 */
void TestDialogRegistry::environmentOverridesStackingMode()
{
    // The override exists so a tester can flip stacking strategies for one
    // run without writing to (and later having to clean up) real preferences.
    qputenv("OPENSWMM_DIALOG_STACKING", "qt");
    QCOMPARE(DialogRegistry::configuredStackingMode(),
             DialogRegistry::StackingMode::QtRaiseOnActivate);

    qputenv("OPENSWMM_DIALOG_STACKING", "native");
    QCOMPARE(DialogRegistry::configuredStackingMode(),
             DialogRegistry::StackingMode::NativeChildWindow);

    qunsetenv("OPENSWMM_DIALOG_STACKING");
}

QTEST_MAIN(TestDialogRegistry)
#include "test_dialog_registry.moc"
