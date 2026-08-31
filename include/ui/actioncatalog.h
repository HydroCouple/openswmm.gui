#ifndef ACTIONCATALOG_H
#define ACTIONCATALOG_H

/*!
 * \file actioncatalog.h
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
 *  - tab:             compact-toolbar tab id ("" = menu-only). Consumed
 *                     by the tabbed-toolbar phase; audited before then.
 *  - menuPath:        canonical menu location (documentation + menubar
 *                     audit; "/" separates submenu levels).
 *  - tags:            behavior bits, see ActionTag.
 */

#include <iterator>

namespace openswmmvis::ui {

enum ActionTag : unsigned {
    NoTags              = 0u,
    /// Enabled only while a project is open (applyProjectOpenToActions set).
    RequiresProject     = 1u << 0,
    /// Enabled only during an explicit edit session (applyEditSessionToActions set).
    RequiresEditSession = 1u << 1,
    /// Belongs to the contextual Mesh 2D surface.
    Contextual2D        = 1u << 2,
};

struct ActionCatalogEntry {
    const char *id;
    const char *objectName;
    const char *category;
    const char *defaultShortcut;
    const char *icon;
    const char *tab;
    const char *menuPath;
    unsigned    tags;
};

inline constexpr ActionCatalogEntry kActionCatalog[] = {
    // ── File ────────────────────────────────────────────────────────────
    {"file.new",              "actionNew",              "File", "std:New",         "New", "home", "File", NoTags},
    {"file.open",             "actionOpen",             "File", "std:Open",        "Open", "home", "File", NoTags},
    {"file.save",             "actionSave",             "File", "std:Save",        "Save", "home", "File", NoTags},
    {"file.saveAs",           "actionSaveAs",           "File", "std:SaveAs",      "SaveAs",     "", "File", NoTags},
    {"file.exportMap",        "actionExportMap",        "File", "",                "ExportMap",     "", "File", NoTags},
    {"file.print",            "actionPrint",            "File", "std:Print",       "Print",     "", "File", NoTags},
    {"file.clearRecent",      "actionClearRecentFiles", "File", "",                "ClearRecent",     "", "File/Open Recent", NoTags},
    // Literal Ctrl+, (not std:Preferences): the offscreen QPA's platform
    // theme resolves that StandardKey to nothing, and Ctrl+, is the
    // convention on every platform anyway (Cmd+, on macOS via Qt::CTRL).
    {"app.preferences",       "actionSettings",         "Application", "Ctrl+,", "Settings", "", "Tools", NoTags},
    // Literal Ctrl+Q for the same offscreen-theme reason as app.preferences.
    {"app.quit",              "actionExit",             "Application", "Ctrl+Q", "Exit",     "", "File", NoTags},

    // ── Edit / selection ────────────────────────────────────────────────
    {"edit.undo",             "actionUndo",             "Edit", "std:Undo",        "Undo", "home", "Edit", NoTags},
    {"edit.redo",             "actionRedo",             "Edit", "std:Redo",        "Redo", "home", "Edit", NoTags},
    {"edit.copy",             "actionCopy",             "Edit", "std:Copy",        "Copy", "home", "Edit", NoTags},
    {"find.search",           "actionSearch",           "Edit", "std:Find",        "Search", "home", "Edit", NoTags},
    {"edit.editExisting",     "actionEditExisting",     "Edit", "Ctrl+E",          "SelectEdit", "model", "Edit", NoTags},
    {"select.invert",         "actionInvertSelection",  "Edit", "",                "InvertSelection", "home", "Edit", NoTags},
    {"select.upstream",       "actionSelectUpstream",   "Edit", "",                "SelectUpstream", "home", "Edit", NoTags},
    {"select.downstream",     "actionSelectDownstream", "Edit", "",                "SelectDownstream", "home", "Edit", NoTags},

    // ── Map navigation / tools ──────────────────────────────────────────
    {"map.pan",               "actionPan",              "Map", "",                 "Move", "home", "View", NoTags},
    {"map.zoomIn",            "actionZoomIn",           "Map", "std:ZoomIn",       "ZoomIn", "home", "View", NoTags},
    {"map.zoomOut",           "actionZoomOut",          "Map", "std:ZoomOut",      "ZoomOut", "home", "View", NoTags},
    {"map.zoomExtent",        "actionZoomExtent",       "Map", "Ctrl+Shift+F",     "Extent", "home", "View", NoTags},
    {"map.zoomToSelection",   "actionZoomToSelection",  "Map", "Ctrl+Shift+J",     "ZoomToSelection", "home", "View", NoTags},
    {"map.select",            "actionSelect",           "Map", "",                 "Select", "home", "Edit", NoTags},
    {"map.selectByPolygon",   "actionSelectByPolygon",  "Map", "",                 "SelectByPolygon", "home", "Edit", NoTags},
    {"map.measure",           "actionMeasure",          "Map", "Ctrl+Shift+M",     "Ruler", "home", "View", NoTags},

    // ── Model authoring (enabled while a project is open) ───────────────
    {"model.addJunction",     "actionAddJunction",      "Model", "",  "Junction", "model", "Model/Add Node", RequiresProject},
    {"model.addVirtualJunction", "actionAddVirtualJunction", "Model", "",  "VirtualJunction", "model", "Model/Add Node", RequiresProject},
    {"model.addOutfall",      "actionAddOutfall",       "Model", "",  "Outfall", "model", "Model/Add Node", RequiresProject},
    {"model.addFlowDivider",  "actionAddFlowDivider",   "Model", "",  "Divider", "model", "Model/Add Node", RequiresProject},
    {"model.addStorage",      "actionAddStorage",       "Model", "",  "Storage", "model", "Model/Add Node", RequiresProject},
    {"model.addPipe",         "actionAddPipe",          "Model", "",  "Polyline", "model", "Model/Add Link", RequiresProject},
    {"model.addPump",         "actionAddPump",          "Model", "",  "Pump", "model", "Model/Add Link", RequiresProject},
    {"model.addOrifice",      "actionAddOrifice",       "Model", "",  "Orifice", "model", "Model/Add Link", RequiresProject},
    {"model.addWeir",         "actionAddWeir",          "Model", "",  "Weir", "model", "Model/Add Link", RequiresProject},
    {"model.addOutlet",       "actionAddOutlet",        "Model", "",  "Outlet", "model", "Model/Add Link", RequiresProject},
    {"model.addSubcatchment", "actionAddSubcatchment",  "Model", "",  "Subcatchment", "model", "Model", RequiresProject},
    {"model.addRainGage",     "actionRainGauge",        "Model", "",  "Rainfall", "model", "Model", RequiresProject},
    {"model.addText",         "actionAddText",          "Model", "",  "Text", "model", "Model", RequiresProject},
    {"model.importFeatureLayer", "actionImportFeatureLayer", "Model", "", "ImportGIS", "model", "Model", NoTags},
    {"model.assignRainGages", "actionAssignRainGages", "Model", "", "AssignRainGages", "model", "Model", RequiresProject},
    {"model.simulationOptions",  "actionOptions",       "Model", "",  "Options", "model", "Model", NoTags},
    {"model.userFlags",       "actionUserFlags",        "Model", "",  "UserFlags", "model", "Model", NoTags},
    {"model.editReactionSystem", "actionEditReactionSystem", "Model", "", "ReactionSystem", "model", "Model", NoTags},
    {"model.editHeatConfig",  "actionEditHeatConfig",   "Model", "",  "HeatConfig", "model", "Model", NoTags},
    {"model.generateMesh",    "actionGenerateMesh",     "Model", "",  "CreateMesh", "mesh2d", "Model", NoTags},

    // ── Climatology ─────────────────────────────────────────────────────
    {"climate.temperature",   "actionTemperature",      "Climate", "", "Thermometer", "model", "Model/Climate", NoTags},
    {"climate.evaporation",   "actionEvaporation",      "Climate", "", "Evaporation", "model", "Model/Climate", NoTags},
    {"climate.wind",          "actionWind",             "Climate", "", "Wind", "model", "Model/Climate", NoTags},
    {"climate.snow",          "actionSnow",             "Climate", "", "Snow", "model", "Model/Climate", NoTags},
    {"climate.solarRadiation","actionSolarRadiation",   "Climate", "", "Sun", "model", "Model/Climate", NoTags},

    // ── Data objects (programmatic Data menu; objectNames assigned at
    //    creation in initializeMenus) ─────────────────────────────────────
    {"data.newTimeSeries",    "actionNewTimeSeries",    "Data", "", "AddTimeSeries", "model", "Model/Data Objects", NoTags},
    {"data.newCurve",         "actionNewCurve",         "Data", "", "AddCurve", "model", "Model/Data Objects", NoTags},
    {"data.newPattern",       "actionNewPattern",       "Data", "", "AddPattern", "model", "Model/Data Objects", NoTags},
    {"data.newLidControl",    "actionNewLidControl",    "Data", "", "LidControl", "model", "Model/Data Objects", NoTags},
    {"data.newPollutant",     "actionNewPollutant",     "Data", "", "Pollutant", "model", "Model/Data Objects", NoTags},
    {"data.newLandUse",       "actionNewLandUse",       "Data", "", "LandUse",      "", "Model/Data Objects", NoTags},
    {"data.newAquifer",       "actionNewAquifer",       "Data", "", "Aquifer",      "", "Model/Data Objects", NoTags},
    {"data.newSnowpack",      "actionNewSnowpack",      "Data", "", "Snowpack",      "", "Model/Data Objects", NoTags},
    {"data.newControlRule",   "actionNewControlRule",   "Data", "", "AddControlRule", "model", "Model/Data Objects", NoTags},
    {"data.newTransect",      "actionNewTransect",      "Data", "", "AddTransect", "model", "Model/Data Objects", NoTags},
    {"data.newUnitHydrograph","actionNewUnitHydrograph","Data", "", "UnitHydrograph",      "", "Model/Data Objects", NoTags},
    {"data.newStreet",        "actionNewStreet",        "Data", "", "Street",      "", "Model/Data Objects", NoTags},
    {"data.newInlet",         "actionNewInlet",         "Data", "", "Inlet",      "", "Model/Data Objects", NoTags},

    // ── Layer / data import ─────────────────────────────────────────────
    {"import.swmmOutput",     "actionAddSWMMOutput",    "Import", "", "AddSWMMOutput", "home", "File/Import", NoTags},
    {"import.vector",         "actionAddVectorData",    "Import", "", "AddVector", "home", "File/Import", NoTags},
    {"import.raster",         "actionAddRasterData",    "Import", "", "AddRaster", "home", "File/Import", NoTags},
    {"import.wms",            "actionAddWMSData",       "Import", "", "AddWMS", "home", "File/Import", NoTags},
    {"import.delimited",      "actionAddDelimeteredData","Import", "", "AddDelimetered", "home", "File/Import", NoTags},
    {"import.basemap",        "actionAddBasemap",       "Import", "", "AddBasemap",     "", "File/Import", NoTags},
    {"import.mesh2d",         "actionAddMesh2D",        "Import", "", "AddMesh", "home", "File/Import", NoTags},

    // ── Simulation ──────────────────────────────────────────────────────
    {"sim.run",               "actionExecute",          "Simulation", "Ctrl+R", "Execute", "home", "Analysis", NoTags},
    {"sim.pause",             "actionPauseExecution",   "Simulation", "",       "Pause", "home", "Analysis", NoTags},
    {"sim.stop",              "actionCancelExecution",  "Simulation", "Ctrl+.", "CancelExecution", "home", "Analysis", NoTags},

    // ── Results playback ────────────────────────────────────────────────
    {"results.play",          "actionPlay",             "Playback", "", "Play", "results", "Results", NoTags},
    {"results.pause",         "actionPause",            "Playback", "", "Pause", "results", "Results", NoTags},
    {"results.stop",          "actionStop",             "Playback", "", "Stop", "results", "Results", NoTags},
    {"results.skipBack",      "actionSkipBack",         "Playback", "", "SkipBack", "results", "Results", NoTags},
    {"results.skipForward",   "actionSkipForward",      "Playback", "", "SkipForward", "results", "Results", NoTags},
    {"results.setStyle",      "actionSetStyle",         "Playback", "", "Style", "results", "Results", NoTags},
    {"view.showLegend",       "actionShowLegend",       "View",     "", "Legend", "results", "View", NoTags},

    // ── Analysis / reporting ────────────────────────────────────────────
    {"analysis.summarize",       "actionSummarizeResults",     "Analysis", "",             "Summarize", "analysis", "Analysis", NoTags},
    {"analysis.report",          "actionReport",               "Analysis", "",             "Report", "analysis", "Analysis", NoTags},
    {"analysis.tabularView",     "actionTabularView",          "Analysis", "Ctrl+Shift+A", "TableView", "analysis", "Analysis", NoTags},
    {"analysis.plotTimeSeries",  "actionPlotTimeSeries",       "Analysis", "Ctrl+T",       "Chart", "analysis", "Analysis", NoTags},
    {"analysis.plotProfile",     "actionPlotProfile",          "Analysis", "Ctrl+Shift+T", "Profile", "analysis", "Analysis", NoTags},
    {"analysis.flowBalanceDown", "actionFlowBalanceDownstream","Analysis", "",             "FlowBalanceDownstream", "analysis", "Analysis", NoTags},
    {"analysis.flowBalanceUp",   "actionFlowBalanceUpstream",  "Analysis", "",             "FlowBalanceUpstream", "analysis", "Analysis", NoTags},
    {"analysis.travelTimeDown",  "actionTravelTimeDownstream", "Analysis", "",             "TravelTimeDownstream", "analysis", "Analysis", NoTags},
    {"analysis.travelTimeUp",    "actionTravelTimeUpstream",   "Analysis", "",             "TravelTimeUpstream", "analysis", "Analysis", NoTags},
    {"analysis.massBalance",     "actionShowMassBalance",      "Analysis", "",             "Chartpie", "analysis", "Analysis", NoTags},

    // ── Mesh 2D / terrain ───────────────────────────────────────────────
    {"mesh.selectVertices",   "actionMeshSelectVertex", "Mesh 2D", "", "SelectTriNode", "mesh2d", "Model/Mesh", Contextual2D},
    {"mesh.selectEdges",      "actionMeshSelectEdge",   "Mesh 2D", "", "SelectTriEdge", "mesh2d", "Model/Mesh", Contextual2D},
    // Cell Data — assign per-cell parameters from a raster or a vector field.
    {"mesh.assignFromRaster", "actionMeshAssignFromRaster", "Mesh 2D", "", "MeshAssignRaster", "mesh2d", "Model/Mesh", Contextual2D},
    {"mesh.assignFromVector", "actionMeshAssignFromVector", "Mesh 2D", "", "MeshAssignVector", "mesh2d", "Model/Mesh", Contextual2D},
    // Groundwater (2D) — preview of the pending [2D_AQUIFER] editor.
    {"mesh.gw2dParams",       "actionMesh2DGWParams",   "Mesh 2D", "", "GW2DParams", "mesh2d", "Model/Mesh", Contextual2D},
    {"mesh.gw2dInitCond",     "actionMesh2DGWInitCond", "Mesh 2D", "", "GW2DInit",   "mesh2d", "Model/Mesh", Contextual2D},
    {"terrain.profile",       "actionTerrainProfile",   "Model",   "", "Profile",  "model", "Model", NoTags},

    // ── View / panels (dock toggle actions; objectNames assigned where
    //    the View → Panels menu is built) ────────────────────────────────
    {"view.styleManager",       "actionStyleManager",             "View",   "",           "StyleManager", "view", "Tools", NoTags},
    {"view.layerStylingDock",   "actionLayerStylingDock",         "Panels", "Ctrl+Alt+8", "LayerStyling", "view", "View", NoTags},
    {"view.dock.layers",        "actionToggleDockLayers",         "Panels", "Ctrl+Alt+1", "DockLayers", "view", "View/Panels", NoTags},
    {"view.dock.objectBrowser", "actionToggleDockObjectBrowser",  "Panels", "Ctrl+Alt+2", "DockObjectBrowser", "view", "View/Panels", NoTags},
    {"view.dock.properties",    "actionToggleDockProperties",     "Panels", "Ctrl+Alt+3", "DockProperties", "view", "View/Panels", NoTags},
    {"view.dock.sectionView",   "actionToggleDockSectionView",    "Panels", "Ctrl+Alt+9", "DockSectionView", "view", "View/Panels", NoTags},
    {"view.dock.attributeTable","actionToggleDockAttributeTable", "Panels", "Ctrl+Alt+4", "DockAttributeTable", "view", "View/Panels", NoTags},
    {"view.dock.legend",        "actionToggleDockLegend",         "Panels", "Ctrl+Alt+5", "DockLegend", "view", "View/Panels", NoTags},
    {"view.dock.simulationStatus","actionToggleDockSimulationStatus","Panels","Ctrl+Alt+6","DockSimulationStatus", "view", "View/Panels", NoTags},
    {"view.dock.messageLogs",   "actionToggleDockMessageLogs",    "Panels", "Ctrl+Alt+7", "DockMessageLogs", "view", "View/Panels", NoTags},
    {"view.showWelcome",        "actionShowWelcome",              "View",   "",           "Help", "view", "Help", NoTags},

    // ── Tools / window / help ───────────────────────────────────────────
    {"tools.plugins",         "actionPlugins",          "Tools",  "",       "Plugins", "", "Tools", NoTags},
    {"window.minimize",       "actionWindowMinimize",   "Window", "Ctrl+M", "WindowMinimize", "", "Window", NoTags},
    {"window.zoom",           "actionWindowZoom",       "Window", "",       "WindowZoom", "", "Window", NoTags},
    {"app.commandPalette",    "actionCommandPalette",   "Application", "Ctrl+Shift+P", "CommandPalette", "", "View", NoTags},
    {"help.keyboardShortcuts","actionKeyboardShortcuts","Help",   "",       "KeyboardShortcuts", "", "Help", NoTags},
    {"app.help",              "actionHelp",             "Help",   "std:HelpContents", "Help", "", "Help", NoTags},
    {"app.about",             "actionAbout",            "Help",   "",       "About", "", "Help", NoTags},
};

inline constexpr std::size_t kActionCatalogSize = std::size(kActionCatalog);

/// Known compact-toolbar tab ids ("" = menu-only entry).
inline constexpr const char *kActionCatalogTabs[] = {
    "home", "model", "mesh2d", "analysis", "results", "view",
};

}   // namespace openswmmvis::ui

#endif // ACTIONCATALOG_H
