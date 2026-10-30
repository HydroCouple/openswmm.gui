/**
 * @file      swmmmodellayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Map layer that renders an OpenSWMMEngine network (nodes, links,
 *         subcatchments, rain gages) and provides geometry-editing and
 *         spatial-query APIs.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMMODELLAYER_H
#define SWMMMODELLAYER_H

#include "layers/openswmmvislayer.h"
#include "layers/swmm_category.h"
#include "layers/nodecoupling2d.h"   // 1D↔2D node coupling snapshots
#include "core/preferencesmanager.h" // NewProjectSpec defaults structs
#include "render/labelconfig.h"
#include "render/iattributeprovider.h" // Slice DM.3
#include "render/markershape.h"
#include "render/legendsymbolitem.h" // X4 — legend-as-editor facade

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

// Forward declaration — nanoflann types are confined to swmmmodellayer.cpp
// so the header stays free of the nanoflann.hpp template machinery.
struct SWMMKdTrees;

#include <QColor>
#include <QDateTime>

#include <openswmm/engine/openswmm_callbacks.h>      // SWMM_Engine typedef
#include <openswmm/engine/openswmm_infrastructure.h> // SWMM_InletUsage (by value)
#include <QFont>
#include <QMap>
#include <QPen>
#include <QBrush>
#include <QPointer>
#include <QPolygonF>
#include <QSet>
#include <QVariantMap>

class OpenSWMMVisWorkspace;
class SpatialReferenceSystem;
class MapCanvas; // setEditCanvas() — undo-stack host for mediated edits

namespace OpenSWMM::Render
{
class IFeatureRenderer;
class GraduatedRenderer; // classifyGraduatedIfNeeded — data-derived breaks.
class Rule;
class RuleList;           // Slice B.4 — see ruleList() override below.
enum class ClassEditKind; // X4 — legend-as-editor facade (full def in ifeaturerenderer.h)
}

// Slice BS Phase 6.9.2 — hydrograph MVC layer. Forward-declared so the
// header doesn't drag in <QAbstractItemModel>; accessors return pointers
// that consumers can wire to QTableView / QListView. See
// include/layers/hydrographmodels.h.
class HydrographGroupListModel;
class HydrographRtkTableModel;
class HydrographIaTableModel;
class HydrographDecayTableModel;

namespace openswmmvis::ui
{
class UserFlagsModel; // [USER_FLAGS] / [USER_FLAG_VALUES] store — see ensureUserFlagsModel().
}

// Persistent per-kind symbol adapter set — see elementSymbolAdapter().
class SwmmElementSymbolAdapter;

/*!
 * \struct SWMMElementSymbol
 * \brief Rendering style for a class of SWMM network elements.
 */
struct SWMMElementSymbol {
    QColor fillColor = Qt::blue;
    QColor outlineColor = Qt::darkBlue;
    double outlineWidth = 1.0;
    double size = 8.0; /*!< Marker diameter / line width in pixels. */
    OpenSWMM::Render::MarkerShape markerShape = OpenSWMM::Render::MarkerShape::Circle;
    /*!< Point-marker glyph (per-kind defaults
                                        seeded in SWMMModelLayer ctor). Ignored
                                        for line and polygon categories. */
    bool showLabel = false;
    QFont labelFont;
    QColor labelColor = Qt::black;

    // Slice BI Phase 8.13.8-mini (2026-05-24) — flow-direction arrows
    // for link kinds (Conduits / Pumps / Orifices / Weirs / Outlets).
    // Ignored for point / polygon kinds. The arrow points from the
    // link's upstream node to its downstream node — i.e. follows the
    // polyline tangent at the midpoint of the visible polyline.
    /*! Off in the shared default because this struct also backs node and
     *  polygon kinds, whose editors would otherwise show a ticked "Show
     *  flow arrows". The five LINK kinds are seeded on in the
     *  SWMMModelLayer constructor. */
    bool showArrows = false;
    double arrowSize = 16.0; /*!< Arrowhead length in pixels (along the link). */
    /*! Arrowhead width in pixels, ACROSS the link — independent of the
     *  length so an arrow can be made stubby or needle-thin. The default
     *  is deliberately narrower than the length so the head reads as a
     *  direction marker rather than a blob. Projects saved before the width
     *  became independent carry no arrowWidthPx key and so adopt this
     *  default (they previously drew at 1.2 x length). */
    double arrowWidth = 8.0;
    QColor arrowColor = QColor(34, 34, 34); /*!< Near-black. */
    // Slice FX.1 — was `true` by default, but that gates arrows on a
    // bound `.out` (every link has flow=0 pre-simulation). Users who
    // toggle "Show flow arrows" expect arrows to appear immediately,
    // independent of whether a sim has run. The flow-positive filter
    // remains opt-in via the Arrows tab → "Only when flow > 0".
    bool arrowOnlyWhenFlowPos = false;
};

/*!
 * \class SWMMModelLayer
 * \brief Renders the SWMM network elements (nodes, links, subcatchments, rain gages)
 *        for one OpenSWMMEngine model.
 * \details The layer uses the coordinate frame of the OpenSWMMEngine model as its
 *          native CRS.  When the canvas CRS differs, coordinates are reprojected
 *          using GDAL's OGRCoordinateTransformation.
 *
 *          The layer also provides:
 *          - Selection tracking (highlighted with a secondary colour).
 *          - Identify-by-point returning element attributes as QVariantMap.
 *          - Label display driven by element names.
 */
class SWMMModelLayer : public OpenSWMMVisLayer,
                       public OpenSWMM::Render::IAttributeProvider // Slice DM.3
{
    Q_OBJECT
    Q_INTERFACES(OpenSWMM::Render::IAttributeProvider) // Slice DM.3

    // The batched scene-item renderer reads the SoA + GDAL transform
    // directly so paint() is a single tight pass over the cached data.
    friend class SWMMLayerItem;
    // Phase B.RHI — the QSG renderer reads the same flat buffers to
    // populate its QSGGeometryNode vertex data on geometry change.
    friend class SWMMLayerQSGRenderer;

public:
    /*!
     * \brief The SWMM-element Category enum.
     *
     *        Definition moved to \c include/layers/swmm_category.h so leaf
     *        tests + sublayer headers can depend on the enum alone without
     *        pulling in the full layer's engine + Qt GUI dependency graph.
     *        Re-exported here as a member alias so existing call-sites
     *        (`SWMMModelLayer:: Category`, `SWMMModelLayer:: CatJunctions`,
     *        …) continue to compile unchanged.
     */
    using Category = OpenSWMMVis::SwmmCategory;
    static constexpr Category CatJunctions = OpenSWMMVis::CatJunctions;
    static constexpr Category CatOutfalls = OpenSWMMVis::CatOutfalls;
    static constexpr Category CatStorage = OpenSWMMVis::CatStorage;
    static constexpr Category CatDividers = OpenSWMMVis::CatDividers;
    static constexpr Category CatConduits = OpenSWMMVis::CatConduits;
    static constexpr Category CatPumps = OpenSWMMVis::CatPumps;
    static constexpr Category CatOrifices = OpenSWMMVis::CatOrifices;
    static constexpr Category CatWeirs = OpenSWMMVis::CatWeirs;
    static constexpr Category CatOutlets = OpenSWMMVis::CatOutlets;
    static constexpr Category CatSubcatchments = OpenSWMMVis::CatSubcatchments;
    static constexpr Category CatRainGages = OpenSWMMVis::CatRainGages;
    static constexpr Category NumCategories = OpenSWMMVis::NumCategories;

    /*!
     * \enum DataCategory
     * \brief Slice BM.0 — non-spatial data-object categories surfaced
     *        below the spatial section in the Object Browser. Keyed
     *        independently from `Category` to keep the existing tree
     *        logic untouched.
     *
     *        Each enum value maps 1:1 to an engine `swmm_<type>_count`
     *        + `swmm_<type>_id` accessor pair (see `dataObjectCount` /
     *        `dataObjectNameAt`). Curves and Time Series both come out
     *        of the unified engine tables array — they are partitioned
     *        on the GUI side via `swmm_table_get_type`.
     */
    enum DataCategory {
        DataCurves = 0,
        DataTimeSeries,
        DataPatterns,
        DataLIDControls,
        DataPollutants,
        DataLandUses,
        DataAquifers,
        DataSnowpacks,
        DataControls,
        DataTransects,
        DataHydrographs,
        DataStreets,
        DataInlets,
        NumDataCategories
    };

    Q_PROPERTY(QString modelFilePath READ modelFilePath NOTIFY modelFilePathChanged)
    Q_PROPERTY(bool showNodes READ showNodes WRITE setShowNodes NOTIFY showNodesChanged)
    Q_PROPERTY(bool showLinks READ showLinks WRITE setShowLinks NOTIFY showLinksChanged)
    Q_PROPERTY(bool showSubcatchments READ showSubcatchments WRITE setShowSubcatchments NOTIFY
                   showSubcatchmentsChanged)
    Q_PROPERTY(
        bool showRainGages READ showRainGages WRITE setShowRainGages NOTIFY showRainGagesChanged)
    Q_PROPERTY(bool showLabels READ showLabels WRITE setShowLabels NOTIFY showLabelsChanged)

public:
    /**
     * @param[in] modelFilePath Model filename/provenance copied as supplied; no engine/file is
     *   opened here.
     * @param[in] parent Optional workspace QObject owner.
     * @note GUI-thread construction seeds per-kind renderers/symbols/opacities and live preference
     *   inheritance. A symbol field still equal to its inherited preference follows later changes;
     *   customized fields stay. Model/attribute edits bump edit revision immediately and coalesce
     *   derived-style refresh, and invalidate lazy inlet connectors. Ordinary
     *   layer/engine/cache/editor methods require serialized owning-thread access; explicitly
     *   separated open/build phases are exceptions.
     */
    explicit SWMMModelLayer(const QString &modelFilePath, OpenSWMMVisWorkspace *parent = nullptr);

    /**
     * @brief Close/destroy owned engine, notify engine borrowers and destroy owned forward/inverse
     *   coordinate transforms.
     * @note Scene renderer/items borrow layer state and must be removed before destruction. Owned
     *   registries/models/adapters are QObject children; no external engine handle remains valid.
     */
    ~SWMMModelLayer() override;

    // ----- Model file -----------------------------------------------------

    /**
     * @return Absolute filesystem interpretation of stored nonempty path, or empty for untitled
     *   engine. Does not canonicalize/check existence.
     */
    [[nodiscard]] QString modelFilePath() const;
    /**
     * @param[in] path Raw new path/provenance; can be empty, relative or nonexistent.
     * @note Equality no-op; change stores and emits modelFilePathChanged with raw argument. Does
     *   not reload engine, rebase references, rename layer, change CRS or touch disk.
     */
    void setModelFilePath(const QString &path);

    // ----- Self-description for the Layer Properties dialog ----------------
    /**
     * @return Absolute model path, or localized no-file placeholder for untitled model.
     */
    [[nodiscard]] QString sourceDescription() const override;
    /**
     * @return Owned localized nonempty network-category counts plus
     *   curve/timeseries/pattern/pollutant/transect/control counts; data catalog lookup can
     *   populate caches.
     */
    [[nodiscard]] QVector<QPair<QString, QString>> extendedMetadata() const override;

    /** Raw engine handle — valid only after a successful loadModel().
     * @return Borrowed current engine handle, null until adoption and invalid after
     *   close/reload/destruction; never close/destroy it externally. Not an independently
     *   thread-safe snapshot.
     */
    [[nodiscard]] SWMM_Engine engine() const;

    /*!
     * \brief Whether the layer's CRS was ASSIGNED — read from the .inp's
     *        [OPTIONS] CRS, or set after load by the user (CRS picker,
     *        Simulation Options, canvas reprojection, .oswp restore) — as
     *        opposed to auto-derived on open from [MAP] UNITS or the
     *        preferences default. Only an assigned CRS is written back to
     *        the .inp on save; a model that carried none must stay that way.
     * @return Whether CRS came from model input or later explicit setSRS, rather than automatic
     *   local/preferences default. This controls write-back policy, not coordinate validity.
     */
    [[nodiscard]] bool crsAssigned() const noexcept { return m_crsAssigned; }

    /*!
     * \brief Loads (or reloads) the SWMM input file and rebuilds geometry caches.
     * \returns true on success.
     * @param[out] warnings Existing list retained and appended with nonblocking engine-open
     *   diagnostics.
     * @param[out] errors Existing list retained and appended with hard-open failure detail.
     * @return False after releasing prior model if hard open fails; true after build/adopt even
     *   when permissive-open diagnostics exist.
     * @note Synchronous destructive reload: closes current engine/caches first, opens lenient
     *   editable engine, copies geometry and performs GUI unit/CRS finalization. Does not
     *   run/finalize hydraulics or write result files. No rollback to previous model on failure.
     */
    bool loadModel(QList<QString> &warnings, QList<QString> &errors);

    /*!
     * \brief Create + open an engine for \a path (worker-thread safe).
     *
     * Pure engine C-API calls — no QObject mutation, no signals — so it may
     * run in a QtConcurrent worker while the GUI thread stays responsive.
     * On failure returns nullptr and fills \a errorDetail with the engine's
     * detailed diagnostic (section/line for parse errors).
     *
     * \param openMs  Optional out: elapsed open time in ms (for the load
     *                timing log adoptOpenEngine() emits).
     * @param[in] path Nonempty existing model path; .inp text is checked for disallowed control
     *   bytes before engine open.
     * @param[out] errorDetail Optional failure diagnostic destination, unchanged on success.
     * @return Newly created lenient OPENED engine handle on success, exclusive ownership to caller
     *   until adoptOpenEngine(); caller must close/destroy abandoned handles. Null on
     *   path/read/creation/hard-parser failure; failed engine is cleaned up.
     * @note openMs is optional milliseconds assigned only on success. Worker-safe phase mutates no
     *   layer/QObjects/signals; restores authored adverse-slope conduit orientation for editing.
     *   Empty report/output paths prevent output files. Parse/validation warnings remain on
     *   returned engine; simulation uses a separate strict open.
     */
    static SWMM_Engine openEngineForPath(const QString &path, QString *errorDetail,
                                         qint64 *openMs = nullptr);

    /*!
     * \brief Populate the SoA arrays + geometry caches from \a engine.
     *
     * The heavy half of a model open — per-object SoA copy + buildGeometryCache
     * (KD-trees, bboxes, scene coords). Reads engine C-API and writes plain member arrays without widget creation
     * or singleton access. The geometry build can emit base extentChanged, so
     * worker callers must isolate/block notifications and all layer access. Does NOT
     * assign m_engine (ownership stays with the caller until adoptOpenEngine).
     *
     * The layer MUST NOT be painted while this runs (its scene arrays are being
     * resized/filled) — callers keep it non-visible until adoptOpenEngine.
     *
     * @param[out] soaMsOut Optional elapsed source-array-copy milliseconds.
     * @param[out] geomMsOut Optional elapsed geometry-cache-build milliseconds.
     * @param[in] engine Nonnull readable engine borrowed until adopt/cleanup; must be stable and
     *   compatible with editable spatial C API.
     * @note Existing SoA arrays must be empty: this method reserves and appends, it does not clear
     *   them or adopt the engine. Cleans duplicate link bends/polygon closing points, orients bends
     *   to endpoints and relocates zero-coordinate rain gages to feature-vertex mean in GUI cache
     *   only. Builds geometry/index arrays and timing telemetry. Worker-safe only with exclusive
     *   layer/engine access and no paint/read/edit while arrays are rebuilt; optional phase outputs
     *   are milliseconds.
     */
    void buildFromEngine(SWMM_Engine engine, qint64 *soaMsOut = nullptr,
                         qint64 *geomMsOut = nullptr);

    /**
     * @brief Rebuild geometry after a mediated native engine edit without replacing the engine.
     * @note GUI thread only, with exclusive engine access. Keeps styling and named selection;
     * emits one geometryChanged/attributeChanged/modelEdited/repaintRequested batch.
     */
    void rebuildFromEngine();
    /**
     * @brief Adopt a validated replacement engine for snapshot undo/redo.
     * @param[in] engine Nonnull editable engine; ownership transfers to this layer.
     * @note GUI thread only. Destroys the prior engine, preserves style and named visibility/
     * selection, rebuilds caches, and emits the same edit notifications as rebuildFromEngine().
     */
    void replaceEngineForEdit(SWMM_Engine engine);

    /*!
     * \brief Adopt an engine whose geometry was already built by
     *        buildFromEngine(). GUI-thread only: assigns the engine, syncs
     *        units, resolves the CRS, and emits the load signals.
     * @param[in] openMs Engine-open milliseconds for telemetry; negative recorded as zero.
     * @param[in] soaMs Source-array-copy milliseconds for telemetry.
     * @param[in] geomMs Geometry-cache-build milliseconds for telemetry.
     * @param[in] engine Nonnull successfully opened/built engine whose exclusive ownership
     *   transfers here; current arrays must already match it.
     * @param[out] warnings Appended trimmed engine errors/warnings as nonblocking editing
     *   diagnostics.
     * @param[out] errors Unused in this implementation; unchanged.
     * @return True after adoption/finalization; no extra validation failure path is provided.
     * @note GUI-thread only. If another engine remains attached, closeEngine() clears arrays, so
     *   normal caller must close before buildFromEngine(). Synchronizes global UnitSystem, resolves
     *   CRS from input then local MAP/preferences then untitled, records whether assigned, requests
     *   repaint, logs supplied phase milliseconds and emits path/modelLoaded. Leaves engine
     *   OPENED/BUILDING for edits and preserves untitled display name when path empty.
     */
    bool adoptOpenEngine(SWMM_Engine engine, QList<QString> &warnings, QList<QString> &errors,
                         qint64 openMs = -1, qint64 soaMs = 0, qint64 geomMs = 0);

    /** Close and destroy the engine, clearing all geometry caches.
     * @note If attached, emits engineAboutToClose before close/destroy and invalidates all borrowed
     *   engine handles. Clears network arrays, per-object hides/selection,
     *   category-location/spatial caches and marks scene rebuild. Does not emit
     *   selectionChanged/modelLoaded/repaint or clear every style/scene/registry cache; caller
     *   coordinates replacement/UI refresh. GUI-thread only; worker builders/paints must have
     *   stopped.
     */
    void closeEngine();

    // ----- In-memory new project (File → New) -----------------------------

    /*! \brief Everything a blank untitled project needs — the in-memory
     *  replacement for the synthetic temp .inp File → New used to write. */
    struct NewProjectSpec {
        QString name; ///< "[TITLE]" + display name ("Untitled").
        QDateTime startDateTime;
        QDateTime endDateTime;
        bool forNewEngine = true;                   ///< Gate NODE_CONTINUITY / ANDERSON_ACCEL / 2D.
        PreferencesManager::SimulationDefaults sim; ///< THREADS already maxed by caller.
        PreferencesManager::TwoDDefaults twoD; ///< Used iff forNewEngine && sim.module2DEnabled.
    };

    /*!
     * \brief Create a blank BUILDING-state engine (`swmm_engine_new`) and
     *        stamp the spec's defaults on it via the options C API — no file
     *        involved. Never calls `swmm_finalize_model` (validation needs
     *        ≥ 1 node + outfall, which a blank project has not got).
     * \returns nullptr on failure with \a errorDetail filled.
     * @param[in] spec Copied configuration source: title, start/end times, simulation defaults and
     *   conditional modern-engine/2D options. Caller supplies coherent dates/ranges/options.
     * @param[out] errorDetail Optional detailed required-option failure message; unchanged on
     *   success.
     * @return Exclusive new BUILDING engine or null after cleanup if creation/required-option
     *   seeding fails. No finalize call, network objects or filesystem model are created; caller
     *   must close/destroy until adopted.
     * @note Worker-safe engine-only phase. Option time steps are seconds, flow tolerances/min slope
     *   percent and remaining values follow preference/C API contracts. Optional2D extended-key
     *   refusals log warnings without failing; required option errors aggregate and reject entire
     *   engine.
     */
    static SWMM_Engine createBlankEngine(const NewProjectSpec &spec, QString *errorDetail);

    /*!
     * \brief In-memory sibling of the loadModel path: createBlankEngine →
     *        buildFromEngine → adoptOpenEngine, all synchronous (a blank
     *        engine build is instant, so no worker hop). The layer keeps its
     *        empty modelFilePath until the first Save As.
     * @param[in] spec Blank-engine configuration, with coherent dates and supported preference
     *   defaults.
     * @param[out] warnings Appended engine diagnostics during GUI adoption.
     * @param[out] errors Appended hard blank-engine creation/default-seeding failure.
     * @return False after releasing old engine when blank creation fails, true after synchronous
     *   build/adoption. No rollback, save or hydraulic validation; stored modelFilePath is not
     *   cleared by this method, so caller must construct an untitled layer for untitled behavior.
     */
    bool adoptNewEngine(const NewProjectSpec &spec, QList<QString> &warnings,
                        QList<QString> &errors);

    // ----- Element visibility toggles -------------------------------------

    /**
     * @return Stored visibility gate for four node kinds, independently of
     *   per-object/category/render-rule visibility.
     */
    [[nodiscard]] bool showNodes() const;
    /**
     * @param[in] show Requested four node kinds visibility.
     * @note Changed flag marks scene rebuild and emits showNodesChanged(show)/repaintRequested().
     *   Equality is a no-op; does not change per-object hidden sets or engine data.
     */
    void setShowNodes(bool show);

    /**
     * @return Stored visibility gate for five hydraulic link kinds, independently of
     *   per-object/category/render-rule visibility.
     */
    [[nodiscard]] bool showLinks() const;
    /**
     * @param[in] show Requested five hydraulic link kinds visibility.
     * @note Changed flag marks scene rebuild and emits showLinksChanged(show)/repaintRequested().
     *   Equality is a no-op; does not change per-object hidden sets or engine data.
     */
    void setShowLinks(bool show);

    /**
     * @return Stored visibility gate for subcatchment polygons/outlet lines, independently of
     *   per-object/category/render-rule visibility.
     */
    [[nodiscard]] bool showSubcatchments() const;
    /**
     * @param[in] show Requested subcatchment polygons/outlet lines visibility.
     * @note Changed flag marks scene rebuild and emits
     *   showSubcatchmentsChanged(show)/repaintRequested(). Equality is a no-op; does not change
     *   per-object hidden sets or engine data.
     */
    void setShowSubcatchments(bool show);

    /**
     * @return Stored visibility gate for rain gage point geometry, independently of
     *   per-object/category/render-rule visibility.
     */
    [[nodiscard]] bool showRainGages() const;
    /**
     * @param[in] show Requested rain gage point geometry visibility.
     * @note Changed flag marks scene rebuild and emits
     *   showRainGagesChanged(show)/repaintRequested(). Equality is a no-op; does not change
     *   per-object hidden sets or engine data.
     */
    void setShowRainGages(bool show);

    /**
     * @return Stored visibility gate for legacy global label display flag, independently of
     *   per-object/category/render-rule visibility.
     */
    [[nodiscard]] bool showLabels() const;
    /**
     * @param[in] show Requested legacy global label display flag visibility.
     * @note Changed flag marks scene rebuild and emits showLabelsChanged(show)/repaintRequested().
     *   Also synchronizes inherited LabelConfig.enabled through base setter even if legacy flag was
     *   already equal; this can produce additional config/repaint notification.
     */
    void setShowLabels(bool show);

    /*! \brief Slice X.18 — full label configuration.
     *
     *         Supersedes the binary `showLabels` toggle: every layer that
     *         paints text labels now consults `labelConfig().enabled` AS
     *         WELL AS the legacy `showLabels` for backwards compatibility
     *         with .oswp files written by earlier builds.  Setters keep
     *         the two in sync until the legacy flag is removed in a
     *         follow-up. */
    // VS.10 — labelConfig() inherited from OpenSWMMVisLayer; only the setter
    // is overridden to keep the legacy m_showLabels flag in sync.
    /**
     * @param[in] cfg Full value configuration copied through base setter; enabled synchronizes
     *   legacy showLabels.
     * @note Equality against base config is a no-op. Changed legacy enabled emits showLabelsChanged
     *   and marks scene rebuild before base config/paint notifications. Per-kind label
     *   configuration remains separate; no engine edits.
     */
    void setLabelConfig(const OpenSWMM::Render::LabelConfig &cfg) override;

    /*! Per-kind QSG render scope. Each flag means "this kind's GEOMETRY is
     *  drawn by the QSG (GPU) overlay; the CPU SWMMLayerItem must NOT
     *  draw it".  Symmetrically, the QSG renderer (SWMMLayerQSGRenderer)
     *  uploads empty geometry for any kind NOT in the scope, so a kind's
     *  geometry is drawn by exactly one pipeline.
     *
     *  EXCEPTION — text labels. Labels are ALWAYS painted by the CPU pass
     *  (SWMMLayerItem's label block, deliberately not gated on these
     *  flags); the QSG renderer has no text pipeline. This hybrid is the
     *  documented contract (LAYER_STYLING_LABELING_PLAN_2026-08-16) — see
     *  the doc comment atop swmmlayerqsgrenderer.cpp before changing it.
     *
     *  Progressive migration: nodes go QSG first, then links, then
     *  catchments. Default is empty — i.e. everything stays on the
     *  CPU path, the QSG overlay never runs, and behaviour matches
     *  the legacy renderer.
     *
     *  Slice §QSG-1 (2026-05-26) — replaces the previous bool
     *  `glRenderingEnabled` which was an all-or-nothing gate. */
    enum QsgKind : quint8 {
        QsgNone = 0x00,
        QsgNodes = 0x01, ///< Junctions / outfalls / storage / dividers.
        QsgLinks = 0x02, ///< Conduits / pumps / orifices / weirs / outlets.
        QsgCatch = 0x04, ///< Subcatchments (polygons + outlet lines).
        QsgGages = 0x08, ///< Rain gages.
    };
    Q_DECLARE_FLAGS(QsgKinds, QsgKind)

    /**
     * @return Stored geometry ownership bitmask; default none, text remains CPU-painted.
     */
    [[nodiscard]] QsgKinds qsgRenderKinds() const noexcept { return m_qsgKinds; }
    /**
     * @param[in] kinds Geometry ownership mask copied without filtering unknown bits.
     * @note Equality no-op; changed value emits qsgRenderKindsChanged/repaintRequested without
     *   rebuilding source geometry or changing CPU label ownership.
     */
    void setQsgRenderKinds(QsgKinds kinds);

    /*! Convenience: does the QSG overlay own this kind (i.e. is the
     *  flag set)? Hot-path helpers in SWMMLayerItem use this to skip
     *  CPU painting, and SWMMLayerQSGRenderer uses the negation to
     *  short-circuit vertex uploads.
     * @param[in] k Single QsgKind ownership flag; testFlag semantics apply to zero/composite
     *   values.
     * @return Whether stored QSG ownership scope contains flag, without checking layer visibility
     *   or live renderer readiness.
     */
    [[nodiscard]] bool qsgOwnsKind(QsgKind k) const noexcept { return m_qsgKinds.testFlag(k); }

    /*! Back-compat shim — keeps the old all-or-nothing API working
     *  while callers (mapcanvas.cpp, swmmlayeritem.cpp, tests) are
     *  migrated to the per-kind flags. setGlRenderingEnabled(true) is
     *  equivalent to enabling every QSG kind.
     * @return Whether any QSG kind flag is set, not whether a renderer initialized successfully.
     */
    [[nodiscard]] bool glRenderingEnabled() const noexcept { return m_qsgKinds != QsgNone; }
    /**
     * @param[in] on True selects all four geometry-kind QSG flags, false clears scope.
     * @note Compatibility facade delegates to setQsgRenderKinds(); label rendering remains CPU.
     */
    void setGlRenderingEnabled(bool on)
    {
        setQsgRenderKinds(on ? QsgKinds(QsgNodes | QsgLinks | QsgCatch | QsgGages)
                             : QsgKinds(QsgNone));
    }

    // ----- Per-object visibility (Slice O) --------------------------------

    /*!
     * \brief Per-object visibility flag. Names present in
     *        \ref m_hiddenObjects are skipped by \ref populateScene. The
     *        Object Browser drives this state: a leaf checkbox is the
     *        only source of truth for an individual object; group-header
     *        toggles apply in bulk to every child but do not add a
     *        separate gate — after a group toggle the children's states
     *        fully determine visibility, and subsequent per-leaf toggles
     *        don't back-propagate to the header.
     * @param[in] name Exact stored object name, potentially shared by distinct engine namespaces.
     * @return True unless name occurs in aggregate hidden set. This untyped legacy query cannot
     *   distinguish a same-named visible kind from another hidden kind; use typed overload for tree
     *   rows.
     */
    [[nodiscard]] bool isObjectVisible(const QString &name) const;

    /*!
     * \brief Typed variant — visibility of the object of category \p c
     *        named \p name. SWMM names are per-type namespaces (a rain
     *        gage and a subcatchment may legally share a name), so the
     *        Object Browser uses this to read leaf check states without
     *        one kind's toggle bleeding into a same-named other kind.
     * @param[in] name Exact object name.
     * @param[in] c Spatial category, mapped to node/link/catch/gage bit.
     * @return Typed hidden-mask inverse when present, otherwise legacy hidden-name inverse. Does
     *   not validate existence or apply category/global/zoom/rule gates.
     */
    [[nodiscard]] bool isObjectVisible(const QString &name, Category c) const;
    /**
     * @param[in] name Exact name; no type information is supplied.
     * @param[in] visible Requested per-name visibility for all namespaces sharing that name.
     * @note Unknown names may be stored hidden. Changed hide/show updates aggregate sets, all-kind
     *   masks, known category counter, flag arrays and rebuild state, then emits one repaint. An
     *   already hidden typed name is not expanded by hide(false); show(true) removes its entire
     *   typed mask. No engine change or dedicated visibility signal.
     */
    void setObjectVisible(const QString &name, bool visible);

    /*!
     * \brief Batch form — apply the same visibility to every name in
     *        \p names with a single repaintRequested() emission.
     *        Used by the group-header checkbox so toggling a category
     *        of 1000+ objects causes only one canvas refresh.
     * @param[in] names Exact name list; duplicates are harmless.
     * @param[in] visible Requested per-name visibility for all namespaces sharing that name.
     * @note Unknown names may be stored hidden. Changed hide/show updates aggregate sets, all-kind
     *   masks, known category counter, flag arrays and rebuild state, then emits one repaint. An
     *   already hidden typed name is not expanded by hide(false); show(true) removes its entire
     *   typed mask. No engine change or dedicated visibility signal.
     */
    void setObjectsVisible(const QList<QString> &names, bool visible);

    /*!
     * \brief Names currently hidden via per-object toggles. Used by the
     *        Object Browser to seed leaf-row check states on refresh.
     * @return Owned aggregate hidden-name set, including any typed hide even if other same-named
     *   kinds remain visible. Type-specific masks are not included.
     */
    [[nodiscard]] QSet<QString> hiddenObjects() const { return m_hiddenObjects; }

    /*!
     * \brief Largest rendered marker half-bound across every SWMM
     *        element type, in PIXELS. Callers use this as a floor for
     *        the Select tool's pixel tolerance so clicks landing
     *        inside the visible glyph always hit, regardless of the
     *        user's tolerance preference. For a square Storage glyph
     *        the half-bound is half-width * sqrt(2) (diagonal); for
     *        circular Junctions it's the radius; polygons and
     *        polylines return 0 (they're handled by bbox / segment-
     *        distance tests inside identifyAt, which don't need the
     *        floor).
     * @return Conservative maximum legacy marker half-bound in screen pixels across junction,
     *   outfall, storage, divider, gages; polygon/line geometry excluded. Uses fixed
     *   circle/diagonal multipliers rather than current arbitrary glyph geometry, virtual/inlet
     *   overrides or renderer data-defined sizes.
     */
    [[nodiscard]] double maxMarkerHalfBoundPx() const;

    // ----- Category-aware API (consumed by SWMMObjectTreeModel) -----------

    /*!
     * \brief Number of objects in the given category. O(1).
     * @param[in] c Spatial category.
     * @return Current source-array category membership count; invalid category0, independent of
     *   custom display ordering/visibility.
     */
    [[nodiscard]] int categoryCount(Category c) const;

    /*!
     * \brief Object name at (category, row). O(1) — reads through the
     *        per-category index buckets populated in buildGeometryCache.
     *        Returns an empty string if the indices are out of range.
     * @param[in] c Spatial category.
     * @param[in] row Zero-based display row within this category, not global engine index.
     * @return Object name in category display-row order, honoring user SoA permutation; empty for
     *   invalid row/category/stale index.
     */
    [[nodiscard]] QString objectNameAt(Category c, int row) const;

    /*!
     * \brief SoA index at (category, row) — the index `objectNameAt`
     *        resolves the name from, and therefore also the ENGINE
     *        index for that object. Returns -1 when out of range.
     *
     *        The SoA-index == engine-index invariant holds because
     *        buildFromEngine() appends in engine order (0..count-1),
     *        adds append to the tail on both sides, and deletes remove
     *        the same slot from both. Existing code already depends on
     *        it: applyNodeDelete() feeds a `swmm_node_index()` result
     *        straight into `m_nodes.removeAt()`, and its cascade loop
     *        passes one integer to both `swmm_link_get_from_node()` and
     *        `m_links[i]`. See also applyLinkDelete / applyGageDelete /
     *        applySubcatchDelete.
     *
     *        Callers that want a row's engine index MUST use this and
     *        not a locally-written index lookup: this honours the
     *        intra-category ordering overrides (`m_objectOrderOverrides`)
     *        exactly as objectNameAt does, and a variant that forgets
     *        that branch returns a different object under a user-sorted
     *        category. Keep this function physically adjacent to
     *        objectNameAt so the two cannot drift.
     * @param[in] c Spatial category.
     * @param[in] row Zero-based display row within this category, not global engine index.
     * @return Engine/source-array index honoring display permutation, minus one for invalid
     *   row/category; indices can renumber after deletion.
     */
    [[nodiscard]] int soaIndexAt(Category c, int row) const;

    /*!
     * \brief Slice BM.0 — number of non-spatial data objects in
     *        \p c. Returns 0 when the engine handle is null or when
     *        the underlying `swmm_<type>_count` call fails.
     *
     *        Curves and Time Series share the engine's unified
     *        `tables` array, so their counts are produced by walking
     *        the table list and filtering on `swmm_table_get_type`.
     *        All other categories map directly to a dedicated count
     *        accessor.
     * @param[in] c Non-spatial data category.
     * @return Count from live bound registry when present, otherwise engine table/group counts;
     *   controls/hydrographs remain engine-sourced. Invalid category/no engine gives0. Registry
     *   staging can differ from engine until flush.
     */
    [[nodiscard]] int dataObjectCount(DataCategory c) const;

    /*!
     * \brief Slice BM.0 — name of a non-spatial data object at
     *        (category, row), or an empty string when out of range.
     * @param[in] c Non-spatial data category.
     * @param[in] row Zero-based category row, not necessarily unified engine table index.
     * @return Name from the same registry/engine source as dataObjectCount, empty for invalid
     *   category/row/no engine. Control parser BADPARAM yields localized Rule N [unnamed]; engine
     *   control/hydrograph IDs use128-byte buffers.
     */
    [[nodiscard]] QString dataObjectNameAt(DataCategory c, int row) const;

    /*!
     * \brief Slice DA.3 — suggest a unique default name for the next
     *        new data object of \p c. Scans existing names and returns
     *        the smallest positive integer \p n such that
     *        `<Prefix><n>` is not already in use (case-insensitive).
     *
     *        Prefix per category:
     *          Curves → "Curve",  TimeSeries → "TS",  Patterns → "Pattern",
     *          LIDControls → "LID",  Pollutants → "Pollut",
     *          LandUses → "LandUse",  Aquifers → "Aquifer",
     *          Snowpacks → "Snowpack",  Controls → "Rule",
     *          Transects → "Transect",  Hydrographs → "UH",
     *          Streets → "Street",  Inlets → "Inlet".
     *
     *        Modern affordance — legacy SWMM-GUI requires manual
     *        naming.  Caller may overwrite in the New Data Object
     *        dialog.
     * @param[in] c Non-spatial data category supplying prefix and namespace.
     * @return Smallest positive prefixed integer unused case-insensitively in current category;
     *   unknown category uses Object prefix. Does not reserve or create name, so later changes can
     *   introduce a collision.
     */
    [[nodiscard]] QString suggestUniqueDataObjectName(DataCategory c) const;

    /*!
     * \brief List the engine's table ids whose type matches `tableType`
     *        (Slice DA.4.3).
     *
     * \details Tables (time series + all 12 curve kinds) share one engine
     *          array, partitioned by `swmm_table_get_type`. Pickers need
     *          a filtered list — tidal curves only, time series only, etc.
     *          Centralised here so the pattern is reused across the new
     *          `DataObjectPickerEditor` and the existing
     *          `NodeCompoundEditDialog:: populateTimeSeriesCombo` (which
     *          may be refactored to delegate here in a follow-up).
     *
     *          `tableType` codes (from openswmm_tables.h):
     *            0 = TIMESERIES
     *            1 = CURVE_STORAGE   2 = CURVE_DIVERSION  3 = CURVE_RATING
     *            4 = CURVE_SHAPE     5 = CURVE_CONTROL    6 = CURVE_TIDAL
     *            7..11 = CURVE_PUMP1..PUMP5
     *
     *          A `tableType` of -1 means "any non-timeseries table"
     *          (i.e. every curve kind).
     * @param[in] tableType Unified engine table-type code; any negative value selects all
     *   non-timeseries types.
     * @return Owned nonempty matching IDs in engine table order, empty without engine. Failed type
     *   reads skipped; staged registry-only edits are not read by this engine-only helper.
     */
    [[nodiscard]] QStringList tableIdsOfType(int tableType) const;
    /*! Table ids whose engine type is one of \p tableTypes, in table order.
     * @param[in] tableTypes Exact unified engine table-type codes; empty selects none, negative
     *   values have no wildcard interpretation here.
     * @return Owned nonempty matching IDs in engine table order, each once; no-engine/empty set
     *   yields empty. Failed reads skipped; staged registry-only changes excluded.
     */
    [[nodiscard]] QStringList tableIdsOfTypes(const QList<int> &tableTypes) const;

    /*!
     * \brief Create a new data object on the engine (DB.4b).
     *
     * \details Moves the per-DataCategory engine-commit switch out of
     *          `ObjectBrowserPanel:: addNewDataObject` so callers
     *          OTHER than the Object Browser (e.g. the picker
     *          buttons in NodeCompoundEditDialog) can create new TS /
     *          patterns / UH groups inline without dispatching upward.
     *
     *          The options map matches the keys produced by
     *          `NewDataObjectDialog:: getNew(...)` — e.g. "patternType",
     *          "curveType", "lidType", "units", "inletType", "skeleton",
     *          "rainGage", "response".
     *
     *          On success this method emits dataObjectsChanged and updates any live
     *          registry mirror; browser selection remains the caller's responsibility.
     *
     * \param c        Data-object category to create.
     * \param name     Name for the new object (caller-supplied, typically
     *                 from `suggestUniqueDataObjectName`).
     * \param options  Per-category creation options (see above).
     * \param outError If non-null, populated with an error message on
     *                 failure (empty on success).
     * \returns true on success, false otherwise.
     * @note Requires attached engine/nonblank trimmed name, but passes original untrimmed UTF-8
     *   name to engine. Missing option defaults: storage curve, monthly pattern, bio-cell LID, mg/L
     *   pollutant, GRATE inlet, empty rule, hydrograph response0. Hydrograph optional gage add
     *   return is ignored, so failure can leave a partial gage assignment; no transaction/undo.
     * @note Success dirties table partition for curves/timeseries, mirrors any live bound registry
     *   using additive load, emits hydrographChanged for hydrographs and always dataObjectsChanged.
     *   Does not itself mark modelEdited or select browser row; registry paths can emit further
     *   changes. Error string clears on success.
     */
    [[nodiscard]] bool createDataObject(DataCategory c, const QString &name,
                                        const QVariantMap &options, QString *outError = nullptr);

    /*!
     * \brief Aggregate check state for a category: Checked when every
     *        member is visible, Unchecked when every member is hidden,
     *        PartiallyChecked otherwise. O(1) — derived from the
     *        per-category hidden-count counter that setObjectVisibleAt /
     *        setCategoryVisible / setObjectVisible all maintain.
     * @param[in] c Spatial category; unknown/empty category is treated checked.
     * @return Checked for zero members/hidden count 0, Unchecked if all hidden, PartiallyChecked
     *   otherwise. Uses cached aggregate counter rather than rescanning typed masks; names shared
     *   across kinds can make legacy untyped bookkeeping less precise.
     */
    [[nodiscard]] Qt::CheckState categoryCheckState(Category c) const;

    /*!
     * \brief Toggle visibility for a single leaf referenced by
     *        (category, row). Updates m_hiddenObjects plus the
     *        category hidden count and emits repaintRequested() on change.
     *        Preferred over setObjectVisible(name, …) when the caller
     *        already knows the category — avoids the name→(cat, row)
     *        lookup on the hot path of QTreeView:: setData().
     * @param[in] c Spatial category establishing typed namespace.
     * @param[in] row Zero-based display row honoring custom order.
     * @param[in] visible Requested visibility for this kind bit only.
     * @note Invalid/empty row no-op. Maintains aggregate hidden names, typed mask and category
     *   counter; changed state rebuilds flags/marks scene rebuild/emits one repaint. Same-named
     *   other kinds retain their bit. Does not change global category gate or engine data.
     */
    void setObjectVisibleAt(Category c, int row, bool visible);

    /*!
     * \brief Bulk-toggle every member of a category. Used by the
     *        group-header checkbox in the Object Browser; emits a single
     *        repaintRequested() regardless of how many objects the
     *        category contains.
     * @param[in] c Spatial category, all current display rows; empty category no-op.
     * @param[in] visible Requested typed visibility applied to every member.
     * @note Sets cached category hidden count directly to0/total, rebuilds flags/scene and emits
     *   one repaint only if masks changed. Other namespace bits sharing names are retained.
     *   Category headers remain aggregates rather than independent visibility gates.
     */
    void setCategoryVisible(Category c, bool visible);

    /*!
     * \brief Per-kind (sub-layer) opacity in [0, 1], applied on top of the
     *        layer opacity. Lets the user fade, say, Conduits independently
     *        of Junctions via the layer-tree Opacity column. Default 1.0.
     *        The painter multiplies each feature's alpha by this; setting it
     *        flags a rebuild + repaint.
     * @param[in] c Spatial category.
     * @return Stored kind alpha0..1, initially 1; invalid category returns1. Multiplies overall
     *   layer opacity during paint.
     */
    [[nodiscard]] qreal categoryOpacity(Category c) const;
    /**
     * @param[in] c Spatial category; invalid ignored.
     * @param[in] opacity Kind alpha clamped0..1; caller should supply finite value.
     * @note Fuzzy-equal normalized value no-op; change marks feature-colour/scene rebuild and emits
     *   repaintRequested. Does not mutate engine values or notify a dedicated property signal.
     */
    void setCategoryOpacity(Category c, qreal opacity);

    /*!
     * \brief Locate an object by name, returning true and writing
     *        \p cat + \p row on success. O(1) hash lookup against the
     *        `m_objectLocation` map built at cache time. Used by the
     *        SelectionManager → tree model bridge to convert
     *        SWMMObjectRef into a QModelIndex without rescanning the SoA.
     * @param[in] name Exact single-keyed object name, potentially shared across namespaces.
     * @param[out] cat Optional category destination assigned only on success.
     * @param[out] row Optional current category display row assigned only on success.
     * @return False leaves outputs unchanged if name missing; true returns cached name-map winner.
     *   Node beats colliding link initially, then subcatch/gage overwrite earlier namespaces; later
     *   reorder can overwrite winner. Typed callers should use category/SoA APIs to avoid
     *   ambiguity.
     */
    bool findObjectLocation(const QString &name, Category *cat, int *row) const;

    // ----- Category ordering (Slice T.2) ----------------------------------

    /*!
     * \brief User-configurable display order of categories in the
     *        Object Browser. Defaults to the enum sequence at load
     *        time; Slice T.2's drag-and-drop rewrites the vector and
     *        the tree model re-renders in the new order.
     *
     *        Only the display order changes — `categoryCount()` /
     *        `objectNameAt()` still take the enum category so all
     *        existing call sites keep working.
     * @return Owned current display-only category permutation, normally enum order after
     *   geometry-cache initialization; constructor can leave it empty until first build. Does not
     *   reorder engine/source arrays.
     */
    [[nodiscard]] QVector<Category> categoryOrder() const;

    /*! Replace the full category order. Rejects an input that doesn't
     *  cover every enum value exactly once (safety net so a malformed
     *  vector can't silently drop categories). Emits `categoryOrderChanged`
     *  on success; Object Browser reacts via `modelReset`.
     * @param[in] order Full permutation of every valid spatial category exactly once.
     * @note Invalid/equal input ignored. Accepted changed order emits categoryOrderChanged without
     *   repaint, engine edits or row-array reorder; browser/model observers rebuild their own
     *   views.
     */
    void setCategoryOrder(const QVector<Category> &order);

    // ----- Intra-category object ordering (Slice T.3) ---------------------

    /*!
     * \brief Install a user-defined display order for the given
     *        category. `soaIndices` is a permutation of the underlying
     *        SoA indices for the category (m_nodes for CatJunctions /
     *        Outfalls / Storage / Dividers; m_links for CatConduits /
     *        Pumps / Orifices / Weirs / Outlets; direct m_catchments /
     *        m_gages for the area categories). Size and membership must
     *        match exactly, otherwise the call is rejected (defensive:
     *        a malformed vector would silently drop or duplicate
     *        objects).
     *
     *        After a successful call, `categoryCount()` is unchanged
     *        but `objectNameAt(cat, row)` follows the override.
     *        `m_objectLocation` is rewritten for this category so
     *        `findObjectLocation()` returns the new display row.
     *        Emits `categoryOrderChanged()` — the tree model resets
     *        and re-renders in the new order.
     * @param[in] cat Spatial category whose display order changes.
     * @param[in] soaIndices Full same-size permutation of default category source/engine indices;
     *   rows are not indices within category.
     * @note Rejects wrong size/membership/duplicates. Accepted order stores even when equal,
     *   rewrites category name-map display rows and emits categoryOrderChanged; no repaint or
     *   engine reorder. Global name collisions can change name-map winner. Overrides are cleared by
     *   rebuildCategoryIndex().
     */
    void setObjectOrder(Category cat, const QVector<int> &soaIndices);

    /*! Drop the user override for this category; display falls back
     *  to the default per-category index bucket.
     * @param[in] cat Spatial category whose override is removed.
     * @note Missing override no-op; removal restores default name-map display rows and emits
     *   categoryOrderChanged, without engine reorder/repaint.
     */
    void clearObjectOrder(Category cat);

    /*! Read-only access to the current override for a category. Empty
     *  when no override is installed.
     * @param[in] cat Spatial category.
     * @return Owned installed display-row-to-source-index override, empty if none. Source indices
     *   can change after topology edits/reload; never persist without revalidation.
     */
    [[nodiscard]] QVector<int> objectOrder(Category cat) const;

    /*! Default SoA permutation for a category (visible-row → SoA
     *  index) when NO user override is installed. Used by the tree
     *  model's drag-drop handler as the starting point for a drag
     *  on a category that hasn't been reordered before.
     * @param[in] cat Spatial category.
     * @return Owned native membership index permutation, independent of override; invalid category
     *   empty. Source indices can change after topology edits/reload; never persist without
     *   revalidation.
     */
    [[nodiscard]] QVector<int> defaultObjectOrder(Category cat) const;

    // ----- Symbology ------------------------------------------------------

    /**
     * @return Value copy of Junction legacy symbol: marker/line sizes, widths and arrow dimensions
     *   in screen pixels, alpha/labels/glyph configuration. Renderer-derived per-feature overrides
     *   and global preferences may independently influence paint.
     */
    [[nodiscard]] SWMMElementSymbol junctionSymbol() const;
    /**
     * @param[in] s Full symbol value copied; caller maintains sensible finite nonnegative pixel
     *   geometry and valid colours.
     * @note Copies all fields without equality/range checks, resynchronizes persistent editor
     *   adapter and requests repaint. Marks scene rebuild, syncs SingleSymbol renderer in place and
     *   marks lazy rule list dirty. Classified renderers retain their own styles. Engine unchanged.
     */
    void setJunctionSymbol(const SWMMElementSymbol &s);

    /**
     * @return Value copy of Outfall legacy symbol: marker/line sizes, widths and arrow dimensions
     *   in screen pixels, alpha/labels/glyph configuration. Renderer-derived per-feature overrides
     *   and global preferences may independently influence paint.
     */
    [[nodiscard]] SWMMElementSymbol outfallSymbol() const;
    /**
     * @param[in] s Full symbol value copied; caller maintains sensible finite nonnegative pixel
     *   geometry and valid colours.
     * @note Copies all fields without equality/range checks, resynchronizes persistent editor
     *   adapter and requests repaint. Marks scene rebuild, syncs SingleSymbol renderer in place and
     *   marks lazy rule list dirty. Classified renderers retain their own styles. Engine unchanged.
     */
    void setOutfallSymbol(const SWMMElementSymbol &s);

    /**
     * @return Value copy of Storage legacy symbol: marker/line sizes, widths and arrow dimensions
     *   in screen pixels, alpha/labels/glyph configuration. Renderer-derived per-feature overrides
     *   and global preferences may independently influence paint.
     */
    [[nodiscard]] SWMMElementSymbol storageSymbol() const;
    /**
     * @param[in] s Full symbol value copied; caller maintains sensible finite nonnegative pixel
     *   geometry and valid colours.
     * @note Copies all fields without equality/range checks, resynchronizes persistent editor
     *   adapter and requests repaint. Marks scene rebuild, syncs SingleSymbol renderer in place and
     *   marks lazy rule list dirty. Classified renderers retain their own styles. Engine unchanged.
     */
    void setStorageSymbol(const SWMMElementSymbol &s);

    /**
     * @return Value copy of Divider legacy symbol: marker/line sizes, widths and arrow dimensions
     *   in screen pixels, alpha/labels/glyph configuration. Renderer-derived per-feature overrides
     *   and global preferences may independently influence paint.
     */
    [[nodiscard]] SWMMElementSymbol dividerSymbol() const;
    /**
     * @param[in] s Full symbol value copied; caller maintains sensible finite nonnegative pixel
     *   geometry and valid colours.
     * @note Copies all fields without equality/range checks, resynchronizes persistent editor
     *   adapter and requests repaint. Marks scene rebuild, syncs SingleSymbol renderer in place and
     *   marks lazy rule list dirty. Classified renderers retain their own styles. Engine unchanged.
     */
    void setDividerSymbol(const SWMMElementSymbol &s);

    /**
     * @return Value copy of VirtualJunction legacy symbol: marker/line sizes, widths and arrow
     *   dimensions in screen pixels, alpha/labels/glyph configuration. Renderer-derived per-feature
     *   overrides and global preferences may independently influence paint.
     */
    [[nodiscard]] SWMMElementSymbol virtualJunctionSymbol() const;
    /**
     * @param[in] s Full symbol value copied; caller maintains sensible finite nonnegative pixel
     *   geometry and valid colours.
     * @note Copies all fields without equality/range checks, resynchronizes persistent editor
     *   adapter and requests repaint. Marks scene rebuild but does not sync CatJunctions renderer,
     *   preserving regular junction style. Engine unchanged.
     */
    void setVirtualJunctionSymbol(const SWMMElementSymbol &s);

    /*! Inlet junctions — same CatJunctions bucket as virtual junctions
     *  (D-G1), distinct glyph.
     * @return Value copy of InletJunction legacy symbol: marker/line sizes, widths and arrow
     *   dimensions in screen pixels, alpha/labels/glyph configuration. Renderer-derived per-feature
     *   overrides and global preferences may independently influence paint.
     */
    [[nodiscard]] SWMMElementSymbol inletJunctionSymbol() const;
    /**
     * @param[in] s Full symbol value copied; caller maintains sensible finite nonnegative pixel
     *   geometry and valid colours.
     * @note Copies all fields without equality/range checks, resynchronizes persistent editor
     *   adapter and requests repaint. Marks scene rebuild but does not sync CatJunctions renderer,
     *   preserving regular junction style. Engine unchanged.
     */
    void setInletJunctionSymbol(const SWMMElementSymbol &s);

    /*! Dashed host → capture-node connector overlay (§3.2). Category-less:
     *  it draws no features of its own, only the relation lines.
     * @return Value copy of InletConnector legacy symbol: marker/line sizes, widths and arrow
     *   dimensions in screen pixels, alpha/labels/glyph configuration. Renderer-derived per-feature
     *   overrides and global preferences may independently influence paint.
     */
    [[nodiscard]] SWMMElementSymbol inletConnectorSymbol() const;
    /**
     * @param[in] s Full symbol value copied; caller maintains sensible finite nonnegative pixel
     *   geometry and valid colours.
     * @note Copies all fields without equality/range checks, resynchronizes persistent editor
     *   adapter and requests repaint. No category/per-kind renderer or scene-rebuild flag applies
     *   to this relation-only overlay. Engine unchanged.
     */
    void setInletConnectorSymbol(const SWMMElementSymbol &s);

    /**
     * @return Value copy of Conduit legacy symbol: marker/line sizes, widths and arrow dimensions
     *   in screen pixels, alpha/labels/glyph configuration. Renderer-derived per-feature overrides
     *   and global preferences may independently influence paint.
     */
    [[nodiscard]] SWMMElementSymbol conduitSymbol() const;
    /**
     * @param[in] s Full symbol value copied; caller maintains sensible finite nonnegative pixel
     *   geometry and valid colours.
     * @note Copies all fields without equality/range checks, resynchronizes persistent editor
     *   adapter and requests repaint. Marks scene rebuild, syncs SingleSymbol renderer in place and
     *   marks lazy rule list dirty. Classified renderers retain their own styles. Engine unchanged.
     */
    void setConduitSymbol(const SWMMElementSymbol &s);

    /**
     * @return Value copy of Pump legacy symbol: marker/line sizes, widths and arrow dimensions in
     *   screen pixels, alpha/labels/glyph configuration. Renderer-derived per-feature overrides and
     *   global preferences may independently influence paint.
     */
    [[nodiscard]] SWMMElementSymbol pumpSymbol() const;
    /**
     * @param[in] s Full symbol value copied; caller maintains sensible finite nonnegative pixel
     *   geometry and valid colours.
     * @note Copies all fields without equality/range checks, resynchronizes persistent editor
     *   adapter and requests repaint. Marks scene rebuild, syncs SingleSymbol renderer in place and
     *   marks lazy rule list dirty. Classified renderers retain their own styles. Engine unchanged.
     */
    void setPumpSymbol(const SWMMElementSymbol &s);

    /**
     * @return Value copy of Orifice legacy symbol: marker/line sizes, widths and arrow dimensions
     *   in screen pixels, alpha/labels/glyph configuration. Renderer-derived per-feature overrides
     *   and global preferences may independently influence paint.
     */
    [[nodiscard]] SWMMElementSymbol orificeSymbol() const;
    /**
     * @param[in] s Full symbol value copied; caller maintains sensible finite nonnegative pixel
     *   geometry and valid colours.
     * @note Copies all fields without equality/range checks, resynchronizes persistent editor
     *   adapter and requests repaint. Marks scene rebuild, syncs SingleSymbol renderer in place and
     *   marks lazy rule list dirty. Classified renderers retain their own styles. Engine unchanged.
     */
    void setOrificeSymbol(const SWMMElementSymbol &s);

    /**
     * @return Value copy of Weir legacy symbol: marker/line sizes, widths and arrow dimensions in
     *   screen pixels, alpha/labels/glyph configuration. Renderer-derived per-feature overrides and
     *   global preferences may independently influence paint.
     */
    [[nodiscard]] SWMMElementSymbol weirSymbol() const;
    /**
     * @param[in] s Full symbol value copied; caller maintains sensible finite nonnegative pixel
     *   geometry and valid colours.
     * @note Copies all fields without equality/range checks, resynchronizes persistent editor
     *   adapter and requests repaint. Marks scene rebuild, syncs SingleSymbol renderer in place and
     *   marks lazy rule list dirty. Classified renderers retain their own styles. Engine unchanged.
     */
    void setWeirSymbol(const SWMMElementSymbol &s);

    /*! Outlets now have a real symbol channel (previously aliased the
     *  conduit symbol). NOTE: the link PEN for outlets still comes from
     *  the preferences "outlet" pen (paint parity decision — see
     *  linkPenForType in swmmlayeritem.cpp); this struct drives flow
     *  arrows, labels, renderer seeding and persistence.
     * @return Value copy of Outlet legacy symbol: marker/line sizes, widths and arrow dimensions in
     *   screen pixels, alpha/labels/glyph configuration. Renderer-derived per-feature overrides and
     *   global preferences may independently influence paint.
     */
    [[nodiscard]] SWMMElementSymbol outletSymbol() const;
    /**
     * @param[in] s Full symbol value copied; caller maintains sensible finite nonnegative pixel
     *   geometry and valid colours.
     * @note Copies all fields without equality/range checks, resynchronizes persistent editor
     *   adapter and requests repaint. Marks scene rebuild, syncs SingleSymbol renderer in place and
     *   marks lazy rule list dirty. Classified renderers retain their own styles. Engine unchanged.
     */
    void setOutletSymbol(const SWMMElementSymbol &s);

    /**
     * @return Value copy of Subcatchment legacy symbol: marker/line sizes, widths and arrow
     *   dimensions in screen pixels, alpha/labels/glyph configuration. Renderer-derived per-feature
     *   overrides and global preferences may independently influence paint.
     */
    [[nodiscard]] SWMMElementSymbol subcatchmentSymbol() const;
    /**
     * @param[in] s Full symbol value copied; caller maintains sensible finite nonnegative pixel
     *   geometry and valid colours.
     * @note Copies all fields without equality/range checks, resynchronizes persistent editor
     *   adapter and requests repaint. Marks scene rebuild, syncs SingleSymbol renderer in place and
     *   marks lazy rule list dirty. Classified renderers retain their own styles. Engine unchanged.
     */
    void setSubcatchmentSymbol(const SWMMElementSymbol &s);

    /**
     * @return Value copy of RainGage legacy symbol: marker/line sizes, widths and arrow dimensions
     *   in screen pixels, alpha/labels/glyph configuration. Renderer-derived per-feature overrides
     *   and global preferences may independently influence paint.
     */
    [[nodiscard]] SWMMElementSymbol rainGageSymbol() const;
    /**
     * @param[in] s Full symbol value copied; caller maintains sensible finite nonnegative pixel
     *   geometry and valid colours.
     * @note Copies all fields without equality/range checks, resynchronizes persistent editor
     *   adapter and requests repaint. Marks scene rebuild, syncs SingleSymbol renderer in place and
     *   marks lazy rule list dirty. Classified renderers retain their own styles. Engine unchanged.
     */
    void setRainGageSymbol(const SWMMElementSymbol &s);

    /*! Slice U-4 — expose the 12 per-kind SWMMElementSymbol adapters as
     *  styleable subjects for the unified LayerStyleDialog.
     *
     *  Adapter-ownership refactor (LAYER_STYLING_LABELING_PLAN follow-up):
     *  the subject wrappers are fresh per call (cheap, non-owning), but the
     *  underlying SwmmElementSymbolAdapter QObjects are the layer's
     *  PERSISTENT set from elementSymbolAdapter() — one instance per kind
     *  for the layer's lifetime. Every UI surface (dialog subjects,
     *  SingleSymbolPanel, kind tree) edits the same adapter, which is what
     *  makes the dialog's Cancel snapshot/rollback authoritative and stops
     *  the per-open adapter leak. */
    /**
     * @return Owned fourteen editor wrappers borrowing persistent layer-owned per-kind adapters,
     *   grouped nodes/links/areas/other. Wrappers must not outlive layer; construction may lazily
     *   allocate/resync adapters and notify mounted editors.
     */
    [[nodiscard]] std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
    styleSubjects() override;

    /*!
     * \brief The layer's persistent per-kind symbol adapter for a subject
     *        routing id ("model.junctions" … "model.virtualjunctions").
     *        Lazily constructed on first request, parented to the layer,
     *        resynced from the live struct on every fetch. Returns nullptr
     *        for unknown ids.
     * @param[in] routingId Exact model.* routing key for fourteen symbol channels, including
     *   virtual/inlet/connector overrides.
     * @return Borrowed persistent layer QObject child, lazily created and resynced from current
     *   symbol on every fetch; null for unknown key. Adapter writes route through layer setter,
     *   updating all mounted views; do not delete or retain beyond layer lifetime.
     */
    [[nodiscard]] SwmmElementSymbolAdapter *elementSymbolAdapter(const QString &routingId);

    /*! Data-edit epoch — bumped on every modelEdited / attributeChanged.
     *  Consumers (label text cache) compare against their stored value to
     *  detect stale derived data.
     * @return Immediately incremented epoch for each modelEdited/attributeChanged emission,
     *   including events coalesced into one later derived-style refresh. Not a count of undo
     *   commands or geometry-only rebuilds.
     */
    [[nodiscard]] quint64 editRevision() const { return m_editRevision; }

    // ----- Renderer (Slice BI Phase 8.13.6.5) -----------------------------
    // The renderer is the §J.2 seam every future paint path will go through.
    // Sub-phase 8.13.6.5 is API plumbing only — the existing paint loop in
    // SWMMLayerItem still reads the per-kind SWMMElementSymbol members
    // (m_junctionSym, m_conduitSym, …) directly. The paint refactor lands
    // later (after Slice BB Phase 8.6.1 provides a proper ColorRamp type),
    // at which point the default renderer will be swapped from this
    // placeholder SingleSymbolRenderer to a MultiKindRenderer adapter that
    // delegates to the 11 per-category symbols.

    /*!
     * \brief The IFeatureRenderer for this layer.
     * \details Constructed eagerly so callers never have to null-check.
     *          v1 default is a SingleSymbolRenderer placeholder; the
     *          eventual default (paint-refactor sub-phase) is a
     *          MultiKindRenderer wrapping the 11 per-category symbols.
     * @return Borrowed nonnull owned placeholder renderer; invalidated by setRenderer/destruction.
     *   Current multi-kind feature styling uses kindRenderer slots rather than this pointer.
     */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *renderer() const override;

    /*!
     * \brief Replaces the current renderer.
     * \details The layer takes ownership.  A null pointer is rejected
     *          (silent no-op) so renderer() never returns nullptr.
     *          Emits \ref rendererChanged() when the pointer actually changes.
     * @param[in] r Exclusive nonnull renderer ownership, moved on change; null ignored.
     * @note Emits rendererChanged only, with no explicit repaint/kind-style/rule synchronization.
     *   Never reconstruct ownership from already owned raw pointer.
     */
    void setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r) override;

    // ----- Per-kind renderer (Slice BI-MK.1 / BI-MK.LT, 2026-05-24) --------
    //
    // Stores 11 per-kind IFeatureRenderer entries, indexed by Category, so
    // SymbologyDialog's left-pane kind picker and LayerTreePanel's sub-row
    // right-click menu can drive per-kind styling independently. Single-
    // symbol renderers double-bind to the legacy `m_*Sym` SWMMElementSymbol
    // fields (write-through both ways) so the current bucketed paint code
    // in SWMMLayerItem keeps working without a full per-feature symbolFor()
    // refactor. Graduated/Categorized renderers are stored + drive legends
    // / persistence; canvas painting from them is deferred to the full
    // Phase 8.13.6.4 paint-loop refactor.

    /*! Returns the renderer for one Category kind (never null).
     * @param[in] c Spatial category, bounds checked against current renderer slots.
     * @return Borrowed owned renderer or null for invalid/uninitialized slot. Constructor seeds
     *   valid categories; replacement/reset/destruction invalidates pointers.
     */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *kindRenderer(Category c) const;

    /*! Replaces the per-kind renderer for \p c. Takes ownership; null is
     *  rejected (silent no-op). When the new renderer is a SingleSymbol,
     *  the layer extracts colour/outline/size back to the legacy
     *  `SWMMElementSymbol` field for that kind so the existing paint
     *  loop reflects the change. Emits rendererChanged + repaintRequested.
     * @param[in] c Spatial category; invalid rejects incoming ownership.
     * @param[in] r Fresh exclusive nonnull renderer moved into kind slot; null ignored.
     * @note SingleSymbol supported fields write back through corresponding legacy symbol
     *   setter/adapter. Replaces old renderer, marks rule-list stale, synchronously rebuilds
     *   per-feature colour/size/shape/offset caches and emits rendererChanged/repaintRequested.
     *   Symbol writeback can emit additional repaint. No engine data edits; caller must not wrap
     *   already owned raw pointer.
     */
    void setKindRenderer(Category c, std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r);

    /*! Re-seed the per-kind renderer for \p c from the matching
     *  current SWMMElementSymbol (except the outlet factory seed). Equivalent to "Reset
     *  Kind to Defaults" in the layer-tree context menu.
     * @param[in] c Valid spatial category; invalid ignored.
     * @note Replaces renderer with SingleSymbol seeded from CURRENT legacy symbol, except outlets
     *   use hardcoded default. Emits rendererChanged/repaintRequested; does not call
     *   setKindRenderer(), rebuild override caches, mark rule list dirty or reset stored symbol
     *   itself. Historical factory-reset name does not imply all style fields reset.
     */
    void resetKindRendererToDefaults(Category c);

    /*! Convenience: stable string key used for MultiKindRenderer keying
     *  + .oswp persistence. e.g. CatJunctions → "Junctions".
     * @param[in] c Spatial category.
     * @return Stable nonlocalized persistence/rendering key, empty for invalid category. No
     *   existence or visibility check.
     */
    [[nodiscard]] static QString kindKey(Category c);

    // ----- X4: legend-as-editor facade ------------------------------------
    // The model layer is multi-kind, so it has no single IFeatureRenderer for
    // the legend dock to drive. These mirror the IFeatureRenderer class-edit
    // contract but aggregate across the 11 per-kind renderers: legend rows
    // carry a kind-qualified classKey ("Junctions<inner>"), and the
    // colour/size accessors decode the kind and delegate to the matching
    // kindRenderer() — i.e. edits land on the very objects the painter reads
    // (single source of truth). Mutating setters rebuild that kind's
    // per-feature override cache and request a repaint.
    /**
     * @return Owned rows from nonempty-category renderers with classKey kindKey + unit-separator +
     *   inner key. Includes one LID leaf-badge row if any engine node has LID. Does not filter
     *   hidden kinds/objects, and user labels are prefixed with stable kind key.
     */
    [[nodiscard]] QList<OpenSWMM::Render::LegendSymbolItem> legendSymbolItems() const;
    /**
     * @param[in] kind Requested renderer class-edit operation.
     * @return True if ANY kind renderer supports operation, including empty categories; individual
     *   qualified class keys may still reject it.
     */
    [[nodiscard]] bool supportsClassEdit(OpenSWMM::Render::ClassEditKind kind) const;
    /**
     * @param[in] classKey Exact legend-qualified key with U+001F separator and known kind key.
     * @return Renderer class colour or invalid QColor for malformed/unknown/unsupported key. No
     *   cache refresh or ownership transfer.
     */
    [[nodiscard]] QColor colorForClass(const QString &classKey) const;
    /**
     * @param[in] classKey Exact kind-qualified legend class key.
     * @param[in] color Colour forwarded to cloned renderer; renderer decides
     *   validity/unsupported-class behavior.
     * @note Malformed key/missing renderer no-op. Otherwise clone/mutate/reinstall through
     *   setKindRenderer, rebuilding overrides and notifying even if inner edit is unsupported/no
     *   change. Does not mutate existing renderer in place.
     */
    void setColorForClass(const QString &classKey, const QColor &color);
    /**
     * @param[in] classKey Exact kind-qualified legend class key.
     * @return Renderer class marker/line size in pixels, or negative sentinel for
     *   malformed/unknown/unsupported key.
     */
    [[nodiscard]] qreal sizeForClass(const QString &classKey) const;
    /**
     * @param[in] classKey Exact kind-qualified legend class key.
     * @param[in] size Requested absolute pixel size forwarded to renderer; no facade-level clamp.
     * @note Valid decoded kind clones/mutates/reinstalls renderer through setKindRenderer with its
     *   cache/signal side effects, even when inner key rejects edit. No engine edit/undo command
     *   here.
     */
    void setSizeForClass(const QString &classKey, qreal size);

    // ----- Rule Model (Slice B.4, Phase B) --------------------------------
    //
    // 11 kindRenderer slots → 11 Rules, one per Category. The RuleList
    // is lazy-built on first access; each Rule holds a CLONE of the
    // matching kindRenderer at construction time. Rule-side renderer
    // swaps (via Z.3b's setRendererById) propagate back to the layer
    // via setKindRenderer, which keeps the legacy paint-time + override
    // cache code paths intact.
    //
    // External kind renderer replacement marks the mirror stale. The next
    // ruleList access rebuilds it and invalidates previous borrowed rules.
    /**
     * @return Borrowed lazy owned eleven-rule mirror cloned from current kind renderers. Dirty
     *   access replaces/destroys entire previous list and invalidates all borrowed Rule pointers,
     *   including via const access; do not hold across other style changes/ruleList calls. GUI
     *   thread only.
     */
    [[nodiscard]] OpenSWMM::Render::RuleList *ruleList() override;
    /**
     * @return Borrowed lazy owned eleven-rule mirror cloned from current kind renderers. Dirty
     *   access replaces/destroys entire previous list and invalidates all borrowed Rule pointers,
     *   including via const access; do not hold across other style changes/ruleList calls. GUI
     *   thread only.
     */
    [[nodiscard]] const OpenSWMM::Render::RuleList *ruleList() const override;

    // ----- IAttributeProvider (Slice DM.3) --------------------------------
    //
    // Returns the per-kind static engine fields a user can theme by
    // (length, slope, diameter, invertElev, area, impervPct, …). All
    // entries are isDynamic=false (statics don't change per animation
    // frame). Drives the attribute combo in the Graduated / Categorized
    // renderer panels — see RENDERING_DIALOG_DEMO_PLAN.md §2.
    /**
     * @param[in] cat Spatial category selecting static built-in descriptors; unknown/rain-gage has
     *   no built-in fields.
     * @return Owned static field descriptors plus every typed uf: user-flag definition. Built-in
     *   display/unit text advertises metres/ha/percent; numeric identify values follow engine
     *   getter conventions without conversion by this descriptor method. Const call can lazily
     *   create/load UserFlagsModel QObject, so GUI thread only.
     */
    [[nodiscard]] QVector<OpenSWMM::Render::AttributeField>
    availableAttributes(OpenSWMMVis::SwmmCategory cat) const override;

    // ----- Flow-direction arrows (Slice BI Phase 8.13.8-mini, 2026-05-24) -
    //
    // Per-link-kind toggle for arrowheads drawn at the polyline midpoint
    // pointing upstream → downstream. Wraps the showArrows / arrowSize /
    // arrowColor / arrowOnlyWhenFlowPos fields on the corresponding
    // SWMMElementSymbol so the painter can drive arrows from a single
    // place. `c` must be a link kind (Conduits / Pumps / Orifices /
    // Weirs / Outlets); other categories silently no-op the setter and
    // return false from the getter.

    /*! True if flow-direction arrows are enabled for the link kind.
     * @param[in] c Link spatial category: conduit, pump, orifice, weir, outlet.
     * @return Stored arrows flag, false for non-link kind. Does not test current layer/object
     *   visibility or flow.
     */
    [[nodiscard]] bool linkArrowsEnabled(Category c) const;

    /*! Toggle flow-direction arrows for a link kind. Emits repaintRequested.
     * @param[in] c Link spatial category; non-link kind ignored.
     * @param[in] enabled Requested visibility of authored-direction midpoint arrow.
     * @note Equality no-op; changes corresponding symbol field, resyncs persistent adapter and
     *   requests repaint. Does not synchronize renderer/rule list or mark geometry rebuild/model
     *   edited.
     */
    void setLinkArrowsEnabled(Category c, bool enabled);

    // Slice FX.1 — per-kind arrow size / colour / flow-positive filter.
    // Mirror the linkArrowsEnabled getter+setter shape; setters emit
    // repaintRequested when the underlying field actually changes.
    /**
     * @param[in] c Link spatial category: conduit, pump, orifice, weir, outlet.
     * @return Stored arrowhead length pixels, 10 for non-link kind; no clamping. Does not test
     *   current layer/object visibility or flow.
     */
    [[nodiscard]] double linkArrowSize(Category c) const;
    /**
     * @param[in] c Link spatial category; non-link kind ignored.
     * @param[in] pixels Raw arrowhead length in screen pixels; no finite/range clamp.
     * @note Equality no-op; changes corresponding symbol field, resyncs persistent adapter and
     *   requests repaint. Does not synchronize renderer/rule list or mark geometry rebuild/model
     *   edited.
     */
    void setLinkArrowSize(Category c, double pixels);
    /*! Arrowhead width ACROSS the link, independent of its length.
     * @param[in] c Link spatial category: conduit, pump, orifice, weir, outlet.
     * @return Stored arrowhead cross-width pixels, 12 for non-link kind; independent of length. Does
     *   not test current layer/object visibility or flow.
     */
    [[nodiscard]] double linkArrowWidth(Category c) const;
    /**
     * @param[in] c Link spatial category; non-link kind ignored.
     * @param[in] pixels Raw cross-link arrowhead width in screen pixels; no finite/range clamp.
     * @note Equality no-op; changes corresponding symbol field, resyncs persistent adapter and
     *   requests repaint. Does not synchronize renderer/rule list or mark geometry rebuild/model
     *   edited.
     */
    void setLinkArrowWidth(Category c, double pixels);
    /**
     * @param[in] c Link spatial category: conduit, pump, orifice, weir, outlet.
     * @return Stored arrow colour with alpha, near-black for non-link kind. Does not test current
     *   layer/object visibility or flow.
     */
    [[nodiscard]] QColor linkArrowColor(Category c) const;
    /**
     * @param[in] c Link spatial category; non-link kind ignored.
     * @param[in] col Valid arrow colour including alpha; invalid ignored.
     * @note Equality no-op; changes corresponding symbol field, resyncs persistent adapter and
     *   requests repaint. Does not synchronize renderer/rule list or mark geometry rebuild/model
     *   edited.
     */
    void setLinkArrowColor(Category c, const QColor &col);
    /**
     * @param[in] c Link spatial category: conduit, pump, orifice, weir, outlet.
     * @return Stored positive-flow-only filter flag, false for non-link kind. Does not test current
     *   layer/object visibility or flow.
     */
    [[nodiscard]] bool linkArrowOnlyWhenFlowPos(Category c) const;
    /**
     * @param[in] c Link spatial category; non-link kind ignored.
     * @param[in] onlyPos Whether painter must suppress arrow unless current signed engine flow is
     *   positive.
     * @note Equality no-op; changes corresponding symbol field, resyncs persistent adapter and
     *   requests repaint. Does not synchronize renderer/rule list or mark geometry rebuild/model
     *   edited.
     */
    void setLinkArrowOnlyWhenFlowPos(Category c, bool onlyPos);

    /*! Live link-flow accessor used by the painter's `arrowOnlyWhenFlowPos`
     *  short-circuit (Phase 8.13.8-α). Returns 0 when the engine handle
     *  is null or the index is out of range.
     * @param[in] linkIdx Nonnegative engine link index, not category display row.
     * @return Signed current engine link flow in model FLOW_UNITS, 0 for absent engine/invalid
     *   index/API failure. Before simulation an editable model commonly reports0; no output-file
     *   binding or unit conversion here.
     */
    [[nodiscard]] double linkFlow(int linkIdx) const;

    /*! Slice BI Phase 8.13.6.4 — per-feature paint-loop refactor.
     *
     *  True when the kind's renderer is non-SingleSymbol (Graduated /
     *  Categorized / RuleBased) and the layer has pre-computed an
     *  override colour for every feature in this kind. The painter
     *  reads this flag to decide whether to use the legacy bucketed
     *  fast path (false, single pen+brush per kind) or the per-
     *  feature override path (true).
     * @param[in] c Spatial category.
     * @return Cached slow-feature-path flag, false for invalid kind. Non-single slots set true;
     *   SingleSymbol requires varying resolved size across sampled features.
     */
    [[nodiscard]] bool kindUsesOverrides(Category c) const;

    /*! Returns the cached per-feature colour for one feature in kind
     *  \p c, or an invalid QColor when no override exists. The painter
     *  uses this for the per-feature paint path. Index is the global SoA/engine
     *  index, not the category display row used by objectNameAt().
     * @param[in] c Spatial category.
     * @param[in] idx Global source/engine index in appropriate node/link/catch/gage array, not
     *   category display row.
     * @return Cached colour or invalid QColor for absent/invalid slot. Accessor does not
     *   rebuild/validate cache freshness; caller coordinates source/renderer changes.
     */
    [[nodiscard]] QColor featureColor(Category c, int idx) const;

    /*! Slice BI Phase 8.13.43-α — per-feature SIZE override (pixels).
     *  Returns a negative sentinel (-1.0) when no override exists for
     *  this feature; positive values are absolute pixel sizes (the
     *  painter scales the kind's static glyph radius by `size / static`).
     * @param[in] c Spatial category.
     * @param[in] idx Global source/engine index in appropriate node/link/catch/gage array, not
     *   category display row.
     * @return Resolved absolute pixel size, or negative sentinel minus one when absent/invalid.
     *   Accessor does not rebuild/validate cache freshness; caller coordinates source/renderer
     *   changes.
     */
    [[nodiscard]] double featureSize(Category c, int idx) const;

    /*! M3 — per-feature marker shape sampled from the renderer (Categorized /
     *  Rule-based). Returns the MarkerShape as int, or -1 when the renderer
     *  has no per-feature shape override (use the kind's base shape).
     * @param[in] c Spatial category.
     * @param[in] idx Global source/engine index in appropriate node/link/catch/gage array, not
     *   category display row.
     * @return MarkerShape integer or minus one to use base kind glyph. Accessor does not
     *   rebuild/validate cache freshness; caller coordinates source/renderer changes.
     */
    [[nodiscard]] int featureShape(Category c, int idx) const;

    /*! Slice Z.5b-paint-graduated — per-feature line OFFSET override
     *  (pixels). Returns 0.0 when no offset is configured for this
     *  feature; positive = right of forward direction, negative =
     *  left. The paint host (SWMMLayerItem) reads this per visible
     *  link when the kind uses Graduated/Categorized renderers; the
     *  fast SingleSymbol path reads the kind's first symbol layer
     *  directly (via lineOffsetForKindRenderer).
     * @param[in] c Spatial category.
     * @param[in] idx Global source/engine index in appropriate node/link/catch/gage array, not
     *   category display row.
     * @return Signed line offset pixels (positive right of authored forward direction), zero for
     *   absent/invalid. Accessor does not rebuild/validate cache freshness; caller coordinates
     *   source/renderer changes.
     */
    [[nodiscard]] double featureOffset(Category c, int idx) const;

    /*! True when any feature in kind \p c has a non-zero offset
     *  override. The painter consults this to decide whether to take
     *  the per-feature slow path; when false the kind keeps its
     *  legacy fast path.
     * @param[in] c Spatial category.
     * @return Cached whether any resolved offset magnitude exceeds1e-12pixels, false invalid kind.
     *   Early rebuild exits can leave previous offset state until a nonempty rebuild.
     */
    [[nodiscard]] bool kindHasAnyOffset(Category c) const;

    /*! Recomputes the per-feature colour AND per-feature size override
     *  caches for one kind, by iterating every feature and calling
     *  `kindRenderer(c)->symbolFor(featureRef, attrs)`. Called
     *  automatically from setKindRenderer when the new renderer is
     *  non-SingleSymbol or has data-defined size enabled.
     * @param[in] c Valid spatial category; invalid ignored.
     * @note Clears colour/size/shape caches and override flag, derives Graduated classification,
     *   resolves renderer symbolFor once per feature and stores first-layer props by native SoA
     *   index. Category names use display ordering while index destination uses default type
     *   buckets. Graduated skeleton shapes ignored; size used only explicit size/width axes.
     * @note Non-single renderer activates overrides even with no valid output; SingleSymbol varying
     *   sizes activates size-only cache and clears colours, otherwise drops colour/size/shape
     *   caches. Offset values retained independently. No source edits/signals; null/empty early
     *   return can retain prior offset cache/flag. GUI-thread synchronously reads identify/user
     *   flags.
     */
    void rebuildKindFeatureColors(Category c);

    /*! Data-derive a GraduatedRenderer's breaks + value range for kind \p c
     *  when they are not yet set (empty breaks). Gathers the renderer's
     *  classifyAttribute() across this kind's features (static model fields
     *  via identifyByName) and calls GraduatedRenderer:: classifyIfNeeded.
     *  Shared by rebuildKindFeatureColors (paint cache) and the Rule→layer
     *  rendererReplaced handler (editor reads the Rule's renderer) so both
     *  see the same data-derived classification. No-op when already
     *  classified, the attribute is empty, or it isn't a static field.
     * @param[in] c Category whose static attributes provide samples.
     * @param[in] g Borrowed mutable GraduatedRenderer; null no-op.
     * @note Synchronously gathers finite convertible identify values by category display
     *   names/typed namespace. Classifies only absent breaks with nonempty classify attribute;
     *   separately derives independent size range only when invalid and finite max>min.
     *   Missing/dynamic attributes yield no samples; existing authored breaks remain. No source
     *   edits/layer signals here.
     */
    void classifyGraduatedIfNeeded(Category c, OpenSWMM::Render::GraduatedRenderer *g);

    // ----- Selection ------------------------------------------------------

    /*!
     * Kind bits — one per SWMM object namespace. Names are NOT unique
     * across kinds (generated models routinely pair every subcatchment
     * with a same-named rain gage), so typed selection and hidden state
     * carry a per-name bitmask of the kinds they apply to.
     */
    static constexpr quint8 kKindNode = 0x01;
    static constexpr quint8 kKindLink = 0x02;
    static constexpr quint8 kKindCatch = 0x04;
    static constexpr quint8 kKindGage = 0x08;
    static constexpr quint8 kKindAll = 0x0F;

    /*! Category → kind bit (kKindNode/Link/Gage/Catch) mapping. Public so
     *  callers that only know a Category (e.g. SWMMAttributeTableModel,
     *  which is scoped to one category per table) can pass the right
     *  kindHint to applyRename() without re-deriving the same mapping.
     * @param[in] c Spatial category.
     * @return Namespace node/link/catch/gage bit, 0 for invalid category; subtypes share one
     *   namespace bit.
     */
    [[nodiscard]] static quint8 kindBitForCategory(Category c) noexcept;

    /*! Selection-bus object type (SWMMObjectRef::ObjectType value: 1=Node,
     *  2=Link, 3=Subcatchment, 4=RainGage) → identifyByName() kind mask;
     *  kKindAll for anything else. A conduit may share its name with a node,
     *  so a typed ref must pass this or it identifies the node instead.
     * @param[in] objectType Selection-bus numeric type: 1 node, 2 link, 3 subcatchment, 4 rain gage.
     * @return Matching namespace bit, all-kind mask for any other value; unknown input does not
     *   reject a name lookup.
     */
    [[nodiscard]] static constexpr quint8 kindMaskForObjectType(int objectType) noexcept
    {
        switch (objectType) {
        case 1:
            return kKindNode;
        case 2:
            return kKindLink;
        case 3:
            return kKindCatch;
        case 4:
            return kKindGage;
        default:
            return kKindAll;
        }
    }

    /*!
     * \brief One selected element: its name plus a bitmask of the kinds
     *        (kKind*) the selection applies to. SWMM names are per-type
     *        namespaces — a rain gage and a subcatchment may legally share
     *        a name — so selection must carry the kind or a click on one
     *        lights up both. Order is preserved (the profile tool seeds
     *        its start/end nodes from shift-click order).
     */
    struct SelectedElement {
        QString name;
        quint8 kinds = 0;
        /**
         * @param[in] o Typed reference compared by exact case-sensitive name and raw kind mask.
         * @return True only when both stored fields match; no namespace normalization, existence
         *   check or bit filtering.
         */
        bool operator==(const SelectedElement &o) const
        {
            return kinds == o.kinds && name == o.name;
        }
    };

    /*!
     * \brief Returns the names of currently selected network elements.
     * @return Owned ordered aggregate names derived from typed selection. Normal nonzero masks
     *   deduplicate by first name occurrence; zero-mask entries can repeat because accumulation
     *   remains0. Unknown names are retained.
     */
    [[nodiscard]] QStringList selectedElementNames() const;

    /*!
     * \brief Typed selection — canonical form. Each entry names WHICH kind
     *        is selected, so same-named objects of other kinds stay
     *        unselected.
     * @return Borrowed canonical typed selection, preserving supplied order, duplicates,
     *   zero/unknown bits and unknown names. Invalidated by replacement/close/destruction; copy for
     *   independent snapshot.
     */
    [[nodiscard]] const QVector<SelectedElement> &selectedElements() const
    {
        return m_selectedElements;
    }

    /*!
     * \brief Replaces the selection with typed entries (kind-exact).
     * @param[in] sel Complete ordered typed-reference vector copied verbatim; no
     *   existence/name/mask validation.
     * @note Equal canonical vector no-op. Rebuilds derived aggregate names/masks and live renderer
     *   selection flags, emits selectionChanged(names) then repaintRequested without geometry
     *   rebuild or engine edit. Names with nonzero accumulated masks deduplicate first occurrence;
     *   zero masks may duplicate aggregate names.
     */
    void setSelectedElements(const QVector<SelectedElement> &sel);

    /*!
     * \brief Selects network elements by name (replaces prior selection).
     *        Legacy name-only form: each name selects EVERY kind bearing
     *        it. Prefer setSelectedElements() when the caller knows the
     *        kind.
     * @param[in] names Ordered legacy exact names; each expands to kKindAll, including unknown
     *   names.
     * @note Delegates to typed replacement and its equality/flag/signal behavior. Same-named
     *   objects across all namespaces become selected; typed callers should preserve explicit
     *   masks.
     */
    void setSelectedElementNames(const QStringList &names);

    /**
     * @brief Replace typed selection with empty vector through ordinary setter; already empty is a
     *   no-op, otherwise selection/repaint notifications follow.
     */
    void clearSelection();

    // ----- Identify -------------------------------------------------------

    /*!
     * \brief Returns attributes of the network element closest to the map point.
     * \param mapX        X in canvas CRS.
     * \param mapY        Y in canvas CRS.
     * \param canvasSRS   Canvas CRS (may be nullptr).
     * \param tolerance   Search radius in map units.
     * \param visibility  Optional visibility for another view of this geometry;
     *                    receives the category and engine/SoA index, and replaces
     *                    the model's own hidden-object filter (used by outputs).
     * \returns           Attribute map, or an empty map if nothing was found.
     *                    Includes "elementType", "elementName", and native point x/y when
     *   applicable;
     *                    use identifyByName for full static properties.
     */
    /**
     * @note canvasSRS argument is currently unused; caller must prepare cached transform through
     *   onCanvasCRSChanged. Coordinates/tolerance start in canvas CRS; inverse transform maps click
     *   and diagonally offset tolerance point into layer units, taking maximum axis delta.
     *   Transform statuses are unchecked; unavailable inverse keeps raw coordinates.
     * @note Strict-distance priority: nearest visible node/gage within tolerance, then nearest link
     *   segment within one-third tolerance, then first containing subcatch polygon. Typed hidden
     *   state or optional callback filters candidates; global showNodes/showLinks/layer gates are
     *   not consulted here. KD search has linear point fallback only on no hit; ties follow
     *   search/source order.
     * @return Lightweight hit map only: elementType, elementName and point x/y in native layer
     *   units when applicable; empty for miss. Full static properties require identifyByName().
     *   Const call can lazily rebuild KD trees, GUI-thread serialized.
     */
    [[nodiscard]] QVariantMap
    identifyAt(double mapX, double mapY, const SpatialReferenceSystem *canvasSRS,
               double tolerance = 1e-6,
               const std::function<bool(Category, int)> &visibility = {}) const;

    /*!
     * \brief Convenience overload — no CRS needed.
     * @param[in] mapX Canvas-map X, without scene Y inversion.
     * @param[in] mapY Canvas-map Y, without scene Y inversion.
     * @param[in] tolerance Nonnegative finite search radius in canvas-map units, transformed by
     *   cached inverse CRS when available.
     * @return Same lightweight tiered hit result as full overload, using model typed hidden state
     *   and already prepared transform; no new CRS is inferred.
     */
    [[nodiscard]] QVariantMap identifyAt(double mapX, double mapY, double tolerance = 1e-6) const;

    /*!
     * \brief Identify an object by name (instead of by map coordinates).
     * \details Used by panels that already know the object reference and just
     *          need its attribute map (Object Browser → PropertiesPanel).
     *          Returns an empty map if the name doesn't match any cached
     *          node / link / subcatchment / gage.
     * \param kindMask kKind* bits limiting which kinds may match. SWMM names
     *          are per-type namespaces (a gage and a subcatchment may legally
     *          share a name), and the default kKindAll resolves collisions by
     *          the legacy precedence node → link → catchment → gage — so a
     *          caller that KNOWS the kind (e.g. the Attribute Table, scoped
     *          to one category) must pass it or a shadowed gage/catchment
     *          reads the wrong object's map.
     * @param[in] name Exact cached identifier, restricted to supplied namespace mask.
     * @return Owned static/computed attribute map with Type/Name and available geometry/engine
     *   fields, empty if no eligible cached match. Namespace precedence node→link→catch→gage,
     *   lowest index within kind. No visibility filter.
     * @note X/Y are cached native model-map coordinates; static numeric fields are direct engine
     *   getter values in their model unit conventions, apart from time-flooded seconds converted to
     *   hours. Tags use256-byte buffers; circular diameter populated only xsect shape 0/1. Current
     *   engine statistics need not equal loaded output-file results. Adds typed uf: user flag
     *   values, lazily creating/loading user flag model even via const access; GUI thread only.
     */
    [[nodiscard]] QVariantMap identifyByName(const QString &name, quint8 kindMask = kKindAll) const;

    /*!
     * \brief Layer-CRS bounding box of a cached object by name.
     * \details
     *  - Node / Rain Gage: single-point extent (caller pads for a usable zoom).
     *  - Link:             bbox of the cached polyline (endpoints + vertices).
     *  - Subcatchment:     bbox of the cached polygon ring.
     *  Returns an invalid `MapExtent` if the name isn't known, or for a
     *  polygon/polyline with < 1 vertex. The canvas's zoom-to-object flow
     *  uses this so subcatchments and links frame their whole geometry
     *  instead of falling through the "needs X/Y" branch.
     * @param[in] name Exact untyped cached name resolved through single-keyed location winner.
     * @return Native layer-CRS bbox, point-degenerate for nodes/gages, all NaN for unknown/empty
     *   geometry. This implementation resolves display row through DEFAULT buckets, so custom
     *   object-order overrides can cause a different object extent; caller must not assume typed
     *   namespaces/ordering are honored. Does not apply canvas transform/padding/visibility.
     */
    [[nodiscard]] class MapExtent objectExtent(const QString &name) const;

    // ----- Spatial-index queries (O(log N + k), backed by nanoflann KD-tree) ---

    /*!
     * \brief Names of all nodes (any type) whose layer-CRS coordinates fall
     *        inside the rectangle given in **canvas CRS**.
     *        The inverse CRS transform is applied internally, matching the
     *        CRS-aware behaviour of identifyAt.  Hidden objects are excluded.
     *        O(log N + k) via the internal KD-tree; rebuilt lazily after any
     *        coordinate-mutating operation (applyNodeMove / applyNodeAdd /
     *        rollbackTailNodeAdd).
     * @param[in] canvasMinX, canvasMinY Lower finite canvas-MAP rectangle coordinates, without
     *   scene Y inversion.
     * @param[in] canvasMaxX, canvasMaxY Upper finite rectangle coordinates; caller supplies ordered
     *   bounds.
     * @return KD-circumradius candidates refined to inverse-four-corner axis-aligned layer bbox,
     *   excluding ANY aggregate hidden-name membership (typed namespace masks not distinguished).
     *   KD output order is distance/search order, not display order; const may rebuild trees.
     *   Nonlinear CRS rectangle envelope can overselect versus original canvas rectangle; inverse
     *   failure/status unchecked can retain raw coordinates. No selection mutation/global
     *   visibility gates.
     */
    [[nodiscard]] QStringList nodesInRect(double canvasMinX, double canvasMinY, double canvasMaxX,
                                          double canvasMaxY) const;

    /*!
     * \brief Nearest-neighbor snap query for vertex editing.
     *
     * \details Searches for the closest node (via the KD-tree, O(log N))
     *          and the closest link interior vertex (bbox-filtered linear
     *          scan) within \p mapRadius of (\p mapX, \p mapY).  All
     *          coordinates are in layer CRS — the same system that
     *          MapTool:: toMapCoords() and m_nodes[i].x/y use.
     *
     * \param[in]  mapX, mapY   Query point in layer CRS.
     * \param[in]  mapRadius    Search radius in layer CRS units.
     * \param[out] outPt        Set to the nearest candidate when returning true.
     * \returns true if a candidate was found within mapRadius.
     */
    /*! Find the nearest snap candidate (node, link interior vertex, or
     *  subcatchment polygon vertex) within \p mapRadius of (\p mapX, \p mapY).
     *  If \p excludePos is set the candidate at that exact position is skipped
     *  — pass the drag-start position to avoid self-snapping to the vertex
     *  being dragged while still snapping to every other vertex, including
     *  other vertices on the same object.
     * @param[in] excludePos Optional native layer-map self position; all candidates with squared
     *   distance<1e-12 are excluded, not solely the dragged object.
     * @note Uses cached native coordinates with no canvas inverse transform. Strict radius
     *   comparison excludes exactly-on-radius candidates; zero radius yields no result. Considers
     *   nodes first, then link INTERIOR vertices, then catch ring vertices; ties retain earlier.
     *   Nodes/catches use aggregate hidden-name filter, but link vertices currently do NOT filter
     *   hidden links. Gages/link segments excluded. OutPt unchanged on miss; caller supplies
     *   positive finite radius.
     */
    [[nodiscard]] bool snapNearestPoint(double mapX, double mapY, double mapRadius, QPointF &outPt,
                                        std::optional<QPointF> excludePos = std::nullopt) const;

    /*!
     * \brief Names of all rain gages whose coordinates fall inside the
     *        canvas-CRS rectangle. Same semantics as nodesInRect.
     * @param[in] canvasMinX, canvasMinY Lower finite canvas-MAP rectangle coordinates, without
     *   scene Y inversion.
     * @param[in] canvasMaxX, canvasMaxY Upper finite rectangle coordinates; caller supplies ordered
     *   bounds.
     * @return KD-circumradius candidates refined to inverse-four-corner axis-aligned layer bbox,
     *   excluding ANY aggregate hidden-name membership (typed namespace masks not distinguished).
     *   KD output order is distance/search order, not display order; const may rebuild trees.
     *   Nonlinear CRS rectangle envelope can overselect versus original canvas rectangle; inverse
     *   failure/status unchecked can retain raw coordinates. No selection mutation/global
     *   visibility gates.
     */
    [[nodiscard]] QStringList gagesInRect(double canvasMinX, double canvasMinY, double canvasMaxX,
                                          double canvasMaxY) const;

    /*!
     * \brief Names of all links whose polyline bbox overlaps the
     *        canvas-CRS rectangle. Backed by a per-link bbox cache
     *        (built in buildGeometryCache, kept fresh on every
     *        coord-mutating edit). Hidden objects excluded.
     *
     *        Avoids the O(N²) trap the rubber-band tool previously hit
     *        — every link iteration used to call `linkIndex(name)` (a
     *        linear name-scan of m_links) just to look up the polyline
     *        for a fresh bbox compute.
     * @param[in] canvasMinX, canvasMinY Lower finite canvas-MAP rectangle coordinates, without
     *   scene Y inversion.
     * @param[in] canvasMaxX, canvasMaxY Upper finite rectangle coordinates; caller supplies ordered
     *   bounds.
     * @return Native cached feature bbox overlaps inverse-four-corner axis-aligned layer bbox,
     *   excluding ANY aggregate hidden-name membership (typed namespace masks not distinguished).
     *   Outputs ascending native source order; wrong-sized bbox cache returns empty. Nonlinear CRS
     *   rectangle envelope can overselect versus original canvas rectangle; inverse failure/status
     *   unchecked can retain raw coordinates. No selection mutation/global visibility gates.
     */
    [[nodiscard]] QStringList linksInRect(double canvasMinX, double canvasMinY, double canvasMaxX,
                                          double canvasMaxY) const;

    /*!
     * \brief Names of all subcatchments whose polygon bbox overlaps
     *        the canvas-CRS rectangle. Same caching pattern as
     *        `linksInRect`.
     * @param[in] canvasMinX, canvasMinY Lower finite canvas-MAP rectangle coordinates, without
     *   scene Y inversion.
     * @param[in] canvasMaxX, canvasMaxY Upper finite rectangle coordinates; caller supplies ordered
     *   bounds.
     * @return Native cached feature bbox overlaps inverse-four-corner axis-aligned layer bbox,
     *   excluding ANY aggregate hidden-name membership (typed namespace masks not distinguished).
     *   Outputs ascending native source order; wrong-sized bbox cache returns empty. Nonlinear CRS
     *   rectangle envelope can overselect versus original canvas rectangle; inverse failure/status
     *   unchecked can retain raw coordinates. No selection mutation/global visibility gates.
     */
    [[nodiscard]] QStringList subcatchmentsInRect(double canvasMinX, double canvasMinY,
                                                  double canvasMaxX, double canvasMaxY) const;

    // ----- Polygon (lasso) queries ----------------------------------------
    // Same contract as the *InRect queries above, but the test region is an
    // arbitrary polygon given in canvas CRS. Candidates are first gathered
    // with the polygon's bounding box (reusing the accelerated *InRect path),
    // then refined with an exact point-in-polygon test performed in layer CRS
    // (nodes/gages at their coordinate; links/subcatchments at their bbox
    // centre). Hidden objects are excluded. \p canvasPoly needs >= 3 vertices.

    /**
     * @param[in] canvasPoly Finite canvas-MAP polygon with at least 3 vertices; shorter returns
     *   empty.
     * @return Owned source-ordered names from rectangle candidates whose native point lies inside
     *   vertex-wise inverse-projected polygon (OddEvenFill). Links/catches use bbox CENTER, not
     *   intersection/containment of full geometry. Aggregate hidden-name filtering comes from
     *   rectangle query, even for a same-named visible other kind. Transform failure can use
     *   original input vertex.
     */
    [[nodiscard]] QStringList nodesInPolygon(const QPolygonF &canvasPoly) const;
    /**
     * @param[in] canvasPoly Finite canvas-MAP polygon with at least 3 vertices; shorter returns
     *   empty.
     * @return Owned source-ordered names from rectangle candidates whose native point lies inside
     *   vertex-wise inverse-projected polygon (OddEvenFill). Links/catches use bbox CENTER, not
     *   intersection/containment of full geometry. Aggregate hidden-name filtering comes from
     *   rectangle query, even for a same-named visible other kind. Transform failure can use
     *   original input vertex.
     */
    [[nodiscard]] QStringList gagesInPolygon(const QPolygonF &canvasPoly) const;
    /**
     * @param[in] canvasPoly Finite canvas-MAP polygon with at least 3 vertices; shorter returns
     *   empty.
     * @return Owned source-ordered names from rectangle candidates whose native bbox center lies
     *   inside vertex-wise inverse-projected polygon (OddEvenFill). Links/catches use bbox CENTER,
     *   not intersection/containment of full geometry. Aggregate hidden-name filtering comes from
     *   rectangle query, even for a same-named visible other kind. Transform failure can use
     *   original input vertex.
     */
    [[nodiscard]] QStringList linksInPolygon(const QPolygonF &canvasPoly) const;
    /**
     * @param[in] canvasPoly Finite canvas-MAP polygon with at least 3 vertices; shorter returns
     *   empty.
     * @return Owned source-ordered names from rectangle candidates whose native bbox center lies
     *   inside vertex-wise inverse-projected polygon (OddEvenFill). Links/catches use bbox CENTER,
     *   not intersection/containment of full geometry. Aggregate hidden-name filtering comes from
     *   rectangle query, even for a same-named visible other kind. Transform failure can use
     *   original input vertex.
     */
    [[nodiscard]] QStringList subcatchmentsInPolygon(const QPolygonF &canvasPoly) const;

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    /**
     * @param[in] scene Required nonnull target scene when layer is visible.
     * @param[in] canvasExtent Currently unused; full cached geometry drives item bounds.
     * @param[in] canvasSRS Unused here; cached transforms prepared separately.
     * @note Hidden layer no-op. Adds a fresh scene-owned batched item borrowing layer, sets layer Z
     *   and records pointer. Does not remove an older item; caller must depopulate before repeated
     *   population. GUI-thread only.
     */
    void populateScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @param[in] scene Borrowed target scene; null no-op, including retained item pointer.
     * @note Scans/deletes all batched items owned by this layer, invalidates vacated rectangle,
     *   clears pointer and marks rebuild. Source arrays/engine untouched. GUI-thread only.
     */
    void depopulateScene(QGraphicsScene *scene) override;

    /**
     * @param[in] scene Target scene, non-null if a visible populate is needed.
     * @param[in] canvasExtent Forwarded only to population.
     * @param[in] canvasSRS Forwarded but population does not rebuild transform.
     * @note No-op unless rebuild flag set. Depopulates/repopulates, retaining rebuild flag while
     *   hidden so later display can create an item. GUI-thread only.
     */
    void refreshScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @param[in] newCanvasSRS Borrowed nullable canvas CRS.
     * @note Rebuilds forward/inverse transformation and scene coordinate caches, then marks item
     *   rebuild. Does not itself issue repaint notification. GUI-thread only.
     */
    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    /*!
     * \brief Rebuild derived geometry caches from current GUI source coordinates.
     * \details Source coordinates must already match the intended engine state.
     *          This method does not read new spatial values from the engine.
     * @brief Rebuild derived geometry/spatial/category/scene caches from current GUI source arrays
     *   and request repaint.
     * @note Does not re-read spatial data from engine; use edit/reload-model paths to synchronize
     *   source arrays. Rebuild clears intra-category order overrides.
     */
    void reloadGeometry();

    /*!
     * \brief Reverse-transform a canvas-CRS coordinate into the layer's
     *        native CRS.
     * \details Used by the status-bar cursor read-out so the displayed
     *          coordinate is in the user's data CRS even when the canvas
     *          renders via on-the-fly reprojection (e.g. geographic layer
     *          displayed in Web Mercator). When the layer CRS equals the
     *          canvas CRS (no forward transform), passes inputs through
     *          unchanged. The inverse transform is cached so per-mouse-move
     *          calls don't recreate a GDAL transformation object.
     * \return  true on success; on failure, lx/ly equal cx/cy.
     * @param[in] cx Canvas-map X.
     * @param[in] cy Canvas-map Y.
     * @param[out] lx Native layer-map X; initialized/restored to input on failure.
     * @param[out] ly Native layer-map Y; initialized/restored to input on failure.
     * @return True identity when no forward transform, or successful cached inverse transform;
     *   false cannot create inverse/project. Const lazily creates owned inverse, so serialize; does
     *   not apply scene Y flip.
     */
    bool transformCanvasToLayer(double cx, double cy, double &lx, double &ly) const;

    /*!
     * \brief Transform every vertex of a canvas-CRS polygon into layer CRS.
     * \details Helper for the *InPolygon queries so the point-in-polygon test
     *          runs in the same coordinate frame as the cached feature
     *          coordinates. Uses transformCanvasToLayer per vertex.
     * @param[in] poly Canvas-MAP polygon, without scene Y inversion.
     * @return Owned vertex-wise inverse-transformed copy, same point count; failed point conversion
     *   keeps original canvas coordinates. No edge densification or validity/closure check.
     *   Serialized access may initialize inverse transform lazily.
     */
    [[nodiscard]] QPolygonF polygonCanvasToLayer(const QPolygonF &poly) const;

    /*!
     * \brief Forward-transform a layer-CRS coordinate into the canvas CRS.
     * \details Map tools collect mouse input in canvas CRS (via toMapCoords)
     *          but several helpers (e.g. snap, cachedNodeCoord) return
     *          coordinates in the layer's native CRS. When the two CRSes
     *          differ — typically because a basemap forces the canvas to
     *          Web Mercator while the SWMM model is in a different
     *          projection — tools need to round-trip those values back to
     *          canvas CRS so interactive rubber-band / snap-ring rendering
     *          stays aligned with what the user clicked. When the layer
     *          CRS equals the canvas CRS, this passes inputs through
     *          unchanged.
     * \return  true on success; on failure, cx/cy equal lx/ly.
     * @param[in] lx Native layer-map X.
     * @param[in] ly Native layer-map Y.
     * @param[out] cx Canvas-map X, restored to input on failure.
     * @param[out] cy Canvas-map Y, restored to input on failure.
     * @return True identity with no transform or successful reprojection, false transform failure.
     *   Does not apply scene Y flip or mutate coordinate source.
     */
    bool transformLayerToCanvas(double lx, double ly, double &cx, double &cy) const;

    /*!
     * \brief Classify a name into its SWMM object class via O(1) lookup
     *        against `m_objectLocation`.
     * \details Used by the SelectionManager bridge to translate the layer's
     *          name-only selection set into typed SWMMObjectRefs. Hot path
     *          on rubber-band selection (called once per selected name);
     *          must stay O(1) — do not regress to a linear SoA scan.
     * \return  0=Unknown, 1=Node, 2=Link, 3=Subcatchment, 4=RainGage —
     *          matches the SWMMObjectRef:: ObjectType enum values.
     * @param[in] name Exact untyped cached name.
     * @return Selection-bus1node/2 link/3catch/4gage, 0unknown. Uses single-keyed location collision
     *   winner rather than typed engine namespaces.
     */
    [[nodiscard]] int objectTypeFor(const QString &name) const;

    // ----- Geometry editing API (Phase 2) --------------------------------

    /*!
     * \brief Returns the cache index of the node with the given name, or -1
     *        if none matches.
     * @param[in] name Exact identifier in node namespace.
     * @return First cached matching global engine/source index, minus one missing. Distinct
     *   namespace hashes avoid node/link name collisions; indices renumber after deletion/reload.
     */
    [[nodiscard]] int nodeIndex(const QString &name) const;

    /*!
     * \brief Returns the cache index of the link with the given name, or -1
     *        if none matches.
     * @param[in] name Exact identifier in link namespace.
     * @return First cached matching global engine/source index, minus one missing. Distinct
     *   namespace hashes avoid node/link name collisions; indices renumber after deletion/reload.
     */
    [[nodiscard]] int linkIndex(const QString &name) const;

    /*!
     * \brief Cached layer-CRS coordinate of a node by index.
     * \returns true on success; x / y untouched on failure.
     * @param[in] idx Global node source index.
     * @param[out] x Optional native layer-map X destination, only assigned on success.
     * @param[out] y Optional native layer-map Y destination, only assigned on success.
     * @return False out-of-range unchanged; true for cached coordinate, including pending drag
     *   preview/nonfinite producer values. Does not read current engine.
     */
    bool cachedNodeCoord(int idx, double *x, double *y) const;

    /*!
     * \brief Layer-CRS position of any named element (node, gage, or link
     *        midpoint) by object name.
     * \details Uses the internal name→SoA hash for O(1) lookup. For links
     *          the midpoint of the cached polyline is returned.
     * \returns true on success; x / y untouched on failure.
     * @param[in] name Exact untyped name resolved through source-location collision winner.
     * @param[out] x Optional native layer-map representative X, assigned only on success.
     * @param[out] y Optional native layer-map representative Y, assigned only on success.
     * @return True for representative position, false unchanged if missing/empty/invalid. Links use
     *   middle VERTEX by index, not arc-length midpoint; catch uses bbox center, not polygon
     *   centroid. Current gage branch calls cachedNodeCoord with gage index and can return
     *   same-index NODE position or fail; use cachedGageCoord for authored gage positions. No
     *   visibility filter.
     */
    bool elementPosition(const QString &name, double *x, double *y) const;

    /*!
     * \brief Cached layer-CRS polyline of a link by index, including the
     *        endpoint coordinates of its from/to nodes.
     * @param[in] idx Global link/subcatch source index, not category display row.
     * @return Owned full native layer-map polyline with valid cached endpoint positions plus stored
     *   interior bends; missing endpoints omitted, invalid link empty. No transform, unit
     *   conversion, engine read or visibility filtering.
     */
    [[nodiscard]] QVector<QPointF> cachedLinkPolyline(int idx) const;

    /*!
     * \brief Cached interior-only polyline of a link (endpoints stripped).
     * \details The engine's set_link_vertices API writes interior points
     *          only; this helper returns the corresponding subsection of
     *          the cached polyline for round-trip edits.
     * @param[in] idx Global link/subcatch source index, not category display row.
     * @return Owned native layer-map interior bends only, excluding node endpoints; invalid link
     *   empty. No transform, unit conversion, engine read or visibility filtering.
     */
    [[nodiscard]] QVector<QPointF> cachedLinkInteriorVertices(int idx) const;

    /*!
     * \brief Number of links the render-facing scene-coordinate arrays
     *        currently describe.
     * \details Test seam. m_linkVertexCount / m_linkVertexOffset are what
     *          SWMMLayerQSGRenderer walks when it draws links, and their
     *          `size == m_links.size()` invariant is otherwise observable
     *          only from inside that friend class — which is how a stale
     *          set survived a delete-everything undetected.
     * @return Render flat-layout link-count slots, narrowed to int; must match cachedLinkCount
     *   after coherent rebuild. During bulk/build/preview layout changes caller must not paint
     *   incomplete arrays.
     */
    [[nodiscard]] int renderLinkCount() const { return int(m_linkVertexCount.size()); }

    /*!
     * \brief The hidden flag the renderers read for link \p idx — what
     *        actually decides whether the base link pass emits it.
     * \details Test seam, sibling of renderLinkCount(). The QSG base pass
     *          skips a link whose flag is set; a selected link is still drawn
     *          by the selection pass, which is why a wrongly-hidden link looks
     *          fine until it is deselected.
     * @param[in] idx Global cached link index.
     * @return Cached typed hidden byte, true when set; false for invalid/missing flag. Does not
     *   apply global/kind/zoom gates, and selection overlay can still draw selected hidden
     *   features.
     */
    [[nodiscard]] bool renderLinkHidden(int idx) const
    {
        return idx >= 0 && size_t(idx) < m_linkHiddenFlag.size() &&
               m_linkHiddenFlag[size_t(idx)] != 0;
    }

    /*!
     * \brief Cached layer-CRS polygon of a subcatchment by index. Vertex
     *        order matches the .inp [Polygons] section. Returns empty
     *        when \p idx is out of range or the subcatchment has no
     *        polygon (engine permits coordinate-less subcatchments).
     * @param[in] idx Global link/subcatch source index, not category display row.
     * @return Owned native layer-map vertices as stored/cleaned; single rings are open,
     *   multiple rings retain explicit closing vertices. Invalid catch empty. No
     *   transform, unit conversion, engine read or visibility filtering.
     */
    [[nodiscard]] QVector<QPointF> cachedSubcatchVertices(int idx) const;

    /*!
     * \brief Number of subcatchments cached. Equivalent to
     *        `categoryCount(CatSubcatchments)` but provided alongside
     *        \ref cachedSubcatchVertices for direct iteration.
     * @return Current GUI subcatchment source-array slot count, narrowed to int; no engine
     *   query/visibility filter.
     */
    [[nodiscard]] int cachedSubcatchCount() const;

    /*! \brief Cached top-level object counts (all node/link kinds summed).
     *         Convenience for load-summary logging.
     * @return Current GUI node source-array slot count, narrowed to int; no engine query/visibility
     *   filter.
     */
    [[nodiscard]] int cachedNodeCount() const;
    /**
     * @return Current GUI link source-array slot count, narrowed to int; no engine query/visibility
     *   filter.
     */
    [[nodiscard]] int cachedLinkCount() const;
    /**
     * @return Current GUI rain gage source-array slot count, narrowed to int; no engine
     *   query/visibility filter.
     */
    [[nodiscard]] int cachedGageCount() const;

    /*!
     * \brief A rain gage's coordinate AS THE ENGINE STORES IT.
     * \details Deliberately NOT the cached `m_gages` position. A gage with no
     *          `[SYMBOLS]` row arrives at (0, 0), and buildGeometryCache()
     *          relocates such gages to the mean of every model vertex so they
     *          do not stack on the origin — a display convenience that invents
     *          a coordinate. Any spatial computation (Thiessen cells,
     *          interpolation weights) must see the engine's own value, because
     *          several un-located gages would otherwise all land on one
     *          fabricated point and quietly dominate the result.
     * \returns false when \p idx is out of range or no engine is attached.
     *          A returned (0, 0) means "un-located" and callers should exclude
     *          the gage rather than use it.
     * @param[in] idx Global rain-gage engine index.
     * @param[out] x Optional authored native model-map X destination, assigned on success.
     * @param[out] y Optional authored native model-map Y destination, assigned on success.
     * @return True only after attached-engine spatial read succeeds; false unchanged for no
     *   engine/invalid index/read failure. Despite name, reads ENGINE coordinates to avoid visual
     *   origin-relocation/drag-preview cache values.
     */
    [[nodiscard]] bool cachedGageCoord(int idx, double *x, double *y) const;

    /*! Monotonically increasing scene-geometry cache counter. Some
     *  full-rebuild early returns do not bump it.  Renderers can compare against a cached
     *  value to cheaply detect whether scene geometry has changed without
     *  storing a pointer or connecting a signal.
     * @return Geometry-cache epoch bumped by several incremental edits and some full rebuild
     *   outcomes; not every rebuildSceneCoords call increments (no-engine/empty-catch early exit
     *   skips bump), and a single edit may increment more than once.
     */
    [[nodiscard]] quint64 geomRevision() const { return m_geomRevision; }

    // ----- Simulation-options pass-through (Slice U) ----------------------

    /*!
     * \brief Read a single SWMM OPTIONS key from the engine. Returns the
     *        \p fallback when the engine has no value for the key or the
     *        engine isn't open.
     *
     *        Kept as a layer method (rather than going direct to
     *        `swmm_options_get`) so the SimulationOptionsDialog sees a
     *        single model object rather than a mix of engine + layer
     *        APIs, and so the optionsChanged() signal stays a single
     *        pipe.
     * @param[in] key Nonempty engine OPTIONS keyword bytes, passed as NUL-terminated key.
     * @param[in] fallback Value returned for no engine, empty key, read failure or empty engine
     *   text.
     * @return Trimmed UTF-8 option string read through 512-byte buffer, or fallback. No
     *   unit/grammar conversion beyond engine textual convention; oversized values can fail getter
     *   rather than truncate transparently.
     */
    [[nodiscard]] QString getOption(const QByteArray &key, const QString &fallback = {}) const;

    /*!
     * \brief Write a single SWMM OPTIONS key. On success emits
     *        `optionsChanged({key})`. Returns true on success, false if
     *        the engine isn't open or rejected the value.
     *
     *        Batch-apply should prefer `setOptions(QMap)` to emit a
     *        single multi-key signal per round-trip.
     * @param[in] key Nonempty engine OPTIONS keyword bytes.
     * @param[in] value Text encoded UTF-8 and validated by engine option grammar/lifecycle.
     * @return True after accepted write and optionsChanged({key}), false for no engine/empty
     *   key/rejection. Equality is not checked and accepted same value still signals. No automatic
     *   modelEdited, UnitSystem/CRS synchronization, undo or persistence.
     */
    bool setOption(const QByteArray &key, const QString &value);

    /*!
     * \brief Batch write — applies a set of key/value pairs, then emits
     *        `optionsChanged(changedKeys)` once. Returns the number of
     *        keys the engine accepted.
     * @param[in] values Keyword/text map applied sequentially in QMap key order.
     * @return Number accepted, 0without engine/empty map. Failed writes skipped while later keys
     *   continue; partial update is retained, no rollback/error aggregation. Emits one
     *   optionsChanged with accepted keys when nonempty; no modelEdited/unit/CRS sync/undo itself.
     */
    int setOptions(const QMap<QByteArray, QString> &values);

    /*!
     * \brief Returns the conduit length recorded in the engine, or -1 if
     *        the link is not a conduit / is out of range.
     * @param[in] linkIdx Global engine link index; requires conduit subtype.
     * @return Authored conduit length in model linear units (feet US FLOW_UNITS, metres SI), minus
     *   one for no engine/nonconduit/API failure. No geometric recalculation.
     */
    [[nodiscard]] double engineLinkLength(int linkIdx) const;

    /*!
     * \brief Compute polyline length in the SWMM model's expected linear unit
     *        (feet for US-customary FLOW_UNITS — CFS/GPM/MGD; metres for SI —
     *        CMS/LPS/MLD).
     *
     *        Used by the auto-length code path to convert canvas/layer-CRS
     *        coordinates into the unit the engine writes into [CONDUITS]:
     *          - Projected CRS:   raw Euclidean × `linearUnitsToMetres()`
     *                             → metres, then to feet if not SI.
     *          - Geographic CRS:  great-circle distance per segment
     *                             (WGS-84 sphere) → metres, then to feet if not SI.
     *          - No SRS:          raw Euclidean (preserves legacy behaviour
     *                             for projects without a CRS set).
     * @param[in] vertices Ordered full polyline in native layer CRS, including endpoints; finite
     *   coordinates expected.
     * @return Zero for fewer than 2 points; otherwise cumulative length converted to engine
     *   FLOW_UNITS feet/metres. Geographic CRS uses 6378137m spherical haversine per segment (not
     *   ellipsoidal geodesic); projected uses CRS metres-per-unit, undeclared/local fallback
     *   assumes raw metres. Missing flow units defaults CFS/feet. No engine write.
     */
    [[nodiscard]] double polylineLengthInModelUnits(const QVector<QPointF> &vertices) const;

    /*!
     * \brief Returns true when the link at \p linkIdx is a conduit (the
     *        only link type whose length is stored as an independent
     *        geometry value and is therefore eligible for auto-length).
     * @param[in] linkIdx Global cached link index.
     * @return True for in-range cached link type 0, false invalid/other subtype; does not query
     *   engine or verify length/shape eligibility.
     */
    [[nodiscard]] bool isConduit(int linkIdx) const;

    // ----- Tool-facing hit-test (Slice R Phase 3) -------------------------

    /*!
     * \struct PickResult
     * \brief Tool-facing click-pick result. `valid` is false when no
     *        feature was hit within \p tolerance of the canvas-CRS
     *        click point. `cat` and `soaIndex` give the typed SoA
     *        position so tools can reach the underlying geometry
     *        without a second name → index lookup. `name` is the
     *        SWMM object id (matches selection-bus `SWMMObjectRef:: name`).
     */
    struct PickResult {
        bool valid = false;
        Category cat = NumCategories;
        int soaIndex = -1;
        QString name;
    };

    /*! Tiered click-pick in the same priority order as `identifyAt`
     *  (nodes + gages first, then links at 1/3 tolerance, then
     *  subcatchments via point-in-polygon). Returns the typed hit so
     *  MoveNode / EditVertex / Select-tool right-click don't need to
     *  go through `scene->items()` + dynamic_cast on retiring
     *  `NodeGraphicsItem` / `LinkGraphicsItem` placeholders.
     *
     *  \p sceneX \p sceneY are in canvas CRS (the same coordinate
     *  system the Select tool's `selectAtPoint` passes to
     *  `identifyAt`). \p tolerance is also in canvas CRS units; the
     *  layer inverts through its m_transform as needed. */
    /**
     * @param[in] sceneX Canvas-MAP X, without scene Y inversion despite historical scene name.
     * @param[in] sceneY Canvas-MAP Y, without scene Y inversion.
     * @param[in] tolerance Nonnegative finite canvas-map radius; cached inverse transform adjusts
     *   search tolerance.
     * @param[in] visibility Optional category/global-SoA predicate replacing model hidden-state
     *   filter.
     * @return Typed valid/name/category/global-SoA hit from lightweight identify priority, or
     *   default invalid. Resolves by returned namespace to avoid shared-name collisions; fallback
     *   maps display row through override/type bucket. Does not change selection or engine.
     */
    [[nodiscard]] PickResult
    pickAt(double sceneX, double sceneY, double tolerance,
           const std::function<bool(Category, int)> &visibility = {}) const;

    /*! Live-preview a node move — mutates the cached SoA coord plus
     *  every attached link's endpoint, emits `repaintRequested()`,
     *  but does NOT write through to the engine. Called by
     *  MapToolMoveNode on every mouseMoveEvent during a drag;
     *  MoveNodeCommand:: redo commits the final position via
     *  `applyNodeMove` when the drag is released. Returns false if
     *  the index is out of range or no node cache exists.
     * @param[in] idx Global node source index; invalid returns false.
     * @param[in] newX New cached native layer-map X; no finite/range validation.
     * @param[in] newY New cached native layer-map Y; no finite/range validation.
     * @return True after GUI-cache-only preview and repaint; engine, undo/modelEdited, bbox/KD-tree
     *   dirty state and item bounds remain untouched. Refreshes node scene point, attached link
     *   scenes and catch outlet lines. Caller commits/restores through proper apply edit; spatial
     *   queries can retain stale acceleration until then.
     */
    bool previewNodeMove(int idx, double newX, double newY);

    /*! Rain-gage twin of \ref previewNodeMove() : rewrite the gage's cached
     *  map coord + scene point only (no engine write, no modelEdited) so
     *  the symbol follows the cursor during a drag. MoveGageCommand:: redo
     *  commits the final position via \ref applyGageMove on release.
     *  Gages have no attached links or outlet lines, so this is just the
     *  coord + scene-point rewrite and a repaint.
     * @param[in] idx Global gage source index; invalid returns false.
     * @param[in] newX New cached native layer-map X; no finite/range validation.
     * @param[in] newY New cached native layer-map Y; no finite/range validation.
     * @return True after GUI-cache-only preview and repaint; engine, undo/modelEdited, bbox/KD-tree
     *   dirty state and item bounds remain untouched. Refreshes transformed/Y-inverted gage scene
     *   point when its slot exists; transform failure status unchecked. Caller commits/restores
     *   through proper apply edit; spatial queries can retain stale acceleration until then.
     */
    bool previewGageMove(int idx, double newX, double newY);

    /*!
     * \brief Indices of links whose from/to endpoint is the given node.
     * @param[in] nodeIdx Global cached/engine node index.
     * @return Owned ascending global link indices whose ENGINE endpoints match, empty without
     *   engine/invalid node. Linear scan, skips neither hidden links nor failed individual endpoint
     *   reads; failed reads leave minus-one sentinel.
     */
    [[nodiscard]] QVector<int> linksAttachedToNode(int nodeIdx) const;

    /*!
     * \brief Which end of a link is attached to \p nodeIdx: 0 = from,
     *        1 = to, or -1 if the node is not an endpoint of the link.
     * @param[in] linkIdx Global engine/cached link index.
     * @param[in] nodeIdx Global node index compared verbatim; caller should supply valid
     *   nonnegative index.
     * @return Zero for from, one for to, minus one neither/no-engine/invalid link; self-loop chooses from.
     *   Endpoint read return codes ignored with minus-one initialization, so an invalid minus-one
     *   node input can match a failed getter.
     */
    [[nodiscard]] int linkEndForNode(int linkIdx, int nodeIdx) const;

    /*! Returns the from-node (upstream) engine index for \p linkIdx, or -1.
     * @param[in] linkIdx Global cached/engine link index.
     * @return Engine from/upstream node index, minus one without engine/invalid link/read failure.
     *   Uses live engine rather than potentially previewed GUI endpoint coordinates.
     */
    [[nodiscard]] int linkFromNodeIdx(int linkIdx) const;

    /*! Returns the to-node (downstream) engine index for \p linkIdx, or -1.
     * @param[in] linkIdx Global cached/engine link index.
     * @return Engine to/downstream node index, minus one without engine/invalid link/read failure.
     *   Uses live engine rather than potentially previewed GUI endpoint coordinates.
     */
    [[nodiscard]] int linkToNodeIdx(int linkIdx) const;

    /*!
     * \brief Apply a new coordinate to a node: engine + cache + attached
     *        link endpoint updates. Does not push an undo command — the
     *        caller (tool or MoveNodeCommand) is responsible for that.
     * \param idx                Cache/engine node index.
     * \param newX, newY         New coordinate in the layer CRS.
     * \returns                  true on success.
     *
     * Emits repaintRequested() on success.
     * @return False invalid cache index/engine coordinate rejection, true after accepted cached
     *   move (including cache-only success if no engine). No facade finite/range clamp.
     * @note Refreshes node, attached link bboxes/scene endpoints and catch outlet lines; marks
     *   KD/scene rebuild, recomputes extent, emits repaint then modelEdited. Does not emit
     *   geometryChanged or push undo; attached-link enumeration requires engine, so cache-only
     *   moves cannot refresh those links through this route.
     */
    bool applyNodeMove(int idx, double newX, double newY);

    /*!
     * \brief Write the conduit length for a link. No-op if the link is
     *        not a conduit.
     * @param[in] linkIdx Global cached/engine conduit index.
     * @param[in] length Authored length in project linear units (ft for US, metres SI), validated
     *   by engine.
     * @return False for no engine/nonconduit/rejection; true after write and
     *   attributeChanged(linkName). No geometric coordinate edit, auto-length recursion,
     *   modelEdited or undo here.
     */
    bool applyLinkLength(int linkIdx, double length);

    /** Reassign one link endpoint without changing offsets, length or bends.
     * Validates node eligibility, refreshes cached map geometry and announces
     * geometry/attribute/model edits. The caller owns any undo command.
     */
    bool applyLinkEndpoint(int linkIdx, bool upstream, int nodeIdx);
    /** Apply an endpoint edit through the project's undo stack when attached. */
    bool pushLinkEndpointEdit(int linkIdx, bool upstream, int nodeIdx);

    /*!
     * \brief Apply the user's answer to legacy UpdateOffsets' "convert all
     *        link offsets?" prompt after LINK_OFFSETS has been flipped.
     *
     * The engine stores offsets as depths in BOTH modes (the parser
     * normalises elevations; the .inp writer re-adds the invert), so:
     * - convertValues == true  (legacy "Yes": same physics, new numbers) is a
     *   no-op on the store — the option flip alone changes file and display.
     * - convertValues == false (legacy "No": same numbers, new meaning)
     *   reinterprets each stored depth under the new convention. Conduits use
     *   both ends, orifices offset_up, weirs/outlets the crest, pumps skipped.
     * Emits `geometryChanged()` once whenever an engine is attached, including representation-only
     *   mode.
     * \param toElevation   The mode just switched TO.
     * \param convertValues The prompt answer (true = Yes).
     * @note Requires engine. convertValues=true leaves stored depths intact; false reinterprets
     *   numbers under requested representation, pumps skipped, weir/outlet crest only, conduit both
     *   endpoints. Get failures skip fields, set return codes ignored, so partial updates are
     *   possible. Always emits geometryChanged when engine exists, even representation-only/no
     *   writes; does not set LINK_OFFSETS option, emit modelEdited or create undo itself.
     */
    void convertLinkOffsets(bool toElevation, bool convertValues);

    /*!
     * \brief Slice SC.1 — Write a cross-section to a link via
     *        `swmm_link_set_xsect`. Emits `attributeChanged(linkName)` on
     *        success so the Map symbology + Attribute Table + Property
     *        Browser refresh in one tick. Returns false on engine error
     *        (e.g., bad shape code or geom param out of range).
     * @param[in] linkIdx Global engine/cached link index.
     * @param[in] shape SWMM_XSectShape code; legal shape/link combinations checked by engine.
     * @param[in] g1 Primary shape parameter: usually full depth/diameter in project length units;
     *   STREET/IRREGULAR use referenced store index instead.
     * @param[in] g2 Shape-specific secondary parameter, usually width in project length units;
     *   apply engine shape contract.
     * @param[in] g3 Shape-specific third parameter, such as side-slope/radius; dimensions depend on
     *   shape.
     * @param[in] g4 Shape-specific fourth parameter; unused slots still forwarded.
     * @return True after engine acceptance and attributeChanged(linkName), false no engine/invalid
     *   index/rejection. Some engine reference failures occur after raw shape fields mutate, so
     *   false is not a universal rollback guarantee. No geometry/source cache rebuild, modelEdited
     *   or undo here.
     */
    bool applyLinkXsect(int linkIdx, int shape, double g1, double g2, double g3, double g4);

    /*!
     * \brief Slice SC.1 — Write the parallel-barrels count. Emits
     *        `attributeChanged` on success.
     * @param[in] linkIdx Global cached/engine link index.
     * @param[in] barrels Positive integer parallel-conduit count; less than 1rejected locally,
     *   engine checks supported subtype.
     * @return True after accepted write/attributeChanged; false missing engine,
     *   index/range/type/lifecycle error. No modelEdited or undo itself.
     */
    bool applyLinkBarrels(int linkIdx, int barrels);

    /*!
     * \brief Slice SC.1 — Write the FHWA culvert chart code (0 = none).
     *        Emits `attributeChanged` on success.
     * @param[in] linkIdx Global cached/engine link index.
     * @param[in] code Engine FHWA culvert chart code, 0none; legal range/subtype validated by
     *   engine.
     * @return True after write/attributeChanged, false invalid engine/index/rejection; no facade
     *   clamp, modelEdited or undo.
     */
    bool applyLinkCulvertCode(int linkIdx, int code);

    /*!
     * \brief Convert node \p name to \p newNodeType (SWMM_NodeType value)
     *        via `swmm_node_convert`. The engine preserves common props
     *        (invert, depths, coordinates), clears old-type-specific
     *        fields and applies new-type defaults. On success the cached
     *        SoA nodeType and category buckets are updated and
     *        repaintRequested() + geometryChanged() + attributeChanged()
     *        are emitted so map symbology, Object Browser, Attribute
     *        Table, and Property Browser all refresh in one tick.
     * \param[out] outCleared   Engine-reported cleared field names.
     * \param[out] outWarnings  Engine-reported topology warnings.
     * \param[out] outError     Human-readable failure reason.
     * @param[in] name Exact node namespace identifier, required in both cache and engine.
     * @param[in] newNodeType Engine SWMM_NodeType code, validated by conversion API.
     * @return False for no engine/missing object/conversion rejection; true after cache
     *   type/category rebuild. Optional cleared/warning lists are replaced only on success;
     *   outError is not cleared on success.
     * @note Engine conversion can clear subtype-specific properties; node conversion also resets
     *   virtual/inlet flags and connector state. Rebuild clears intra-category order overrides;
     *   emits repaint, geometryChanged, attributeChanged. No explicit modelEdited or undo command
     *   here.
     */
    bool applyNodeConvert(const QString &name, int newNodeType, QStringList *outCleared = nullptr,
                          QStringList *outWarnings = nullptr, QString *outError = nullptr);

    /*!
     * \brief Convert link \p name to \p newLinkType (SWMM_LinkType value)
     *        via `swmm_link_convert`. Endpoints, offsets, and interior
     *        vertices are preserved by the engine. Same cache update and
     *        signal contract as applyNodeConvert().
     * @param[in] name Exact link namespace identifier, required in both cache and engine.
     * @param[in] newLinkType Engine SWMM_LinkType code, validated by conversion API.
     * @param[out] outCleared Optional list replaced on successful conversion, unchanged if
     *   rejected.
     * @param[out] outWarnings Optional list replaced on successful conversion, unchanged if
     *   rejected.
     * @param[out] outError Optional failure message, unchanged on success.
     * @return False for no engine/missing object/conversion rejection; true after cache
     *   type/category rebuild. Optional cleared/warning lists are replaced only on success;
     *   outError is not cleared on success.
     * @note Engine conversion can clear subtype-specific properties; node conversion also resets
     *   virtual/inlet flags and connector state. Rebuild clears intra-category order overrides;
     *   emits repaint, geometryChanged, attributeChanged. No explicit modelEdited or undo command
     *   here.
     */
    bool applyLinkConvert(const QString &name, int newLinkType, QStringList *outCleared = nullptr,
                          QStringList *outWarnings = nullptr, QString *outError = nullptr);

    /*!
     * \brief Reverse link \p linkIdx: the upstream node becomes the downstream
     *        node and vice versa, leaving the physical link unchanged.
     * \details Swaps node 1/node 2 via `swmm_link_set_nodes`, reverses the
     *          interior vertices (the engine stores them upstream→downstream,
     *          so without this the drawn polyline zig-zags), and — for conduits
     *          only — swaps the two end-attached value pairs, offset_up/
     *          offset_dn and the entry/exit loss coefficients, so the invert
     *          profile and head losses stay exactly where they were. Mirrors
     *          the engine's own adverse-slope reversal in PostParseResolver,
     *          minus the parse-time-derived `direction` and conduit `slope`
     *          (recomputed on the next engine open, and not exposed by any C
     *          API). Non-conduits carry a single crest offset in offset_up with
     *          offset_dn unused, so nothing is swapped for them.
     *
     *          Self-inverse: flipping twice restores the original state, which
     *          is what lets FlipLinkCommand use one call for redo and undo.
     *          Emits the applyLinkInteriorVertices() signal set (repaint +
     *          modelEdited) plus attributeChanged() for the From/To cells.
     * @param[in] linkIdx Global cached/engine link index.
     * @return False if initial engine/index/endpoint/node-swap check fails; true after endpoint
     *   swap even if later offset/loss/interior-vertex writes fail.
     * @note Nontransactional operation: conduit end offsets/loss pairs swapped when readable, set
     *   statuses ignored; cached ends swapped and interior list reversed through geometry edit
     *   whose result is ignored. Calls attributeChanged after ordinary geometry notifications if
     *   successful. Does not negate initial-flow sign or explicitly update parse-time
     *   direction/slope; next engine open recomputes those. Caller owns undo/error reconciliation.
     */
    bool applyLinkFlip(int linkIdx);

    // ===== Virtual junctions ==============================================
    // Engine-side split/fuse semantics (swmm_conduit_split /
    // swmm_virtual_junction_fuse / swmm_node_set_virtual) so CLI, Python,
    // MCP and GUI share one implementation. See
    // workplans/VIRTUAL_JUNCTION_GUI_PLAN_2026-08-01.md.

    /*! \brief True when cached node \p soaIdx is a virtual junction.
     * @param[in] soaIdx Global cached node index.
     * @return In-range cached virtual flag, false invalid; no engine validation and inlet junctions
     *   can also be virtual.
     */
    bool nodeIsVirtual(int soaIdx) const
    {
        return soaIdx >= 0 && soaIdx < m_nodes.size() && m_nodes[soaIdx].isVirtual != 0;
    }

    /*!
     * \brief Set or clear a node's virtual-junction flag via
     *        `swmm_node_set_virtual` (full engine validation). On a violated
     *        usage rule \p outError receives the actionable message from
     *        virtualJunctionRuleText().
     * @param[in] name Exact engine node identifier.
     * @param[in] makeVirtual True requests validated virtual role, false demotes and also clears
     *   inlet role.
     * @param[out] outError Optional actionable failure text, unchanged on success.
     * @return False for no engine/missing cached node/engine rule rejection, true after cached role
     *   update. Marks scene rebuild, emits repaint/attributeChanged and invalidates inlet
     *   connectors on demotion. No topology split, modelEdited or undo itself.
     */
    bool applySetVirtual(const QString &name, bool makeVirtual, QString *outError = nullptr);

    /*!
     * \brief Split conduit \p linkName at normalized polyline position \p t,
     *        inserting a virtual junction, via `swmm_conduit_split`. The
     *        original conduit keeps its name/upstream end; the new conduit
     *        \p newLinkName carries the remainder. Caches for the node, both
     *        conduits and the scene arrays are synchronized incrementally.
     * \param[out] outNodeIdx  Engine/cache index of the inserted node.
     * \param[out] outLinkIdx  Engine/cache index of the new conduit.
     * @param[in] linkName Exact engine conduit identifier also present in cache.
     * @param[in] t Normalized interior polyline split position, finite 0 < t < 1 validated by engine.
     * @param[in] newNodeName New unique node namespace identifier, passed UTF-8.
     * @param[in] newLinkName New unique downstream conduit identifier, passed UTF-8.
     * @param[out] outError Optional failure message, unchanged on success.
     * @return True if split and virtual-role validation both succeed. False with valid nonnegative
     *   output indices can mean split COMMITTED but virtual role refused: cache sync retained a new
     *   plain junction. Caller must inspect outputs and reconcile/undo partial success. OutError
     *   not cleared on success.
     * @note syncSplitCaches appends new node/link, refreshes original conduit endpoints/bends,
     *   clears stale typed hide/selection for reused names, rebuilds category maps (dropping custom
     *   row order), marks KD/scene/connector caches, recomputes extent/item bounds and emits
     *   repaint, geometryChanged, attributeChanged(original). No explicit modelEdited or undo push.
     */
    bool applyInsertVirtualJunction(const QString &linkName, double t, const QString &newNodeName,
                                    const QString &newLinkName, int *outNodeIdx = nullptr,
                                    int *outLinkIdx = nullptr, QString *outError = nullptr);

    /*!
     * \brief Re-fuse the two conduits of virtual junction \p nodeName into
     *        one via `swmm_virtual_junction_fuse` (upstream conduit's name
     *        survives; the node coordinate becomes an interior vertex unless
     *        collinear; the downstream conduit and node are deleted).
     * @param[in] nodeName Exact engine/cached inserted-junction identifier.
     * @param[out] outError Optional failure message, unchanged on success.
     * @return True after engine fuse and matching incremental cache compaction, false on missing
     *   engine/node/role/topology rejection. Requires current cached virtual role and engine fusion
     *   topology rules.
     * @note Success removes node/downstream conduit, retains upstream name/merged bends, renumbers
     *   source indices, rebuilds category maps/order and spatial/bounds/connector state, emits
     *   repaint/geometryChanged. No explicit modelEdited or undo. Existing borrowed source indices
     *   become stale; failed demotion rollback result is ignored in plain-split inverse.
     */
    bool applyFuseVirtualJunction(const QString &nodeName, QString *outError = nullptr);

    /*!
     * \brief Split conduit \p linkName at \p t inserting a PLAIN junction, via
     *        `swmm_conduit_split` with `make_virtual = 0`. Identical to
     *        applyInsertVirtualJunction except that no virtual-junction rule
     *        validation runs, so this is the split available to models that do
     *        not route with DYNWAVE (virtual junctions require it — rule 619).
     * @param[in] linkName Exact engine conduit identifier also present in cache.
     * @param[in] t Normalized interior polyline split position, finite 0 < t < 1 validated by engine.
     * @param[in] newNodeName New unique node namespace identifier, passed UTF-8.
     * @param[in] newLinkName New unique downstream conduit identifier, passed UTF-8.
     * @param[out] outError Optional failure message, unchanged on success.
     * @param[out] outNodeIdx Optional new global node index, initialized minus one before
     *   validation.
     * @param[out] outLinkIdx Optional new global downstream link index, initialized minus one
     *   before validation.
     * @return True after engine split/cache sync, false on missing engine/conduit/invalid
     *   split/duplicate names. Outputs initialized minus one and assigned only on success. OutError
     *   not cleared on success.
     * @note syncSplitCaches appends new node/link, refreshes original conduit endpoints/bends,
     *   clears stale typed hide/selection for reused names, rebuilds category maps (dropping custom
     *   row order), marks KD/scene/connector caches, recomputes extent/item bounds and emits
     *   repaint, geometryChanged, attributeChanged(original). No explicit modelEdited or undo push.
     */
    bool applyInsertJunctionSplit(const QString &linkName, double t, const QString &newNodeName,
                                  const QString &newLinkName, int *outNodeIdx = nullptr,
                                  int *outLinkIdx = nullptr, QString *outError = nullptr);

    /*!
     * \brief Inverse of applyInsertJunctionSplit — re-fuses the conduit pair
     *        and deletes the junction.
     * \details Both the engine (`vj_fuse`) and applyFuseVirtualJunction guard
     *          the fuse on the virtual flag, so this flags the node virtual
     *          just long enough to reuse that exact inverse, then fuses. The
     *          transient flag never persists: on success the node is deleted,
     *          and on failure the flag is rolled back. Safe because a fresh
     *          split leaves an identical-cross-section, zero-offset through
     *          pair (VJ rules 609/611/613 pass) and the DYNWAVE rule 619 is
     *          enforced only at parse time, never on edit.
     * @param[in] nodeName Exact engine/cached inserted-junction identifier.
     * @param[out] outError Optional failure message, unchanged on success.
     * @return True after engine fuse and matching incremental cache compaction, false on missing
     *   engine/node/role/topology rejection. Temporarily promotes a plain junction to virtual, then
     *   uses virtual fuse; failed fuse attempts demotion rollback when role was newly enabled.
     * @note Success removes node/downstream conduit, retains upstream name/merged bends, renumbers
     *   source indices, rebuilds category maps/order and spatial/bounds/connector state, emits
     *   repaint/geometryChanged. No explicit modelEdited or undo. Existing borrowed source indices
     *   become stale; failed demotion rollback result is ignored in plain-split inverse.
     */
    bool applyFuseJunctionSplit(const QString &nodeName, QString *outError = nullptr);

    /*! \brief Actionable text for a virtual-junction / inlet-junction rule
     *         code (609..621 VJ, 623..635 inlet).
     * @param[in] engineErrorCode Engine virtual/inlet validation error code.
     * @return Localized actionable text for known609..635 odd rule codes, otherwise localized
     *   Engine error N. Does not probe engine state.
     */
    static QString virtualJunctionRuleText(int engineErrorCode);

    // ===== Inlet junctions and inlet usage ================================
    // Engine surface: swmm_node_is_inlet / _inlet_eligible / _set_inlet
    // (openswmm_nodes.h), swmm_inlet_usage_* (openswmm_infrastructure.h),
    // swmm_conduit_split_inlet / swmm_inlet_junction_fuse (openswmm_edit.h).
    // See workplans/INLET_EDITOR_AND_INLET_JUNCTION_GUI_PLAN_2026-09-05.md.

    /*! \brief True when cached node \p soaIdx is an inlet junction. An inlet
     *         junction is also a virtual junction, so callers that branch on
     *         both must test this one FIRST.
     * @param[in] soaIdx Global cached node index.
     * @return True for in-range cached inlet role, false invalid/ordinary node. Inlet is also
     *   virtual; test this first when choosing display overrides.
     */
    [[nodiscard]] bool nodeIsInlet(int soaIdx) const
    {
        return soaIdx >= 0 && soaIdx < m_nodes.size() && m_nodes[soaIdx].isInlet != 0;
    }

    /*! \brief Tri-state capability probe, cached for the layer's lifetime:
     *         true when the bound engine exposes the inlet-junction surface.
     *         Callers hide the add tool / Convert-To entries / usage page
     *         when this is false.
     * @return Cached usage-count availability plus node 0 inlet-flag query success when nodes
     *   exist. No-engine false is not cached; otherwise verdict persists for layer lifetime,
     *   including engine replacement, so it is a feature probe rather than current-row eligibility.
     */
    [[nodiscard]] bool engineSupportsInletJunctions() const;

    /*! \brief Read the inlet-usage row hosted by (\p hostKind, \p hostIdx).
     *  \param hostKind SWMM_INLET_HOST_LINK (0) or SWMM_INLET_HOST_NODE (1).
     *  \returns false when no row exists (\p out untouched).
     * @param[in] hostIdx Nonnegative global host engine index.
     * @param[out] out Required destination, passed to engine getter only after row lookup.
     * @return False missing engine/null output/negative index/no usage/get failure. Any hostKind
     *   other than NODE follows LINK lookup; caller must use documented enum. Failed initial lookup
     *   leaves output untouched; engine getter owns later failure semantics.
     */
    [[nodiscard]] bool inletUsageFor(int hostKind, int hostIdx, SWMM_InletUsage *out) const;

    /*! \brief Create or replace the usage row for \p usage's host via
     *         `swmm_inlet_usage_set`. Emits attributeChanged for the host
     *         object + repaintRequested (the connector overlay follows).
     * @param[in] usage Complete host/design/capture/placement row in engine SWMM_InletUsage
     *   conventions; indices must match current engine, physical values use API project units.
     * @param[out] outError Optional mapped engine failure text, unchanged on success.
     * @return False no engine/rejected set, true after accepted upsert. Invalidates connector
     *   cache, emits modelEdited via markEdited, repaintRequested, and attributeChanged(hostName)
     *   when readable. No undo command unless caller enters through pushInletUsageEdit().
     */
    bool applySetInletUsage(const SWMM_InletUsage &usage, QString *outError = nullptr);

    /*! \brief Remove the usage row hosted by (\p hostKind, \p hostIdx).
     *         A missing row is a successful no-op.
     * @param[in] hostKind NODE or LINK enum; non-NODE follows link lookup.
     * @param[in] hostIdx Global host engine index; missing/invalid lookup can be treated already
     *   absent.
     * @param[out] outError Optional failure message, unchanged for success/no-op.
     * @return True for absent row with no notifications, or after accepted removal; false no
     *   engine/engine rejection. Actual removal dirties connectors, marks model edited, repaints
     *   and notifies host attribute if readable. Does not remove inlet role/topology itself.
     */
    bool applyRemoveInletUsage(int hostKind, int hostIdx, QString *outError = nullptr);

    /*! \brief Set or clear a node's inlet-junction flag via
     *         `swmm_node_set_inlet`. Clearing leaves the node a virtual
     *         junction and drops its usage row (engine contract).
     * @param[in] name Exact engine node name also in GUI cache.
     * @param[in] makeInlet True validates/promotes inlet+virtual role; false clears inlet
     *   role/usage but retains virtual role.
     * @param[out] outError Optional mapped engine failure diagnostic, unchanged on success.
     * @return True after accepted flag/cache change, false no engine/missing node/validation
     *   rejection. Dirties connector/scene caches, marks model edited and emits
     *   repaint/attributeChanged; no undo or split itself. Accepted identical values still notify.
     */
    bool applySetInlet(const QString &name, bool makeInlet, QString *outError = nullptr);

    /*! \brief Split STREET conduit \p linkName at \p t, inserting an inlet
     *         junction wired to \p inletId / \p captureNode, via
     *         `swmm_conduit_split_inlet` (atomic engine-side). Cache sync is
     *         identical to applyInsertVirtualJunction.
     * @param[in] linkName Existing compatible engine street-conduit identifier.
     * @param[in] t Finite normalized interior polyline position0<t<1, validated by engine.
     * @param[in] newNodeName Unique new inlet-junction node identifier.
     * @param[in] newLinkName Unique new downstream conduit identifier.
     * @param[in] inletId Existing compatible inlet-design identifier.
     * @param[in] captureNode Existing eligible receiving node identifier, distinct from inserted
     *   node.
     * @param[out] outNodeIdx Optional inserted engine/node index, initialized minus one before
     *   validation.
     * @param[out] outLinkIdx Optional inserted downstream link index, initialized minus one before
     *   validation.
     * @param[out] outError Optional actionable failure text, unchanged on success.
     * @return True after atomic engine split/usage insertion and GUI cache sync, false rejection
     *   with output indices minus one; engine helper rolls split back on post-split failure.
     * @note Successful cache sync appends/renumbers source state, rebuilds
     *   maps/order/spatial/bounds and emits geometry/repaint/original-attribute signals, then marks
     *   model edited. No undo stack push here.
     */
    bool applyInsertInletJunction(const QString &linkName, double t, const QString &newNodeName,
                                  const QString &newLinkName, const QString &inletId,
                                  const QString &captureNode, int *outNodeIdx = nullptr,
                                  int *outLinkIdx = nullptr, QString *outError = nullptr);

    /*! \brief Inverse of applyInsertInletJunction — drops the usage row and
     *         re-fuses the conduit pair (`swmm_inlet_junction_fuse`).
     * @param[in] nodeName Exact cached inlet-junction identifier.
     * @param[out] outError Optional failure message, unchanged on success.
     * @return True after engine fuse and matching cache compaction, false missing engine/node/inlet
     *   role/topology rejection. Success drops usage/node/downstream conduit, merges retained
     *   upstream geometry, renumbers indices and emits ordinary sync geometry/repaint plus
     *   modelEdited. Caller owns undo/persistence.
     */
    bool applyFuseInletJunction(const QString &nodeName, QString *outError = nullptr);

    /*! One dashed host → capture-node overlay line, in SCENE coordinates
     *  (same space as m_nodeScenePts / the catchment outlet lines, so the
     *  paint loops draw it with no extra transform). Rebuilt lazily from the
     *  engine's usage rows; see inletConnectors(). */
    struct InletConnector {
        QPointF host;    ///< conduit midpoint (link host) or node position
        QPointF capture; ///< capture (underdrain) node position
    };

    /*! \brief The connector overlay geometry, rebuilt on demand when the
     *         usage rows or the geometry changed. Empty when the engine has
     *         no usage rows or predates the inlet-usage API.
     * @return Borrowed lazy vector of scene-map host→capture line endpoints, possibly empty. Link
     *   host is half accumulated SCENE polyline length, node host cached point; capture endpoint
     *   cached node point. Invalid usage/geometry rows skipped. Reference/elements invalidated by
     *   dirty rebuild/layer destruction; GUI-thread const access mutates cache.
     */
    [[nodiscard]] const QVector<InletConnector> &inletConnectors() const;

    /*! \brief Bind the canvas whose undo stack mediated edits push onto.
     *  \details Property adapters have no canvas of their own, but an inlet
     *           usage edit must be undoable exactly like the map-tool edits
     *           (MVC contract: one mutation = one MapCommand). The project
     *           window calls this once with its canvas; without it the layer
     *           still applies edits, just not undoably (headless tests).
     *
     *           Both are defined out-of-line: Qt 6's QPointer<T> instantiates
     *           a static_cast to/from QObject*, so it needs the COMPLETE
     *           MapCanvas — which this header only forward-declares (same
     *           reason as include/ui/panels/legenddock.h).
     * @param[in] canvas Optional borrowed QObject canvas supplying undo stack; QPointer clears
     *   automatically on canvas deletion.
     * @note Copies binding without signals, ownership transfer, command replay or edit. Null forces
     *   direct non-undo fallback.
     */
    void setEditCanvas(MapCanvas *canvas);
    /**
     * @return Borrowed currently bound undo-host canvas or null after explicit clearing/deletion;
     *   caller must not outlive QObject or assume stack exists.
     */
    [[nodiscard]] MapCanvas *editCanvas() const;

    /*! \brief Apply one inlet-usage row as a single undoable step. Pushes a
     *         SetInletUsageCommand on the edit canvas's stack when one is
     *         bound; falls back to applySetInletUsage() otherwise.
     * @param[in] usage Complete engine-unit row copied into undo command or passed to direct
     *   setter.
     * @return True when command pushed to bound undo stack, regardless of command redo failure;
     *   without usable stack returns actual applySetInletUsage result. Push invokes redo
     *   immediately under Qt stack convention; caller must inspect command/UI diagnostics for
     *   rejected edits.
     */
    bool pushInletUsageEdit(const SWMM_InletUsage &usage);
    /*! \brief Undoable counterpart of applyRemoveInletUsage().
     * @param[in] hostKind NODE/LINK enum for row identity.
     * @param[in] hostIdx Global engine host index.
     * @return True when removal command pushed, not proof redo succeeded; without usable canvas
     *   stack returns actual direct-removal result. Command snapshot/undo owns row restoration;
     *   layer owns no extra transaction.
     */
    bool pushInletUsageRemoval(int hostKind, int hostIdx);

    /*! \brief A node's 1D↔2D coupling structure ([2D_NODE_COUPLING]) as the
     *         engine holds it; false for an unknown node.
     * @param[in] node Exact engine node identifier.
     * @param[out] out Required snapshot destination, assigned after successful node/type read.
     * @return nodecoupling2d:: read result: false null engine/output/unknown node/type failure;
     *   true copies authorship/effective type and successfully read nonempty parameter tokens.
     *   Individual parameter-query failures omitted. Tokens retain section units/text through
     *   128-byte reads, no GUI conversion.
     */
    bool nodeCoupling2D(const QString &node, nodecoupling2d::State *out) const;
    /*! \brief Make the engine hold \a state for its node, then emit
     *         attributeChanged and mark the model edited.
     * @param[in] state Desired node row: unauthored resets; authored sets type and canonical
     *   parameter tokens, omitted tokens cleared. Unknown keys ignored.
     * @param[out] error Optional failure detail under nodecoupling2d:: write semantics; may remain
     *   unchanged for null/reset/empty-clear failures.
     * @return Underlying write result; false can leave type/earlier parameters partly changed.
     *   Regardless of success emits attributeChanged(state.node) and modelEdited; no explicit
     *   repaint/undo/rollback. Failed empty-token clears are ignored by helper.
     */
    bool applyNodeCoupling2D(const nodecoupling2d::State &state, QString *error = nullptr);
    /*! \brief Apply several nodes' coupling states as ONE undoable step
     *         (SetNodeCoupling2DCommand on the edit canvas's stack; applied
     *         directly without one). Every state is checked against the
     *         node's allowed types first — nothing is written when one is
     *         illegal. Unchanged nodes are skipped.
     * @param[in] states Ordered desired snapshots, prevalidated for node existence and authored
     *   allowed class type before any writes; duplicates are not consolidated.
     * @param[in] text Undo command display label; unused by direct fallback.
     * @param[out] error Optional prevalidation/direct-write failure detail, not cleared on success.
     * @return False prevalidation failure without writes; true no actual changes or command pushed.
     *   Undo-stack true does not certify redo success. Direct fallback applies changed rows in
     *   input order and stops at first failure with earlier writes retained; no transaction.
     * @note Exact token snapshot comparison determines changes, so numerically equivalent textual
     *   tokens can still edit. Each direct attempted write emits attribute/model notifications even
     *   on failure.
     */
    bool pushNodeCoupling2DEdits(const QVector<nodecoupling2d::State> &states, const QString &text,
                                 QString *error = nullptr);

    /*!
     * \brief Apply interior vertices to a link: engine + cache, rebuilding
     *        the cached polyline from the node endpoints + new interior.
     * @param[in] linkIdx Global cached/engine link index.
     * @param[in] interior Native layer-map bends only, excluding endpoint nodes; coincident
     *   neighbors cleaned. Empty clears bends.
     * @return False invalid index/engine geometry write rejected, true after accepted GUI update
     *   (cache-only allowed without engine).
     * @note Refreshes cached full-polyline bbox and link scene/grid, marks scene rebuild, emits
     *   repaint/modelEdited. Does not recompute overall model extent, refresh batched item box,
     *   auto-conduit length or emit geometryChanged. Caller supplies undo.
     */
    bool applyLinkInteriorVertices(int linkIdx, const QVector<QPointF> &interior);

    /*!
     * \brief Replace a subcatchment's polygon vertices: engine + cache.
     *        Recomputes the centroid as the vertex average.
     * @param[in] idx Global cached/engine subcatchment index.
     * @param[in] vertices Nonempty native layer-map ring; coincident neighbors/redundant closure
     *   cleaned. Empty cleaned ring rejected, but fewer than 3unique points not locally rejected.
     * @return False invalid index/empty input/engine polygon rejection, true after cache update
     *   (cache-only allowed). Engine centroid write is unchecked after accepted polygon, so true
     *   need not guarantee centroid persisted.
     * @note Stores arithmetic vertex mean as centroid, updates native bbox/scene/outlet line, marks
     *   rebuild, recomputes extent and emits repaint/modelEdited. Does not update authored
     *   hydrologic area, explicitly refresh batched item bounds or emit geometryChanged; caller
     *   supplies undo.
     */
    bool applySubcatchVertices(int idx, const QVector<QPointF> &vertices);
    /**
     * @param[in] idx Global cached/engine catch index.
     * @param[in] areaInModelUnits Hydrologic authored area in project area units: acres US
     *   FLOW_UNITS, hectares SI; engine validates.
     * @return True after accepted engine write and attributeChanged(name), false no engine/invalid
     *   index/rejection. Does not change polygon, modelEdited, undo or automatic geometric area
     *   itself.
     */
    bool applySubcatchArea(int idx, double areaInModelUnits);

    /*!
     * \brief Assign a rain gage to a subcatchment: engine + change notification.
     * \details Mirrors \ref applyHydrographSetGage. Until this existed the only
     *          write paths were the property adapter and the attribute table,
     *          both calling `swmm_subcatch_set_gage` directly and emitting only
     *          their own local edit signal — which is why a bulk assignment had
     *          no way to refresh every view. Routing through the layer emits
     *          `attributeChanged` + `modelEdited` like every other mediated
     *          edit.
     * \param idx       Zero-based subcatchment index.
     * \param gageName  Rain gage identifier. Must name an existing gage; an
     *                  empty string is rejected, since SWMM requires a gage.
     * \returns false when the index is out of range, the gage is unknown, or
     *          the engine rejected the write.
     * @note Accepted identical assignment still notifies. Outside bulk emits
     *   attributeChanged(catchName)/modelEdited without explicit repaint; inside bulk sets
     *   dirty/attribute flags so outer close rebuilds geometry once and emits modelEdited. No undo
     *   or clearing of a required gage assignment.
     */
    bool applySubcatchSetGage(int idx, const QString &gageName);

    /*!
     * \brief Announce unit-hydrograph / RDII edits made straight through the
     *        engine (rain-gage assignment). Emits hydrographChanged("") and
     *        modelEdited(), or — inside a bulk scope — defers both to
     *        endBulkEdit so a batch announces once.
     * @note No engine/data validation or mutation. Outside bulk emits broad
     *   hydrographChanged(empty)/modelEdited immediately; inside sets geometry/attribute/hydro
     *   flags so outer close rebuilds caches and announces once. Caller uses this after direct
     *   engine edits.
     */
    void markHydrographsEdited();

    /*!
     * \brief Add a new node: engine + cache. Engine must be OPENED.
     * \param name      Unique null-terminated node identifier.
     * \param nodeType  0=Junction, 1=Outfall, 2=Storage, 3=Divider
     *                  (matches SWMM_NodeType).
     * \param x, y      Initial coordinate in the layer CRS.
     * \param[out] outIdx  Newly assigned node index on success.
     * \returns true on success. On failure \p outIdx is -1.
     * @note Requires editable engine, node type/identifier accepted by engine. Output pointer
     *   optional, initialized minus one before validation. Coordinate setter failure attempts
     *   pop-last rollback (rollback status ignored); post-add index failure returns false without
     *   pop, so false does not universally guarantee no engine mutation.
     * @note Success appends cache, clears stale typed hide/selection for reused name. Bulk scope
     *   defers all derived work; otherwise rebuilds category maps/order, scene point, flags,
     *   KD/bounds/item box and emits repaint/geometryChanged. Does not explicitly emit modelEdited
     *   or push undo. Native XY are layer map units, not scene-Y-flipped or SI normalized.
     */
    bool applyNodeAdd(const QString &name, int nodeType, double x, double y, int *outIdx = nullptr);

    /*!
     * \brief Undo an add by removing the *tail* entry of the node cache.
     * \details Only valid immediately after applyNodeAdd when the new node
     *          is still at the end of the node list. Used by AddNodeCommand:: undo
     *          while the engine lacks a general-purpose swmm_node_remove.
     *          Returns false if the tail name doesn't match \p name.
     * @param[in] name Exact expected tail node name; does not remove an arbitrary non-tail object.
     * @return False no engine/empty cache/name mismatch/engine tail-delete rejection, true after
     *   matching cache removal and scene/category compaction. Engine helper owns reference/cascade
     *   eligibility; caller must preserve cache-engine index invariant.
     * @note Runs immediate derived work even inside a bulk scope; signals can be blocked by outer
     *   scope. Nonbulk emits repaint/geometryChanged, marks rebuild; no modelEdited/undo or
     *   explicit overall extent recomputation. Existing selection/hidden names are not explicitly
     *   removed here.
     */
    bool rollbackTailNodeAdd(const QString &name);

    /*!
     * \brief Add a new link: engine + cache.
     * \param name             Unique identifier.
     * \param linkType         0=Conduit, 1=Pump, 2=Orifice, 3=Weir, 4=Outlet.
     * \param fromNodeName     From-node name (must already exist).
     * \param toNodeName       To-node name (must already exist).
     * \param interiorVertices Interior (non-endpoint) polyline points.
     * \param[out] outIdx      Link index on success.
     * @return True after engine add and GUI append, false for no engine/empty name/engine
     *   reject/missing index/endpoints. Optional output starts minus one. Missing index/endpoints
     *   attempts pop-last rollback; post-add node/vertex setter return codes ignored, so true does
     *   not guarantee every requested field persisted.
     * @note Interior coordinates are native layer-map units; cleans coincident bends, then stores
     *   without endpoints. Clears stale typed state for reused name. Bulk defers derived work;
     *   ordinary path extends scene flat arrays/grid, bounds, item box and emits
     *   repaint/geometryChanged. No modelEdited/undo itself.
     */
    bool applyLinkAdd(const QString &name, int linkType, const QString &fromNodeName,
                      const QString &toNodeName, const QVector<QPointF> &interiorVertices,
                      int *outIdx = nullptr);

    /*! Undo tail link add (swmm_link_pop_last).
     * @param[in] name Exact expected tail link name; does not remove an arbitrary non-tail object.
     * @return False no engine/empty cache/name mismatch/engine tail-delete rejection, true after
     *   matching cache removal and scene/category compaction. Engine helper owns reference/cascade
     *   eligibility; caller must preserve cache-engine index invariant.
     * @note Runs immediate derived work even inside a bulk scope; signals can be blocked by outer
     *   scope. Nonbulk emits repaint/geometryChanged, marks rebuild; no modelEdited/undo or
     *   explicit overall extent recomputation. Existing selection/hidden names are not explicitly
     *   removed here.
     */
    bool rollbackTailLinkAdd(const QString &name);

    /*!
     * \brief Move a rain gage: engine `[SYMBOLS]` coordinate + cached scene
     *        point, in lockstep (a bare `swmm_spatial_set_gage_coord` would
     *        leave the canvas stale until the next geometry rebuild). The
     *        rain-gage twin of \ref applyNodeMove; gages carry no attached
     *        links, so there is no bbox pass.
     * \param idx         Cache/engine gage index.
     * \param newX, newY  New coordinate in the layer CRS.
     * \returns           true on success. Emits repaintRequested() and
     *                    modelEdited() ([SYMBOLS] is authored data).
     * @return False invalid cached index/engine setter failure, true after engine/cache update
     *   (cache-only accepted if engine null). No facade finite/range validation.
     * @note Rewrites transformed/Y-inverted scene point if present, marks KD/scene dirty, bumps
     *   geometry revision, recomputes extent, emits repaint/modelEdited. Does not explicitly
     *   refresh batched item bbox or emit geometryChanged; caller owns undo.
     */
    bool applyGageMove(int idx, double newX, double newY);

    /*! Add a rain gage: engine + cache.
     * @param[in] name Nonempty unique engine rain-gage namespace identifier, passed UTF-8.
     * @param[in] x Authored native layer-map X, not scene-Y-flipped.
     * @param[in] y Authored native layer-map Y; engine validates coordinate.
     * @param[out] outIdx Optional new engine index, initialized minus one before validation.
     * @return False on no engine/empty name/add/index/coordinate failure; failed post-add steps
     *   attempt delete rollback. True appends GUI row and reports index.
     * @note Unlike node/link add, this path does not clear stale typed state for a reused name.
     *   Bulk defers scene work; otherwise appends scene point/maps, bounds, item box and emits
     *   repaint/geometryChanged. No modelEdited or undo itself.
     */
    bool applyGageAdd(const QString &name, double x, double y, int *outIdx = nullptr);

    /*! Undo tail gage add (swmm_gage_delete on tail).
     * @param[in] name Exact expected tail gage name; does not remove an arbitrary non-tail object.
     * @return False no engine/empty cache/name mismatch/engine tail-delete rejection, true after
     *   matching cache removal and scene/category compaction. Engine helper owns reference/cascade
     *   eligibility; caller must preserve cache-engine index invariant.
     * @note Only gage rollback honors active bulk scope by deferring derived work. Nonbulk emits
     *   repaint/geometryChanged, marks rebuild; no modelEdited/undo or explicit overall extent
     *   recomputation. Existing selection/hidden names are not explicitly removed here.
     */
    bool rollbackTailGageAdd(const QString &name);

    /*! Add a subcatchment polygon: engine + cache.
     * @param[in] name Nonempty unique engine catchment namespace identifier.
     * @param[in] polygon Native layer-map polygon vertices; coincident points/redundant closing
     *   point cleaned, empty/degenerate ring still accepted by facade.
     * @param[out] outIdx Optional new engine index, initialized minus one before validation.
     * @return False no engine/empty name/add/index failure, index failure attempts delete rollback.
     *   True can follow unchecked polygon/centroid setter failure, so does not certify complete
     *   geometry persistence.
     * @note Nonempty ring centroid stored as arithmetic vertex mean, not area centroid. Appends
     *   cache without clearing stale typed state; bulk defers work, ordinary path extends
     *   scene/bounds and emits repaint/geometryChanged. No auto authored area, modelEdited or undo.
     */
    bool applySubcatchAdd(const QString &name, const QVector<QPointF> &polygon,
                          int *outIdx = nullptr);

    /*! Undo tail subcatch add (swmm_subcatch_delete on tail).
     * @param[in] name Exact expected tail catchment name; does not remove an arbitrary non-tail
     *   object.
     * @return False no engine/empty cache/name mismatch/engine tail-delete rejection, true after
     *   matching cache removal and scene/category compaction. Engine helper owns reference/cascade
     *   eligibility; caller must preserve cache-engine index invariant.
     * @note Runs immediate derived work even inside a bulk scope; signals can be blocked by outer
     *   scope. Nonbulk emits repaint/geometryChanged, marks rebuild; no modelEdited/undo or
     *   explicit overall extent recomputation. Existing selection/hidden names are not explicitly
     *   removed here.
     */
    bool rollbackTailSubcatchAdd(const QString &name);

    /*!
     * \brief Rename any network element (node, link, subcatchment, or gage).
     * \details Calls the appropriate engine rename function, updates all GUI
     *          caches (geometry cache name, selection set, object-location
     *          map), and emits repaintRequested() + geometryChanged().
     * \param oldName  Current name of the element.
     * \param newName  Desired new name (must not already exist).
     * \param kindHint SWMM names are per-type namespaces, so \p oldName may
     *                 match more than one kind at once (e.g. a rain gage and
     *                 a subcatchment both named "S1"). Pass exactly one of
     *                 kKindNode/kKindLink/kKindGage/kKindCatch when the
     *                 caller knows which kind it means — every UI call site
     *                 does, since a rename is always initiated from a
     *                 kind-specific editor/row. The default, kKindAll, means
     *                 "caller doesn't know" and falls back to a best-effort
     *                 guess (node, then link, then gage, then catchment —
     *                 first match wins), matching this method's pre-existing
     *                 behaviour for any not-yet-migrated caller.
     * \returns        true on success; false if oldName is not found (under
     *                 \p kindHint, when given), newName is empty, newName is
     *                 already in use, or the engine rejects the rename.
     * @note Invalid/unroundtrippable whitespace/semicolon/bracket name, empty/equal names or engine
     *   rejection returns false. Exactly one known hint bit restricts lookup; ANY other mask
     *   (including0/composite) falls back node→link→gage→catch, not a filtered composite search.
     * @note Success patches source names and namespace-aware indices/hidden/selection, splitting
     *   multi-kind selected entries so other same-named types remain old. Marks rebuild, emits
     *   repaint/geometryChanged/objectRenamed(old, new, renamedBit), not
     *   selectionChanged/modelEdited/undo. Engine rename owns cross-reference updates; geometry
     *   coordinates unchanged.
     */
    bool applyRename(const QString &oldName, const QString &newName, quint8 kindHint = kKindAll);

    // ===== Bulk edit — coalesce the per-object cache-rebuild storm =======
    //
    // Every applyXxxAdd / applyXxxDelete rebuilds the whole derived cache
    // (category index, model extent, link spatial grid) and then emits
    // repaintRequested() + geometryChanged(), whose listeners each do
    // O(model) work of their own. That is affordable for one object and
    // quadratic for a selection: deleting 100 junctions from a 104k-node
    // model costs ~21 s, of which only ~4.6 s is the engine.
    //
    // Inside a bulk edit the engine call and the SoA removal/append still
    // happen IMMEDIATELY — only the derived work is deferred. That is
    // deliberate: it keeps the "SoA index == engine index" invariant true
    // at every instant, which the rest of this class relies on.
    //
    // Always use the RAII BulkEdit guard; never call beginBulkEdit() /
    // endBulkEdit() by hand. A batch that begins and never ends would
    // leave the layer permanently signal-blocked with stale caches.
    class BulkEdit
    {
    public:
        /**
         * @param[in] layer Optional borrowed layer guarded by QPointer; nonnull begins a nested
         *   bulk scope immediately.
         * @note Guard must stay on owning GUI thread and outlive all intended mutations. No engine
         *   snapshot/rollback; destruction closes scope if layer remains alive.
         */
        explicit BulkEdit(SWMMModelLayer *layer) : m_layer(layer)
        {
            if (m_layer)
                m_layer->beginBulkEdit();
        }
        /**
         * @brief End bound bulk scope if layer remains alive, performing outermost cache
         *   rebuild/notifications; null/deleted owner is a no-op.
         */
        ~BulkEdit()
        {
            if (m_layer)
                m_layer->endBulkEdit();
        }
        /**
         * @brief Copy construction disabled so one guard cannot close the same bulk scope twice.
         */
        BulkEdit(const BulkEdit &) = delete;
        /**
         * @brief Copy assignment disabled; scope ownership cannot transfer through assignment.
         */
        BulkEdit &operator=(const BulkEdit &) = delete;

    private:
        QPointer<SWMMModelLayer> m_layer;
    };

    /*! Open a bulk-edit scope. Nestable; only the outermost pair does work.
     *  Prefer the BulkEdit guard.
     * @note Outermost entry resets dirty/attribute/hydro flags and calls blockSignals(true); nested
     *   entries only increment depth. Does not preserve previous signalsBlocked state.
     *   Engine/source edits still commit immediately; queries/paint must wait until derived caches
     *   rebuilt. No transaction/rollback.
     */
    void beginBulkEdit();

    /*! Close a bulk-edit scope. On the outermost close, rebuilds every
     *  derived cache once and emits repaintRequested() + geometryChanged()
     *  exactly once — but only if a mutation actually occurred.
     * @note Unbalanced close no-op; inner close decrements only. Outermost unblocks signals
     *   unconditionally, even if previously blocked. If dirty, synchronizes engine endpoint
     *   indices, rebuilds all geometry/maps/order/flags, refreshes item bbox and emits
     *   repaint/geometryChanged, then deferred hydrographChanged(empty) and modelEdited when their
     *   flags set. Cache rebuild can be expensive; no rollback.
     */
    void endBulkEdit();

    /*! True while a bulk-edit scope is open. The applyXxxAdd/Delete
     *  helpers consult this to skip their per-object derived work.
     * @return Whether nesting depth is positive, not whether mutations occurred or engine has an
     *   atomic transaction.
     */
    [[nodiscard]] bool bulkEditActive() const { return m_bulkDepth > 0; }

    /*!
     * \brief Delete a node, cascade-deleting all attached links.
     * \details Identifies cascade links before deletion so the caller can
     *          snapshot them. Modifies engine state + all caches.
     * \param name                 Node to delete.
     * \param[out] cascadeLinkNames Names of links deleted as cascade.
     * @return False no engine/missing node/delete rejection, true after engine cascade and matching
     *   immediate source removals. Optional cascadeLinkNames is APPENDED with predelete link names
     *   from impact report, not cleared; not a pre-deletion snapshot callback.
     * @note Node/link source indices renumber immediately. Bulk defers derived arrays/signals;
     *   ordinary path compacts scenes, rebuilds category maps/order, extent and emits
     *   repaint/geometryChanged. Retained typed selection/hidden names can outlive deleted objects
     *   until explicit cleanup/reuse. No modelEdited/undo itself.
     */
    bool applyNodeDelete(const QString &name, QStringList *cascadeLinkNames = nullptr);

    /*! Delete a single link.
     * @param[in] name Exact existing link engine namespace identifier.
     * @return False no engine/missing object/engine rejection, true after source removal and index
     *   compaction. Engine applies reference/cascade rules; GUI assumes engine/source arrays were
     *   coherent before call.
     * @note Bulk defers derived work; otherwise compacts matching scene arrays, rebuilds category
     *   maps/order, marks scene rebuild and emits repaint/geometryChanged. Does not explicitly
     *   recompute overall extent, remove typed selected/hidden names, emit modelEdited or create
     *   undo.
     */
    bool applyLinkDelete(const QString &name);

    /*! Delete a rain gage.
     * @param[in] name Exact existing gage engine namespace identifier.
     * @return False no engine/missing object/engine rejection, true after source removal and index
     *   compaction. Engine applies reference/cascade rules; GUI assumes engine/source arrays were
     *   coherent before call.
     * @note Bulk defers derived work; otherwise compacts matching scene arrays, rebuilds category
     *   maps/order, marks scene rebuild and emits repaint/geometryChanged. Does not explicitly
     *   recompute overall extent, remove typed selected/hidden names, emit modelEdited or create
     *   undo.
     */
    bool applyGageDelete(const QString &name);

    /*! Delete a subcatchment.
     * @param[in] name Exact existing subcatchment engine namespace identifier.
     * @return False no engine/missing object/engine rejection, true after source removal and index
     *   compaction. Engine applies reference/cascade rules; GUI assumes engine/source arrays were
     *   coherent before call.
     * @note Bulk defers derived work; otherwise compacts matching scene arrays, rebuilds category
     *   maps/order, marks scene rebuild and emits repaint/geometryChanged. Does not explicitly
     *   recompute overall extent, remove typed selected/hidden names, emit modelEdited or create
     *   undo.
     */
    bool applySubcatchDelete(const QString &name);

    /*!
     * \brief Batch deletion (perf-plan Phase A2): one swmm_*_delete_many
     *        engine call per kind, one order-preserving SoA sweep per vector.
     * \details Replaces K per-object deletes whose dominant cost was the
     *          engine's per-delete name-index rehash plus K O(N) removeAt
     *          calls.  Nodes delete first (cascading their links); link names
     *          are resolved AFTER that batch, so a selected link the cascade
     *          already removed is skipped, matching the per-object path.
     *          Opens its own nestable BulkEdit scope — inside a
     *          BulkEditCommand it joins the outer scope; called bare it
     *          supplies the single rebuild+repaint itself.
     * \param[out] cascadeLinkNames Names of links the node batch cascaded.
     * \returns true when anything was deleted.
     * @param[in] nodeNames Node namespace names resolved before node batch; unknown names ignored.
     * @param[in] linkNames Link names resolved AFTER node cascade, already-deleted links ignored.
     * @param[in] subcatchNames Catchment names resolved against current engine.
     * @param[in] gageNames Rain-gage names resolved after earlier batches.
     * @note Executes kind batches node→link→catch→gage inside nested BulkEdit, continues after any
     *   failed kind. True means at least one kind batch succeeded, not every requested object
     *   deleted; false can indicate no matching input or failures. Successful kind sweeps preserve
     *   survivor order and append node-cascade link names to optional output. No
     *   transaction/rollback, error list or modelEdited/undo itself.
     */
    bool applyDeleteMany(const QStringList &nodeNames, const QStringList &linkNames,
                         const QStringList &subcatchNames, const QStringList &gageNames,
                         QStringList *cascadeLinkNames = nullptr);

    // ===== Slice BS Phase 6.9.2 — hydrograph + RDII decay MVC layer ======
    //
    // Every mutation to [HYDROGRAPHS] / [RDII_DECAY] data routes through one
    // of the helpers below — never call `swmm_hydrograph_*` /
    // `swmm_rdii_decay_*` directly from a dialog. On success each helper
    // emits hydrographChanged(uhName) so all subscribed Qt models refresh in
    // lock-step. The model layer is the single Qt-side mediator over the
    // engine's BS-02 C API.
    //
    // See: docs/GUI_IMPLEMENTATION_PLAN.md Slice BS Phase 6.9.2.

    /*! Create a new UH group with a gage assignment and one initial response
     *  row keyed at month=-1 (ALL). Mirrors the existing
     *  NewDataObjectDialog(Hydrographs) seed flow but folded into the model
     *  layer so a single hydrographChanged() fires.
     * @param[in] name Nonempty UH group identifier, passed UTF-8.
     * @param[in] gageName Optional existing gage identifier; empty leaves assignment
     *   unchanged/unset.
     * @param[in] initialResponse Response ordinal 0 short, 1 medium, 2 long; other values rejected
     *   locally.
     * @return True after ALL-month RTK seed(0, 0, 1) and optional gage accepted; false no
     *   engine/empty name/invalid response/rejection. Existing row can be upserted rather than
     *   requiring a new group. Gage failure can leave seeded row committed and no change signal. No
     *   transaction.
     * @note Accepted mutation emits hydrographChanged(name) only; no explicit modelEdited, undo,
     *   persistence or runtime RDII cache rebuild. Inside a generic bulk scope this signal is
     *   blocked without setting deferred hydrograph flag; call markHydrographsEdited() when the
     *   caller requires deferred edit notification.
     */
    bool applyHydrographAddGroup(const QString &name, const QString &gageName, int initialResponse);

    /*! Remove a UH group: parameter rows, gage assignment, [RDII_DECAY]
     *  rows, and any [RDII] node assignments referencing it.
     * @param[in] name Nonempty group identifier, engine uses case-insensitive group identity.
     * @return False no engine/empty name/rejection, true after accepted removal, including engine
     *   idempotent absence behavior. Drops parameter/gage/decay/node-RDII references. Emits
     *   hydrographChanged(empty) for broad refresh, not modelEdited/undo itself.
     */
    bool applyHydrographRemoveGroup(const QString &name);

    /*! Rename a UH group; walks all four engine containers (entries,
     *  gage_assignments, rdii_decay, rdii_assigns). Rejects empty / duplicate
     *  names.
     * @param[in] oldName Nonempty current group name; GUI enumeration compares exact UTF-8 case
     *   through 64-byte ID buffer.
     * @param[in] newName Nonempty different new group identifier; engine validates
     *   duplicate/reference semantics.
     * @return False missing engine/empty/equal/not found/rejection, true after accepted engine
     *   rename. Enumeration can miss overlong/case-different names even though engine rename itself
     *   is case-insensitive. Emits hydrographChanged(empty), no modelEdited/undo.
     */
    bool applyHydrographRenameGroup(const QString &oldName, const QString &newName);

    /*! Set, replace, or clear the rain gage assigned to a UH group. Pass an
     *  empty string to clear an existing assignment.
     * @param[in] name Nonempty group identifier.
     * @param[in] gageName Assigned gage identifier; empty passes null to clear assignment.
     * @return True engine accepted, false no engine/empty name/rejection.
     * @note Accepted mutation emits hydrographChanged(name) only; no explicit modelEdited, undo,
     *   persistence or runtime RDII cache rebuild. Inside a generic bulk scope this signal is
     *   blocked without setting deferred hydrograph flag; call markHydrographsEdited() when the
     *   caller requires deferred edit notification.
     */
    bool applyHydrographSetGage(const QString &name, const QString &gageName);

    /*! Upsert R/T/K for one (group, month, response) row.
     * @param[in] name Nonempty source UH group key, case-insensitive row identity.
     * @param[in] month Minus one ALL, or0..11 January..December.
     * @param[in] response 0 short, 1 medium, 2 long.
     * @param[in] r Rainfall-response fraction, dimensionless; facade/API do not independently
     *   validate numeric range.
     * @param[in] t Time to peak hours; numeric value forwarded without finite/range clamp.
     * @param[in] k Dimensionless recession/peak-time ratio, base duration t*(1+k); numeric value
     *   forwarded unchecked.
     * @return True after row upsert, false invalid engine/key/month/response/rejection. Numeric
     *   values are not range/finiteness checked by this facade/current key-based setter; caller
     *   must enforce sensible physical values. Missing row appends with other fields zero.
     * @note Accepted mutation emits hydrographChanged(name) only; no explicit modelEdited, undo,
     *   persistence or runtime RDII cache rebuild. Inside a generic bulk scope this signal is
     *   blocked without setting deferred hydrograph flag; call markHydrographsEdited() when the
     *   caller requires deferred edit notification.
     */
    bool applyHydrographSetRtk(const QString &name, int month, int response, double r, double t,
                               double k);

    /*! Upsert linear-IA (dmax, drecov, dinit) for one (group, month,
     *  response) row.
     * @param[in] name Nonempty source UH group key, case-insensitive row identity.
     * @param[in] month Minus one ALL, or0..11 January..December.
     * @param[in] response 0 short, 1 medium, 2 long.
     * @param[in] dmax Maximum abstraction in project rainfall-depth units (inches US, millimetres
     *   SI).
     * @param[in] drecov Linear abstraction recovery in project rainfall-depth units per day.
     * @param[in] dinit Initial already-used abstraction in project rainfall-depth units.
     * @return True after row upsert, false invalid engine/key/month/response/rejection. Numeric
     *   values are not range/finiteness checked by this facade/current key-based setter; caller
     *   must enforce sensible physical values. Missing row appends with other fields zero.
     * @note Accepted mutation emits hydrographChanged(name) only; no explicit modelEdited, undo,
     *   persistence or runtime RDII cache rebuild. Inside a generic bulk scope this signal is
     *   blocked without setting deferred hydrograph flag; call markHydrographsEdited() when the
     *   caller requires deferred edit notification.
     */
    bool applyHydrographSetIa(const QString &name, int month, int response, double dmax,
                              double drecov, double dinit);

    /*! Remove one (group, month, response) parameter row. Idempotent.
     * @param[in] name Nonempty UH group key.
     * @param[in] month Minus one ALL, or0..11 January..December.
     * @param[in] response 0 short, 1 medium, 2 long.
     * @return True after engine accepted idempotent removal, false no engine/empty name/invalid
     *   key/rejection.
     * @note Accepted mutation emits hydrographChanged(name) only; no explicit modelEdited, undo,
     *   persistence or runtime RDII cache rebuild. Inside a generic bulk scope this signal is
     *   blocked without setting deferred hydrograph flag; call markHydrographsEdited() when the
     *   caller requires deferred edit notification.
     */
    bool applyHydrographRemoveEntry(const QString &name, int month, int response);

    /*! Bulk-clear every per-month parameter row for a group, preserving any
     *  existing month=-1 (ALL) row. Used by the editor when switching from
     *  per-season to ALL.
     * @param[in] name Nonempty UH group identifier.
     * @return True after engine removes per-month rows while retaining ALL entries, false no
     *   engine/empty name/rejection.
     * @note Accepted mutation emits hydrographChanged(name) only; no explicit modelEdited, undo,
     *   persistence or runtime RDII cache rebuild. Inside a generic bulk scope this signal is
     *   blocked without setting deferred hydrograph flag; call markHydrographsEdited() when the
     *   caller requires deferred edit notification.
     */
    bool applyHydrographClearMonths(const QString &name);

    /*! Upsert one [RDII_DECAY] row for a (group, response) pair. The last
     *  three arguments configure the optional degree-day snow model
     *  (snowOn=false leaves the row snow-free).
     * @param[in] name Nonempty UH identifier, exact-case group/response identity in engine decay
     *   rows.
     * @param[in] response 0 short, 1 medium, 2 long.
     * @param[in] k_dep Nonnegative depletion coefficient, inverse project rainfall-depth units.
     * @param[in] k_0 Nonnegative base recovery rate per hour.
     * @param[in] k_T Nonnegative thermal recovery rate at reference temperature, per hour.
     * @param[in] T_ref Reference temperature degrees Celsius.
     * @param[in] theta_rec Thermal recovery sensitivity, inverse degrees Celsius.
     * @param[in] T_freeze Temperature below which recovery is suppressed, degrees Celsius.
     * @param[in] snowOn Enable optional degree-day snow behavior; stored engine flag0/1.
     * @param[in] snow_T Rain/snow partition and melt-base temperature degrees Celsius.
     * @param[in] snow_ddf Nonnegative degree-day melt factor, project rain-depth units per degree
     *   Celsius per day.
     * @return True after accepted upsert, false no engine/empty name/response/rate rejection.
     *   Supply finite values: negative-rate comparisons exist but no independent finiteness guard.
     *   Group existence is not resolved here.
     * @note Accepted mutation emits hydrographChanged(name) only; no explicit modelEdited, undo,
     *   persistence or runtime RDII cache rebuild. Inside a generic bulk scope this signal is
     *   blocked without setting deferred hydrograph flag; call markHydrographsEdited() when the
     *   caller requires deferred edit notification.
     */
    bool applyRdiiDecaySet(const QString &name, int response, double k_dep, double k_0, double k_T,
                           double T_ref, double theta_rec, double T_freeze, bool snowOn = false,
                           double snow_T = 1.0, double snow_ddf = 0.0);

    /*! Remove the [RDII_DECAY] row for a (group, response) pair. Idempotent.
     *  In the GUI's MVC layer this is the "untick Active" path.
     * @param[in] name Nonempty exact-case decay-row group identifier.
     * @param[in] response 0 short, 1 medium, 2 long.
     * @return Underlying idempotent removal success, false missing engine/empty name/invalid
     *   response/rejection.
     * @note Accepted mutation emits hydrographChanged(name) only; no explicit modelEdited, undo,
     *   persistence or runtime RDII cache rebuild. Inside a generic bulk scope this signal is
     *   blocked without setting deferred hydrograph flag; call markHydrographsEdited() when the
     *   caller requires deferred edit notification.
     */
    bool applyRdiiDecayRemove(const QString &name, int response);

    /*! Shared QAbstractListModel of UH group names. Lazily constructed; one
     *  per layer. Re-emits modelReset() on hydrographChanged("").
     * @return Borrowed lazily constructed layer QObject child, shared across all views. Table
     *   callers setContext to select group/month, which changes every view using the same model; do
     *   not delete/outlive layer. GUI-thread only.
     */
    HydrographGroupListModel *hydrographGroupListModel();

    /*! Shared QAbstractTableModel for one (group, month) R/T/K grid (3 rows
     *  × 4 cols). Call setContext() to rebind.
     * @return Borrowed lazily constructed layer QObject child, shared across all views. Table
     *   callers setContext to select group/month, which changes every view using the same model; do
     *   not delete/outlive layer. GUI-thread only.
     */
    HydrographRtkTableModel *hydrographRtkModel();

    /*! Shared QAbstractTableModel for one (group, month) linear-IA grid (3
     *  rows × 4 cols). Call setContext() to rebind.
     * @return Borrowed lazily constructed layer QObject child, shared across all views. Table
     *   callers setContext to select group/month, which changes every view using the same model; do
     *   not delete/outlive layer. GUI-thread only.
     */
    HydrographIaTableModel *hydrographIaModel();

    /*! Shared QAbstractTableModel for one group's [RDII_DECAY] grid (3 rows
     *  × 8 cols). Call setContext() to rebind. Season-agnostic — there is
     *  no month dimension in the engine model.
     * @return Borrowed lazily constructed layer QObject child, shared across all views. Table
     *   callers setContext to select group/month, which changes every view using the same model; do
     *   not delete/outlive layer. GUI-thread only.
     */
    HydrographDecayTableModel *hydrographDecayModel();

    /*! Lazy-init / re-use the TimeseriesRegistry scoped to the current
     *  engine handle. Returns nullptr if the engine isn't open. Returned
     *  as a `QObject*` to keep this header free of the timeseries include;
     *  callers downcast to `openswmmvis:: timeseries:: TimeseriesRegistry*`.
     *  Shared across every UI that mutates time series so all views
     *  (Object Browser, NodeCompoundEditDialog pickers, …) see the same
     *  provider instances.
     * @return Borrowed typed timeseries registry exposed as QObject, or null without current
     *   engine. Current engine identity controls lazy reuse; changed identity schedules previous
     *   registry deleteLater and creates/loads a new QObject child. Borrowed providers may become
     *   invalid after event-loop deletion.
     * @note Replacement seeds before wiring edit signals, then registry membership/rename changes
     *   emit dataObjectsChanged/modelEdited and provider content changes mark edited. Membership
     *   changes also dirty unified-table partition; deferred saveToEngine flush is not itself dirty
     *   tracking. Every fetch updates project anchor from model path and parses simulation start
     *   options; optionsChanged keeps start live while editor open. Same engine/count does not
     *   force reload; GUI-thread only.
     */
    QObject *ensureTimeseriesRegistry();

    /*! Same lazy-init pattern as ensureTimeseriesRegistry, for the
     *  PatternRegistry.
     * @return Borrowed typed pattern registry exposed as QObject, or null without current engine.
     *   Current engine identity controls lazy reuse; changed identity schedules previous registry
     *   deleteLater and creates/loads a new QObject child. Borrowed providers may become invalid
     *   after event-loop deletion.
     * @note Replacement seeds before wiring edit signals, then registry membership/rename changes
     *   emit dataObjectsChanged/modelEdited and provider content changes mark edited. Registry
     *   attaches engine after initial load for immediate staged provider creation/removal behavior;
     *   factor/type edits mark model edited. Same engine/count does not force reload; GUI-thread
     *   only.
     */
    QObject *ensurePatternRegistry();

    /*! Same lazy-init pattern, for the CurveRegistry.
     * @return Borrowed typed curve registry exposed as QObject, or null without current engine.
     *   Current engine identity controls lazy reuse; changed identity schedules previous registry
     *   deleteLater and creates/loads a new QObject child. Borrowed providers may become invalid
     *   after event-loop deletion.
     * @note Replacement seeds before wiring edit signals, then registry membership/rename changes
     *   emit dataObjectsChanged/modelEdited and provider content changes mark edited. Membership
     *   changes also dirty unified-table partition; deferred saveToEngine flush is not itself dirty
     *   tracking. Same engine/count does not force reload; GUI-thread only.
     */
    QObject *ensureCurveRegistry();

    /*! Push every curve and pattern the registries hold into the engine.
     *  Called right before the model is written, so a modeless editor that
     *  is still open cannot keep its edits out of the saved .inp. Registries
     *  that were never created are left alone; the flush does not mark the
     *  project edited (see ensureCurveRegistry).
     * @note Only already-created curve/pattern registries bound to CURRENT engine are flushed;
     *   never creates them, no-engine no-op. saveToEngine results ignored, so partial/rejected
     *   writes are not surfaced. No timeseries/other registry flush, no direct modelEdited or
     *   persistence here.
     */
    void flushDataObjectRegistries();

    // ===== Slice BR Phase 6.8.1 — control-rule MVC layer ====================
    //
    // Mirrors the hydrograph pattern at Phase 6.9.2 (`applyHydrograph*` +
    // `hydrographChanged`) and the curve/pattern/timeseries `ensureXRegistry`
    // accessors above. The four `applyControlRule*` helpers are the **only**
    // place engine `swmm_control_*` mutation calls live; all UI surfaces
    // (RulesEditorDialog, SWMMControlRulePropertyAdapter, Object Browser)
    // route through them and re-render off the `controlRulesChanged(name)`
    // signal.
    //
    // The engine has no per-rule mutator (DA-ENG-11), so each apply helper
    // snapshots every rule, modifies the target slot in the snapshot, calls
    // `swmm_control_clear_rules`, and re-`swmm_control_add_rule`s the
    // snapshot. This matches today's `SWMMControlRulePropertyAdapter
    // ::setRuleText` round-trip.

    /*! \brief Append a new control rule. `body` should already include the
     *  `RULE <name>` header (matching the engine's storage format); the
     *  apply helper does not synthesise one. Returns false if the engine
     *  is closed, `name` is empty, or the registry already holds it.
     * @param[in] name Nonempty registry identity used for case-insensitive duplicate check; caller
     *   must keep it identical to body RULE header.
     * @param[in] body Complete rule text including valid matching RULE header; facade does not
     *   synthesize/verify it in this add path.
     * @param[out] outError Optional failure diagnostic, unchanged on success.
     * @return True after engine append and registry create, false missing engine/empty supplied
     *   name/duplicate registry name/rejection. Engine parses identity from body, so mismatched
     *   header can diverge registry identity. Emits controlRulesChanged(name), no explicit
     *   modelEdited/undo.
     */
    bool applyControlRuleAdd(const QString &name, const QString &body, QString *outError = nullptr);

    /*! \brief Replace the body of an existing rule (typically the user-
     *  edited text from the code editor). Routes through the snapshot+clear
     *  +re-add round-trip so the engine's rule order is preserved.
     * @param[in] name Existing parsed engine rule identity, case-insensitive match.
     * @param[in] newBody Complete rule text with valid roundtrippable RULE header; different header
     *   implicitly renames.
     * @param[out] outError Optional failure reason, unchanged on success.
     * @return True after validated header/uniqueness, snapshot list rewrite and provider mirror;
     *   false missing key/read/rewrite error. Clear/re-add rewrite attempts best-effort original
     *   restoration on failure, but is not atomic.
     * @note Preserves rule order; body snapshot has8192-byte limit, IDs256bytes. Renamed header
     *   emits broad controlRulesChanged(empty), otherwise name-specific. Provider body/rename calls
     *   do not provide checked rollback; no explicit modelEdited/undo here.
     */
    bool applyControlRuleReplace(const QString &name, const QString &newBody,
                                 QString *outError = nullptr);

    /*! \brief Rename `oldName` to `newName`. Rewrites the `RULE <name>`
     *  header on the target slot's body so the engine round-trip preserves
     *  the rename. Refuses on duplicate (case-insensitive) or empty
     *  `newName`.
     * @param[in] oldName Nonempty existing parsed rule identity, case-insensitive matching.
     * @param[in] newName Nonempty roundtrippable unique rule identifier, validated before edits.
     * @param[out] outError Optional failure diagnostic, unchanged on success.
     * @return True after mirror/list rename, false invalid/duplicate/missing/snapshot/rewrite
     *   rejection. Case-only/equal-insensitive branch updates registry identity only and can
     *   succeed even when old rule is absent; engine body casing remains unchanged.
     * @note Other renames rewrite first RULE token or synthesize header when missing, preserve
     *   remaining rule order and attempt best-effort rollback on engine add failure. Emits newName
     *   for case-only, broad empty otherwise; no explicit modelEdited/undo.
     */
    bool applyControlRuleRename(const QString &oldName, const QString &newName,
                                QString *outError = nullptr);

    /*! \brief Drop the rule with `name` from the engine + registry.
     *  Returns false if not found.
     * @param[in] name Nonempty parsed rule identity matched case-insensitively.
     * @param[out] outError Optional failure reason, unchanged on success.
     * @return True after removing first matching rule from rewritten engine list and registry,
     *   false absent/no-engine/snapshot/rewrite failure. Failed rewrite attempts original
     *   restoration without guarantee; not idempotent absence. Emits broad
     *   controlRulesChanged(empty), not explicit modelEdited/undo.
     */
    bool applyControlRuleRemove(const QString &name, QString *outError = nullptr);

    /*! \brief Lazy accessor for the project-scoped `ControlRuleRegistry`.
     *  Lifetime is bound to the layer; the registry is constructed on
     *  first call and `loadFromEngine` is invoked so the providers mirror
     *  the engine's current rule list. Returned as a `QObject*` to keep
     *  this header free of the controls include; callers downcast to
     *  `openswmmvis:: controls:: ControlRuleRegistry*`.
     * @return Borrowed ControlRule registry exposed as QObject, or null without engine. Reuses only
     *   matching engine identity; otherwise schedules old child deleteLater and creates/loads
     *   replacement before wiring notifications. Old providers can invalidate after event-loop
     *   deletion.
     * @note No layer dirty/dataObjects notification hooks are installed by this accessor; mediated
     *   applyControlRule methods emit controlRulesChanged. Same-engine fetch does not force reload;
     *   GUI-thread only.
     */
    QObject *ensureControlRuleRegistry();

    // ===== Slice BQ Phase 6.7.4 — transect MVC layer =========================
    //
    // Mirrors the curve / pattern / control-rule pattern. The applyTransect*
    // helpers are the only place engine `swmm_transect_*` mutation calls
    // live; all UI surfaces (TransectEditorDialog, SWMMTransectPropertyAdapter,
    // Object Browser) route through them and re-render off
    // `transectChanged(name)`.

    /*! \brief Create a new transect. Refuses on duplicate or empty name.
     * @param[in] name Nonempty unique transect identifier, duplicate check uses live registry.
     * @param[out] outError Optional failure message, unchanged on success.
     * @return True after engine accepts operation, false missing
     *   engine/empty/unknown/duplicate/rejection as applicable. Appends engine transect then
     *   mirrors registry create; registry-create result ignored. Emits transectChanged(name). No
     *   explicit modelEdited or undo; membership registry hooks announce dataObjectsChanged
     *   separately.
     */
    bool applyTransectAdd(const QString &name, QString *outError = nullptr);

    /*! \brief Rename. Refuses on duplicate (case-insensitive) or empty.
     * @param[in] oldName Current existing transect identifier.
     * @param[in] newName Nonempty unique new identifier, case-only permitted when engine accepts.
     * @param[out] outError Optional failure message, unchanged on success.
     * @return True after engine accepts operation, false missing
     *   engine/empty/unknown/duplicate/rejection as applicable. Engine rename first, registry
     *   mirror second with ignored rename result; emits broad transectChanged(empty). No explicit
     *   modelEdited or undo; membership registry hooks announce dataObjectsChanged separately.
     */
    bool applyTransectRename(const QString &oldName, const QString &newName,
                             QString *outError = nullptr);

    /*! \brief Remove a transect by name.
     * @param[in] name Existing nonempty transect identifier.
     * @param[out] outError Optional failure message, unchanged on success.
     * @return True after engine accepts operation, false missing
     *   engine/empty/unknown/duplicate/rejection as applicable. Engine removal first, registry
     *   mirror second, emits broad transectChanged(empty); missing target returns false. No
     *   explicit modelEdited or undo; membership registry hooks announce dataObjectsChanged
     *   separately.
     */
    bool applyTransectRemove(const QString &name, QString *outError = nullptr);

    /**
     * @param[in] name Existing engine transect identifier.
     * @param[in] comments Full comment text encoded UTF-8; empty clears.
     * @return False no engine/unknown index/otherwise true even if engine comment setter rejects,
     *   because its status is ignored. Mirrors matching provider if available and emits
     *   transectChanged(name). Provider signal may mark model edited; no direct
     *   undo/transaction/range clamp.
     */
    bool applyTransectSetComments(const QString &name, const QString &comments);
    /**
     * @param[in] name Existing engine transect identifier.
     * @param[in] nLeft Left overbank Manning coefficient, engine convention.
     * @param[in] nRight Right overbank Manning coefficient.
     * @param[in] nChannel Main channel Manning coefficient.
     * @return False no engine/unknown index/engine setter rejection; true after accepted write.
     *   Mirrors matching provider if available and emits transectChanged(name). Provider signal may
     *   mark model edited; no direct undo/transaction/range clamp.
     */
    bool applyTransectSetRoughness(const QString &name, double nLeft, double nRight,
                                   double nChannel);
    /**
     * @param[in] name Existing engine transect identifier.
     * @param[in] xLeft Left bank station in project cross-section length units.
     * @param[in] xRight Right bank station in same units; engine validates ordering/range.
     * @return False no engine/unknown index/engine setter rejection; true after accepted write.
     *   Mirrors matching provider if available and emits transectChanged(name). Provider signal may
     *   mark model edited; no direct undo/transaction/range clamp.
     */
    bool applyTransectSetBankStations(const QString &name, double xLeft, double xRight);
    /**
     * @param[in] name Existing engine transect identifier.
     * @param[in] xLeft Left encroachment station in project cross-section length units.
     * @param[in] xRight Right encroachment station in same units.
     * @return False no engine/unknown index/engine setter rejection; true after accepted write.
     *   Mirrors matching provider if available and emits transectChanged(name). Provider signal may
     *   mark model edited; no direct undo/transaction/range clamp.
     */
    bool applyTransectSetEncroachmentStations(const QString &name, double xLeft, double xRight);
    /**
     * @param[in] name Existing engine transect identifier.
     * @param[in] stationMul Station-coordinate multiplier, dimensionless.
     * @param[in] elevOffset Bed-elevation offset in project length units.
     * @param[in] meander Channel/meander length factor, dimensionless.
     * @return False no engine/unknown index/engine setter rejection; true after accepted write.
     *   Mirrors matching provider if available and emits transectChanged(name). Provider signal may
     *   mark model edited; no direct undo/transaction/range clamp.
     */
    bool applyTransectSetModifiers(const QString &name, double stationMul, double elevOffset,
                                   double meander);
    /*! \brief Snapshot-and-rewrite the full station list.
     * @param[in] name Existing engine transect identifier.
     * @param[in] stations Complete ordered (station, elevation) points in project cross-section
     *   length units; empty clears all.
     * @return False no engine/unknown/clear/add failure; true after every point appended. Clears
     *   original stations BEFORE adding, so failure leaves partial engine replacement, without
     *   success signal/registry mirror or rollback.
     * @note On success mirrors provider setAllPoints (return/reason ignored), then
     *   transectChanged(name). No sorting/unit conversion/geometry validation here; provider may
     *   reject a sequence engine already accepted. No direct undo.
     */
    bool applyTransectSetStations(const QString &name,
                                  const QVector<QPair<double, double>> &stations);

    /*! \brief Lazy accessor for the project-scoped TransectRegistry.
     * @return Borrowed Transect registry exposed as QObject, or null without engine. Reuses only
     *   matching engine identity; otherwise schedules old child deleteLater and creates/loads
     *   replacement before wiring notifications. Old providers can invalidate after event-loop
     *   deletion.
     * @note Membership/rename changes emit dataObjectsChanged only; provider points/metadata
     *   changes invoke markEdited at edit time. Save flush does not itself mark dirty. Same-engine
     *   fetch does not force reload; GUI-thread only.
     */
    QObject *ensureTransectRegistry();

    /*! \brief Lazy accessor for the project-scoped StreetRegistry.
     * @return Borrowed Street registry exposed as QObject, or null without engine. Reuses only
     *   matching engine identity; otherwise schedules old child deleteLater and creates/loads
     *   replacement before wiring notifications. Old providers can invalidate after event-loop
     *   deletion.
     * @note Membership/rename changes emit dataObjectsChanged only; provider parameter changes
     *   invoke markEdited at edit time. Save flush does not itself mark dirty. Same-engine fetch
     *   does not force reload; GUI-thread only.
     */
    QObject *ensureStreetRegistry();

    /*! \brief Lazy accessor for the project-scoped PollutantRegistry.
     * @return Borrowed Pollutant registry exposed as QObject, or null without engine. Reuses only
     *   matching engine identity; otherwise schedules old child deleteLater and creates/loads
     *   replacement before wiring notifications. Old providers can invalidate after event-loop
     *   deletion.
     * @note Membership/rename changes emit dataObjectsChanged only; provider parameter changes
     *   invoke markEdited at edit time. Save flush does not itself mark dirty. Same-engine fetch
     *   does not force reload; GUI-thread only.
     */
    QObject *ensurePollutantRegistry();

    /*! \brief Lazy accessor for the project-scoped LandUseRegistry.
     * @return Borrowed LandUse registry exposed as QObject, or null without engine. Reuses only
     *   matching engine identity; otherwise schedules old child deleteLater and creates/loads
     *   replacement before wiring notifications. Old providers can invalidate after event-loop
     *   deletion.
     * @note Membership/rename changes emit dataObjectsChanged only; provider parameter changes
     *   invoke markEdited at edit time. Save flush does not itself mark dirty. Same-engine fetch
     *   does not force reload; GUI-thread only.
     */
    QObject *ensureLandUseRegistry();

    /*! \brief Lazy accessor for the project-scoped AquiferRegistry.
     * @return Borrowed Aquifer registry exposed as QObject, or null without engine. Reuses only
     *   matching engine identity; otherwise schedules old child deleteLater and creates/loads
     *   replacement before wiring notifications. Old providers can invalidate after event-loop
     *   deletion.
     * @note Membership/rename changes emit dataObjectsChanged only; provider parameter changes
     *   invoke markEdited at edit time. Save flush does not itself mark dirty. Same-engine fetch
     *   does not force reload; GUI-thread only.
     */
    QObject *ensureAquiferRegistry();

    /*! \brief Lazy accessor for the project-scoped SnowpackRegistry.
     * @return Borrowed Snowpack registry exposed as QObject, or null without engine. Reuses only
     *   matching engine identity; otherwise schedules old child deleteLater and creates/loads
     *   replacement before wiring notifications. Old providers can invalidate after event-loop
     *   deletion.
     * @note Membership/rename changes emit dataObjectsChanged only; provider parameter changes
     *   invoke markEdited at edit time. Save flush does not itself mark dirty. Same-engine fetch
     *   does not force reload; GUI-thread only.
     */
    QObject *ensureSnowpackRegistry();

    /*! \brief Lazy accessor for the project-scoped InletRegistry.
     * @return Borrowed Inlet registry exposed as QObject, or null without engine. Reuses only
     *   matching engine identity; otherwise schedules old child deleteLater and creates/loads
     *   replacement before wiring notifications. Old providers can invalidate after event-loop
     *   deletion.
     * @note Membership/rename changes emit dataObjectsChanged only; provider parameter changes
     *   invoke markEdited at edit time. Save flush does not itself mark dirty. Same-engine fetch
     *   does not force reload; GUI-thread only.
     */
    QObject *ensureInletRegistry();

    /*! \brief Lazy accessor for the project-scoped LidControlRegistry.
     * @return Borrowed LidControl registry exposed as QObject, or null without engine. Reuses only
     *   matching engine identity; otherwise schedules old child deleteLater and creates/loads
     *   replacement before wiring notifications. Old providers can invalidate after event-loop
     *   deletion.
     * @note Membership/rename changes emit dataObjectsChanged only; provider parameter changes
     *   invoke markEdited at edit time. Save flush does not itself mark dirty. Same-engine fetch
     *   does not force reload; GUI-thread only.
     */
    QObject *ensureLidControlRegistry();

    // ===== User flags ([USER_FLAGS] / [USER_FLAG_VALUES]) ==================
    //
    // Phase 1 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md. Same lazy-init /
    // engine-handle-guard pattern as the registries above. All engine
    // user-flag mutation calls live in UserFlagsModel; UI surfaces (User
    // Flags Manager dialog, Attribute Table flag columns, Attribute Panel
    // rows) share this instance and re-render off its defsChanged() /
    // valueChanged() signals.

    /*! \brief Lazy accessor for the project-scoped UserFlagsModel.
     *  Returns nullptr if the engine isn't open.
     * @return Borrowed typed engine-bound QObject child, created lazily and reused for same engine
     *   identity; null without engine. Engine replacement schedules old model deleteLater and loads
     *   new model, invalidating old borrowed references later.
     * @note Definition/value changes emit modelEdited immediately, triggering edit epoch/coalesced
     *   style rebuild. Const identify/catalog paths call this via const_cast, so all access must be
     *   serialized on GUI thread.
     */
    openswmmvis::ui::UserFlagsModel *ensureUserFlagsModel();

public slots:
    /*! \brief Records that the engine now holds edits not yet on disk.
     *  \details Emits modelEdited(). Provided as a slot so UI surfaces that
     *           write the engine outside the layer's own apply* API (the
     *           property browser, the attribute table, the curve / time
     *           series / pattern editors, the comprehensive object editors)
     *           can connect their own "edited" signals straight to it.
     * @brief Emit modelEdited unconditionally without comparing/persisting data.
     * @note During blockSignals/bulk scope signal is suppressed and this inline method does not
     *   itself set deferred bulk dirty flags. Ordinary emission immediately bumps editRevision and
     *   schedules derived-style cache refresh.
     */
    void markEdited() { emit modelEdited(); }

signals:
    /*! Emitted before the editing engine is closed; borrowed-handle editors
     * must stop callbacks and discard their context synchronously. */
    void engineAboutToClose();
    /**
     * @param[in] path Raw path stored by setter/adoption; can be relative or empty despite
     *   modelFilePath getter presenting absolute.
     * @brief Announce stored model provenance change or successful adoption, without implying a
     *   save/file exists.
     */
    void modelFilePathChanged(const QString &path);
    /**
     * @param[in] show New global nodes gate; per-object/category/style visibility remains
     *   independent.
     * @brief Announce changed legacy visibility flag before repaint.
     */
    void showNodesChanged(bool show);
    /**
     * @param[in] show New global links gate; per-object/category/style visibility remains
     *   independent.
     * @brief Announce changed legacy visibility flag before repaint.
     */
    void showLinksChanged(bool show);
    /**
     * @param[in] show New global subcatchments gate; per-object/category/style visibility remains
     *   independent.
     * @brief Announce changed legacy visibility flag before repaint.
     */
    void showSubcatchmentsChanged(bool show);
    /**
     * @param[in] show New global raingages gate; per-object/category/style visibility remains
     *   independent.
     * @brief Announce changed legacy visibility flag before repaint.
     */
    void showRainGagesChanged(bool show);
    /**
     * @param[in] show New global labels gate; per-object/category/style visibility remains
     *   independent.
     * @brief Announce changed legacy visibility flag before repaint.
     */
    void showLabelsChanged(bool show);

    // VS.10 — labelConfigChanged() is inherited from OpenSWMMVisLayer.

    /**
     * @param[in] selectedNames Ordered derived aggregate selected names; typed masks remain in
     *   selectedElements(), names can be shared by namespaces.
     * @brief Announce canonical selection replacement, without geometry/engine edit; typed state is
     *   coherent before emission.
     */
    void selectionChanged(const QStringList &selectedNames);
    /**
     * @brief Announce GUI engine adoption/unit+CRS finalization complete and source arrays
     *   prepared; nonblocking permissive diagnostics do not prevent emission.
     */
    void modelLoaded();
    /**
     * @param[in] errorMessage Human-readable hard-load diagnostic supplied by producer.
     * @brief Expose load-error notification channel; current synchronous load helpers append error
     *   lists instead of emitting this signal themselves.
     */
    void modelLoadError(const QString &errorMessage);
    /*! \brief Slice BI Phase 8.13.6.5 — emitted when setRenderer() swaps the
     *         renderer pointer.
     * @note Also emitted by kind replacement/reset/legend edits, not only placeholder
     *   setRenderer(). Borrowed renderer/rule identities can change; actual repaint emission
     *   depends on mutating path.
     */
    void rendererChanged();

    /** @brief Announce an authored model presentation change without frame/repaint noise.
     * @details Emitted after category opacity, per-object visibility and global
     * geometry/label visibility setters change their stored values. This does not
     * announce model-data edits or transient selection/playback redraws. */
    void presentationChanged();

    /*!
     * \brief Emitted after any OPTIONS key has been written to the engine
     *        (via `setOption()` — the single entry point used by the
     *        SimulationOptionsDialog's Apply / OK).  Carries the list of
     *        keys that changed in this batch so observers can decide
     *        whether the change is relevant.
     *
     *        Enables MVC-style live sync: the main-window Flow Units
     *        combo, the status-bar Offset-Mode checkbox, and any other
     *        UI mirroring engine state bind once and refresh through
     *        this signal instead of polling on tab-switch.
     * @param[in] keys Accepted option-key names, not necessarily numerically changed values; batch
     *   can contain only successful subset.
     * @note Observer notification does not itself guarantee modelEdited/persistence or
     *   transactional whole-batch success.
     */
    void optionsChanged(const QStringList &keys);

    /*! Emitted when `setCategoryOrder()` accepts a new vector. The
     *  Object Browser tree model listens and reshapes without
     *  recomputing the per-category hidden counts (category membership
     *  didn't change, only the display order).
     * @note Also emitted by per-category object-order install/removal, including accepted identical
     *   object-order vectors; no engine/source-array reorder implied.
     */
    void categoryOrderChanged();

    /*! Emitted whenever the set of elements changes — an add, remove,
     *  or rollback of any node / link / gage / subcatchment. Panels
     *  that show element lists (Object Browser, Attribute Table) connect
     *  to this signal and call refresh() so their views stay in sync
     *  without polling.
     * @note Also used for type/offset changes and dirty bulk close, not a guarantee every source
     *   coordinate edit emits it. Borrowed engine/source indices may renumber before emission;
     *   caller must re-resolve identities.
     */
    void geometryChanged();

    /*! Emitted after a single object's attribute is mutated (e.g. conduit
     *  length or subcatchment area recalculated from geometry). The attribute
     *  table and property browser connect to this signal so they can refresh
     *  just the affected row/adapter without a full model rebuild.
     * @param[in] objectName Affected exact object name; namespace may require caller context when
     *   names collide.
     * @note Some coupling write failures still emit this signal to reflect partial mutation.
     *   Self-connection bumps edit epoch/coalesces derived styles; does not automatically emit
     *   modelEdited.
     */
    void attributeChanged(const QString &objectName);
    /** @brief Refresh extension-derived field metadata or values without editing the model.
     * @param[in] schemaChanged True after registration/removal, false after value invalidation.
     * @note GUI-thread notification for read-only tables and presentation consumers. */
    void extensionAttributesChanged(bool schemaChanged);

    /*! Emitted by applyRename() after a node, link, subcatchment or gage was
     *  renamed in the engine and every layer cache. Views that hold an object
     *  by name (the property browser's bound adapter) re-key on this. It is
     *  a notification only: handlers must not re-enter applyRename or emit
     *  edit signals back (the panel cross-wiring would ping-pong).
     * @param[in] oldName Previous exact identifier.
     * @param[in] newName Committed new identifier.
     * @param[in] kind Exactly one kKindNode/Link/Catch/Gage namespace BIT, not selection
     *   object-type enum.
     * @note Typed selections may split entries across names before this signal; selectionChanged is
     *   not emitted by rename.
     */
    void objectRenamed(const QString &oldName, const QString &newName, int kind);

    /*! Slice §QSG-1 — fired whenever setQsgRenderKinds() flips one or
     *  more kinds. MapCanvas listens so it can short-circuit the
     *  per-frame QSG repaint+grab when the scope is empty, and the
     *  CPU SWMMLayerItem listens so it can force a repaint that
     *  re-evaluates its skip-kind branches.
     * @param[in] kinds New stored geometry ownership flags, including any unfiltered unknown bits.
     * @note Labels remain CPU-owned; scope does not guarantee renderer initialization or replace
     *   base layer visibility.
     */
    void qsgRenderKindsChanged(QsgKinds kinds);

    /*! Slice BS Phase 6.9.2 — emitted whenever any [HYDROGRAPHS] /
     *  [RDII_DECAY] / [RDII] mutation lands in the engine via one of the
     *  applyHydrograph* / applyRdiiDecay* helpers. Empty uhName means
     *  "rebuild everything" (used by rename, since the old name disappears).
     *  All hydrograph models listen here and refresh; consumer UIs
     *  (Object Browser, property panel, NodeCompoundEditDialog picker, etc.)
     *  bind to those models and never poll the engine directly.
     * @param[in] uhName Affected UH group name, or empty for broad refresh after
     *   rename/remove/direct mark/bulk close.
     * @note Ordinary signals are suppressed in bulk; only paths setting deferred hydro flag
     *   announce at outer close. Does not alone mark model edited.
     */
    void hydrographChanged(const QString &uhName);

    /*! Slice BR Phase 6.8.1 — emitted whenever any [CONTROLS] mutation lands
     *  in the engine via one of the applyControlRule* helpers. The argument
     *  is the rule name affected, or empty for "rebuild everything"
     *  (rename / remove / clear). The list-model + SWMMControlRulePropertyAdapter
     *  + future RulesEditorDialog subscribe here and re-render off the same
     *  ControlRuleRegistry, so inline-edit-in-property-panel and
     *  edit-in-dialog converge through a single signal.
     * @param[in] ruleName Affected rule name or empty when identity/list changed broadly.
     * @note Current helpers emit only after accepted operation; best-effort failed-list restoration
     *   has no success notification and may require caller reconciliation.
     */
    void controlRulesChanged(const QString &ruleName);

    /*! Slice BQ Phase 6.7.4 — emitted whenever any [TRANSECTS] mutation
     *  lands via one of the applyTransect* helpers. Empty string means
     *  "rebuild everything" (rename / remove). All transect models
     *  subscribe here and refresh; consumer UIs (Object Browser,
     *  property panel, TransectEditorDialog) bind to those models.
     * @param[in] transectName Affected transect identifier, empty for rename/remove broad refresh.
     * @note Station replacement can partly mutate then fail without emitting; success means facade
     *   operation completed, not every unchecked provider mirror accepted.
     */
    void transectChanged(const QString &transectName);

    /*! Emitted whenever the SET of non-spatial data objects changes — a
     *  provider added / removed / renamed in any of the typed registries
     *  (time series, curves, patterns, transects, …) or an engine-side
     *  create via createDataObject(). Registry mutations fire at dialog
     *  submit time, which may be BEFORE the registry's deferred
     *  saveToEngine() flush — dataObjectCount()/dataObjectNameAt() are
     *  registry-preferred so subscribers see staged objects immediately.
     *  The Object Browser tree model and Attribute Table listen here;
     *  this is the data-object counterpart of geometryChanged().
     * @note Can announce staged registry membership before deferred engine flush; controls have
     *   their own channel and accessor does not forward registry membership into this signal. No
     *   payload/ownership transfer.
     */
    void dataObjectsChanged();

    /*! Emitted whenever anything that belongs in the .inp has been written to
     *  the engine, so the in-memory model no longer matches the file on disk.
     *
     *  This is the dirty-tracking channel, deliberately separate from the
     *  view-refresh signals above: SWMMVisProjectWindow connects it to
     *  setHasChanges(true), and SWMMVis:: onRunSimulation gates its pre-run
     *  auto-save on that flag. Emitting a refresh signal is NOT sufficient —
     *  vertex moves and provider content edits emit only repaintRequested()
     *  and provider-local signals — so any new engine-mutating path must also
     *  call markEdited().
     *
     *  Purely visual state (selection, zoom, symbology, layer order) must NOT
     *  emit this: on a large model a false positive costs a full .inp rewrite
     *  on the next run.
     * @note This is explicit dirty notification, not automatic proof every engine mutation emits
     *   it. Ordinary self-connection advances editRevision immediately and schedules coalesced
     *   derived style rebuild. Blocked signals are not replayed unless corresponding bulk flags
     *   were explicitly set.
     */
    void modelEdited();

private:
    // X4 — decode a kind-qualified legend class key ("<kindKey><sep><inner>")
    // back to its Category + inner class key. Returns false when the key
    // isn't kind-qualified or the kind is unknown.
    /**
     * @param[in] key Key split at first U+001F into exact stable category key and arbitrary inner
     *   key.
     * @param[out] catOut Optional category assigned on successful decode.
     * @param[out] innerOut Optional suffix assigned on successful decode; can be empty.
     * @return False leaves outputs untouched for absent separator/unknown category; true does not
     *   validate renderer supports inner key.
     */
    bool decodeLegendClassKey(const QString &key, Category *catOut, QString *innerOut) const;

    struct NodeGeom {
        double x, y;
        int objectType;
        int nodeType;
        int isVirtual = 0;
        /*! Inlet junction (implies isVirtual). Mirrors
         *  swmm_node_is_inlet; the renderers bucket on it
         *  BEFORE isVirtual. */
        int isInlet = 0;
        QString name;
    };
    struct LinkGeom {
        QVector<QPointF> vertices; // interior bend points only (no node endpoints)
        int linkType = -1;
        int fromNodeIdx = -1;
        int toNodeIdx = -1;
        QString name;
    };
    struct CatchGeom {
        QVector<QPointF> vertices;
        QString name;
    };

    /*! Scene-space drainage connector: PIA of subcatchment → outlet node/subcatchment. */
    struct OutletLine {
        QLineF line;
        int catchIdx;
    };

    /**
     * @brief Rebuild category/name/order indexes, native feature bboxes, model extent, node/gage KD
     *   trees, scene/grid/outlet coordinates and typed flags from current GUI source arrays.
     * @note Full synchronous pass, clears intra-category display overrides. Empty geometry still
     *   clears scene/flags but leaves previous extent when no finite bounds. Does not re-read
     *   source arrays from engine or synchronize endpoints; bulk caller must do that first.
     *   setExtent can notify through base setter, so callers of worker build must isolate
     *   layer/signals/paint appropriately.
     */
    void buildGeometryCache();
    /**
     * @param[in] canvasSRS Borrowed nullable canvas CRS, paired with layer srs().
     * @note Identical cached layer/canvas WKT pair no-op, including prior failed transform
     *   creation. Otherwise destroys forward/inverse, attempts owned OGR transform only for valid
     *   different CRSes, and rebuilds scene coords. Missing/failed transform uses identity; does
     *   not reset validity for automatic retry or emit repaint itself.
     */
    void rebuildTransform(const SpatialReferenceSystem *canvasSRS);
    /**
     * @brief Copy cached native node/gage XY into owned flat arrays and rebuild two nanoflann
     *   L2trees, then clear dirty flag.
     * @note Replaces all previous tree/adaptor pointers, retains empty trees without buildIndex. No
     *   finite-coordinate filtering/visibility gate, engine read or locking. Const mutable
     *   operation requires serialized layer access.
     */
    void rebuildKdTrees() const; ///< (Re-)build the nanoflann node + gage trees.
    /**
     * @brief Rebuild node/gage trees when absent, explicitly dirty or stored point counts differ
     *   from current source arrays.
     * @note Same-count coordinate changes need dirty flag; preview methods deliberately do not set
     *   it. Const access may allocate/rebuild synchronously, not thread-safe.
     */
    void ensureKdTrees() const; ///< Rebuild only if m_kdDirty is set.

    /*!
     * \brief Recompute every cached scene-coordinate (links, nodes,
     *        catchments, gages) from the SoA + current m_transform.
     *        Called from buildGeometryCache (geometry change) and from
     *        rebuildTransform (CRS change). Edit paths refresh the
     *        affected entries directly via refreshSceneCoordsForNode /
     *        refreshSceneCoordsForLink to avoid a full rebuild on a
     *        single drag preview.
     *
     *        The cached points include the scene-space Y-flip
     *        (toScene(mx, my) = QPointF(mx, -my)) so SWMMLayerItem:: paint
     *        can hand them straight to QPainter:: drawLines without any
     *        per-vertex math.
     * @note Replaces native-to-canvas projected/Y-inverted node/gage arrays, full link flat
     *   XY/offset/count/bboxes, catch rings/bboxes, link spatial grid and drainage outlet lines.
     *   Batch reprojection helper preserves input on failed points. Link zero-width/height bboxes
     *   inflate0.001 scene units for indexing.
     * @note Outlet origins are arithmetic means of scene polygon vertices, not area
     *   centroids/guaranteed interior poles. Geometry revision increments ONLY after outlet-line
     *   pass when engine+nonempty catches exist; early no-engine/empty-catch return skips bump even
     *   though other scene arrays changed. No signals itself.
     */
    void rebuildSceneCoords();
    /**
     * @param[in] nodeIdx Global cached source index; invalid index ignored.
     * @note Resizes node scene vector, projects one native cached point with unchecked single-point
     *   transform and Y inversion. No revision, grid/extent/bbox/flags/signals. Caller owns
     *   notification/coherent source-array mutation; GUI-thread serialized.
     */
    void refreshSceneCoordsForNode(int nodeIdx);
    /**
     * @param[in] linkIdx Global cached source index; invalid index ignored.
     * @note Rewrites same-size flat link slice and scene bbox using unchecked transforms/Y
     *   inversion. Layout/count mismatch falls back full rebuildSceneCoords; zero bbox axes
     *   inflate0.001 scene units. In-place success does NOT rebuild link spatial grid, increment
     *   revision or update native bbox/overall extent. Caller owns notification/coherent
     *   source-array mutation; GUI-thread serialized.
     */
    void refreshSceneCoordsForLink(int linkIdx);
    /**
     * @param[in] catchIdx Global cached source index; invalid index ignored.
     * @note Replaces one projected/Y-inverted scene ring, bbox and outgoing outlet line from
     *   arithmetic vertex mean; increments geometry revision. Incoming lines from other catchments
     *   draining to this one are not recomputed here. Caller owns notification/coherent
     *   source-array mutation; GUI-thread serialized.
     */
    void refreshSceneCoordsForSubcatch(int catchIdx);
    /**
     * @param[in] nodeIdx Global cached source index; invalid index ignored.
     * @note Patches destination of existing outlet lines whose engine outlet node matches. No
     *   engine/invalid node no-op; does not create missing lines, recompute origin or bump
     *   revision. Caller owns notification/coherent source-array mutation; GUI-thread serialized.
     */
    void refreshCatchOutletLinesForNode(int nodeIdx);

    // Incremental scene/cache mutations — O(1) per element plus an
    // O(L) spatial-grid touch where unavoidable. These let single
    // add / rename / delete operations skip the full O(N·V) OGR
    // re-transform that rebuildSceneCoords() does on every feature.
    /**
     * @brief Append tail node zero flags/projected point, type bucket/name maps; mark KD dirty and
     *   bump revision. Caller must not already have appended corresponding scene/bucket entries.
     * @note Empty source vector no-op; requires exactly one newly appended source row and prior
     *   parallel-array shape invariants. No engine/source mutation, overall extent or notifications
     *   here. Single-point OGR status unchecked.
     */
    void appendNodeSceneEntry(); ///< For new tail entry in m_nodes.
    /**
     * @brief Append tail full-polyline flat slice, offset/count, bboxes/zero flags/type
     *   bucket/maps; refresh link coords, rebuild link grid and bump revision. Existing custom
     *   order overrides remain untouched.
     * @note Empty source vector no-op; requires exactly one newly appended source row and prior
     *   parallel-array shape invariants. No engine/source mutation, overall extent or notifications
     *   here. Single-point OGR status unchecked.
     */
    void appendLinkSceneEntry(); ///< For new tail entry in m_links.
    /**
     * @brief Append tail native bbox/zero flags, resize scene rings, refresh new ring/outlet line
     *   and name maps. Geometry revision increments in refresh and again here.
     * @note Empty source vector no-op; requires exactly one newly appended source row and prior
     *   parallel-array shape invariants. No engine/source mutation, overall extent or notifications
     *   here. Single-point OGR status unchecked.
     */
    void appendCatchSceneEntry(); ///< For new tail entry in m_catchments.
    /**
     * @brief Append tail projected/Y-inverted point, zero flags, name maps, mark KD dirty and bump
     *   revision.
     * @note Empty source vector no-op; requires exactly one newly appended source row and prior
     *   parallel-array shape invariants. No engine/source mutation, overall extent or notifications
     *   here. Single-point OGR status unchecked.
     */
    void appendGageSceneEntry(); ///< For new tail entry in m_gages.
    /**
     * @param[in] nodeIdx Former global source index already removed from source vector; negative
     *   no-op, parallel layout must still represent pre-removal state.
     * @note Removes node point/flags and decrements cached link endpoints strictly above removed
     *   index; equal endpoints left unchanged. Marks KD dirty. Bumps geometry revision but does not
     *   rebuild category/name maps, extent or emit signals; caller coordinates these. Never call
     *   twice for same removed row.
     */
    void compactNodeSceneEntry(int nodeIdx);
    /**
     * @param[in] linkIdx Former global source index already removed from source vector; negative
     *   no-op, parallel layout must still represent pre-removal state.
     * @note Erases flat link slice and shifts later offsets, removes counts/bboxes/flags and
     *   rebuilds link grid. Bumps geometry revision but does not rebuild category/name maps, extent
     *   or emit signals; caller coordinates these. Never call twice for same removed row.
     */
    void compactLinkSceneEntry(int linkIdx);
    /**
     * @param[in] catchIdx Former global source index already removed from source vector; negative
     *   no-op, parallel layout must still represent pre-removal state.
     * @note Removes ring/bboxes/flags, outgoing outlet line and decrements stored source-catch
     *   indices. Other lines aimed at removed catch are not explicitly identified. Bumps geometry
     *   revision but does not rebuild category/name maps, extent or emit signals; caller
     *   coordinates these. Never call twice for same removed row.
     */
    void compactCatchSceneEntry(int catchIdx);
    /**
     * @param[in] gageIdx Former global source index already removed from source vector; negative
     *   no-op, parallel layout must still represent pre-removal state.
     * @note Removes gage point/flags and marks KD dirty. Bumps geometry revision but does not
     *   rebuild category/name maps, extent or emit signals; caller coordinates these. Never call
     *   twice for same removed row.
     */
    void compactGageSceneEntry(int gageIdx);

    /*!
     * \brief Re-read every link's from/to node index from the engine.
     * \details LinkGeom:: fromNodeIdx / toNodeIdx is authoritative data, not
     *          a derived cache: it is written only at load, VJ-fuse and
     *          link-add, and rebuildSceneCoords() consumes it rather than
     *          recomputing it. So buildGeometryCache() does NOT restore it,
     *          while compactNodeSceneEntry() — which endBulkEdit() skipped —
     *          is what renumbers it during ordinary single deletes.
     *
     *          Without this call at batch end, every link after a deleted
     *          node would resolve to the wrong endpoints and be drawn
     *          SILENTLY wrong. One O(L) pass, and stronger than replaying
     *          the per-delete deltas because it reads truth from the engine
     *          instead of assuming the deltas were all applied.
     *
     *          Called only from endBulkEdit() — deliberately NOT from
     *          buildGeometryCache(), which is on the model-load path where
     *          the SoA is already authoritative.
     * @brief Refresh every cached link endpoint index from current engine after bulk node
     *   compaction.
     * @note No engine no-op; failed individual getter leaves minus-one endpoint. Does not rebuild
     *   scene/index arrays or issue signals itself; must precede bulk geometry rebuild.
     */
    void syncLinkEndpointIndicesFromEngine();

    /*! Update name-keyed indices (m_objectLocation, m_nameToSoa,
     *  m_hiddenObjects/m_hiddenKindMask) when a single element is renamed.
     *  Geometry is unchanged, so this is the only work needed — caller
     *  must NOT also call rebuildCategoryIndex() or buildGeometryCache().
     *  \param renamedKind Exactly one kKind* bit — the kind that was
     *         actually renamed in the engine. Only that bit of the hidden
     *         state migrates to \p newName; hidden state for any OTHER
     *         kind still bearing \p oldName (a same-named object that was
     *         NOT renamed) stays under \p oldName. m_objectLocation /
     *         m_nameToSoa remain name-only keyed (pre-existing limitation,
     *         unrelated to this parameter — see class comment on name
     *         collisions).
     * @param[in] oldName Old exact object name.
     * @param[in] newName New exact name already committed to engine/source geometry; empty/equal
     *   ignored.
     * @note Moves untyped location entry only when it still represents renamed kind, moves exactly
     *   matching namespace map and typed hidden bits. Other namespace entries stay old, though
     *   global map cannot restore a shadowed name automatically. Does not rename source fields,
     *   selection or flags itself; caller handles those and notifications. Geometry/order/spatial
     *   arrays unchanged.
     */
    void renameInIndices(const QString &oldName, const QString &newName, quint8 renamedKind);

    /*! Recompute m_extent from current SoA + bbox caches without
     *  doing any OGR transform. Used by incremental delete paths.
     * @brief Sweep native cached node/gage coordinates, link bends and catch vertices into model
     *   extent without CRS transformation.
     * @note Uses source geometry rather than stored bboxes. Empty/no-bounds model leaves previous
     *   extent; no explicit finite filtering or scene/item-box refresh. setExtent may notify
     *   through base setter.
     */
    void recomputeExtentFromCaches();

    // Incremented at the end of every rebuildSceneCoords() call.
    // SWMMLayerQSGRenderer uses this to invalidate its subcatchment
    // triangulation cache without needing a signal or pointer comparison.
    quint64 m_geomRevision = 0;

    // Bulk-edit scope depth, and whether anything inside it actually
    // mutated. m_bulkDirty gates the batch-end rebuild so an all-failed
    // batch costs nothing — and so a batch of pure no-ops does not emit a
    // spurious geometryChanged().
    int m_bulkDepth = 0;
    bool m_bulkDirty = false;
    bool m_bulkAttrsEdited = false; ///< endBulkEdit also emits modelEdited
    bool m_bulkHydroEdited = false; ///< endBulkEdit also emits hydrographChanged("")

    // See crsAssigned(). Set by srsChanged after load; the load path resets
    // it to whether the .inp itself carried a CRS.
    bool m_crsAssigned = false;

    /*!
     * \brief Uniform-grid spatial index over scene-space link bboxes.
     *        Phase A.1 of the 5M-rendering plan (docs/RENDERING_5M_PLAN.md).
     *
     *        SWMMLayerItem:: paint queries `query(exposed)` to get the SoA
     *        indices of links whose bbox intersects the viewport, instead
     *        of iterating all m_links and bbox-testing each one. At zoomed
     *        in views this collapses paint's per-link work from O(N) to
     *        O(visible). When the GL pipeline lands (Phase B), the same
     *        query result feeds glDrawArrays so the GPU upload subrange
     *        and the CPU cull share one index.
     *
     *        Grid is keyed off m_linkSceneBBoxes — rebuilt by
     *        rebuildSceneCoords() (geometry / CRS change). Cell size is
     *        chosen automatically as ~16x the median link bbox diagonal
     *        so most links live in 1–2 cells; outliers get inserted into
     *        every cell they cross (no clipping).
     *
     *        Memory: vector<int> per cell, total entries ≈ N for short
     *        links. At 5M with ~100x100 grid → ~500 entries/cell on avg.
     *        Build is O(N); query is O(cells_in_rect + entries_returned).
     */
    struct LinkSpatialGrid {
        QRectF extent; ///< total bbox covered by the grid (scene space)
        double cellW = 0.0;
        double cellH = 0.0;
        int cols = 0;
        int rows = 0;
        QVector<QVector<int>> cells; ///< size cols*rows; cell (cx,cy) -> link SoA indices

        /**
 * @brief Reset link grid extent, cell dimensions/count and stored buckets to empty.
 * @note Invalidates all bucket references; no source geometry mutation or notifications.
 */
        void clear()
        {
            extent = {};
            cellW = cellH = 0.0;
            cols = rows = 0;
            cells.clear();
        }
        /**
 * @return Whether grid bucket vector is empty, not whether every allocated bucket contains links.
 */
        [[nodiscard]] bool isEmpty() const { return cells.isEmpty(); }

        /*! Rebuild the grid from a parallel array of scene-space bboxes
         *  (one per link, indexed by SoA position). Called by
         *  rebuildSceneCoords().
         * @param[in] linkBBoxes Scene-map bbox per global link slot; finite valid positive
         *   dimensions expected, invalid QRectF skipped.
         * @note Clears old state; no valid positive diagonals yields empty grid. Uses center Tukey
         *   fences10IQR for robust extent, median-diagonal/occupancy sizing with
         *   approximately1024²bucket cap; outliers clamp to boundary buckets without disappearing.
         *   Retains full source indices, allocates per-bucket references. Does not store bboxes for
         *   exact refinement.
         */
        void rebuild(const QVector<QRectF> &linkBBoxes);

        /*! Broad-phase link indices for grid buckets touched by \p rect.
         *  Order is grid traversal rather than SoA order; callers must
         *  refine candidates by actual bbox/geometry intersection.
         * @param[in] rect Finite valid positive-area scene-map query rectangle; invalid/empty
         *   returns empty.
         * @return Owned unique broad-phase link indices in bucket traversal order, not sorted
         *   source order and NOT exact bbox intersections. Outside-extent requests clamp to
         *   boundary buckets to retain outlier candidates; caller must precisely cull. Dedup
         *   scratch allocated through largest encountered index. Const query uses local storage,
         *   but concurrent rebuild unsafe.
         */
        [[nodiscard]] QVector<int> query(const QRectF &rect) const;
    };
    LinkSpatialGrid m_linkGrid;

    /*!
     * \brief Rebuild the per-category index buckets (m_nodesByType,
     *        m_linksByType), the name→(category, row) lookup, and the
     *        per-category hidden-count array from m_hiddenObjects.
     *        Called whenever the SoA is repopulated (loadModel / add /
     *        remove). Independent of the extent computation in
     *        buildGeometryCache().
     * @brief Rebuild type buckets, display-name/source hashes and category hidden counters after
     *   source-array membership changes.
     * @note Clears all intra-category permutations, retains valid category display sequence.
     *   Unknown node/link types fold to junction/conduit. Namespaced lookup keeps first definition;
     *   untyped name lookup prefers node over link but later catch/gage overwrite. Hidden counters
     *   derive aggregate hidden names rather than per-kind mask; flags are rebuilt separately. No
     *   signals itself.
     */
    void rebuildCategoryIndex();

    SWMM_Engine m_engine = nullptr;

    QString m_modelFilePath;
    bool m_showNodes = true;
    bool m_showLinks = true;
    bool m_showSubcatchments = true;
    bool m_showRainGages = true;
    bool m_showLabels = false;
    // VS.10 — m_labelConfig moved to OpenSWMMVisLayer (base owns it now).
    // Per-kind QSG scope (see QsgKind / qsgRenderKinds() above).
    // Default empty means the QSG overlay never runs and every kind is
    // drawn by the CPU SWMMLayerItem path — matches pre-§QSG-1 behaviour
    // and is what the Preferences toggle inverts when the user enables
    // the experimental GPU path.
    QsgKinds m_qsgKinds = QsgNone;

    // Slice O — per-object hidden set. Names listed here are skipped by
    // populateScene. NOTE: SWMM names are per-type namespaces — real
    // models routinely pair every subcatchment with a same-named rain
    // gage, and links may share names with nodes — so membership alone
    // cannot say WHICH kind the user hid. m_hiddenKindMask below carries
    // that type information; this set stays the name-keyed canonical
    // membership for the legacy API and .oswp persistence.
    QSet<QString> m_hiddenObjects;

    // Per-name bitmask of hidden kinds. Invariant: a name is in
    // m_hiddenObjects iff its mask entry exists and is non-zero. Names
    // hidden through the legacy name-only API carry kKindAll.
    QHash<QString, quint8> m_hiddenKindMask;

    // Per-name bitmask of SELECTED kinds — canonical typed selection.
    // m_selectedNames mirrors the keys for legacy name-based readers.
    // Names selected through the legacy name-only API carry kKindAll.
    QHash<QString, quint8> m_selectedKindMask;

    QVector<NodeGeom> m_nodes;
    QVector<LinkGeom> m_links;
    QVector<CatchGeom> m_catchments;
    QVector<NodeGeom> m_gages;

    // Per-feature bbox caches, parallel to m_links / m_catchments.
    // Computed once in buildGeometryCache() and refreshed on edits
    // that touch coords (applyNodeMove / applyLinkInteriorVertices /
    // applyNodeAdd). linksInRect / subcatchmentsInRect iterate these
    // arrays directly — no per-call name lookup, no per-call vertex
    // loop. Big-model rubber-band selects went from O(N²) (linkIndex
    // linear scan + per-link vertex bbox) to O(N) with constant work
    // per item.
    QVector<MapExtent> m_linkBboxes;
    QVector<MapExtent> m_catchBboxes;

    // Scene-space coordinate cache, parallel to the SoAs above. Computed
    // once in rebuildSceneCoords() (on geometry change or CRS change) and
    // refreshed incrementally by edit paths. SWMMLayerItem::paint reads
    // from these directly — no per-vertex Transform()/toScene() call on
    // the paint hot path. Per-feature scene-space bounding rects support
    // viewport cull without a per-paint per-link bbox compute.
    QVector<QPointF> m_nodeScenePts;
    QVector<QRectF> m_linkSceneBBoxes;
    QVector<QVector<QPointF>> m_catchScenePts;
    QVector<QRectF> m_catchSceneBBoxes;
    QVector<QPointF> m_gageScenePts;
    QVector<OutletLine> m_catchOutletLines; ///< PIA → outlet, built in rebuildSceneCoords.

    // Phase A.3 — flat-array link scene-coords. Replaces the previous
    // per-link QVector<QPointF> with one big std::vector<double> of
    // interleaved (x, y) pairs, plus per-link (offset, count). At 5M
    // links this saves ~120 MB of QVector overhead and gives the GL
    // pipeline (Phase B) a buffer it can stream into a VBO.
    //
    //   m_linkSceneFlat[(m_linkVertexOffset[i] + v) * 2 + 0]  =  x
    //   m_linkSceneFlat[(m_linkVertexOffset[i] + v) * 2 + 1]  =  y   (Y-flipped)
    //   v in [0, m_linkVertexCount[i])
    //
    // Doubles, not floats: SWMM models in projected CRS (state plane
    // feet, UTM meters) routinely have 6-7 digit coordinates, where
    // float's ~7 significant digits leaves ~0.5-1 m of quantisation
    // error. That shows up at high zoom as a visible gap between a
    // node glyph (drawn from the double m_nodeScenePts) and the link
    // endpoint that should be coincident with it. Y is pre-flipped so
    // paint feeds straight into QPainter::drawLines without per-vertex
    // math. GPU renderers that need single-precision VBOs subtract a
    // local origin and downcast on upload. Any vertex-count change
    // (rare; only when an editor adds/removes a vertex) triggers a
    // full rebuildSceneCoords; in-place node moves only rewrite the
    // affected link's slice of m_linkSceneFlat.
    std::vector<double> m_linkSceneFlat;
    std::vector<uint32_t> m_linkVertexOffset; // size == m_links.size()
    std::vector<uint32_t> m_linkVertexCount;  // size == m_links.size()

    // Phase A.2 of the 5M-rendering plan: selection / hidden state as
    // dense byte arrays parallel to the SoAs. SWMMLayerItem::paint reads
    // these directly instead of hashing each name into a QSet<QString>
    // every frame. Maintained alongside m_selectedNames / m_hiddenObjects
    // — those QString-keyed members remain the canonical state (used by
    // identify, attribute panel, persistence) but the flag arrays are
    // the fast path on the paint loop. Kept coherent by
    // setSelectedElementNames / setObjectVisible* / rebuildCategoryIndex.
    std::vector<uint8_t> m_nodeSelectedFlag; // 1 if m_nodes[i] selected
    std::vector<uint8_t> m_linkSelectedFlag;
    std::vector<uint8_t> m_catchSelectedFlag;
    std::vector<uint8_t> m_gageSelectedFlag;
    std::vector<uint8_t> m_nodeHiddenFlag;
    std::vector<uint8_t> m_linkHiddenFlag;
    std::vector<uint8_t> m_catchHiddenFlag;
    std::vector<uint8_t> m_gageHiddenFlag;

    // Per-category row → SoA-index buckets. Built in buildGeometryCache,
    // cleared in closeEngine. Used by the virtualised Object Browser tree
    // model to resolve QModelIndex (category, row) → backing name in O(1)
    // without scanning the whole SoA on every data() call.
    QVector<int> m_nodesByType[4]; // junction/outfall/storage/divider
    QVector<int> m_linksByType[5]; // conduit/pump/orifice/weir/outlet

    // Per-category count of hidden members. Kept in sync by
    // setObjectVisible, setObjectVisibleAt, setCategoryVisible so
    // categoryCheckState() is O(1). Seeded to 0 at cache rebuild.
    int m_hiddenCountByCategory[NumCategories] = {};

    // Per-kind (sub-layer) opacity in [0,1], default 1.0. Multiplies each
    // feature's alpha in both paint paths; edited via the layer-tree Opacity
    // column on kind rows. NumCategories entries, brace-init below leaves
    // them 0 — the ctor fills 1.0 (see swmmmodellayer.cpp).
    qreal m_categoryOpacity[NumCategories];

    // User-controlled display order for the Object Browser (Slice T.2).
    // Seeded to the enum sequence in rebuildCategoryIndex() so a fresh
    // model starts with the default order.
    QVector<Category> m_categoryOrder;

    // Per-category object order overrides (Slice T.3). Absent key =
    // category uses its default bucket order; present key = visible
    // row r maps to `m_objectOrderOverrides[c][r]`, which is the SoA
    // index for nodes (m_nodes) / links (m_links) / catch (m_catchments)
    // / gages (m_gages). Sparse by design: only categories the user
    // actually reordered carry memory. Cleared on geometry rebuild so
    // stale indices can't survive an add/remove.
    QHash<Category, QVector<int>> m_objectOrderOverrides;

    // name → (category, row) lookup seeded at cache time. Serves the
    // legacy name-based setObjectVisible(name, bool) entry point and
    // the SelectionManager → model-index mapping in the Object Browser
    // without a full O(N) scan of m_nodes / m_links per call.
    QHash<QString, QPair<Category, int>> m_objectLocation;

    // Phase A.2 — name → (kind, SoA index) for the flag-array paint
    // path. `kind` selects which array (m_nodes / m_links /
    // m_catchments / m_gages) the SoA index addresses. Built alongside
    // m_objectLocation in rebuildCategoryIndex and kept coherent on
    // edits. Distinct from m_objectLocation because that one stores
    // *display* row inside per-category buckets, whereas paint needs
    // the raw SoA index — different translation, different consumers.
    enum class SoaKind : int8_t { Node = 0, Link = 1, Catch = 2, Gage = 3 };
    struct SoaLocation {
        SoaKind kind;
        int soaIdx;
    };
    QHash<QString, SoaLocation> m_nameToSoa;

    // Per-kind name → SoA index. Deliberately SEPARATE from m_nameToSoa,
    // which cannot serve these lookups: it is single-keyed, and while nodes
    // beat links there (links insert only when absent), catchments and gages
    // insert UNCONDITIONALLY and therefore overwrite a colliding node or
    // link. identifyByName / nodeIndex / linkIndex must instead resolve
    // node → link → catchment → gage, which is what the linear scans they
    // replace did. SWMM keeps a separate namespace per kind, so such
    // collisions are legal and do occur in real models.
    //
    // Within a kind, first definition wins — matching the scans' behaviour
    // of returning the lowest index. Maintained in lockstep with
    // m_nameToSoa: rebuilt in rebuildCategoryIndex(), swapped in
    // renameObject(), extended in appendNodeSceneEntry() /
    // appendLinkSceneEntry(). Deletions always go through
    // rebuildCategoryIndex(), so no incremental erase path is needed.
    QHash<QString, int> m_nodeByName;
    QHash<QString, int> m_linkByName;
    QHash<QString, int> m_catchByName;
    QHash<QString, int> m_gageByName;

    /*! Refresh `m_*SelectedFlag` and `m_*HiddenFlag` from
     *  `m_selectedNames` + `m_hiddenObjects`. Cost: O(|selection| +
     *  |hidden|), bounded by user actions, not by total object count.
     *  Called on every selection / visibility mutation so the flag
     *  arrays match the canonical QString-keyed state.
     * @brief Resize/zero all node/link/catch/gage selected/hidden byte arrays and populate them
     *   from current typed name masks.
     * @note Aggregate hidden membership without typed mask means all kinds hidden; selection with
     *   missing mask means none. Iterates every source slot, so same-named namespaces receive their
     *   own bit. No category-counter recomputation, signals or engine writes.
     */
    void rebuildFlagArrays();

    /*! A newly created object must start visible and unselected. Hidden and
     *  selected state are keyed by NAME: a fused / deleted object's name
     *  stays in `m_hiddenObjects` / `m_hiddenKindMask` (the .oswp sidecar
     *  persists and re-applies that set without checking the object still
     *  exists) and in the selection mirrors, and the generated names
     *  (`<base>_B`, `J<n>`) deterministically reuse a dead object's name —
     *  so the next split / add inherited the dead object's hidden bit the
     *  moment rebuildFlagArrays() ran, and the new link vanished from the
     *  map as soon as it was deselected. Drop \p kindBit for \p name from
     *  both sets before the flag arrays are rebuilt from them. Pure state
     *  fix-up: no signal, no count adjustment (a name with no live object
     *  never counted towards m_hiddenCountByCategory).
     * @param[in] name Reused freshly added exact object name.
     * @param[in] kindBit Namespace bit whose stale hide/selection must be removed.
     * @note Clears that hidden bit and prunes other bits lacking current namespaced cache entries;
     *   legacy hide-only name starts as all hidden. Strips selected kind from canonical entries and
     *   rebuilds selection mirrors. No flags/counters/signals itself; caller rebuilds them after
     *   append. Requires coherent pre/post-add map ordering.
     */
    void forgetStaleObjectState(const QString &name, quint8 kindBit);

    SWMMElementSymbol m_junctionSym;
    /*! Marker override for virtual junctions — same CatJunctions bucket
     *  (decision D-G1: no 5th persisted category), distinct glyph. */
    SWMMElementSymbol m_virtualJunctionSym;
    /*! Marker override for inlet junctions — same CatJunctions bucket,
     *  tested BEFORE the virtual override (an inlet junction is virtual). */
    SWMMElementSymbol m_inletJunctionSym;
    /*! Style of the dashed host → capture-node overlay (§3.2). Only
     *  fillColor (line colour) and outlineWidth (line width) are read. */
    SWMMElementSymbol m_inletConnectorSym;

    /*! Lazily-rebuilt overlay geometry; invalidated by attributeChanged /
     *  geometryChanged self-connections in the ctor. */
    mutable QVector<InletConnector> m_inletConnectors;
    mutable bool m_inletConnectorsDirty = true;
    /*! Tri-state engine capability cache: -1 unknown, 0 no, 1 yes. */
    mutable int m_inletJunctionSupport = -1;
    /*! Canvas whose undo stack mediated (non-map-tool) edits push onto.
     *  Non-owning; see setEditCanvas(). */
    QPointer<MapCanvas> m_editCanvas;
    /*! Rebuild m_inletConnectors from the engine's usage rows.
     * @brief Clear lazy connector cache and gather valid engine usage hosts/capture-node positions
     *   from prepared scene arrays.
     * @note Marks cache clean even for no engine/unsupported usage API. Link midpoint uses
     *   transformed scene-segment lengths, not model-space length; zero-length polyline uses first
     *   point. Failed usage/capture/host bounds reads skipped; visibility gates are left to
     *   painter. No notification/engine writes.
     */
    void rebuildInletConnectors() const;

    /*! Cache sync shared by every conduit-split entry point (plain virtual
     *  split and the inlet-junction split). Appends the inserted node and
     *  the new downstream conduit, re-reads the partitioned interior
     *  vertices of both conduits, and emits the geometry signal set.
     * @param[in] li Original conduit index, valid in cache/engine.
     * @param[in] newNode New engine-tail node index matching next GUI slot.
     * @param[in] newLink New engine-tail downstream conduit index matching next GUI slot.
     * @param[in] newNodeName Inserted node name.
     * @param[in] newLinkName Inserted downstream conduit name.
     * @param[in] origLinkName Original conduit identity used for attributeChanged.
     * @param[in] isVirtual Raw cached virtual role flag.
     * @param[in] isInlet Raw cached inlet role flag.
     * @note Requires already committed engine split and exclusive GUI state. Reads engine
     *   geometry/endpoints, appends node/link caches and refreshes original segment; individual
     *   reads are unchecked. Clears stale typed object state, rebuilds
     *   indices/order/spatial/bounds, emits repaint/geometry/attribute notifications. No
     *   rollback/engine mutation itself.
     */
    void syncSplitCaches(int li, int newNode, int newLink, const QString &newNodeName,
                         const QString &newLinkName, const QString &origLinkName, int isVirtual,
                         int isInlet);

    /*! Cache sync shared by both fuse entry points. Removes the node and the
     *  retired downstream conduit, then re-reads the surviving conduit's
     *  downstream end and merged interior vertices.
     * @param[in] ni Removed node former GUI index.
     * @param[in] dn Removed downstream conduit former GUI index, minus one if none identified.
     * @param[in] surviving Surviving conduit index AFTER engine compaction.
     * @note Requires committed matching engine fuse. Removes/compacts GUI arrays, rereads surviving
     *   endpoints/interior bends, rebuilds category/spatial/connector/bounds and emits
     *   repaint/geometryChanged. Read failures are not propagated; no direct modelEdited/undo.
     */
    void syncFuseCaches(int ni, int dn, int surviving);
    SWMMElementSymbol m_outfallSym;
    SWMMElementSymbol m_storageSym;
    SWMMElementSymbol m_dividerSym;
    SWMMElementSymbol m_conduitSym;
    SWMMElementSymbol m_pumpSym;
    SWMMElementSymbol m_orificeSym;
    SWMMElementSymbol m_weirSym;
    SWMMElementSymbol
        m_outletSym; // Slice FX.1 — Outlets honor showArrows independently of Conduits.
    SWMMElementSymbol m_subcatchSym;
    SWMMElementSymbol m_gageSym;

    // Persistent per-kind symbol adapters keyed by subject routing id
    // ("model.junctions" …). Lazily built by elementSymbolAdapter(); owned
    // via QObject parenting. set*Symbol keeps their cached structs truthful
    // through resyncSymbolAdapter().
    QHash<QString, SwmmElementSymbolAdapter *> m_symbolAdapters;
    /*! Push the freshly-set struct into the persistent adapter (if built)
     *  without re-invoking the writer.
     * @param[in] routingId Exact cached adapter key; missing/null entry no-op.
     * @param[in] s Current complete symbol copied into adapter cache.
     * @note Resync notifies editor without calling writer/layer setters; no new adapter allocated
     *   and no layer paint signal emitted here.
     */
    void resyncSymbolAdapter(const QString &routingId, const SWMMElementSymbol &s);

    // Data-edit epoch for derived style/label caches. Bumped whenever
    // modelEdited or attributeChanged fires (self-connections in the ctor)
    // so consumers (SWMMLayerItem's label cache) can detect staleness
    // without wiring their own signal plumbing. Distinct from
    // m_geomRevision, which tracks scene-coordinate rebuilds only.
    quint64 m_editRevision = 0;
    /*! Coalescing guard — the ctor's self-connections bump m_editRevision
     *  immediately but defer invalidateDerivedStyleCaches() through a
     *  zero-timeout singleShot so bulk edits cost one rebuild. */
    bool m_derivedStyleCachesPending = false;
    /*! Invalidate style caches derived from model DATA: clears every
     *  graduated kind renderer's independent size value range and rebuilds
     *  that kind's override cache so sizes re-normalise against the edited
     *  values.
     * @brief Clear independent Graduated size ranges, rebuild each kind resolved feature caches and
     *   request repaint.
     * @note Does not clear authored classification breaks. Normally reached through coalesced
     *   model/attribute edit callback after editRevision already incremented; can synchronously
     *   scan all feature attributes.
     */
    void invalidateDerivedStyleCaches();

    // Slice BI Phase 8.13.6.5 — renderer plumbing. Eagerly initialised in
    // the ctor (default placeholder: SingleSymbolRenderer) so renderer()
    // never returns null. The existing paint loop still reads the per-kind
    // m_*Sym members; the paint refactor sub-phase swaps in a
    // MultiKindRenderer adapter and flips the paint path.
    std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> m_renderer;

    // Slice BI-MK.1 / BI-MK.LT (2026-05-24) — per-kind renderer storage.
    // Indexed by Category ordinal; size == NumCategories. SymbologyDialog's
    // left-pane picker and the layer-tree sub-row right-click menu drive
    // edits here. Single-symbol entries are kept in sync with the legacy
    // m_*Sym fields (write-through both directions) so the existing paint
    // loop reflects user edits without a per-feature symbolFor() refactor.
    std::vector<std::unique_ptr<OpenSWMM::Render::IFeatureRenderer>> m_kindRenderers;

    // Slice B.4 — RuleList mirroring the per-kind renderers. Lazy-built
    // on first ruleList() call (cloned from m_kindRenderers). mutable
    // so the const override can populate on first read.
    mutable std::unique_ptr<OpenSWMM::Render::RuleList> m_ruleList;
    /**
     * @brief Replace lazy rule model with one cloned renderer per spatial category and bind rule
     *   replacement/style-load propagation.
     * @note Clears dirty flag, names rules by stable kind key and connects later ruleListChanged
     *   repopulation hooks after initial seed. Does not itself paint/modify engine; destroys prior
     *   list/borrowed rule pointers.
     */
    void buildRuleListLazy() const;
    // Feedback C1 — Rule → kind plumbing shared by the lazy build and by
    // lists repopulated in place (Style Manager "Apply to layer" loads a
    // library file into the existing RuleList through RuleList::fromJson).
    /**
     * @param[in] rule Nullable borrowed Rule, expected owned by current list.
     * @param[in] c Spatial category associated with this rule.
     * @note Null/already kindAttached property no-op. Sets dynamic property and connects
     *   rendererReplaced once; property is not category-specific, so a previously bound rule cannot
     *   be rebound by this helper. Layer-bound connection lifetime follows QObject ownership.
     */
    void attachRuleToKind(OpenSWMM::Render::Rule *rule, Category c);
    /**
     * @param[in] rule Nullable borrowed rule; null ignored.
     * @param[in] c Spatial category receiving style projection.
     * @note Classifies rule Graduated renderer in place, clones/reinstalls kind renderer and
     *   back-propagates supported SingleSymbol first-layer props into legacy symbol setter. Can
     *   produce multiple repaints and dirty rule mirror; classified renderers retain independent
     *   output axes. No transactional rollback or engine edit.
     */
    void applyRuleToKind(OpenSWMM::Render::Rule *rule, Category c);
    /**
     * @brief Attach/apply fresh unbound loaded rules whose exact names match stable kind keys,
     *   retaining foreign/already-bound rules.
     * @note GUI-thread handler; emits ordinary renderer/symbol repaint side effects per applied
     *   kind. If anything applied, clears mirror-dirty flag so next accessor preserves loaded rule
     *   identities; duplicates matching same kind can apply in list order.
     */
    void onRuleListRepopulated();
    // M1 — set when a kind renderer changes after the RuleList was built, so
    // the next ruleList() rebuilds from the live per-kind renderers instead
    // of handing back a stale mirror (the "dialog doesn't match what's drawn
    // on open" bug). Rebuild happens only at read time (dialog open), never
    // mid-session, so an open dialog's rule pointers stay valid.
    mutable bool m_ruleListDirty = false;

    // M2 — keep the per-kind renderer (the single source of truth)
    // consistent when a legacy struct setter is called directly (e.g.
    // preferences / project load). Updates only SingleSymbol renderers (in
    // place — no signal, no recursion); classified renderers are left intact
    // since the struct is the single-symbol fallback. Marks the RuleList
    // dirty so the next dialog open reflects the change.
    /**
     * @param[in] c Spatial category; out-of-range renderer slot ignored.
     * @param[in] s Current copied legacy symbol, with pixel widths/sizes and colours.
     * @note Writes converted SymbolStyle only for SingleSymbol slot, preserving classified renderer
     *   state; marks rule list dirty for valid slot even when non-single. Does not recurse through
     *   symbol setter or emit layer signals.
     */
    void syncSingleRendererFromStruct(Category c, const SWMMElementSymbol &s);

    // Slice BI Phase 8.13.6.4 (2026-05-24) — per-feature colour-override
    // cache populated when a kind's renderer is Graduated / Categorized /
    // RuleBased. Indexed by Category × SoA index. The painter checks
    // m_kindUsesOverrides[c] to switch between the legacy bucketed fast
    // path and the per-feature override path.
    QVector<QColor> m_kindFeatureColors[NumCategories];
    QVector<double> m_kindFeatureSizes
        [NumCategories]; /*!< Slice BI Phase 8.13.43-α — negative = no override. */
    QVector<double> m_kindFeatureOffsets
        [NumCategories]; /*!< Slice Z.5b-paint-graduated — per-feature line offset (px). 0 = no override. */
    QVector<int> m_kindFeatureShapes
        [NumCategories]; /*!< M3 — per-feature MarkerShape (int); -1 = no override. */
    bool m_kindHasAnyOffset[NumCategories] =
        {}; /*!< Slice Z.5b-paint-graduated — short-circuit flag. */
    bool m_kindUsesOverrides[NumCategories] = {};

    QStringList m_selectedNames;
    // Canonical typed selection (ordered). m_selectedNames and
    // m_selectedKindMask are derived mirrors, rebuilt atomically in
    // setSelectedElements().
    QVector<SelectedElement> m_selectedElements;

    // Engine-table partition cache (curves vs. timeseries). The engine
    // stores both in the same unified table list keyed by type; without
    // this cache, the Object Browser data-category views and pickers
    // re-walked the entire table list on every row count / name lookup —
    // O(N) per call × N rows = O(N²) per refresh.  Filled lazily in
    // ensureTablePartition(); invalidated when a curve or timeseries is
    // added / removed / renamed, and on every modelLoaded().
    mutable QVector<int> m_curveTableIdx;
    mutable QVector<int> m_tsTableIdx;
    mutable bool m_tablePartitionDirty = true;
    /**
     * @brief Rebuild const curve/timeseries engine-index partition when explicitly dirty or engine
     *   table count differs from cached total.
     * @note Failed type reads are skipped; unchanged-count type replacement is not detected unless
     *   caller marks dirty. No engine clears cache and dirty flag. Serialized owning-thread cache
     *   access; does not load registries or emit.
     */
    void ensureTablePartition() const;

public:
    /*! Invalidate the cached engine-table partition (curves vs. time
     *  series). External mutators (e.g. CurveRegistry / TimeseriesRegistry
     *  during INP import) call this so the next Object Browser refresh
     *  picks up the new entries.
     * @brief Mark unified curve/timeseries table partition dirty without reloading registries,
     *   engine access or emitting; next catalog fallback rebuilds it.
     */
    void invalidateDataObjectCache() { m_tablePartitionDirty = true; }

private:
    // GDAL transform (layer CRS → canvas CRS)
    class OGRCoordinateTransformation *m_transform = nullptr;

    // Cached inverse (canvas CRS → layer CRS); built lazily on first
    // transformCanvasToLayer() call and invalidated whenever m_transform is
    // rebuilt or destroyed. Mutable so the public const accessor can populate.
    mutable class OGRCoordinateTransformation *m_inverseTransform = nullptr;

    // The (layer CRS, canvas CRS) pair m_transform was last built from, so
    // rebuildTransform() can no-op when nothing about the projection
    // changed. One srsChanged emission reaches this layer through two
    // independent listeners (see rebuildTransform), and each arrival
    // otherwise redoes rebuildSceneCoords over every node, link and
    // subcatchment vertex. WKT rather than the authority string because a
    // Local CRS has no authority code — precisely the case on the models
    // where the redundant rebuild costs the most.
    QString m_transformLayerWkt;
    QString m_transformCanvasWkt;
    bool m_transformValid = false;

    // Dirty flag — skip scene rebuild when only the view extent changed
    bool m_needsRebuild = true;

    // KD-tree spatial index for point-feature queries (nodes + gages).
    // Mutable so const query methods (identifyAt, nodesInRect, …) can
    // trigger a lazy rebuild without breaking const-correctness.
    mutable bool m_kdDirty = true;
    mutable std::unique_ptr<SWMMKdTrees> m_kdTrees;

    // Batched scene renderer (created in populateScene). Edit paths that
    // mutate SoA coordinates call refreshBoundingRect() on it so the
    // scene's BSP index stays aligned after moves beyond the prior
    // extent. Cleared in depopulateScene.
    class SWMMLayerItem *m_batchedItem = nullptr;

    // Slice BS Phase 6.9.2 — hydrograph MVC models, lazily constructed on
    // first accessor call. Owned by the layer (parented through the
    // QObject tree, destroyed with it). Shared across all consumer UIs.
    HydrographGroupListModel *m_uhGroupListModel = nullptr;
    HydrographRtkTableModel *m_uhRtkModel = nullptr;
    HydrographIaTableModel *m_uhIaModel = nullptr;
    HydrographDecayTableModel *m_uhDecayModel = nullptr;

    // Lazy-loaded data-object registries scoped to the current engine
    // handle. Forward-declared as `QObject*` to keep this header free of
    // the registry includes; the .cpp downcasts to the concrete type.
    // Shared across every UI that mutates these data objects so all
    // views see the same provider instances.
    QObject *m_tsRegistry = nullptr;
    void *m_tsRegistryEngineHandle = nullptr;
    QObject *m_patternRegistry = nullptr;
    void *m_patternRegistryEngineHandle = nullptr;
    QObject *m_curveRegistry = nullptr;
    void *m_curveRegistryEngineHandle = nullptr;

    // Slice BR Phase 6.8.1 — control-rule registry mirror.
    QObject *m_controlRuleRegistry = nullptr;
    void *m_controlRuleRegistryEngineHandle = nullptr;

    // Slice BQ Phase 6.7.4 — transect registry mirror.
    QObject *m_transectRegistry = nullptr;
    void *m_transectRegistryEngineHandle = nullptr;

    // Street registry mirror (parametric [STREETS] cross-sections).
    QObject *m_streetRegistry = nullptr;
    void *m_streetRegistryEngineHandle = nullptr;

    // Pollutant registry mirror ([POLLUTANTS]).
    QObject *m_pollutantRegistry = nullptr;
    void *m_pollutantRegistryEngineHandle = nullptr;

    // Land-use registry mirror ([LANDUSES]).
    QObject *m_landUseRegistry = nullptr;
    void *m_landUseRegistryEngineHandle = nullptr;

    // Aquifer registry mirror ([AQUIFERS]).
    QObject *m_aquiferRegistry = nullptr;
    void *m_aquiferRegistryEngineHandle = nullptr;

    // Snowpack registry mirror ([SNOWPACKS]).
    QObject *m_snowpackRegistry = nullptr;
    void *m_snowpackRegistryEngineHandle = nullptr;

    // Inlet registry mirror ([INLETS]).
    QObject *m_inletRegistry = nullptr;
    void *m_inletRegistryEngineHandle = nullptr;

    // LID-control registry mirror ([LID_CONTROLS]).
    QObject *m_lidControlRegistry = nullptr;
    void *m_lidControlRegistryEngineHandle = nullptr;

    // User-flags store — see ensureUserFlagsModel().
    openswmmvis::ui::UserFlagsModel *m_userFlagsModel = nullptr;
    void *m_userFlagsModelEngineHandle = nullptr;
};

Q_DECLARE_METATYPE(SWMMModelLayer *)
Q_DECLARE_METATYPE(SWMMElementSymbol)
Q_DECLARE_OPERATORS_FOR_FLAGS(SWMMModelLayer::QsgKinds)

#endif // SWMMMODELLAYER_H
