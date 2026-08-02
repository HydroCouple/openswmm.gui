// UI redesign iteration 2 (R2) — ribbon framework unit tests: the pure
// width solver (trailing-first two-pass demotion, non-collapsible clamp,
// promotion hysteresis), the captioned RibbonGroup (defaultAction
// mirroring, mode switching at constant height, measured widths, widget
// hosts staying rigid) and the last-used RibbonSplitButton (settings
// restore, promote on trigger AND on programmatic toggled(true)).
#include <QtTest/QtTest>

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QPixmap>
#include <QLayout>
#include <QMainWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QToolBar>
#include <QToolButton>

#include "ui/toolbars/ribboncompactor.h"
#include "ui/toolbars/ribbongroup.h"
#include "ui/toolbars/ribbonlayoutsolver.h"
#include "ui/toolbars/ribbonsplitbutton.h"

using openswmmvis::ui::RibbonGroup;
using openswmmvis::ui::RibbonGroupWidths;
using openswmmvis::ui::RibbonMode;
using openswmmvis::ui::RibbonSplitButton;
using openswmmvis::ui::applyRibbonHysteresis;
using openswmmvis::ui::kRibbonRowHeight;
using openswmmvis::ui::solveRibbonModes;

namespace {

QIcon testIcon()
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::gray);
    return QIcon(pixmap);
}

QAction *makeAction(QObject *parent, const QString &text, const QString &name,
                    bool checkable = false)
{
    auto *action = new QAction(text, parent);
    action->setObjectName(name);
    action->setIcon(testIcon());
    action->setCheckable(checkable);
    return action;
}

}   // namespace

class TestRibbonGroup : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();

    // solver
    void solverFitsAllFull();
    void solverDemotesTrailingFirst();
    void solverTwoPassCollapse();
    void solverNonCollapsibleClamp();
    void solverDegenerateWidths();
    void hysteresisHoldsPromotionsInDeadBand();

    // group
    void groupMirrorsDefaultAction();
    void groupModeSwitchingKeepsHeight();
    void groupWidthsShrinkAcrossModes();
    void groupWidgetHostIsRigid();

    // iteration 3 — honest widths + left-packing + single-tool faces
    void widthForModeRespectsChildMinimumWidth();
    void groupSizePolicyFixedUntilStretchWidget();
    void singleActionGroupNeverCollapses();
    void wrappedLabelFitsRow();

    // split button
    void splitRestoresLastUsedFromSettings();
    void splitUnknownNameFallsBackToFirst();
    void splitPromotesOnTrigger();
    void splitPromotesOnProgrammaticToggle();

    // compactor (R5)
    void compactorStepsTrailingGroupWithWidth();
    void compactorLeftPacksWithTrailingSpacer();

private:
    void wipeRibbonSettings();

    QVector<RibbonGroupWidths> mThree;   // shared solver fixture
};

void TestRibbonGroup::wipeRibbonSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("SWMMVis::Ribbon"));
    settings.remove(QString());
    settings.endGroup();
}

void TestRibbonGroup::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("ribbongroup-test"));
    wipeRibbonSettings();

    RibbonGroupWidths g;
    g.full = 100; g.compact = 60; g.collapsed = 30;
    mThree = {g, g, g};
}

void TestRibbonGroup::init()
{
    wipeRibbonSettings();
}

void TestRibbonGroup::cleanupTestCase()
{
    wipeRibbonSettings();
}

void TestRibbonGroup::solverFitsAllFull()
{
    const auto modes = solveRibbonModes(400, mThree, 4);
    QCOMPARE(modes, (QVector<RibbonMode>{RibbonMode::Full, RibbonMode::Full,
                                         RibbonMode::Full}));
}

void TestRibbonGroup::solverDemotesTrailingFirst()
{
    // Full row = 308; demoting only the trailing group (268) fits 280.
    const auto modes = solveRibbonModes(280, mThree, 4);
    QCOMPARE(modes, (QVector<RibbonMode>{RibbonMode::Full, RibbonMode::Full,
                                         RibbonMode::Compact}));
}

void TestRibbonGroup::solverTwoPassCollapse()
{
    // All-compact (188) still overflows 150, so pass 2 collapses from the
    // trailing end until it fits (60 + 30 + 30 + 8 = 128).
    const auto modes = solveRibbonModes(150, mThree, 4);
    QCOMPARE(modes, (QVector<RibbonMode>{RibbonMode::Compact,
                                         RibbonMode::Collapsed,
                                         RibbonMode::Collapsed}));
}

void TestRibbonGroup::solverNonCollapsibleClamp()
{
    auto groups = mThree;
    groups[1].collapsible = false;
    const auto modes = solveRibbonModes(10, groups, 4);   // can never fit
    QVERIFY(modes.at(1) != RibbonMode::Collapsed);
    QCOMPARE(modes.at(0), RibbonMode::Collapsed);
    QCOMPARE(modes.at(2), RibbonMode::Collapsed);
}

void TestRibbonGroup::solverDegenerateWidths()
{
    QCOMPARE(solveRibbonModes(200, {}, 4).size(), 0);
    const auto modes = solveRibbonModes(0, mThree, 4);
    QCOMPARE(modes, (QVector<RibbonMode>{RibbonMode::Collapsed,
                                         RibbonMode::Collapsed,
                                         RibbonMode::Collapsed}));
    // Negative width behaves like zero — maximally demoted, no crash.
    QCOMPARE(solveRibbonModes(-50, mThree, 4).size(), 3);
}

void TestRibbonGroup::hysteresisHoldsPromotionsInDeadBand()
{
    RibbonGroupWidths g;
    g.full = 100; g.compact = 60; g.collapsed = 30;
    const QVector<RibbonGroupWidths> one = {g};

    // 105 px fits Full, but not with 32 fewer — promotion held back.
    QCOMPARE(applyRibbonHysteresis({RibbonMode::Compact}, 105, one, 0),
             (QVector<RibbonMode>{RibbonMode::Compact}));
    // 140 px sustains Full through the dead band — promotion lands.
    QCOMPARE(applyRibbonHysteresis({RibbonMode::Compact}, 140, one, 0),
             (QVector<RibbonMode>{RibbonMode::Full}));
    // Demotions are immediate: no dead band on the way down.
    QCOMPARE(applyRibbonHysteresis({RibbonMode::Full}, 70, one, 0),
             (QVector<RibbonMode>{RibbonMode::Compact}));
    // Group-count change falls back to the plain solve.
    QCOMPARE(applyRibbonHysteresis({}, 140, one, 0),
             (QVector<RibbonMode>{RibbonMode::Full}));
}

void TestRibbonGroup::groupMirrorsDefaultAction()
{
    RibbonGroup group(QStringLiteral("Navigate"));
    QAction *pan = makeAction(&group, QStringLiteral("Pan"),
                              QStringLiteral("actionTestPan"), true);
    QToolButton *button = group.addAction(pan);

    QCOMPARE(button->defaultAction(), pan);
    QCOMPARE(group.buttonForAction(pan), button);

    pan->setChecked(true);
    QVERIFY(button->isChecked());
    pan->setEnabled(false);
    QVERIFY(!button->isEnabled());
    pan->setEnabled(true);

    QSignalSpy spy(pan, &QAction::triggered);
    button->click();
    QCOMPARE(spy.count(), 1);
}

void TestRibbonGroup::groupModeSwitchingKeepsHeight()
{
    QWidget host;
    auto *group = new RibbonGroup(QStringLiteral("Project"), &host);
    QAction *a = makeAction(group, QStringLiteral("New"),
                            QStringLiteral("actionTestNew"));
    QAction *b = makeAction(group, QStringLiteral("Open"),
                            QStringLiteral("actionTestOpen"));
    QToolButton *buttonA = group->addAction(a);
    group->addAction(b);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    QCOMPARE(group->height(), kRibbonRowHeight);
    QCOMPARE(buttonA->toolButtonStyle(), Qt::ToolButtonTextUnderIcon);
    QCOMPARE(buttonA->iconSize().width(), openswmmvis::ui::kRibbonIconFull);

    group->setMode(RibbonMode::Compact);
    QCOMPARE(buttonA->toolButtonStyle(), Qt::ToolButtonIconOnly);
    QCOMPARE(buttonA->iconSize().width(), openswmmvis::ui::kRibbonIconCompact);
    QVERIFY(buttonA->isVisible());
    QCOMPARE(group->height(), kRibbonRowHeight);

    group->setMode(RibbonMode::Collapsed);
    QVERIFY(!buttonA->isVisible());
    QToolButton *popup = nullptr;
    const auto toolButtons = group->findChildren<QToolButton *>();
    for (QToolButton *tb : toolButtons) {
        if (tb->isVisible() && tb->text() == QStringLiteral("Project"))
            popup = tb;
    }
    QVERIFY2(popup, "collapsed popup button not visible");
    QVERIFY(popup->menu());
    QCOMPARE(popup->menu()->actions().size(), 2);
    QCOMPARE(popup->menu()->actions().first(), a);
    QCOMPARE(group->height(), kRibbonRowHeight);

    group->setMode(RibbonMode::Full);
    QVERIFY(buttonA->isVisible());
    QCOMPARE(buttonA->toolButtonStyle(), Qt::ToolButtonTextUnderIcon);
}

void TestRibbonGroup::groupWidthsShrinkAcrossModes()
{
    RibbonGroup group(QStringLiteral("Zoom"));
    for (int i = 0; i < 4; ++i) {
        group.addAction(makeAction(&group,
                                   QStringLiteral("Long Command %1").arg(i),
                                   QStringLiteral("actionTestWide%1").arg(i)));
    }
    const int full      = group.widthForMode(RibbonMode::Full);
    const int compact   = group.widthForMode(RibbonMode::Compact);
    const int collapsed = group.widthForMode(RibbonMode::Collapsed);
    QVERIFY2(full > compact,
             qPrintable(QStringLiteral("full %1 !> compact %2")
                            .arg(full).arg(compact)));
    QVERIFY2(compact > collapsed,
             qPrintable(QStringLiteral("compact %1 !> collapsed %2")
                            .arg(compact).arg(collapsed)));

    const RibbonGroupWidths widths = group.groupWidths();
    QCOMPARE(widths.full, full);
    QVERIFY(widths.collapsible);
}

void TestRibbonGroup::groupWidgetHostIsRigid()
{
    RibbonGroup group(QStringLiteral("Timeline"));
    group.addAction(makeAction(&group, QStringLiteral("Play"),
                               QStringLiteral("actionTestPlay")));
    group.addWidget(new QComboBox(&group), 1);

    QVERIFY(!group.isCollapsible());
    const RibbonGroupWidths widths = group.groupWidths();
    QCOMPARE(widths.full, widths.compact);
    QCOMPARE(widths.full, widths.collapsed);
    QVERIFY(!widths.collapsible);

    // A collapse request clamps to Compact — member widgets can't live
    // inside a popup menu.
    group.setMode(RibbonMode::Collapsed);
    QVERIFY(group.mode() != RibbonMode::Collapsed);
}

void TestRibbonGroup::widthForModeRespectsChildMinimumWidth()
{
    // An explicit setMinimumWidth is invisible to a raw sizeHint but the
    // inner layout clamps the widget back up to it — the group must
    // advertise at least that much or its children overlap when starved
    // (the analysis 1D/2D combos regression).
    RibbonGroup group(QStringLiteral("Results"));
    auto *combo = new QComboBox(&group);
    combo->setMinimumWidth(200);
    group.addWidget(combo);

    const int full = group.widthForMode(RibbonMode::Full);
    QVERIFY2(full >= 200,
             qPrintable(QStringLiteral("group width %1 ignores the 200 px "
                                       "child minimum").arg(full)));
}

void TestRibbonGroup::groupSizePolicyFixedUntilStretchWidget()
{
    // Fixed by default so the host toolbar can't inflate groups with
    // leftover width; a stretch>0 member widget (the timeline slider)
    // flips the group to Expanding so IT absorbs the slack instead.
    RibbonGroup group(QStringLiteral("Playback"));
    QCOMPARE(group.sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);

    group.addWidget(new QComboBox(&group), 0);
    QCOMPARE(group.sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);

    group.addWidget(new QComboBox(&group), 1);
    QCOMPARE(group.sizePolicy().horizontalPolicy(), QSizePolicy::Expanding);
}

void TestRibbonGroup::singleActionGroupNeverCollapses()
{
    // One lone tool must never turn into a caption-titled dropdown —
    // icon-only Compact is its floor.
    RibbonGroup one(QStringLiteral("Edit"));
    one.addAction(makeAction(&one, QStringLiteral("Edit Existing"),
                             QStringLiteral("actTestEditOne")));
    QVERIFY(!one.groupWidths().collapsible);
    one.setMode(RibbonMode::Collapsed);
    QCOMPARE(one.mode(), RibbonMode::Compact);

    RibbonGroup two(QStringLiteral("Zoom"));
    two.addAction(makeAction(&two, QStringLiteral("In"),
                             QStringLiteral("actTestZoomIn")));
    two.addAction(makeAction(&two, QStringLiteral("Out"),
                             QStringLiteral("actTestZoomOut")));
    QVERIFY(two.groupWidths().collapsible);
    two.setMode(RibbonMode::Collapsed);
    QCOMPARE(two.mode(), RibbonMode::Collapsed);
}

void TestRibbonGroup::wrappedLabelFitsRow()
{
    // ArcGIS-style two-line faces: an iconText with '\n' renders as two
    // centered lines — narrower than the one-line face, taller, and
    // still inside the fixed row (icon + two lines + caption budget).
    RibbonGroup group(QStringLiteral("Analysis"));
    QAction *flat = makeAction(&group,
                               QStringLiteral("Flow Balance Downstream"),
                               QStringLiteral("actTestFlat"));
    QAction *wrapped = makeAction(&group,
                                  QStringLiteral("Flow Balance Downstream"),
                                  QStringLiteral("actTestWrapped"));
    wrapped->setIconText(QStringLiteral("Flow Balance\nDownstream"));
    QToolButton *flatButton = group.addAction(flat);
    QToolButton *wrappedButton = group.addAction(wrapped);

    QVERIFY2(wrappedButton->sizeHint().width() < flatButton->sizeHint().width(),
             "wrapping did not narrow the face");
    QVERIFY(wrappedButton->sizeHint().height() > flatButton->sizeHint().height());

    // Two label lines + the 0.85x caption line must fit the fixed row.
    QFont captionFont = group.font();
    captionFont.setPointSizeF(captionFont.pointSizeF() * 0.85);
    const int captionH = QFontMetrics(captionFont).height();
    QVERIFY2(wrappedButton->sizeHint().height() + captionH + 6
                 <= kRibbonRowHeight,
             qPrintable(QStringLiteral("button %1 + caption %2 overflow the "
                                       "%3 px row")
                            .arg(wrappedButton->sizeHint().height())
                            .arg(captionH).arg(kRibbonRowHeight)));
    QCOMPARE(group.sizeHint().height(), kRibbonRowHeight);
}

void TestRibbonGroup::splitRestoresLastUsedFromSettings()
{
    QWidget host;
    QAction *a = makeAction(&host, QStringLiteral("Select"),
                            QStringLiteral("actionTestSelect"), true);
    QAction *b = makeAction(&host, QStringLiteral("Select By Region"),
                            QStringLiteral("actionTestSelectRegion"), true);

    QSettings settings;
    settings.beginGroup(QStringLiteral("SWMMVis::Ribbon"));
    settings.setValue(QStringLiteral("LastUsed/select"),
                      QStringLiteral("actionTestSelectRegion"));
    settings.endGroup();

    RibbonSplitButton button(QStringLiteral("select"), {a, b}, &host);
    QCOMPARE(button.defaultAction(), b);
    QVERIFY(button.menu());
    QCOMPARE(button.menu()->actions().size(), 2);
}

void TestRibbonGroup::splitUnknownNameFallsBackToFirst()
{
    QWidget host;
    QAction *a = makeAction(&host, QStringLiteral("A"), QStringLiteral("actA"));
    QAction *b = makeAction(&host, QStringLiteral("B"), QStringLiteral("actB"));

    QSettings settings;
    settings.beginGroup(QStringLiteral("SWMMVis::Ribbon"));
    settings.setValue(QStringLiteral("LastUsed/fam"),
                      QStringLiteral("doesNotExist"));
    settings.endGroup();

    RibbonSplitButton button(QStringLiteral("fam"), {a, b}, &host);
    QCOMPARE(button.defaultAction(), a);
}

void TestRibbonGroup::splitPromotesOnTrigger()
{
    QWidget host;
    QAction *a = makeAction(&host, QStringLiteral("Junction"),
                            QStringLiteral("actJunction"));
    QAction *b = makeAction(&host, QStringLiteral("Outfall"),
                            QStringLiteral("actOutfall"));
    RibbonSplitButton button(QStringLiteral("addNode"), {a, b}, &host);
    QCOMPARE(button.defaultAction(), a);

    b->trigger();
    QCOMPARE(button.defaultAction(), b);

    QSettings settings;
    settings.beginGroup(QStringLiteral("SWMMVis::Ribbon"));
    QCOMPARE(settings.value(QStringLiteral("LastUsed/addNode")).toString(),
             QStringLiteral("actOutfall"));
    settings.endGroup();
}

void TestRibbonGroup::splitPromotesOnProgrammaticToggle()
{
    // The tool-sync path: Esc returns to Select by CHECKING the action
    // programmatically (no trigger). The face must follow and show the
    // checked state.
    QWidget host;
    QAction *a = makeAction(&host, QStringLiteral("Select"),
                            QStringLiteral("selA"), true);
    QAction *b = makeAction(&host, QStringLiteral("Select By Region"),
                            QStringLiteral("selB"), true);
    RibbonSplitButton button(QStringLiteral("selectFam"), {a, b}, &host);
    QCOMPARE(button.defaultAction(), a);

    b->setChecked(true);
    QCOMPARE(button.defaultAction(), b);
    QVERIFY(button.isChecked());

    a->setChecked(true);
    b->setChecked(false);
    QCOMPARE(button.defaultAction(), a);
    QVERIFY(button.isChecked());
}

void TestRibbonGroup::compactorStepsTrailingGroupWithWidth()
{
    using openswmmvis::ui::RibbonCompactor;

    // Host the bar in a QMainWindow exactly like the app does — a
    // standalone QToolBar's overflow machinery doesn't re-layout the way
    // the QMainWindow-managed one does. Short captions keep caption
    // width out of the arithmetic; the trailing group gets five actions
    // so Compact → Collapsed is a big step.
    QMainWindow window;
    auto *barPtr = new QToolBar(&window);
    window.addToolBar(barPtr);
    QToolBar &bar = *barPtr;
    auto makeGroup = [this, &bar](const QString &caption, int actionCount,
                                  const QString &prefix) {
        auto *group = new RibbonGroup(caption, &bar);
        for (int i = 0; i < actionCount; ++i)
            group->addAction(makeAction(group,
                                        QStringLiteral("%1 Command %2")
                                            .arg(prefix).arg(i),
                                        prefix + QString::number(i)));
        bar.addWidget(group);
        return group;
    };
    RibbonGroup *g1 = makeGroup(QStringLiteral("A"), 3, QStringLiteral("a"));
    RibbonGroup *g2 = makeGroup(QStringLiteral("B"), 3, QStringLiteral("b"));
    RibbonGroup *g3 = makeGroup(QStringLiteral("C"), 5, QStringLiteral("c"));

    const int f1 = g1->widthForMode(RibbonMode::Full);
    const int f2 = g2->widthForMode(RibbonMode::Full);
    const int f3 = g3->widthForMode(RibbonMode::Full);
    const int c1 = g1->widthForMode(RibbonMode::Compact);
    const int c2 = g2->widthForMode(RibbonMode::Compact);
    const int c3 = g3->widthForMode(RibbonMode::Compact);
    const int col3 = g3->widthForMode(RibbonMode::Collapsed);

    new RibbonCompactor(&bar, {g1, g2, g3});

    // Wide: everything Full.
    window.resize(f1 + f2 + f3 + 120, 220);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_COMPARE(g3->mode(), RibbonMode::Full);
    QTRY_COMPARE(g1->mode(), RibbonMode::Full);

    // Slightly too narrow for three Full rows: the TRAILING group steps
    // down first, the leading ones stay Full. (f3 - c3 is large, so the
    // 20 px shave plus toolbar overhead still fits {Full, Full, Compact}.)
    window.resize(f1 + f2 + f3 - 20, 220);
    QTRY_COMPARE(g3->mode(), RibbonMode::Compact);
    QCOMPARE(g1->mode(), RibbonMode::Full);
    QCOMPARE(g2->mode(), RibbonMode::Full);

    // Much narrower: pass 2 collapses the trailing group behind its
    // caption popup; the popup still reaches every action. The toolbar's
    // own relayout (un-hiding the shrunken group) is asynchronous, so
    // the visibility probe retries.
    window.resize(c1 + c2 + col3 + 80, 220);
    QTRY_COMPARE(g3->mode(), RibbonMode::Collapsed);
    const auto findPopup = [g3]() -> QToolButton * {
        const auto buttons = g3->findChildren<QToolButton *>();
        for (QToolButton *tb : buttons)
            if (tb->isVisible() && tb->text() == QStringLiteral("C"))
                return tb;
        return nullptr;
    };
    QTRY_VERIFY2(findPopup() != nullptr, "collapsed popup face not visible");
    QToolButton *popup = findPopup();
    QVERIFY(popup->menu());
    QCOMPARE(popup->menu()->actions().size(), 5);
    QSignalSpy spy(popup->menu()->actions().first(), &QAction::triggered);
    popup->menu()->actions().first()->trigger();
    QCOMPARE(spy.count(), 1);

    // Wide again: promotions land (the huge width holds through the
    // 32 px dead band) and the row returns to Full.
    window.resize(f1 + f2 + f3 + 120, 220);
    QTRY_COMPARE(g3->mode(), RibbonMode::Full);
    QCOMPARE(g1->mode(), RibbonMode::Full);
}

void TestRibbonGroup::compactorLeftPacksWithTrailingSpacer()
{
    using openswmmvis::ui::RibbonCompactor;

    // The app appends an Expanding zero-min spacer to each bar so the
    // Fixed-width groups pack left and leftover width pools at the end.
    // The spacer must neither inflate the groups nor trip the solver
    // (its one extra inter-item spacing is discounted by objectName).
    QMainWindow window;
    auto *bar = new QToolBar(&window);
    window.addToolBar(bar);
    auto makeGroup = [bar](const QString &caption, const QString &prefix) {
        auto *group = new RibbonGroup(caption, bar);
        for (int i = 0; i < 3; ++i)
            group->addAction(makeAction(group,
                                        QStringLiteral("%1 Command %2")
                                            .arg(prefix).arg(i),
                                        prefix + QString::number(i)));
        bar->addWidget(group);
        return group;
    };
    RibbonGroup *g1 = makeGroup(QStringLiteral("A"), QStringLiteral("a"));
    RibbonGroup *g2 = makeGroup(QStringLiteral("B"), QStringLiteral("b"));
    auto *spacer = new QWidget(bar);
    spacer->setObjectName(QStringLiteral("ribbonBarSpacer"));
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    spacer->setMinimumSize(0, 0);
    bar->addWidget(spacer);

    const int f1 = g1->widthForMode(RibbonMode::Full);
    const int f2 = g2->widthForMode(RibbonMode::Full);

    new RibbonCompactor(bar, {g1, g2});

    // Plenty of slack: both Full, and NOT inflated by the leftover —
    // each group's on-screen width equals its measured Full width while
    // the spacer soaks up the rest.
    window.resize(f1 + f2 + 300, 220);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_COMPARE(g1->mode(), RibbonMode::Full);
    QTRY_COMPARE(g2->mode(), RibbonMode::Full);
    QTRY_COMPARE(g1->width(), f1);
    QTRY_COMPARE(g2->width(), f2);
    QVERIFY2(spacer->width() > 100,
             qPrintable(QStringLiteral("spacer %1 px did not absorb the "
                                       "leftover").arg(spacer->width())));

    // Starved: demotion still engages with the spacer present.
    window.resize(f1 + f2 - 40, 220);
    QTRY_COMPARE(g2->mode(), RibbonMode::Compact);
    QCOMPARE(g1->mode(), RibbonMode::Full);
}

QTEST_MAIN(TestRibbonGroup)
#include "test_ribbongroup.moc"
