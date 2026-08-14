// Welcome-screen chrome (installMdiWorkspaceChrome): the MDI backdrop
// tracks the theme palette, and every activated sub-window in TabbedView
// stays maximized so none of them is left floating as a small framed
// child over the tab the user selected.
//
// Ground truth for the underlying Qt behaviour is tests/scratch/
// mdi_tab_probe.cpp; the two QMdiArea facts these tests pin down are:
//   * QMdiArea::QMdiArea snapshots palette(QPalette::Dark) once and has no
//     PaletteChange handling (qmdiarea.cpp:1681), and its paintEvent fills
//     the viewport with that snapshot;
//   * QMdiAreaPrivate::_q_deactivateAllWindows (qmdiarea.cpp:682-683) only
//     hands the maximized state to the incoming window when an outgoing
//     one is BOTH maximized and visible, and TabbedView never hides an
//     inactive sub-window.
#include <QtTest/QtTest>

#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPalette>
#include <QTabBar>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/mdiworkspacechrome.h"
#include "ui/theme/thememanager.h"
#include "ui/theme/themetokens.h"

using openswmmvis::ui::ThemeManager;
using openswmmvis::ui::darkColors;
using openswmmvis::ui::installMdiWorkspaceChrome;
using openswmmvis::ui::lightColors;

namespace {

/*!
 * \brief The app's central widget, built exactly as uic emits it
 *        (ui_swmmvis.h:629-826).
 *
 * Order matters: TabbedView and the tab properties are set BEFORE the
 * welcome QWidget is handed to addSubWindow(), and that happens while the
 * area is still hidden — which is what puts the welcome sub-window into
 * QMdiArea's pendingPlacements rather than through the normal activation
 * path. Reproducing that ordering is the whole point of this fixture.
 */
struct Workspace
{
    QMainWindow win;
    QMdiArea *area = nullptr;
    QWidget *welcome = nullptr;

    Workspace()
    {
        area = new QMdiArea(&win);
        area->setViewMode(QMdiArea::TabbedView);
        area->setDocumentMode(true);
        area->setTabsClosable(true);
        area->setTabsMovable(true);

        welcome = new QWidget();
        auto *lay = new QVBoxLayout(welcome);
        lay->addWidget(new QLabel(QStringLiteral("Stormwater Management Model")));
        area->addSubWindow(welcome);
        welcome->setWindowTitle(QStringLiteral("Welcome"));

        win.setCentralWidget(area);
        win.resize(1200, 800);
    }

    void show()
    {
        win.show();
        QApplication::processEvents();
    }

    QMdiSubWindow *welcomeSub() const
    {
        for (QMdiSubWindow *s : area->subWindowList())
            if (s->widget() == welcome)
                return s;
        return nullptr;
    }

    /*!
     * A project tab, following the app's real open sequence: the canvas
     * carries WA_OpaquePaintEvent and the 200x150 floor from
     * swmmvisprojectwindow.cpp, addSubWindow() lands the tab immediately
     * (swmmvis.cpp:4544) and show() + setActiveSubWindow() only follow
     * once the async .inp load finishes (swmmvis.cpp:4729-4730) — hence
     * the event pump in between.
     */
    QMdiSubWindow *addProject(const QString &title)
    {
        auto *sub = new QMdiSubWindow;
        auto *canvas = new QWidget;
        canvas->setAttribute(Qt::WA_OpaquePaintEvent);
        sub->setWidget(canvas);
        sub->setWindowTitle(title);
        sub->setMinimumSize(200, 150);
        area->addSubWindow(sub);
        QApplication::processEvents();
        sub->show();
        area->setActiveSubWindow(sub);
        QApplication::processEvents();
        return sub;
    }
};

//! Sibling z-order: among QWidget siblings the LATER entry in the parent's
//! children() list paints on top. That is what decides whether a restored
//! sub-window is actually seen or is harmlessly buried behind the
//! maximized active one.
int zIndex(QMdiSubWindow *s)
{
    return s->parentWidget() ? s->parentWidget()->children().indexOf(s) : -1;
}

/*!
 * \brief Sub-windows the user would actually see floating over the tab
 *        they selected.
 *
 * Not simply "visible and un-maximized": Qt deliberately calls
 * showNormal() on the OUTGOING window (qmdiarea.cpp:684-690) and
 * TabbedView never hides it, so one restored sub-window is always on the
 * list and that alone is fine. It only becomes the reported defect when
 * it also paints ABOVE the active window. Same definition as
 * tests/scratch/mdi_tab_probe.cpp:71-81.
 */
int visibleStrays(QMdiArea *area)
{
    QMdiSubWindow *active = area->activeSubWindow();
    if (!active)
        return 0;
    int n = 0;
    for (QMdiSubWindow *s : area->subWindowList())
        if (s != active && s->isVisible() && !s->isMaximized()
            && zIndex(s) > zIndex(active))
            ++n;
    return n;
}

}   // namespace

class TestMdiTabMaximize : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void backdropMatchesWindowSurface();
    void backdropFollowsThemeSwitch();
    void welcomeIsMaximizedOnFirstShow();
    void switchingTabsLeavesNoStray();
    void hiddenWelcomeDoesNotStrandTheNextTab();
    void tabBarSwitchingLeavesNoStray();
    void hiddenSubWindowIsNotResurrected();
};

//! Every test starts from Light so backdrop expectations are stable and
//! the themeChanged edge in backdropFollowsThemeSwitch() is a real flip.
void TestMdiTabMaximize::init()
{
    ThemeManager::instance()->setMode(ThemeManager::Mode::Light);
    ThemeManager::instance()->apply();
}

void TestMdiTabMaximize::backdropMatchesWindowSurface()
{
    Workspace w;
    // Before: the ctor snapshot of the *pre-theme* palette. Not asserted —
    // it depends on whichever style ran first — but this is the value the
    // welcome tab used to show through.
    installMdiWorkspaceChrome(w.area);

    QCOMPARE(w.area->background().color(),
             qApp->palette().color(QPalette::Window));
    QCOMPARE(w.area->background().color(), lightColors().surfaceWindow);
    QVERIFY(w.area->background().isOpaque());
}

void TestMdiTabMaximize::backdropFollowsThemeSwitch()
{
    Workspace w;
    installMdiWorkspaceChrome(w.area);
    w.show();

    ThemeManager::instance()->setMode(ThemeManager::Mode::Dark);
    QCOMPARE(w.area->background().color(), darkColors().surfaceWindow);

    ThemeManager::instance()->setMode(ThemeManager::Mode::Light);
    QCOMPARE(w.area->background().color(), lightColors().surfaceWindow);
}

void TestMdiTabMaximize::welcomeIsMaximizedOnFirstShow()
{
    Workspace w;
    installMdiWorkspaceChrome(w.area);
    w.show();

    QMdiSubWindow *welcome = w.welcomeSub();
    QVERIFY(welcome);
    QVERIFY(welcome->isMaximized());
    QCOMPARE(visibleStrays(w.area), 0);
}

void TestMdiTabMaximize::switchingTabsLeavesNoStray()
{
    Workspace w;
    installMdiWorkspaceChrome(w.area);
    w.show();

    QMdiSubWindow *welcome = w.welcomeSub();
    QMdiSubWindow *model = w.addProject(QStringLiteral("ModelA"));
    QVERIFY(model->isMaximized());
    QCOMPARE(visibleStrays(w.area), 0);

    w.area->setActiveSubWindow(welcome);
    QApplication::processEvents();
    QVERIFY(welcome->isMaximized());
    QCOMPARE(visibleStrays(w.area), 0);

    w.area->setActiveSubWindow(model);
    QApplication::processEvents();
    QVERIFY(model->isMaximized());
    QCOMPARE(visibleStrays(w.area), 0);
}

//! The reported bug, and the one function here that fails without the
//! fix. swmmvis.cpp hides the welcome sub-window in place — at startup
//! when "show on startup" is off (initializeWelcomeScreen) and when the
//! tab's X is clicked (eventFilter) — which is exactly the state that
//! stops Qt handing the maximized flag to the next activation. The
//! switching tests above pass either way; they guard against the fix
//! itself breaking the ordinary paths.
void TestMdiTabMaximize::hiddenWelcomeDoesNotStrandTheNextTab()
{
    Workspace w;
    installMdiWorkspaceChrome(w.area);
    w.show();

    QMdiSubWindow *welcome = w.welcomeSub();
    QVERIFY(welcome->isMaximized());

    welcome->hide();            // hidden WHILE maximized
    QApplication::processEvents();

    QMdiSubWindow *model = w.addProject(QStringLiteral("ModelA"));
    QVERIFY(model->isMaximized());
    QCOMPARE(visibleStrays(w.area), 0);
}

//! Same journey driven through the MDI's own QTabBar, which is what a
//! real tab click does (QMdiAreaPrivate::_q_currentTabChanged) rather
//! than setActiveSubWindow().
void TestMdiTabMaximize::tabBarSwitchingLeavesNoStray()
{
    Workspace w;
    installMdiWorkspaceChrome(w.area);
    w.show();
    w.addProject(QStringLiteral("ModelA"));

    auto *tabs = w.area->findChild<QTabBar *>();
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 2);
    // Tab order follows childWindows (appendChild): 0 = welcome, 1 =
    // model. addProject already left the current index at 1, so start at
    // 0 to make all three iterations real switches.
    QCOMPARE(tabs->currentIndex(), 1);

    for (int i : {0, 1, 0}) {
        tabs->setCurrentIndex(i);
        QApplication::processEvents();
        QCOMPARE(visibleStrays(w.area), 0);
    }
}

/*!
 * The guard re-asserts the maximized state, and showMaximized() also
 * SHOWS a window — so it must skip explicitly hidden sub-windows or it
 * would resurrect a welcome tab the user dismissed.
 *
 * Reaching the guard with a hidden sub-window needs two things. First,
 * Qt's one-shot showActiveWindowMaximized — armed by appendChild for the
 * first sub-window (qmdiarea.cpp:825-831) and consumed by
 * emitWindowActivated (:1021-1025), which calls showMaximized() BEFORE
 * emitting subWindowActivated — must already be spent, so the first
 * w.show() has to happen with the welcome still visible. Second, the sub
 * must be hidden *and* normal, which is where Qt leaves an outgoing tab
 * (showNormal at :684-690) whose X the user then clicks. Re-showing the
 * main window afterwards runs QMdiArea::showEvent →
 * activateCurrentWindow() (:2404-2405), which has no visibility guard and
 * activates the hidden sub. setActiveSubWindow() cannot provoke this:
 * QMdiAreaPrivate::activateWindow bails on a hidden child (:968).
 */
void TestMdiTabMaximize::hiddenSubWindowIsNotResurrected()
{
    Workspace w;
    installMdiWorkspaceChrome(w.area);
    w.show();                   // spends Qt's one-shot maximize arm

    QMdiSubWindow *welcome = w.welcomeSub();
    QVERIFY(welcome);
    welcome->showNormal();      // the state Qt leaves an outgoing tab in
    welcome->hide();            // the tab-X close path: hidden AND normal
    QApplication::processEvents();

    w.win.hide();
    QApplication::processEvents();
    w.win.show();               // showEvent activates the hidden sub
    QApplication::processEvents();

    QVERIFY(welcome->isHidden());
    QVERIFY(!welcome->isVisible());
    QVERIFY(!welcome->isMaximized());
}

QTEST_MAIN(TestMdiTabMaximize)
#include "test_mdi_tab_maximize.moc"
