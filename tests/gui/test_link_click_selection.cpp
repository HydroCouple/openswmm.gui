// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file      test_link_click_selection.cpp
 * @brief     A plain click on a link selects that link only — not its end
 *         nodes, and not a same-named node (typed_selection_fixture has a
 *         node and a link both called X1).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/tools/maptoolselect.h"
#include "project/openswmmvisworkspace.h"
#include "selection/selectionmanager.h"
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include <QDir>
#include <QFile>
#include <QTest>

namespace
{
/** @brief Locate the typed-selection click fixture.
 * @return typed_selection_fixture.inp under SWMMVIS_GUI_TEST_DATA or ".". */
QString fixturePath()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .filePath(QStringLiteral("typed_selection_fixture.inp"));
}
/** @brief Produce stable sorted typed identity diagnostics.
 * @param[in] refs Object references whose integer kind and name are formatted.
 * @return Space-separated sorted kind:name strings without modifying the selection. */
QString describe(const QSet<SWMMObjectRef> &refs)
{
    QStringList out;
    for (const auto &r : refs)
        out << QStringLiteral("%1:%2").arg(int(r.objectType)).arg(r.name);
    out.sort();
    return out.join(QLatin1Char(' '));
}
} // namespace

class TestLinkClickSelection : public QObject
{
    Q_OBJECT
    OpenSWMMVisWorkspace *ws = nullptr;
    SWMMVisProjectWindow *window = nullptr;
    QStringList log;

    /** @brief Convert current test-window map coordinates to canvas pixels.
     * @param[in] x Horizontal model/map coordinate.
     * @param[in] y Vertical model/map coordinate.
     * @return Canvas-local integer point; requires a loaded current window. */
    QPoint pixelOf(double x, double y) const
    {
        int px = 0, py = 0;
        window->canvas()->toPixelCoords(x, y, px, py);
        return {px, py};
    }
    /** @brief Clear selection and deliver a real canvas move/left click, recording bus and layer identities.
     * @param[in] at Canvas-local pixel.
     * @param[in] what Diagnostic label appended to the test log.
     * @return Copied selected object references after twenty-millisecond pre-click and fifty-millisecond post-click waits. */
    QSet<SWMMObjectRef> clickAt(const QPoint &at, const QString &what)
    {
        window->selectionManager()->clear();
        QTest::qWait(20);
        QTest::mouseMove(window->canvas(), at);
        QTest::mouseClick(window->canvas(), Qt::LeftButton, Qt::NoModifier, at);
        QTest::qWait(50);
        const auto sel = window->selectionManager()->selection();
        log << QStringLiteral("%1 -> bus {%2} layer [%3]")
                   .arg(what, describe(sel),
                        window->modelLayer()->selectedElementNames().join(QLatin1Char(',')));
        return sel;
    }

private slots:
    /** @brief Load/show the shared project, settle layout, zoom to the model and activate its selection tool. */
    void initTestCase()
    {
        ws = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        window = new SWMMVisProjectWindow(ws, fixturePath(), nullptr);
        QList<QString> warnings, errors;
        QVERIFY(window->loadModel(warnings, errors));
        window->resize(900, 700);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(200);
        // Let deferred layout/extent changes land before mapping clicks to
        // pixels; an early click maps through a stale transform.
        QTest::qWait(1000);
        window->canvas()->zoomToFullExtent();
        QTest::qWait(500);
        window->canvas()->setActiveTool(window->selectTool());
    }
    /** @brief Attempt to retain click diagnostics under the fixture-relative output folder, then destroy project/workspace.
     * Directory creation and log writing are best-effort rather than assertions. */
    void cleanupTestCase()
    {
        const QString out =
            QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
                .absoluteFilePath(QStringLiteral("../../output/link_click_selection"));
        QDir().mkpath(out);
        QFile f(out + QStringLiteral("/clicks.txt"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(log.join(QLatin1Char('\n')).toUtf8() + '\n');
        delete window;
        delete ws;
    }
    /** @brief Verify clicking the C1 interior bend selects only its typed link reference. */
    void clickOnConduitMidpointSelectsOnlyTheConduit()
    {
        const auto sel = clickAt(pixelOf(500.0, 50.0), QStringLiteral("C1 at its vertex"));
        QCOMPARE(describe(sel),
                 describe({SWMMObjectRef(SWMMObjectRef::Link, QStringLiteral("C1"))}));
    }
    /** @brief Verify clicking X1 link selects the link rather than the same-named node. */
    void clickOnSameNamedLinkSelectsTheLink()
    {
        const auto sel = clickAt(pixelOf(1500.0, 0.0), QStringLiteral("X1 link midpoint"));
        QCOMPARE(describe(sel),
                 describe({SWMMObjectRef(SWMMObjectRef::Link, QStringLiteral("X1"))}));
    }
    /** @brief Verify clicking J1 selects only its typed node reference. */
    void clickOnNodeSelectsOnlyTheNode()
    {
        const auto sel = clickAt(pixelOf(0.0, 0.0), QStringLiteral("J1 node"));
        QCOMPARE(describe(sel),
                 describe({SWMMObjectRef(SWMMObjectRef::Node, QStringLiteral("J1"))}));
    }
    // The same click inside the full main window, with every dock attached
    // (object browser, attribute table, property grid, section view, ...).
    /** @brief Verify typed link-only selection persists with all main-window docks connected.
     * Opens a real main window, checks C1/X1 clicks and delayed selection stability, then hides and intentionally leaves that window allocated because its pre-existing direct teardown crashes. */
    void mainWindowClickSelectsOnlyTheConduit()
    {
        auto *win = new SWMMVis(nullptr);
        win->resize(1400, 900);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));
        QVERIFY(QMetaObject::invokeMethod(win, "openSingleINP", Q_ARG(QString, fixturePath())));
        QTRY_VERIFY_WITH_TIMEOUT(win->findChild<SWMMVisProjectWindow *>() &&
                                     win->findChild<SWMMVisProjectWindow *>()->modelLayer(),
                                 20000);
        QTest::qWait(1500);
        SWMMVisProjectWindow *pw = win->findChild<SWMMVisProjectWindow *>();
        QVERIFY(pw);
        pw->canvas()->zoomToFullExtent();
        QTest::qWait(200);
        pw->canvas()->setActiveTool(pw->selectTool());
        auto *saved = window;
        window = pw;
        const auto conduit =
            clickAt(pixelOf(500.0, 50.0), QStringLiteral("main window: C1 at its vertex"));
        QTest::qWait(500);
        log << QStringLiteral("main window: C1 after 500 ms -> bus {%1}")
                   .arg(describe(pw->selectionManager()->selection()));
        const auto link =
            clickAt(pixelOf(1500.0, 0.0), QStringLiteral("main window: X1 link midpoint"));
        QTest::qWait(500);
        log << QStringLiteral("main window: X1 after 500 ms -> bus {%1}")
                   .arg(describe(pw->selectionManager()->selection()));
        window = saved;
        const QString c1 = describe(pw->selectionManager()->selection());
        // Deleting SWMMVis directly crashes in a status-bar units slot during
        // teardown (separate issue); hide it and let the process reclaim it.
        win->hide();
        QCOMPARE(describe(conduit),
                 describe({SWMMObjectRef(SWMMObjectRef::Link, QStringLiteral("C1"))}));
        QCOMPARE(describe(link),
                 describe({SWMMObjectRef(SWMMObjectRef::Link, QStringLiteral("X1"))}));
        QCOMPARE(c1, describe({SWMMObjectRef(SWMMObjectRef::Link, QStringLiteral("X1"))}));
    }
};

QTEST_MAIN(TestLinkClickSelection)
#include "test_link_click_selection.moc"
