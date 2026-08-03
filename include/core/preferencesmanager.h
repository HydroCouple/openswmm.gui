/*!
 * \file   preferencesmanager.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice V — Settings & Preferences infrastructure.
 *
 * Typed, signal-driven accessor for user preferences. Two-level scope:
 *   - App scope: persisted to QSettings, per-user / per-installation.
 *   - Project scope: overrides attached to the active project (planned
 *     for Phase 12's .oswp serializer; in-memory only today).
 *
 * Live binding: every setter emits `preferenceChanged(group, key)` so
 * the Select tool, the batched renderer's LOD, the project window's
 * default tool, etc. refresh without restart.
 */

#ifndef PREFERENCESMANAGER_H
#define PREFERENCESMANAGER_H

#include "render/colorramp.h"
#include "plot/numberformat.h"

#include <QBrush>
#include <QColor>
#include <QMap>
#include <QObject>
#include <QPen>
#include <QSettings>
#include <QString>

class PreferencesManager : public QObject
{
    Q_OBJECT

public:
    /*! Process-wide singleton. Constructed on first access; reads
     *  current values from QSettings at construction so app-scope
     *  keys are immediately available to early callers. */
    static PreferencesManager *instance();

    // ── Selection (MapToolSelect) ────────────────────────────────────────
    /*! Pixel-tolerance floor for click-picking. Effective tolerance at
     *  pick time is `max(clickTolerancePx, markerFloor + 4 px halo)`,
     *  so clicks inside the visible glyph always hit regardless of
     *  this value. Default 16. */
    [[nodiscard]] int  clickTolerancePx() const;
    void setClickTolerancePx(int pixels);

    /*! Drag threshold in pixels before a click turns into a rubber-band
     *  rectangle select. Default 8. */
    [[nodiscard]] int  dragThresholdPx() const;
    void setDragThresholdPx(int pixels);

    /*! When the user clicks in empty space (no hit-target), clear the
     *  current selection. Default true. */
    [[nodiscard]] bool clearSelectionOnMiss() const;
    void setClearSelectionOnMiss(bool on);

    /*! Selection highlight pen for features of a given vector class.
     *  \p className is one of "link", "node", "subcatchment", "gage"
     *  (case-insensitive); unknown classes return a default yellow pen.
     *
     *  Width semantics for the "link" class are **additive** over the
     *  base link pen: a width of 2.0 means the selected-link halo is
     *  2 px wider than the conduit/pump/etc. pen at the same scale.
     *
     *  Stored under SWMMVis/Preferences/Selection/Pen/<Class>. On first
     *  load, legacy SWMMVis/Preferences/Selection/Color/<Class> values
     *  are migrated into both a pen (with the saved colour) and a brush
     *  (with the saved colour, alpha 180 for polygons / 255 for glyphs)
     *  and the legacy key is removed. */
    [[nodiscard]] QPen   selectionPen(const QString &className) const;
    void setSelectionPen(const QString &className, const QPen &pen);

    /*! Selection fill brush for polygonal/glyph classes ("subcatchment",
     *  "node", "gage"). Querying for "link" returns Qt::NoBrush since
     *  links are stroked, not filled. Stored under
     *  SWMMVis/Preferences/Selection/Brush/<Class>. */
    [[nodiscard]] QBrush selectionBrush(const QString &className) const;
    void setSelectionBrush(const QString &className, const QBrush &brush);

    /*! Convenience wrapper around `selectionPen(className).color()`.
     *  Retained for callers that only need the colour. */
    [[nodiscard]] QColor selectionColor(const QString &className) const;

    /*! Clears any user-set pen + brush for \p className so the next
     *  query returns the compile-time defaults. Emits preferenceChanged
     *  for both keys. Used by Preferences dialog's "Reset to defaults". */
    void resetSelectionStyleToDefault(const QString &className);

    // ── Appearance ───────────────────────────────────────────────────────
    /*! Chrome theme mode. One of "System" (follow the OS appearance,
     *  default) / "Light" / "Dark". Consumed by ThemeManager. */
    [[nodiscard]] QString appearanceMode() const;
    void setAppearanceMode(const QString &mode);

    // ── Canvas / Default tool ────────────────────────────────────────────
    /*! Default tool active when a project opens. One of
     *  "Select" / "Pan" / "Zoom". Default "Select". */
    [[nodiscard]] QString defaultTool() const;
    void setDefaultTool(const QString &tool);

    /*! CRS mode when the loaded .inp has no embedded CRS.
     *  "LocalAuto" (default) — auto-generate a local CRS from [MAP] Units.
     *  "EPSG"               — use defaultCrsAuthority() / defaultCrsCode(). */
    [[nodiscard]] QString defaultCrsMode() const;
    void setDefaultCrsMode(const QString &mode);

    /*! Default CRS authority (e.g. "EPSG") + code (e.g. 4326) used
     *  when the loaded .inp has no CRS and mode is "EPSG". */
    [[nodiscard]] QString defaultCrsAuthority() const;
    [[nodiscard]] int     defaultCrsCode()      const;
    void setDefaultCrsAuthority(const QString &authority);
    void setDefaultCrsCode(int code);

    // ── GPU / QSG rendering ──────────────────────────────────────────────
    /*! Slice §QSG-4 (2026-05-27) — single switch for the GPU scene-graph
     *  rendering path. When enabled, every newly-added SWMMModelLayer
     *  has its qsgRenderKinds set to all four kinds (Nodes | Links |
     *  Catch | Gages), the CPU SWMMLayerItem skips every kind, and the
     *  QSG overlay draws everything. When disabled, the CPU painter
     *  path renders the network — left as a fallback for users who hit
     *  GPU-driver-specific issues. Default true. */
    [[nodiscard]] bool qsgRenderEnabled()  const;
    void setQsgRenderEnabled(bool enabled);

    /*! Mesh Tiled LOD plan P1.1 — GPU scene-graph rendering for 2D terrain
     *  mesh layers. When enabled, MapCanvas hands the topmost visible
     *  SWMM2DMeshLayer to SWMM2DMeshQSGRenderer and the CPU
     *  SWMM2DMeshGraphicsItem paint is skipped (qsgOwnsRendering gate).
     *  QPainter path remains the fallback. App-wide env kill-switch:
     *  OPENSWMM_QSG_MESH=0. Default true. */
    [[nodiscard]] bool qsgMeshRenderEnabled() const;
    void setQsgMeshRenderEnabled(bool enabled);

    /*! Whether a large raster opened without internal overviews gets an
     *  external `.ovr` pyramid built automatically in the background (once
     *  per file, never modifying the source). Off ⇒ big rasters stay on the
     *  slow full-resolution warp until the user builds pyramids manually.
     *  Default true. */
    [[nodiscard]] bool autoBuildRasterOverviews() const;
    void setAutoBuildRasterOverviews(bool enabled);

    // ── Snapping ─────────────────────────────────────────────────────────
    /*! Whether vertex snapping is active for all drawing tools. Default true. */
    [[nodiscard]] bool snapEnabled()     const;
    void setSnapEnabled(bool enabled);

    /*! Snap detection radius in screen pixels. Default 12. */
    [[nodiscard]] int  snapTolerancePx() const;
    void setSnapTolerancePx(int px);

    /*! If true, also snap to link polyline and subcatchment polygon vertices
     *  in addition to node/gage centres. Default true. */
    [[nodiscard]] bool snapToVertices()  const;
    void setSnapToVertices(bool enabled);

    // ── Rendering / Map LOD ──────────────────────────────────────────────
    /*! Minimum view-transform `m11()` required for label drawing in
     *  the batched renderer. Below this the label pass is skipped
     *  entirely. Default 0.5. */
    [[nodiscard]] qreal labelLodM11Min() const;
    void setLabelLodM11Min(qreal m11);

    /*! Full rendering pen for a link subtype — colour, width, cap, join,
     *  style and dash are all user-tunable via the Preferences dialog's
     *  Rendering page (a QPropertyModel-backed editor).
     *
     *  \p linkType is one of: "conduit", "pump", "orifice", "weir", "outlet"
     *  (case-insensitive). Unknown keys fall back to the conduit default.
     *
     *  Stored under SWMMVis/Preferences/Rendering/LinkPen/<Type>. On first
     *  load any legacy SWMMVis/Preferences/Rendering/LinkColor/<Type>
     *  values are migrated to a pen carrying the default width/cap for
     *  that link type, then the legacy key is removed. */
    [[nodiscard]] QPen   linkPen(const QString &linkType) const;
    void setLinkPen(const QString &linkType, const QPen &pen);

    /*! Convenience wrapper around `linkPen(linkType).color()`. Retained
     *  for call sites that only need the colour. */
    [[nodiscard]] QColor linkColor(const QString &linkType) const;

    /*! Clears any user-set pen for \p linkType so the next linkPen() call
     *  returns the compile-time default. Emits preferenceChanged. Used by
     *  the Preferences dialog's "Reset to defaults" button. */
    void resetLinkPenToDefault(const QString &linkType);

    /*! Outline pen for a node marker class — colour, width, cap, join,
     *  style and dash are all user-tunable via the Preferences dialog's
     *  Rendering page (a QPropertyModel-backed editor).
     *
     *  \p nodeType is one of: "junction", "outfall", "storage", "divider"
     *  (case-insensitive). Unknown keys fall back to the junction default.
     *
     *  Stored under SWMMVis/Preferences/Rendering/NodePen/<Type>. */
    [[nodiscard]] QPen   nodePen(const QString &nodeType) const;
    void setNodePen(const QString &nodeType, const QPen &pen);

    /*! Fill brush for a node marker class. Used as the glyph fill in the
     *  CPU + QSG paint paths. Stored under
     *  SWMMVis/Preferences/Rendering/NodeBrush/<Type>. */
    [[nodiscard]] QBrush nodeBrush(const QString &nodeType) const;
    void setNodeBrush(const QString &nodeType, const QBrush &brush);

    /*! Marker diameter in pixels for a node class. Default 8 (junction /
     *  divider), 12.5 (outfall), 12.0 (storage). Range 1–64 px. Stored
     *  under SWMMVis/Preferences/Rendering/NodeSize/<Type>. */
    [[nodiscard]] double nodeSize(const QString &nodeType) const;
    void setNodeSize(const QString &nodeType, double sizePx);

    /*! Clears any user-set pen + brush + size for \p nodeType so the
     *  next query returns the compile-time defaults. Emits
     *  preferenceChanged for each cleared key. */
    void resetNodeStyleToDefault(const QString &nodeType);

    // ── Rendering / Custom color ramps (Slice BB-α) ──────────────────────
    /*! User-authored colour ramps keyed by display name, stored under
     *  SWMMVis/Preferences/Rendering/CustomRamps as a JSON array. Each
     *  entry is the RasterColorRamp::toJson() payload plus a "name"
     *  field. Reads return the latest persisted map; writes update both
     *  the in-memory cache and QSettings. Emits preferenceChanged with
     *  group "Rendering", key "CustomRamps". */
    [[nodiscard]] QMap<QString, RasterColorRamp> customColorRamps() const;
    void saveCustomColorRamp(const QString &name, const RasterColorRamp &ramp);
    void removeCustomColorRamp(const QString &name);

    // ── Simulation ───────────────────────────────────────────────────────
    /*! Progress-tick interval (ms) for live UI updates while a
     *  simulation runs. 1 Hz by default — short enough to feel live,
     *  long enough to not starve the event loop on small models. */
    [[nodiscard]] int progressTickMs() const;
    void setProgressTickMs(int ms);

    /*! Default animation playback speed multiplier restored at startup.
     *  Valid values: 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 (mirrors the
     *  animation toolbar's Speed combo). Default 1.0. Per-session
     *  overrides live in `.oswp` (Slice BA Phase 8.5). */
    [[nodiscard]] double animationSpeed() const;
    void setAnimationSpeed(double speed);

    // ── Profile plot path discovery ──────────────────────────────────────
    /*! Maximum number of candidate simple paths the profile-plot tool will
     *  enumerate between two endpoints before truncating.  Default 100.
     *  Range 1–1000000 (the upper bound is a sanity floor — exhaustive
     *  enumeration is exponential in the worst case so very high values
     *  may freeze the UI on heavily-meshed networks; the router's
     *  wall-clock soft cap will still bail out before total hang). */
    [[nodiscard]] int  profileMaxPaths() const;
    void setProfileMaxPaths(int n);

    /*! Radius (in screen pixels, not scene units) of the start/end endpoint
     *  halo drawn on the map while a profile is being picked.  The halo is
     *  cosmetic — its size stays constant regardless of map zoom.  Default
     *  10 px, which sits slightly outside the default 8 px junction marker
     *  so the halo reads clearly as an indicator without occluding the
     *  node glyph. */
    [[nodiscard]] int  profileEndpointHaloRadiusPx() const;
    void setProfileEndpointHaloRadiusPx(int px);

    /*! Pen used to stroke the start-endpoint halo.  Width is also in screen
     *  pixels (the pen is cosmetic).  Default: solid green, width 3 px. */
    [[nodiscard]] QPen profileStartEndpointPen() const;
    void setProfileStartEndpointPen(const QPen &pen);

    /*! Pen used to stroke the end-endpoint halo.  Default: solid red,
     *  width 3 px. */
    [[nodiscard]] QPen profileEndEndpointPen() const;
    void setProfileEndEndpointPen(const QPen &pen);

    // ── Map Decorations / Scale Bar ──────────────────────────────────────
    [[nodiscard]] QColor  scaleBarPenColor()     const;
    void setScaleBarPenColor(const QColor &color);

    [[nodiscard]] int     scaleBarPenWidth()     const;  ///< Default 2; range 1–20
    void setScaleBarPenWidth(int width);

    [[nodiscard]] int     scaleBarPenStyle()     const;  ///< Qt::PenStyle as int; default Qt::SolidLine
    void setScaleBarPenStyle(int style);

    [[nodiscard]] QString scaleBarFontFamily()   const;  ///< Default "sans-serif"
    void setScaleBarFontFamily(const QString &family);

    [[nodiscard]] int     scaleBarFontSize()     const;  ///< Default 8; range 4–72
    void setScaleBarFontSize(int size);

    [[nodiscard]] int     scaleBarUnits()        const;  ///< ScaleBarSettings::Units as int; default 0 (Auto)
    void setScaleBarUnits(int units);

    [[nodiscard]] int     scaleBarPosition()     const;  ///< ScaleBarSettings::Position as int; default 0 (BottomLeft)
    void setScaleBarPosition(int position);

    [[nodiscard]] int     scaleBarMaxBarLength() const;  ///< Default 100; range 20–500
    void setScaleBarMaxBarLength(int length);

    [[nodiscard]] int  scaleBarLabelDecimals()   const;  ///< -1=auto, 0=whole, n=n decimals. Default -1.
    void setScaleBarLabelDecimals(int decimals);

    [[nodiscard]] bool scaleBarCompactNotation() const;  ///< Default false.
    void setScaleBarCompactNotation(bool compact);

    // ── Map Decorations / Measure Tool ───────────────────────────────────
    [[nodiscard]] QColor  measureLineColor()   const;  ///< Segment + vertex dot color. Default Qt::red
    void setMeasureLineColor(const QColor &color);

    [[nodiscard]] QString measureLabelFontFamily() const;  ///< Default "sans-serif"
    void setMeasureLabelFontFamily(const QString &family);

    [[nodiscard]] int     measureLabelFontSize()   const;  ///< Default 8; range 4–72
    void setMeasureLabelFontSize(int size);

    [[nodiscard]] int     measureLabelDecimals()   const;  ///< Decimal places in labels. Default 2; range 0–6
    void setMeasureLabelDecimals(int decimals);

    [[nodiscard]] QColor  measureFillColor()   const;  ///< Area polygon fill base color. Default #6495ED (cornflower blue)
    void setMeasureFillColor(const QColor &color);

    [[nodiscard]] int     measureFillOpacity() const;  ///< Area fill opacity 0–100 %. Default 30
    void setMeasureFillOpacity(int opacity);

    // ── Plots: default numeric precision ─────────────────────────────────
    /*! App-wide default for how chart plots render numbers, separately for
     *  the X and Y axes. A plot inherits these unless its own properties
     *  dialog overrides them. `*FormatMode` is `NumberFormatMode` as int
     *  (0=Decimals, 1=SignificantFigures); `*Precision` is the digit count
     *  (decimals 0–10, sig figs 1–10). Time/date axes are unaffected. */
    [[nodiscard]] int  plotXAxisFormatMode() const;  ///< Default 0 (Decimals)
    void setPlotXAxisFormatMode(int mode);
    [[nodiscard]] int  plotXAxisPrecision()  const;  ///< Default 0; range 0–10
    void setPlotXAxisPrecision(int count);

    [[nodiscard]] int  plotYAxisFormatMode() const;  ///< Default 0 (Decimals)
    void setPlotYAxisFormatMode(int mode);
    [[nodiscard]] int  plotYAxisPrecision()  const;  ///< Default 2; range 0–10
    void setPlotYAxisPrecision(int count);

    /*! Convenience: the global default X/Y format as a value type. */
    [[nodiscard]] openswmmvis::plot::NumberFormat plotXAxisFormat() const;
    [[nodiscard]] openswmmvis::plot::NumberFormat plotYAxisFormat() const;

    // ── Element naming prefixes ──────────────────────────────────────────
    /*! Name prefix used when auto-generating IDs for newly placed SWMM objects.
     *  \p kind is one of: "junction", "outfall", "storage", "divider",
     *  "conduit", "pump", "orifice", "weir", "outlet", "raingage", "subcatchment".
     *  Unknown kinds return the \p kind string itself as a safe fallback.
     *
     *  Defaults: junction→"J", outfall→"O", storage→"S", divider→"D",
     *            conduit→"C", pump→"Pu", orifice→"Or", weir→"W",
     *            outlet→"Ou", raingage→"RG", subcatchment→"Sub". */
    [[nodiscard]] QString elementNamePrefix(const QString &kind) const;
    void setElementNamePrefix(const QString &kind, const QString &prefix);

    // ── Terrain editing defaults ─────────────────────────────────────────
    /*! Default node invert offset (model vertical units) applied when terrain
     *  assistance is active. Negative = invert below ground. Default 0.0. */
    [[nodiscard]] double terrainDefaultNodeOffset() const;
    void setTerrainDefaultNodeOffset(double offset);

    /*! Default link endpoint invert offset used to estimate conduit slope.
     *  Default 0.0. */
    [[nodiscard]] double terrainDefaultLinkOffset() const;
    void setTerrainDefaultLinkOffset(double offset);

    // ── Application defaults applied on project creation ─────────────────
    /*! Whether the auto-length policy is enabled by default when a project
     *  opens. The Status-bar checkbox in SWMMVis still surfaces the
     *  per-project override. Default true. */
    [[nodiscard]] bool autoLengthEnabled() const;
    void setAutoLengthEnabled(bool enabled);

    /*! Engine version string activated by default at startup / on File→New.
     *  One of SWMM_VERSION (refactored engine, default) or
     *  LEGACY_SWMM_VERSION (SWMM5 legacy engine). */
    [[nodiscard]] QString defaultEngineMode() const;
    void setDefaultEngineMode(const QString &version);

    // ── Simulation defaults for new (blank) projects ─────────────────────
    /*! Bundled defaults written into the synthesized .inp on File→New and
     *  pre-populated in the New Project dialog. Persisted under
     *  SWMMVis/Preferences/SimulationDefaults so users can edit once and
     *  every subsequent new project inherits the choices. */
    struct SimulationDefaults
    {
        QString flowUnits           = QStringLiteral("CFS");
        QString infiltrationModel   = QStringLiteral("HORTON");
        QString flowRouting         = QStringLiteral("DYNWAVE");

        bool    ignoreRainfall      = true;
        bool    ignoreRdii          = true;
        bool    ignoreSnowmelt      = true;
        bool    ignoreGroundwater   = true;
        bool    ignoreQuality       = true;
        bool    ignoreRouting       = true;
        bool    module2DEnabled     = false;

        bool    allowPonding        = false;
        bool    skipSteadyState     = false;
        double  minSlopePct         = 0.0;     ///< MIN_SLOPE (%)

        QString sweepStart          = QStringLiteral("01/01");
        QString sweepEnd            = QStringLiteral("12/31");
        double  dryDays             = 0.0;

        // Time steps stored as seconds.
        int     reportStepSec       = 60;      ///< REPORT_STEP (1 minute)
        int     dryStepSec          = 5 * 60;  ///< DRY_STEP (runoff dry-weather step, 5 min)
        int     wetStepSec          = 60;      ///< WET_STEP (1 minute)
        int     ruleStepSec         = 0;       ///< RULE_STEP (0 = use routing step)
        double  routingStepSec      = 5.0;     ///< ROUTING_STEP (seconds, fractional ok)

        int     maxTrials           = 8;
        double  headTolerance       = 0.005;   ///< HEAD_TOLERANCE
        double  sysFlowTolPct       = 5.0;     ///< SYS_FLOW_TOL surface = percent
        double  latFlowTolPct       = 5.0;     ///< LAT_FLOW_TOL surface = percent

        // Dynamic-wave specific.
        QString inertialDamping     = QStringLiteral("PARTIAL"); ///< "Dampen"
        QString normalFlowLimited   = QStringLiteral("BOTH");    ///< Slope+Froude
        QString forceMainEquation   = QStringLiteral("H-W");
        QString surchargeMethod     = QStringLiteral("EXTRAN");
        bool    variableStepOn      = true;
        double  variableStepFactor  = 0.75;    ///< VARIABLE_STEP Courant fraction (0 disables)
        double  minRoutingStepSec   = 0.5;     ///< MINIMUM_STEP (CFL floor)
        double  lengtheningStepSec  = 0.0;     ///< LENGTHENING_STEP

        // New-engine only. Emitted only when defaultEngineMode is the
        // refactored engine; for legacy engine the keys are suppressed.
        QString nodeContinuity      = QStringLiteral("SEMI_IMPLICIT");
        bool    andersonAccel       = true;

        /*! THREADS option. 0 = let the engine auto-pick (default in INP). The
         *  GUI defaults the live value to QThread::idealThreadCount() so a
         *  fresh project starts maxed out, but the persisted preference is 0
         *  so users who never open the dialog still get the engine's auto. */
        int     threads             = 0;
    };

    [[nodiscard]] SimulationDefaults simulationDefaults() const;
    void setSimulationDefaults(const SimulationDefaults &d);

    // ── 2D model defaults for new projects ───────────────────────────────
    /*! Bundled 2D defaults: the [2D_OPTIONS] solver keys emitted into the
     *  synthesized .inp on File→New (when the 2D module default is on) and
     *  used as the missing-key fallbacks in Simulation Options' 2D tab,
     *  plus the mesh-generation seeds MeshGenerationDialog starts from.
     *  Distances are stored in SI (metres); consumers unit-scale exactly
     *  as the mesh dialog's seedDefaults always has. Persisted under
     *  SWMMVis/Preferences/TwoDDefaults. */
    struct TwoDDefaults
    {
        // [2D_OPTIONS] solver keys (defaults mirror the engine's).
        double  maxTimestepSec     = 10.0;    ///< MAX_TIMESTEP (s)
        double  theta              = 0.8;     ///< THETA
        double  cflNumber          = 0.7;     ///< CFL_NUMBER
        int     ltsTiers           = 4;       ///< LTS_TIERS
        double  hMove              = 0.003;   ///< H_MOVE (m)
        double  froudeMax          = 1.5;     ///< FROUDE_MAX
        double  dryDepth           = 0.001;   ///< DRY_DEPTH (m)
        double  limiterEpsilon     = 1e-6;    ///< LIMITER_EPSILON
        double  fluxDhEps          = 0.004;   ///< FLUX_DH_EPS (m)
        QString cellClosure        = QStringLiteral("FLAT");  ///< CELL_CLOSURE
        QString faceReconstruction = QStringLiteral("MEAN");  ///< FACE_RECONSTRUCTION
        double  vfrMinWetFrac      = 0.01;    ///< VFR_MIN_WET_FRAC
        double  couplingCd         = 0.65;    ///< COUPLING_CD
        double  couplingSync       = 0.0;     ///< COUPLING_SYNC (0 = every routing step)
        bool    couplingAreaAuto   = false;   ///< COUPLING_AREA AUTO vs DEFAULT
        QString rainfallMode       = QStringLiteral("NATURAL_NEIGHBOUR"); ///< RAINFALL_MODE
        bool    report2D           = false;   ///< REPORT_2D

        // Mesh-generation seeds (SI canonical; the dialog unit-scales).
        double  meshMinAngleDeg      = 33.0;
        double  meshMaxArea          = 0.0;   ///< 0 = unconstrained
        int     meshMaxSteiner       = -1;    ///< -1 = unlimited
        double  meshIdwPower         = 2.0;
        double  meshSimplifyEpsM     = 0.1;
        double  meshSnapEpsM         = 0.01;
        double  meshNodeFlattenRadM  = 5.0;
        bool    meshMinNodeSepOn     = true;
        double  meshMinNodeSepM      = 2.0;
        bool    meshThinningOn       = true;
        double  meshThinningTol      = 0.6;   ///< normal-dot threshold
        int     meshThinningPasses   = 3;
        double  meshBoundaryBufferM  = 0.0;   ///< 0 = (auto)
        bool    meshMaxBoundaryEdgeOn = false;
        double  meshMaxBoundaryEdgeM = 20.0;
        double  meshManningsN        = 0.035;
        bool    meshOutputExternal   = true;
    };

    [[nodiscard]] TwoDDefaults twoDDefaults() const;
    void setTwoDDefaults(const TwoDDefaults &d);

signals:
    /*! Emitted after any successful set*. `group` is one of
     *  "Selection" / "Canvas" / "Rendering" / "Simulation" /
     *  "Decorations" / "Output"; `key` identifies the specific setting. */
    void preferenceChanged(const QString &group, const QString &key);

private:
    explicit PreferencesManager(QObject *parent = nullptr);

    QSettings m_settings;
};

#endif // PREFERENCESMANAGER_H
