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
#include <QtTest/QtTest>

#include <QCloseEvent>
#include <QDialog>
#include <QSettings>
#include <QShowEvent>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "ui/dialogs/dialogregistry.h"

using openswmmvis::ui::DialogRegistry;

namespace {

// The registry is installed as a filter on the individual dialog rather than
// on qApp: that drives the REAL eventFilter() path (calling it directly is
// not possible — it is protected) without leaking tracking between test
// functions. WindowActivate in particular is not synthesised by the
// offscreen platform from an ordinary show(), so it has to be sent by hand.
//
// The concrete QShowEvent/QCloseEvent subclasses matter: QWidget::event()
// downcasts by type, so handing it a bare QEvent with those type tags is
// formally undefined behaviour even though the subclasses add no members.

void sendShow(DialogRegistry *reg, QDialog *dlg)
{
    dlg->installEventFilter(reg);   // idempotent: Qt de-dupes filters
    QShowEvent e;
    QCoreApplication::sendEvent(dlg, &e);
}

void sendActivate(DialogRegistry *reg, QDialog *dlg)
{
    dlg->installEventFilter(reg);
    QEvent e(QEvent::WindowActivate);   // no dedicated subclass exists
    QCoreApplication::sendEvent(dlg, &e);
}

void sendClose(DialogRegistry *reg, QDialog *dlg)
{
    dlg->installEventFilter(reg);
    QCloseEvent e;
    QCoreApplication::sendEvent(dlg, &e);
}

}   // namespace

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

private:
    /// The registry is a process-wide singleton, so every test starts by
    /// closing any dialog the previous one left registered. Dialogs that
    /// were stack-allocated are already destroyed by now and read back as
    /// null QPointers, which openDialogs() skips — so this only has to
    /// handle live leftovers.
    void drain();

    QTemporaryDir mSettingsDir;   ///< keeps configuredStackingMode() off the
                                  ///< developer's real preferences
};

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

void TestDialogRegistry::initTestCase()
{
    // configuredStackingMode() falls back to a QSettings lookup when the
    // environment override is absent; point that at a throwaway directory so
    // the test can never read (or write) the real preference file.
    QVERIFY(mSettingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("dialogregistry-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       mSettingsDir.path());
}

void TestDialogRegistry::init()
{
    drain();
}

void TestDialogRegistry::tracksModelessTopLevelDialogs()
{
    auto *reg = DialogRegistry::instance();
    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("Plain"));
    sendShow(reg, &dlg);

    QCOMPARE(reg->openDialogs().size(), 1);
    QCOMPARE(reg->openDialogs().first().data(), &dlg);
}

void TestDialogRegistry::ignoresModalDialogs()
{
    auto *reg = DialogRegistry::instance();
    QDialog dlg;
    dlg.setModal(true);           // blocks interaction; not user-managed
    sendShow(reg, &dlg);

    QCOMPARE(reg->openDialogs().size(), 0);
}

void TestDialogRegistry::ignoresEmbeddedDialogs()
{
    auto *reg = DialogRegistry::instance();
    QWidget host;
    QDialog embedded(&host);
    embedded.setWindowFlags(Qt::Widget);   // laid out inside host, not a window
    QVERIFY(!embedded.isWindow());
    sendShow(reg, &embedded);

    QCOMPARE(reg->openDialogs().size(), 0);
}

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
    QCOMPARE(open.at(2).data(), &first);        // now the top of the stack
    QCOMPARE(open.at(0).data(), &second);       // relative order of the rest kept
    QCOMPARE(open.at(1).data(), &third);
}

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

void TestDialogRegistry::emitsChangeSignal()
{
    auto *reg = DialogRegistry::instance();
    QSignalSpy spy(reg, &DialogRegistry::openDialogsChanged);

    QDialog dlg;
    sendShow(reg, &dlg);
    QCOMPARE(spy.count(), 1);            // Window menu rebuilds on this

    sendClose(reg, &dlg);
    QCOMPARE(spy.count(), 2);
}

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
