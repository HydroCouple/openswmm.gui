/**
 * @file      test_ribbon_organization.cpp
 * @brief     Ribbon organization contract on the real main window (process-aware editing plan,
 *            Phase 1): one ribbon face per command, Quick Access pinned below the tab strip,
 *            no View tab, a contextual Animation tab, stacks inside the row budget, catalog
 *            tab fields that match the mounted placement, and every catalog command reachable
 *            from a menu or the ribbon.
 *
 * The catalog's `tab` field is the single declaration of where a command's ribbon face lives
 * ("strip" = Quick Access, "" = menu and palette only). These cases compare that declaration
 * with what SWMMVis actually mounts, so a regrouping that forgets the catalog (or the reverse)
 * fails here rather than in a screenshot review.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/tools/maptoolmeshselectedge.h"
#include "mesh/meshobjectref.h"
#include "mesh/meshcellgeom.h"
#include "selection/selectionmanager.h"
#include "ui/panels/propertiespanel.h"
#include "ui/panels/meshaggregateselection.h"
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "ui/actioncatalog.h"
#include "ui/actionregistry.h"
#include "ui/toolbars/compacttoolbarcontroller.h"
#include "ui/toolbars/ribbongroup.h"
#include "ui/toolbars/ribbonsplitbutton.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QHash>
#include <QMenu>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointF>
#include <QPointer>
#include <QPushButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTreeView>
#include <QSet>
#include <QTabBar>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <openswmm/engine/openswmm_model.h> // swmm_options_set_ext

using openswmmvis::ui::ActionRegistry;
using openswmmvis::ui::CompactToolbarController;
using openswmmvis::ui::kActionCatalog;
using openswmmvis::ui::kActionCatalogStrip;
using openswmmvis::ui::kRibbonIconFull;
using openswmmvis::ui::kRibbonRowHeight;
using openswmmvis::ui::RibbonGroup;
using openswmmvis::ui::RibbonSplitButton;

namespace
{

/// Width budget for the tab-specific tools to keep all labels, in addition to the
/// permanent Quick Access group. Model carries the most commands.
struct LabelledWidth {
    const char *tab;
    int window;
};
constexpr LabelledWidth kLabelledWidths[] = {
    {"home", 1600}, {"features", 1600}, {"analysis", 1600}, {"model", 1760}};

/** @brief Every command with a button face under @p host; split buttons count all members. */
QList<QAction *> facesOf(const QWidget *host)
{
    QList<QAction *> out;
    for (QToolButton *button : host->findChildren<QToolButton *>()) {
        if (auto *split = qobject_cast<RibbonSplitButton *>(button)) {
            out += split->members();
            continue;
        }
        if (QAction *action = button->defaultAction())
            out.append(action);
    }
    return out;
}

/** @brief Collect every action reachable through @p menu and its submenus. */
void collectMenuActions(const QMenu *menu, QSet<QAction *> &out)
{
    for (QAction *action : menu->actions()) {
        out.insert(action);
        if (action->menu())
            collectMenuActions(action->menu(), out);
    }
}

} // namespace

class TestRibbonOrganization : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_win = new SWMMVis(nullptr);
        m_controller = m_win->findChild<CompactToolbarController *>();
        QVERIFY2(m_controller, "no compact toolbar controller");
        m_quick = m_win->findChild<QWidget *>(QStringLiteral("ribbonQuickAccess"));
        QVERIFY2(m_quick, "the Quick Access cluster was never built");
        QTest::qWait(50);
    }

    void cleanupTestCase()
    {
        // Close before deleting (as test_featurelayer_hostwiring does): deleting a
        // shown main window lets the MDI area emit subWindowActivated while it is
        // half destroyed, and the Window-menu rebuild then reads it.
        closeProjects(); // a failed check can leave the status test's project open
        m_win->close();
        delete m_win;
        m_win = nullptr;
    }

    /** @brief Seven tabs; View is gone; Terrain, Mesh 2D and Animation start hidden. */
    void tabsAreTheOrganizedSet()
    {
        const QStringList ids = m_controller->tabIds();
        for (const char *id : {"home", "model", "terrain", "mesh2d", "features", "analysis",
                               "results"})
            QVERIFY2(ids.contains(QLatin1String(id)), id);
        QVERIFY2(!ids.contains(QStringLiteral("view")), "the View tab is still mounted");
        // No project is open, so none of the contextual tabs has content.
        QVERIFY(!m_controller->isTabVisible(QStringLiteral("terrain")));
        QVERIFY(!m_controller->isTabVisible(QStringLiteral("mesh2d")));
        QVERIFY2(!m_controller->isTabVisible(QStringLiteral("results")),
                 "Animation must stay hidden until a results layer exists");
    }

    /** @brief Large Select ▾, Pan, Full Extent, Undo and Redo stay below every tab. */
    void quickAccessLeadsTheToolsRow()
    {
        QStringList names;
        for (QAction *action : facesOf(m_quick))
            names << action->objectName();
        const QStringList expected = {QStringLiteral("actionSelect"),
                                      QStringLiteral("actionSelectByPolygon"),
                                      QStringLiteral("actionPan"),
                                      QStringLiteral("actionZoomExtent"),
                                      QStringLiteral("actionUndo"),
                                      QStringLiteral("actionRedo")};
        QCOMPARE(names, expected);

        auto *strip = m_win->findChild<QToolBar *>(QStringLiteral("toolBarTabStrip"));
        auto *tabs = m_win->findChild<QTabBar *>(QStringLiteral("compactToolbarTabBar"));
        auto *quickBar = m_win->findChild<QToolBar *>(QStringLiteral("toolBarQuickAccess"));
        QVERIFY(strip && tabs && quickBar);
        QVERIFY(!strip->isAncestorOf(m_quick));
        QVERIFY(quickBar->isAncestorOf(m_quick));
        QVERIFY(!quickBar->isMovable());
        QVERIFY(!quickBar->isFloatable());
        const auto buttons = m_quick->findChildren<QToolButton *>();
        QCOMPARE(buttons.size(), 5);
        for (QToolButton *button : buttons) {
            QCOMPARE(button->iconSize(), QSize(kRibbonIconFull, kRibbonIconFull));
            QVERIFY(button->width() >= 44);
            QCOMPARE(button->height(), 44);
        }

        m_win->resize(1600, 900);
        m_win->show();
        const QString previous = m_controller->currentTab();
        for (const QString &id : m_controller->tabIds()) {
            const bool wasVisible = m_controller->isTabVisible(id);
            m_controller->setTabVisible(id, true);
            m_controller->setCurrentTab(id);
            QTRY_VERIFY(m_quick->isVisibleTo(m_win));
            QTRY_VERIFY(quickBar->geometry().top() > strip->geometry().bottom());
            QTRY_VERIFY(quickBar->x() == strip->x());
            // The bigger buttons must never enlarge the tab-strip row.
            QVERIFY(strip->height() < buttons.first()->height());
            for (QToolButton *button : buttons)
                QTRY_VERIFY(button->isVisibleTo(m_win));
            m_controller->setTabVisible(id, wasVisible);
        }
        m_controller->setCurrentTab(previous);
    }

    /** @brief No command has two ribbon faces (Quick Access included in the count). */
    void everyCommandHasOneRibbonFace()
    {
        QHash<QAction *, int> faces;
        for (QAction *action : facesOf(m_quick))
            ++faces[action];
        for (const QString &id : m_controller->tabIds())
            for (QToolBar *bar : m_controller->toolbarsForTab(id))
                if (bar)
                    for (QAction *action : facesOf(bar))
                        ++faces[action];
        QStringList duplicated;
        for (auto it = faces.cbegin(); it != faces.cend(); ++it)
            if (it.value() > 1)
                duplicated << QStringLiteral("%1 (%2 faces)")
                                  .arg(it.key()->objectName().isEmpty() ? it.key()->text()
                                                                        : it.key()->objectName())
                                  .arg(it.value());
        duplicated.sort();
        QVERIFY2(duplicated.isEmpty(), qPrintable(duplicated.join(QStringLiteral(", "))));
    }

    /** @brief The catalog `tab` field names exactly where each registered command is mounted. */
    void catalogTabMatchesPlacement()
    {
        QHash<QAction *, QSet<QString>> placed;
        for (QAction *action : facesOf(m_quick))
            placed[action].insert(QString::fromLatin1(kActionCatalogStrip));
        for (const QString &id : m_controller->tabIds())
            for (QToolBar *bar : m_controller->toolbarsForTab(id))
                if (bar)
                    for (QAction *action : facesOf(bar))
                        placed[action].insert(id);

        auto *registry = ActionRegistry::instance();
        QStringList mismatched;
        for (const auto &entry : kActionCatalog) {
            QAction *action = registry->action(QString::fromLatin1(entry.id));
            if (!action)
                continue;
            const QString declared = QString::fromLatin1(entry.tab);
            const QSet<QString> actual = placed.value(action);
            const bool ok = declared.isEmpty() ? actual.isEmpty()
                                               : (actual.size() == 1 && actual.contains(declared));
            if (!ok) {
                QStringList where(actual.cbegin(), actual.cend());
                where.sort();
                mismatched << QStringLiteral("%1 declares '%2', mounted on [%3]")
                                  .arg(QLatin1String(entry.id), declared,
                                       where.join(QLatin1Char(' ')));
            }
        }
        QVERIFY2(mismatched.isEmpty(), qPrintable(mismatched.join(QStringLiteral("\n"))));
    }

    /** @brief Every registered catalog command is in a menu or has a ribbon face. */
    void everyCatalogCommandIsReachable()
    {
        QVERIFY(!m_win->findChild<QAction *>(QStringLiteral("actionMeshBoundaryCondition")));
        QVERIFY(!ActionRegistry::instance()->action(QStringLiteral("mesh.boundaryCondition")));
        QSet<QAction *> reachable;
        for (QAction *top : m_win->menuBar()->actions()) {
            reachable.insert(top);
            if (top->menu())
                collectMenuActions(top->menu(), reachable);
        }
        for (QAction *action : facesOf(m_quick))
            reachable.insert(action);
        for (const QString &id : m_controller->tabIds())
            for (QToolBar *bar : m_controller->toolbarsForTab(id))
                if (bar)
                    for (QAction *action : facesOf(bar))
                        reachable.insert(action);

        // File → Open Recent is rebuilt from the recent-files list, with a
        // fresh "Clear Recent Files" entry only while the list has entries
        // (the Welcome page offers the same command). The catalog action
        // therefore never sits in a menu; it stays reachable from the palette.
        const QSet<QString> rebuiltMenuOnly = {QStringLiteral("file.clearRecent")};
        auto *registry = ActionRegistry::instance();
        QStringList unreachable;
        for (const auto &entry : kActionCatalog) {
            if (rebuiltMenuOnly.contains(QString::fromLatin1(entry.id)))
                continue;
            QAction *action = registry->action(QString::fromLatin1(entry.id));
            if (action && !reachable.contains(action))
                unreachable << QString::fromLatin1(entry.id);
        }
        QVERIFY2(unreachable.isEmpty(), qPrintable(unreachable.join(QStringLiteral(", "))));
    }

    /** @brief Stack columns hold at most three rows and fit under the group caption. */
    void stacksFitTheRowBudget()
    {
        int columns = 0;
        for (const QString &id : m_controller->tabIds()) {
            for (QToolBar *bar : m_controller->toolbarsForTab(id)) {
                if (!bar)
                    continue;
                for (auto *column : bar->findChildren<QWidget *>(
                         QStringLiteral("ribbonStackColumn"))) {
                    ++columns;
                    const int rows = column->findChildren<QToolButton *>().size();
                    QVERIFY2(rows >= 1 && rows <= 3,
                             qPrintable(QStringLiteral("%1: %2 rows").arg(id).arg(rows)));
                    QVERIFY2(column->sizeHint().height() <= kRibbonRowHeight - 22,
                             qPrintable(QStringLiteral("%1: stack %2 px over budget")
                                            .arg(id)
                                            .arg(column->sizeHint().height())));
                }
            }
        }
        QVERIFY2(columns > 0, "no stack columns were mounted");
    }

    /** @brief Keep labels at the existing tab-specific width budgets plus the permanent group.
     * Set RIBBON_WIDTH_DEBUG for per-group widths. */
    void labelsRenderAtCommonWindowWidth()
    {
        m_win->show();
        QStringList report;
        QStringList iconOnly;
        auto *quickBar = m_win->findChild<QToolBar *>(QStringLiteral("toolBarQuickAccess"));
        QVERIFY(quickBar);
        for (const auto &target : kLabelledWidths) {
            const QString id = QLatin1String(target.tab);
            const int windowWidth = target.window + quickBar->minimumWidth();
            m_win->resize(windowWidth, 900);
            m_controller->setCurrentTab(id);
            QCoreApplication::processEvents();
            QTest::qWait(50); // the compactor solves on a zero timer
            int full = 0;
            for (QToolBar *bar : m_controller->toolbarsForTab(id)) {
                if (!bar)
                    continue;
                for (auto *group : bar->findChildren<RibbonGroup *>(Qt::FindDirectChildrenOnly)) {
                    const auto widths = group->groupWidths();
                    full += widths.full;
                    if (group->mode() != openswmmvis::ui::RibbonMode::Full)
                        iconOnly << QStringLiteral("%1/%2 at %3 px")
                                        .arg(id, group->caption())
                                        .arg(windowWidth);
                    if (qEnvironmentVariableIsSet("RIBBON_WIDTH_DEBUG"))
                        qInfo().noquote() << id << group->caption() << "full" << widths.full
                                          << "compact" << widths.compact;
                }
            }
            report << QStringLiteral("%1 %2 px").arg(id).arg(full);
        }
        qInfo().noquote() << "labeled widths:" << report.join(QStringLiteral(", "));
        QVERIFY2(iconOnly.isEmpty(), qPrintable(QStringLiteral("groups without labels: %1")
                                                    .arg(iconOnly.join(QStringLiteral(", ")))));
    }

    /** @brief The Panels button opens the View → Panels menu, the home of every dock toggle. */
    void panelsButtonOpensThePanelsMenu()
    {
        auto *home = m_win->findChild<QToolBar *>(QStringLiteral("toolBarHome"));
        auto *menu = m_win->findChild<QMenu *>(QStringLiteral("menuViewPanels"));
        QVERIFY(home && menu);
        QToolButton *panels = nullptr;
        for (QToolButton *button : home->findChildren<QToolButton *>())
            if (button->defaultAction() &&
                button->defaultAction()->objectName() == QLatin1String("actionPanelsMenu"))
                panels = button;
        QVERIFY2(panels, "no Panels button on the Home tab");
        QCOMPARE(panels->defaultAction()->menu(), menu);
        QCOMPARE(panels->popupMode(), QToolButton::InstantPopup);
    }

    /** @brief The Mesh 2D process labels follow the active project's options: the
     *  host's status provider reads them each time Model → Mesh → Process opens.
     *  Runs last: it opens a project on the shared window and closes it again. */
    void processStatusFollowsProjectOptions()
    {
        QVERIFY(QMetaObject::invokeMethod(m_win, "onNewProject", Qt::DirectConnection));
        const auto projects = m_win->findChildren<SWMMVisProjectWindow *>();
        QCOMPARE(projects.size(), 1);
        QPointer<SWMMVisProjectWindow> project = projects.first();
        QTRY_VERIFY(project->modelLayer()->engine());
        auto *mdi = m_win->findChild<QMdiArea *>();
        QVERIFY(mdi);
        QTRY_COMPARE(mdi->activeSubWindow(), static_cast<QMdiSubWindow *>(project.data()));
        SWMMModelLayer *model = project->modelLayer();

        auto *menu = m_win->findChild<QMenu *>(QStringLiteral("menuModelMeshProcess"));
        QVERIFY(menu);
        const auto label = [&](const char *key) {
            emit menu->aboutToShow(); // the refresh the menu does on open
            auto *action =
                m_win->findChild<QAction *>(QStringLiteral("actionMeshProcess") +
                                            QLatin1String(key));
            return action ? action->text() : QStringLiteral("<missing>");
        };
        const QString off = QStringLiteral(" — off");

        // A new project: no pollutants, heat transport off, snow and 2D GW on.
        QCOMPARE(label("SurfaceQuality"), QStringLiteral("Surface Quality — no pollutants"));
        QCOMPARE(label("Heat"), QStringLiteral("Heat") + off);
        QCOMPARE(label("Snow"), QStringLiteral("Snow"));
        QCOMPARE(label("Groundwater"), QStringLiteral("Groundwater"));
        QCOMPARE(label("SurfaceFlow"), QStringLiteral("Surface Flow"));

        QVERIFY(model->setOption("IGNORE_QUALITY", QStringLiteral("YES")));
        QVERIFY(model->setOption("IGNORE_SNOWMELT", QStringLiteral("YES")));
        QVERIFY(model->setOption("HEAT_TRANSPORT", QStringLiteral("YES")));
        // A [2D_OPTIONS] key: the Simulation Options 2D page writes it this way.
        QCOMPARE(swmm_options_set_ext(model->engine(), "GROUNDWATER", "NO"), 0);
        QCOMPARE(label("SurfaceQuality"), QStringLiteral("Surface Quality") + off);
        QCOMPARE(label("Snow"), QStringLiteral("Snow") + off);
        QCOMPARE(label("Heat"), QStringLiteral("Heat"));
        QCOMPARE(label("Groundwater"), QStringLiteral("Groundwater") + off);

        closeProjects();
        QTRY_VERIFY(!project);
    }

    /** @brief Process locks (plan §5, Phase 4): a switched-off process disables the commands
     *  that only write its records, with the reason on hover; its editors stay openable (they
     *  open read-only) and switching it back on re-enables everything without a restart. */
    void processLocksGateWriteOnlyCommands()
    {
        QVERIFY(QMetaObject::invokeMethod(m_win, "onNewProject", Qt::DirectConnection));
        const auto projects = m_win->findChildren<SWMMVisProjectWindow *>();
        QCOMPARE(projects.size(), 1);
        QPointer<SWMMVisProjectWindow> project = projects.first();
        QTRY_VERIFY(project->modelLayer()->engine());
        auto *mdi = m_win->findChild<QMdiArea *>();
        QTRY_COMPARE(mdi->activeSubWindow(), static_cast<QMdiSubWindow *>(project.data()));
        SWMMModelLayer *model = project->modelLayer();
        auto *registry = ActionRegistry::instance();
        const auto command = [registry](const char *id) {
            QAction *action = registry->action(QString::fromLatin1(id));
            return action;
        };
        const char *writers[] = {"mesh.assignGroundwater", "mesh.assignSurfaceQuality"};
        // Editors open read-only instead; snow and heat are set up before they are switched
        // on (the engine reads IGNORE_SNOWMELT YES for any model without a snowpack).
        const char *editors[] = {"mesh.gw2dParams",     "mesh.gw2dEdgeBoundaries",
                                 "mesh.gw2dNodeBeds",   "mesh.gw2dInitCond",
                                 "mesh.surfaceQuality2d", "mesh.snow",
                                 "mesh.heatFromRaster"};
        for (const char *id : writers)
            QVERIFY2(command(id), id);
        for (const char *id : editors)
            QVERIFY2(command(id), id);

        // A new project: groundwater AUTO (authoring rows is how it turns on), quality on,
        // heat off — heat never locks, so its raster assignment stays usable.
        for (const char *id : writers)
            QVERIFY2(command(id)->isEnabled(), id);
        QVERIFY(command("mesh.heatFromRaster")->isEnabled());

        QVERIFY(model->setOption("IGNORE_QUALITY", QStringLiteral("YES")));
        QVERIFY(model->setOption("IGNORE_SNOWMELT", QStringLiteral("YES")));
        QCOMPARE(swmm_options_set_ext(model->engine(), "GROUNDWATER", "NO"), 0);
        emit model->optionsChanged({QStringLiteral("GROUNDWATER")});
        const QHash<QString, QString> reasons = {
            {QStringLiteral("mesh.assignGroundwater"), QStringLiteral("2D groundwater is off.")},
            {QStringLiteral("mesh.assignSurfaceQuality"), QStringLiteral("Water quality is off.")}};
        for (const char *id : writers) {
            const QString key = QString::fromLatin1(id);
            QVERIFY2(!command(id)->isEnabled(), id);
            QVERIFY2(command(id)->toolTip().contains(reasons.value(key)),
                     qPrintable(command(id)->toolTip()));
            QVERIFY2(registry->disabledReason(key).startsWith(reasons.value(key)),
                     qPrintable(registry->disabledReason(key)));
        }
        for (const char *id : editors)
            QVERIFY2(command(id)->isEnabled(), id);

        QVERIFY(model->setOption("IGNORE_QUALITY", QStringLiteral("NO")));
        QVERIFY(model->setOption("IGNORE_SNOWMELT", QStringLiteral("NO")));
        QCOMPARE(swmm_options_set_ext(model->engine(), "GROUNDWATER", "YES"), 0);
        emit model->optionsChanged({QStringLiteral("GROUNDWATER")});
        for (const char *id : writers) {
            QVERIFY2(command(id)->isEnabled(), id);
            QVERIFY2(!command(id)->toolTip().contains(QStringLiteral(" is off.")),
                     qPrintable(command(id)->toolTip()));
        }

        closeProjects();
        QTRY_VERIFY(!project);
    }

    /** @brief Host wiring for aggregate editing (process-aware plan Phase 3): selecting several
     *  mesh cells in a project shows them in Properties as one aggregate, not an empty panel. */
    void propertiesAggregatesSeveralMeshCells()
    {
        QVERIFY(QMetaObject::invokeMethod(m_win, "onNewProject", Qt::DirectConnection));
        const auto projects = m_win->findChildren<SWMMVisProjectWindow *>();
        QCOMPARE(projects.size(), 1);
        QPointer<SWMMVisProjectWindow> project = projects.first();
        QTRY_VERIFY(project->modelLayer()->engine());
        auto *mdi = m_win->findChild<QMdiArea *>();
        QTRY_COMPARE(mdi->activeSubWindow(), static_cast<QMdiSubWindow *>(project.data()));

        mesh::MeshResult strip; // two unit squares, four cells
        for (int x = 0; x <= 2; ++x)
            for (int y = 0; y <= 1; ++y) {
                mesh::MeshVertex v;
                v.xy = QPointF(x, y);
                strip.vertices.append(v);
            }
        for (int x = 0; x < 2; ++x) {
            const int b0 = x * 2, t0 = b0 + 1, b1 = b0 + 2, t1 = b0 + 3;
            mesh::MeshTriangle a, c;
            a.v0 = b0, a.v1 = b1, a.v2 = t1;
            c.v0 = b0, c.v1 = t1, c.v2 = t0;
            strip.triangles << a << c;
        }
        strip.ok = true;
        auto *layer = new SWMM2DMeshLayer(strip, QStringLiteral("aggregate_host.2dm"));
        project->canvas()->addLayer(layer, false);

        auto *panel = m_win->findChild<PropertiesPanel *>();
        QVERIFY(panel);
        QSet<SWMMObjectRef> cells;
        for (int t = 0; t < 3; ++t)
            cells.insert(mesh::MeshObjectRef::cell(layer->sourcePath(), t));
        project->selectionManager()->select(cells, SelectionManager::Mode::Replace);
        QTRY_COMPARE(panel->windowTitle(), QStringLiteral("2D mesh: 3 cells"));
        QVERIFY(!panel->findChild<QPushButton *>(QStringLiteral("meshAggregateApply")));
        QVERIFY(!panel->findChild<QPushButton *>(QStringLiteral("meshAggregateCancel")));

        project->selectionManager()->select(
            QSet<SWMMObjectRef>{mesh::MeshObjectRef::cell(layer->sourcePath(), 0)},
            SelectionManager::Mode::Replace);
        QTRY_COMPARE(panel->windowTitle(), QStringLiteral("2D Cell 0"));

        // Exercise a visible dock in the real host, including focus changes
        // while committing directly from the field editor.
        m_win->resize(1600, 1000);
        m_win->show();
        panel->show();
        QTest::qWait(30);
        // The real map tool's rectangle drives the full Properties route.
        auto *canvas = project->canvas();
        canvas->setExtent(MapExtent(-0.2, -0.2, 2.2, 1.2), false);
        MapToolMeshSelectEdge edgeTool(canvas, project->selectionManager());
        edgeTool.setBoundaryOnly(true);
        edgeTool.activate();
        const auto mouse = [&](QEvent::Type type, double x, double y) {
            int px = 0, py = 0;
            edgeTool.toPixelCoords(x, y, px, py);
            const QPointF point(px, py);
            QMouseEvent event(type, point, point, Qt::LeftButton,
                             type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                             Qt::NoModifier);
            if (type == QEvent::MouseButtonPress) edgeTool.mousePressEvent(&event);
            if (type == QEvent::MouseMove) edgeTool.mouseMoveEvent(&event);
            if (type == QEvent::MouseButtonRelease) edgeTool.mouseReleaseEvent(&event);
        };
        mouse(QEvent::MouseButtonPress, -0.1, -0.1);
        mouse(QEvent::MouseMove, 2.1, 0.1);
        mouse(QEvent::MouseButtonRelease, 2.1, 0.1);
        edgeTool.deactivate();
        QCOMPARE(project->selectionManager()->size(), 2);
        QTRY_COMPARE(panel->windowTitle(), QStringLiteral("2D mesh: 2 edges"));
        auto *tree = panel->findChild<QTreeView *>();
        QVERIFY(tree);
        const auto field = [tree](const QString &label) {
            auto *model = tree->model();
            for (int g = 0; g < model->rowCount(); ++g) {
                const auto group = model->index(g, 0);
                for (int r = 0; r < model->rowCount(group); ++r)
                    if (model->index(r, 0, group).data().toString() == label)
                        return model->index(r, 1, group);
            }
            return QModelIndex();
        };
        const auto before = layer->edgeBCs();
        const auto selectedEdges = project->selectionManager()->selection();
        const int undoBefore = canvas->undoStack()->index();
        tree->expandAll();
        const auto typeField = field("BC Type");
        QVERIFY(typeField.isValid());
        tree->scrollTo(typeField);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                          tree->visualRect(typeField).center());
        auto *type = tree->findChild<QComboBox *>();
        QVERIFY(type);
        const auto normal = type->model()->index(type->findData(int(mesh::MeshBCTypes::Type::NormalFlow)), 0);
        type->view()->scrollTo(normal);
        QTest::mouseClick(type->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          type->view()->visualRect(normal).center());
        const auto slopeField = field("Friction Slope");
        QVERIFY(slopeField.flags() & Qt::ItemIsEditable);
        tree->scrollTo(slopeField);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                          tree->visualRect(slopeField).center());
        auto *slope = tree->findChild<QDoubleSpinBox *>();
        QVERIFY(slope);
        slope->setFocus();
        slope->selectAll();
        QTest::keyClicks(slope, "0.005");
        QTest::keyClick(slope, Qt::Key_Return);
        QTRY_COMPARE(canvas->undoStack()->index(), undoBefore + 2);
        auto *aggregate = panel->findChild<MeshAggregateSelection *>();
        QVERIFY(aggregate);
        QVERIFY(aggregate->stagedColumns().isEmpty());
        for (const auto &ref : project->selectionManager()->selection()) {
            QString key;
            int cell = -1, local = -1;
            QVERIFY(mesh::MeshObjectRef::parseEdge(ref, &key, &cell, &local));
            QCOMPARE(layer->edgeBCs()[mesh::edgeSlot(cell, local)].slope, 0.005);
        }
        // Switching away and selecting one edited edge must read the applied
        // record from the shared mesh, independently of the aggregate draft.
        project->selectionManager()->clear();
        project->selectionManager()->select({*selectedEdges.cbegin()}, SelectionManager::Replace);
        QCoreApplication::processEvents();
        QCOMPARE(field("BC Type").data(Qt::EditRole).toInt(), int(mesh::MeshBCTypes::Type::NormalFlow));
        QCOMPARE(field("Friction Slope").data(Qt::EditRole).toDouble(), .005);
        canvas->undoStack()->undo();
        canvas->undoStack()->undo();
        QCOMPARE(layer->edgeBCs(), before);

        project->selectionManager()->clear();
        closeProjects();
        QTRY_VERIFY(!project);
    }

private:
    /** @brief Close every project window, answering Discard to the prompt an
     *  untitled project always raises; the windows delete themselves on close. */
    void closeProjects()
    {
        QTimer discard;
        discard.setInterval(10);
        connect(&discard, &QTimer::timeout, this, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                if (auto *button = box->button(QMessageBox::Discard))
                    button->click();
        });
        discard.start();
        for (auto *project : m_win->findChildren<SWMMVisProjectWindow *>())
            project->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    SWMMVis *m_win = nullptr;
    CompactToolbarController *m_controller = nullptr;
    QWidget *m_quick = nullptr;
};

QTEST_MAIN(TestRibbonOrganization)
#include "test_ribbon_organization.moc"
