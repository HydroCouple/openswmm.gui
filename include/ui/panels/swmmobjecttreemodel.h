/**
 * @file      swmmobjecttreemodel.h
 * @brief     Virtualised tree model backing the Object Browser dock.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Virtualised tree model backing the Object Browser dock. Replaces the
 * former QTreeWidget-per-object approach so the browser scales to the
 * millions-of-objects target set by Slice R (batched map renderer).
 *
 * Hierarchy
 *   root
 *     └ Category row (Junctions / Outfalls / … / Rain Gages)
 *         └ Leaf row (synthesised from SWMMModelLayer SoA on demand)
 *
 * Category lists and an optional non-spatial section divider are cached; leaf rows exist purely as
 * QModelIndex coordinates that index into the layer's per-category
 * buckets. `data()` fetches names through the O(1) layer API, so 1M
 * leaves costs nothing at rest — only visible rows trigger data() calls.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMOBJECTTREEMODEL_H
#define SWMMOBJECTTREEMODEL_H

#include <QAbstractItemModel>
#include <QPointer>
#include <QVector>

#include "layers/swmmmodellayer.h"
#include "selection/selectionmanager.h"

class SWMMObjectTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    // Extra data-role id's used by the panel when translating model
    // indices into SWMMObjectRef / (Category, row) tuples. Kept outside
    // Qt::UserRole so nothing collides with the delegate's own roles.
    enum Role {
        RoleCategory = Qt::UserRole + 10,  // int(SWMMModelLayer::Category)
        RoleRow = Qt::UserRole + 11,       // row within category
        RoleIsLeaf = Qt::UserRole + 12,    // bool
        RoleObjectRef = Qt::UserRole + 13, // QVariant::fromValue<SWMMObjectRef>
        // Slice BM.0 — non-spatial data-objects section.
        RoleSection = Qt::UserRole + 14,      // int(Section)
        RoleDataCategory = Qt::UserRole + 15, // int(SWMMModelLayer::DataCategory)
    };

    /*! Slice BM.0 — top-level partitioning of the tree. */
    enum Section : int {
        SectionNetwork = 0, ///< Spatial categories (Junctions … Rain Gages)
        SectionDivider = 1, ///< Visual separator row (non-selectable)
        SectionData = 2,    ///< Non-spatial data objects (Curves … Inlets)
    };

    /**
     * @param[in] parent Optional QObject owner; source layer is borrowed, never owned.
     * @brief Construct empty single-column virtual tree with no source and no pending reload.
     *   GUI-thread model/layer access only. No leaf objects allocated.
     */
    explicit SWMMObjectTreeModel(QObject *parent = nullptr);
    /**
     * @brief Destroy cached category vectors and QObject connections; context-bound queued timer is
     *   canceled, source layer remains untouched. No pending visibility/order changes flushed or
     *   undone.
     */
    ~SWMMObjectTreeModel() override;

    /*! Bind the model to a SWMM layer. Triggers beginResetModel /
     *  endResetModel and rebuilds the cached visible-category list. Pass
     *  nullptr to detach.
     * @param[in] layer Optional borrowed source retained as QPointer; null detaches, equal pointer
     *   no-op.
     * @note Disconnect old model/category/data notifications, replace source, reset visible
     *   category vectors and connect new notifications. Network model/category/hydrograph changes
     *   reset synchronously; data-object/transect/control changes coalesce through scheduleReload.
     *   Does not change layer visibility/order, engine state, selection or own source. Layer
     *   destruction guards queries but does not itself issue a model reset. Pending old-source
     *   timer is not canceled and later resets current source.
     */
    void setLayer(SWMMModelLayer *layer);

    /*! Which SWMMModelLayer::Category is rendered at top-level row
     *  \p topRow, or `NumCategories` if the row is out of range or the
     *  row is a data / divider row.
     * @param[in] topRow Zero-based top-level tree row, includes network/data/divider ordering.
     * @return Cached network category, NumCategories for negative/outside network rows; no live
     *   count/source query.
     */
    SWMMModelLayer::Category categoryAtTopRow(int topRow) const;

    /*! Top-level row for a given category, or -1 when that category
     *  contains no visible objects (empty categories are omitted).
     * @param[in] c Network category to locate in cached visible order.
     * @return First cached matching top row or -1, even if source has since been destroyed or
     *   counts changed before reset.
     */
    int topRowForCategory(SWMMModelLayer::Category c) const;

    /*! Model index for a specific leaf (category, row), or invalid if
     *  either is out of range / the category is hidden.
     * @param[in] c Spatial category; must be cached visible and have current source.
     * @param[in] row Zero-based current display row within category, including object-order
     *   override.
     * @return New virtual leaf index at column zero or invalid for missing
     *   source/category/out-of-range. No per-leaf allocation or selection mutation; invalidated by
     *   reset/reorder.
     */
    QModelIndex indexFor(SWMMModelLayer::Category c, int row) const;

    /*! Convenience: model index for a selection-bus object. Walks the
     *  layer's name→(cat,row) map — O(1) hash lookup. Returns an
     *  invalid QModelIndex when the object is unknown.
     * @param[in] ref Selection reference; only name used, type IGNORED. Empty name rejects.
     * @return Spatial leaf found by source name-location hash or invalid; does not resolve
     *   non-spatial data objects. Colliding names follow layer hash entry regardless of ref.type,
     *   so callers requiring typed resolution must check separately. No selection/visibility
     *   change.
     */
    QModelIndex indexFor(const SWMMObjectRef &ref) const;

    /*! Slice BM.0 — section discriminator for a top-level row.
     * @param[in] topRow Raw top-row number; function classifies without checking total row count.
     * @return Network for negative or rows before cached network end, Divider only at separator
     *   when both sections nonempty, Data for every later row including beyond tree. Use category
     *   lookups/index validation to establish actual existence.
     */
    Section sectionAtTopRow(int topRow) const;

    /*! Slice BM.0 — which SWMMModelLayer::DataCategory is rendered at
     *  top-level row \p topRow, or `NumDataCategories` when out of range
     *  or the row is in another section.
     * @param[in] topRow Zero-based top-level tree row including optional divider offset.
     * @return Cached data category or NumDataCategories for network/divider/out-of-range; no
     *   source/liveness query.
     */
    SWMMModelLayer::DataCategory dataCategoryAtTopRow(int topRow) const;

    /*! Slice BM.0 — top-level row for a given data category, or -1 when
     *  empty / not currently shown.
     * @param[in] c Data category to locate in cached nonempty enum-order list.
     * @return Matching top row including network/divider offset or -1; independent of current
     *   source liveness/count until reset.
     */
    int topRowForDataCategory(SWMMModelLayer::DataCategory c) const;

    // ── QAbstractItemModel interface ─────────────────────────────────────
    /**
     * @param[in] row Zero-based top row or child row, checked through hasIndex/current rowCount.
     * @param[in] column Only zero can pass single-column bounds.
     * @param[in] parent Invalid for top level, this model category index for leaves; leaf/divider
     *   has no children.
     * @return Virtual tagged category/divider or parent-top-row encoded leaf index, otherwise
     *   invalid. No leaf object allocation, engine mutation or sorting. Caller must not pass
     *   foreign/stale indexes.
     */
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    /**
     * @param[in] child Expected current index from this model; invalid/top-level returns invalid.
     * @return Category at encoded top row for leaf, with section tag derived from cached vectors.
     *   Does not validate stale internal ID against bounds/source, so old/foreign indexes
     *   unsupported. No allocation of tree item objects.
     */
    QModelIndex parent(const QModelIndex &child) const override;
    /**
     * @param[in] parent Invalid root, current category row, or leaf/divider; foreign/stale parent
     *   unsupported.
     * @return Zero without live source; root cached visible categories plus optional divider,
     *   category current live count, other parents zero. No per-leaf cache allocation or provider
     *   registry construction explicitly here.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] parent Unused, implementation leaves parameter unnamed; no hierarchy/source
     *   requirement.
     * @return One for root, categories and leaves even with no source. No mutation.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] index Expected current model index; invalid/null source/unresolved category or
     *   leaf bounds returns invalid.
     * @param[in] role Qt display/decoration/check/font and model Role* metadata; unsupported role
     *   invalid.
     * @return Network/data headings with live counts/icons, divider Data Objects bold italic, or
     *   current leaf name/reference. Network rows expose typed visibility checks; data rows have no
     *   checks. Storage display adds [LID] when engine query returns nonnegative control, with
     *   query status ignored. RoleObjectRef retains raw name without [LID] suffix; RoleRow is
     *   category display row.
     * @note RoleIsLeaf reports tag classification before category/leaf bounds. No engine/model
     *   writes, selection changes or per-leaf object ownership; storage display performs engine
     *   reads, so source state/lifetime must permit them.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @param[in] index Current spatial heading/leaf; data/divider/invalid/no-source rejected.
     * @param[in] value Converted toInt; ONLY exact Qt::Checked means visible,
     *   PartiallyChecked/other values mean hidden.
     * @param[in] role Only Qt::CheckStateRole accepted.
     * @return True after category/leaf visibility setter called, false for unsupported
     *   context/category/role. No independent leaf-row validation or setter-result reporting;
     *   current indexes required.
     * @note Direct presentation change without undo or engine content write. Emit check-role
     *   dataChanged for category and entire child range, or leaf and parent. Does not change
     *   selection/edit state; no rename/EditRole support.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @param[in] index Current tagged index; invalid root permits drops, foreign/stale indexes
     *   unsupported.
     * @return Divider inert; data heading/leaf enabled/selectable; spatial leaf
     *   selectable/checkable/draggable; spatial heading enabled/checkable/draggable/drop-enabled
     *   but not selectable. No editable cells. Does not query source liveness/count or current
     *   simulation state, so flags alone do not establish valid edit target.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @param[in] section Only logical column zero recognized.
     * @param[in] orientation Horizontal only.
     * @param[in] role DisplayRole only.
     * @return Translated Object, otherwise invalid. No vertical numbering/unit conversion or source
     *   query.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    // Drag-and-drop for category reordering (Slice T.2).
    /**
     * @return MoveAction only; no state change or guarantee a particular payload/target accepted.
     */
    Qt::DropActions supportedDropActions() const override;
    /**
     * @return Category-row and leaf-row custom application MIME formats. Payloads encode signed
     *   32-bit category, then leaf display row where applicable; no source/project identity or
     *   version field.
     */
    QStringList mimeTypes() const override;
    /**
     * @param[in] indexes Only FIRST index inspected, additional selected rows ignored. Expected
     *   current spatial category/leaf indexes.
     * @return Newly allocated parentless QMimeData owned by caller/view, or null for
     *   empty/invalid/unresolved category. Category payload has category enum; leaf payload has
     *   category plus current display-row coordinate. No stable object ID, layer identity, source
     *   mutation or lifetime pin; only current same-source drag is meaningful.
     */
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    /**
     * @param[in] data Required borrowed nonnull MIME object whenever MoveAction reaches format
     *   check; no null guard.
     * @param[in] action Only MoveAction considered.
     * @param[in] row Ignored preliminary target row, implementation unnamed.
     * @param[in] column Ignored preliminary column, implementation unnamed.
     * @param[in] parent Current target index or invalid root; category format permits root/network
     *   heading, leaf format requires ANY valid parent.
     * @return Preliminary shape acceptance only; no payload parsing, category equality, source
     *   presence or row bounds check. Leaf format may provisionally accept data/divider rows later
     *   rejected by dropMimeData. Category format wins when both present in this check, while
     *   actual drop decodes leaf first. No mutation.
     */
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override;
    /**
     * @param[in] data Borrowed nonnull custom payload; format check delegated to canDropMimeData,
     *   stream status/version not checked.
     * @param[in] action MoveAction required; others false.
     * @param[in] row Insertion row for category-child leaf drop or top-level heading drop;
     *   invalid/outside range means append in relevant path.
     * @param[in] column Ignored, preliminary check passed zero; implementation unnamed.
     * @param[in] parent Current target: network heading inserts leaf at row, sibling leaf inserts
     *   AFTER itself, root rejects leaf format. Headings can reorder at root/network heading.
     * @return True after source order setter attempted, false for rejected context, malformed
     *   enum/row/order size, cross-category leaf move or calculated no-op. True can occur for
     *   already-last category append, and does not independently confirm setter accepted.
     * @note Spatial presentation ordering only, no undo/engine object reindexing or multi-item
     *   move. Leaf moves current display-row permutation, falling back to default if override size
     *   differs; category move uses full order including hidden categories. Destination adjusted
     *   after removing source. Successful layer setter emits categoryOrderChanged and synchronously
     *   resets this model. Payload contains no origin, so host must prevent stale/cross-project
     *   foreign drops.
     */
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

public slots:
    /*! Full reset — call when the layer's SoA has been replaced
     *  wholesale (model reload / CRS reproject / add-node that shifts
     *  the category counts).
     * @note begin/endResetModel rebuild only nonempty network categories in source categoryOrder,
     *   then nonempty data categories in enum order. Optional divider synthesized by
     *   index/rowCount, no leaf data materialized. Null source clears vectors. Invalidates existing
     *   indexes/selection; does not clear pending scheduled timer, preserve expansion, mutate layer
     *   or emit visibility updates.
     */
    void reload();

    /*! Coalescing wrapper — schedules a single reload() on the next
     *  event-loop turn no matter how many change signals fire this turn.
     *  Data-object mutations arrive in bursts (a registry rename fires
     *  remove+add pairs; bulk imports fire per object) and some fire
     *  BEFORE the mutation completes (providerAboutToBeRemoved), so the
     *  deferred reload both dedupes and reads post-mutation state.
     * @note Set pending flag and post context-bound zero-delay timer; repeated calls while pending
     *   no-op. Timer clears flag then resets CURRENT source, even after rebind or an intervening
     *   immediate reload. Model destruction cancels callback. No immediate reset/engine mutation;
     *   requires GUI event loop.
     */
    void scheduleReload();

private:
    bool m_reloadPending = false;

    // internalId sentinels for top-level rows. Leaf rows store their
    // parent row index as internalId instead — leaf internalId is always
    // < kSeparatorId so the discriminator is unambiguous.
    static constexpr quintptr kCategoryId = std::numeric_limits<quintptr>::max();
    static constexpr quintptr kDataCategoryId = std::numeric_limits<quintptr>::max() - 1;
    static constexpr quintptr kSeparatorId = std::numeric_limits<quintptr>::max() - 2;

    QPointer<SWMMModelLayer> m_layer;

    // Visible-category list — excludes categories with 0 members so the
    // tree doesn't show empty "Pumps (0)" / etc. headers. Built on
    // reload(); index i in this vector is the model's top-level row for
    // that category.
    QVector<SWMMModelLayer::Category> m_visible;

    // Slice BM.0 — visible data-object categories (non-spatial). Same
    // contract as m_visible: empty categories are omitted. Rendered
    // below m_visible with a non-selectable separator row between when
    // both sections are non-empty.
    QVector<SWMMModelLayer::DataCategory> m_visibleData;
};

#endif // SWMMOBJECTTREEMODEL_H
