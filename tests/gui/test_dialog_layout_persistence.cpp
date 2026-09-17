// UI redesign iteration 2 (D1) — dialog layout persistence: the extended
// helper vocabulary (geometry, named splitters, header state, tab index,
// nav-page index, checkable view toggles — all opt-in by objectName,
// qt_* internals ignored, ints bounds-checked, stale header blobs
// rejected) and the app-wide DialogLayoutWatcher (restore on first Show
// only, save on Hide/Close, "noLayoutPersistence" opt-out).
//
// QSettings are fully isolated into a per-run QTemporaryDir (IniFormat)
// so nothing bleeds to or from the developer's real preferences.
#include <QtTest/QtTest>

#include <QAction>
#include <QDialog>
#include <QLabel>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include "ui/dialogs/dialoglayoutpersistence.h"
#include "ui/dialogs/dialoglayoutwatcher.h"

using openswmmvis::ui::clampToVisibleScreen;
using openswmmvis::ui::DialogLayoutWatcher;
using openswmmvis::ui::kNoLayoutPersistenceProp;
using openswmmvis::ui::restoreDialogLayout;
using openswmmvis::ui::saveDialogLayout;

namespace {

// A dialog with one of every persistable state kind.
struct Fixture {
    QDialog dialog;
    QSplitter *split = nullptr;         // named "main"
    QSplitter *anonSplit = nullptr;     // unnamed — must never persist
    QTableWidget *table = nullptr;      // named "grid"
    QTabWidget *tabs = nullptr;         // named "tabs"
    QStackedWidget *pages = nullptr;    // named "pages"
    QAction *toggle = nullptr;          // named "showDetail", checkable

    explicit Fixture(const QString &dialogName)
    {
        dialog.setObjectName(dialogName);
        auto *layout = new QVBoxLayout(&dialog);

        split = new QSplitter(&dialog);
        split->setObjectName(QStringLiteral("main"));
        split->addWidget(new QLabel(QStringLiteral("left")));
        split->addWidget(new QLabel(QStringLiteral("right")));
        layout->addWidget(split);

        anonSplit = new QSplitter(&dialog);
        anonSplit->addWidget(new QLabel(QStringLiteral("a")));
        anonSplit->addWidget(new QLabel(QStringLiteral("b")));
        layout->addWidget(anonSplit);

        table = new QTableWidget(3, 3, &dialog);
        table->setObjectName(QStringLiteral("grid"));
        layout->addWidget(table);

        tabs = new QTabWidget(&dialog);
        tabs->setObjectName(QStringLiteral("tabs"));
        tabs->addTab(new QLabel(QStringLiteral("one")), QStringLiteral("One"));
        tabs->addTab(new QLabel(QStringLiteral("two")), QStringLiteral("Two"));
        tabs->addTab(new QLabel(QStringLiteral("three")), QStringLiteral("Three"));
        layout->addWidget(tabs);

        pages = new QStackedWidget(&dialog);
        pages->setObjectName(QStringLiteral("pages"));
        pages->addWidget(new QLabel(QStringLiteral("p0")));
        pages->addWidget(new QLabel(QStringLiteral("p1")));
        layout->addWidget(pages);

        toggle = new QAction(QStringLiteral("Show Detail"), &dialog);
        toggle->setObjectName(QStringLiteral("showDetail"));
        toggle->setCheckable(true);
        dialog.addAction(toggle);
    }
};

}   // namespace

class TestDialogLayoutPersistence : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void geometryRoundTrips();
    void namedSplitterOptInOnly();
    void headerStateRoundTrips();
    void tabAndPageIndicesBoundsChecked();
    void toggleRestoreFiresToggled();
    void emptyObjectNameIsNoOp();
    void offscreenRectGetsClamped();
    void clampKeepsUsableRectUntouched();
    void clampShrinksRectTooBigForItsScreen();
    void clampKeepsTitleBarReachable();
    void clampRescuesBarelyOverlappingRect();
    void clampLeavesRectUsableOnSomeScreen();
    void watcherPersistsAcrossInstances();
    void watcherRespectsOptOutProperty();
    void watcherRestoresOncePerInstance();

private:
    QTemporaryDir mSettingsDir;   // member: path must outlive every test
};

void TestDialogLayoutPersistence::initTestCase()
{
    QVERIFY(mSettingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("dialoglayout-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       mSettingsDir.path());
}

void TestDialogLayoutPersistence::init()
{
    QSettings settings;
    settings.clear();
}

void TestDialogLayoutPersistence::geometryRoundTrips()
{
    // Rect chosen inside the offscreen screen's available geometry so the
    // clamp leaves it untouched and exact equality holds (no WM frames
    // offscreen).
    const QRect rect(120, 110, 420, 330);
    {
        Fixture f(QStringLiteral("GeomDialog"));
        f.dialog.setGeometry(rect);
        saveDialogLayout(&f.dialog);
    }
    Fixture fresh(QStringLiteral("GeomDialog"));
    QVERIFY(restoreDialogLayout(&fresh.dialog));
    QCOMPARE(fresh.dialog.geometry(), rect);
}

void TestDialogLayoutPersistence::namedSplitterOptInOnly()
{
    {
        Fixture f(QStringLiteral("SplitDialog"));
        f.dialog.resize(500, 400);
        f.split->setSizes({100, 300});
        f.anonSplit->setSizes({350, 50});
        saveDialogLayout(&f.dialog);
    }
    QSettings settings;
    QVERIFY(settings.contains(
        QStringLiteral("Dialogs/SplitDialog/splitter/main")));
    // The unnamed splitter must have left no trace.
    settings.beginGroup(QStringLiteral("Dialogs/SplitDialog/splitter"));
    QCOMPARE(settings.childKeys().size(), 1);
    settings.endGroup();

    Fixture fresh(QStringLiteral("SplitDialog"));
    fresh.dialog.resize(500, 400);
    QVERIFY(restoreDialogLayout(&fresh.dialog));
    const QList<int> sizes = fresh.split->sizes();
    QVERIFY2(sizes.first() < sizes.last(),
             qPrintable(QStringLiteral("expected restored 100/300 split, got "
                                       "%1/%2").arg(sizes.first()).arg(sizes.last())));
}

void TestDialogLayoutPersistence::headerStateRoundTrips()
{
    {
        Fixture f(QStringLiteral("HeaderDialog"));
        f.table->setColumnWidth(0, 137);
        f.table->setColumnWidth(1, 61);
        saveDialogLayout(&f.dialog);
    }
    Fixture fresh(QStringLiteral("HeaderDialog"));
    QVERIFY(restoreDialogLayout(&fresh.dialog));
    QCOMPARE(fresh.table->columnWidth(0), 137);
    QCOMPARE(fresh.table->columnWidth(1), 61);
}

void TestDialogLayoutPersistence::tabAndPageIndicesBoundsChecked()
{
    {
        Fixture f(QStringLiteral("TabDialog"));
        f.tabs->setCurrentIndex(2);
        f.pages->setCurrentIndex(1);
        saveDialogLayout(&f.dialog);
    }
    // Valid indices restore.
    {
        Fixture fresh(QStringLiteral("TabDialog"));
        QVERIFY(restoreDialogLayout(&fresh.dialog));
        QCOMPARE(fresh.tabs->currentIndex(), 2);
        QCOMPARE(fresh.pages->currentIndex(), 1);
    }
    // A stored index beyond the widget's count degrades to the default
    // instead of misfiring (dialog reworked since the save).
    {
        QSettings settings;
        settings.setValue(QStringLiteral("Dialogs/TabDialog/tab/tabs"), 7);
        Fixture fresh(QStringLiteral("TabDialog"));
        restoreDialogLayout(&fresh.dialog);
        QCOMPARE(fresh.tabs->currentIndex(), 0);
    }
}

void TestDialogLayoutPersistence::toggleRestoreFiresToggled()
{
    {
        Fixture f(QStringLiteral("ToggleDialog"));
        f.toggle->setChecked(true);
        saveDialogLayout(&f.dialog);
    }
    Fixture fresh(QStringLiteral("ToggleDialog"));
    QSignalSpy spy(fresh.toggle, &QAction::toggled);
    QVERIFY(restoreDialogLayout(&fresh.dialog));
    QVERIFY(fresh.toggle->isChecked());
    QCOMPARE(spy.count(), 1);   // dependent views get their update
}

void TestDialogLayoutPersistence::emptyObjectNameIsNoOp()
{
    Fixture f{QString()};
    f.dialog.setGeometry(QRect(100, 100, 300, 200));
    saveDialogLayout(&f.dialog);
    QVERIFY(!restoreDialogLayout(&f.dialog));
    QSettings settings;
    QVERIFY(settings.allKeys().isEmpty());
}

void TestDialogLayoutPersistence::offscreenRectGetsClamped()
{
    QSettings settings;
    settings.setValue(QStringLiteral("Dialogs/ClampDialog/geometry"),
                      QRect(5000, 5000, 400, 300));
    Fixture fresh(QStringLiteral("ClampDialog"));
    QVERIFY(restoreDialogLayout(&fresh.dialog));
    const QRect got = fresh.dialog.geometry();
    bool onScreen = false;
    const auto screens = QGuiApplication::screens();
    for (const QScreen *screen : screens)
        onScreen = onScreen || screen->availableGeometry().contains(got.center());
    QVERIFY2(onScreen, qPrintable(QStringLiteral("clamped rect still off-screen: "
                                                 "%1,%2").arg(got.x()).arg(got.y())));
}

// ── clampToVisibleScreen() — the rules the old center-point test missed ────
//
// The previous clamp accepted any rect whose CENTER landed on a connected
// screen, which let three unusable geometries through: oversized (title bar
// and buttons off the edge), title-bar-above-the-menu-bar (nothing left to
// drag), and barely-overlapping (a few pixels of body visible). These pin
// each rule down against the current screen layout rather than hard-coded
// numbers, so they hold on any test machine.

void TestDialogLayoutPersistence::clampKeepsUsableRectUntouched()
{
    const QScreen *primary = QGuiApplication::primaryScreen();
    QVERIFY(primary);
    const QRect avail = primary->availableGeometry();

    // Comfortably inside, well clear of every edge including the title strip.
    const QRect fine(avail.x() + 120, avail.y() + 120,
                     qMin(400, avail.width()  / 2),
                     qMin(300, avail.height() / 2));
    QCOMPARE(clampToVisibleScreen(fine, 28), fine);
}

void TestDialogLayoutPersistence::clampShrinksRectTooBigForItsScreen()
{
    const QScreen *primary = QGuiApplication::primaryScreen();
    QVERIFY(primary);
    const QRect avail = primary->availableGeometry();

    // Centered on a valid screen — the old clamp passed this straight
    // through — but twice as wide and tall as the screen can show.
    QRect huge(0, 0, avail.width() * 2, avail.height() * 2);
    huge.moveCenter(avail.center());

    const QRect got = clampToVisibleScreen(huge, 28);
    QVERIFY2(got.width()  <= avail.width(),
             "clamped rect is still wider than its screen");
    QVERIFY2(got.height() <= avail.height(),
             "clamped rect is still taller than its screen");
    QVERIFY2(avail.contains(got.adjusted(0, -28, 0, 0)),
             "clamped rect (incl. title bar) is not fully on screen");
}

void TestDialogLayoutPersistence::clampKeepsTitleBarReachable()
{
    const QScreen *primary = QGuiApplication::primaryScreen();
    QVERIFY(primary);
    const QRect avail = primary->availableGeometry();

    // Client rect starts 4 px below the top of the available area, so the
    // 28 px title bar — the window's only drag handle — sits above it and
    // under the menu bar. Body is plainly visible, so the old center test
    // accepted it and the window could never be moved again.
    const QRect tucked(avail.x() + 100, avail.y() + 4, 420, 300);
    const QRect got = clampToVisibleScreen(tucked, 28);

    QVERIFY2(got.top() - 28 >= avail.top(),
             qPrintable(QStringLiteral("title bar still above the available "
                                       "area: top=%1 avail.top=%2")
                            .arg(got.top()).arg(avail.top())));
}

void TestDialogLayoutPersistence::clampRescuesBarelyOverlappingRect()
{
    const QScreen *primary = QGuiApplication::primaryScreen();
    QVERIFY(primary);
    const QRect avail = primary->availableGeometry();

    // Only ~10 px of the window pokes onto the screen — its center is off,
    // so even the old clamp would move it, but this asserts the new minimum
    // visible-area contract explicitly.
    const QRect sliver(avail.right() - 10, avail.y() + 200, 500, 360);
    const QRect got = clampToVisibleScreen(sliver, 28);
    const QRect visible = avail.intersected(got);

    QVERIFY2(visible.width()  >= qMin(got.width(),  160),
             "too little of the window is horizontally reachable");
    QVERIFY2(visible.height() >= qMin(got.height(), 60),
             "too little of the window is vertically reachable");
}

void TestDialogLayoutPersistence::clampLeavesRectUsableOnSomeScreen()
{
    // General postcondition: whatever screen the clamp settles on, the result
    // must be genuinely usable there.
    //
    // NOTE this does NOT prove the largest-overlap screen SELECTION rule —
    // CI runs single-screen (offscreen QPA: one 800x600 screen), where every
    // candidate rule agrees. The rule that a window straddling two monitors
    // keeps the one it mostly occupies, rather than being yanked to primary
    // because its center fell in the gap between them, needs real hardware:
    // see the multi-monitor steps in
    // workplans/DIALOG_WINDOW_MANAGEMENT_VERIFICATION.md.
    const auto screens = QGuiApplication::screens();
    QVERIFY(!screens.isEmpty());

    const QScreen *primary = QGuiApplication::primaryScreen();
    QVERIFY(primary);
    const QRect avail = primary->availableGeometry();

    QRect mostlyHere(avail.x() + avail.width() - 300, avail.y() + 150, 400, 300);
    const QRect got = clampToVisibleScreen(mostlyHere, 28);

    // Whichever screen it ends up on, it must be genuinely usable there.
    bool usable = false;
    for (const QScreen *s : screens) {
        const QRect a = s->availableGeometry();
        const QRect vis = a.intersected(got);
        if (vis.width() >= qMin(got.width(), 160)
            && vis.height() >= qMin(got.height(), 60)
            && got.top() - 28 >= a.top())
            usable = true;
    }
    QVERIFY2(usable, "clamped rect is not usable on any connected screen");
}

void TestDialogLayoutPersistence::watcherPersistsAcrossInstances()
{
    DialogLayoutWatcher watcher;
    qApp->installEventFilter(&watcher);
    const QRect rect(140, 120, 460, 340);
    {
        Fixture f(QStringLiteral("WatchedDialog"));
        f.dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&f.dialog));
        f.dialog.setGeometry(rect);
        f.tabs->setCurrentIndex(1);
        f.dialog.hide();   // the watcher saves here
    }
    {
        Fixture fresh(QStringLiteral("WatchedDialog"));
        fresh.dialog.show();   // the watcher restores here, pre-map
        QVERIFY(QTest::qWaitForWindowExposed(&fresh.dialog));
        QCOMPARE(fresh.dialog.geometry(), rect);
        QCOMPARE(fresh.tabs->currentIndex(), 1);
        fresh.dialog.hide();
    }
    qApp->removeEventFilter(&watcher);
}

void TestDialogLayoutPersistence::watcherRespectsOptOutProperty()
{
    DialogLayoutWatcher watcher;
    qApp->installEventFilter(&watcher);
    {
        Fixture f(QStringLiteral("OptedOutDialog"));
        f.dialog.setProperty(kNoLayoutPersistenceProp, true);
        f.dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&f.dialog));
        f.dialog.hide();
    }
    QSettings settings;
    QVERIFY(!settings.contains(
        QStringLiteral("Dialogs/OptedOutDialog/geometry")));
    qApp->removeEventFilter(&watcher);
}

void TestDialogLayoutPersistence::watcherRestoresOncePerInstance()
{
    DialogLayoutWatcher watcher;
    qApp->installEventFilter(&watcher);

    Fixture f(QStringLiteral("OnceDialog"));
    f.dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.dialog));
    f.dialog.hide();

    // Sneak a different geometry into the store; a RE-show of the same
    // live instance must NOT snap to it (only first show restores).
    QSettings settings;
    settings.setValue(QStringLiteral("Dialogs/OnceDialog/geometry"),
                      QRect(200, 210, 350, 250));
    const QRect before = f.dialog.geometry();
    f.dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.dialog));
    QCOMPARE(f.dialog.geometry(), before);
    f.dialog.hide();

    qApp->removeEventFilter(&watcher);
}

QTEST_MAIN(TestDialogLayoutPersistence)
#include "test_dialog_layout_persistence.moc"
