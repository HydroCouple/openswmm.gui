/*!
 * \file   test_a11y_scene_exposure.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  The map's QGraphicsScene must not be enumerated by the platform
 *         accessibility bridge.
 *
 *         A live session reached 9.6-10.6 GB with `heap` attributing 4.5 GB to
 *         93.8 million QMacAccessibilityElement objects allocated by
 *         libqcocoa, alongside ~800 MB of NSMutableArray storage holding them.
 *         The map scene carries one QGraphicsItem per network element
 *         (122,917 QGraphicsPathItem + 43,861 QGraphicsEllipseItem on West
 *         Whiteland), and macOS re-walks the accessibility tree on every window
 *         activation — which is why switching tabs was the trigger, and why
 *         dwelling on a tab made the next switch worse.
 *
 *         Exposing map geometry to a screen reader has no value: no assistive
 *         client can do anything with 122k individually-announced pipe
 *         segments. Dialogs and controls stay fully accessible; only the
 *         scene-bearing view is made a leaf.
 *
 *         This test both PINS that contract and documents the measurement:
 *         it fails loudly if a future change re-exposes the scene.
 */
#include "map/openswmmvisgraphicsview.h"
#include "map/openswmmvisscene.h"

#include <QAccessible>
#include <QAccessibleInterface>
#include <QApplication>
#include <QGraphicsEllipseItem>
#include <QStandardItemModel>
#include <QTableView>
#include <QGraphicsScene>
#include <QObject>
#include <QTest>

class TestA11ySceneExposure : public QObject
{
    Q_OBJECT
private slots:
    void sceneItemsAreNotAccessibleChildren();
    void reportTableAccessibleChildCount();
};

// Build a scene with many items and assert the accessible interface for the
// view does not enumerate them. Without the fix the child count tracks the
// item count, and every window activation mints one platform wrapper per
// child — the allocation storm the heap dump caught.
void TestA11ySceneExposure::sceneItemsAreNotAccessibleChildren()
{
    // isActive() cannot be forced under the offscreen platform (there is no
    // platform bridge to engage), but queryAccessibleInterface still builds
    // the interface, and childCount() is what the bridge would walk.
    QAccessible::setActive(true);

    OpenSWMMVisScene scene;
    OpenSWMMVisGraphicsView view(&scene, nullptr);

    constexpr int kItems = 5000;
    for (int i = 0; i < kItems; ++i)
        scene.addItem(new QGraphicsEllipseItem(i, i, 1.0, 1.0));
    QCOMPARE(scene.items().size(), kItems);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY2(iface, "the view should still expose an accessible interface");

    // The view itself remains announceable (it keeps a role and a name) —
    // what must not happen is the scene's contents becoming children.
    const int children = iface->childCount();
    QVERIFY2(children < 100,
             qPrintable(QStringLiteral("accessible childCount() = %1 for a scene of "
                                       "%2 items; the scene must not be enumerated")
                            .arg(children).arg(kItems)));
}

// Diagnostic: a QTableView the size of the Attribute Table on a large model.
// QAccessibleTable materialises one accessible cell per visible cell and the
// macOS bridge wraps each in a QMacAccessibilityElement. This slot reports the
// count rather than asserting a threshold — it exists to locate the source of
// the ~94 million wrappers seen in the field, and to make the number visible
// if it ever changes.
void TestA11ySceneExposure::reportTableAccessibleChildCount()
{
    QAccessible::setActive(true);

    QStandardItemModel model(41898, 20);
    QTableView view;
    view.setModel(&model);
    view.resize(1200, 800);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(iface);
    qInfo() << "table rows=" << model.rowCount()
            << "cols=" << model.columnCount()
            << "accessible childCount=" << iface->childCount();
}

QTEST_MAIN(TestA11ySceneExposure)
#include "test_a11y_scene_exposure.moc"
