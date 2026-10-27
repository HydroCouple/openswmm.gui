/**
 * @file      layertreepanel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Layer-tree dock widget: two-level category/layer model backed by
 *         LayerTreeModel and displayed in a QTreeView with drag-reorder support.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef LAYERTREEPANEL_H
#define LAYERTREEPANEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QVector>
#include <QWidget>

#include <array>
#include <vector>
#include <map>
#include <memory>

#include "ui/panels/layertreetargets.h"

class QTreeView;
class QToolBar;
class QLineEdit;
class QPoint;
class QSortFilterProxyModel;
class MapCanvas;
class OpenSWMMVisLayer;
class OpenSWMMVisWorkspace;
class SWMMResultsLayer;
class SWMM2DResultsLayer;

// Slice S3 — sublayer row type forward decl.
namespace OpenSWMM::Render
{
class ISublayer;
}

/*!
 * \class LayerTreeModel
 * \brief QAbstractItemModel that mirrors the layer stack of a MapCanvas, grouped
 *        by category.
 * \details Two-level tree:
 *
 *          Root
 *          ├── Category (e.g. "SWMM Model")
 *          │   └── Layer
 *          ├── Category (e.g. "Vectors")
 *          │   ├── Layer
 *          │   └── Layer
 *          └── …
 *
 *          Categories are derived from each layer's `OpenSWMMVisLayerType`. A
 *          category is shown only if it contains at least one layer. Within a
 *          category, layers appear in canvas-stack order (top-of-stack first).
 *
 *          Columns: [0] visibility checkbox + layer/category name + icon ·
 *                   [1] opacity (layer rows only).
 *
 *          The model reacts to layerAdded / layerRemoved / layerOrderChanged
 *          signals from the canvas so it stays in sync automatically.
 *
 *          Reordering (Slice LTR-2026-09-19): category rows, layer rows and
 *          sublayer rows are all draggable. A layer can only be dropped
 *          inside its own category (its type decides the category, so a
 *          cross-category drop is refused with the forbidden cursor); a
 *          sublayer only inside its host layer. Every drop changes the
 *          canvas paint order (or the host's sublayer paint order) and is
 *          undoable; the canvas keeps the stack grouped by category so the
 *          tree is always an exact picture of the drawing order.
 */
class LayerTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    /**
     * @param[in] canvas Optional borrowed canvas retained in QPointer, layer stack enumerated
     *   synchronously.
     * @param[in] parent Optional QObject owner; model never owns canvas/layers/sublayers.
     * @brief Construct GUI-thread layer/category/kind/sublayer model and bind source. No per-object
     *   leaves or engine writes; null binding resets empty tree.
     */
    explicit LayerTreeModel(MapCanvas *canvas = nullptr, QObject *parent = nullptr);
    /**
     * @brief Destroy cached row sentinels and QObject connections, not canvas or source layers. All
     *   outstanding indexes become invalid, no source/undo mutation.
     */
    ~LayerTreeModel() override;

    /*!
     * \brief Rebind to a different MapCanvas (called on MDI tab switch).
     *        Pass nullptr to detach (empty tree).
     * @param[in] canvas Optional borrowed canvas, null detaches; equal NONNULL pointer is no-op,
     *   null-to-null still resets.
     * @note Reset, disconnect all old canvas-to-model signals, connect stack/suborder and
     *   destruction events, rebuild groups and per-layer bindings. Canvas destruction invokes null
     *   reset to remove dangling layer rows. Row sentinels valid only until next rebuild/reset; no
     *   ownership transfer, stack reorder or engine write on binding. Old-layer connections are not
     *   globally removed merely by canvas switch; callbacks reject unlisted targets or refresh
     *   current cache.
     */
    void setCanvas(MapCanvas *canvas);
    /*! Bound canvas, or nullptr once it is unbound or destroyed. Out-of-line
     *  so QPointer<MapCanvas>::data() is not instantiated against the
     *  forward declaration.
     * @return Borrowed bound canvas or null after detach/destruction, no ownership transfer or
     *   guarantee cached source indexes remain valid.
     */
    [[nodiscard]] MapCanvas *canvas() const;

    // QAbstractItemModel interface
    /**
     * @param[in] row Zero-based root group, layer sibling, or child row, bounded through hasIndex.
     * @param[in] column Zero/one only; name/visibility and opacity columns.
     * @param[in] parent Invalid root, this-model category or layer index. Current indexes required,
     *   not stale/foreign pointers.
     * @return Tagged internal-pointer index: null group, borrowed layer, cached KindRow/SublayerRow
     *   sentinel; invalid for unsupported/bounds. No object data ownership. Sentinel addresses
     *   remain stable only until reset/rebuild.
     */
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    /**
     * @param[in] child Expected current model index; invalid/group returns invalid.
     * @return Current layer row for kind/sublayer, group for layer, otherwise invalid if host no
     *   longer cached. Does not validate arbitrary foreign raw pointers; caller observes reset
     *   invalidation. No source mutation.
     */
    QModelIndex parent(const QModelIndex &child) const override;
    /**
     * @param[in] parent Invalid root or current model row; foreign/stale indexes unsupported.
     * @return Zero without live canvas; cached nonempty group count, group layers, eleven model
     *   kinds, or cached reversed sublayer count. Child sentinel rows have no descendants. No
     *   per-SWMM-object enumeration or allocation.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] parent Unused, implementation unnamed; hierarchy/canvas not inspected.
     * @return Two even without source or for leaf rows.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /** @brief Report lazy collection children without allocating per-observation rows. */
    bool hasChildren(const QModelIndex &parent = {}) const override;
    /** @brief Whether another bounded batch of series metadata can be exposed. */
    bool canFetchMore(const QModelIndex &parent) const override;
    /** @brief Append at most 128 stable series row sentinels; samples are never read. */
    void fetchMore(const QModelIndex &parent) override;
    /** @return Stable collection series ID for a series child, empty for other rows. */
    QString seriesIdForIndex(const QModelIndex &index) const;
    /**
     * @param[in] index Expected current model index; invalid/group-out-of-range returns invalid.
     *   Layer/raw sentinel lifetime must follow resets.
     * @param[in] role Qt display/edit/check/decoration/font/tooltip/UserRole; supported
     *   combinations vary by row kind.
     * @return Names/icons/counts/visibility and opacity. Layer/kind opacity Display/Edit is rounded
     *   percent string and UserRole raw fraction. Sublayer opacity Display is percent string, Edit
     *   numeric percent, UserRole currently INVALID. Category heading bold/noneditable; model kinds
     *   count all categories including zero members. Sublayer tooltip reports animated/static.
     * @note Live source getters, no unit conversion, ownership transfer or engine write. Sublayer
     *   missing UserRole means current opacity delegate seeds zero there; data contract does not
     *   claim uniform roles across row kinds.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @param[in] index Current non-category name/check or opacity cell; invalid/category
     *   unsupported.
     * @param[in] value Check: only exact Qt::Checked means visible. Opacity: numeric or text after
     *   removing ALL percent signs/trimming; value interpreted as PERCENT even for UserRole.
     * @param[in] role CheckStateRole on column zero; EditRole/UserRole on opacity column one only.
     * @return True after accepted parse/setter invocation, including equality; false unsupported
     *   row/role/parse/context. Setter normalization/success not independently reported.
     * @note Direct presentation setters, no undo command. Divide percent by 100 then
     *   layer/category/sublayer setter enforces its own range. Emit dataChanged in addition to
     *   source notifications. No name edit, engine content write or category visibility bulk edit.
     *   Host raw pointers/current sentinels must remain alive.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    /**
     * @param[in] index Expected current tagged model index, invalid root no flags.
     * @return Categories enabled/selectable/draggable/drop-enabled; layers/kinds/sublayers
     *   enabled/selectable, column-zero checkable where supported, opacity editable. Layer/sublayer
     *   column zero draggable, sublayer rows drop-enabled, kind rows no drag. No
     *   simulation-state/source-liveness validation or root drop flag.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @param[in] section Logical column zero Layer or one Opacity, other invalid.
     * @param[in] orientation Horizontal only.
     * @param[in] role DisplayRole only.
     * @return Translated header, otherwise invalid; no vertical numbering/source access.
     */
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    // Drag-and-drop reordering (within a category only).
    /**
     * @return MoveAction only; no copy/ownership transfer or validation of a particular target.
     */
    Qt::DropActions supportedDropActions() const override;
    /**
     * @param[in] data Required nonnull borrowed MIME object; payload read by QDataStream,
     *   status/version unchecked.
     * @param[in] action Ignored preliminary action; actual drop enforces Move.
     * @param[in] row Ignored preliminary insertion row.
     * @param[in] column Ignored preliminary column.
     * @param[in] parent Current target root/group/host/sublayer; rules depend on format.
     * @return Category format root/group, layer format same cached category, sublayer format same
     *   host row/child; other false. Preliminary check does not require live canvas, valid source
     *   sublayer index or stream status. Format priority category, layer, sublayer differs from
     *   actual category/sublayer/layer decode. No source mutation; host prevents stale/foreign
     *   pointer payload use.
     */
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override;
    /**
     * @param[in] data Required nonnull internal same-process MIME payload; stream
     *   status/origin/version not verified.
     * @param[in] action MoveAction required.
     * @param[in] row Display insertion slot; negative append where applicable, bounded/adjusted
     *   after removing source.
     * @param[in] column Ignored, implementation unnamed.
     * @param[in] parent Current category for layer move; same host/child for sublayer; root/group
     *   slot for category. Must satisfy current-source invariants even though function does not
     *   call preliminary checker.
     * @return True after accepted reorder request, false invalid context/category/slot/no-op or
     *   canvas sublayer failure.
     * @note Category moves use reorderCategory, layer move same group via canvas moveLayer,
     *   sublayer converts reversed display slot to paint index then canvas moveSublayer. Undo
     *   follows canvas command support; no engine content mutation. Layer pointer decoded only
     *   matched in cache, sublayer host is unchecked cast after pointer match. All view indexes can
     *   reset during synchronous canvas notifications.
     */
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;
    /**
     * @param[in] indexes Only first index inspected. Current eligible category/layer/sublayer
     *   indexes required; kind rows are not drag-enabled and must not be supplied directly.
     * @return New parentless QMimeData caller/view owns or null for empty/unresolved target.
     *   Category stores int32 CategoryId; layer stores raw quintptr; sublayer stores host pointer
     *   plus bottom-up paint index. No source/project identity, pointer pin or stable sublayer ID;
     *   remaining selection ignored.
     */
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    /**
     * @return Internal layer-row, category and sublayer-row MIME format names. Raw layer pointer
     *   payloads are valid only in this process/current source lifetime; no cross-project
     *   serialization contract.
     */
    QStringList mimeTypes() const override;

    /*!
     * \brief Returns the OpenSWMMVisLayer for a layer-row index, or nullptr for
     *        category rows / invalid indices.
     * @param[in] index Current model index; invalid/group gives null.
     * @return Borrowed layer row pointer or host for kind/sublayer; no ownership transfer or extra
     *   liveness/foreign-index validation.
     */
    [[nodiscard]] OpenSWMMVisLayer *layerForIndex(const QModelIndex &index) const;

    /*!
     * \brief True if \p index is a category header row (no associated layer).
     * @param[in] index Expected current model index, invalid is supported; reset invalidates prior
     *   sentinel pointers.
     * @return True for valid index with null internal pointer, no row-range/source check.
     *   Read-only, no selection/source mutation.
     */
    [[nodiscard]] bool isCategoryIndex(const QModelIndex &index) const;

    // ---- Slice BI-MK.LT: 3-level tree for multi-kind layers ----------------
    //
    // Under each `SWMMModelLayer` row the model exposes 11 sub-rows, one per
    // Category enum value (Junctions, Outfalls, ..., RainGages). The sub-row
    // surfaces a per-kind visibility checkbox and feeds the right-click
    // context menu's Style submenu.

    /*! True if \p index is a kind-row (3rd-level sub-row under a SWMMModelLayer).
     * @param[in] index Expected current model index, invalid is supported; reset invalidates prior
     *   sentinel pointers.
     * @return True for valid nonnull pointer in current kind sentinel set; no foreign-model check.
     *   Read-only, no selection/source mutation.
     */
    [[nodiscard]] bool isKindIndex(const QModelIndex &index) const;

    /*! Parent layer for a kind-row index; nullptr for any other row type.
     * @param[in] index Expected current model index, invalid is supported; reset invalidates prior
     *   sentinel pointers.
     * @return Borrowed host for recognized kind sentinel, null otherwise. Read-only, no
     *   selection/source mutation.
     */
    [[nodiscard]] OpenSWMMVisLayer *kindParentLayer(const QModelIndex &index) const;

    /*! Kind ordinal (0..10, matches SWMMModelLayer::Category) for a kind-row
     *  index; -1 for any other row type.
     * @param[in] index Expected current model index, invalid is supported; reset invalidates prior
     *   sentinel pointers.
     * @return Stored model category enum ordinal 0..10 for kind, -1 otherwise; fixed enum order,
     *   not paint-order index. Read-only, no selection/source mutation.
     */
    [[nodiscard]] int kindOrdinal(const QModelIndex &index) const;

    // ---- Slice S3 (RENDERING_OUTPUT_SUBLAYERS_PLAN.md §4.1) -----------------
    // 3rd-level sub-rows under any ISublayerHost layer that is NOT already
    // multi-kind. SWMM2DResultsLayer adopts this — SWMMResultsLayer keeps
    // its existing OUT.3 kind-row UX (the eligibility rule below avoids
    // dual sub-row schemes on the same layer; the kind-vs-sublayer
    // interaction on multi-kind hosts is a follow-up UX iteration).

    /*! True if \p index is a sublayer-row (3rd-level sub-row under an ISublayerHost).
     * @param[in] index Expected current model index, invalid is supported; reset invalidates prior
     *   sentinel pointers.
     * @return True for valid nonnull pointer in current sublayer sentinel set; no foreign-model
     *   check. Read-only, no selection/source mutation.
     */
    [[nodiscard]] bool isSublayerIndex(const QModelIndex &index) const;

    /*! Parent layer for a sublayer-row index; nullptr for any other row type.
     * @param[in] index Expected current model index, invalid is supported; reset invalidates prior
     *   sentinel pointers.
     * @return Borrowed host for recognized sublayer sentinel, null otherwise. Read-only, no
     *   selection/source mutation.
     */
    [[nodiscard]] OpenSWMMVisLayer *sublayerParentLayer(const QModelIndex &index) const;

    /*! The ISublayer pointer for a sublayer-row index; nullptr otherwise.
     * @param[in] index Expected current model index, invalid is supported; reset invalidates prior
     *   sentinel pointers.
     * @return Borrowed sublayer for recognized sentinel, null otherwise; no ownership pin.
     *   Read-only, no selection/source mutation.
     */
    [[nodiscard]] OpenSWMM::Render::ISublayer *sublayerForIndex(const QModelIndex &index) const;

    /*!
     * \brief Slice LTR-2026-09-19 — moves the category group \p categoryId
     *        (an openswmmvis::ui::CategoryId currently shown in the tree) to
     *        tree row \p dstTreeRow, then batch-reorders the canvas layer
     *        stack so paint order follows (an undoable ReorderLayersCommand)
     *        and records the new group order on the canvas
     *        (MapCanvas::setLayerGroupOrder, persisted per project).
     *
     *        Tree row 0 = top of tree = highest canvas z-order. Rows are
     *        positions among the NON-EMPTY categories the tree displays;
     *        \p dstTreeRow is the row the group occupies after the move.
     *        Returns false (no-op) when the id is not shown, the row is out
     *        of range, or the move is a no-op.
     * @param[in] categoryId Currently displayed nonempty CategoryId; absent rejected.
     * @param[in] dstTreeRow Final zero-based position among displayed groups, not insertion slot;
     *   outside range/equality rejected.
     * @return True after grouped canvas permutation and stored preference update attempted, false
     *   missing canvas/invalid/no-op/incomplete permutation.
     * @note Preserve within-group bottom-up canvas order, move group preference around visible
     *   neighbors while absent groups retain relative slots. Preference set BEFORE canvas undoable
     *   reorder and not itself snapshotted here, so undo paint-order replay need not revert
     *   preference. No engine write; canvas signal reset re-derives actual group order.
     */
    bool reorderCategory(int categoryId, int dstTreeRow);

    /*! CategoryId of a category row; -1 for any other row / invalid index.
     * @param[in] index Expected current model index, invalid is supported; reset invalidates prior
     *   sentinel pointers.
     * @return CategoryId of valid cached group row, -1 for other rows/outside group range.
     *   Read-only, no selection/source mutation.
     */
    [[nodiscard]] int categoryIdForIndex(const QModelIndex &index) const;

    // Index lookup for re-selecting a row after a model reset (every
    // reorder resets, which drops the view's selection).
    /**
     * @param[in] categoryId Exact visible CategoryId.
     * @return Column-zero current group index or invalid; no source mutation, persistent-index
     *   lifetime or hidden-group creation.
     */
    [[nodiscard]] QModelIndex indexForCategory(int categoryId) const;
    /**
     * @param[in] layer Borrowed pointer identity looked up in current cache, null/unlisted invalid.
     * @return Column-zero current layer row or invalid. Pointer not owned/dereferenced to verify
     *   lifetime; caller respects source/reset lifetime.
     */
    [[nodiscard]] QModelIndex indexForLayer(OpenSWMMVisLayer *layer) const;
    /**
     * @param[in] layer Borrowed current host identity.
     * @param[in] sublayer Borrowed sublayer identity, compared by address without dereference.
     * @return Matching column-zero current sentinel index or invalid; no creation, ownership pin or
     *   source reorder.
     */
    [[nodiscard]] QModelIndex indexForSublayer(OpenSWMMVisLayer *layer,
                                               OpenSWMM::Render::ISublayer *sublayer) const;

    /*!
     * \brief Slice GUI-2026-05-30 §2 / §3 — notify the model that the
     *        sublayer or kind paint-order on a host layer has changed
     *        externally (context-menu Move Up / Down or drop).  Rebuilds
     *        the cached sub-row storage and emits a model reset so the
     *        view re-renders in the new order.
     * @note Full model reset rebuilds kind and sublayer sentinel storage, leaving categories
     *   unchanged. Kinds stay fixed enum order; sublayers reverse host paint order. Invalidates all
     *   indexes; no layer/paint-order write or undo command itself. Requires current host/layer
     *   pointers alive.
     */
    void notifyHostSubOrderChanged();

signals:
    /** @brief Forward qualified native collection selection to the visible tree.
     * @param[in] layer Borrowed attached collection.
     * @param[in] ids Stable series IDs; no source samples are read. */
    void collectionSelectionChanged(class TimeSeriesLayer *layer, const QStringList &ids);

private slots:
    /**
     * @param[in] layer Unused borrowed notification payload, implementation unnamed.
     * @brief Reset model and rebuild category/kind/sublayer caches.
     * @note Read CURRENT canvas stack after layer addition; argument not otherwise used. No
     *   canvas/engine write, but all old indexes/sentinels invalidated and per-layer signals
     *   rebound.
     */
    void onLayerAdded(OpenSWMMVisLayer *layer);
    /**
     * @param[in] layer Unused borrowed notification payload, implementation unnamed.
     * @brief Reset model and rebuild category/kind/sublayer caches.
     * @note Read CURRENT canvas stack after removal; argument not dereferenced or deleted. No
     *   canvas/engine write, but all old indexes/sentinels invalidated and per-layer signals
     *   rebound.
     */
    void onLayerRemoved(OpenSWMMVisLayer *layer);
    /**
     * @brief Reset model and rebuild category/kind/sublayer caches.
     * @note Re-derive groups/layer rows from CURRENT canvas stack after reorder/undo/redo. No
     *   canvas/engine write, but all old indexes/sentinels invalidated and per-layer signals
     *   rebound.
     */
    void onLayerOrderChanged();
    /**
     * @param[in] layer Optional borrowed layer; null/unlisted/missing row no-op.
     * @brief Emit all-role dataChanged across the current layer row, both columns, without
     *   resetting children.
     * @note No source write/undo, name/visibility/opacity are read live by view.
     */
    void onLayerDataChanged(OpenSWMMVisLayer *layer);
    /**
     * @param[in] host Ignored notification host, implementation unnamed; all hosts rebuilt.
     * @brief Forward to notifyHostSubOrderChanged, invalidating indexes for reorder and undo/redo.
     *   No independent source mutation.
     */
    void onSublayerOrderChanged(OpenSWMMVisLayer *host);

private:
    struct Category {
        int id = -1; // openswmmvis::ui::CategoryId
        QString name;
        QString iconAlias;                  // Qt resource alias under :/swmmvis/
        QVector<OpenSWMMVisLayer *> layers; // canvas-stack order, top first
    };

    // Slice BI-MK.LT — one KindRow per (multi-kind layer, kind ordinal).
    // Allocated in m_kindRowStorage, then std::array makes the addresses
    // stable between model rebuilds so they're safe to use as QModelIndex
    // internalPointers. Hash is keyed on the parent layer pointer; entries
    // exist only for layers that support multi-kind styling (today: only
    // SWMMModelLayer with 11 kinds).
    struct KindRow {
        OpenSWMMVisLayer *layer = nullptr;
        int kindOrdinal = -1;
    };
    static constexpr int kKindsPerSwmmModelLayer = 11;

    // Slice S3 — one SublayerRow per (host layer, sublayer pointer).
    // Storage is a std::vector reserved to the final size at rebuild time
    // and never modified afterwards, so element addresses stay stable for
    // the model's lifetime — same stability contract as the KindRow
    // std::array storage.
    /** @brief Immutable child identity; unique allocation survives vector growth. */
    struct SeriesRow { OpenSWMMVisLayer *layer = nullptr; QString id; };
    std::map<OpenSWMMVisLayer *, std::vector<std::unique_ptr<SeriesRow>>> m_seriesRows;
    QSet<const void *> m_seriesRowPtrs;

    struct SublayerRow {
        OpenSWMMVisLayer *layer = nullptr;
        OpenSWMM::Render::ISublayer *sublayer = nullptr;
    };

    /**
     * @note Clear group/layer cache, scan canvas highest stack index first, bucket type and
     *   preserve first-appearance group order. Rebind listed layer name/opacity/visibility/children
     *   notifications, then rebuild kind/sublayer rows. Null canvas returns before clearing child
     *   sentinel storage; public reset/zero rowCount makes old indexes unusable. Does not detach
     *   all old unlisted-layer connections, bracket reset or change source order. Caller supplies
     *   lifetime/reset context.
     */
    void rebuildCategories();
    /**
     * @note Clear/reallocate kind sentinels for each current SWMMModelLayer, exactly eleven in enum
     *   order even empty categories, never results layers. Rewire renderer/geometry refresh
     *   callbacks. Addresses stable only between rebuilds, no reset notification itself, layer
     *   mutation or kind paint-order application.
     */
    void rebuildKindRows();
    /**
     * @note Clear/reallocate sublayer sentinels for current ISublayerHost layers except kind-host
     *   models, reverse host bottom-up order, omit null children. Reserve final vector size so
     *   pointer addresses stable until next rebuild. Rebind child invalidated to all-host-child-row
     *   dataChanged, no own source/order write or reset bracket. Host/sublayer raw pointers must
     *   remain valid through cache lifetime.
     */
    void rebuildSublayerRows();
    /**
     * @param[in] layer Pointer identity queried in current layer-group hash, not dereferenced.
     * @return Cached group TREE ROW, not CategoryId, or -1 absent/null. No source liveness
     *   check/mutation.
     */
    int categoryOf(OpenSWMMVisLayer *layer) const;
    /**
     * @param[in] sublayerRowPtr Optional pointer to actual current SublayerRow; nonnull
     *   arbitrary/stale pointer unsupported because cast is dereferenced.
     * @return Address position within current host vector or -1 missing/null; linear scan, no
     *   pointer ownership or source mutation.
     */
    int sublayerRowIndex(const void *sublayerRowPtr) const;

    // QPointer, not a raw pointer: the canvas dies with its project window
    // while this dock outlives it, and setCanvas() disconnects from the OLD
    // canvas — on a raw pointer that was a use-after-free (SIGSEGV in
    // QObject::disconnect from onActiveSubWindowChanged).
    QPointer<MapCanvas> m_canvas;
    QVector<Category> m_categories;
    QHash<OpenSWMMVisLayer *, int> m_layerToCategory;
    QHash<OpenSWMMVisLayer *, std::array<KindRow, kKindsPerSwmmModelLayer>> m_kindRowStorage;
    QSet<const void *> m_kindRowPtrSet; // O(1) kind-row discriminator
    // Slice S3 — sublayer-row storage. The std::vector inside each hash
    // node is sized once at rebuild time and never grown, so element
    // addresses (used as QModelIndex internalPointers) are stable for the
    // model's lifetime. m_sublayerRowPtrSet provides O(1) discrimination
    // between sublayer-row, kind-row, layer, and category indices.
    QHash<OpenSWMMVisLayer *, std::vector<SublayerRow>> m_sublayerRowStorage;
    QSet<const void *> m_sublayerRowPtrSet;

    // Slice LTR-2026-09-19: no cached category order here. The displayed
    // category sequence is DERIVED from the canvas stack (first appearance,
    // top first) so the tree can never disagree with paint order — including
    // after undo/redo. The per-project preference that places a group whose
    // layers are all gone lives on the canvas (MapCanvas::layerGroupOrder).
};

// ---------------------------------------------------------------------------

/*!
 * \class LayerTreePanel
 * \brief Embeddable widget containing the layer tree.
 * \details The panel shows the LayerTreeModel in a QTreeView. Layer-adding
 *          actions live on the main application toolbar (`actionAddVectorData`,
 *          `actionAddRasterData`, `actionAddWMSData`, `actionAddBasemap`,
 *          `actionAddSWMMOutput`). Per-layer operations (Zoom To, Remove,
 *          Move Up/Down, Show/Hide, Properties) live on the right-click
 *          context menu over a tree row.
 *
 *          The canvas binding is mutable — call setCanvas() on MDI tab switch
 *          to re-point the tree at the active project's canvas.
 */
class LayerTreePanel : public QWidget
{
    Q_OBJECT

public:
    /**
     * @param[in] canvas Optional borrowed source guarded by QPointer, forwarded to owned model.
     * @param[in] parent Optional QWidget owner; panel owns view/proxy/model/search/delegate, never
     *   source objects.
     * @brief Construct GUI-thread categorized layer browser with internal reorder and inline
     *   opacity editor, initially expanded; no layer creation or engine edit.
     */
    explicit LayerTreePanel(MapCanvas *canvas = nullptr, QWidget *parent = nullptr);
    /**
     * @brief Destroy owned controls/proxy/model and QObject connections, leaving
     *   project/canvas/layers untouched. No registry flush or active-results change.
     */
    ~LayerTreePanel() override;

    /*!
     * \brief Returns the currently selected layer, or nullptr.
     * @return Borrowed layer for CURRENT view index, host for kind/sublayer, null category/invalid.
     *   Uses current index rather than all selected rows; no ownership pin, selection mutation or
     *   engine query.
     */
    [[nodiscard]] OpenSWMMVisLayer *selectedLayer() const;

    /*!
     * \brief Rebind the panel and its model to a different canvas.
     * @param[in] canvas Optional borrowed source, equal nonnull pointer no-op; null always
     *   forwarded to model for teardown reset.
     * @note Store QPointer and rebind model; leave search/filter and cached active-results pointers
     *   unchanged. No source ownership transfer, layer addition/removal or result activation. Model
     *   reset remembers/restores only identities still resolvable.
     */
    void setCanvas(MapCanvas *canvas);

    /**
     * @return Borrowed panel-owned source model, valid until panel destruction; caller must map
     *   proxy indexes before using it. No ownership transfer.
     */
    [[nodiscard]] LayerTreeModel *model() const { return m_model; }

signals:

    /*!
     * \brief Emitted when the user selects a layer in the tree.
     * @param[in] layer Borrowed current layer/host, null for group/empty selection.
     * @note Emitted on view selection change, including resets/restores; does not activate canvas
     *   layer itself.
     */
    void layerSelected(OpenSWMMVisLayer *layer);

    /*!
     * \brief Emitted when the user selects a SWMM kind sub-row (e.g.
     *        "Storage"). \p kindOrdinal matches the SWMMModelLayer::Category
     *        enum value. Fires in addition to layerSelected (which collapses
     *        kind rows to their parent layer). SWMMVis listens and focuses
     *        the matching category in the Object Browser.
     * @param[in] layer Borrowed recognized model host.
     * @param[in] kindOrdinal Stored SWMMModelLayer Category ordinal 0..10.
     * @note Additional signal after layerSelected for kind row; receiver chooses object-browser
     *   focus.
     */
    void kindSelected(OpenSWMMVisLayer *layer, int kindOrdinal);

    /*!
     * \brief Emitted when the user requests to view properties for a layer.
     *        \p routingId is empty for plain "Properties…"; the sentinel
     *        "symbology" (from "Styles ▸ Edit Symbology…") asks the dialog
     *        to open on the Symbology tab.
     * @param[in] layer Borrowed layer target, no modal-lifetime pin.
     * @param[in] routingId Empty default properties, symbology sentinel selects style tab; copied
     *   into signal.
     * @note Request only, host opens UI. Sublayer/kind properties currently create their own
     *   LayerStyleDialog instead.
     */
    void layerPropertiesRequested(OpenSWMMVisLayer *layer, const QString &routingId = QString());

    /*!
     * \brief Emitted when the user picks "Open Attribute Table" on a layer,
     *        object-type or mesh-sublayer row. SWMMVis raises the Attribute
     *        Table dock and switches it to \p target
     *        (FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §7).
     * @param[in] target Value descriptor from current row type, containing borrowed layer and
     *   optional category/mesh family.
     * @note Request only; host raises/binds table. Menu enables by target isValid, not source
     *   lifetime guarantee.
     */
    void attributeTableRequested(const openswmmvis::ui::AttributeTableTarget &target);

    /*!
     * \brief Emitted when the user picks an Export entry on a row. SWMMVis
     *        opens the export dialog for \p target and runs the write
     *        (FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §8).
     * @param[in] target Current row export descriptor, borrowed layer plus geometry/result/category
     *   options.
     * @note Request only, no file dialog/write by emission; host validates target
     *   lifetime/destination.
     */
    void exportRequested(const openswmmvis::ui::LayerExportTarget &target);

    /*!
     * \brief Emitted when the user picks "Set Style…" on a layer's context
     *        menu. SWMMVis listens and opens the SymbologyDialog — same
     *        path as the animation-toolbar's Set Style action.
     * @param[in] layer Borrowed style target.
     * @note Declared legacy style request; current menus use layerPropertiesRequested or construct
     *   scoped style dialog, so this implementation does not emit it.
     */
    void layerStyleRequested(OpenSWMMVisLayer *layer);

    /*!
     * \brief Slice BI-MK.LT — emitted when the user picks a Style option
     *        from a kind sub-row's right-click menu. \p kindOrdinal matches
     *        the SWMMModelLayer::Category enum value. \p rendererId is one
     *        of "single", "graduated", "categorized", "rule" (or empty
     *        meaning "open dialog without changing class"). SWMMVis listens
     *        and (a) swaps the kind's IFeatureRenderer to the matching
     *        class with sensible defaults, then (b) opens SymbologyDialog
     *        pre-scoped to that kind + tab when BI-MK.1 ships.
     * @param[in] layer Borrowed kind host.
     * @param[in] kindOrdinal Category ordinal.
     * @param[in] rendererId Requested renderer-class token single/graduated/categorized/rule or
     *   empty.
     * @note Legacy signal remains declared, but current menu renderer actions are null placeholders
     *   so normal UI does not emit it. Styling lives in Properties dialog.
     */
    void layerKindStyleRequested(OpenSWMMVisLayer *layer, int kindOrdinal,
                                 const QString &rendererId);

    /*!
     * \brief Slice PT.1 — emitted when the user picks "Plot timeseries…"
     *        on a kind sub-row's right-click menu and selects an object
     *        name. SWMMVis listens and routes to its existing AT.2 picker
     *        (openTimeSeriesPlotFor) which pops the variable picker and
     *        opens the Comparison Plot Dialog.
     * @param[in] kindOrdinal SWMMModelLayer category ordinal for chosen object.
     * @param[in] objectName Exact action text object name.
     * @param[in] resultsLayer Optional borrowed explicit output source for results rows; null model
     *   row leaves receiver source choice.
     * @note Request only; kind menu scans first 50 objects, output sublayer scans until 50 names
     *   present in output. Host owns variable picker/chart and preserves results lifetime.
     */
    void plotKindObjectRequested(int kindOrdinal, const QString &objectName,
                                 SWMMResultsLayer *resultsLayer = nullptr);

    /*!
     * \brief Emitted when the user right-clicks a SWMM Output (.out) layer
     *        and picks "Plot Time Series…". The receiver pops an object
     *        picker (type + id read from the .out) and then the variable
     *        picker, plotting against \p layer specifically. Lets the user
     *        anchor the plot to the data layer rather than the model.
     * @param[in] layer Borrowed chosen 1D output source, not lifetime-pinned.
     * @note Request object/variable picker for this source; no plot creation/active-source change
     *   itself.
     */
    void plotTimeSeriesFromOutputLayerRequested(class SWMMResultsLayer *layer);

    /*!
     * \brief Emitted when the user picks "Set as Active Results Layer" on a
     *        results layer's context menu. SWMMVis routes this to the project
     *        window's setActiveResultsLayer / setActive2DResultsLayer so the
     *        chosen layer becomes the target for all analysis tools.
     * @param[in] layer Borrowed chosen 1D output source.
     * @note Request host to activate analysis source; emission alone does not update cached check
     *   mark or canvas.
     */
    void setActiveResultsLayerRequested(class SWMMResultsLayer *layer);
    /**
     * @param[in] layer Borrowed chosen 2D results source.
     * @note Request host to activate 2D analysis source, no direct active-state mutation by signal.
     */
    void setActive2DResultsLayerRequested(class SWMM2DResultsLayer *layer);

public slots:
    /*!
     * \brief Cache the project window's active 1D / 2D results layers so the
     *        context-menu "Set as Active Results Layer" entry can render a
     *        check-mark on the active one. Wired from the project window's
     *        activeResultsLayerChanged / active2DResultsLayerChanged signals.
     * @param[in] layer Optional borrowed 1D results layer stored in QPointer, null clears cached
     *   marker.
     * @note Cache only for NEXT context-menu check state. Does not activate source, bind model,
     *   emit request, refresh open menu or edit engine/undo.
     */
    void setActiveResultsLayer(SWMMResultsLayer *layer);
    /**
     * @param[in] layer Optional borrowed 2D results layer stored in QPointer, null clears cached
     *   marker.
     * @note Cache only for NEXT context-menu check state. Does not activate source, bind model,
     *   emit request, refresh open menu or edit engine/undo.
     */
    void setActive2DResultsLayer(SWMM2DResultsLayer *layer);

private slots:
    /**
     * @note Missing canvas/target no-op. Sublayer selection only removes TraceSublayer via
     *   removeAnalysisSublayer, never its host; other sublayers no-op. Layer/kind selection finds
     *   host canvas index, calls takeLayer with default undo enabled then deleteLater on returned
     *   layer. Deferred deletion means layer lifetime is not preserved solely by removal history;
     *   caller cannot assume removal undo can restore it. No confirmation or engine-table deletion
     *   here.
     */
    void onRemoveSelectedLayer();
    /**
     * @brief Forward selected layer/child-host to zoomToLayer, category/empty target no-op. No
     *   property dialog or selection write.
     */
    void onZoomToSelectedLayer();
    /**
     * @brief Request selected layer sibling row -1 through moveSelectedLayerToRow.
     * @note Null/category no-op. If invoked directly with child current index, its child row number
     *   supplies requested row while host is moved; normal menus route child moves separately.
     *   Canvas command handles undo; no engine content write.
     */
    void onMoveLayerUp();
    /**
     * @brief Request selected layer sibling row +1 through moveSelectedLayerToRow.
     * @note Null/category no-op. If invoked directly with child current index, its child row number
     *   supplies requested row while host is moved; normal menus route child moves separately.
     *   Canvas command handles undo; no engine content write.
     */
    void onMoveLayerDown();
    /**
     * @brief Request current category tree row -1 through bounded category mover; non-category
     *   no-op.
     * @note Empty/edge moves no-op via helper; full grouped canvas order/persistent preference
     *   follows reorderCategory, no engine write.
     */
    void onMoveCategoryUp();
    /**
     * @brief Request current category tree row +1 through bounded category mover; non-category
     *   no-op.
     * @note Empty/edge moves no-op via helper; full grouped canvas order/persistent preference
     *   follows reorderCategory, no engine write.
     */
    void onMoveCategoryDown();
    /**
     * @brief Emit layerSelected for current layer/host/null, then kindSelected if current row is
     *   model kind.
     * @note No feature-selection bus write, canvas activation, engine edit or duplicate-signal
     *   suppression; resets/restore can notify.
     */
    void onSelectionChanged();
    /**
     * @param[in] index Current proxy index; opacity column ignored.
     * @note Column-zero Trace child opens trace properties. Other resolvable layer/host triggers
     *   zoom of CURRENT selected layer, which may differ from supplied index if called directly.
     *   Category does nothing, no editor/undo command opened for ordinary layer.
     */
    void onLayerDoubleClicked(const QModelIndex &index);
    /**
     * @param[in] pos Tree viewport-local position; invalid row no-op, valid row made current BEFORE
     *   menu.
     * @note Category reorders; kind/sublayer offers scoped properties/table/export/visibility and
     *   bounded plots; trace child has guarded properties/details/recalculate/remove. Ordinary
     *   layer adds navigation/export/table/order/remove/style/results activation and flow
     *   animation; several future actions disabled. Ordering through canvas undo APIs,
     *   visibility/style direct setters. Most menu snapshots use borrowed raw layer/sublayer
     *   pointers without post-menu lifetime revalidation; host preserves source through modal
     *   reentry. Trace uses QPointer guard. Layer removal schedules deletion, analysis-child
     *   removal is direct and not undoable. No universal engine-running gate or generic file write
     *   here.
     */
    void onContextMenuRequested(const QPoint &pos);
    /**
     * @param[in] text Raw literal case-insensitive substring, whitespace not trimmed; empty
     *   controls drag enablement.
     * @brief Set immediate recursive fixed-string filter, disable drag AND drops for nonempty text
     *   and expand all accepted rows.
     * @note Context-menu moves still operate unfiltered source-row positions, not proxy positions.
     *   No debounce, bus/engine edit or saved-filter persistence. Missing proxy no-op.
     */
    void onSearchTextChanged(const QString &text);

private:
    /**
     * @note Constructor-only owned UI/model/proxy wiring, not idempotent. Single selection,
     *   recursive filter, name/opacity columns, inline percent delegate. Model/proxy reset/layout
     *   changes expand all, remember/restore category/layer/sublayer identity around resets; kind
     *   identity not retained. No main add-layer toolbar or source/engine creation.
     */
    void setupUi();
    /**
     * @param[in] layer Optional borrowed target, current canvas required; must outlive deferred GIS
     *   open callback.
     * @note Use canvas-projected layer extent: valid fits exactly; finite ordered nonnull
     *   degenerate extent padded 5% largest span each side, or 25 canvas units for point.
     *   Null/invalid GIS vector extent connects SingleShot openFinished retry on success, repeated
     *   calls can add retries. Callback uses CURRENT panel canvas, not original canvas, so host
     *   must coordinate project switches. No selection/layer edit; canvas setExtent manages
     *   navigation history.
     */
    void zoomToLayer(OpenSWMMVisLayer *layer);
    /**
     * @param[in] proxyIdx Expected current panel proxy index, invalid returns invalid; if no proxy
     *   supplied index returned unchanged.
     * @return Borrowed-coordinate source index, invalidated by reset, no source/selection mutation.
     */
    QModelIndex toSourceIndex(const QModelIndex &proxyIdx) const;

    // Slice LTR-2026-09-19 — reorder helpers. Layer moves are expressed in
    // tree rows within the layer's category (the canvas keeps the stack
    // grouped, so the sibling's canvas index is the exact target).
    /**
     * @param[in] targetRow Final source-model sibling row among host category, clamped to
     *   0..rows-1, not filtered proxy row.
     * @note Resolve current layer/child-host, find category sibling canvas index and call canvas
     *   moveLayer. Equality/missing canvas/host/sibling/index no-op. No cross-category move, own
     *   command/save or selection restoration beyond reset hooks.
     */
    void moveSelectedLayerToRow(int targetRow);
    /**
     * @param[in] targetRow Final source-tree category row, clamped among nonempty groups.
     * @note Require current category/canvas, then reorderCategory; helper ignores return status.
     *   Does not move merely selected child host group or filtered proxy row. Canvas
     *   undo/preference semantics apply.
     */
    void moveSelectedCategoryToRow(int targetRow);
    // Every reorder resets the model (and expandAll() follows), which drops
    // the selection. Remember what was current before the reset and put the
    // selection back on the same category / layer / sublayer afterwards.
    /**
     * @note Reset saved identity, then store category ID, guarded layer, or guarded host plus RAW
     *   sublayer address. Kind rows intentionally not retained. Called before model reset, compares
     *   later child identity without dereference; no ownership transfer, source/selection write or
     *   persistent QModelIndex storage.
     */
    void rememberCurrentRow();
    /**
     * @note Resolve saved identity in rebuilt source, clear saved record, map through current
     *   filter, set current/scroll when visible. Missing/destroyed/filtered identity no-op. May
     *   emit layerSelected via selection change, no explicit signal guard or kind restoration; no
     *   source reorder.
     */
    void restoreRememberedRow();

    struct RememberedRow {
        int categoryId = -1;
        QPointer<OpenSWMMVisLayer> layer;
        OpenSWMM::Render::ISublayer *sublayer = nullptr; // compared by address only
    };
    RememberedRow m_remembered;

    QPointer<MapCanvas> m_canvas; // see LayerTreeModel::m_canvas
    QTreeView *m_treeView = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    LayerTreeModel *m_model = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;

    // Cached active analysis layers (for the context-menu check-state only).
    QPointer<SWMMResultsLayer> m_activeResults1D;
    QPointer<SWMM2DResultsLayer> m_activeResults2D;
};

#endif // LAYERTREEPANEL_H
