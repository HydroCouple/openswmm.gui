/*!
 * \file swmmvisactions.cpp
 *
 * UI redesign P1 — SWMMVis::registerActions(): the single adoption point
 * where every catalog-listed QAction (authored in forms/swmmvis.ui or
 * created programmatically during the constructor's initialize* chain)
 * is registered with the ActionRegistry, which then applies the
 * catalog/user-override shortcut bindings. Kept in its own translation
 * unit so src/swmmvis.cpp stops growing.
 */

#include "swmmvis.h"
#include "ui_swmmvis.h"

#include "swmmvisprojectwindow.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "ui/actioncatalog.h"
#include "ui/actionregistry.h"
#include "ui/dialogs/preferencesdialog.h"
#include "ui/theme/iconfactory.h"
#include "ui/toolbars/compacttoolbarcontroller.h"
#include "ui/toolbars/mesheditingtoolbar.h"
#include "ui/toolbars/ribboncompactor.h"
#include "ui/toolbars/ribbongroup.h"
#include "ui/toolbars/terraintoolbar.h"
#include "ui/widgets/commandpalette.h"

#include <QAction>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>

void SWMMVis::registerActions()
{
    // App-level Undo / Redo, routed to the ACTIVE project's map undo stack.
    // Created disabled; onActiveSubWindowChanged() rebinds enabled-state to
    // the focused project's stack (canUndo/canRedoChanged) on every tab
    // switch, so the pair always mirrors the visible canvas.
    auto *undo = new QAction(tr("&Undo"), this);
    undo->setObjectName(QStringLiteral("actionUndo"));
    undo->setEnabled(false);
    auto *redo = new QAction(tr("&Redo"), this);
    redo->setObjectName(QStringLiteral("actionRedo"));
    redo->setEnabled(false);
    connect(undo, &QAction::triggered, this, [this] {
        if (auto *pw = activeProjectWindow())
            if (pw->canvas() && pw->canvas()->undoStack())
                pw->canvas()->undoStack()->undo();
    });
    connect(redo, &QAction::triggered, this, [this] {
        if (auto *pw = activeProjectWindow())
            if (pw->canvas() && pw->canvas()->undoStack())
                pw->canvas()->undoStack()->redo();
    });

    // Edit menu front: Undo, Redo, separator, then the .ui-authored items.
    if (ui->menuEdit && !ui->menuEdit->actions().isEmpty()) {
        QAction *first = ui->menuEdit->actions().first();
        ui->menuEdit->insertAction(first, undo);
        ui->menuEdit->insertAction(first, redo);
        ui->menuEdit->insertSeparator(first);
    }

    // Command palette (P7) — a searchable route to every registered
    // action. Lazy find-or-create so the widget only exists once used.
    auto *paletteAction = new QAction(tr("&Command Palette…"), this);
    paletteAction->setObjectName(QStringLiteral("actionCommandPalette"));
    paletteAction->setToolTip(tr("Search and run any command by name"));
    connect(paletteAction, &QAction::triggered, this, [this] {
        auto *dlg = findChild<openswmmvis::ui::CommandPalette *>(
            QStringLiteral("commandPalette"));
        if (!dlg)
            dlg = new openswmmvis::ui::CommandPalette(this);
        dlg->popup();
    });
    if (ui->menuView) {
        ui->menuView->addSeparator();
        ui->menuView->addAction(paletteAction);
    }

    // Help → Keyboard Shortcuts… (P8) — Preferences pre-navigated to the
    // Keyboard page.
    auto *shortcutsAction = new QAction(tr("&Keyboard Shortcuts…"), this);
    shortcutsAction->setObjectName(QStringLiteral("actionKeyboardShortcuts"));
    connect(shortcutsAction, &QAction::triggered, this, [this] {
        PreferencesDialog dlg(this);
        dlg.openAtCategory(tr("Keyboard"));
        dlg.exec();
    });
    if (ui->menuHelp) {
        QAction *anchor = ui->menuHelp->actions().isEmpty()
                              ? nullptr
                              : ui->menuHelp->actions().constLast();   // before About
        ui->menuHelp->insertAction(anchor, shortcutsAction);
    }

    // Adopt-in-place sweep: every catalog entry whose objectName resolves
    // under the main window is registered and gets its effective shortcut
    // applied. Entries that don't resolve (not yet created on this
    // platform/build) are tolerated, mirroring the historical
    // findChild-by-name behavior.
    auto *registry = openswmmvis::ui::ActionRegistry::instance();
    registry->registerFromCatalog(this);

    // Theme-aware icon sweep (P3): re-assign each registered action's icon
    // through IconFactory so glyphs recolor with the light/dark theme.
    // Entries with an empty icon alias keep whatever they already have.
    for (const auto &entry : openswmmvis::ui::kActionCatalog) {
        if (!entry.icon || !*entry.icon)
            continue;
        QAction *act = registry->action(QString::fromLatin1(entry.id));
        if (!act)
            continue;
        const QIcon themed =
            openswmmvis::ui::IconFactory::icon(QString::fromLatin1(entry.icon));
        if (!themed.isNull())
            act->setIcon(themed);
    }

    // P6 — assemble the tabbed compact toolbar now that every action
    // (including Undo/Redo and the dock toggles) exists and is themed.
    initializeCompactToolbar();
    updateMesh2DTabVisibility();
}

void SWMMVis::initializeCompactToolbar()
{
    using openswmmvis::ui::CompactToolbarController;
    using openswmmvis::ui::RibbonGroup;

    // Iteration 2 (R3) — ArcGIS-style captioned groups: each tab is a
    // row of RibbonGroup widgets inside its QToolBar (buttons showing
    // icon + text over a small centered caption, closed by the group's
    // trailing rule). Unresolved names are skipped, same tolerance as
    // the registry sweep.
    const auto resolve = [this](std::initializer_list<const char *> names) {
        QList<QAction *> actions;
        for (const char *name : names)
            if (auto *act = findChild<QAction *>(QLatin1String(name)))
                actions.append(act);
        return actions;
    };
    const auto addGroup = [this, &resolve](QToolBar *bar, const QString &caption,
                                           std::initializer_list<const char *> names) {
        auto *group = new RibbonGroup(caption, this);
        for (QAction *act : resolve(names))
            group->addAction(act);
        bar->addWidget(group);
        return group;
    };

    // Ribbon faces read better with short labels; menus keep the full
    // action text (iconText only affects tool buttons).
    static constexpr struct { const char *name; const char *label; }
    kShortLabels[] = {
        {"actionZoomToSelection",       QT_TR_NOOP("To Selection")},
        {"actionZoomExtent",            QT_TR_NOOP("Extent")},
        {"actionInvertSelection",       QT_TR_NOOP("Invert")},
        {"actionSelectUpstream",        QT_TR_NOOP("Upstream")},
        {"actionSelectDownstream",      QT_TR_NOOP("Downstream")},
        {"actionSummarizeResults",      QT_TR_NOOP("Summarize")},
        {"actionImportFeatureLayer",    QT_TR_NOOP("Feature Layer")},
        {"actionAddSWMMOutput",         QT_TR_NOOP("SWMM Output")},
        {"actionAddDelimeteredData",    QT_TR_NOOP("Delimited")},
        {"actionFlowBalanceDownstream", QT_TR_NOOP("Balance Down")},
        {"actionFlowBalanceUpstream",   QT_TR_NOOP("Balance Up")},
        {"actionTravelTimeDownstream",  QT_TR_NOOP("Travel Down")},
        {"actionTravelTimeUpstream",    QT_TR_NOOP("Travel Up")},
        {"actionShowMassBalance",       QT_TR_NOOP("Mass Balance")},
        {"actionPlotTimeSeries",        QT_TR_NOOP("Time Series")},
        {"actionPlotProfile",           QT_TR_NOOP("Profile")},
        {"actionPauseExecution",        QT_TR_NOOP("Pause")},
        {"actionCancelExecution",       QT_TR_NOOP("Cancel")},
        {"actionGenerateMesh",          QT_TR_NOOP("Generate")},
        {"actionLayerStylingDock",      QT_TR_NOOP("Layer Styling")},
        {"actionStyleManager",          QT_TR_NOOP("Styles")},
        {"actionShowWelcome",           QT_TR_NOOP("Welcome")},
    };
    for (const auto &entry : kShortLabels)
        if (auto *act = findChild<QAction *>(QLatin1String(entry.name)))
            act->setIconText(tr(entry.label));

    mToolBarHome = new QToolBar(tr("Home"), this);
    mToolBarHome->setObjectName(QStringLiteral("toolBarHome"));
    addGroup(mToolBarHome, tr("Project"),
             {"actionNew", "actionOpen", "actionSave"});
    addGroup(mToolBarHome, tr("History"), {"actionUndo", "actionRedo"});
    addGroup(mToolBarHome, tr("Navigate"),
             {"actionPan", "actionZoomIn", "actionZoomOut", "actionZoomExtent",
              "actionZoomToSelection"});
    // R4 — last-used split-button families: the face is the last-used
    // member (persisted per family id) and mirrors its checked state, so
    // programmatic tool sync (Esc → Select) stays visible on the ribbon.
    {
        auto *group = new RibbonGroup(tr("Select"), this);
        group->addFamily(QStringLiteral("select"),
                         resolve({"actionSelect", "actionSelectByPolygon"}));
        for (QAction *act : resolve({"actionInvertSelection",
                                     "actionSelectUpstream",
                                     "actionSelectDownstream"}))
            group->addAction(act);
        mToolBarHome->addWidget(group);
    }
    addGroup(mToolBarHome, tr("Inspect"),
             {"actionMeasure", "actionSearch", "actionCopy"});
    {
        auto *group = new RibbonGroup(tr("Import"), this);
        group->addFamily(QStringLiteral("import"),
                         resolve({"actionAddSWMMOutput", "actionAddVectorData",
                                  "actionAddRasterData", "actionAddWMSData",
                                  "actionAddDelimeteredData", "actionAddBasemap"}));
        mToolBarHome->addWidget(group);
    }
    addGroup(mToolBarHome, tr("Run"),
             {"actionExecute", "actionPauseExecution", "actionCancelExecution"});

    mToolBarModel = new QToolBar(tr("Model"), this);
    mToolBarModel->setObjectName(QStringLiteral("toolBarModel"));
    addGroup(mToolBarModel, tr("Edit"), {"actionEditExisting"});
    {
        auto *group = new RibbonGroup(tr("Draw"), this);
        group->addFamily(QStringLiteral("addNode"),
                         resolve({"actionAddJunction", "actionAddOutfall",
                                  "actionAddFlowDivider", "actionAddStorage"}));
        group->addFamily(QStringLiteral("addLink"),
                         resolve({"actionAddPipe", "actionAddPump",
                                  "actionAddOrifice", "actionAddWeir",
                                  "actionAddOutlet"}));
        for (QAction *act : resolve({"actionAddSubcatchment",
                                     "actionRainGauge", "actionAddText"}))
            group->addAction(act);
        mToolBarModel->addWidget(group);
    }
    {
        auto *group = new RibbonGroup(tr("Climate"), this);
        group->addFamily(QStringLiteral("climate"),
                         resolve({"actionTemperature", "actionWind",
                                  "actionSnow", "actionEvaporation",
                                  "actionSolarRadiation"}));
        mToolBarModel->addWidget(group);
    }
    {
        auto *group = new RibbonGroup(tr("Data Objects"), this);
        group->addFamily(QStringLiteral("addDataObject"),
                         resolve({"actionNewTimeSeries", "actionNewCurve",
                                  "actionNewPattern", "actionNewControlRule",
                                  "actionNewTransect", "actionNewLidControl",
                                  "actionNewPollutant"}));
        mToolBarModel->addWidget(group);
    }
    addGroup(mToolBarModel, tr("Setup"),
             {"actionOptions", "actionUserFlags", "actionImportFeatureLayer"});
    addGroup(mToolBarModel, tr("Mesh 2D"), {"actionGenerateMesh"});

    mToolBarMesh2D = new QToolBar(tr("Mesh 2D"), this);
    mToolBarMesh2D->setObjectName(QStringLiteral("toolBarMesh2D"));
    addGroup(mToolBarMesh2D, tr("Mesh"), {"actionGenerateMesh"});

    mToolBarView = new QToolBar(tr("View"), this);
    mToolBarView->setObjectName(QStringLiteral("toolBarView"));
    addGroup(mToolBarView, tr("Panels"),
             {"actionToggleDockLayers", "actionToggleDockObjectBrowser",
              "actionToggleDockProperties", "actionToggleDockAttributeTable",
              "actionToggleDockLegend", "actionToggleDockSimulationStatus",
              "actionToggleDockMessageLogs"});
    addGroup(mToolBarView, tr("Styling"),
             {"actionLayerStylingDock", "actionStyleManager"});
    addGroup(mToolBarView, tr("Start"), {"actionShowWelcome"});

    // Analysis: the "Results Layers" widget group leads the bar (added by
    // initializeAnalysisLayerCombos); the action groups follow. Tabular
    // View gains its first toolbar home in the Report group.
    addGroup(mToolBarAnalysis, tr("Report"),
             {"actionSummarizeResults", "actionReport", "actionTabularView"});
    mGroupPlots = addGroup(mToolBarAnalysis, tr("Plots"),
                           {"actionPlotTimeSeries", "actionPlotProfile"});
    addGroup(mToolBarAnalysis, tr("Network Analysis"),
             {"actionFlowBalanceDownstream", "actionFlowBalanceUpstream",
              "actionTravelTimeDownstream", "actionTravelTimeUpstream",
              "actionShowMassBalance"});

    // US.A2 — explicit override dropdown on the Plot Profile face: force
    // a Network or 2D-Surface profile even when both are loaded. Moved
    // here from initializeMapTools() (R3): the ribbon button must exist
    // before a menu can be attached to it.
    {
        auto *menu = new QMenu(this);
        QAction *net  = menu->addAction(tr("Network Profile…"));
        QAction *surf = menu->addAction(tr("Surface (2D mesh) Profile…"));
        connect(net,  &QAction::triggered, this,
                [this]() { onPlotProfileTriggered(1); });
        connect(surf, &QAction::triggered, this,
                [this]() { onPlotProfileTriggered(2); });
        if (auto *btn = mGroupPlots->buttonForAction(ui->actionPlotProfile)) {
            btn->setMenu(menu);
            btn->setPopupMode(QToolButton::MenuButtonPopup);
        }
    }

    mCompactToolbar = new CompactToolbarController(this);
    mCompactToolbar->addTab(QStringLiteral("home"), tr("Home"),
                            {mToolBarHome});
    mCompactToolbar->addTab(QStringLiteral("model"), tr("Model"),
                            QList<QToolBar *>{mToolBarModel,
                                              static_cast<QToolBar *>(mTerrainToolbar)});
    mCompactToolbar->addTab(QStringLiteral("mesh2d"), tr("Mesh 2D"),
                            QList<QToolBar *>{mToolBarMesh2D,
                                              static_cast<QToolBar *>(mMeshEditingToolbar)},
                            /*contextual*/ true);
    mCompactToolbar->addTab(QStringLiteral("analysis"), tr("Analysis"),
                            {mToolBarAnalysis});
    mCompactToolbar->addTab(QStringLiteral("results"), tr("Results"),
                            {mToolBarAnimation});
    mCompactToolbar->addTab(QStringLiteral("view"), tr("View"),
                            {mToolBarView});
    mCompactToolbar->finalize();

    // R5 — responsive ribbon: one compactor per group-bearing bar steps
    // trailing groups Full → Compact → Collapsed as width shrinks
    // (promotions only past the 32 px dead band). Terrain/MeshEditing
    // sibling bars host no groups and stay as-is.
    for (QToolBar *bar : {mToolBarHome, mToolBarModel, mToolBarMesh2D,
                          mToolBarView, mToolBarAnalysis, mToolBarAnimation}) {
        const auto groups = bar->findChildren<RibbonGroup *>(
            Qt::FindDirectChildrenOnly);
        if (!groups.isEmpty())
            new openswmmvis::ui::RibbonCompactor(bar, groups);
    }

    // Right corner of the tab strip: the command-palette launcher (P7).
    if (auto *paletteAction =
            findChild<QAction *>(QStringLiteral("actionCommandPalette")))
        mCompactToolbar->stripToolBar()->addAction(paletteAction);
}

void SWMMVis::updateMesh2DTabVisibility()
{
    if (!mCompactToolbar)
        return;

    disconnect(mMeshTabConnAdd);
    disconnect(mMeshTabConnRemove);

    auto *pw = activeProjectWindow();
    MapCanvas *canvas = pw ? pw->canvas() : nullptr;

    const auto hasMeshLayer = [](MapCanvas *c) {
        if (!c)
            return false;
        const auto layers = c->layers();
        for (OpenSWMMVisLayer *layer : layers) {
            if (qobject_cast<SWMM2DMeshLayer *>(layer))
                return true;
        }
        return false;
    };

    mCompactToolbar->setTabVisible(QStringLiteral("mesh2d"),
                                   hasMeshLayer(canvas));
    if (canvas) {
        const auto refresh = [this, canvas, hasMeshLayer] {
            mCompactToolbar->setTabVisible(QStringLiteral("mesh2d"),
                                           hasMeshLayer(canvas));
        };
        mMeshTabConnAdd = connect(canvas, &MapCanvas::layerAdded,
                                  this, refresh);
        mMeshTabConnRemove = connect(canvas, &MapCanvas::layerRemoved,
                                     this, refresh);
    }
}

QMenu *SWMMVis::createPopupMenu()
{
    // Dock toggles only — the compact-toolbar rows are tab-managed and
    // individually hiding them would break the tab illusion.
    auto *menu = new QMenu(this);
    for (const char *name :
         {"actionToggleDockLayers", "actionToggleDockObjectBrowser",
          "actionToggleDockProperties", "actionToggleDockAttributeTable",
          "actionToggleDockLegend", "actionToggleDockSimulationStatus",
          "actionToggleDockMessageLogs"}) {
        if (auto *act = findChild<QAction *>(QLatin1String(name)))
            menu->addAction(act);
    }
    return menu;
}

void SWMMVis::rebindUndoRedoActions(SWMMVisProjectWindow *pw)
{
    auto *undo = findChild<QAction *>(QStringLiteral("actionUndo"));
    auto *redo = findChild<QAction *>(QStringLiteral("actionRedo"));
    if (!undo || !redo)
        return;

    disconnect(mUndoEnableConn);
    disconnect(mRedoEnableConn);

    MapUndoStack *stack =
        (pw && pw->canvas()) ? pw->canvas()->undoStack() : nullptr;
    if (!stack) {
        undo->setEnabled(false);
        redo->setEnabled(false);
        return;
    }
    undo->setEnabled(stack->canUndo());
    redo->setEnabled(stack->canRedo());
    mUndoEnableConn = connect(stack, &QUndoStack::canUndoChanged,
                              undo, &QAction::setEnabled);
    mRedoEnableConn = connect(stack, &QUndoStack::canRedoChanged,
                              redo, &QAction::setEnabled);
}
