// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      test_profileplot_keyboard.cpp
 * @brief     Review A-01: the 1D profile's keyboard node cursor and its screen-reader interface.
 * @details   Synthetic 12-node path with an HGL and a Max HGL series over two periods; no engine.
 *            Checks that focus places the cursor, the keys walk the nodes and announce each
 *            move, playback updates the value without announcing, Enter and the Menu key act as
 *            clicks on the node, the details provider adds the track values, the cursor is drawn
 *            only with keyboard focus, and moving the cursor keeps its node in view.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "plot/profileplotoptions.h"
#include "plot/profileplotwidget.h"

#include "accessible_checks.h"

#include <QAccessible>
#include <QHBoxLayout>
#include <QImage>
#include <QLineEdit>
#include <QLocale>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <memory>

namespace
{

constexpr int kNodes = 12;

/** A line edit followed by the plot, so Tab can give the plot keyboard focus. */
struct ProfileHost {
    QWidget host;
    QLineEdit *before = new QLineEdit(&host);
    ProfilePlotWidget *plot = new ProfilePlotWidget(&host);
    ProfilePlotOptions options;

    ProfileHost()
    {
        using namespace ProfileBuilder;
        auto *layout = new QHBoxLayout(&host);
        layout->addWidget(before);
        layout->addWidget(plot, 1);
        host.resize(1000, 520);

        options.setLegendVisible(false);
        options.setShowNodeRoses(false);
        options.setShowBranchStubs(false);
        PathStatic path;
        SourceSeries source;
        source.periodCount = 2;
        source.reportStepSec = 60;
        for (int i = 0; i < kNodes; ++i) {
            NodeStatic node;
            node.name = QStringLiteral("N%1").arg(i);
            node.invertElev = 100.0 - i;
            node.maxDepth = 2.5;
            node.kind = i + 1 == kNodes ? NodeKind::Outfall : NodeKind::Junction;
            path.nodes.append(node);
            path.chainage.append(100.0 * i);
            source.nodeHead.append({float(node.invertElev + 0.5), float(node.invertElev + 1.0)});
            if (i > 0) {
                LinkStatic link;
                link.name = QStringLiteral("L%1").arg(i);
                link.length = 100;
                link.maxDepth = 1.0;
                path.links.append(link);
                source.linkVelocity.append({1.f, 1.f});
            }
        }
        auto derived = std::make_shared<SourceDerived>(compute(path, source, kGravityFps2));
        ProfilePlotWidget::SeriesBinding hgl, maxHgl;
        hgl.label = QStringLiteral("run — HGL");
        hgl.kind = OutputKind::HGL;
        hgl.derived = derived;
        hgl.pen = QPen(Qt::blue, 2);
        maxHgl.label = QStringLiteral("run — Max HGL");
        maxHgl.kind = OutputKind::MaxHGL;
        maxHgl.derived = derived;
        maxHgl.pen = QPen(Qt::red, 1);
        plot->setOptions(&options);
        plot->setPath(path);
        plot->setSeries({hgl, maxHgl});
        plot->setCurrentPeriod(0);
        plot->fitToExtent();
    }

    void showAndTabIn()
    {
        host.show();
        QVERIFY(QTest::qWaitForWindowActive(&host));
        before->setFocus(Qt::OtherFocusReason);
        QTest::keyClick(before, Qt::Key_Tab);
        QVERIFY(plot->hasFocus());
    }
};

} // namespace

class TestProfilePlotKeyboard : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QLocale::setDefault(QLocale::c()); }
    /** @brief Tab puts the cursor on the first node; the interface reads it and the path. */
    void focusPutsTheCursorOnTheFirstNode();
    /** @brief Arrows, Shift+arrows and Ctrl+Home/End walk the nodes; each move is announced. */
    void keysWalkTheNodesAndAnnounce();
    /** @brief A period change updates the value but sends no announcement. */
    void playbackUpdatesTheValueSilently();
    /** @brief Enter acts as a click on the node; Menu and Shift+F10 as a right-click. */
    void enterSelectsAndMenuKeyOpensTheNodeMenu();
    /** @brief The details provider's text (the attribute tracks) ends the value. */
    void detailsProviderAddsTrackValues();
    /** @brief The cursor is drawn with keyboard focus only; a click focus shows none. */
    void cursorIsDrawnOnlyWithKeyboardFocus();
    /** @brief Clicking a node moves the cursor there. */
    void clickingANodeMovesTheCursor();
    /** @brief Moving the cursor off screen shifts the view to show its node. */
    void cursorNodeStaysInView();
};

void TestProfilePlotKeyboard::focusPutsTheCursorOnTheFirstNode()
{
    ProfileHost h;
    h.showAndTabIn();
    QCOMPARE(h.plot->cursorNode(), 0);

    QAccessibleInterface *iface = a11ytest::accessibleFor(h.plot);
    QVERIFY(iface);
    QCOMPARE(iface->role(), QAccessible::Chart);
    QCOMPARE(iface->text(QAccessible::Name), QStringLiteral("1D profile"));
    QCOMPARE(iface->text(QAccessible::Value),
             QStringLiteral("N0, node 1 of 12; invert 100.00; rim 102.50; run — HGL 100.50; "
                            "run — Max HGL 101.00"));
    const QString description = iface->text(QAccessible::Description);
    QVERIFY2(description.startsWith(QStringLiteral("Path from N0 to N11, 12 nodes. Shown: "
                                                   "run — HGL, run — Max HGL. Vertical axis: ")),
             qPrintable(description));
    QVERIFY(description.contains(QStringLiteral("Ctrl+Home")));
}

void TestProfilePlotKeyboard::keysWalkTheNodesAndAnnounce()
{
    ProfileHost h;
    h.showAndTabIn();
    a11ytest::AccessibleEventRecorder events;
    QStringList expected;
    const auto press = [&](Qt::Key key, Qt::KeyboardModifiers mods, int node) {
        QTest::keyClick(h.plot, key, mods);
        QCOMPARE(h.plot->cursorNode(), node);
        expected.append(h.plot->cursorSummary());
    };
    press(Qt::Key_Right, Qt::NoModifier, 1);
    press(Qt::Key_Right, Qt::ShiftModifier, 11);
    press(Qt::Key_Left, Qt::NoModifier, 10);
    press(Qt::Key_Home, Qt::ControlModifier, 0);
    press(Qt::Key_End, Qt::ControlModifier, 11);
    QVERIFY(expected.last().contains(QStringLiteral("outfall")));
    QTest::keyClick(h.plot, Qt::Key_Right); // already at the end: nothing to announce
    QCOMPARE(h.plot->cursorNode(), 11);
    QCOMPARE(events.valuesFor(h.plot), expected);

    QTest::keyClick(h.plot, Qt::Key_Home); // plain Home is not a cursor key
    QCOMPARE(h.plot->cursorNode(), 11);
}

void TestProfilePlotKeyboard::playbackUpdatesTheValueSilently()
{
    ProfileHost h;
    h.showAndTabIn();
    a11ytest::AccessibleEventRecorder events;
    h.plot->setCurrentPeriod(1);
    QCOMPARE(events.count(QAccessible::ValueChanged, h.plot), 0);
    QVERIFY(a11ytest::accessibleFor(h.plot)
                ->text(QAccessible::Value)
                .contains(QStringLiteral("run — HGL 101.00")));
}

void TestProfilePlotKeyboard::enterSelectsAndMenuKeyOpensTheNodeMenu()
{
    ProfileHost h;
    h.showAndTabIn();
    QTest::keyClick(h.plot, Qt::Key_Right);
    QTest::keyClick(h.plot, Qt::Key_Right);
    QSignalSpy clicked(h.plot, &ProfilePlotWidget::nodeClicked);
    QSignalSpy menu(h.plot, &ProfilePlotWidget::nodeRightClicked);

    QTest::keyClick(h.plot, Qt::Key_Return);
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(clicked.first().first().toInt(), 2);

    QTest::keyClick(h.plot, Qt::Key_Menu);
    QTest::keyClick(h.plot, Qt::Key_F10, Qt::ShiftModifier);
    QTest::keyClick(h.plot, Qt::Key_F10); // plain F10 is not a menu key
    QCOMPARE(menu.count(), 2);
    QCOMPARE(menu.first().first().toInt(), 2);
    const QPoint at = h.plot->mapFromGlobal(menu.first().at(1).toPoint());
    QVERIFY(h.plot->rect().contains(at));
    QCOMPARE(h.plot->nodeIndexAt(at + QPoint(0, 2)), 2); // the menu opens on the node
}

void TestProfilePlotKeyboard::detailsProviderAddsTrackValues()
{
    ProfileHost h;
    h.plot->setNodeDetailsProvider(
        [](int node) { return QStringLiteral("Depth (ft) %1").arg(node + 0.5); });
    h.showAndTabIn();
    QTest::keyClick(h.plot, Qt::Key_Right);
    QVERIFY(h.plot->cursorSummary().endsWith(QStringLiteral("; Depth (ft) 1.5")));
    h.plot->setNodeDetailsProvider({});
    QVERIFY(h.plot->cursorSummary().endsWith(QStringLiteral("run — Max HGL 100.00")));
}

void TestProfilePlotKeyboard::cursorIsDrawnOnlyWithKeyboardFocus()
{
    ProfileHost h;
    h.host.show();
    QVERIFY(QTest::qWaitForWindowActive(&h.host));
    h.before->setFocus(Qt::OtherFocusReason);
    const QImage unfocused = h.plot->grab().toImage();

    h.plot->setFocus(Qt::MouseFocusReason); // a click on blank plot area
    QCOMPARE(h.plot->cursorNode(), -1);
    QCOMPARE(h.plot->grab().toImage(), unfocused);

    QTest::keyClick(h.plot, Qt::Key_Right);
    QCOMPARE(h.plot->cursorNode(), 0);
    const QImage withCursor = h.plot->grab().toImage();
    QVERIFY(withCursor != unfocused);

    h.before->setFocus(Qt::OtherFocusReason); // the cursor hides with focus
    QCOMPARE(h.plot->grab().toImage(), unfocused);
}

void TestProfilePlotKeyboard::clickingANodeMovesTheCursor()
{
    ProfileHost h;
    h.host.show();
    QVERIFY(QTest::qWaitForWindowActive(&h.host));
    QPoint onNode(-1, -1);
    for (int y = 0; y < h.plot->height() && onNode.x() < 0; y += 2)
        for (int x = 0; x < h.plot->width(); x += 2)
            if (h.plot->nodeIndexAt(QPoint(x, y)) == 3) {
                onNode = QPoint(x, y);
                break;
            }
    QVERIFY(onNode.x() >= 0);
    QTest::mouseClick(h.plot, Qt::LeftButton, {}, onNode);
    QCOMPARE(h.plot->cursorNode(), 3);
    QTest::keyClick(h.plot, Qt::Key_Right);
    QCOMPARE(h.plot->cursorNode(), 4);
}

void TestProfilePlotKeyboard::cursorNodeStaysInView()
{
    ProfileHost h;
    h.showAndTabIn();
    const auto &vx = h.plot->virtualChainageTable();
    QCOMPARE(vx.size(), kNodes);
    h.plot->setVisibleXRange(vx.first() - 10.0, vx.first() + 150.0);
    QTest::keyClick(h.plot, Qt::Key_End, Qt::ControlModifier);
    const QRectF visible = h.plot->visibleDataRange();
    QVERIFY(visible.left() <= vx.last() && vx.last() <= visible.right());
    QVERIFY(qAbs(visible.width() - 160.0) < 1e-6); // shifted, not zoomed
}

QTEST_MAIN(TestProfilePlotKeyboard)
#include "test_profileplot_keyboard.moc"
