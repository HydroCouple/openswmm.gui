/*!
 * \file   swmmobjecttreemodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 * Only the Category rows materialise as rows; leaf rows exist purely as
 * QModelIndex coordinates that index into the layer's per-category
 * buckets. `data()` fetches names through the O(1) layer API, so 1M
 * leaves costs nothing at rest — only visible rows trigger data() calls.
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
        RoleCategory   = Qt::UserRole + 10,   // int(SWMMModelLayer::Category)
        RoleRow        = Qt::UserRole + 11,   // row within category
        RoleIsLeaf     = Qt::UserRole + 12,   // bool
        RoleObjectRef  = Qt::UserRole + 13,   // QVariant::fromValue<SWMMObjectRef>
        // Slice BM.0 — non-spatial data-objects section.
        RoleSection      = Qt::UserRole + 14, // int(Section)
        RoleDataCategory = Qt::UserRole + 15, // int(SWMMModelLayer::DataCategory)
    };

    /*! Slice BM.0 — top-level partitioning of the tree. */
    enum Section : int {
        SectionNetwork = 0, ///< Spatial categories (Junctions … Rain Gages)
        SectionDivider = 1, ///< Visual separator row (non-selectable)
        SectionData    = 2, ///< Non-spatial data objects (Curves … Inlets)
    };

    explicit SWMMObjectTreeModel(QObject *parent = nullptr);
    ~SWMMObjectTreeModel() override;

    /*! Bind the model to a SWMM layer. Triggers beginResetModel /
     *  endResetModel and rebuilds the cached visible-category list. Pass
     *  nullptr to detach. */
    void setLayer(SWMMModelLayer *layer);

    /*! Which SWMMModelLayer::Category is rendered at top-level row
     *  \p topRow, or `NumCategories` if the row is out of range or the
     *  row is a data / divider row. */
    SWMMModelLayer::Category categoryAtTopRow(int topRow) const;

    /*! Top-level row for a given category, or -1 when that category
     *  contains no visible objects (empty categories are omitted). */
    int topRowForCategory(SWMMModelLayer::Category c) const;

    /*! Model index for a specific leaf (category, row), or invalid if
     *  either is out of range / the category is hidden. */
    QModelIndex indexFor(SWMMModelLayer::Category c, int row) const;

    /*! Convenience: model index for a selection-bus object. Walks the
     *  layer's name→(cat,row) map — O(1) hash lookup. Returns an
     *  invalid QModelIndex when the object is unknown. */
    QModelIndex indexFor(const SWMMObjectRef &ref) const;

    /*! Slice BM.0 — section discriminator for a top-level row. */
    Section sectionAtTopRow(int topRow) const;

    /*! Slice BM.0 — which SWMMModelLayer::DataCategory is rendered at
     *  top-level row \p topRow, or `NumDataCategories` when out of range
     *  or the row is in another section. */
    SWMMModelLayer::DataCategory dataCategoryAtTopRow(int topRow) const;

    /*! Slice BM.0 — top-level row for a given data category, or -1 when
     *  empty / not currently shown. */
    int topRowForDataCategory(SWMMModelLayer::DataCategory c) const;

    // ── QAbstractItemModel interface ─────────────────────────────────────
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int         rowCount(const QModelIndex &parent = {}) const override;
    int         columnCount(const QModelIndex &parent = {}) const override;
    QVariant    data(const QModelIndex &index,
                     int role = Qt::DisplayRole) const override;
    bool        setData(const QModelIndex &index,
                        const QVariant &value,
                        int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant    headerData(int section, Qt::Orientation orientation,
                           int role = Qt::DisplayRole) const override;

    // Drag-and-drop for category reordering (Slice T.2).
    Qt::DropActions supportedDropActions() const override;
    QStringList     mimeTypes() const override;
    QMimeData      *mimeData(const QModelIndexList &indexes) const override;
    bool            canDropMimeData(const QMimeData *data, Qt::DropAction action,
                                     int row, int column,
                                     const QModelIndex &parent) const override;
    bool            dropMimeData(const QMimeData *data, Qt::DropAction action,
                                  int row, int column,
                                  const QModelIndex &parent) override;

public slots:
    /*! Full reset — call when the layer's SoA has been replaced
     *  wholesale (model reload / CRS reproject / add-node that shifts
     *  the category counts). */
    void reload();

private:
    // internalId sentinels for top-level rows. Leaf rows store their
    // parent row index as internalId instead — leaf internalId is always
    // < kSeparatorId so the discriminator is unambiguous.
    static constexpr quintptr kCategoryId      = std::numeric_limits<quintptr>::max();
    static constexpr quintptr kDataCategoryId  = std::numeric_limits<quintptr>::max() - 1;
    static constexpr quintptr kSeparatorId     = std::numeric_limits<quintptr>::max() - 2;

    QPointer<SWMMModelLayer>             m_layer;

    // Visible-category list — excludes categories with 0 members so the
    // tree doesn't show empty "Pumps (0)" / etc. headers. Built on
    // reload(); index i in this vector is the model's top-level row for
    // that category.
    QVector<SWMMModelLayer::Category>    m_visible;

    // Slice BM.0 — visible data-object categories (non-spatial). Same
    // contract as m_visible: empty categories are omitted. Rendered
    // below m_visible with a non-selectable separator row between when
    // both sections are non-empty.
    QVector<SWMMModelLayer::DataCategory> m_visibleData;
};

#endif // SWMMOBJECTTREEMODEL_H
