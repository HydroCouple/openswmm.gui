/**
 * @file      swmmvisactions.cpp
 * @brief     UI redesign P1 — SWMMVis::registerActions(): the single adoption point where every
 *            catalog-listed QAction (authored in forms/swmmvis.ui or created programmatically
 *            during the constructor's initialize* chain) is registered with the ActionRegistry,
 *            which then applies the catalog/user-override shortcut bindings.
 *
 * UI redesign P1 — SWMMVis::registerActions(): the single adoption point
 * where every catalog-listed QAction (authored in forms/swmmvis.ui or
 * created programmatically during the constructor's initialize* chain)
 * is registered with the ActionRegistry, which then applies the
 * catalog/user-override shortcut bindings. Kept in its own translation
 * unit so src/swmmvis.cpp stops growing.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "swmmvis.h"
#include "ui_swmmvis.h"

#include "swmmvisprojectwindow.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/gisrasterlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmresultslayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "ui/actioncatalog.h"
#include "ui/actionregistry.h"
#include "ui/dialogs/preferencesdialog.h"
#include "ui/dialogs/componentexecutiondialog.h"
#include "simulation/componentexecution.h"
#include "ui/theme/iconfactory.h"
#include "ui/toolbars/compacttoolbarcontroller.h"
#include "ui/toolbars/mesheditingtoolbar.h"
#include "ui/toolbars/ribboncompactor.h"
#include "ui/toolbars/ribbongroup.h"
#include "ui/toolbars/ribbonsplitbutton.h"
#include "ui/toolbars/terraintoolbar.h"
#include "ui/widgets/commandpalette.h"

#include <QAction>
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>

void SWMMVis::registerActions()
{
    qApp->installEventFilter(this);
    auto *componentExecution = new QAction(tr("Component execution…"), this);
    componentExecution->setObjectName(QStringLiteral("actionComponentExecution"));
    connect(componentExecution, &QAction::triggered, this, [this] {
        auto *pw = activeProjectWindow();
        if (!pw || pw->isClosing() || pw->property("extensionRunCount").toInt() ||
            pw->property("extensionReadLeases").toInt() || !pw->property("extensionWriteLease").toString().isEmpty())
            return;
        if (mPreparingRunAttempts.contains(pw->extensionSessionId().toString(QUuid::WithoutBraces) +
                                          '/' + pw->engineVersion()))
            return;
        auto *model = openswmmvis::simulation::ComponentEngineRegistry::forProject(pw);
        if (!model->property("dirtyHookInstalled").toBool()) {
            connect(model, &openswmmvis::simulation::ComponentEngineRegistry::settingsChanged,
                    pw, [pw] { pw->setHasChanges(true); });
            model->setProperty("dirtyHookInstalled", true);
        }
        ComponentExecutionDialog dialog(model, this);
        dialog.exec();
    });
    if (ui->menuAnalysis)
        ui->menuAnalysis->addAction(componentExecution);
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
        auto *dlg = findChild<openswmmvis::ui::CommandPalette *>(QStringLiteral("commandPalette"));
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
                              : ui->menuHelp->actions().constLast(); // before About
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
        const QIcon themed = openswmmvis::ui::IconFactory::icon(QString::fromLatin1(entry.icon));
        if (!themed.isNull())
            act->setIcon(themed);
    }

    // Availability reasons (process-aware editing plan §5): a disabled
    // command explains itself on hover. The enable state stays with the
    // code that owns it (project open, run state, mesh toolbar, undo stack).
    {
        const QString needProject = tr("Open or create a project first.");
        for (const auto &entry : openswmmvis::ui::kActionCatalog)
            if (entry.tags & openswmmvis::ui::RequiresProject)
                registry->setDisabledReason(QString::fromLatin1(entry.id), needProject);
        for (const char *id : {"import.swmmOutput", "import.vector", "import.raster", "import.wms",
                               "import.wfs", "import.delimited", "import.basemap", "import.mesh2d"})
            registry->setDisabledReason(QString::fromLatin1(id), needProject);
        const QString noRun = tr("No simulation is running.");
        registry->setDisabledReason(QStringLiteral("sim.pause"), noRun);
        registry->setDisabledReason(QStringLiteral("sim.stop"), noRun);
        const QString needMesh = tr("Load or generate a 2D mesh first.");
        for (const char *id : {"mesh.selectVertices", "mesh.selectEdges", "mesh.autoCouple",
                               "mesh.remap", "mesh.editInTable", "mesh.reviewOwnership"})
            registry->setDisabledReason(QString::fromLatin1(id), needMesh);
        registry->setDisabledReason(QStringLiteral("mesh.assignInfiltration"),
                                    tr("Select one or more 2D cells with the Cells tool."));
        registry->setDisabledReason(QStringLiteral("mesh.clearSelection"),
                                    tr("Nothing is selected."));
        registry->setDisabledReason(QStringLiteral("edit.undo"),
                                    tr("Nothing to undo in the active project."));
        registry->setDisabledReason(QStringLiteral("edit.redo"),
                                    tr("Nothing to redo in the active project."));
    }

    // P6 — assemble the tabbed compact toolbar now that every action
    // (including Undo/Redo and the dock toggles) exists and is themed.
    initializeCompactToolbar();
    updateContextualTabs();
}

void SWMMVis::initializeCompactToolbar()
{
    using openswmmvis::ui::CompactToolbarController;
    using openswmmvis::ui::IconFactory;
    using openswmmvis::ui::kRibbonIconFull;
    using openswmmvis::ui::RibbonGroup;
    using openswmmvis::ui::RibbonSplitButton;

    // Process-aware editing plan, Phase 1 (docs/development/
    // process-aware-spatial-editing.md §4): every command has ONE ribbon
    // home, recorded in its catalog entry's `tab` field and checked against
    // this layout by test_ribbon_organization. Groups keep the ArcGIS-style
    // captions and large icon + label faces; related families sit in
    // visible three-row stacks (small icon + label, every row its own
    // button — never a dropdown, which the August revision ruled out).
    // Select, Pan, Full Extent, Undo and Redo live in the Quick Access
    // group below the tab strip instead of on five tabs. Unresolved names are
    // skipped, same tolerance as the registry sweep.
    /**
     * @brief Resolve existing child actions by object name. Argument `names`: borrowed
     * initializer-list of required nonnull C strings read synchronously. Returns: borrowed matching
     * QAction list in requested order, skipping absent entries; no ownership transfer.
     */
    const auto resolve = [this](std::initializer_list<const char *> names) {
        QList<QAction *> actions;
        for (const char *name : names)
            if (auto *act = findChild<QAction *>(QLatin1String(name)))
                actions.append(act);
        return actions;
    };
    /**
     * @brief Append a captioned ribbon group of large faces. Argument `bar`: required borrowed
     * toolbar receiving the group widget. Argument `caption`: copied group title. Argument
     * `names`: action object names resolved synchronously (may be empty when the group only
     * holds stacks). Returns: new Qt-owned group; shares original action identities.
     */
    const auto addGroup = [this, &resolve](QToolBar *bar, const QString &caption,
                                           std::initializer_list<const char *> names) {
        auto *group = new RibbonGroup(caption, this);
        for (QAction *act : resolve(names))
            group->addAction(act);
        bar->addWidget(group);
        return group;
    };
    /**
     * @brief Append stack columns to a group. Argument `group`: required borrowed group.
     * Argument `columns`: one inner list per column, at most three names each, resolved in order;
     * an inner list that resolves to nothing adds no column.
     */
    using Columns = std::initializer_list<std::initializer_list<const char *>>;
    const auto addStacks = [&resolve](RibbonGroup *group, Columns columns) {
        for (const auto &column : columns) {
            const QList<QAction *> actions = resolve(column);
            if (!actions.isEmpty())
                group->addStack(actions);
        }
    };

    // Ribbon faces read better with short labels; menus keep the full
    // action text (iconText only affects tool buttons). Large faces wrap
    // onto two lines at a word boundary, ArcGIS-Pro style ('\n' renders
    // natively in QToolButton); stacked rows sit beside a small icon and
    // stay on one line. Pinning iconText also freezes faces whose action
    // text mutates at runtime (dock toggles track their dock titles).
    static constexpr struct {
        const char *name;
        const char *label;
    } kShortLabels[] = {
        // Home — large faces
        {"actionZoomToSelection", QT_TR_NOOP("Zoom to\nSelection")},
        {"actionInvertSelection", QT_TR_NOOP("Invert")},
        {"actionSelectUpstream", QT_TR_NOOP("Upstream")},
        {"actionSelectDownstream", QT_TR_NOOP("Downstream")},
        // Quick Access (icon-only there; the label still names the split member)
        {"actionZoomExtent", QT_TR_NOOP("Full\nExtent")},
        {"actionSelectByPolygon", QT_TR_NOOP("Select by\nPolygon")},
        // Home — stacked rows
        {"actionSaveAs", QT_TR_NOOP("Save As")},
        {"actionExportMap", QT_TR_NOOP("Export Map")},
        {"actionCopy", QT_TR_NOOP("Copy View")},
        {"actionAddSWMMOutput", QT_TR_NOOP("SWMM Output")},
        {"actionAdd2DResults", QT_TR_NOOP("2D Results")},
        {"actionAddMesh2D", QT_TR_NOOP("2D Mesh")},
        {"actionAddVectorData", QT_TR_NOOP("Vector Data")},
        {"actionAddRasterData", QT_TR_NOOP("Raster Data")},
        {"actionAddDelimeteredData", QT_TR_NOOP("Delimited")},
        {"actionAddWMSData", QT_TR_NOOP("Web Layers")},
        {"actionAddWFSData", QT_TR_NOOP("WFS")},
        {"actionAddBasemap", QT_TR_NOOP("Basemap")},
        {"actionPauseExecution", QT_TR_NOOP("Pause")},
        {"actionCancelExecution", QT_TR_NOOP("Cancel")},
        {"actionToggleDockSimulationStatus", QT_TR_NOOP("Simulation Status")},
        {"actionLayerStylingDock", QT_TR_NOOP("Layer Styling")},
        {"actionStyleManager", QT_TR_NOOP("Styles")},
        {"actionShowWelcome", QT_TR_NOOP("Welcome")},
        // Model — large faces
        {"actionEditExisting", QT_TR_NOOP("Edit\nNetwork")},
        // Every Climate member opens the one Climatology dialog (on its own
        // tab), so the split face reads "Climate"; the dropdown and the menu
        // keep the member names (they show text, not iconText).
        {"actionTemperature", QT_TR_NOOP("Climate")},
        {"actionWind", QT_TR_NOOP("Climate")},
        {"actionSnow", QT_TR_NOOP("Climate")},
        {"actionEvaporation", QT_TR_NOOP("Climate")},
        {"actionSolarRadiation", QT_TR_NOOP("Climate")},
        {"actionOptions", QT_TR_NOOP("Simulation\nOptions")},
        {"actionImportFeatureLayer", QT_TR_NOOP("Import\nFeatures")},
        {"actionGenerateMesh", QT_TR_NOOP("Generate\nMesh")},
        // Model — stacked rows
        {"actionAddJunction", QT_TR_NOOP("Junction")},
        {"actionAddOutfall", QT_TR_NOOP("Outfall")},
        {"actionAddStorage", QT_TR_NOOP("Storage")},
        {"actionAddFlowDivider", QT_TR_NOOP("Flow Divider")},
        {"actionAddVirtualJunction", QT_TR_NOOP("Virtual Junction")},
        {"actionAddInletJunction", QT_TR_NOOP("Inlet Junction")},
        {"actionAddLidNode", QT_TR_NOOP("LID Node")},
        {"actionAddPipe", QT_TR_NOOP("Conduit")},
        {"actionAddPump", QT_TR_NOOP("Pump")},
        {"actionAddOrifice", QT_TR_NOOP("Orifice")},
        {"actionAddWeir", QT_TR_NOOP("Weir")},
        {"actionAddOutlet", QT_TR_NOOP("Outlet")},
        {"actionAddSubcatchment", QT_TR_NOOP("Subcatchment")},
        {"actionRainGauge", QT_TR_NOOP("Rain Gage")},
        {"actionAssignRainGages", QT_TR_NOOP("Assign Rain Gages")},
        {"actionNewSnowpack", QT_TR_NOOP("Snowpack")},
        {"actionNewUnitHydrograph", QT_TR_NOOP("Unit Hydrograph")},
        {"actionNewTimeSeries", QT_TR_NOOP("Time Series")},
        {"actionNewCurve", QT_TR_NOOP("Curve")},
        {"actionNewPattern", QT_TR_NOOP("Pattern")},
        {"actionNewTransect", QT_TR_NOOP("Transect")},
        {"actionNewStreet", QT_TR_NOOP("Street")},
        {"actionNewInlet", QT_TR_NOOP("Inlet")},
        {"actionNewAquifer", QT_TR_NOOP("Aquifer")},
        {"actionNewLidControl", QT_TR_NOOP("LID Control")},
        {"actionNewControlRule", QT_TR_NOOP("Control Rule")},
        {"actionNewPollutant", QT_TR_NOOP("Pollutant")},
        {"actionNewLandUse", QT_TR_NOOP("Land Use")},
        {"actionEditReactionSystem", QT_TR_NOOP("Reactions")},
        {"actionEditInitialQuality", QT_TR_NOOP("Initial Quality")},
        {"actionEditWaterAgeSources", QT_TR_NOOP("Water Age")},
        {"actionEditHeatConfig", QT_TR_NOOP("Heat")},
        {"actionUserFlags", QT_TR_NOOP("User Flags")},
        {"actionSetProjectCRS", QT_TR_NOOP("Project CRS")},
        {"actionAddText", QT_TR_NOOP("Text Layers")},
        // Features
        {"actionFeatureEditMode", QT_TR_NOOP("Edit\nFeatures")},
        // Mesh 2D (MeshEditingToolbar labels its own tools; these are the
        // host-created commands it places). Groundwater has five commands,
        // so they sit in stacked rows and keep to one line.
        {"actionBurnChannels", QT_TR_NOOP("Burn\nChannels")},
        {"actionMeshAssignFromRaster", QT_TR_NOOP("From\nRaster")},
        {"actionMeshAssignFromVector", QT_TR_NOOP("From\nShapefile")},
        {"actionMeshAssignInfilToSelection", QT_TR_NOOP("Infiltration\nto Selection")},
        {"actionAssignGroundwater", QT_TR_NOOP("Assign Groundwater")},
        {"actionMesh2DGWParams", QT_TR_NOOP("Aquifer Properties")},
        {"actionMesh2DGWInitCond", QT_TR_NOOP("State")},
        {"actionMesh2DGWEdgeBoundaries", QT_TR_NOOP("Edge Boundaries")},
        {"actionMesh2DGWNodeBeds", QT_TR_NOOP("Node Beds")},
        {"actionMesh2DSurfaceQuality", QT_TR_NOOP("Surface\nQuality")},
        {"actionAssignSurfaceQuality", QT_TR_NOOP("Assign\nSurface Quality")},
        {"actionMeshSnow", QT_TR_NOOP("Assign\nSnow")},
        {"actionMeshHeatFromRaster", QT_TR_NOOP("Heat from\nRaster")},
        {"actionMeshProfile", QT_TR_NOOP("Mesh\nProfile")},
        // Analysis
        {"actionSummarizeResults", QT_TR_NOOP("Summarize")},
        {"actionTabularView", QT_TR_NOOP("Tabular\nView")},
        {"actionTabularResults", QT_TR_NOOP("Tabular\nResults")},
        {"actionPlotTimeSeries", QT_TR_NOOP("Time\nSeries")},
        {"actionPlotProfile", QT_TR_NOOP("Profile")},
        {"actionPlotProfile2D", QT_TR_NOOP("2D Profile")},
        {"actionRainfallVisualization", QT_TR_NOOP("Rainfall Visualization")},
        {"actionSaved2DSections", QT_TR_NOOP("Saved 2D Sections")},
        {"actionFlowBalanceDownstream", QT_TR_NOOP("Flow Balance Downstream")},
        {"actionFlowBalanceUpstream", QT_TR_NOOP("Flow Balance Upstream")},
        {"actionTravelTimeDownstream", QT_TR_NOOP("Travel Time Downstream")},
        {"actionTravelTimeUpstream", QT_TR_NOOP("Travel Time Upstream")},
        {"actionExport2DResults", QT_TR_NOOP("Export\n2D Results")},
        {"actionToggleDockToolbox", QT_TR_NOOP("Analysis\nToolbox")},
        // Animation
        {"actionSkipBack", QT_TR_NOOP("Skip\nBack")},
        {"actionSkipForward", QT_TR_NOOP("Skip\nForward")},
        {"actionShowLegend", QT_TR_NOOP("Show\nLegend")},
        {"actionSetStyle", QT_TR_NOOP("Set\nStyle")},
    };
    for (const auto &entry : kShortLabels)
        if (auto *act = findChild<QAction *>(QLatin1String(entry.name)))
            act->setIconText(tr(entry.label));

    // ── Home: project files, navigation, derived selection, data in, run,
    //    and the window (panels and styling) that replaced the View tab.
    mToolBarHome = new QToolBar(tr("Home"), this);
    mToolBarHome->setObjectName(QStringLiteral("toolBarHome"));
    addStacks(addGroup(mToolBarHome, tr("Project"), {"actionNew", "actionOpen", "actionSave"}),
              {{"actionSaveAs", "actionExportMap", "actionCopy"}});
    addGroup(mToolBarHome, tr("Navigate"),
             {"actionZoomIn", "actionZoomOut", "actionZoomToSelection", "actionMeasure"});
    addGroup(mToolBarHome, tr("Selection"),
             {"actionInvertSelection", "actionSelectUpstream", "actionSelectDownstream",
              "actionSearch"});
    addStacks(addGroup(mToolBarHome, tr("Import"), {}),
              {{"actionAddSWMMOutput", "actionAdd2DResults", "actionAddMesh2D"},
               {"actionAddVectorData", "actionAddRasterData", "actionAddDelimeteredData"},
               {"actionAddWMSData", "actionAddWFSData", "actionAddBasemap"}});
    addStacks(addGroup(mToolBarHome, tr("Run"), {"actionExecute"}),
              {{"actionPauseExecution", "actionCancelExecution",
                "actionToggleDockSimulationStatus"}});
    {
        // Every dock toggle stays one click away behind the same View →
        // Panels menu (the former View tab repeated all sixteen as faces).
        auto *group = addGroup(mToolBarHome, tr("Window"), {});
        auto *panels =
            new QAction(IconFactory::icon(QStringLiteral("PanelLeft")), tr("Panels"), this);
        panels->setObjectName(QStringLiteral("actionPanelsMenu"));
        panels->setToolTip(tr("Show or hide a panel — the same list as View → Panels"));
        panels->setMenu(findChild<QMenu *>(QStringLiteral("menuViewPanels")));
        group->addAction(panels)->setPopupMode(QToolButton::InstantPopup);
        addStacks(group, {{"actionLayerStylingDock", "actionStyleManager", "actionShowWelcome"}});
    }

    // ── Model: network authoring, catchment and climate, data objects,
    //    process definitions and project setup.
    mToolBarModel = new QToolBar(tr("Model"), this);
    mToolBarModel->setObjectName(QStringLiteral("toolBarModel"));
    addGroup(mToolBarModel, tr("Edit"), {"actionEditExisting"});
    addStacks(addGroup(mToolBarModel, tr("Nodes"), {}),
              {{"actionAddJunction", "actionAddOutfall", "actionAddStorage"},
               {"actionAddFlowDivider", "actionAddVirtualJunction", "actionAddInletJunction"},
               {"actionAddLidNode"}});
    addStacks(addGroup(mToolBarModel, tr("Links"), {}),
              {{"actionAddPipe", "actionAddPump", "actionAddOrifice"},
               {"actionAddWeir", "actionAddOutlet"}});
    {
        // Subcatchment and Rain Gage are drawing tools like the node and link
        // tools, so they take stack rows too; Climate keeps its split face.
        auto *group = addGroup(mToolBarModel, tr("Catchment"), {});
        addStacks(group, {{"actionAddSubcatchment", "actionRainGauge", "actionAssignRainGages"}});
        addStacks(group, {{"actionNewSnowpack", "actionNewUnitHydrograph"}});
        // R4 — last-used split-button family: the face is the last-used
        // member (persisted per family id) and mirrors its checked state.
        group->addFamily(QStringLiteral("climate"),
                         resolve({"actionTemperature", "actionWind", "actionSnow",
                                  "actionEvaporation", "actionSolarRadiation"}));
    }
    addStacks(addGroup(mToolBarModel, tr("Data Objects"), {}),
              {{"actionNewTimeSeries", "actionNewCurve", "actionNewPattern"},
               {"actionNewTransect", "actionNewStreet", "actionNewInlet"},
               {"actionNewAquifer", "actionNewLidControl", "actionNewControlRule"}});
    addStacks(addGroup(mToolBarModel, tr("Processes"), {}),
              {{"actionNewPollutant", "actionNewLandUse", "actionEditReactionSystem"},
               {"actionEditInitialQuality", "actionEditWaterAgeSources", "actionEditHeatConfig"}});
    if (ui->actionAddText) {
        ui->actionAddText->setText(tr("Text Layers…"));
        ui->actionAddText->setToolTip(
            tr("Manage text layers, generate labels and edit text on the map"));
    }
    addStacks(addGroup(mToolBarModel, tr("Setup"), {"actionOptions", "actionImportFeatureLayer"}),
              {{"actionUserFlags", "actionSetProjectCRS", "actionAddText"}});
    // Generate Mesh lives here, not on the contextual Mesh 2D tab: it is how
    // a project gets its first mesh, before that tab can appear.
    addGroup(mToolBarModel, tr("2D Mesh"), {"actionGenerateMesh"});

    // Features — editable, GeoPackage-backed layers the user draws
    // (MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN §5.1). Its own tab rather
    // than a Model group: the drawing tools are modal, and none of it edits
    // the SWMM network. Returning to plain selection after a drawing tool is
    // the Quick Access Select button, visible on every tab.
    mToolBarFeatures = new QToolBar(tr("Features"), this);
    mToolBarFeatures->setObjectName(QStringLiteral("toolBarFeatures"));
    addGroup(mToolBarFeatures, tr("Layer"), {"actionNewFeatureLayer"});
    addGroup(mToolBarFeatures, tr("Edit"), {"actionFeatureEditMode", "actionFeatureDelete"});
    addGroup(mToolBarFeatures, tr("Draw"),
             {"actionFeatureDrawPoint", "actionFeatureDrawLine", "actionFeatureDrawPolygon"});
    addStacks(addGroup(mToolBarFeatures, tr("Modify"), {}),
              {{"actionFeatureAddPart", "actionFeatureAddHole", "actionFeatureEditVertex"},
               {"actionFeatureMove"}});

    // Mesh 2D (contextual) is one bar: MeshEditingToolbar builds Mesh →
    // Process → Select → the active process's group → Cell Data → Profile
    // (process-aware editing plan §4.2). Burn Channels joins its Mesh group.
    if (mMeshEditingToolbar)
        for (QAction *act : resolve({"actionBurnChannels"}))
            mMeshEditingToolbar->addMeshAction(act);

    // Analysis: the "Results Layers" widget group is added in
    // initializeAnalysisLayerCombos; action groups follow here. Show Mass
    // Balance stays menu-only — it opens the same viewer as Report.
    addGroup(mToolBarAnalysis, tr("Report"),
             {"actionSummarizeResults", "actionReport", "actionTabularView",
              "actionTabularResults"});
    mGroupPlots = addGroup(mToolBarAnalysis, tr("Plots"),
                           {"actionPlotTimeSeries", "actionPlotProfile", "actionPlotProfile2D"});
    addStacks(mGroupPlots, {{"actionRainfallVisualization", "actionSaved2DSections"}});
    addStacks(addGroup(mToolBarAnalysis, tr("Network"), {}),
              {{"actionFlowBalanceDownstream", "actionFlowBalanceUpstream"},
               {"actionTravelTimeDownstream", "actionTravelTimeUpstream"}});
    addGroup(mToolBarAnalysis, tr("Export"), {"actionExport2DResults"});
    addGroup(mToolBarAnalysis, tr("Toolbox"), {"actionToggleDockToolbox"});

    // Left-pack (iteration 3): groups are horizontally Fixed, so an
    // Expanding zero-min trailing spacer absorbs the leftover row width
    // — set icon separation, unused gap at the end by design. The
    // Results bar is skipped: its Timeline group is itself Expanding and
    // takes the slack there. RibbonCompactor discounts the spacer's one
    // inter-item spacing by objectName.
    for (QToolBar *bar : {mToolBarHome, mToolBarModel, mToolBarAnalysis, mToolBarFeatures}) {
        auto *spacer = new QWidget(bar);
        spacer->setObjectName(QStringLiteral("ribbonBarSpacer"));
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        spacer->setMinimumSize(0, 0);
        bar->addWidget(spacer);
    }

    mCompactToolbar = new CompactToolbarController(this);
    mCompactToolbar->addTab(QStringLiteral("home"), tr("Home"), {mToolBarHome});
    mCompactToolbar->addTab(QStringLiteral("model"), tr("Model"), {mToolBarModel});
    // Contextual tabs appear with their content (updateContextualTabs):
    // Terrain with a raster, Mesh 2D with a mesh or 2D results, Animation
    // with any results layer.
    mCompactToolbar->addTab(QStringLiteral("terrain"), tr("Terrain"),
                            QList<QToolBar *>{static_cast<QToolBar *>(mTerrainToolbar)},
                            /*contextual*/ true);
    mCompactToolbar->addTab(QStringLiteral("mesh2d"), tr("Mesh 2D"),
                            QList<QToolBar *>{static_cast<QToolBar *>(mMeshEditingToolbar)},
                            /*contextual*/ true);
    // Non-contextual: the New Feature Layer action must be reachable before
    // any feature layer exists, so the tab cannot be gated on having one.
    // Its actions are still project-gated by applyProjectOpenToActions.
    mCompactToolbar->addTab(QStringLiteral("features"), tr("Features"), {mToolBarFeatures});
    mCompactToolbar->addTab(QStringLiteral("analysis"), tr("Analysis"), {mToolBarAnalysis});
    mCompactToolbar->addTab(QStringLiteral("results"), tr("Animation"), {mToolBarAnimation},
                            /*contextual*/ true);

    // Quick Access: a permanent group at the left of the tools row, below
    // the tabs. Its larger targets never increase the tab-strip height.
    // Select reuses the "select" split family
    // (and its persisted last-used member); the faces are the same QActions
    // as the menus, so checked state follows the canvas tool everywhere.
    {
        auto *bar = new QToolBar(tr("Quick Access"), this);
        bar->setObjectName(QStringLiteral("toolBarQuickAccess"));
        auto *group = new RibbonGroup(tr("Quick Access"), bar);
        auto *quick = new QWidget(group);
        quick->setObjectName(QStringLiteral("ribbonQuickAccess"));
        quick->setAccessibleName(tr("Quick Access"));
        auto *row = new QHBoxLayout(quick);
        row->setContentsMargins(2, 0, 2, 0);
        row->setSpacing(2);
        const QSize iconSize(kRibbonIconFull, kRibbonIconFull);
        constexpr int buttonSize = 44;
        auto *select = new RibbonSplitButton(
            QStringLiteral("select"), resolve({"actionSelect", "actionSelectByPolygon"}), quick);
        select->setToolButtonStyle(Qt::ToolButtonIconOnly);
        select->setIconSize(iconSize);
        // Keep a full-size primary target in addition to the native menu arrow.
        const int menuWidth = select->style()->pixelMetric(QStyle::PM_MenuButtonIndicator,
                                                          nullptr, select);
        select->setFixedSize(buttonSize + menuWidth, buttonSize);
        row->addWidget(select);
        /**
         * @brief Append icon-only Quick Access buttons. Argument `names`: action object names
         * resolved synchronously; absent names are skipped.
         */
        const auto addQuick = [&](std::initializer_list<const char *> names) {
            for (QAction *act : resolve(names)) {
                auto *button = new QToolButton(quick);
                button->setAutoRaise(true);
                button->setDefaultAction(act);
                button->setToolButtonStyle(Qt::ToolButtonIconOnly);
                button->setIconSize(iconSize);
                button->setFixedSize(buttonSize, buttonSize);
                row->addWidget(button);
            }
        };
        addQuick({"actionPan", "actionZoomExtent"});
        auto *rule = new QFrame(quick);
        rule->setFrameShape(QFrame::VLine);
        rule->setFrameShadow(QFrame::Plain);
        rule->setForegroundRole(QPalette::Dark);
        rule->setFixedHeight(26);
        row->addWidget(rule);
        addQuick({"actionUndo", "actionRedo"});
        group->addWidget(quick);
        bar->addWidget(group);
        mCompactToolbar->setPermanentToolbar(bar);
    }
    mCompactToolbar->finalize();

    // R5 — responsive ribbon: one compactor per group-bearing bar steps
    // trailing groups Full → Compact → Collapsed as width shrinks
    // (promotions only past the 32 px dead band). The Mesh 2D bar's
    // process groups that are hidden are left out of the solve.
    for (QToolBar *bar : {mToolBarHome, mToolBarModel, static_cast<QToolBar *>(mMeshEditingToolbar),
                          mToolBarAnalysis, mToolBarAnimation, mToolBarFeatures}) {
        const auto groups = bar->findChildren<RibbonGroup *>(Qt::FindDirectChildrenOnly);
        if (!groups.isEmpty())
            new openswmmvis::ui::RibbonCompactor(bar, groups);
    }

    // Right corner of the tab strip: the command-palette launcher (P7).
    if (auto *paletteAction = findChild<QAction *>(QStringLiteral("actionCommandPalette")))
        mCompactToolbar->stripToolBar()->addAction(paletteAction);
}

void SWMMVis::updateContextualTabs()
{
    const bool haveProject = activeProjectWindow() != nullptr;
    for (const char *name : {"actionAddVectorData", "actionAddRasterData", "actionAddSWMMOutput",
                             "actionAddMesh2D", "actionAdd2DResults", "actionAddWMSData",
                             "actionAddWFSData", "actionAddBasemap", "actionAddDelimeteredData"}) {
        if (auto *action = findChild<QAction *>(QLatin1String(name)))
            action->setEnabled(haveProject);
    }
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
    /**
     * @brief Test contextual mesh-tab eligibility. Argument `c`: nullable borrowed canvas read
     * synchronously. Returns: true if any layer is a mesh or 2D results (visibility is irrelevant),
     * false otherwise.
     */
    const auto hasMesh2DContent = [](MapCanvas *c) {
        if (!c)
            return false;
        const auto layers = c->layers();
        for (OpenSWMMVisLayer *layer : layers) {
            if (qobject_cast<SWMM2DMeshLayer *>(layer) || qobject_cast<SWMM2DResultsLayer *>(layer))
                return true;
        }
        return false;
    };
    // The Terrain tab reveals once any raster (DEM candidate) is loaded.
    /**
     * @brief Test contextual terrain-tab eligibility. Argument `c`: nullable borrowed canvas.
     * Returns: true if any GISRasterLayer exists, including invisible/basemap rasters; false
     * otherwise.
     */
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

    // The Animation tab reveals with any results layer (1D or 2D, including
    // the live layer a running simulation attaches).
    /**
     * @brief Test contextual animation-tab eligibility. Argument `c`: nullable borrowed canvas.
     * Returns: true if any 1D or 2D results layer exists; false otherwise.
     */
    const auto hasResults = [](MapCanvas *c) {
        if (!c)
            return false;
        const auto layers = c->layers();
        for (OpenSWMMVisLayer *layer : layers) {
            if (qobject_cast<SWMMResultsLayer *>(layer) || qobject_cast<SWMM2DResultsLayer *>(layer))
                return true;
        }
        return false;
    };

    mCompactToolbar->setTabVisible(QStringLiteral("mesh2d"), hasMesh2DContent(canvas));
    mCompactToolbar->setTabVisible(QStringLiteral("terrain"), hasRaster(canvas));
    mCompactToolbar->setTabVisible(QStringLiteral("results"), hasResults(canvas));
    if (canvas) {
        /**
         * @brief Refresh Terrain/Mesh/Animation contextual tab visibility on the captured active canvas.
         * Takes no arguments and returns no value; connections use the compact-controller lifetime
         * and are explicitly replaced on project switches. Does not own the raw canvas.
         */
        const auto refresh = [this, canvas, hasMesh2DContent, hasRaster, hasResults] {
            mCompactToolbar->setTabVisible(QStringLiteral("mesh2d"), hasMesh2DContent(canvas));
            mCompactToolbar->setTabVisible(QStringLiteral("terrain"), hasRaster(canvas));
            mCompactToolbar->setTabVisible(QStringLiteral("results"), hasResults(canvas));
        };
        // Receiver context is the CONTROLLER, not `this`:
        // onActiveSubWindowChanged later blanket-disconnects
        // (canvas, layerAdded, this, nullptr) for the results combos,
        // which silently killed this refresh — the mesh loads
        // asynchronously AFTER activation, so the Mesh 2D tab never
        // appeared (iteration-3 regression fix).
        mMeshTabConnAdd = connect(canvas, &MapCanvas::layerAdded, mCompactToolbar, refresh);
        mMeshTabConnRemove = connect(canvas, &MapCanvas::layerRemoved, mCompactToolbar, refresh);
    }
}

QMenu *SWMMVis::createPopupMenu()
{
    // Dock toggles only — the compact-toolbar rows are tab-managed and
    // individually hiding them would break the tab illusion.
    auto *menu = new QMenu(this);
    for (const char *name :
         {"actionToggleDockToolbox", "actionToggleDockLayers", "actionToggleDockObjectBrowser",
          "actionToggleDockProperties", "actionToggleDockSectionView", "actionToggleDockAttributeTable",
          "actionToggleDockLegend", "actionToggleDockSimulationStatus", "actionToggleDockMessageLogs"}) {
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

    MapUndoStack *stack = (pw && pw->canvas()) ? pw->canvas()->undoStack() : nullptr;
    if (!stack) {
        undo->setEnabled(false);
        redo->setEnabled(false);
        return;
    }
    undo->setEnabled(stack->canUndo());
    redo->setEnabled(stack->canRedo());
    mUndoEnableConn = connect(stack, &QUndoStack::canUndoChanged, undo, &QAction::setEnabled);
    mRedoEnableConn = connect(stack, &QUndoStack::canRedoChanged, redo, &QAction::setEnabled);
}
