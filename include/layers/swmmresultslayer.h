/*!
 * \file   swmmresultslayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Layer that colour-maps SWMM simulation output over the network
 *         geometry for interactive time-stepped playback.
 */

#ifndef SWMMRESULTSLAYER_H
#define SWMMRESULTSLAYER_H

#include "layers/openswmmvislayer.h"
#include "layers/gisrasterlayer.h"   // RasterColorRamp
#include "layers/swmmmodellayer.h"

#include <openswmm/engine/openswmm_output.h>

#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QPen>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <memory>

class OpenSWMMVisWorkspace;
class SpatialReferenceSystem;
class QGraphicsItem;   // Slice §Y.2 — m_itemByFeature stores QGraphicsItem*
class QGraphicsSimpleTextItem;   // L-1 — m_labelByFeature stores label items

// Slice U-0 — granular per-Category sublayer mix. The legacy four
// sublayers (NodeMarker / ConduitLine / ConduitArrow / SubcatchmentFill)
// are replaced by one FeatureSublayer instance per SWMMModelLayer::Category
// (11 in total). Each instance owns its own archetype-appropriate
// style bag so every visual object type can be styled independently.
#include "render/iattributeprovider.h"   // Slice DM.1
#include "render/isublayerhost.h"
#include "render/sublayers/feature/featuresublayer.h"

namespace OpenSWMM::Render {
class IFeatureRenderer;
class RuleList;   // Slice B.5 — see ruleList() override below.
enum class ClassEditKind;   // Gap B1 — legend facade (full def in ifeaturerenderer.h)
}

/*!
 * \enum SWMMResultVariable
 * \brief Selectable result output variables for colour-mapped display.
 */
enum class SWMMResultVariable
{
    // Node results
    NodeDepth           = 0,
    NodeHead            = 1,
    NodeVolume          = 2,
    NodeInflow          = 3,
    NodeOverflow        = 4,
    NodeLateralInflow   = 5,
    // Link results
    LinkFlow            = 10,
    LinkDepth           = 11,
    LinkVelocity        = 12,
    LinkCapacity        = 13,
    // Subcatchment results
    SubcatchRunoff      = 20,
    SubcatchInfiltration = 21,
    SubcatchEvaporation = 22,
    SubcatchSnowDepth   = 23,
};

/*!
 * \class SWMMResultsLayer
 * \brief Overlays time-stepped simulation results on the SWMM network geometry.
 * \details Reads output from an OpenSWMMCore binary results file (.out), maps
 *          a selected variable at the current simulation time step to a colour
 *          ramp, and paints the network elements with those colours.
 *
 *          Supports:
 *          - Animation: step forward/backward through the time series.
 *          - Any SWMMResultVariable (node depth/head, link flow/velocity, etc.).
 *          - A configurable RasterColorRamp for consistent colour mapping.
 *          - Legend rendering.
 *
 *          The layer delegates geometry loading to an associated SWMMModelLayer
 *          and only manages the colour-mapping / time-stepped rendering.
 */
class SWMMResultsLayer : public OpenSWMMVisLayer,
                         public OpenSWMM::Render::ISublayerHost,
                         public OpenSWMM::Render::IAttributeProvider  // Slice DM.1
{
    Q_OBJECT
    Q_INTERFACES(OpenSWMM::Render::IAttributeProvider)  // Slice DM.1

    Q_PROPERTY(QString   resultsFilePath  READ resultsFilePath   NOTIFY resultsFilePathChanged)
    Q_PROPERTY(QString   scenarioName     READ scenarioName      WRITE setScenarioName
               NOTIFY scenarioNameChanged)
    Q_PROPERTY(QColor    profileLineColor READ profileLineColor  WRITE setProfileLineColor
               NOTIFY profileLineColorChanged)
    Q_PROPERTY(QPen      profileHglLinePen      READ profileHglLinePen
               WRITE setProfileHglLinePen        NOTIFY profileStyleChanged)
    Q_PROPERTY(QBrush    profileHglFillBrush    READ profileHglFillBrush
               WRITE setProfileHglFillBrush      NOTIFY profileStyleChanged)
    Q_PROPERTY(QPen      profileEglLinePen      READ profileEglLinePen
               WRITE setProfileEglLinePen        NOTIFY profileStyleChanged)
    // EGL renders as a line only (no physically-meaningful fill above HGL).
    Q_PROPERTY(QPen      profileMaxHglLinePen   READ profileMaxHglLinePen
               WRITE setProfileMaxHglLinePen     NOTIFY profileStyleChanged)
    Q_PROPERTY(QBrush    profileMaxHglFillBrush READ profileMaxHglFillBrush
               WRITE setProfileMaxHglFillBrush   NOTIFY profileStyleChanged)
    Q_PROPERTY(QPen      profileMaxEglLinePen   READ profileMaxEglLinePen
               WRITE setProfileMaxEglLinePen     NOTIFY profileStyleChanged)
    Q_PROPERTY(int       currentTimeStep  READ currentTimeStep   WRITE setCurrentTimeStep
               NOTIFY currentTimeStepChanged)
    Q_PROPERTY(QDateTime currentDateTime  READ currentDateTime   NOTIFY currentDateTimeChanged)
    Q_PROPERTY(int       totalTimeSteps   READ totalTimeSteps    NOTIFY totalTimeStepsChanged)
    Q_PROPERTY(SWMMResultVariable variable READ variable         WRITE setVariable
               NOTIFY variableChanged)
    Q_PROPERTY(bool      showLegend       READ showLegend        WRITE setShowLegend
               NOTIFY showLegendChanged)

public:

    explicit SWMMResultsLayer(const QString &resultsFilePath,
                              class SWMMModelLayer *modelLayer,
                              OpenSWMMVisWorkspace *parent = nullptr);

    ~SWMMResultsLayer() override;

    /*! The SWMM model layer that supplies network topology + attributes
     *  for this run.  Categorized rendering on results layers reads
     *  categorical strings ("tag", "Link type", …) through here. */
    [[nodiscard]] class SWMMModelLayer *modelLayer() const { return m_modelLayer; }

    // ----- Results file ---------------------------------------------------

    [[nodiscard]] QString resultsFilePath() const;

    /*!
     * \brief Path of the `.rpt` summary report written by the run that
     *        produced this layer's `.out` file (empty when unknown).
     * \details Set by SWMMVis when a simulation finishes; persisted in the
     *          `.oswp` sidecar (keyed by the result path) so the Report
     *          Viewer can list every run's report after a project reload.
     */
    [[nodiscard]] QString reportFilePath() const;
    void setReportFilePath(const QString &path);

    /*!
     * \brief User-visible name for this run/scenario (editable, persisted in .oswp).
     * \details Defaults to "Run — HH:MM:SS" at construction time.
     */
    [[nodiscard]] QString scenarioName() const;
    void setScenarioName(const QString &name);

    /*!
     * \brief Per-layer line/fill color used by Slice BC's profile plot.
     * \details Auto-assigned at construction from the categorical palette
     *          (cycled by an internal layer counter), but user-editable via
     *          the Sources panel.  Persisted in .oswp once the result-layer
     *          schema upgrades to objects (currently still a path-string array).
     */
    [[nodiscard]] QColor profileLineColor() const;
    void setProfileLineColor(const QColor &color);

    // -------------------------------------------------------------------
    // Per-source styling for Slice BC's profile plot.
    //
    // Each result layer (think "model run" or "scenario") carries its own
    // pen+brush for the four overlay layers — Current HGL, Current EGL,
    // Max HGL, Max EGL — so two layers plotted side by side can be styled
    // independently (e.g. baseline solid blue vs. proposed dashed red).
    //
    // Defaults are derived from profileLineColor() at construction.
    // setProfileLineColor() does NOT re-derive them: once the user
    // customises a pen, it stays put across colour changes.  Each setter
    // emits the matching changed() signal; the dialog uses that to
    // refresh its preview swatch.
    // -------------------------------------------------------------------
    [[nodiscard]] QPen   profileHglLinePen()      const;
    [[nodiscard]] QBrush profileHglFillBrush()    const;
    [[nodiscard]] QPen   profileEglLinePen()      const;
    [[nodiscard]] QPen   profileMaxHglLinePen()   const;
    [[nodiscard]] QBrush profileMaxHglFillBrush() const;
    [[nodiscard]] QPen   profileMaxEglLinePen()   const;

    void setProfileHglLinePen     (const QPen &pen);
    void setProfileHglFillBrush   (const QBrush &brush);
    void setProfileEglLinePen     (const QPen &pen);
    void setProfileMaxHglLinePen  (const QPen &pen);
    void setProfileMaxHglFillBrush(const QBrush &brush);
    void setProfileMaxEglLinePen  (const QPen &pen);

    /*!
     * \brief Writes this layer's per-source profile pens & brushes to the
     *        current QSettings position.  Called from the dialogs' source
     *        persistence loops while QSettings is already positioned at
     *        the matching array index.
     *
     *        Keys written (prefix-free, relative to the current group/array index):
     *          profileHglLinePen, profileHglFillBrush,
     *          profileEglLinePen,
     *          profileMaxHglLinePen, profileMaxHglFillBrush,
     *          profileMaxEglLinePen
     *        Values are QPen / QBrush variants — Qt serialises them via
     *        QDataStream so colour, width, style, and dash pattern all round-trip.
     */
    void writeProfileStyle(class QSettings &settings) const;

    /*!
     * \brief Companion of writeProfileStyle.  Reads any of the six keys
     *        present at the current QSettings position and pushes them
     *        through the per-setter, so missing keys leave the layer's
     *        defaults untouched.
     */
    void readProfileStyle(class QSettings &settings);

    /*!
     * \brief Opens the binary results file.
     * \returns true if the file was opened and indexed successfully.
     */
    bool openResults(QList<QString> &warnings, QList<QString> &errors);

    void closeResults();

    // ----- Per-output node summary statistics (Slice QA.3 + QA-01) -------
    //
    // These four accessors return the same statistics the editing-engine
    // exposes via `swmm_node_get_stat_*`, but sourced from THIS output
    // file rather than the editing engine's ambient state. The node
    // attribute panel (Slice QA.2) dispatches to one of these when the
    // user picks a non-null "Stats source" from the combo, so multiple
    // loaded SWMMResultsLayers can be compared in the same panel.
    //
    // Backed by engine API QA-01 (`swmm_output_get_node_stat_*` in
    // openswmm_output.h), which aggregates the four flooding stats
    // on-demand from the .out file's per-period node results. The
    // aggregation runs at REPORT-step resolution — see the QA-01
    // header doc for the precision caveat when the routing step is
    // much finer than the report step.
    //
    // \p nodeName is the SWMM model-side identifier (e.g. "J1"); the
    // implementation resolves it to the output-side index via the
    // already-built nodeOutputIndex() map. Returns 0.0 for unknown
    // names or when the underlying file is closed.
    [[nodiscard]] double nodeStatMaxDepth(const QString &nodeName) const;
    [[nodiscard]] double nodeStatMaxOverflow(const QString &nodeName) const;
    [[nodiscard]] double nodeStatVolFlooded(const QString &nodeName) const;
    [[nodiscard]] double nodeStatTimeFlooded(const QString &nodeName) const;

    // ----- Animation ------------------------------------------------------

    [[nodiscard]] int       currentTimeStep()  const;
    [[nodiscard]] QDateTime currentDateTime()  const;
    [[nodiscard]] int       totalTimeSteps()   const;
    [[nodiscard]] QDateTime startDateTime()    const;
    [[nodiscard]] QDateTime endDateTime()      const;
    [[nodiscard]] int       reportStepSeconds() const;

    /*!
     * \brief Seeks to the given 0-based time step.
     */
    void setCurrentTimeStep(int step);

    /*!
     * \brief Returns the period index closest to \p dt on this layer's time grid.
     * \details Used by AnimationController to snap secondary layers to the
     *          primary layer's current time.  Result is clamped to [0, totalTimeSteps-1].
     */
    [[nodiscard]] int periodIndexForDateTime(const QDateTime &dt) const;

    /*!
     * \brief Causal peer of \ref periodIndexForDateTime: the latest period at
     *        or before \p dt (floor instead of nearest), clamped to
     *        [0, totalTimeSteps-1]. Used by AnimationController so a secondary
     *        output never displays a frame ahead of the animation cursor.
     */
    [[nodiscard]] int periodIndexForDateTimeAsOf(const QDateTime &dt) const;

    /*!
     * \brief Advances to the next time step (wraps at end if \p loop is true).
     */
    void stepForward(bool loop = false);

    /*!
     * \brief Goes back one time step (wraps at start if \p loop is true).
     */
    void stepBackward(bool loop = false);

    // ----- Variable & colour mapping --------------------------------------

    [[nodiscard]] SWMMResultVariable variable() const;
    void setVariable(SWMMResultVariable var);

    [[nodiscard]] RasterColorRamp colorRamp() const;
    void setColorRamp(const RasterColorRamp &ramp);

    /*!
     * \brief Automatically sets the colour ramp range from the data min/max
     *        across all time steps for the current variable.
     */
    void autoStretchColorRamp();

    // ----- Renderer (Slice BI Phase 8.13.6.3) -----------------------------
    // The renderer is the §J.2 seam every future paint path will go through.
    // In sub-phase 8.13.6.3 this is API plumbing only — the existing paint
    // loop still reads m_colorRamp / m_variable directly. Sub-phase 8.13.6.4
    // will swap the paint loop to consult m_renderer instead, after which
    // m_colorRamp / m_variable become private implementation details of the
    // default GraduatedRenderer.

    /*!
     * \brief The IFeatureRenderer that will drive this layer's paint pass.
     * \details Constructed eagerly as a default GraduatedRenderer so callers
     *          never have to null-check.  Owned by the layer; do not delete.
     *          Returned non-const so the caller may classify / configure the
     *          renderer in place (mutations the layer should know about should
     *          go via setRenderer() to trigger \ref rendererChanged()).
     */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *renderer() const;

    /*!
     * \brief Replaces the current renderer.
     * \details The layer takes ownership.  A null pointer is rejected (the
     *          method silently no-ops) so renderer() never returns nullptr.
     *          Emits \ref rendererChanged() when the renderer pointer actually
     *          changes.
     */
    void setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r);

    // ----- Per-kind renderers (Slice OUT.1) -------------------------------
    // Mirrors SWMMModelLayer's per-kind renderer slots so the user can
    // style Junctions vs Outfalls (etc.) independently for the same
    // NodeDepth/LinkFlow variable. The layer-level renderer() above stays
    // as the fallback when no kind-renderer is installed.

    /*!
     * \brief Returns the per-kind renderer for category \p c, or nullptr
     *        when none has been installed yet (callers should fall back
     *        to the layer-level renderer()).
     *
     *        The kind ordinals match SWMMModelLayer::Category (11
     *        entries: 4 node kinds + 5 link kinds + Subcatchments +
     *        RainGages — though RainGages doesn't carry results today).
     */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *kindRenderer(
        SWMMModelLayer::Category c) const;

    /*!
     * \brief Installs (or replaces) the per-kind renderer for category
     *        \p c. The layer takes ownership. A null pointer clears the
     *        slot (paint reverts to the layer-level renderer). Emits
     *        \ref rendererChanged() when the slot pointer changes.
     */
    void setKindRenderer(SWMMModelLayer::Category c,
                         std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r);

    /*!
     * \brief Restores the per-kind slot for \p c to its compile-time
     *        default (GraduatedRenderer keyed to the scope's primary
     *        result variable, Viridis ramp, EqualInterval 5-bin).
     */
    void resetKindRendererToDefaults(SWMMModelLayer::Category c);

    /*!
     * \brief True when the installed kind renderer for \p c is semantically
     *        identical to the compile-time default — ignoring derived state
     *        (classification breaks, sampled ramp range). Gap A2.1 installs
     *        defaults eagerly on openResults(); the project serializer uses
     *        this to elide untouched slots so .oswp files stay minimal.
     */
    [[nodiscard]] bool kindRendererIsDefault(SWMMModelLayer::Category c) const;

    /*!
     * \brief Re-clones the live kind renderer for \p c into the Rule
     *        mirror (no-op when the rule list hasn't been built). Dialog
     *        editors read the mirror; call this before mounting one so it
     *        reflects in-place mutations (variable retargeting,
     *        re-classification, per-frame rebin) the setKindRenderer sync
     *        hook cannot observe.
     */
    void refreshRuleMirror(SWMMModelLayer::Category c);

    // ----- Gap B1 — legend-as-editor facade (mirrors SWMMModelLayer) -----
    //
    // The legend views read rows through legendSymbolItems() and route
    // per-class edits through the facade below. Class keys are
    // kind-qualified ("<kindKey>\x1F<innerKey>") so edits land on the
    // right kind renderer even when two kinds share inner keys.

    [[nodiscard]] QList<OpenSWMM::Render::LegendSymbolItem> legendSymbolItems();
    [[nodiscard]] bool   supportsClassEdit(OpenSWMM::Render::ClassEditKind kind) const;
    [[nodiscard]] QColor colorForClass(const QString &classKey) const;
    void                 setColorForClass(const QString &classKey, const QColor &color);
    [[nodiscard]] qreal  sizeForClass(const QString &classKey) const;

    // ----- Rule Model (Slice B.5, Phase B) --------------------------------
    //
    // Mirrors SWMMModelLayer::ruleList() — 11 kindRenderer slots → 11
    // Rules, lazy-built on first access. Each Rule owns a clone of the
    // matching kindRenderer; Rule-side swaps propagate back via
    // setKindRenderer. Same one-way sync caveat as B.4 — external
    // setKindRenderer calls don't refresh the in-memory RuleList.
    [[nodiscard]] OpenSWMM::Render::RuleList *ruleList() override;
    [[nodiscard]] const OpenSWMM::Render::RuleList *ruleList() const override;

    // ----- IAttributeProvider (Slice DM.1) --------------------------------
    //
    // Returns the engine output codes available for the given category.
    // All entries are isDynamic=true (per-frame values). Drives the
    // attribute combo in the Graduated / Categorized renderer panels —
    // see RENDERING_DIALOG_DEMO_PLAN.md §2.
    [[nodiscard]] QVector<OpenSWMM::Render::AttributeField>
        availableAttributes(OpenSWMMVis::SwmmCategory cat) const override;

    // ----- Legend ---------------------------------------------------------

    [[nodiscard]] bool showLegend() const;
    void setShowLegend(bool show);

    // ----- Bulk output access (Slice BC) ----------------------------------

    /*!
     * \brief Raw `SWMM_Output` handle to the open `.out` file, or nullptr
     *        when results aren't open yet.  Exposed so Slice BC's profile
     *        plot can call `swmm_output_get_node_series` /
     *        `swmm_output_get_link_series` for bulk time-series fetches
     *        without a per-period round-trip.
     */
    [[nodiscard]] SWMM_Output outputHandle() const;

    /*!
     * \brief Output-file index for the named node, or -1 if not found.
     *        The output file uses its own indexing scheme (a name-keyed
     *        order) that may differ from the engine's node index, so
     *        callers must resolve via name.
     */
    [[nodiscard]] int nodeOutputIndex(const QString &name) const;

    /*!
     * \brief Output-file index for the named link, or -1.
     */
    [[nodiscard]] int linkOutputIndex(const QString &name) const;

    /*!
     * \brief Output-file index for the named subcatchment, or -1.
     *        Mirrors \ref nodeOutputIndex / \ref linkOutputIndex; added in Slice BL
     *        so the Comparison Plot Dialog can read subcatchment series.
     */
    [[nodiscard]] int subcatchOutputIndex(const QString &name) const;

    /*!
     * \brief Engine-reported flow units (0=CFS, 1=GPM, 2=MGD, 3=CMS, 4=LPS,
     *        5=MLD). -1 when results aren't open. Used by the Comparison
     *        Plot Dialog to label axes in US vs SI.
     */
    [[nodiscard]] int flowUnits() const;

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    void populateScene(QGraphicsScene *scene,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    void depopulateScene(QGraphicsScene *scene) override;

    /*! L-1 — a label-config change (enable / font / placement) only affects
     *  the label items, but refreshScene early-returns when the scene is
     *  Clean. Escalate to Values so the next refresh runs restyleScene →
     *  refreshLabels and the change is reflected without an animation tick. */
    void setLabelConfig(const OpenSWMM::Render::LabelConfig &cfg) override;

    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    // ----- ISublayerHost interface (Slice U-0) ------------------------------
    //
    // Returns the granular per-Category sublayer mix (paint order = list
    // order, bottom-up). Order matches SWMMModelLayer::Category but groups
    // by archetype so polygon fills paint first, then lines, then point
    // markers, then rain gages on top:
    //
    //   [0]  Subcatchments        (FillKind,    polygon archetype, dynamic)
    //   [1]  Conduits             (LineKind,    line archetype,    dynamic)
    //   [2]  Pumps                (LineKind,    line archetype,    dynamic)
    //   [3]  Orifices             (LineKind,    line archetype,    dynamic)
    //   [4]  Weirs                (LineKind,    line archetype,    dynamic)
    //   [5]  Outlets              (LineKind,    line archetype,    dynamic)
    //   [6]  Junctions            (MarkerKind,  point archetype,   dynamic)
    //   [7]  Outfalls             (MarkerKind,  point archetype,   dynamic)
    //   [8]  Storage              (MarkerKind,  point archetype,   dynamic)
    //   [9]  Dividers             (MarkerKind,  point archetype,   dynamic)
    //   [10] RainGages            (MarkerKind,  point archetype,   static)
    //
    // The sublayer instances are owned by this layer via QObject parent-
    // child. dispatchAnimationTick() inherits the default base-class
    // implementation, which invalidates only the dynamic sublayers per
    // Decision 3 (see ISublayerHost).
    [[nodiscard]] QList<OpenSWMM::Render::ISublayer *> sublayers() const override;

    /*! Reorder sublayers in paint order (bottom-up).  Emits
     *  repaintRequested() on success.  Returns false on out-of-range indices. */
    bool moveSublayer(int from, int to) override;

    /*! Typed convenience accessor by Category. Returns nullptr if the
     *  category has no sublayer (out-of-range Category). */
    [[nodiscard]] OpenSWMM::Render::FeatureSublayer *
        featureSublayer(SWMMModelLayer::Category c) const;

    /*!
     * \brief Phase 8 (2026-05-25) — ramp-aware legend rows.
     *
     *        Replaces the generic ISublayer::legendSymbolItems()
     *        single-swatch output with graduated legend rows that reflect
     *        what populateScene actually paints (5 bins from m_colorRamp,
     *        ranged across each sublayer's sampled attribute range, labelled
     *        with value samples). Used by LegendLayerTreeModel when the
     *        layer is a SWMMResultsLayer; the generic ISublayer aggregator
     *        remains the fallback for other host layers.
     *
     *        Each item carries the sublayerId so the legend dock's
     *        right-click → "Edit Sublayer Style…" path (Phase 5) keeps
     *        working unchanged.
     *
     *        Non-const because it may trigger lazy ensure*AttributeRange()
     *        sampling when first asked for an attribute's legend bins.
     */
    [[nodiscard]] QList<OpenSWMM::Render::LegendSymbolItem>
        sublayerLegendItems();

    /*! Slice U-2 — surface every visible-results sublayer (and the
     *  scenario / variable controls) as styleable subjects for the
     *  unified LayerStyleDialog. */
    [[nodiscard]] std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
        styleSubjects() override;

signals:
    void resultsFilePathChanged(const QString &path);
    void scenarioNameChanged(const QString &name);
    void profileLineColorChanged(const QColor &color);
    void profileStyleChanged();   /*!< any of profile{Hgl,Egl,MaxHgl,MaxEgl}Line/FillBrush changed */
    void currentTimeStepChanged(int step);
    void currentDateTimeChanged(const QDateTime &dt);
    void totalTimeStepsChanged(int count);
    void variableChanged(SWMMResultVariable var);
    void showLegendChanged(bool show);
    void resultsOpened();
    void resultsError(const QString &message);
    /*! \brief Emitted when setRenderer() swaps the renderer pointer. */
    void rendererChanged();

public:
    /*!
     * \brief Slice §Y.2 — incremental scene refresh.
     * \details Branches on m_sceneDirty: ValuesDirty walks the per-feature
     *          item cache and applies setBrush/setPen in place; Structural
     *          falls back to the base class depopulate + populate path.
     *          Clean returns immediately.  The cache is populated during
     *          populateScene and cleared in depopulateScene.
     */
    void refreshScene(QGraphicsScene *scene,
                      const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

private:
    void fetchResultsForStep(int step);
    void buildOutputIdMaps();

    /*! Slice §Y.2 — apply current per-feature colour / pen state to the
     *  cached scene items without destroying / recreating them. Called from
     *  refreshScene when m_sceneDirty == ValuesDirty. */
    void restyleScene(QGraphicsScene *scene);

    /*! L-1 — (re)build / update per-feature labels from labelConfig(). Each
     *  visible feature gets a "name: value unit" text item showing the
     *  current timestep's value for the colouring attribute. Called at the
     *  end of populateScene (structural) and restyleScene (per-frame) so the
     *  text follows the animation. No-op (and clears) when labels are off. */
    void refreshLabels(QGraphicsScene *scene);
    void clearLabels();

    /*! Slice §Y.2 — incremental restyle path can't safely handle line
     *  sublayers with flow arrows on (arrow geometry depends on the flow
     *  value, not just its colour). Returns true when at least one visible
     *  sublayer would need a structural rebuild. */
    bool requiresStructuralRebuildForRestyle() const;

    // Phase 7 (2026-05-25) — lazy attribute-range samplers. Walk every
    // period of (kind, outCode) once; cache the (min, max) result. Cleared
    // on closeResults. Used by populateScene to build a local
    // RasterColorRamp whose range is data-derived rather than the
    // hardcoded [0, 1] of the layer's m_colorRamp. Returns {0,1} on
    // sampling failure so paint still has a defined ramp.
    QPair<double, double> ensureNodeAttributeRange(int outCode);
    QPair<double, double> ensureLinkAttributeRange(int outCode);
    QPair<double, double> ensureSubcatchAttributeRange(int outCode);

    // Engine output handle ------------------------------------------------
    SWMM_Output          m_handle         = nullptr;

    // File & scenario metadata --------------------------------------------
    QString              m_resultsFilePath;
    QString              m_reportFilePath;   ///< sibling .rpt of the run (may be empty)
    QString              m_scenarioName;
    QColor               m_profileLineColor;
    int                  m_reportStepSec  = 0;

    // Per-source profile styling -----------------------------------------
    // Initialised from m_profileLineColor at construction; independently
    // editable thereafter (color changes do NOT clobber them).
    QPen                 m_profileHglLinePen;
    QBrush               m_profileHglFillBrush;
    QPen                 m_profileEglLinePen;
    QPen                 m_profileMaxHglLinePen;
    QBrush               m_profileMaxHglFillBrush;
    QPen                 m_profileMaxEglLinePen;

    // Model layer (non-owning) --------------------------------------------
    class SWMMModelLayer *m_modelLayer    = nullptr;

    // Animation state -----------------------------------------------------
    int                  m_currentStep    = 0;
    int                  m_totalSteps     = 0;
    QDateTime            m_startDateTime;
    QDateTime            m_endDateTime;

    // Variable & colour mapping -------------------------------------------
    SWMMResultVariable   m_variable       = SWMMResultVariable::NodeDepth;
    RasterColorRamp      m_colorRamp;
    bool                 m_showLegend     = true;

    // Slice BI Phase 8.13.6.3 — renderer plumbing. Initialised eagerly in
    // the ctor (default GraduatedRenderer) so renderer() never returns null.
    // The existing paint loop still reads m_colorRamp / m_variable; 8.13.6.4
    // refactors that to consult m_renderer instead.
    std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> m_renderer;

    // Slice OUT.1 — per-kind renderer slots. Indexed by
    // SWMMModelLayer::Category ordinal; slots default to nullptr (no
    // override) so paint can fall back to m_renderer. Slice OUT.2 hooks
    // the per-feature override cache into these and refactors the paint
    // loop in populateScene() to consult them.
    std::vector<std::unique_ptr<OpenSWMM::Render::IFeatureRenderer>> m_kindRenderers;

    // Slice B.5 — RuleList mirroring m_kindRenderers (lazy-built).
    mutable std::unique_ptr<OpenSWMM::Render::RuleList> m_ruleList;
    /*! Re-entrancy guard for the kind ↔ Rule mirror sync: set while a
     *  Rule-side renderer swap is being propagated INTO setKindRenderer so
     *  the mirror-refresh there doesn't bounce back into the Rule. */
    bool m_ruleKindSyncGuard = false;
    void buildRuleListLazy() const;

    // Slice Z.7a — per-frame rebinning for Rules with rebinPerFrame=true.
    // Connected to currentTimeStepChanged in buildRuleListLazy.
    void rebinDynamicRulesIfNeeded();

    // Slice OUT.2 — per-feature override cache. Sized to category row
    // count at rebuild time. Read by populateScene's paint loop in
    // preference to m_colorRamp.colorForValue(). Rebuilt whenever
    // result values change (setVariable / setCurrentTimeStep /
    // animation tick) OR when setKindRenderer() swaps a slot.
    QVector<QColor>  m_kindFeatureColors[SWMMModelLayer::NumCategories];
    QVector<double>  m_kindFeatureSizes [SWMMModelLayer::NumCategories]; /*!< negative = no override */
    QVector<double>  m_kindFeatureWidths[SWMMModelLayer::NumCategories]; /*!< negative = no override */
    QVector<int>     m_kindFeatureShapes[SWMMModelLayer::NumCategories]; /*!< -1 = no override */
    QVector<int>     m_kindFeatureDashes[SWMMModelLayer::NumCategories]; /*!< -1 = no override */
    bool             m_kindUsesOverrides[SWMMModelLayer::NumCategories] = {};

    // Slice X.21 — categorized-renderer string-attribute cache.  Keyed
    // by "<categoryOrdinal>/<classifyAttribute>" → per-row string
    // value pulled from m_modelLayer->identifyByName(...).  Identity-
    // by-name is a hot lookup that touches every cached node / link /
    // subcatch entry, so re-fetching it every animation tick is
    // wasteful — the source data is topology-invariant.  Flat string
    // key avoids the QHash<QPair<int,QString>, …> requirement of an
    // explicit qHash overload.
    mutable QHash<QString, QVector<QString>> m_categoricalAttrCache;

    /*! Slice OUT.2 — recomputes the per-feature override cache for the
     *  one kind \p c by walking every feature in the category, building
     *  an attribute map from the current result value, and asking the
     *  per-kind renderer for the symbol. Sets m_kindUsesOverrides[c].
     *  Quietly clears the cache when no per-kind renderer is installed
     *  for c, when no result vector is available for the kind's scope,
     *  or when the renderer's classifyAttribute doesn't match the
     *  current variable's enumerator name. */
    void rebuildKindFeatureOverrides(SWMMModelLayer::Category c);

    /*! Slice OUT.2 — rebuilds every kind whose geometry scope matches
     *  the layer's current m_variable (Nodes / Links / Subcatch). */
    void rebuildAllActiveKindFeatureOverrides();

    /*! Gap A2.2 — called on the layer thread when an async full-run
     *  attribute-range scan resolves. Re-classifies every Graduated kind
     *  renderer in \p scope (0 = node, 1 = link, 2 = subcatch) whose
     *  classify attribute maps to \p outCode and whose RangeMode is
     *  FixedOverRun, so breaks span the whole run instead of the frame
     *  that happened to be showing at install time. */
    void reclassifyKindsForResolvedRange(int scope, int outCode);

    // Per-step result caches (fetched from .out each time step changes) ----
    QVector<float>       m_nodeResults;     /*!< [nodeIdx] for current step */
    QVector<float>       m_linkResults;     /*!< [linkIdx] for current step */
    QVector<float>       m_subcatchResults; /*!< [subIdx]  for current step */

    // Phase 2 (2026-05-25) — concurrent multi-attribute paint. Each visible
    // sublayer carries its own `style.attribute` Q_PROPERTY; per period,
    // fetchResultsForStep populates a vector per (kind, varCode) pair that
    // any visible sublayer needs. populateScene reads from these maps using
    // the sublayer's attribute. The legacy m_nodeResults/m_linkResults/
    // m_subcatchResults remain populated (for the active m_variable) so
    // existing callers continue to work unchanged.
    QHash<int, QVector<float>>  m_nodeResultsByVar;
    QHash<int, QVector<float>>  m_linkResultsByVar;
    QHash<int, QVector<float>>  m_subcatchResultsByVar;

    // Phase 7 (2026-05-25) — per-attribute observed range, computed by
    // sampling EVERY period of a variable when first needed. Replaces the
    // hardcoded [0, 1] m_colorRamp range so values aren't clamped at one
    // end of the ramp. Cleared on closeResults / openResults; lazily
    // populated by ensure*AttributeRange() helpers in the cpp.
    QHash<int, QPair<double, double>>  m_nodeAttributeRange;
    QHash<int, QPair<double, double>>  m_linkAttributeRange;
    QHash<int, QPair<double, double>>  m_subcatchAttributeRange;

    // Per-attribute scan in-flight markers. A request from the paint path
    // (ensure*AttributeRange) returns a placeholder range immediately,
    // queues a background QtConcurrent scan of every period, and on
    // completion the result is inserted into the matching cache and a
    // repaintRequested() is emitted from the layer thread.
    QSet<int>  m_nodeRangePending;
    QSet<int>  m_linkRangePending;
    QSet<int>  m_subcatchRangePending;

    // Name → output-file index maps (built once on openResults) -----------
    QHash<QString, int>  m_nodeOutputIdx;
    QHash<QString, int>  m_linkOutputIdx;
    QHash<QString, int>  m_subcatchOutputIdx;

    // GDAL coordinate transform -------------------------------------------
    class OGRCoordinateTransformation *m_transform = nullptr;

    // Granular per-Category sublayer storage (Slice U-0) -------------------
    // One FeatureSublayer per SWMMModelLayer::Category. Indexed by the
    // category ordinal; size = SWMMModelLayer::NumCategories. Constructed
    // in the ctor with `this` as QObject parent (no manual delete). The
    // sublayers() return-list orders them by archetype (polygon → line →
    // point) so paint order looks correct out of the box.
    OpenSWMM::Render::FeatureSublayer *m_featureSublayers[SWMMModelLayer::NumCategories] = {};

    // User-customisable sublayer paint order (Slice GUI-2026-05-30 §2).
    // Populated lazily on first sublayers() / moveSublayer() call from the
    // archetype-default sequence (polygon → line → marker).  Reordered via
    // moveSublayer(); persisted by ISublayerHost::saveSublayersToJson and
    // restored by loadSublayersFromJson on .oswp load.
    mutable QList<OpenSWMM::Render::ISublayer *> m_sublayerOrder;

    // Slice §Y.2 — incremental scene refresh state.
    //
    // m_sceneDirty drives refreshScene's branch.  Structural forces the
    // legacy depopulate + populate path (every QGraphicsItem destroyed and
    // re-allocated).  Values reuses the cached items and only mutates
    // brush / pen.  Clean returns immediately.  The default is Structural
    // so the first paint after construction (or after a results file
    // open) still produces a full scene.
    enum class SceneDirty : int { Clean = 0, Values = 1, Structural = 2 };
    SceneDirty m_sceneDirty { SceneDirty::Structural };

    // Per-Category item cache.  populateScene records the primary
    // QGraphicsItem for every feature row into the matching slot; null
    // entries are valid (NaN values / missing geometry / filtered rows).
    // restyleScene walks the slots and applies setBrush / setPen in place.
    // depopulateScene clears the slots.  Sizing tracks the model's
    // categoryCount at population time; restyleScene falls back to
    // structural when the size mismatches the current count (mid-session
    // topology edit).
    QVector<QGraphicsItem *> m_itemByFeature[SWMMModelLayer::NumCategories];

    // L-1 — parallel per-feature label items ("name: value"). Same sizing /
    // lifecycle contract as m_itemByFeature: built/refreshed by refreshLabels,
    // cleared by depopulateScene (base destroys the tagged scene items).
    QVector<QGraphicsSimpleTextItem *> m_labelByFeature[SWMMModelLayer::NumCategories];

    /*! Slice §Y.2 — flip to the strongest pending dirtiness without ever
     *  downgrading (Structural beats Values beats Clean).  Used by the
     *  setter cluster (setVariable, setColorRamp, sublayer toggles, …) so
     *  a pending structural rebuild isn't lost when a subsequent values-
     *  only mutation arrives. */
    void escalateSceneDirty(SceneDirty next);
};

Q_DECLARE_METATYPE(SWMMResultsLayer *)
Q_DECLARE_METATYPE(SWMMResultVariable)

#endif // SWMMRESULTSLAYER_H
