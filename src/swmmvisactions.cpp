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
#include "layers/gisrasterlayer.h"
#include "layers/swmm2dresultslayer.h"
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
    updateContextualTabs();
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
    // action text (iconText only affects tool buttons). Iteration 3:
    // every ribbon-visible action gets a human face label, wrapped onto
    // two lines at a word boundary ArcGIS-Pro style when one line would
    // be wide (QToolButton renders '\n' natively). Pinning iconText also
    // freezes faces whose action text mutates at runtime (the Properties
    // dock title tracks the selected layer).
    static constexpr struct { const char *name; const char *label; }
    kShortLabels[] = {
        // Home
        {"actionZoomToSelection",       QT_TR_NOOP("Zoom to\nSelection")},
        {"actionZoomExtent",            QT_TR_NOOP("Full\nExtent")},
        {"actionInvertSelection",       QT_TR_NOOP("Invert")},
        {"actionSelectUpstream",        QT_TR_NOOP("Upstream")},
        {"actionSelectDownstream",      QT_TR_NOOP("Downstream")},
        {"actionSelectByPolygon",       QT_TR_NOOP("Select by\nPolygon")},
        {"actionAddSWMMOutput",         QT_TR_NOOP("SWMM\nOutput")},
        {"actionAdd2DResults",          QT_TR_NOOP("2D\nResults")},
        {"actionAddVectorData",         QT_TR_NOOP("Vector\nData")},
        {"actionAddRasterData",         QT_TR_NOOP("Raster\nData")},
        {"actionAddWMSData",            QT_TR_NOOP("Web\nLayers")},
        {"actionAddDelimeteredData",    QT_TR_NOOP("Delimited")},
        {"actionAddBasemap",            QT_TR_NOOP("Basemap")},
        {"actionAddMesh2D",             QT_TR_NOOP("2D Mesh")},
        {"actionPauseExecution",        QT_TR_NOOP("Pause")},
        {"actionCancelExecution",       QT_TR_NOOP("Cancel")},
        // Model
        {"actionEditExisting",          QT_TR_NOOP("Edit\nExisting")},
        {"actionAddJunction",           QT_TR_NOOP("Junction")},
        {"actionAddVirtualJunction",    QT_TR_NOOP("Virtual\nJunction")},
        {"actionAddOutfall",            QT_TR_NOOP("Outfall")},
        {"actionAddFlowDivider",        QT_TR_NOOP("Flow\nDivider")},
        {"actionAddStorage",            QT_TR_NOOP("Storage")},
        {"actionAddPipe",               QT_TR_NOOP("Pipe")},
        {"actionAddPump",               QT_TR_NOOP("Pump")},
        {"actionAddOrifice",            QT_TR_NOOP("Orifice")},
        {"actionAddWeir",               QT_TR_NOOP("Weir")},
        {"actionAddOutlet",             QT_TR_NOOP("Outlet")},
        {"actionAddSubcatchment",       QT_TR_NOOP("Subcatchment")},
        {"actionRainGauge",             QT_TR_NOOP("Rain\nGauge")},
        {"actionAddText",               QT_TR_NOOP("Text")},
        {"actionSolarRadiation",        QT_TR_NOOP("Solar\nRadiation")},
        {"actionNewTimeSeries",         QT_TR_NOOP("Time\nSeries")},
        {"actionNewCurve",              QT_TR_NOOP("Curve")},
        {"actionNewPattern",            QT_TR_NOOP("Pattern")},
        {"actionNewControlRule",        QT_TR_NOOP("Control\nRule")},
        {"actionNewTransect",           QT_TR_NOOP("Transect")},
        {"actionNewLidControl",         QT_TR_NOOP("LID\nControl")},
        {"actionNewPollutant",          QT_TR_NOOP("Pollutant")},
        {"actionNewLandUse",            QT_TR_NOOP("Land\nUse")},
        {"actionEditReactionSystem",    QT_TR_NOOP("Reaction\nSystem")},
        {"actionEditHeatConfig",        QT_TR_NOOP("Heat")},
        {"actionOptions",               QT_TR_NOOP("Simulation\nOptions")},
        {"actionUserFlags",             QT_TR_NOOP("User\nFlags")},
        {"actionImportFeatureLayer",    QT_TR_NOOP("Import\nFeature Layer")},
        {"actionAssignRainGages",       QT_TR_NOOP("Assign\nRain Gages")},
        {"actionGenerateMesh",          QT_TR_NOOP("Generate\nMesh")},
        {"actionMeshAssignFromRaster",  QT_TR_NOOP("From\nRaster")},
        {"actionMeshAssignFromVector",  QT_TR_NOOP("From\nShapefile")},
        {"actionMesh2DGWParams",        QT_TR_NOOP("Aquifer\nParameters")},
        {"actionMesh2DGWInitCond",      QT_TR_NOOP("Initial\nConditions")},
        // Analysis
        {"actionSummarizeResults",      QT_TR_NOOP("Summarize")},
        {"actionTabularView",           QT_TR_NOOP("Tabular\nView")},
        {"actionFlowBalanceDownstream", QT_TR_NOOP("Flow Balance\nDownstream")},
        {"actionFlowBalanceUpstream",   QT_TR_NOOP("Flow Balance\nUpstream")},
        {"actionTravelTimeDownstream",  QT_TR_NOOP("Travel Time\nDownstream")},
        {"actionTravelTimeUpstream",    QT_TR_NOOP("Travel Time\nUpstream")},
        {"actionShowMassBalance",       QT_TR_NOOP("Mass\nBalance")},
        {"actionPlotTimeSeries",        QT_TR_NOOP("Time\nSeries")},
        {"actionPlotProfile",           QT_TR_NOOP("Profile")},
        {"actionPlotProfile2D",         QT_TR_NOOP("2D Profile")},
        // Results
        {"actionSkipBack",              QT_TR_NOOP("Skip\nBack")},
        {"actionSkipForward",           QT_TR_NOOP("Skip\nForward")},
        {"actionShowLegend",            QT_TR_NOOP("Show\nLegend")},
        {"actionSetStyle",              QT_TR_NOOP("Set\nStyle")},
        // View (dock faces pinned — see note above)
        {"actionToggleDockLayers",           QT_TR_NOOP("Layers")},
        {"actionToggleDockObjectBrowser",    QT_TR_NOOP("Object\nBrowser")},
        {"actionToggleDockProperties",       QT_TR_NOOP("Properties")},
        {"actionToggleDockSectionView",      QT_TR_NOOP("Section\nView")},
        {"actionToggleDockAttributeTable",   QT_TR_NOOP("Attribute\nTable")},
        {"actionToggleDockLegend",           QT_TR_NOOP("Legend")},
        {"actionToggleDockSimulationStatus", QT_TR_NOOP("Simulation\nStatus")},
        {"actionToggleDockMessageLogs",      QT_TR_NOOP("Message\nLogs")},
        {"actionLayerStylingDock",      QT_TR_NOOP("Layer\nStyling")},
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
    // Iteration 3 — import sources unstacked: every source is its own
    // button (auto-compact still demotes the group when width demands).
    addGroup(mToolBarHome, tr("Import"),
             {"actionAddSWMMOutput", "actionAdd2DResults",
              "actionAddVectorData", "actionAddRasterData",
              "actionAddWMSData", "actionAddDelimeteredData",
              "actionAddBasemap", "actionAddMesh2D"});
    addGroup(mToolBarHome, tr("Run"),
             {"actionExecute", "actionPauseExecution", "actionCancelExecution"});

    mToolBarModel = new QToolBar(tr("Model"), this);
    mToolBarModel->setObjectName(QStringLiteral("toolBarModel"));
    addGroup(mToolBarModel, tr("Select"), {"actionSelect"});
    addGroup(mToolBarModel, tr("Edit"), {"actionEditExisting"});
    // Iteration 3 — node/link tools unstacked into captioned groups so
    // every draw tool is a visible, individually-toggled button.
    addGroup(mToolBarModel, tr("Nodes"),
             {"actionAddJunction", "actionAddVirtualJunction", "actionAddOutfall",
              "actionAddFlowDivider", "actionAddStorage"});
    addGroup(mToolBarModel, tr("Links"),
             {"actionAddPipe", "actionAddPump", "actionAddOrifice",
              "actionAddWeir", "actionAddOutlet"});
    addGroup(mToolBarModel, tr("Draw"),
             {"actionAddSubcatchment", "actionRainGauge", "actionAddText"});
    {
        auto *group = new RibbonGroup(tr("Climate"), this);
        group->addFamily(QStringLiteral("climate"),
                         resolve({"actionTemperature", "actionWind",
                                  "actionSnow", "actionEvaporation",
                                  "actionSolarRadiation"}));
        mToolBarModel->addWidget(group);
    }
    // Iteration 3 — data objects unstacked (one button per object type).
    addGroup(mToolBarModel, tr("Data Objects"),
             {"actionNewTimeSeries", "actionNewCurve", "actionNewPattern",
              "actionNewControlRule", "actionNewTransect",
              "actionNewLidControl", "actionNewPollutant",
              "actionNewLandUse", "actionEditReactionSystem",
              "actionEditHeatConfig"});
    addGroup(mToolBarModel, tr("Setup"),
             {"actionOptions", "actionUserFlags", "actionImportFeatureLayer"});
    addGroup(mToolBarModel, tr("Tools"), {"actionAssignRainGages"});
    addGroup(mToolBarModel, tr("Mesh 2D"), {"actionGenerateMesh"});

    mToolBarMesh2D = new QToolBar(tr("Mesh 2D"), this);
    mToolBarMesh2D->setObjectName(QStringLiteral("toolBarMesh2D"));
    addGroup(mToolBarMesh2D, tr("Mesh"), {"actionGenerateMesh"});
    addGroup(mToolBarMesh2D, tr("Cell Data"),
             {"actionMeshAssignFromRaster", "actionMeshAssignFromVector"});
    addGroup(mToolBarMesh2D, tr("Groundwater (2D)"),
             {"actionMesh2DGWParams", "actionMesh2DGWInitCond"});

    mToolBarView = new QToolBar(tr("View"), this);
    mToolBarView->setObjectName(QStringLiteral("toolBarView"));
    addGroup(mToolBarView, tr("Panels"),
             {"actionToggleDockLayers", "actionToggleDockObjectBrowser",
              "actionToggleDockProperties", "actionToggleDockSectionView",
              "actionToggleDockAttributeTable",
              "actionToggleDockLegend", "actionToggleDockSimulationStatus",
              "actionToggleDockMessageLogs"});
    addGroup(mToolBarView, tr("Styling"),
             {"actionLayerStylingDock", "actionStyleManager"});
    addGroup(mToolBarView, tr("Start"), {"actionShowWelcome"});

    // Analysis: add a leftmost Select shortcut so users can return to pick
    // mode without switching tabs. Results-layer selectors then follow.
    {
        auto *group = new RibbonGroup(tr("Select"), this);
        for (QAction *act : resolve({"actionSelect"}))
            group->addAction(act);
        const auto acts = mToolBarAnalysis->actions();
        if (!acts.isEmpty())
            mToolBarAnalysis->insertWidget(acts.constFirst(), group);
        else
            mToolBarAnalysis->addWidget(group);
    }
    // The "Results Layers" widget group is added in initializeAnalysisLayerCombos;
    // action groups follow here. Tabular View gains its first toolbar home in
    // the Report group.
    addGroup(mToolBarAnalysis, tr("Report"),
             {"actionSummarizeResults", "actionReport", "actionTabularView"});
    mGroupPlots = addGroup(mToolBarAnalysis, tr("Plots"),
                           {"actionPlotTimeSeries", "actionPlotProfile",
                            "actionPlotProfile2D"});
    addGroup(mToolBarAnalysis, tr("Network Analysis"),
             {"actionFlowBalanceDownstream", "actionFlowBalanceUpstream",
              "actionTravelTimeDownstream", "actionTravelTimeUpstream",
              "actionShowMassBalance"});

    // Left-pack (iteration 3): groups are horizontally Fixed, so an
    // Expanding zero-min trailing spacer absorbs the leftover row width
    // — set icon separation, unused gap at the end by design. The
    // Results bar is skipped: its Timeline group is itself Expanding and
    // takes the slack there. RibbonCompactor discounts the spacer's one
    // inter-item spacing by objectName.
    for (QToolBar *bar : {mToolBarHome, mToolBarModel, mToolBarMesh2D,
                          mToolBarView, mToolBarAnalysis}) {
        auto *spacer = new QWidget(bar);
        spacer->setObjectName(QStringLiteral("ribbonBarSpacer"));
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        spacer->setMinimumSize(0, 0);
        bar->addWidget(spacer);
    }

    mCompactToolbar = new CompactToolbarController(this);
    mCompactToolbar->addTab(QStringLiteral("home"), tr("Home"),
                            {mToolBarHome});
    mCompactToolbar->addTab(QStringLiteral("model"), tr("Model"),
                            {mToolBarModel});
    // Iteration 3 — the terrain (DEM) controls get their own contextual
    // tab (revealed once a raster layer exists), captioned ribbon groups
    // inside the TerrainToolbar itself.
    mCompactToolbar->addTab(QStringLiteral("terrain"), tr("Terrain"),
                            QList<QToolBar *>{
                                static_cast<QToolBar *>(mTerrainToolbar)},
                            /*contextual*/ true);
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

void SWMMVis::updateContextualTabs()
{
    if (!mCompactToolbar)
        return;

    disconnect(mMeshTabConnAdd);
    disconnect(mMeshTabConnRemove);

    auto *pw = activeProjectWindow();
    MapCanvas *canvas = pw ? pw->canvas() : nullptr;

    // Results layers count too: SWMM2DResultsLayer is a SIBLING of
    // SWMM2DMeshLayer, and the 2D-results tools on the Mesh Editing bar
    // (cell picking, mesh profile) must be reachable with a results-only
    // file loaded.
    const auto hasMesh2DContent = [](MapCanvas *c) {
        if (!c)
            return false;
        const auto layers = c->layers();
        for (OpenSWMMVisLayer *layer : layers) {
            if (qobject_cast<SWMM2DMeshLayer *>(layer)
                || qobject_cast<SWMM2DResultsLayer *>(layer))
                return true;
        }
        return false;
    };
    // The Terrain tab reveals once any raster (DEM candidate) is loaded.
    const auto hasRaster = [](MapCanvas *c) {
        if (!c)
            return false;
        const auto layers = c->layers();
        for (OpenSWMMVisLayer *layer : layers) {
            if (qobject_cast<GISRasterLayer *>(layer))
                return true;
        }
        return false;
    };

    mCompactToolbar->setTabVisible(QStringLiteral("mesh2d"),
                                   hasMesh2DContent(canvas));
    mCompactToolbar->setTabVisible(QStringLiteral("terrain"),
                                   hasRaster(canvas));
    if (canvas) {
        const auto refresh = [this, canvas, hasMesh2DContent, hasRaster] {
            mCompactToolbar->setTabVisible(QStringLiteral("mesh2d"),
                                           hasMesh2DContent(canvas));
            mCompactToolbar->setTabVisible(QStringLiteral("terrain"),
                                           hasRaster(canvas));
        };
        // Receiver context is the CONTROLLER, not `this`:
        // onActiveSubWindowChanged later blanket-disconnects
        // (canvas, layerAdded, this, nullptr) for the results combos,
        // which silently killed this refresh — the mesh loads
        // asynchronously AFTER activation, so the Mesh 2D tab never
        // appeared (iteration-3 regression fix).
        mMeshTabConnAdd = connect(canvas, &MapCanvas::layerAdded,
                                  mCompactToolbar, refresh);
        mMeshTabConnRemove = connect(canvas, &MapCanvas::layerRemoved,
                                     mCompactToolbar, refresh);
    }
}

QMenu *SWMMVis::createPopupMenu()
{
    // Dock toggles only — the compact-toolbar rows are tab-managed and
    // individually hiding them would break the tab illusion.
    auto *menu = new QMenu(this);
    for (const char *name :
         {"actionToggleDockLayers", "actionToggleDockObjectBrowser",
          "actionToggleDockProperties", "actionToggleDockSectionView",
          "actionToggleDockAttributeTable",
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
