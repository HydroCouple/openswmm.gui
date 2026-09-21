// A tab is labelled with the model's BASE NAME, so two models called the same
// thing in different folders are indistinguishable on the tab bar. The fix is
// a tooltip carrying the full path (refreshSubWindowTabToolTips).
//
// It rests on two QMdiArea facts, and both are pinned here so a Qt upgrade
// that changes either fails loudly instead of silently blanking the tooltips:
//
//   * QMdiArea never sets a tab tooltip itself. Its event filter answers
//     WindowTitleChange / ModifiedChange with setTabText and
//     WindowIconChange with setTabIcon (qmdiarea.cpp:2646-2654); nothing
//     touches the tooltip. So ours survives a rename and a dirty flag.
//   * Tab index == position in subWindowList() — the same rule
//     setSubWindowTabVisible() depends on. Closing a tab shifts every later
//     index, which is why the app re-runs the refresh on sub-window changes.
#include <QtTest/QtTest>

#include <QApplication>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QTabBar>
#include <QWidget>

#include "ui/mdiworkspacechrome.h"

using openswmmvis::ui::refreshSubWindowTabToolTips;

namespace {

/*! The central widget as uic emits it: TabbedView, tabs closable/movable. */
QMdiArea *makeTabbedArea(QWidget *host)
{
    auto *area = new QMdiArea(host);
    area->setViewMode(QMdiArea::TabbedView);
    area->setTabsClosable(true);
    return area;
}

QMdiSubWindow *addDoc(QMdiArea *area, const QString &title)
{
    auto *sub = area->addSubWindow(new QWidget);
    sub->setWindowTitle(title);
    return sub;
}

QTabBar *tabBarOf(QMdiArea *area) { return area->findChild<QTabBar *>(); }

}   // namespace

class TestMdiTabToolTips : public QObject
{
    Q_OBJECT

private slots:
    /*! The whole point: same base name, different folders. */
    void toolTipCarriesFullPath();
    /*! Qt fact 1 — a title change rewrites the text, never the tooltip. */
    void toolTipSurvivesTitleChange();
    /*! Qt fact 2 — indices follow subWindowList(), so a close shifts them. */
    void refreshAfterCloseRetargetsTabs();
    /*! No path (untitled project, welcome tab) falls back to the label. */
    void emptyPathFallsBackToTitle();
};

void TestMdiTabToolTips::toolTipCarriesFullPath()
{
    QWidget host;
    QMdiArea *area = makeTabbedArea(&host);

    QMdiSubWindow *a = addDoc(area, QStringLiteral("site_drainage_model"));
    QMdiSubWindow *b = addDoc(area, QStringLiteral("site_drainage_model"));

    const QHash<QMdiSubWindow *, QString> paths{
        {a, QStringLiteral("/models/north/site_drainage_model.inp")},
        {b, QStringLiteral("/models/south/site_drainage_model.inp")},
    };
    refreshSubWindowTabToolTips(
        area, [&paths](QMdiSubWindow *s) { return paths.value(s); });

    QTabBar *bar = tabBarOf(area);
    QVERIFY(bar);
    QCOMPARE(bar->count(), 2);

    // The labels are identical — that is the bug being fixed — and only the
    // tooltips tell the two apart.
    QCOMPARE(bar->tabText(0), bar->tabText(1));
    QCOMPARE(bar->tabToolTip(0),
             QDir::toNativeSeparators(paths.value(a)));
    QCOMPARE(bar->tabToolTip(1),
             QDir::toNativeSeparators(paths.value(b)));
    QVERIFY(bar->tabToolTip(0) != bar->tabToolTip(1));
}

void TestMdiTabToolTips::toolTipSurvivesTitleChange()
{
    QWidget host;
    QMdiArea *area = makeTabbedArea(&host);
    QMdiSubWindow *sub = addDoc(area, QStringLiteral("model"));

    const QString path = QStringLiteral("/models/a/model.inp");
    refreshSubWindowTabToolTips(area, [&](QMdiSubWindow *) { return path; });

    QTabBar *bar = tabBarOf(area);
    QVERIFY(bar);
    const QString before = bar->tabToolTip(0);

    // A dirty marker goes through the same WindowTitleChange path the app
    // uses (updateWindowTitle appends " *"), and QMdiArea answers it with
    // setTabText only.
    sub->setWindowTitle(QStringLiteral("model *"));
    QCoreApplication::processEvents();

    QVERIFY2(bar->tabText(0).contains(QLatin1Char('*')),
             "QMdiArea should have rewritten the tab TEXT");
    QCOMPARE(bar->tabToolTip(0), before);
    QVERIFY2(!bar->tabToolTip(0).isEmpty(),
             "a title change must not blank the tooltip");
}

void TestMdiTabToolTips::refreshAfterCloseRetargetsTabs()
{
    QWidget host;
    QMdiArea *area = makeTabbedArea(&host);

    QMdiSubWindow *a = addDoc(area, QStringLiteral("a"));
    QMdiSubWindow *b = addDoc(area, QStringLiteral("b"));
    QMdiSubWindow *c = addDoc(area, QStringLiteral("c"));

    QHash<QMdiSubWindow *, QString> paths{
        {a, QStringLiteral("/one/a.inp")},
        {b, QStringLiteral("/two/b.inp")},
        {c, QStringLiteral("/three/c.inp")},
    };
    const auto lookup = [&paths](QMdiSubWindow *s) { return paths.value(s); };
    refreshSubWindowTabToolTips(area, lookup);

    QTabBar *bar = tabBarOf(area);
    QVERIFY(bar);
    QCOMPARE(bar->tabToolTip(1), QDir::toNativeSeparators(paths.value(b)));

    // Remove the FIRST document: c slides from index 2 to 1. Without a
    // refresh the old tooltip would now name the wrong model.
    area->removeSubWindow(a);
    delete a;
    paths.remove(a);
    QCoreApplication::processEvents();

    QCOMPARE(area->subWindowList().size(), 2);
    refreshSubWindowTabToolTips(area, lookup);

    QCOMPARE(bar->tabToolTip(0), QDir::toNativeSeparators(paths.value(b)));
    QCOMPARE(bar->tabToolTip(1), QDir::toNativeSeparators(paths.value(c)));
}

void TestMdiTabToolTips::emptyPathFallsBackToTitle()
{
    QWidget host;
    QMdiArea *area = makeTabbedArea(&host);
    addDoc(area, QStringLiteral("Untitled"));

    refreshSubWindowTabToolTips(area, [](QMdiSubWindow *) { return QString(); });

    QTabBar *bar = tabBarOf(area);
    QVERIFY(bar);
    QCOMPARE(bar->tabToolTip(0), QStringLiteral("Untitled"));
}

QTEST_MAIN(TestMdiTabToolTips)
#include "test_mdi_tab_tooltips.moc"
