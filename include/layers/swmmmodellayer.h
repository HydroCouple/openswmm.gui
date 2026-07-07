/*!
 * \file   swmmmodellayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Map layer that renders an OpenSWMMCore network (nodes, links,
 *         subcatchments, rain gages) and provides geometry-editing and
 *         spatial-query APIs.
 */

#ifndef SWMMMODELLAYER_H
#define SWMMMODELLAYER_H

#include "layers/openswmmvislayer.h"
#include "layers/swmm_category.h"
#include "render/labelconfig.h"
#include "render/iattributeprovider.h"   // Slice DM.3
#include "render/markershape.h"
#include "render/legendsymbolitem.h"     // X4 — legend-as-editor facade

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// Forward declaration — nanoflann types are confined to swmmmodellayer.cpp
// so the header stays free of the nanoflann.hpp template machinery.
struct SWMMKdTrees;

#include <QColor>

#include <openswmm/engine/openswmm_callbacks.h>  // SWMM_Engine typedef
#include <QFont>
#include <QMap>
#include <QPen>
#include <QBrush>
#include <QPolygonF>
#include <QSet>
#include <QVariantMap>

class OpenSWMMVisWorkspace;
class SpatialReferenceSystem;

namespace OpenSWMM::Render {
class IFeatureRenderer;
class GraduatedRenderer;   // classifyGraduatedIfNeeded — data-derived breaks.
class RuleList;   // Slice B.4 — see ruleList() override below.
enum class ClassEditKind;   // X4 — legend-as-editor facade (full def in ifeaturerenderer.h)
}

// Slice BS Phase 6.9.2 — hydrograph MVC layer. Forward-declared so the
// header doesn't drag in <QAbstractItemModel>; accessors return pointers
// that consumers can wire to QTableView / QListView. See
// include/layers/hydrographmodels.h.
class HydrographGroupListModel;
class HydrographRtkTableModel;
class HydrographIaTableModel;
class HydrographDecayTableModel;

namespace openswmmvis::ui {
class UserFlagsModel;   // [USER_FLAGS] / [USER_FLAG_VALUES] store — see ensureUserFlagsModel().
}

/*!
 * \struct SWMMElementSymbol
 * \brief Rendering style for a class of SWMM network elements.
 */
struct SWMMElementSymbol
{
    QColor  fillColor    = Qt::blue;
    QColor  outlineColor = Qt::darkBlue;
    double  outlineWidth = 1.0;
    double  size         = 8.0;    /*!< Marker diameter / line width in pixels. */
    OpenSWMM::Render::MarkerShape markerShape
                         = OpenSWMM::Render::MarkerShape::Circle;
                                   /*!< Point-marker glyph (per-kind defaults
                                        seeded in SWMMModelLayer ctor). Ignored
                                        for line and polygon categories. */
    bool    showLabel    = false;
    QFont   labelFont;
    QColor  labelColor   = Qt::black;

    // Slice BI Phase 8.13.8-mini (2026-05-24) — flow-direction arrows
    // for link kinds (Conduits / Pumps / Orifices / Weirs / Outlets).
    // Ignored for point / polygon kinds. The arrow points from the
    // link's upstream node to its downstream node — i.e. follows the
    // polyline tangent at the midpoint of the visible polyline.
    bool    showArrows           = false;          /*!< Toggle off by default. */
    double  arrowSize            = 10.0;           /*!< Arrowhead length in pixels. */
    QColor  arrowColor           = QColor(34, 34, 34);  /*!< Near-black. */
    // Slice FX.1 — was `true` by default, but that gates arrows on a
    // bound `.out` (every link has flow=0 pre-simulation). Users who
    // toggle "Show flow arrows" expect arrows to appear immediately,
    // independent of whether a sim has run. The flow-positive filter
    // remains opt-in via the Arrows tab → "Only when flow > 0".
    bool    arrowOnlyWhenFlowPos = false;
};

/*!
 * \class SWMMModelLayer
 * \brief Renders the SWMM network elements (nodes, links, subcatchments, rain gages)
 *        for one OpenSWMMCore model.
 * \details The layer uses the coordinate frame of the OpenSWMMCore model as its
 *          native CRS.  When the canvas CRS differs, coordinates are reprojected
 *          using GDAL's OGRCoordinateTransformation.
 *
 *          The layer also provides:
 *          - Selection tracking (highlighted with a secondary colour).
 *          - Identify-by-point returning element attributes as QVariantMap.
 *          - Label display driven by element names.
 */
class SWMMModelLayer : public OpenSWMMVisLayer,
                       public OpenSWMM::Render::IAttributeProvider  // Slice DM.3
{
    Q_OBJECT
    Q_INTERFACES(OpenSWMM::Render::IAttributeProvider)  // Slice DM.3

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
     *        (`SWMMModelLayer::Category`, `SWMMModelLayer::CatJunctions`,
     *        …) continue to compile unchanged.
     */
    using Category = OpenSWMMVis::SwmmCategory;
    static constexpr Category CatJunctions     = OpenSWMMVis::CatJunctions;
    static constexpr Category CatOutfalls      = OpenSWMMVis::CatOutfalls;
    static constexpr Category CatStorage       = OpenSWMMVis::CatStorage;
    static constexpr Category CatDividers      = OpenSWMMVis::CatDividers;
    static constexpr Category CatConduits      = OpenSWMMVis::CatConduits;
    static constexpr Category CatPumps         = OpenSWMMVis::CatPumps;
    static constexpr Category CatOrifices      = OpenSWMMVis::CatOrifices;
    static constexpr Category CatWeirs         = OpenSWMMVis::CatWeirs;
    static constexpr Category CatOutlets       = OpenSWMMVis::CatOutlets;
    static constexpr Category CatSubcatchments = OpenSWMMVis::CatSubcatchments;
    static constexpr Category CatRainGages     = OpenSWMMVis::CatRainGages;
    static constexpr Category NumCategories    = OpenSWMMVis::NumCategories;

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
        DataCurves        = 0,
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

    Q_PROPERTY(QString modelFilePath  READ modelFilePath  NOTIFY modelFilePathChanged)
    Q_PROPERTY(bool    showNodes      READ showNodes      WRITE setShowNodes
               NOTIFY showNodesChanged)
    Q_PROPERTY(bool    showLinks      READ showLinks      WRITE setShowLinks
               NOTIFY showLinksChanged)
    Q_PROPERTY(bool    showSubcatchments READ showSubcatchments
               WRITE setShowSubcatchments NOTIFY showSubcatchmentsChanged)
    Q_PROPERTY(bool    showRainGages  READ showRainGages  WRITE setShowRainGages
               NOTIFY showRainGagesChanged)
    Q_PROPERTY(bool    showLabels     READ showLabels     WRITE setShowLabels
               NOTIFY showLabelsChanged)

public:

    explicit SWMMModelLayer(const QString &modelFilePath,
                            OpenSWMMVisWorkspace *parent = nullptr);

    ~SWMMModelLayer() override;

    // ----- Model file -----------------------------------------------------

    [[nodiscard]] QString modelFilePath() const;
    void setModelFilePath(const QString &path);

    // ----- Self-description for the Layer Properties dialog ----------------
    [[nodiscard]] QString sourceDescription() const override;
    [[nodiscard]] QVector<QPair<QString, QString>> extendedMetadata() const override;

    /** Raw engine handle — valid only after a successful loadModel(). */
    [[nodiscard]] SWMM_Engine engine() const;

    /*!
     * \brief Loads (or reloads) the SWMM input file and rebuilds geometry caches.
     * \returns true on success.
     */
    bool loadModel(QList<QString> &warnings, QList<QString> &errors);

    /** Close and destroy the engine, clearing all geometry caches. */
    void closeEngine();

    // ----- Element visibility toggles -------------------------------------

    [[nodiscard]] bool showNodes()        const;
    void setShowNodes(bool show);

    [[nodiscard]] bool showLinks()        const;
    void setShowLinks(bool show);

    [[nodiscard]] bool showSubcatchments() const;
    void setShowSubcatchments(bool show);

    [[nodiscard]] bool showRainGages()    const;
    void setShowRainGages(bool show);

    [[nodiscard]] bool showLabels()       const;
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
    void setLabelConfig(const OpenSWMM::Render::LabelConfig &cfg) override;

    /*! Per-kind QSG render scope. Each flag means "this kind is being
     *  drawn by the QSG (GPU) overlay; the CPU SWMMLayerItem must NOT
     *  draw it".  Symmetrically, the QSG renderer (SWMMLayerQSGRenderer)
     *  uploads empty geometry for any kind NOT in the scope, so a kind
     *  is drawn by exactly one pipeline.
     *
     *  Progressive migration: nodes go QSG first, then links, then
     *  catchments. Default is empty — i.e. everything stays on the
     *  CPU path, the QSG overlay never runs, and behaviour matches
     *  the legacy renderer.
     *
     *  Slice §QSG-1 (2026-05-26) — replaces the previous bool
     *  `glRenderingEnabled` which was an all-or-nothing gate. */
    enum QsgKind : quint8 {
        QsgNone     = 0x00,
        QsgNodes    = 0x01,  ///< Junctions / outfalls / storage / dividers.
        QsgLinks    = 0x02,  ///< Conduits / pumps / orifices / weirs / outlets.
        QsgCatch    = 0x04,  ///< Subcatchments (polygons + outlet lines).
        QsgGages    = 0x08,  ///< Rain gages.
    };
    Q_DECLARE_FLAGS(QsgKinds, QsgKind)

    [[nodiscard]] QsgKinds qsgRenderKinds() const noexcept { return m_qsgKinds; }
    void setQsgRenderKinds(QsgKinds kinds);

    /*! Convenience: does the QSG overlay own this kind (i.e. is the
     *  flag set)? Hot-path helpers in SWMMLayerItem use this to skip
     *  CPU painting, and SWMMLayerQSGRenderer uses the negation to
     *  short-circuit vertex uploads. */
    [[nodiscard]] bool qsgOwnsKind(QsgKind k) const noexcept
        { return m_qsgKinds.testFlag(k); }

    /*! Back-compat shim — keeps the old all-or-nothing API working
     *  while callers (mapcanvas.cpp, swmmlayeritem.cpp, tests) are
     *  migrated to the per-kind flags. setGlRenderingEnabled(true) is
     *  equivalent to enabling every QSG kind. */
    [[nodiscard]] bool glRenderingEnabled() const noexcept
        { return m_qsgKinds != QsgNone; }
    void setGlRenderingEnabled(bool on)
        { setQsgRenderKinds(on ? QsgKinds(QsgNodes | QsgLinks | QsgCatch | QsgGages)
                               : QsgKinds(QsgNone)); }

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
     */
    [[nodiscard]] bool isObjectVisible(const QString &name) const;
    void setObjectVisible(const QString &name, bool visible);

    /*!
     * \brief Batch form — apply the same visibility to every name in
     *        \p names with a single \ref repaintRequested emission.
     *        Used by the group-header checkbox so toggling a category
     *        of 1000+ objects causes only one canvas refresh.
     */
    void setObjectsVisible(const QList<QString> &names, bool visible);

    /*!
     * \brief Names currently hidden via per-object toggles. Used by the
     *        Object Browser to seed leaf-row check states on refresh.
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
     */
    [[nodiscard]] double maxMarkerHalfBoundPx() const;

    // ----- Category-aware API (consumed by SWMMObjectTreeModel) -----------

    /*!
     * \brief Number of objects in the given category. O(1).
     */
    [[nodiscard]] int categoryCount(Category c) const;

    /*!
     * \brief Object name at (category, row). O(1) — reads through the
     *        per-category index buckets populated in buildGeometryCache.
     *        Returns an empty string if the indices are out of range.
     */
    [[nodiscard]] QString objectNameAt(Category c, int row) const;

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
     */
    [[nodiscard]] int dataObjectCount(DataCategory c) const;

    /*!
     * \brief Slice BM.0 — name of a non-spatial data object at
     *        (category, row), or an empty string when out of range.
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
     *          `NodeCompoundEditDialog::populateTimeSeriesCombo` (which
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
     */
    [[nodiscard]] QStringList tableIdsOfType(int tableType) const;

    /*!
     * \brief Create a new data object on the engine (DB.4b).
     *
     * \details Moves the per-DataCategory engine-commit switch out of
     *          `ObjectBrowserPanel::addNewDataObject` so callers
     *          OTHER than the Object Browser (e.g. the picker
     *          buttons in NodeCompoundEditDialog) can create new TS /
     *          patterns / UH groups inline without dispatching upward.
     *
     *          The options map matches the keys produced by
     *          `NewDataObjectDialog::getNew(...)` — e.g. "patternType",
     *          "curveType", "lidType", "units", "inletType", "skeleton",
     *          "rainGage", "response".
     *
     *          On success this method does NOT emit a refresh signal or
     *          update Object Browser selection — callers that own those
     *          flows (the panel) layer those side effects on top.
     *
     * \param c        Data-object category to create.
     * \param name     Name for the new object (caller-supplied, typically
     *                 from `suggestUniqueDataObjectName`).
     * \param options  Per-category creation options (see above).
     * \param outError If non-null, populated with an error message on
     *                 failure (empty on success).
     * \returns true on success, false otherwise.
     */
    [[nodiscard]] bool createDataObject(DataCategory c,
                                          const QString &name,
                                          const QVariantMap &options,
                                          QString *outError = nullptr);

    /*!
     * \brief Aggregate check state for a category: Checked when every
     *        member is visible, Unchecked when every member is hidden,
     *        PartiallyChecked otherwise. O(1) — derived from the
     *        per-category hidden-count counter that setObjectVisibleAt /
     *        setCategoryVisible / setObjectVisible all maintain.
     */
    [[nodiscard]] Qt::CheckState categoryCheckState(Category c) const;

    /*!
     * \brief Toggle visibility for a single leaf referenced by
     *        (category, row). Updates m_hiddenObjects plus the
     *        category hidden count and emits repaintRequested() on change.
     *        Preferred over setObjectVisible(name, …) when the caller
     *        already knows the category — avoids the name→(cat,row)
     *        lookup on the hot path of QTreeView::setData().
     */
    void setObjectVisibleAt(Category c, int row, bool visible);

    /*!
     * \brief Bulk-toggle every member of a category. Used by the
     *        group-header checkbox in the Object Browser; emits a single
     *        repaintRequested() regardless of how many objects the
     *        category contains.
     */
    void setCategoryVisible(Category c, bool visible);

    /*!
     * \brief Per-kind (sub-layer) opacity in [0,1], applied on top of the
     *        layer opacity. Lets the user fade, say, Conduits independently
     *        of Junctions via the layer-tree Opacity column. Default 1.0.
     *        The painter multiplies each feature's alpha by this; setting it
     *        flags a rebuild + repaint.
     */
    [[nodiscard]] qreal categoryOpacity(Category c) const;
    void setCategoryOpacity(Category c, qreal opacity);

    /*!
     * \brief Locate an object by name, returning true and writing
     *        \p cat + \p row on success. O(1) hash lookup against the
     *        `m_objectLocation` map built at cache time. Used by the
     *        SelectionManager → tree model bridge to convert
     *        SWMMObjectRef into a QModelIndex without rescanning the SoA.
     */
    bool findObjectLocation(const QString &name,
                            Category *cat, int *row) const;

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
     */
    [[nodiscard]] QVector<Category> categoryOrder() const;

    /*! Replace the full category order. Rejects an input that doesn't
     *  cover every enum value exactly once (safety net so a malformed
     *  vector can't silently drop categories). Emits `categoryOrderChanged`
     *  on success; Object Browser reacts via `modelReset`. */
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
     */
    void setObjectOrder(Category cat, const QVector<int> &soaIndices);

    /*! Drop the user override for this category; display falls back
     *  to the default per-category index bucket. */
    void clearObjectOrder(Category cat);

    /*! Read-only access to the current override for a category. Empty
     *  when no override is installed. */
    [[nodiscard]] QVector<int> objectOrder(Category cat) const;

    /*! Default SoA permutation for a category (visible-row → SoA
     *  index) when NO user override is installed. Used by the tree
     *  model's drag-drop handler as the starting point for a drag
     *  on a category that hasn't been reordered before. */
    [[nodiscard]] QVector<int> defaultObjectOrder(Category cat) const;

    // ----- Symbology ------------------------------------------------------

    [[nodiscard]] SWMMElementSymbol junctionSymbol()   const;
    void setJunctionSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol outfallSymbol()    const;
    void setOutfallSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol storageSymbol()    const;
    void setStorageSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol dividerSymbol()    const;
    void setDividerSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol conduitSymbol()    const;
    void setConduitSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol pumpSymbol()       const;
    void setPumpSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol orificeSymbol()    const;
    void setOrificeSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol weirSymbol()       const;
    void setWeirSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol subcatchmentSymbol() const;
    void setSubcatchmentSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol rainGageSymbol()   const;
    void setRainGageSymbol(const SWMMElementSymbol &s);

    /*! Slice U-4 — expose the 11 per-kind SWMMElementSymbol adapters as
     *  styleable subjects for the unified LayerStyleDialog. */
    [[nodiscard]] std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
        styleSubjects() override;

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
     */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *renderer() const override;

    /*!
     * \brief Replaces the current renderer.
     * \details The layer takes ownership.  A null pointer is rejected
     *          (silent no-op) so renderer() never returns nullptr.
     *          Emits \ref rendererChanged() when the pointer actually changes.
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

    /*! Returns the renderer for one Category kind (never null). */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *kindRenderer(Category c) const;

    /*! Replaces the per-kind renderer for \p c. Takes ownership; null is
     *  rejected (silent no-op). When the new renderer is a SingleSymbol,
     *  the layer extracts colour/outline/size back to the legacy
     *  `SWMMElementSymbol` field for that kind so the existing paint
     *  loop reflects the change. Emits rendererChanged + repaintRequested. */
    void setKindRenderer(Category c,
                          std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r);

    /*! Re-seed the per-kind renderer for \p c from the matching
     *  SWMMElementSymbol default (factory glyph). Equivalent to "Reset
     *  Kind to Defaults" in the layer-tree context menu. */
    void resetKindRendererToDefaults(Category c);

    /*! Convenience: stable string key used for MultiKindRenderer keying
     *  + .oswp persistence. e.g. CatJunctions → "Junctions". */
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
    [[nodiscard]] QList<OpenSWMM::Render::LegendSymbolItem> legendSymbolItems() const;
    [[nodiscard]] bool   supportsClassEdit(OpenSWMM::Render::ClassEditKind kind) const;
    [[nodiscard]] QColor colorForClass(const QString &classKey) const;
    void                 setColorForClass(const QString &classKey, const QColor &color);
    [[nodiscard]] qreal  sizeForClass(const QString &classKey) const;
    void                 setSizeForClass(const QString &classKey, qreal size);

    // ----- Rule Model (Slice B.4, Phase B) --------------------------------
    //
    // 11 kindRenderer slots → 11 Rules, one per Category. The RuleList
    // is lazy-built on first access; each Rule holds a CLONE of the
    // matching kindRenderer at construction time. Rule-side renderer
    // swaps (via Z.3b's setRendererById) propagate back to the layer
    // via setKindRenderer, which keeps the legacy paint-time + override
    // cache code paths intact.
    //
    // Limitation: external calls to setKindRenderer (e.g. from undo
    // commands) don't re-sync the RuleList. The user must reopen the
    // Layer Style dialog to see fresh state — acceptable for B.4; full
    // bidirectional sync lands in a follow-up if it surfaces a real
    // problem.
    [[nodiscard]] OpenSWMM::Render::RuleList *ruleList() override;
    [[nodiscard]] const OpenSWMM::Render::RuleList *ruleList() const override;

    // ----- IAttributeProvider (Slice DM.3) --------------------------------
    //
    // Returns the per-kind static engine fields a user can theme by
    // (length, slope, diameter, invertElev, area, impervPct, …). All
    // entries are isDynamic=false (statics don't change per animation
    // frame). Drives the attribute combo in the Graduated / Categorized
    // renderer panels — see RENDERING_DIALOG_DEMO_PLAN.md §2.
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

    /*! True if flow-direction arrows are enabled for the link kind. */
    [[nodiscard]] bool linkArrowsEnabled(Category c) const;

    /*! Toggle flow-direction arrows for a link kind. Emits repaintRequested. */
    void setLinkArrowsEnabled(Category c, bool enabled);

    // Slice FX.1 — per-kind arrow size / colour / flow-positive filter.
    // Mirror the linkArrowsEnabled getter+setter shape; setters emit
    // repaintRequested when the underlying field actually changes.
    [[nodiscard]] double linkArrowSize(Category c) const;
    void setLinkArrowSize(Category c, double pixels);
    [[nodiscard]] QColor linkArrowColor(Category c) const;
    void setLinkArrowColor(Category c, const QColor &col);
    [[nodiscard]] bool   linkArrowOnlyWhenFlowPos(Category c) const;
    void setLinkArrowOnlyWhenFlowPos(Category c, bool onlyPos);

    /*! Live link-flow accessor used by the painter's `arrowOnlyWhenFlowPos`
     *  short-circuit (Phase 8.13.8-α). Returns 0 when the engine handle
     *  is null or the index is out of range. */
    [[nodiscard]] double linkFlow(int linkIdx) const;

    /*! Slice BI Phase 8.13.6.4 — per-feature paint-loop refactor.
     *
     *  True when the kind's renderer is non-SingleSymbol (Graduated /
     *  Categorized / RuleBased) and the layer has pre-computed an
     *  override colour for every feature in this kind. The painter
     *  reads this flag to decide whether to use the legacy bucketed
     *  fast path (false, single pen+brush per kind) or the per-
     *  feature override path (true). */
    [[nodiscard]] bool kindUsesOverrides(Category c) const;

    /*! Returns the cached per-feature colour for one feature in kind
     *  \p c, or an invalid QColor when no override exists. The painter
     *  uses this for the per-feature paint path. Index is the SoA
     *  index (matches the order categoryCount / objectNameAt use). */
    [[nodiscard]] QColor featureColor(Category c, int idx) const;

    /*! Slice BI Phase 8.13.43-α — per-feature SIZE override (pixels).
     *  Returns a negative sentinel (-1.0) when no override exists for
     *  this feature; positive values are absolute pixel sizes (the
     *  painter scales the kind's static glyph radius by `size / static`). */
    [[nodiscard]] double featureSize(Category c, int idx) const;

    /*! M3 — per-feature marker shape sampled from the renderer (Categorized /
     *  Rule-based). Returns the MarkerShape as int, or -1 when the renderer
     *  has no per-feature shape override (use the kind's base shape). */
    [[nodiscard]] int featureShape(Category c, int idx) const;

    /*! Slice Z.5b-paint-graduated — per-feature line OFFSET override
     *  (pixels). Returns 0.0 when no offset is configured for this
     *  feature; positive = right of forward direction, negative =
     *  left. The paint host (SWMMLayerItem) reads this per visible
     *  link when the kind uses Graduated/Categorized renderers; the
     *  fast SingleSymbol path reads the kind's first symbol layer
     *  directly (via lineOffsetForKindRenderer). */
    [[nodiscard]] double featureOffset(Category c, int idx) const;

    /*! True when any feature in kind \p c has a non-zero offset
     *  override. The painter consults this to decide whether to take
     *  the per-feature slow path; when false the kind keeps its
     *  legacy fast path. */
    [[nodiscard]] bool kindHasAnyOffset(Category c) const;

    /*! Recomputes the per-feature colour AND per-feature size override
     *  caches for one kind, by iterating every feature and calling
     *  `kindRenderer(c)->symbolFor(featureRef, attrs)`. Called
     *  automatically from setKindRenderer when the new renderer is
     *  non-SingleSymbol or has data-defined size enabled. */
    void rebuildKindFeatureColors(Category c);

    /*! Data-derive a GraduatedRenderer's breaks + value range for kind \p c
     *  when they are not yet set (empty breaks). Gathers the renderer's
     *  classifyAttribute() across this kind's features (static model fields
     *  via identifyByName) and calls GraduatedRenderer::classifyIfNeeded.
     *  Shared by rebuildKindFeatureColors (paint cache) and the Rule→layer
     *  rendererReplaced handler (editor reads the Rule's renderer) so both
     *  see the same data-derived classification. No-op when already
     *  classified, the attribute is empty, or it isn't a static field. */
    void classifyGraduatedIfNeeded(Category c,
                                   OpenSWMM::Render::GraduatedRenderer *g);

    // ----- Selection ------------------------------------------------------

    /*!
     * \brief Returns the names of currently selected network elements.
     */
    [[nodiscard]] QStringList selectedElementNames() const;

    /*!
     * \brief Selects network elements by name (replaces prior selection).
     */
    void setSelectedElementNames(const QStringList &names);

    void clearSelection();

    // ----- Identify -------------------------------------------------------

    /*!
     * \brief Returns attributes of the network element closest to the map point.
     * \param mapX        X in canvas CRS.
     * \param mapY        Y in canvas CRS.
     * \param canvasSRS   Canvas CRS (may be nullptr).
     * \param tolerance   Search radius in map units.
     * \returns           Attribute map, or an empty map if nothing was found.
     *                    Includes "elementType", "elementName", and all SWMM properties.
     */
    [[nodiscard]] QVariantMap identifyAt(double mapX, double mapY,
                                         const SpatialReferenceSystem *canvasSRS,
                                         double tolerance = 1e-6) const;

    /*!
     * \brief Convenience overload — no CRS needed.
     */
    [[nodiscard]] QVariantMap identifyAt(double mapX, double mapY,
                                         double tolerance = 1e-6) const;

    /*!
     * \brief Identify an object by name (instead of by map coordinates).
     * \details Used by panels that already know the object reference and just
     *          need its attribute map (Object Browser → PropertiesPanel).
     *          Returns an empty map if the name doesn't match any cached
     *          node / link / subcatchment / gage.
     */
    [[nodiscard]] QVariantMap identifyByName(const QString &name) const;

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
     */
    [[nodiscard]] QStringList nodesInRect(double canvasMinX, double canvasMinY,
                                          double canvasMaxX, double canvasMaxY) const;

    /*!
     * \brief Nearest-neighbor snap query for vertex editing.
     *
     * \details Searches for the closest node (via the KD-tree, O(log N))
     *          and the closest link interior vertex (bbox-filtered linear
     *          scan) within \p mapRadius of (\p mapX, \p mapY).  All
     *          coordinates are in layer CRS — the same system that
     *          MapTool::toMapCoords() and m_nodes[i].x/y use.
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
     *  other vertices on the same object. */
    [[nodiscard]] bool snapNearestPoint(double mapX, double mapY, double mapRadius,
                                        QPointF &outPt,
                                        std::optional<QPointF> excludePos = std::nullopt) const;

    /*!
     * \brief Names of all rain gages whose coordinates fall inside the
     *        canvas-CRS rectangle. Same semantics as nodesInRect.
     */
    [[nodiscard]] QStringList gagesInRect(double canvasMinX, double canvasMinY,
                                          double canvasMaxX, double canvasMaxY) const;

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
     */
    [[nodiscard]] QStringList linksInRect(double canvasMinX, double canvasMinY,
                                          double canvasMaxX, double canvasMaxY) const;

    /*!
     * \brief Names of all subcatchments whose polygon bbox overlaps
     *        the canvas-CRS rectangle. Same caching pattern as
     *        `linksInRect`.
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

    [[nodiscard]] QStringList nodesInPolygon(const QPolygonF &canvasPoly) const;
    [[nodiscard]] QStringList gagesInPolygon(const QPolygonF &canvasPoly) const;
    [[nodiscard]] QStringList linksInPolygon(const QPolygonF &canvasPoly) const;
    [[nodiscard]] QStringList subcatchmentsInPolygon(const QPolygonF &canvasPoly) const;

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    void populateScene(QGraphicsScene *scene,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    void depopulateScene(QGraphicsScene *scene) override;

    void refreshScene(QGraphicsScene *scene,
                      const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    /*!
     * \brief Re-read every node / link / subcatchment coordinate from the
     *        engine and rebuild the geometry cache + extent.
     * \details Use after a coordinate-mutating operation (CRS reproject, bulk
     *          coordinate edit) so cached vertices match engine state.
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
     */
    bool transformCanvasToLayer(double cx, double cy,
                                double &lx, double &ly) const;

    /*!
     * \brief Transform every vertex of a canvas-CRS polygon into layer CRS.
     * \details Helper for the *InPolygon queries so the point-in-polygon test
     *          runs in the same coordinate frame as the cached feature
     *          coordinates. Uses transformCanvasToLayer per vertex.
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
     */
    bool transformLayerToCanvas(double lx, double ly,
                                double &cx, double &cy) const;

    /*!
     * \brief Classify a name into its SWMM object class via O(1) lookup
     *        against `m_objectLocation`.
     * \details Used by the SelectionManager bridge to translate the layer's
     *          name-only selection set into typed SWMMObjectRefs. Hot path
     *          on rubber-band selection (called once per selected name);
     *          must stay O(1) — do not regress to a linear SoA scan.
     * \return  0=Unknown, 1=Node, 2=Link, 3=Subcatchment, 4=RainGage —
     *          matches the SWMMObjectRef::ObjectType enum values.
     */
    [[nodiscard]] int objectTypeFor(const QString &name) const;

    // ----- Geometry editing API (Phase 2) --------------------------------

    /*!
     * \brief Returns the cache index of the node with the given name, or -1
     *        if none matches.
     */
    [[nodiscard]] int nodeIndex(const QString &name) const;

    /*!
     * \brief Returns the cache index of the link with the given name, or -1
     *        if none matches.
     */
    [[nodiscard]] int linkIndex(const QString &name) const;

    /*!
     * \brief Cached layer-CRS coordinate of a node by index.
     * \returns true on success; x / y untouched on failure.
     */
    bool cachedNodeCoord(int idx, double *x, double *y) const;

    /*!
     * \brief Layer-CRS position of any named element (node, gage, or link
     *        midpoint) by object name.
     * \details Uses the internal name→SoA hash for O(1) lookup. For links
     *          the midpoint of the cached polyline is returned.
     * \returns true on success; x / y untouched on failure.
     */
    bool elementPosition(const QString &name, double *x, double *y) const;

    /*!
     * \brief Cached layer-CRS polyline of a link by index, including the
     *        endpoint coordinates of its from/to nodes.
     */
    [[nodiscard]] QVector<QPointF> cachedLinkPolyline(int idx) const;

    /*!
     * \brief Cached interior-only polyline of a link (endpoints stripped).
     * \details The engine's set_link_vertices API writes interior points
     *          only; this helper returns the corresponding subsection of
     *          the cached polyline for round-trip edits.
     */
    [[nodiscard]] QVector<QPointF> cachedLinkInteriorVertices(int idx) const;

    /*!
     * \brief Cached layer-CRS polygon of a subcatchment by index. Vertex
     *        order matches the .inp [Polygons] section. Returns empty
     *        when \p idx is out of range or the subcatchment has no
     *        polygon (engine permits coordinate-less subcatchments).
     */
    [[nodiscard]] QVector<QPointF> cachedSubcatchVertices(int idx) const;

    /*!
     * \brief Number of subcatchments cached. Equivalent to
     *        `categoryCount(CatSubcatchments)` but provided alongside
     *        \ref cachedSubcatchVertices for direct iteration.
     */
    [[nodiscard]] int cachedSubcatchCount() const;

    /*! Monotonically increasing counter, bumped at the end of every
     *  rebuildSceneCoords() call.  Renderers can compare against a cached
     *  value to cheaply detect whether scene geometry has changed without
     *  storing a pointer or connecting a signal. */
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
     */
    [[nodiscard]] QString getOption(const QByteArray &key,
                                    const QString    &fallback = {}) const;

    /*!
     * \brief Write a single SWMM OPTIONS key. On success emits
     *        `optionsChanged({key})`. Returns true on success, false if
     *        the engine isn't open or rejected the value.
     *
     *        Batch-apply should prefer `setOptions(QMap)` to emit a
     *        single multi-key signal per round-trip.
     */
    bool setOption(const QByteArray &key, const QString &value);

    /*!
     * \brief Batch write — applies a set of key/value pairs, then emits
     *        `optionsChanged(changedKeys)` once. Returns the number of
     *        keys the engine accepted.
     */
    int  setOptions(const QMap<QByteArray, QString> &values);

    /*!
     * \brief Returns the conduit length recorded in the engine, or -1 if
     *        the link is not a conduit / is out of range.
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
     */
    [[nodiscard]] double polylineLengthInModelUnits(
        const QVector<QPointF> &vertices) const;

    /*!
     * \brief Returns true when the link at \p linkIdx is a conduit (the
     *        only link type whose length is stored as an independent
     *        geometry value and is therefore eligible for auto-length).
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
     *        SWMM object id (matches selection-bus `SWMMObjectRef::name`).
     */
    struct PickResult {
        bool     valid    = false;
        Category cat      = NumCategories;
        int      soaIndex = -1;
        QString  name;
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
    [[nodiscard]] PickResult pickAt(double sceneX, double sceneY,
                                    double tolerance) const;

    /*! Live-preview a node move — mutates the cached SoA coord plus
     *  every attached link's endpoint, emits `repaintRequested()`,
     *  but does NOT write through to the engine. Called by
     *  MapToolMoveNode on every mouseMoveEvent during a drag;
     *  MoveNodeCommand::redo commits the final position via
     *  `applyNodeMove` when the drag is released. Returns false if
     *  the index is out of range or no node cache exists. */
    bool previewNodeMove(int idx, double newX, double newY);

    /*!
     * \brief Indices of links whose from/to endpoint is the given node.
     */
    [[nodiscard]] QVector<int> linksAttachedToNode(int nodeIdx) const;

    /*!
     * \brief Which end of a link is attached to \p nodeIdx: 0 = from,
     *        1 = to, or -1 if the node is not an endpoint of the link.
     */
    [[nodiscard]] int linkEndForNode(int linkIdx, int nodeIdx) const;

    /*! Returns the from-node (upstream) engine index for \p linkIdx, or -1. */
    [[nodiscard]] int linkFromNodeIdx(int linkIdx) const;

    /*! Returns the to-node (downstream) engine index for \p linkIdx, or -1. */
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
     */
    bool applyNodeMove(int idx, double newX, double newY);

    /*!
     * \brief Write the conduit length for a link. No-op if the link is
     *        not a conduit.
     */
    bool applyLinkLength(int linkIdx, double length);

    /*!
     * \brief Convert every link's offsets between Depth and Elevation
     *        conventions, mirroring the legacy SWMM-GUI ComputeDepthOffsets /
     *        ComputeElevationOffsets (Uupdate.pas). Conduits convert both the
     *        upstream (from-node) and downstream (to-node) offsets; orifices,
     *        weirs and outlets convert only the upstream offset; pumps carry no
     *        offset and are skipped. Setting the LINK_OFFSETS option only flips
     *        a flag in the engine — the stored offset values must be recomputed
     *        here. Emits `geometryChanged()` once so the Attribute Table and
     *        Object Browser refresh in a single tick.
     * \param toElevation  true  → Depth offsets become Elevation offsets;
     *                      false → Elevation offsets become Depth offsets.
     */
    void convertLinkOffsets(bool toElevation);

    /*!
     * \brief Slice SC.1 — Write a cross-section to a link via
     *        `swmm_link_set_xsect`. Emits `attributeChanged(linkName)` on
     *        success so the Map symbology + Attribute Table + Property
     *        Browser refresh in one tick. Returns false on engine error
     *        (e.g., bad shape code or geom param out of range).
     */
    bool applyLinkXsect(int linkIdx, int shape,
                          double g1, double g2, double g3, double g4);

    /*!
     * \brief Slice SC.1 — Write the parallel-barrels count. Emits
     *        `attributeChanged` on success.
     */
    bool applyLinkBarrels(int linkIdx, int barrels);

    /*!
     * \brief Slice SC.1 — Write the FHWA culvert chart code (0 = none).
     *        Emits `attributeChanged` on success.
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
     */
    bool applyNodeConvert(const QString &name, int newNodeType,
                          QStringList *outCleared  = nullptr,
                          QStringList *outWarnings = nullptr,
                          QString *outError = nullptr);

    /*!
     * \brief Convert link \p name to \p newLinkType (SWMM_LinkType value)
     *        via `swmm_link_convert`. Endpoints, offsets, and interior
     *        vertices are preserved by the engine. Same cache update and
     *        signal contract as applyNodeConvert().
     */
    bool applyLinkConvert(const QString &name, int newLinkType,
                          QStringList *outCleared  = nullptr,
                          QStringList *outWarnings = nullptr,
                          QString *outError = nullptr);

    /*!
     * \brief Apply interior vertices to a link: engine + cache, rebuilding
     *        the cached polyline from the node endpoints + new interior.
     */
    bool applyLinkInteriorVertices(int linkIdx, const QVector<QPointF> &interior);

    /*!
     * \brief Replace a subcatchment's polygon vertices: engine + cache.
     *        Recomputes the centroid as the vertex average.
     */
    bool applySubcatchVertices(int idx, const QVector<QPointF> &vertices);
    bool applySubcatchArea(int idx, double areaInModelUnits);

    /*!
     * \brief Add a new node: engine + cache. Engine must be OPENED.
     * \param name      Unique null-terminated node identifier.
     * \param nodeType  0=Junction, 1=Outfall, 2=Storage, 3=Divider
     *                  (matches SWMM_NodeType).
     * \param x, y      Initial coordinate in the layer CRS.
     * \param[out] outIdx  Newly assigned node index on success.
     * \returns true on success. On failure \p outIdx is -1.
     */
    bool applyNodeAdd(const QString &name, int nodeType,
                      double x, double y,
                      int *outIdx = nullptr);

    /*!
     * \brief Undo an add by removing the *tail* entry of the node cache.
     * \details Only valid immediately after applyNodeAdd when the new node
     *          is still at the end of the node list. Used by AddNodeCommand::undo
     *          while the engine lacks a general-purpose swmm_node_remove.
     *          Returns false if the tail name doesn't match \p name.
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
     */
    bool applyLinkAdd(const QString &name, int linkType,
                      const QString &fromNodeName, const QString &toNodeName,
                      const QVector<QPointF> &interiorVertices,
                      int *outIdx = nullptr);

    /*! Undo tail link add (swmm_link_pop_last). */
    bool rollbackTailLinkAdd(const QString &name);

    /*! Add a rain gage: engine + cache. */
    bool applyGageAdd(const QString &name, double x, double y,
                      int *outIdx = nullptr);

    /*! Undo tail gage add (swmm_gage_delete on tail). */
    bool rollbackTailGageAdd(const QString &name);

    /*! Add a subcatchment polygon: engine + cache. */
    bool applySubcatchAdd(const QString &name,
                          const QVector<QPointF> &polygon,
                          int *outIdx = nullptr);

    /*! Undo tail subcatch add (swmm_subcatch_delete on tail). */
    bool rollbackTailSubcatchAdd(const QString &name);

    /*!
     * \brief Rename any network element (node, link, subcatchment, or gage).
     * \details Calls the appropriate engine rename function, updates all GUI
     *          caches (geometry cache name, selection set, object-location
     *          map), and emits repaintRequested() + geometryChanged().
     * \param oldName  Current name of the element.
     * \param newName  Desired new name (must not already exist).
     * \returns        true on success; false if oldName is not found, newName
     *                 is empty, newName is already in use, or the engine
     *                 rejects the rename.
     */
    bool applyRename(const QString &oldName, const QString &newName);

    /*!
     * \brief Delete a node, cascade-deleting all attached links.
     * \details Identifies cascade links before deletion so the caller can
     *          snapshot them. Modifies engine state + all caches.
     * \param name                 Node to delete.
     * \param[out] cascadeLinkNames Names of links deleted as cascade.
     */
    bool applyNodeDelete(const QString &name,
                         QStringList *cascadeLinkNames = nullptr);

    /*! Delete a single link. */
    bool applyLinkDelete(const QString &name);

    /*! Delete a rain gage. */
    bool applyGageDelete(const QString &name);

    /*! Delete a subcatchment. */
    bool applySubcatchDelete(const QString &name);

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
     *  layer so a single hydrographChanged() fires. */
    bool applyHydrographAddGroup(const QString &name,
                                  const QString &gageName,
                                  int initialResponse);

    /*! Remove a UH group: parameter rows, gage assignment, [RDII_DECAY]
     *  rows, and any [RDII] node assignments referencing it. */
    bool applyHydrographRemoveGroup(const QString &name);

    /*! Rename a UH group; walks all four engine containers (entries,
     *  gage_assignments, rdii_decay, rdii_assigns). Rejects empty / duplicate
     *  names. */
    bool applyHydrographRenameGroup(const QString &oldName, const QString &newName);

    /*! Set, replace, or clear the rain gage assigned to a UH group. Pass an
     *  empty string to clear an existing assignment. */
    bool applyHydrographSetGage(const QString &name, const QString &gageName);

    /*! Upsert R/T/K for one (group, month, response) row. */
    bool applyHydrographSetRtk(const QString &name, int month, int response,
                                double r, double t, double k);

    /*! Upsert linear-IA (dmax, drecov, dinit) for one (group, month,
     *  response) row. */
    bool applyHydrographSetIa(const QString &name, int month, int response,
                               double dmax, double drecov, double dinit);

    /*! Remove one (group, month, response) parameter row. Idempotent. */
    bool applyHydrographRemoveEntry(const QString &name, int month, int response);

    /*! Bulk-clear every per-month parameter row for a group, preserving any
     *  existing month=-1 (ALL) row. Used by the editor when switching from
     *  per-season to ALL. */
    bool applyHydrographClearMonths(const QString &name);

    /*! Upsert one [RDII_DECAY] row for a (group, response) pair. */
    bool applyRdiiDecaySet(const QString &name, int response,
                            double k_dep, double k_0, double k_T,
                            double T_ref, double theta_rec, double T_freeze);

    /*! Remove the [RDII_DECAY] row for a (group, response) pair. Idempotent.
     *  In the GUI's MVC layer this is the "untick Active" path. */
    bool applyRdiiDecayRemove(const QString &name, int response);

    /*! Shared QAbstractListModel of UH group names. Lazily constructed; one
     *  per layer. Re-emits modelReset() on hydrographChanged(""). */
    HydrographGroupListModel  *hydrographGroupListModel();

    /*! Shared QAbstractTableModel for one (group, month) R/T/K grid (3 rows
     *  × 4 cols). Call setContext() to rebind. */
    HydrographRtkTableModel   *hydrographRtkModel();

    /*! Shared QAbstractTableModel for one (group, month) linear-IA grid (3
     *  rows × 4 cols). Call setContext() to rebind. */
    HydrographIaTableModel    *hydrographIaModel();

    /*! Shared QAbstractTableModel for one group's [RDII_DECAY] grid (3 rows
     *  × 8 cols). Call setContext() to rebind. Season-agnostic — there is
     *  no month dimension in the engine model. */
    HydrographDecayTableModel *hydrographDecayModel();

    /*! Lazy-init / re-use the TimeseriesRegistry scoped to the current
     *  engine handle. Returns nullptr if the engine isn't open. Returned
     *  as a `QObject*` to keep this header free of the timeseries include;
     *  callers downcast to `openswmmvis::timeseries::TimeseriesRegistry*`.
     *  Shared across every UI that mutates time series so all views
     *  (Object Browser, NodeCompoundEditDialog pickers, …) see the same
     *  provider instances. */
    QObject *ensureTimeseriesRegistry();

    /*! Same lazy-init pattern as ensureTimeseriesRegistry, for the
     *  PatternRegistry. */
    QObject *ensurePatternRegistry();

    /*! Same lazy-init pattern, for the CurveRegistry. */
    QObject *ensureCurveRegistry();

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
     *  is closed, `name` is empty, or the registry already holds it. */
    bool applyControlRuleAdd(const QString &name, const QString &body,
                               QString *outError = nullptr);

    /*! \brief Replace the body of an existing rule (typically the user-
     *  edited text from the code editor). Routes through the snapshot+clear
     *  +re-add round-trip so the engine's rule order is preserved. */
    bool applyControlRuleReplace(const QString &name, const QString &newBody,
                                   QString *outError = nullptr);

    /*! \brief Rename `oldName` to `newName`. Rewrites the `RULE <name>`
     *  header on the target slot's body so the engine round-trip preserves
     *  the rename. Refuses on duplicate (case-insensitive) or empty
     *  `newName`. */
    bool applyControlRuleRename(const QString &oldName, const QString &newName,
                                  QString *outError = nullptr);

    /*! \brief Drop the rule with `name` from the engine + registry.
     *  Returns false if not found. */
    bool applyControlRuleRemove(const QString &name,
                                  QString *outError = nullptr);

    /*! \brief Lazy accessor for the project-scoped `ControlRuleRegistry`.
     *  Lifetime is bound to the layer; the registry is constructed on
     *  first call and `loadFromEngine` is invoked so the providers mirror
     *  the engine's current rule list. Returned as a `QObject*` to keep
     *  this header free of the controls include; callers downcast to
     *  `openswmmvis::controls::ControlRuleRegistry*`. */
    QObject *ensureControlRuleRegistry();

    // ===== Slice BQ Phase 6.7.4 — transect MVC layer =========================
    //
    // Mirrors the curve / pattern / control-rule pattern. The applyTransect*
    // helpers are the only place engine `swmm_transect_*` mutation calls
    // live; all UI surfaces (TransectEditorDialog, SWMMTransectPropertyAdapter,
    // Object Browser) route through them and re-render off
    // `transectChanged(name)`.

    /*! \brief Create a new transect. Refuses on duplicate or empty name. */
    bool applyTransectAdd(const QString &name, QString *outError = nullptr);

    /*! \brief Rename. Refuses on duplicate (case-insensitive) or empty. */
    bool applyTransectRename(const QString &oldName, const QString &newName,
                              QString *outError = nullptr);

    /*! \brief Remove a transect by name. */
    bool applyTransectRemove(const QString &name, QString *outError = nullptr);

    bool applyTransectSetComments(const QString &name, const QString &comments);
    bool applyTransectSetRoughness(const QString &name,
                                    double nLeft, double nRight, double nChannel);
    bool applyTransectSetBankStations(const QString &name, double xLeft, double xRight);
    bool applyTransectSetEncroachmentStations(const QString &name,
                                               double xLeft, double xRight);
    bool applyTransectSetModifiers(const QString &name,
                                    double stationMul, double elevOffset, double meander);
    /*! \brief Snapshot-and-rewrite the full station list. */
    bool applyTransectSetStations(const QString &name,
                                   const QVector<QPair<double,double>> &stations);

    /*! \brief Lazy accessor for the project-scoped TransectRegistry. */
    QObject *ensureTransectRegistry();

    /*! \brief Lazy accessor for the project-scoped StreetRegistry. */
    QObject *ensureStreetRegistry();

    /*! \brief Lazy accessor for the project-scoped PollutantRegistry. */
    QObject *ensurePollutantRegistry();

    /*! \brief Lazy accessor for the project-scoped LandUseRegistry. */
    QObject *ensureLandUseRegistry();

    /*! \brief Lazy accessor for the project-scoped AquiferRegistry. */
    QObject *ensureAquiferRegistry();

    /*! \brief Lazy accessor for the project-scoped SnowpackRegistry. */
    QObject *ensureSnowpackRegistry();

    /*! \brief Lazy accessor for the project-scoped InletRegistry. */
    QObject *ensureInletRegistry();

    /*! \brief Lazy accessor for the project-scoped LidControlRegistry. */
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
     *  Returns nullptr if the engine isn't open. */
    openswmmvis::ui::UserFlagsModel *ensureUserFlagsModel();

signals:
    void modelFilePathChanged(const QString &path);
    void showNodesChanged(bool show);
    void showLinksChanged(bool show);
    void showSubcatchmentsChanged(bool show);
    void showRainGagesChanged(bool show);
    void showLabelsChanged(bool show);

    // VS.10 — labelConfigChanged() is inherited from OpenSWMMVisLayer.

    void selectionChanged(const QStringList &selectedNames);
    void modelLoaded();
    void modelLoadError(const QString &errorMessage);
    /*! \brief Slice BI Phase 8.13.6.5 — emitted when setRenderer() swaps the
     *         renderer pointer. */
    void rendererChanged();

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
     */
    void optionsChanged(const QStringList &keys);

    /*! Emitted when `setCategoryOrder()` accepts a new vector. The
     *  Object Browser tree model listens and reshapes without
     *  recomputing the per-category hidden counts (category membership
     *  didn't change, only the display order). */
    void categoryOrderChanged();

    /*! Emitted whenever the set of elements changes — an add, remove,
     *  or rollback of any node / link / gage / subcatchment. Panels
     *  that show element lists (Object Browser, Attribute Table) connect
     *  to this signal and call refresh() so their views stay in sync
     *  without polling. */
    void geometryChanged();

    /*! Emitted after a single object's attribute is mutated (e.g. conduit
     *  length or subcatchment area recalculated from geometry). The attribute
     *  table and property browser connect to this signal so they can refresh
     *  just the affected row/adapter without a full model rebuild. */
    void attributeChanged(const QString &objectName);

    /*! Slice §QSG-1 — fired whenever setQsgRenderKinds() flips one or
     *  more kinds. MapCanvas listens so it can short-circuit the
     *  per-frame QSG repaint+grab when the scope is empty, and the
     *  CPU SWMMLayerItem listens so it can force a repaint that
     *  re-evaluates its skip-kind branches. */
    void qsgRenderKindsChanged(QsgKinds kinds);

    /*! Slice BS Phase 6.9.2 — emitted whenever any [HYDROGRAPHS] /
     *  [RDII_DECAY] / [RDII] mutation lands in the engine via one of the
     *  applyHydrograph* / applyRdiiDecay* helpers. Empty uhName means
     *  "rebuild everything" (used by rename, since the old name disappears).
     *  All hydrograph models listen here and refresh; consumer UIs
     *  (Object Browser, property panel, NodeCompoundEditDialog picker, etc.)
     *  bind to those models and never poll the engine directly. */
    void hydrographChanged(const QString &uhName);

    /*! Slice BR Phase 6.8.1 — emitted whenever any [CONTROLS] mutation lands
     *  in the engine via one of the applyControlRule* helpers. The argument
     *  is the rule name affected, or empty for "rebuild everything"
     *  (rename / remove / clear). The list-model + SWMMControlRulePropertyAdapter
     *  + future RulesEditorDialog subscribe here and re-render off the same
     *  ControlRuleRegistry, so inline-edit-in-property-panel and
     *  edit-in-dialog converge through a single signal. */
    void controlRulesChanged(const QString &ruleName);

    /*! Slice BQ Phase 6.7.4 — emitted whenever any [TRANSECTS] mutation
     *  lands via one of the applyTransect* helpers. Empty string means
     *  "rebuild everything" (rename / remove). All transect models
     *  subscribe here and refresh; consumer UIs (Object Browser,
     *  property panel, TransectEditorDialog) bind to those models. */
    void transectChanged(const QString &transectName);

private:
    // X4 — decode a kind-qualified legend class key ("<kindKey><sep><inner>")
    // back to its Category + inner class key. Returns false when the key
    // isn't kind-qualified or the kind is unknown.
    bool decodeLegendClassKey(const QString &key, Category *catOut,
                              QString *innerOut) const;

    struct NodeGeom    { double x, y; int objectType; int nodeType; QString name; };
    struct LinkGeom {
        QVector<QPointF> vertices;   // interior bend points only (no node endpoints)
        int              linkType    = -1;
        int              fromNodeIdx = -1;
        int              toNodeIdx   = -1;
        QString          name;
    };
    struct CatchGeom   { QVector<QPointF> vertices; QString name; };

    /*! Scene-space drainage connector: PIA of subcatchment → outlet node/subcatchment. */
    struct OutletLine  { QLineF line; int catchIdx; };

    void buildGeometryCache();
    void rebuildTransform(const SpatialReferenceSystem *canvasSRS);
    void rebuildKdTrees() const;  ///< (Re-)build the nanoflann node + gage trees.
    void ensureKdTrees()  const;  ///< Rebuild only if m_kdDirty is set.

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
     *        (toScene(mx, my) = QPointF(mx, -my)) so SWMMLayerItem::paint
     *        can hand them straight to QPainter::drawLines without any
     *        per-vertex math.
     */
    void rebuildSceneCoords();
    void refreshSceneCoordsForNode(int nodeIdx);
    void refreshSceneCoordsForLink(int linkIdx);
    void refreshSceneCoordsForSubcatch(int catchIdx);
    void refreshCatchOutletLinesForNode(int nodeIdx);

    // Incremental scene/cache mutations — O(1) per element plus an
    // O(L) spatial-grid touch where unavoidable. These let single
    // add / rename / delete operations skip the full O(N·V) OGR
    // re-transform that rebuildSceneCoords() does on every feature.
    void appendNodeSceneEntry();     ///< For new tail entry in m_nodes.
    void appendLinkSceneEntry();     ///< For new tail entry in m_links.
    void appendCatchSceneEntry();    ///< For new tail entry in m_catchments.
    void appendGageSceneEntry();     ///< For new tail entry in m_gages.
    void compactNodeSceneEntry(int nodeIdx);
    void compactLinkSceneEntry(int linkIdx);
    void compactCatchSceneEntry(int catchIdx);
    void compactGageSceneEntry(int gageIdx);

    /*! Update name-keyed indices (m_objectLocation, m_nameToSoa,
     *  m_hiddenObjects) when a single element is renamed. Geometry
     *  is unchanged, so this is the only work needed — caller must
     *  NOT also call rebuildCategoryIndex() or buildGeometryCache(). */
    void renameInIndices(const QString &oldName, const QString &newName);

    /*! Recompute m_extent from current SoA + bbox caches without
     *  doing any OGR transform. Used by incremental delete paths. */
    void recomputeExtentFromCaches();

    // Incremented at the end of every rebuildSceneCoords() call.
    // SWMMLayerQSGRenderer uses this to invalidate its subcatchment
    // triangulation cache without needing a signal or pointer comparison.
    quint64 m_geomRevision = 0;

    /*!
     * \brief Uniform-grid spatial index over scene-space link bboxes.
     *        Phase A.1 of the 5M-rendering plan (docs/RENDERING_5M_PLAN.md).
     *
     *        SWMMLayerItem::paint queries `query(exposed)` to get the SoA
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
    struct LinkSpatialGrid
    {
        QRectF       extent;         ///< total bbox covered by the grid (scene space)
        double       cellW = 0.0;
        double       cellH = 0.0;
        int          cols  = 0;
        int          rows  = 0;
        QVector<QVector<int>> cells; ///< size cols*rows; cell (cx,cy) -> link SoA indices

        void clear() { extent = {}; cellW = cellH = 0.0; cols = rows = 0; cells.clear(); }
        [[nodiscard]] bool isEmpty() const { return cells.isEmpty(); }

        /*! Rebuild the grid from a parallel array of scene-space bboxes
         *  (one per link, indexed by SoA position). Called by
         *  rebuildSceneCoords(). */
        void rebuild(const QVector<QRectF> &linkBBoxes);

        /*! Indices of links whose cached bbox intersects \p rect. Order
         *  is grid-traversal order, not SoA order — paint can rely on
         *  this to skip the bbox-intersect check inside its loop. */
        [[nodiscard]] QVector<int> query(const QRectF &rect) const;
    };
    LinkSpatialGrid              m_linkGrid;

    /*!
     * \brief Rebuild the per-category index buckets (m_nodesByType,
     *        m_linksByType), the name→(category, row) lookup, and the
     *        per-category hidden-count array from m_hiddenObjects.
     *        Called whenever the SoA is repopulated (loadModel / add /
     *        remove). Independent of the extent computation in
     *        buildGeometryCache().
     */
    void rebuildCategoryIndex();

    SWMM_Engine                  m_engine          = nullptr;

    QString                      m_modelFilePath;
    bool                         m_showNodes       = true;
    bool                         m_showLinks       = true;
    bool                         m_showSubcatchments = true;
    bool                         m_showRainGages   = true;
    bool                         m_showLabels      = false;
    // VS.10 — m_labelConfig moved to OpenSWMMVisLayer (base owns it now).
    // Per-kind QSG scope (see QsgKind / qsgRenderKinds() above).
    // Default empty means the QSG overlay never runs and every kind is
    // drawn by the CPU SWMMLayerItem path — matches pre-§QSG-1 behaviour
    // and is what the Preferences toggle inverts when the user enables
    // the experimental GPU path.
    QsgKinds                     m_qsgKinds = QsgNone;

    // Slice O — per-object hidden set. Names listed here are skipped by
    // populateScene. Object names are unique across a SWMM model, so a
    // flat QSet<QString> covers nodes / links / subcatchments / gages
    // uniformly.
    QSet<QString>                m_hiddenObjects;

    QVector<NodeGeom>            m_nodes;
    QVector<LinkGeom>            m_links;
    QVector<CatchGeom>           m_catchments;
    QVector<NodeGeom>            m_gages;

    // Per-feature bbox caches, parallel to m_links / m_catchments.
    // Computed once in buildGeometryCache() and refreshed on edits
    // that touch coords (applyNodeMove / applyLinkInteriorVertices /
    // applyNodeAdd). linksInRect / subcatchmentsInRect iterate these
    // arrays directly — no per-call name lookup, no per-call vertex
    // loop. Big-model rubber-band selects went from O(N²) (linkIndex
    // linear scan + per-link vertex bbox) to O(N) with constant work
    // per item.
    QVector<MapExtent>           m_linkBboxes;
    QVector<MapExtent>           m_catchBboxes;

    // Scene-space coordinate cache, parallel to the SoAs above. Computed
    // once in rebuildSceneCoords() (on geometry change or CRS change) and
    // refreshed incrementally by edit paths. SWMMLayerItem::paint reads
    // from these directly — no per-vertex Transform()/toScene() call on
    // the paint hot path. Per-feature scene-space bounding rects support
    // viewport cull without a per-paint per-link bbox compute.
    QVector<QPointF>             m_nodeScenePts;
    QVector<QRectF>              m_linkSceneBBoxes;
    QVector<QVector<QPointF>>    m_catchScenePts;
    QVector<QRectF>              m_catchSceneBBoxes;
    QVector<QPointF>             m_gageScenePts;
    QVector<OutletLine>          m_catchOutletLines;  ///< PIA → outlet, built in rebuildSceneCoords.

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
    std::vector<double>          m_linkSceneFlat;
    std::vector<uint32_t>        m_linkVertexOffset;   // size == m_links.size()
    std::vector<uint32_t>        m_linkVertexCount;    // size == m_links.size()

    // Phase A.2 of the 5M-rendering plan: selection / hidden state as
    // dense byte arrays parallel to the SoAs. SWMMLayerItem::paint reads
    // these directly instead of hashing each name into a QSet<QString>
    // every frame. Maintained alongside m_selectedNames / m_hiddenObjects
    // — those QString-keyed members remain the canonical state (used by
    // identify, attribute panel, persistence) but the flag arrays are
    // the fast path on the paint loop. Kept coherent by
    // setSelectedElementNames / setObjectVisible* / rebuildCategoryIndex.
    std::vector<uint8_t>         m_nodeSelectedFlag;   // 1 if m_nodes[i] selected
    std::vector<uint8_t>         m_linkSelectedFlag;
    std::vector<uint8_t>         m_catchSelectedFlag;
    std::vector<uint8_t>         m_gageSelectedFlag;
    std::vector<uint8_t>         m_nodeHiddenFlag;
    std::vector<uint8_t>         m_linkHiddenFlag;
    std::vector<uint8_t>         m_catchHiddenFlag;
    std::vector<uint8_t>         m_gageHiddenFlag;

    // Per-category row → SoA-index buckets. Built in buildGeometryCache,
    // cleared in closeEngine. Used by the virtualised Object Browser tree
    // model to resolve QModelIndex (category, row) → backing name in O(1)
    // without scanning the whole SoA on every data() call.
    QVector<int>                 m_nodesByType[4];   // junction/outfall/storage/divider
    QVector<int>                 m_linksByType[5];   // conduit/pump/orifice/weir/outlet

    // Per-category count of hidden members. Kept in sync by
    // setObjectVisible, setObjectVisibleAt, setCategoryVisible so
    // categoryCheckState() is O(1). Seeded to 0 at cache rebuild.
    int                          m_hiddenCountByCategory[NumCategories] = {};

    // Per-kind (sub-layer) opacity in [0,1], default 1.0. Multiplies each
    // feature's alpha in both paint paths; edited via the layer-tree Opacity
    // column on kind rows. NumCategories entries, brace-init below leaves
    // them 0 — the ctor fills 1.0 (see swmmmodellayer.cpp).
    qreal                        m_categoryOpacity[NumCategories];

    // User-controlled display order for the Object Browser (Slice T.2).
    // Seeded to the enum sequence in rebuildCategoryIndex() so a fresh
    // model starts with the default order.
    QVector<Category>            m_categoryOrder;

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
    struct SoaLocation { SoaKind kind; int soaIdx; };
    QHash<QString, SoaLocation>  m_nameToSoa;

    /*! Refresh `m_*SelectedFlag` and `m_*HiddenFlag` from
     *  `m_selectedNames` + `m_hiddenObjects`. Cost: O(|selection| +
     *  |hidden|), bounded by user actions, not by total object count.
     *  Called on every selection / visibility mutation so the flag
     *  arrays match the canonical QString-keyed state. */
    void rebuildFlagArrays();

    SWMMElementSymbol            m_junctionSym;
    SWMMElementSymbol            m_outfallSym;
    SWMMElementSymbol            m_storageSym;
    SWMMElementSymbol            m_dividerSym;
    SWMMElementSymbol            m_conduitSym;
    SWMMElementSymbol            m_pumpSym;
    SWMMElementSymbol            m_orificeSym;
    SWMMElementSymbol            m_weirSym;
    SWMMElementSymbol            m_outletSym;   // Slice FX.1 — Outlets honor showArrows independently of Conduits.
    SWMMElementSymbol            m_subcatchSym;
    SWMMElementSymbol            m_gageSym;

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
    void buildRuleListLazy() const;
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
    void syncSingleRendererFromStruct(Category c, const SWMMElementSymbol &s);

    // Slice BI Phase 8.13.6.4 (2026-05-24) — per-feature colour-override
    // cache populated when a kind's renderer is Graduated / Categorized /
    // RuleBased. Indexed by Category × SoA index. The painter checks
    // m_kindUsesOverrides[c] to switch between the legacy bucketed fast
    // path and the per-feature override path.
    QVector<QColor>  m_kindFeatureColors[NumCategories];
    QVector<double>  m_kindFeatureSizes[NumCategories];   /*!< Slice BI Phase 8.13.43-α — negative = no override. */
    QVector<double>  m_kindFeatureOffsets[NumCategories]; /*!< Slice Z.5b-paint-graduated — per-feature line offset (px). 0 = no override. */
    QVector<int>     m_kindFeatureShapes[NumCategories];  /*!< M3 — per-feature MarkerShape (int); -1 = no override. */
    bool             m_kindHasAnyOffset[NumCategories]  = {}; /*!< Slice Z.5b-paint-graduated — short-circuit flag. */
    bool             m_kindUsesOverrides[NumCategories] = {};

    QStringList                  m_selectedNames;

    // Engine-table partition cache (curves vs. timeseries). The engine
    // stores both in the same unified table list keyed by type; without
    // this cache, the Object Browser data-category views and pickers
    // re-walked the entire table list on every row count / name lookup —
    // O(N) per call × N rows = O(N²) per refresh.  Filled lazily in
    // ensureTablePartition(); invalidated when a curve or timeseries is
    // added / removed / renamed, and on every modelLoaded().
    mutable QVector<int> m_curveTableIdx;
    mutable QVector<int> m_tsTableIdx;
    mutable bool         m_tablePartitionDirty = true;
    void ensureTablePartition() const;

public:
    /*! Invalidate the cached engine-table partition (curves vs. time
     *  series). External mutators (e.g. CurveRegistry / TimeseriesRegistry
     *  during INP import) call this so the next Object Browser refresh
     *  picks up the new entries. */
    void invalidateDataObjectCache() { m_tablePartitionDirty = true; }

private:

    // GDAL transform (layer CRS → canvas CRS)
    class OGRCoordinateTransformation *m_transform = nullptr;

    // Cached inverse (canvas CRS → layer CRS); built lazily on first
    // transformCanvasToLayer() call and invalidated whenever m_transform is
    // rebuilt or destroyed. Mutable so the public const accessor can populate.
    mutable class OGRCoordinateTransformation *m_inverseTransform = nullptr;

    // Dirty flag — skip scene rebuild when only the view extent changed
    bool                         m_needsRebuild = true;

    // KD-tree spatial index for point-feature queries (nodes + gages).
    // Mutable so const query methods (identifyAt, nodesInRect, …) can
    // trigger a lazy rebuild without breaking const-correctness.
    mutable bool                                 m_kdDirty = true;
    mutable std::unique_ptr<SWMMKdTrees>         m_kdTrees;

    // Batched scene renderer (created in populateScene). Edit paths that
    // mutate SoA coordinates call refreshBoundingRect() on it so the
    // scene's BSP index stays aligned after moves beyond the prior
    // extent. Cleared in depopulateScene.
    class SWMMLayerItem         *m_batchedItem = nullptr;

    // Slice BS Phase 6.9.2 — hydrograph MVC models, lazily constructed on
    // first accessor call. Owned by the layer (parented through the
    // QObject tree, destroyed with it). Shared across all consumer UIs.
    HydrographGroupListModel    *m_uhGroupListModel  = nullptr;
    HydrographRtkTableModel     *m_uhRtkModel        = nullptr;
    HydrographIaTableModel      *m_uhIaModel         = nullptr;
    HydrographDecayTableModel   *m_uhDecayModel      = nullptr;

    // Lazy-loaded data-object registries scoped to the current engine
    // handle. Forward-declared as `QObject*` to keep this header free of
    // the registry includes; the .cpp downcasts to the concrete type.
    // Shared across every UI that mutates these data objects so all
    // views see the same provider instances.
    QObject                     *m_tsRegistry                   = nullptr;
    void                        *m_tsRegistryEngineHandle       = nullptr;
    QObject                     *m_patternRegistry              = nullptr;
    void                        *m_patternRegistryEngineHandle  = nullptr;
    QObject                     *m_curveRegistry                = nullptr;
    void                        *m_curveRegistryEngineHandle    = nullptr;

    // Slice BR Phase 6.8.1 — control-rule registry mirror.
    QObject                     *m_controlRuleRegistry              = nullptr;
    void                        *m_controlRuleRegistryEngineHandle  = nullptr;

    // Slice BQ Phase 6.7.4 — transect registry mirror.
    QObject                     *m_transectRegistry                 = nullptr;
    void                        *m_transectRegistryEngineHandle     = nullptr;

    // Street registry mirror (parametric [STREETS] cross-sections).
    QObject                     *m_streetRegistry                   = nullptr;
    void                        *m_streetRegistryEngineHandle       = nullptr;

    // Pollutant registry mirror ([POLLUTANTS]).
    QObject                     *m_pollutantRegistry                = nullptr;
    void                        *m_pollutantRegistryEngineHandle    = nullptr;

    // Land-use registry mirror ([LANDUSES]).
    QObject                     *m_landUseRegistry                  = nullptr;
    void                        *m_landUseRegistryEngineHandle      = nullptr;

    // Aquifer registry mirror ([AQUIFERS]).
    QObject                     *m_aquiferRegistry                  = nullptr;
    void                        *m_aquiferRegistryEngineHandle      = nullptr;

    // Snowpack registry mirror ([SNOWPACKS]).
    QObject                     *m_snowpackRegistry                 = nullptr;
    void                        *m_snowpackRegistryEngineHandle     = nullptr;

    // Inlet registry mirror ([INLETS]).
    QObject                     *m_inletRegistry                    = nullptr;
    void                        *m_inletRegistryEngineHandle        = nullptr;

    // LID-control registry mirror ([LID_CONTROLS]).
    QObject                     *m_lidControlRegistry               = nullptr;
    void                        *m_lidControlRegistryEngineHandle   = nullptr;

    // User-flags store — see ensureUserFlagsModel().
    openswmmvis::ui::UserFlagsModel *m_userFlagsModel               = nullptr;
    void                        *m_userFlagsModelEngineHandle       = nullptr;
};

Q_DECLARE_METATYPE(SWMMModelLayer *)
Q_DECLARE_METATYPE(SWMMElementSymbol)
Q_DECLARE_OPERATORS_FOR_FLAGS(SWMMModelLayer::QsgKinds)

#endif // SWMMMODELLAYER_H
