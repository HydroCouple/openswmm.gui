/*!
 * \file   test_drawtool_doubleclick.cpp
 * \brief  Double-click finishes a drawing INCLUDING the clicked point.
 *
 * Qt delivers `Press, Release, DblClick, Release` for a double-click: the
 * second click arrives as MouseButtonDblClick, never as a second
 * MouseButtonPress, and MapCanvas::mouseDoubleClickEvent forwards to the tool
 * without calling the QWidget base, so no press is synthesised either. Exactly
 * ONE press reaches the tool, and it has already appended the vertex at the
 * double-clicked location.
 *
 * The draw tools nevertheless stripped that vertex as a "duplicate", which
 * silently dropped the user's closing point. Worse, it pushed short shapes
 * below their commit floor: a triangle drawn as click-click-doubleclick left
 * 2 vertices, and OpenSWMMVisMapToolAddSubcatchment::commit() requires 3, so
 * nothing was created at all and no error was shown.
 *
 * There was no double-click coverage anywhere in the repo before this file —
 * which is why the bug shipped. These gates drive the real tools on a real
 * MapCanvas under the offscreen QPA, following tests/gui/test_asyncload.cpp.
 */
#include <QtTest>

#include "core/preferencesmanager.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/tools/maptooladdsubcatchment.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"

#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QPoint>
#include <QPointF>
#include <QTime>
#include <QVector>

class TestDrawToolDoubleClick : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void doubleClickKeepsTheClickedVertex();
    void triangleFromThreeGesturesIsCreated();
    void doubleClickInPlaceDoesNotDuplicateAVertex();

private:
    OpenSWMMVisWorkspace   *m_workspace = nullptr;
    SWMMVisProjectWindow   *m_window    = nullptr;
    MapCanvas              *m_canvas    = nullptr;
    SWMMModelLayer         *m_layer     = nullptr;

    /*! Subcatchment count, so each gate reads its own effect. */
    int catchmentCount() const { return m_layer->cachedSubcatchCount(); }
};

void TestDrawToolDoubleClick::initTestCase()
{
    m_workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    QVERIFY(m_workspace != nullptr);

    SWMMModelLayer::NewProjectSpec spec;
    spec.name          = QStringLiteral("Untitled");
    spec.forNewEngine  = true;
    spec.startDateTime = QDateTime(QDate(2026, 9, 8), QTime(0, 0));
    spec.endDateTime   = spec.startDateTime.addSecs(3600);
    spec.sim           = PreferencesManager::SimulationDefaults{};
    spec.sim.flowUnits = QStringLiteral("CFS");     // → Local (ft)
    spec.twoD          = PreferencesManager::TwoDDefaults{};

    m_window = new SWMMVisProjectWindow(m_workspace, QString(), nullptr);
    m_window->markUntitled();
    QList<QString> warnings, errors;
    QVERIFY2(m_window->initializeBlankModel(spec, warnings, errors),
             qPrintable(errors.join(QStringLiteral("; "))));

    m_window->resize(900, 700);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window));
    QTest::qWait(150);

    m_canvas = m_window->canvas();
    m_layer  = m_window->modelLayer();
    QVERIFY(m_canvas && m_layer);
    QVERIFY2(m_canvas->width() > 200 && m_canvas->height() > 200,
             qPrintable(QStringLiteral("canvas %1x%2")
                            .arg(m_canvas->width()).arg(m_canvas->height())));
}

void TestDrawToolDoubleClick::cleanupTestCase()
{
    delete m_window;
    delete m_workspace;
}

// ---------------------------------------------------------------------------
// The reported bug: the point under the double-click must become a vertex.
// Four gestures (3 clicks + a double-click) must yield a FOUR-corner polygon.
// ---------------------------------------------------------------------------
void TestDrawToolDoubleClick::doubleClickKeepsTheClickedVertex()
{
    const int before = catchmentCount();

    const QPoint p1(200, 200);
    const QPoint p2(400, 200);
    const QPoint p3(400, 400);
    const QPoint p4(200, 400);      // the double-clicked corner

    {
        OpenSWMMVisMapToolAddSubcatchment tool(m_canvas);
        m_canvas->setActiveTool(&tool);
        QTest::mouseClick (m_canvas, Qt::LeftButton, Qt::NoModifier, p1);
        QTest::mouseClick (m_canvas, Qt::LeftButton, Qt::NoModifier, p2);
        QTest::mouseClick (m_canvas, Qt::LeftButton, Qt::NoModifier, p3);
        QTest::mouseDClick(m_canvas, Qt::LeftButton, Qt::NoModifier, p4);
        m_canvas->setActiveTool(nullptr);
    }

    QCOMPARE(catchmentCount(), before + 1);

    const QVector<QPointF> poly =
        m_layer->cachedSubcatchVertices(catchmentCount() - 1);
    QVERIFY2(poly.size() == 4,
             qPrintable(QStringLiteral("expected 4 vertices, got %1 — the "
                                       "double-clicked corner was dropped")
                            .arg(poly.size())));

    // …and it is the corner the user actually double-clicked, not some other
    // point: compare against the click transformed into layer coordinates.
    double cx = 0, cy = 0, lx = 0, ly = 0;
    m_canvas->toMapCoords(p4.x(), p4.y(), cx, cy);
    m_layer->transformCanvasToLayer(cx, cy, lx, ly);
    const QPointF last = poly.last();
    QVERIFY2(qAbs(last.x() - lx) < 1e-6 * qMax(1.0, qAbs(lx))
             && qAbs(last.y() - ly) < 1e-6 * qMax(1.0, qAbs(ly)),
             qPrintable(QStringLiteral("final vertex (%1,%2) is not the "
                                       "double-clicked point (%3,%4)")
                            .arg(last.x()).arg(last.y()).arg(lx).arg(ly)));
}

// ---------------------------------------------------------------------------
// The silent-failure case. Three gestures is the minimum polygon a user can
// draw; before the fix it committed nothing, with no message anywhere.
// ---------------------------------------------------------------------------
void TestDrawToolDoubleClick::triangleFromThreeGesturesIsCreated()
{
    const int before = catchmentCount();

    {
        OpenSWMMVisMapToolAddSubcatchment tool(m_canvas);
        m_canvas->setActiveTool(&tool);
        QTest::mouseClick (m_canvas, Qt::LeftButton, Qt::NoModifier, QPoint(500, 200));
        QTest::mouseClick (m_canvas, Qt::LeftButton, Qt::NoModifier, QPoint(650, 200));
        QTest::mouseDClick(m_canvas, Qt::LeftButton, Qt::NoModifier, QPoint(650, 350));
        m_canvas->setActiveTool(nullptr);
    }

    QVERIFY2(catchmentCount() == before + 1,
             "click, click, double-click created no subcatchment at all — the "
             "dropped vertex left only 2, below commit()'s 3-vertex floor");
    QCOMPARE(m_layer->cachedSubcatchVertices(catchmentCount() - 1).size(), 3);
}

// ---------------------------------------------------------------------------
// The one genuine duplicate: a single click, then a double-click in the same
// spot. Both presses snap to the same point, so the trailing copy is stripped.
// ---------------------------------------------------------------------------
void TestDrawToolDoubleClick::doubleClickInPlaceDoesNotDuplicateAVertex()
{
    const int before = catchmentCount();
    const QPoint corner(300, 550);

    {
        OpenSWMMVisMapToolAddSubcatchment tool(m_canvas);
        m_canvas->setActiveTool(&tool);
        QTest::mouseClick (m_canvas, Qt::LeftButton, Qt::NoModifier, QPoint(150, 500));
        QTest::mouseClick (m_canvas, Qt::LeftButton, Qt::NoModifier, QPoint(300, 500));
        QTest::mouseClick (m_canvas, Qt::LeftButton, Qt::NoModifier, corner);
        QTest::mouseDClick(m_canvas, Qt::LeftButton, Qt::NoModifier, corner);
        m_canvas->setActiveTool(nullptr);
    }

    QCOMPARE(catchmentCount(), before + 1);
    const QVector<QPointF> poly =
        m_layer->cachedSubcatchVertices(catchmentCount() - 1);
    QVERIFY2(poly.size() == 3,
             qPrintable(QStringLiteral("expected 3 distinct vertices, got %1 — "
                                       "a coincident vertex was kept")
                            .arg(poly.size())));
}

QTEST_MAIN(TestDrawToolDoubleClick)
#include "test_drawtool_doubleclick.moc"
