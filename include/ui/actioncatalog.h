#ifndef ACTIONCATALOG_H
#define ACTIONCATALOG_H

/**
 * @file      actioncatalog.h
 * @brief     UI redesign P1 — the action catalog: one data table describing every main-window
 *            capability.
 *
 * UI redesign P1 — the action catalog: one data table describing every
 * main-window capability. Follows the swmmvis_hydration_audit.h idiom:
 * a pure constexpr table with zero widget dependencies so headless tests
 * can audit it, while ActionRegistry consumes it at runtime to adopt the
 * existing QAction objects (never re-creating them — object identity,
 * objectName-keyed tool sync, and saved window state all keep working).
 *
 * Field semantics:
 *  - id:              stable dotted identifier ("file.new"). QSettings
 *                     shortcut overrides and the command palette key off
 *                     this, so ids are append-only; never repurpose one.
 *  - objectName:      the QAction objectName the registry adopts. Every
 *                     entry names an action that exists by the end of the
 *                     SWMMVis constructor.
 *  - category:        human grouping for the palette / shortcut editor.
 *  - defaultShortcut: "" = none; "std:Name" = QKeySequence::StandardKey
 *                     (platform-correct bindings); anything else is a
 *                     portable QKeySequence string (Ctrl maps to Cmd on
 *                     macOS at runtime).
 *  - icon:            ":/swmmvis/<alias>" qrc alias (without the prefix)
 *                     rendered through the theme-aware IconFactory;
 *                     "" = the action keeps whatever icon it already has.
 *  - tab:             where the action's ONE ribbon face lives: a compact-
 *                     toolbar tab id, "strip" for the Quick Access cluster
 *                     on the tab strip, or "" for menu/palette only.
 *                     test_ribbon_organization compares it with the mounted
 *                     ribbon, so a regrouping must update it.
 *  - menuPath:        canonical menu location (documentation + menubar
 *                     audit; "/" separates submenu levels).
 *  - tags:            behavior bits, see ActionTag.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <iterator>

namespace openswmmvis::ui
{

enum ActionTag : unsigned {
    NoTags = 0u,
    /// Enabled only while a project is open (applyProjectOpenToActions set).
    RequiresProject = 1u << 0,
    /// Enabled only during an explicit edit session (applyEditSessionToActions set).
    RequiresEditSession = 1u << 1,
    /// Belongs to the contextual Mesh 2D surface.
    Contextual2D = 1u << 2,
};

struct ActionCatalogEntry {
    const char *id;
    const char *objectName;
    const char *category;
    const char *defaultShortcut;
    const char *icon;
    const char *tab;
    const char *menuPath;
    unsigned tags;
};

inline constexpr ActionCatalogEntry kActionCatalog[] = {
    // ── File ────────────────────────────────────────────────────────────
    {"file.new", "actionNew", "File", "std:New", "New", "home", "File", NoTags},
    {"file.open", "actionOpen", "File", "std:Open", "Open", "home", "File", NoTags},
    {"file.save", "actionSave", "File", "std:Save", "Save", "home", "File", NoTags},
    {"file.saveAs", "actionSaveAs", "File", "std:SaveAs", "SaveAs", "home", "File", NoTags},
    {"file.exportMap", "actionExportMap", "File", "", "ExportMap", "home", "File", NoTags},
    {"file.print", "actionPrint", "File", "std:Print", "Print", "", "File", NoTags},
    {"file.clearRecent", "actionClearRecentFiles", "File", "", "ClearRecent", "",
     "File/Open Recent", NoTags},
    // Literal Ctrl+, (not std:Preferences): the offscreen QPA's platform
    // theme resolves that StandardKey to nothing, and Ctrl+, is the
    // convention on every platform anyway (Cmd+, on macOS via Qt::CTRL).
    {"app.preferences", "actionSettings", "Application", "Ctrl+,", "Settings", "", "Tools", NoTags},
    // Literal Ctrl+Q for the same offscreen-theme reason as app.preferences.
    {"app.quit", "actionExit", "Application", "Ctrl+Q", "Exit", "", "File", NoTags},

    // ── Edit / selection ────────────────────────────────────────────────
    {"edit.undo", "actionUndo", "Edit", "std:Undo", "Undo", "strip", "Edit", NoTags},
    {"edit.redo", "actionRedo", "Edit", "std:Redo", "Redo", "strip", "Edit", NoTags},
    {"edit.copy", "actionCopy", "Edit", "std:Copy", "Copy", "home", "Edit", NoTags},
    {"find.search", "actionSearch", "Edit", "std:Find", "Search", "home", "Edit", NoTags},
    {"edit.editExisting", "actionEditExisting", "Edit", "Ctrl+E", "SelectEdit", "model", "Edit",
     NoTags},
    {"select.invert", "actionInvertSelection", "Edit", "", "InvertSelection", "home", "Edit",
     NoTags},
    {"select.upstream", "actionSelectUpstream", "Edit", "", "SelectUpstream", "home", "Edit",
     NoTags},
    {"select.downstream", "actionSelectDownstream", "Edit", "", "SelectDownstream", "home", "Edit",
     NoTags},

    // ── Map navigation / tools ──────────────────────────────────────────
    {"map.pan", "actionPan", "Map", "", "Move", "strip", "View", NoTags},
    {"map.zoomIn", "actionZoomIn", "Map", "std:ZoomIn", "ZoomIn", "home", "View", NoTags},
    {"map.zoomOut", "actionZoomOut", "Map", "std:ZoomOut", "ZoomOut", "home", "View", NoTags},
    {"map.zoomExtent", "actionZoomExtent", "Map", "Ctrl+Shift+F", "Extent", "strip", "View",
     NoTags},
    {"map.zoomToSelection", "actionZoomToSelection", "Map", "Ctrl+Shift+J", "ZoomToSelection",
     "home", "View", NoTags},
    {"map.select", "actionSelect", "Map", "", "Select", "strip", "Edit", NoTags},
    {"map.selectByPolygon", "actionSelectByPolygon", "Map", "", "SelectByPolygon", "strip", "Edit",
     NoTags},
    {"map.measure", "actionMeasure", "Map", "Ctrl+Shift+M", "Ruler", "home", "View", NoTags},

    // ── Model authoring (enabled while a project is open) ───────────────
    {"model.addJunction", "actionAddJunction", "Model", "", "Junction", "model", "Model/Add Node",
     RequiresProject},
    {"model.addVirtualJunction", "actionAddVirtualJunction", "Model", "", "VirtualJunction",
     "model", "Model/Add Node", RequiresProject},
    {"model.addJunctionSplit", "actionAddJunctionSplit", "Model", "", "JunctionSplit", "model",
     "Model/Add Node", RequiresProject},
    {"model.addInletJunction", "actionAddInletJunction", "Model", "", "InletJunction", "model",
     "Model/Add Node", RequiresProject},
    {"model.addOutfall", "actionAddOutfall", "Model", "", "Outfall", "model", "Model/Add Node",
     RequiresProject},
    {"model.addFlowDivider", "actionAddFlowDivider", "Model", "", "Divider", "model",
     "Model/Add Node", RequiresProject},
    {"model.addLidNode", "actionAddLidNode", "Model", "", "Storage", "model", "Model/Add Node",
     RequiresProject},
    {"model.addStorage", "actionAddStorage", "Model", "", "Storage", "model", "Model/Add Node",
     RequiresProject},
    {"model.addPipe", "actionAddPipe", "Model", "", "Polyline", "model", "Model/Add Link",
     RequiresProject},
    {"model.addPump", "actionAddPump", "Model", "", "Pump", "model", "Model/Add Link",
     RequiresProject},
    {"model.addOrifice", "actionAddOrifice", "Model", "", "Orifice", "model", "Model/Add Link",
     RequiresProject},
    {"model.addWeir", "actionAddWeir", "Model", "", "Weir", "model", "Model/Add Link",
     RequiresProject},
    {"model.addOutlet", "actionAddOutlet", "Model", "", "Outlet", "model", "Model/Add Link",
     RequiresProject},
    {"model.addSubcatchment", "actionAddSubcatchment", "Model", "", "Subcatchment", "model",
     "Model", RequiresProject},
    {"model.addRainGage", "actionRainGauge", "Model", "", "Rainfall", "model", "Model",
     RequiresProject},
    {"model.addText", "actionAddText", "Model", "", "Text", "model", "Model", RequiresProject},
    {"model.importFeatureLayer", "actionImportFeatureLayer", "Model", "", "ImportGIS", "model",
     "Model", NoTags},
    {"model.assignRainGages", "actionAssignRainGages", "Model", "", "AssignRainGages", "model",
     "Model", RequiresProject},
    {"model.simulationOptions", "actionOptions", "Model", "", "Options", "model", "Model", NoTags},
    {"model.userFlags", "actionUserFlags", "Model", "", "UserFlags", "model", "Model", NoTags},
    {"model.editReactionSystem", "actionEditReactionSystem", "Model", "", "ReactionSystem", "model",
     "Model", NoTags},
    {"model.editHeatConfig", "actionEditHeatConfig", "Model", "", "HeatConfig", "model", "Model",
     NoTags},
    {"model.editInitialQuality", "actionEditInitialQuality", "Model", "", "Droplet", "model",
     "Model", NoTags},
    {"model.editWaterAgeSources", "actionEditWaterAgeSources", "Model", "", "TimeCursor", "model",
     "Model", NoTags},
    {"model.projectCrs", "actionSetProjectCRS", "Model", "", "Globe", "model", "Model", NoTags},
    {"model.generateMesh", "actionGenerateMesh", "Model", "", "CreateMesh", "model", "Model",
     NoTags},
    {"model.burnChannels", "actionBurnChannels", "Mesh 2D", "", "BurnChannels", "mesh2d",
     "Model/Mesh", Contextual2D},

    // ── Feature layers (user-drawn GeoPackage-backed vector layers;
    //    MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN §5.1) ─────────────────
    // RequiresProject: every one of these writes into the project's own
    // .features.gpkg, which is derived from the model path, so none of them
    // means anything before a project is open.
    {"features.newLayer", "actionNewFeatureLayer", "Features", "", "AddVector", "features",
     "Model/Feature Layers", RequiresProject},
    // Edit mode gates the seven tools and the attribute grid, the way a GIS
    // package does: nothing on a feature layer is mutable until it is on.
    {"features.editMode", "actionFeatureEditMode", "Features", "", "Edit", "features",
     "Model/Feature Layers", RequiresProject},
    {"features.delete", "actionFeatureDelete", "Features", "Del", "Delete", "features",
     "Model/Feature Layers", RequiresProject},
    {"features.drawPoint", "actionFeatureDrawPoint", "Features", "", "Node", "features",
     "Model/Feature Layers", RequiresProject},
    {"features.drawLine", "actionFeatureDrawLine", "Features", "", "Polyline", "features",
     "Model/Feature Layers", RequiresProject},
    {"features.drawPolygon", "actionFeatureDrawPolygon", "Features", "", "Polygon", "features",
     "Model/Feature Layers", RequiresProject},
    {"features.addPart", "actionFeatureAddPart", "Features", "", "Add", "features",
     "Model/Feature Layers", RequiresProject},
    {"features.addHole", "actionFeatureAddHole", "Features", "", "Clear", "features",
     "Model/Feature Layers", RequiresProject},
    {"features.editVertex", "actionFeatureEditVertex", "Features", "", "InsertVertex", "features",
     "Model/Feature Layers", RequiresProject},
    {"features.moveFeature", "actionFeatureMove", "Features", "", "Move", "features",
     "Model/Feature Layers", RequiresProject},

    // ── Climatology ─────────────────────────────────────────────────────
    {"climate.temperature", "actionTemperature", "Climate", "", "Thermometer", "model",
     "Model/Climate", NoTags},
    {"climate.evaporation", "actionEvaporation", "Climate", "", "Evaporation", "model",
     "Model/Climate", NoTags},
    {"climate.wind", "actionWind", "Climate", "", "Wind", "model", "Model/Climate", NoTags},
    {"climate.snow", "actionSnow", "Climate", "", "Snow", "model", "Model/Climate", NoTags},
    {"climate.solarRadiation", "actionSolarRadiation", "Climate", "", "Sun", "model",
     "Model/Climate", NoTags},

    // ── Data objects (programmatic Data menu; objectNames assigned at
    //    creation in initializeMenus) ─────────────────────────────────────
    {"data.newTimeSeries", "actionNewTimeSeries", "Data", "", "AddTimeSeries", "model",
     "Model/Data Objects", NoTags},
    {"data.newCurve", "actionNewCurve", "Data", "", "AddCurve", "model", "Model/Data Objects",
     NoTags},
    {"data.newPattern", "actionNewPattern", "Data", "", "AddPattern", "model", "Model/Data Objects",
     NoTags},
    {"data.newLidControl", "actionNewLidControl", "Data", "", "LidControl", "model",
     "Model/Data Objects", NoTags},
    {"data.newPollutant", "actionNewPollutant", "Data", "", "Pollutant", "model",
     "Model/Data Objects", NoTags},
    {"data.newLandUse", "actionNewLandUse", "Data", "", "LandUse", "model", "Model/Data Objects",
     NoTags},
    {"data.newAquifer", "actionNewAquifer", "Data", "", "Aquifer", "model", "Model/Data Objects",
     NoTags},
    {"data.newSnowpack", "actionNewSnowpack", "Data", "", "Snowpack", "model", "Model/Data Objects",
     NoTags},
    {"data.newControlRule", "actionNewControlRule", "Data", "", "AddControlRule", "model",
     "Model/Data Objects", NoTags},
    {"data.newTransect", "actionNewTransect", "Data", "", "AddTransect", "model",
     "Model/Data Objects", NoTags},
    {"data.newUnitHydrograph", "actionNewUnitHydrograph", "Data", "", "UnitHydrograph", "model",
     "Model/Data Objects", NoTags},
    {"data.newStreet", "actionNewStreet", "Data", "", "Street", "model", "Model/Data Objects",
     NoTags},
    {"data.newInlet", "actionNewInlet", "Data", "", "Inlet", "model", "Model/Data Objects", NoTags},

    // ── Layer / data import ─────────────────────────────────────────────
    {"import.swmmOutput", "actionAddSWMMOutput", "Import", "", "AddSWMMOutput", "home",
     "File/Import", NoTags},
    {"import.vector", "actionAddVectorData", "Import", "", "AddVector", "home", "File/Import",
     NoTags},
    {"import.raster", "actionAddRasterData", "Import", "", "AddRaster", "home", "File/Import",
     NoTags},
    {"import.wms", "actionAddWMSData", "Import", "", "AddWMS", "home", "File/Import", NoTags},
    {"import.wfs", "actionAddWFSData", "Import", "", "AddWFS", "home", "File/Import", NoTags},
    {"import.delimited", "actionAddDelimeteredData", "Import", "", "AddDelimetered", "home",
     "File/Import", NoTags},
    {"import.basemap", "actionAddBasemap", "Import", "", "AddBasemap", "home", "File/Import",
     NoTags},
    {"import.mesh2d", "actionAddMesh2D", "Import", "", "AddMesh", "home", "File/Import", NoTags},

    // ── Simulation ──────────────────────────────────────────────────────
    {"sim.run", "actionExecute", "Simulation", "Ctrl+R", "Execute", "home", "Analysis", NoTags},
    {"sim.componentExecution", "actionComponentExecution", "Simulation", "", "Settings", "", "Analysis", RequiresProject},
    {"sim.pause", "actionPauseExecution", "Simulation", "", "Pause", "home", "Analysis", NoTags},
    {"sim.stop", "actionCancelExecution", "Simulation", "Ctrl+.", "CancelExecution", "home",
     "Analysis", NoTags},

    // ── Results playback ────────────────────────────────────────────────
    {"results.play", "actionPlay", "Playback", "", "Play", "results", "Results", NoTags},
    {"results.pause", "actionPause", "Playback", "", "Pause", "results", "Results", NoTags},
    {"results.stop", "actionStop", "Playback", "", "Stop", "results", "Results", NoTags},
    {"results.skipBack", "actionSkipBack", "Playback", "", "SkipBack", "results", "Results",
     NoTags},
    {"results.skipForward", "actionSkipForward", "Playback", "", "SkipForward", "results",
     "Results", NoTags},
    {"results.setStyle", "actionSetStyle", "Playback", "", "Style", "results", "Results", NoTags},
    {"view.showLegend", "actionShowLegend", "View", "", "Legend", "results", "View", NoTags},

    // ── Analysis / reporting ────────────────────────────────────────────
    {"analysis.summarize", "actionSummarizeResults", "Analysis", "", "Summarize", "analysis",
     "Analysis", NoTags},
    {"analysis.report", "actionReport", "Analysis", "", "Report", "analysis", "Analysis", NoTags},
    {"analysis.tabularView", "actionTabularView", "Analysis", "Ctrl+Shift+A", "TableView",
     "analysis", "Analysis", NoTags},
    {"analysis.tabularResults", "actionTabularResults", "Analysis", "", "TableView", "analysis",
     "Analysis", NoTags},
    {"analysis.plotTimeSeries", "actionPlotTimeSeries", "Analysis", "Ctrl+T", "Chart", "analysis",
     "Analysis", NoTags},
    {"analysis.plotProfile", "actionPlotProfile", "Analysis", "Ctrl+Shift+T", "Profile", "analysis",
     "Analysis", NoTags},
    {"analysis.plotProfile2D", "actionPlotProfile2D", "Analysis", "", "Profile2D", "analysis",
     "Analysis", NoTags},
    {"analysis.savedSections2D", "actionSaved2DSections", "Analysis", "", "Profile2D", "analysis",
     "Analysis", NoTags},
    {"analysis.flowBalanceDown", "actionFlowBalanceDownstream", "Analysis", "",
     "FlowBalanceDownstream", "analysis", "Analysis", NoTags},
    {"analysis.flowBalanceUp", "actionFlowBalanceUpstream", "Analysis", "", "FlowBalanceUpstream",
     "analysis", "Analysis", NoTags},
    {"analysis.travelTimeDown", "actionTravelTimeDownstream", "Analysis", "",
     "TravelTimeDownstream", "analysis", "Analysis", NoTags},
    {"analysis.travelTimeUp", "actionTravelTimeUpstream", "Analysis", "", "TravelTimeUpstream",
     "analysis", "Analysis", NoTags},
    // Menu-only: it opens the same report viewer as Report, so a second
    // ribbon face would be a duplicate (process-aware editing plan §3.2).
    {"analysis.massBalance", "actionShowMassBalance", "Analysis", "", "Chartpie", "",
     "Analysis", NoTags},
    {"analysis.rainfallVisualization", "actionRainfallVisualization", "Analysis", "", "Rainfall",
     "analysis", "Analysis", NoTags},
    {"analysis.export2DResults", "actionExport2DResults", "Analysis", "", "Export2DResults",
     "analysis", "Analysis", NoTags},

    // ── Mesh 2D / terrain ───────────────────────────────────────────────
    {"mesh.selectVertices", "actionMeshSelectVertex", "Mesh 2D", "", "SelectTriNode", "mesh2d",
     "Model/Mesh", Contextual2D},
    {"mesh.selectEdges", "actionMeshSelectEdge", "Mesh 2D", "", "SelectTriEdge", "mesh2d",
     "Model/Mesh", Contextual2D},
    {"mesh.selectCells", "actionPick2DCells", "Mesh 2D", "", "SelectCell", "mesh2d", "Model/Mesh",
     Contextual2D},
    // Cell Data — assign per-cell parameters from a raster or a vector field.
    {"mesh.assignFromRaster", "actionMeshAssignFromRaster", "Mesh 2D", "", "MeshAssignRaster",
     "mesh2d", "Model/Mesh", Contextual2D},
    {"mesh.assignFromVector", "actionMeshAssignFromVector", "Mesh 2D", "", "MeshAssignVector",
     "mesh2d", "Model/Mesh", Contextual2D},
    // Groundwater (2D) — preview of the pending [2D_AQUIFER] editor.
    {"mesh.gw2dParams", "actionMesh2DGWParams", "Mesh 2D", "", "GW2DParams", "mesh2d", "Model/Mesh",
     Contextual2D},
    {"mesh.assignGroundwater", "actionAssignGroundwater", "Mesh 2D", "", "GW2DParams", "mesh2d",
     "Model/Mesh", Contextual2D},
    {"mesh.gw2dInitCond", "actionMesh2DGWInitCond", "Mesh 2D", "", "GW2DInit", "mesh2d",
     "Model/Mesh", Contextual2D},
    {"mesh.surfaceQuality2d", "actionMesh2DSurfaceQuality", "Mesh 2D", "", "LandUse", "mesh2d",
     "Model/Mesh", Contextual2D},
    {"mesh.assignSurfaceQuality", "actionAssignSurfaceQuality", "Mesh 2D", "", "LandUse",
     "mesh2d", "Model/Mesh", Contextual2D},
    // Mesh 2D tab commands (process-aware editing plan, Phase 2).
    {"mesh.autoCouple", "actionMeshAutoCouple", "Mesh 2D", "", "Snap", "mesh2d", "Model/Mesh",
     Contextual2D},
    {"mesh.remap", "actionMeshRemap", "Mesh 2D", "", "Remap1D2D", "mesh2d", "Model/Mesh",
     Contextual2D},
    {"mesh.clearSelection", "actionMeshClearSelection", "Mesh 2D", "", "SelectNone", "mesh2d",
     "Model/Mesh", Contextual2D},
    {"mesh.editInTable", "actionMeshEditInTable", "Mesh 2D", "", "TableView", "mesh2d",
     "Model/Mesh", Contextual2D},
    {"mesh.assignInfiltration", "actionMeshAssignInfilToSelection", "Mesh 2D", "", "Inundation2D",
     "mesh2d", "Model/Mesh", Contextual2D},
    {"mesh.reviewOwnership", "actionMeshReviewOwnership", "Mesh 2D", "", "Warning", "mesh2d",
     "Model/Mesh", Contextual2D},
    {"mesh.gw2dEdgeBoundaries", "actionMesh2DGWEdgeBoundaries", "Mesh 2D", "", "SelectTriEdge",
     "mesh2d", "Model/Mesh", Contextual2D},
    {"mesh.gw2dNodeBeds", "actionMesh2DGWNodeBeds", "Mesh 2D", "", "Node", "mesh2d", "Model/Mesh",
     Contextual2D},
    {"mesh.snow", "actionMeshSnow", "Mesh 2D", "", "Snowpack", "mesh2d", "Model", Contextual2D},
    {"mesh.heatFromRaster", "actionMeshHeatFromRaster", "Mesh 2D", "", "HeatConfig", "mesh2d",
     "Model/Mesh", Contextual2D},
    {"mesh.profile", "actionMeshProfile", "Mesh 2D", "", "Profile2D", "mesh2d", "Model/Mesh",
     Contextual2D},
    // Menu and palette mirror of the Mesh 2D process picker (no ribbon face).
    {"mesh.process.surfaceFlow", "actionMeshProcessSurfaceFlow", "Mesh 2D", "", "", "",
     "Model/Mesh/Process", Contextual2D},
    {"mesh.process.infiltration", "actionMeshProcessInfiltration", "Mesh 2D", "", "", "",
     "Model/Mesh/Process", Contextual2D},
    {"mesh.process.groundwater", "actionMeshProcessGroundwater", "Mesh 2D", "", "", "",
     "Model/Mesh/Process", Contextual2D},
    {"mesh.process.surfaceQuality", "actionMeshProcessSurfaceQuality", "Mesh 2D", "", "", "",
     "Model/Mesh/Process", Contextual2D},
    {"mesh.process.snow", "actionMeshProcessSnow", "Mesh 2D", "", "", "", "Model/Mesh/Process",
     Contextual2D},
    {"mesh.process.heat", "actionMeshProcessHeat", "Mesh 2D", "", "", "", "Model/Mesh/Process",
     Contextual2D},
    {"terrain.profile", "actionTerrainProfile", "Model", "", "Profile", "terrain", "Model", NoTags},

    // ── View / panels (dock toggle actions; objectNames assigned where
    //    the View → Panels menu is built) ────────────────────────────────
    {"view.styleManager", "actionStyleManager", "View", "", "StyleManager", "home", "Tools",
     NoTags},
    {"view.layerStylingDock", "actionLayerStylingDock", "Panels", "Ctrl+Alt+8", "LayerStyling",
     "home", "View", NoTags},
    {"view.dock.timeSeries", "actionToggleDockTimeSeries", "Panels", "", "Profile", "",
     "View/Panels", NoTags},
    {"view.dock.profiles1D", "actionToggleDockProfiles1D", "Panels", "", "Profile", "",
     "View/Panels", NoTags},
    {"view.dock.profiles2D", "actionToggleDockProfiles2D", "Panels", "", "Profile2D", "",
     "View/Panels", NoTags},
    {"view.dock.terrainProfiles", "actionToggleDockTerrainProfiles", "Panels", "", "Profile", "",
     "View/Panels", NoTags},
    {"view.dock.toolbox", "actionToggleDockToolbox", "Panels", "", "Plugins", "analysis",
     "View/Panels", NoTags},
    {"view.dock.layers", "actionToggleDockLayers", "Panels", "Ctrl+Alt+1", "DockLayers", "",
     "View/Panels", NoTags},
    {"view.dock.objectBrowser", "actionToggleDockObjectBrowser", "Panels", "Ctrl+Alt+2",
     "DockObjectBrowser", "", "View/Panels", NoTags},
    {"view.dock.properties", "actionToggleDockProperties", "Panels", "Ctrl+Alt+3", "DockProperties",
     "", "View/Panels", NoTags},
    {"view.dock.sectionView", "actionToggleDockSectionView", "Panels", "Ctrl+Alt+9",
     "DockSectionView", "", "View/Panels", NoTags},
    {"view.dock.attributeTable", "actionToggleDockAttributeTable", "Panels", "Ctrl+Alt+4",
     "DockAttributeTable", "", "View/Panels", NoTags},
    {"view.dock.legend", "actionToggleDockLegend", "Panels", "Ctrl+Alt+5", "DockLegend", "",
     "View/Panels", NoTags},
    {"view.dock.simulationStatus", "actionToggleDockSimulationStatus", "Panels", "Ctrl+Alt+6",
     "DockSimulationStatus", "home", "View/Panels", NoTags},
    {"view.dock.messageLogs", "actionToggleDockMessageLogs", "Panels", "Ctrl+Alt+7",
     "DockMessageLogs", "", "View/Panels", NoTags},
    {"view.dock.features", "actionToggleDockFeatures", "Panels", "Ctrl+Alt+0", "Layers", "",
     "View/Panels", NoTags},
    {"view.showWelcome", "actionShowWelcome", "View", "", "Help", "home", "Help", NoTags},

    // ── Tools / window / help ───────────────────────────────────────────
    {"tools.plugins", "actionPlugins", "Tools", "", "Plugins", "", "Tools", NoTags},
    {"window.minimize", "actionWindowMinimize", "Window", "Ctrl+M", "WindowMinimize", "", "Window",
     NoTags},
    {"window.zoom", "actionWindowZoom", "Window", "", "WindowZoom", "", "Window", NoTags},
    {"app.commandPalette", "actionCommandPalette", "Application", "Ctrl+Shift+P", "CommandPalette",
     "", "View", NoTags},
    {"help.keyboardShortcuts", "actionKeyboardShortcuts", "Help", "", "KeyboardShortcuts", "",
     "Help", NoTags},
    {"app.help", "actionHelp", "Help", "std:HelpContents", "Help", "", "Help", NoTags},
    {"app.about", "actionAbout", "Help", "", "About", "", "Help", NoTags},
};

inline constexpr std::size_t kActionCatalogSize = std::size(kActionCatalog);

/// Known compact-toolbar tab ids ("" = menu-only entry). The View tab was
/// dissolved into Home → Window (process-aware editing plan §4); its id is
/// gone from the vocabulary so no command can be assigned to it again.
inline constexpr const char *kActionCatalogTabs[] = {
    "home", "model", "terrain", "mesh2d", "analysis", "results", "features",
};

/// Pseudo-tab of the Quick Access cluster on the tab strip, visible on every tab.
inline constexpr const char *kActionCatalogStrip = "strip";

} // namespace openswmmvis::ui

#endif // ACTIONCATALOG_H
