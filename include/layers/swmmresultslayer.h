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
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <memory>

class OpenSWMMVisWorkspace;
class SpatialReferenceSystem;

namespace OpenSWMM::Render { class IFeatureRenderer; }

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
class SWMMResultsLayer : public OpenSWMMVisLayer
{
    Q_OBJECT

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

    // ----- Results file ---------------------------------------------------

    [[nodiscard]] QString resultsFilePath() const;

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

    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

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

private:
    void fetchResultsForStep(int step);
    void buildOutputIdMaps();

    // Engine output handle ------------------------------------------------
    SWMM_Output          m_handle         = nullptr;

    // File & scenario metadata --------------------------------------------
    QString              m_resultsFilePath;
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

    // Slice OUT.2 — per-feature override cache. Sized to category row
    // count at rebuild time. Read by populateScene's paint loop in
    // preference to m_colorRamp.colorForValue(). Rebuilt whenever
    // result values change (setVariable / setCurrentTimeStep /
    // animation tick) OR when setKindRenderer() swaps a slot.
    QVector<QColor>  m_kindFeatureColors[SWMMModelLayer::NumCategories];
    QVector<double>  m_kindFeatureSizes [SWMMModelLayer::NumCategories]; /*!< negative = no override */
    bool             m_kindUsesOverrides[SWMMModelLayer::NumCategories] = {};

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

    // Per-step result caches (fetched from .out each time step changes) ----
    QVector<float>       m_nodeResults;     /*!< [nodeIdx] for current step */
    QVector<float>       m_linkResults;     /*!< [linkIdx] for current step */
    QVector<float>       m_subcatchResults; /*!< [subIdx]  for current step */

    // Name → output-file index maps (built once on openResults) -----------
    QHash<QString, int>  m_nodeOutputIdx;
    QHash<QString, int>  m_linkOutputIdx;
    QHash<QString, int>  m_subcatchOutputIdx;

    // GDAL coordinate transform -------------------------------------------
    class OGRCoordinateTransformation *m_transform = nullptr;
};

Q_DECLARE_METATYPE(SWMMResultsLayer *)
Q_DECLARE_METATYPE(SWMMResultVariable)

#endif // SWMMRESULTSLAYER_H
