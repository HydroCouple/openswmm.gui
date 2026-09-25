# Generated W0 discovery index

Regenerate with `python3 scripts/audit_gui_baseline.py`. Read [the audit](README.md) for scope, evidence and acceptance gates.

**Source inventory only.** Reference sites include launchers and other consumers; comments are excluded. Runtime registration, reachability, owner/persistence, anonymous pages and plugin-created surfaces require the separate audit. No row is marked visually or functionally accepted by this script.

- 69 dialog declarations; 46 embedded surface declarations.
- 123 dialog implementation files; 24 need helper/dynamic-surface classification.
- 519 standard-dialog reference sites.
- 130 catalog actions; 130 with icon aliases.

| Surface | Kind | Declaration | Entry/consumer sites | Test references |
|---|---|---|---|---|
| AboutDialog | dialog | `include/ui/dialogs/aboutdialog.h:32` | 1 | 6 |
| AddBasemapDialog | dialog | `include/ui/dialogs/addbasemapdialog.h:70` | 6 | 10 |
| AnnotationStyleDialog | dialog | `include/ui/dialogs/annotationstyledialog.h:26` | 2 | 0 |
| AquiferEditorDialog | dialog | `include/ui/dialogs/aquifereditordialog.h:47` | 5 | 9 |
| AssignRainGagesDialog | dialog | `include/ui/dialogs/assignraingagesdialog.h:53` | 1 | 0 |
| CalibrationDataDialog | dialog | `include/ui/dialogs/calibrationdatadialog.h:36` | 0 | 0 |
| ChartPropertiesDialog | dialog | `include/ui/dialogs/chartpropertiesdialog.h:27` | 2 | 1 |
| ClimatologyDialog | dialog | `include/ui/dialogs/climatologydialog.h:40` | 5 | 10 |
| ColorRampEditorDialog | dialog | `include/ui/dialogs/colorrampeditordialog.h:41` | 1 | 1 |
| ComparisonPairsDialog | dialog | `include/ui/dialogs/comparisonpairsdialog.h:28` | 1 | 0 |
| ComparisonPlotDialog | dialog | `include/ui/dialogs/comparisonplotdialog.h:64` | 12 | 0 |
| CRSChangeDialog | dialog | `include/ui/dialogs/crschangedialog.h:28` | 4 | 12 |
| CRSSelectionDialog | dialog | `include/ui/dialogs/crsselectiondialog.h:37` | 6 | 0 |
| CurveEditorDialog | dialog | `include/ui/dialogs/curveeditordialog.h:70` | 18 | 10 |
| CustomReportDialog | dialog | `include/ui/dialogs/customreportdialog.h:31` | 0 | 0 |
| FeatureStyleEditorBase | embedded surface | `include/ui/dialogs/editors/featurestyleeditor.h:47` | 0 | 0 |
| LineFeatureStyleEditor | embedded surface | `include/ui/dialogs/editors/featurestyleeditor.h:109` | 0 | 0 |
| PointFeatureStyleEditor | embedded surface | `include/ui/dialogs/editors/featurestyleeditor.h:87` | 0 | 0 |
| PolygonFeatureStyleEditor | embedded surface | `include/ui/dialogs/editors/featurestyleeditor.h:136` | 0 | 0 |
| GisVectorSymbolEditor | embedded surface | `include/ui/dialogs/editors/gisvectorsymboleditor.h:33` | 0 | 0 |
| KindRendererPanel | embedded surface | `include/ui/dialogs/editors/kindrendererpanel.h:55` | 4 | 0 |
| MarkerShapeEditor | embedded surface | `include/ui/dialogs/editors/markershapeeditor.h:24` | 0 | 0 |
| SwmmElementSymbolEditor | embedded surface | `include/ui/dialogs/editors/swmmelementsymboleditor.h:29` | 0 | 0 |
| LineSymbolStyleEditor | embedded surface | `include/ui/dialogs/editors/symbolstyleeditors.h:59` | 0 | 0 |
| PointSymbolStyleEditor | embedded surface | `include/ui/dialogs/editors/symbolstyleeditors.h:38` | 0 | 0 |
| PolygonSymbolStyleEditor | embedded surface | `include/ui/dialogs/editors/symbolstyleeditors.h:78` | 0 | 0 |
| ContourBandSymbolStyleEditor | embedded surface | `include/ui/dialogs/editors/symbolstyleeditors2d.h:65` | 0 | 0 |
| HillshadeSymbolStyleEditor | embedded surface | `include/ui/dialogs/editors/symbolstyleeditors2d.h:52` | 0 | 0 |
| IsolineSymbolStyleEditor | embedded surface | `include/ui/dialogs/editors/symbolstyleeditors2d.h:79` | 0 | 0 |
| MeshEdgeSymbolStyleEditor | embedded surface | `include/ui/dialogs/editors/symbolstyleeditors2d.h:93` | 0 | 0 |
| MeshNodeSymbolStyleEditor | embedded surface | `include/ui/dialogs/editors/symbolstyleeditors2d.h:107` | 0 | 0 |
| RasterColorRampSymbolStyleEditor | embedded surface | `include/ui/dialogs/editors/symbolstyleeditors2d.h:38` | 0 | 0 |
| VelocityVectorSymbolStyleEditor | embedded surface | `include/ui/dialogs/editors/symbolstyleeditors2d.h:121` | 0 | 0 |
| GroundwaterExchangeDialog | dialog | `include/ui/dialogs/groundwaterexchangedialog.h:37` | 3 | 6 |
| HeatConfigDialog | dialog | `include/ui/dialogs/heatconfigdialog.h:60` | 2 | 14 |
| HydrographGroupEditor | dialog | `include/ui/dialogs/hydrographgroupeditor.h:71` | 9 | 0 |
| ImportFeatureLayerDialog | dialog | `include/ui/dialogs/import/importfeaturelayerdialog.h:42` | 1 | 0 |
| InfilAssignToSelectionDialog | dialog | `include/ui/dialogs/infilassigntoselectiondialog.h:62` | 1 | 0 |
| InitialQualityDialog | dialog | `include/ui/dialogs/initialqualitydialog.h:43` | 3 | 14 |
| InletEditorDialog | dialog | `include/ui/dialogs/inleteditordialog.h:66` | 11 | 0 |
| InletJunctionSetupDialog | dialog | `include/ui/dialogs/inletjunctionsetupdialog.h:45` | 3 | 3 |
| IRendererPanel | embedded surface | `include/ui/dialogs/irendererpanel.h:122` | 13 | 0 |
| IStyleEditorWidget | embedded surface | `include/ui/dialogs/istyleeditorwidget.h:43` | 22 | 0 |
| KindTreeSymbologyPanel | embedded surface | `include/ui/dialogs/kindtreesymbologypanel.h:47` | 1 | 0 |
| LabelExpressionDialog | dialog | `include/ui/dialogs/labelexpressiondialog.h:38` | 1 | 0 |
| LabelsTab | embedded surface | `include/ui/dialogs/labelstab.h:39` | 1 | 0 |
| LandUseEditorDialog | dialog | `include/ui/dialogs/landuseeditordialog.h:50` | 3 | 5 |
| LayerStyleDialog | dialog | `include/ui/dialogs/layerstyledialog.h:87` | 8 | 3 |
| LegendPropertiesDialog | dialog | `include/ui/dialogs/legendpropertiesdialog.h:35` | 1 | 0 |
| LicenseAgreementDialog | dialog | `include/ui/dialogs/licenseagreementdialog.h:28` | 4 | 1 |
| LidControlEditorDialog | dialog | `include/ui/dialogs/lidcontroleditordialog.h:44` | 3 | 0 |
| LinkCompoundEditDialog | dialog | `include/ui/dialogs/linkcompoundeditdialog.h:43` | 3 | 1 |
| Mesh2DGroundwaterDialog | dialog | `include/ui/dialogs/mesh2dgroundwaterdialog.h:56` | 4 | 8 |
| Mesh2DResultsExportDialog | dialog | `include/ui/dialogs/mesh2dresultsexportdialog.h:59` | 1 | 9 |
| MeshAttributeAssignDialog | dialog | `include/ui/dialogs/meshattributeassigndialog.h:82` | 4 | 0 |
| MeshGenerationDialog | dialog | `include/ui/dialogs/meshgenerationdialog.h:60` | 1 | 6 |
| MeshProfilePlotDialog | dialog | `include/ui/dialogs/meshprofileplotdialog.h:38` | 1 | 0 |
| NewFeatureLayerDialog | dialog | `include/ui/dialogs/newfeaturelayerdialog.h:42` | 1 | 0 |
| NewProjectDialog | dialog | `include/ui/dialogs/newprojectdialog.h:26` | 0 | 0 |
| NodeCompoundEditDialog | dialog | `include/ui/dialogs/nodecompoundeditdialog.h:36` | 3 | 0 |
| ObjectDefaultsPage | embedded surface | `include/ui/dialogs/objectdefaultspage.h:27` | 1 | 0 |
| PatternEditorDialog | dialog | `include/ui/dialogs/patterneditordialog.h:82` | 14 | 29 |
| PlotVariablePickerDialog | dialog | `include/ui/dialogs/plotvariablepickerdialog.h:36` | 1 | 14 |
| PluginsDialog | dialog | `include/ui/dialogs/pluginsdialog.h:20` | 1 | 1 |
| PollutantEditorDialog | dialog | `include/ui/dialogs/pollutanteditordialog.h:52` | 3 | 0 |
| PreferencesDialog | dialog | `include/ui/dialogs/preferencesdialog.h:41` | 2 | 4 |
| ProfileOptionsDialog | dialog | `include/ui/dialogs/profileoptionsdialog.h:29` | 3 | 0 |
| ProfilePathPickerDialog | dialog | `include/ui/dialogs/profilepathpickerdialog.h:28` | 3 | 0 |
| ProfilePlotDialog | dialog | `include/ui/dialogs/profileplotdialog.h:61` | 3 | 0 |
| RainfallVisualizationDialog | dialog | `include/ui/dialogs/rainfallvisualizationdialog.h:56` | 5 | 8 |
| RasterProfilePlotDialog | dialog | `include/ui/dialogs/rasterprofileplotdialog.h:41` | 1 | 0 |
| RasterSymbologyPanel | embedded surface | `include/ui/dialogs/rastersymbologypanel.h:61` | 1 | 8 |
| ReactionSystemEditorDialog | dialog | `include/ui/dialogs/reactionsystemeditordialog.h:51` | 1 | 9 |
| RulesEditorDialog | dialog | `include/ui/dialogs/ruleseditordialog.h:55` | 9 | 5 |
| RuleSymbologyTab | embedded surface | `include/ui/dialogs/rulesymbologytab.h:56` | 1 | 26 |
| ScatterPlotDialog | dialog | `include/ui/dialogs/scatterplotdialog.h:27` | 0 | 0 |
| DatesPage | embedded surface | `include/ui/dialogs/simoptions/datespage.h:42` | 2 | 0 |
| FilesPage | embedded surface | `include/ui/dialogs/simoptions/filespage.h:41` | 1 | 0 |
| HydraulicsPage | embedded surface | `include/ui/dialogs/simoptions/hydraulicspage.h:36` | 2 | 0 |
| MeshPage | embedded surface | `include/ui/dialogs/simoptions/meshpage.h:28` | 1 | 0 |
| ModelsPage | embedded surface | `include/ui/dialogs/simoptions/modelspage.h:37` | 3 | 0 |
| PerformancePage | embedded surface | `include/ui/dialogs/simoptions/performancepage.h:27` | 1 | 0 |
| QualityPage | embedded surface | `include/ui/dialogs/simoptions/qualitypage.h:37` | 2 | 0 |
| SimOptionsPage | embedded surface | `include/ui/dialogs/simoptions/simoptionspage.h:24` | 18 | 0 |
| SpatialPage | embedded surface | `include/ui/dialogs/simoptions/spatialpage.h:25` | 2 | 0 |
| TitleNotesPage | embedded surface | `include/ui/dialogs/simoptions/titlenotespage.h:25` | 1 | 0 |
| TwoDPage | embedded surface | `include/ui/dialogs/simoptions/twodpage.h:50` | 4 | 0 |
| SimulationOptionsDialog | dialog | `include/ui/dialogs/simulationoptionsdialog.h:95` | 54 | 72 |
| SnowpackEditorDialog | dialog | `include/ui/dialogs/snowpackeditordialog.h:37` | 3 | 0 |
| StatisticsDashboardDialog | dialog | `include/ui/dialogs/statisticsdashboarddialog.h:44` | 1 | 0 |
| StatusReportDialog | dialog | `include/ui/dialogs/statusreportdialog.h:46` | 1 | 0 |
| StreetEditorDialog | dialog | `include/ui/dialogs/streeteditordialog.h:73` | 4 | 0 |
| StreetSectionPreview | embedded surface | `include/ui/dialogs/streeteditordialog.h:60` | 0 | 0 |
| StyleManagerDialog | dialog | `include/ui/dialogs/stylemanagerdialog.h:41` | 1 | 0 |
| SubcatchCompoundEditDialog | dialog | `include/ui/dialogs/subcatchcompoundeditdialog.h:30` | 1 | 4 |
| SublayerSelectionDialog | dialog | `include/ui/dialogs/sublayerselectiondialog.h:28` | 1 | 0 |
| Swmm2DMeshStylePanel | embedded surface | `include/ui/dialogs/swmm2dmeshstylepanel.h:30` | 1 | 1 |
| Swmm2DResultsStylePanel | embedded surface | `include/ui/dialogs/swmm2dresultsstylepanel.h:30` | 1 | 0 |
| SymbologyTab | embedded surface | `include/ui/dialogs/symbologytab.h:33` | 3 | 0 |
| TabularResultsDialog | dialog | `include/ui/dialogs/tabularresultsdialog.h:30` | 0 | 0 |
| TimeseriesEditorDialog | dialog | `include/ui/dialogs/timeserieseditordialog.h:72` | 13 | 57 |
| TransectEditorDialog | dialog | `include/ui/dialogs/transecteditordialog.h:70` | 9 | 5 |
| UserFlagsDialog | dialog | `include/ui/dialogs/userflagsdialog.h:35` | 1 | 5 |
| UserFlagValuesDialog | dialog | `include/ui/dialogs/userflagvaluesdialog.h:28` | 1 | 3 |
| WaterAgeSourcesDialog | dialog | `include/ui/dialogs/wateragesourcesdialog.h:49` | 2 | 10 |
| WMSConnectionDialog | dialog | `include/ui/dialogs/wmsconnectiondialog.h:53` | 0 | 0 |
| WMTSConnectionDialog | dialog | `include/ui/dialogs/wmtsconnectiondialog.h:49` | 0 | 0 |
| CommandPalette | dialog | `include/ui/widgets/commandpalette.h:25` | 2 | 4 |
| GradientPreviewWidget | embedded surface | `src/ui/dialogs/colorrampeditordialog.cpp:37` | 0 | 0 |
| CategorizedPanel | embedded surface | `src/ui/dialogs/editors/categorizedrendererpanel.cpp:282` | 0 | 0 |
| SymbolDetailsDialog | dialog | `src/ui/dialogs/editors/categorizedrendererpanel.cpp:214` | 0 | 0 |
| GraduatedPanel | embedded surface | `src/ui/dialogs/editors/graduatedrendererpanel.cpp:28` | 0 | 0 |
| RuleBasedPanel | embedded surface | `src/ui/dialogs/editors/rulebasedrendererpanel.cpp:121` | 0 | 0 |
| SingleSymbolPanel | embedded surface | `src/ui/dialogs/editors/singlesymbolrendererpanel.cpp:45` | 0 | 0 |
| Gutter | embedded surface | `src/ui/dialogs/statusreportdialog.cpp:197` | 3 | 0 |
