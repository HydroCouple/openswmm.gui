/*!
 * \file   swmmobjecttreemodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/panels/swmmobjecttreemodel.h"

#include <QDataStream>
#include <QFont>
#include <QIcon>
#include <QIODevice>
#include <QMimeData>

namespace {

// Category → human-readable group name. Kept parallel to the enum order
// so `categoryLabel(cat)` is an O(1) lookup and labels stay consistent
// between the model header and the legacy QTreeWidget code.
const char *categoryLabel(SWMMModelLayer::Category c)
{
    using L = SWMMModelLayer;
    switch (c) {
    case L::CatJunctions:     return QT_TRANSLATE_NOOP("ObjectBrowser", "Junctions");
    case L::CatOutfalls:      return QT_TRANSLATE_NOOP("ObjectBrowser", "Outfalls");
    case L::CatStorage:       return QT_TRANSLATE_NOOP("ObjectBrowser", "Storage Units");
    case L::CatDividers:      return QT_TRANSLATE_NOOP("ObjectBrowser", "Dividers");
    case L::CatConduits:      return QT_TRANSLATE_NOOP("ObjectBrowser", "Conduits");
    case L::CatPumps:         return QT_TRANSLATE_NOOP("ObjectBrowser", "Pumps");
    case L::CatOrifices:      return QT_TRANSLATE_NOOP("ObjectBrowser", "Orifices");
    case L::CatWeirs:         return QT_TRANSLATE_NOOP("ObjectBrowser", "Weirs");
    case L::CatOutlets:       return QT_TRANSLATE_NOOP("ObjectBrowser", "Outlets");
    case L::CatSubcatchments: return QT_TRANSLATE_NOOP("ObjectBrowser", "Subcatchments");
    case L::CatRainGages:     return QT_TRANSLATE_NOOP("ObjectBrowser", "Rain Gages");
    default: return "";
    }
}

QIcon iconForCategory(SWMMModelLayer::Category c)
{
    using L = SWMMModelLayer;
    switch (c) {
    case L::CatJunctions:     return QIcon(QStringLiteral(":/swmmvis/Junction"));
    case L::CatOutfalls:      return QIcon(QStringLiteral(":/swmmvis/Outfall"));
    case L::CatStorage:       return QIcon(QStringLiteral(":/swmmvis/Storage"));
    case L::CatDividers:      return QIcon(QStringLiteral(":/swmmvis/Divider"));
    case L::CatConduits:      return QIcon(QStringLiteral(":/swmmvis/Polyline"));
    case L::CatPumps:         return QIcon(QStringLiteral(":/swmmvis/Pump"));
    case L::CatOrifices:      return QIcon(QStringLiteral(":/swmmvis/Orifice"));
    case L::CatWeirs:         return QIcon(QStringLiteral(":/swmmvis/Weir"));
    case L::CatOutlets:       return QIcon(QStringLiteral(":/swmmvis/Outlet"));
    case L::CatSubcatchments: return QIcon(QStringLiteral(":/swmmvis/Subcatchment"));
    case L::CatRainGages:     return QIcon(QStringLiteral(":/swmmvis/Rainfall"));
    default:                  return QIcon(QStringLiteral(":/swmmvis/Layers"));
    }
}

QIcon iconForObjectType(SWMMObjectRef::ObjectType t)
{
    using R = SWMMObjectRef;
    switch (t) {
    case R::Node:         return QIcon(QStringLiteral(":/swmmvis/Node"));
    case R::Link:         return QIcon(QStringLiteral(":/swmmvis/Polyline"));
    case R::Subcatchment: return QIcon(QStringLiteral(":/swmmvis/Subcatchment"));
    case R::RainGage:     return QIcon(QStringLiteral(":/swmmvis/Rainfall"));
    default:              return QIcon(QStringLiteral(":/swmmvis/Layers"));
    }
}

/*! Mapping from layer category → SelectionManager object type. Only
 *  categories that correspond to a selectable ref-kind are listed;
 *  anything missing falls back to Unknown. */
SWMMObjectRef::ObjectType refTypeForCategory(SWMMModelLayer::Category c)
{
    using L = SWMMModelLayer;
    using R = SWMMObjectRef;
    switch (c) {
    case L::CatJunctions: case L::CatOutfalls:
    case L::CatStorage:   case L::CatDividers:    return R::Node;
    case L::CatConduits:  case L::CatPumps:
    case L::CatOrifices:  case L::CatWeirs:
    case L::CatOutlets:                            return R::Link;
    case L::CatSubcatchments:                      return R::Subcatchment;
    case L::CatRainGages:                          return R::RainGage;
    default:                                       return R::Unknown;
    }
}

// ── Slice BM.0 — DataCategory metadata ──────────────────────────────────────

const char *dataCategoryLabel(SWMMModelLayer::DataCategory c)
{
    using L = SWMMModelLayer;
    switch (c) {
    case L::DataCurves:      return QT_TRANSLATE_NOOP("ObjectBrowser", "Curves");
    case L::DataTimeSeries:  return QT_TRANSLATE_NOOP("ObjectBrowser", "Time Series");
    case L::DataPatterns:    return QT_TRANSLATE_NOOP("ObjectBrowser", "Time Patterns");
    case L::DataLIDControls: return QT_TRANSLATE_NOOP("ObjectBrowser", "LID Controls");
    case L::DataPollutants:  return QT_TRANSLATE_NOOP("ObjectBrowser", "Pollutants");
    case L::DataLandUses:    return QT_TRANSLATE_NOOP("ObjectBrowser", "Land Uses");
    case L::DataAquifers:    return QT_TRANSLATE_NOOP("ObjectBrowser", "Aquifers");
    case L::DataSnowpacks:   return QT_TRANSLATE_NOOP("ObjectBrowser", "Snowpacks");
    case L::DataControls:    return QT_TRANSLATE_NOOP("ObjectBrowser", "Control Rules");
    case L::DataTransects:   return QT_TRANSLATE_NOOP("ObjectBrowser", "Transects");
    case L::DataHydrographs: return QT_TRANSLATE_NOOP("ObjectBrowser", "Unit Hydrographs");
    case L::DataStreets:     return QT_TRANSLATE_NOOP("ObjectBrowser", "Streets");
    case L::DataInlets:      return QT_TRANSLATE_NOOP("ObjectBrowser", "Inlets");
    default:                 return "";
    }
}

/*! Reusable layers icon for data categories — the project's icon set
 *  doesn't yet ship per-data-type glyphs (deferred to a styling pass).
 *  Using :/swmmvis/Layers keeps the look consistent with the existing
 *  default-category icon fall-through. */
QIcon iconForDataCategory(SWMMModelLayer::DataCategory /*c*/)
{
    return QIcon(QStringLiteral(":/swmmvis/Layers"));
}

/*! Map a DataCategory to its selection-bus object-type. */
SWMMObjectRef::ObjectType refTypeForDataCategory(SWMMModelLayer::DataCategory c)
{
    using L = SWMMModelLayer;
    using R = SWMMObjectRef;
    switch (c) {
    case L::DataCurves:      return R::Curve;
    case L::DataTimeSeries:  return R::TimeSeries;
    case L::DataPatterns:    return R::TimePattern;
    case L::DataLIDControls: return R::LIDControl;
    case L::DataPollutants:  return R::Pollutant;
    case L::DataLandUses:    return R::LandUse;
    case L::DataAquifers:    return R::Aquifer;
    case L::DataSnowpacks:   return R::Snowpack;
    case L::DataControls:    return R::Control;
    case L::DataTransects:   return R::Transect;
    case L::DataHydrographs: return R::Hydrograph;
    case L::DataStreets:     return R::Street;
    case L::DataInlets:      return R::Inlet;
    default:                 return R::Unknown;
    }
}

} // anonymous

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SWMMObjectTreeModel::SWMMObjectTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

SWMMObjectTreeModel::~SWMMObjectTreeModel() = default;

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

void SWMMObjectTreeModel::setLayer(SWMMModelLayer *layer)
{
    if (m_layer == layer) return;

    if (m_layer) {
        QObject::disconnect(m_layer.data(), &SWMMModelLayer::modelLoaded,
                            this,           &SWMMObjectTreeModel::reload);
        QObject::disconnect(m_layer.data(), &SWMMModelLayer::categoryOrderChanged,
                            this,           &SWMMObjectTreeModel::reload);
        QObject::disconnect(m_layer.data(), &SWMMModelLayer::hydrographChanged,
                            this, nullptr);
    }

    m_layer = layer;
    reload();

    if (m_layer) {
        // Geometry reload (model re-open, add / remove object) → full reset.
        connect(m_layer.data(), &SWMMModelLayer::modelLoaded,
                this,           &SWMMObjectTreeModel::reload,
                Qt::UniqueConnection);
        // User reorder of categories (Slice T.2) → reshape the
        // visible-category list without touching per-category counts.
        connect(m_layer.data(), &SWMMModelLayer::categoryOrderChanged,
                this,           &SWMMObjectTreeModel::reload,
                Qt::UniqueConnection);
        // Slice BS Phase 6.9.2 — UH group added / removed / renamed via
        // HydrographGroupEditor or any other MVC path: rebuild the
        // Unit Hydrographs branch so the user sees the change in the
        // Object Browser without needing to refresh manually.
        // Qt 6 asserts on Qt::UniqueConnection with non-PMF slots; rely on
        // signal-argument truncation to invoke the no-arg reload() PMF.
        connect(m_layer.data(), &SWMMModelLayer::hydrographChanged,
                this,           &SWMMObjectTreeModel::reload,
                Qt::UniqueConnection);
    }
}

void SWMMObjectTreeModel::reload()
{
    beginResetModel();
    m_visible.clear();
    m_visibleData.clear();
    if (m_layer) {
        // Walk the layer's user-configurable category order (Slice T.2)
        // instead of the hard-coded enum sequence; empty categories
        // still drop out so headers only appear for types the model
        // actually contains.
        const auto order = m_layer->categoryOrder();
        for (auto cat : order) {
            if (m_layer->categoryCount(cat) > 0)
                m_visible.append(cat);
        }
        // Slice BM.0 — data-objects section. Always show every data
        // category with count > 0; ordering matches the enum (no
        // user-reorder yet — deferred to a future Slice AY.4 if asked).
        for (int i = 0; i < int(SWMMModelLayer::NumDataCategories); ++i) {
            const auto dc = static_cast<SWMMModelLayer::DataCategory>(i);
            if (m_layer->dataObjectCount(dc) > 0)
                m_visibleData.append(dc);
        }
    }
    endResetModel();
}

SWMMModelLayer::Category
SWMMObjectTreeModel::categoryAtTopRow(int topRow) const
{
    if (topRow < 0 || topRow >= m_visible.size())
        return SWMMModelLayer::NumCategories;
    return m_visible[topRow];
}

int SWMMObjectTreeModel::topRowForCategory(SWMMModelLayer::Category c) const
{
    return m_visible.indexOf(c);
}

// ── Slice BM.0 — section + data-category lookups ────────────────────────────

SWMMObjectTreeModel::Section
SWMMObjectTreeModel::sectionAtTopRow(int topRow) const
{
    if (topRow < 0) return SectionNetwork;
    if (topRow < m_visible.size()) return SectionNetwork;
    const bool hasBoth = !m_visible.isEmpty() && !m_visibleData.isEmpty();
    const int  divIdx  = m_visible.size();           // row index of divider when both sections
    if (hasBoth && topRow == divIdx) return SectionDivider;
    return SectionData;
}

SWMMModelLayer::DataCategory
SWMMObjectTreeModel::dataCategoryAtTopRow(int topRow) const
{
    if (sectionAtTopRow(topRow) != SectionData)
        return SWMMModelLayer::NumDataCategories;
    const bool hasBoth = !m_visible.isEmpty() && !m_visibleData.isEmpty();
    const int  offset  = m_visible.size() + (hasBoth ? 1 : 0);
    const int  rowInData = topRow - offset;
    if (rowInData < 0 || rowInData >= m_visibleData.size())
        return SWMMModelLayer::NumDataCategories;
    return m_visibleData[rowInData];
}

int SWMMObjectTreeModel::topRowForDataCategory(SWMMModelLayer::DataCategory c) const
{
    const int rowInData = m_visibleData.indexOf(c);
    if (rowInData < 0) return -1;
    const bool hasBoth = !m_visible.isEmpty() && !m_visibleData.isEmpty();
    return m_visible.size() + (hasBoth ? 1 : 0) + rowInData;
}

QModelIndex
SWMMObjectTreeModel::indexFor(SWMMModelLayer::Category c, int row) const
{
    const int top = topRowForCategory(c);
    if (top < 0 || !m_layer) return {};
    if (row < 0 || row >= m_layer->categoryCount(c)) return {};
    return createIndex(row, 0, quintptr(top));
}

QModelIndex
SWMMObjectTreeModel::indexFor(const SWMMObjectRef &ref) const
{
    if (!m_layer || ref.name.isEmpty()) return {};
    SWMMModelLayer::Category cat;
    int row = 0;
    if (!m_layer->findObjectLocation(ref.name, &cat, &row))
        return {};
    return indexFor(cat, row);
}

// ---------------------------------------------------------------------------
// QAbstractItemModel
// ---------------------------------------------------------------------------

QModelIndex
SWMMObjectTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent)) return {};
    if (!parent.isValid()) {
        // Top-level row — tag by section.
        const auto sec = sectionAtTopRow(row);
        switch (sec) {
        case SectionNetwork:  return createIndex(row, column, kCategoryId);
        case SectionData:     return createIndex(row, column, kDataCategoryId);
        case SectionDivider:  return createIndex(row, column, kSeparatorId);
        }
        return {};
    }
    if (parent.internalId() == kCategoryId ||
        parent.internalId() == kDataCategoryId)
        return createIndex(row, column, quintptr(parent.row())); // leaf row
    return {};
}

QModelIndex SWMMObjectTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) return {};
    const auto id = child.internalId();
    if (id == kCategoryId || id == kDataCategoryId || id == kSeparatorId)
        return {};
    // Leaf — child.internalId() holds the parent top-level row. Re-derive
    // the section tag so the round-trip stays consistent with index().
    const int topRow = int(id);
    const auto sec   = sectionAtTopRow(topRow);
    const quintptr parentId = (sec == SectionData) ? kDataCategoryId : kCategoryId;
    return createIndex(topRow, 0, parentId);
}

int SWMMObjectTreeModel::rowCount(const QModelIndex &parent) const
{
    if (!m_layer) return 0;
    if (!parent.isValid()) {
        // Top-level rows: spatial categories + optional divider + data categories.
        const bool hasBoth = !m_visible.isEmpty() && !m_visibleData.isEmpty();
        return m_visible.size() + (hasBoth ? 1 : 0) + m_visibleData.size();
    }
    if (parent.internalId() == kCategoryId)
        return m_layer->categoryCount(categoryAtTopRow(parent.row()));
    if (parent.internalId() == kDataCategoryId)
        return m_layer->dataObjectCount(dataCategoryAtTopRow(parent.row()));
    return 0;
}

int SWMMObjectTreeModel::columnCount(const QModelIndex & /*parent*/) const
{
    return 1;
}

QVariant SWMMObjectTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_layer) return {};

    const auto id = index.internalId();
    const bool isCategoryRow     = (id == kCategoryId);
    const bool isDataCategoryRow = (id == kDataCategoryId);
    const bool isDividerRow      = (id == kSeparatorId);
    const bool isLeaf = !isCategoryRow && !isDataCategoryRow && !isDividerRow;

    if (role == RoleIsLeaf) return isLeaf;

    // ── Section divider row ──────────────────────────────────────────────
    if (isDividerRow) {
        switch (role) {
        case Qt::DisplayRole: return tr("Data Objects");
        case Qt::FontRole:    { QFont f; f.setBold(true); f.setItalic(true); return f; }
        case RoleSection:     return int(SectionDivider);
        default:              return {};
        }
    }

    // ── Spatial category row ─────────────────────────────────────────────
    if (isCategoryRow) {
        const auto cat = categoryAtTopRow(index.row());
        if (cat == SWMMModelLayer::NumCategories) return {};
        const int count = m_layer->categoryCount(cat);
        switch (role) {
        case Qt::DisplayRole:
            return QStringLiteral("%1 (%2)")
                   .arg(tr(categoryLabel(cat))).arg(count);
        case Qt::DecorationRole:
            return iconForCategory(cat);
        case Qt::CheckStateRole:
            return QVariant::fromValue(int(m_layer->categoryCheckState(cat)));
        case Qt::FontRole: { QFont f; f.setBold(true); return f; }
        case RoleSection:  return int(SectionNetwork);
        case RoleCategory: return int(cat);
        default:           return {};
        }
    }

    // ── Data-objects category row (Slice BM.0) ───────────────────────────
    if (isDataCategoryRow) {
        const auto dc = dataCategoryAtTopRow(index.row());
        if (dc == SWMMModelLayer::NumDataCategories) return {};
        const int count = m_layer->dataObjectCount(dc);
        switch (role) {
        case Qt::DisplayRole:
            return QStringLiteral("%1 (%2)")
                   .arg(tr(dataCategoryLabel(dc))).arg(count);
        case Qt::DecorationRole:
            return iconForDataCategory(dc);
        case Qt::FontRole: { QFont f; f.setBold(true); return f; }
        case RoleSection:        return int(SectionData);
        case RoleDataCategory:   return int(dc);
        default:                 return {};
        }
    }

    // ── Leaf row ─────────────────────────────────────────────────────────
    const int topRow = int(id);
    const auto sec   = sectionAtTopRow(topRow);

    if (sec == SectionNetwork) {
        const auto cat = categoryAtTopRow(topRow);
        if (cat == SWMMModelLayer::NumCategories) return {};
        const int row = index.row();
        if (row < 0 || row >= m_layer->categoryCount(cat)) return {};
        const QString name = m_layer->objectNameAt(cat, row);
        switch (role) {
        case Qt::DisplayRole: return name;
        case Qt::DecorationRole:
            return iconForObjectType(refTypeForCategory(cat));
        case Qt::CheckStateRole:
            // Typed query — a rain gage and subcatchment may share a name;
            // this leaf's check state must reflect only its own kind.
            return QVariant::fromValue(int(m_layer->isObjectVisible(name, cat)
                                                ? Qt::Checked : Qt::Unchecked));
        case RoleSection:  return int(SectionNetwork);
        case RoleCategory: return int(cat);
        case RoleRow:      return row;
        case RoleObjectRef:
            return QVariant::fromValue(SWMMObjectRef{refTypeForCategory(cat), name});
        default:           return {};
        }
    }

    if (sec == SectionData) {
        const auto dc = dataCategoryAtTopRow(topRow);
        if (dc == SWMMModelLayer::NumDataCategories) return {};
        const int row = index.row();
        if (row < 0 || row >= m_layer->dataObjectCount(dc)) return {};
        const QString name = m_layer->dataObjectNameAt(dc, row);
        switch (role) {
        case Qt::DisplayRole:    return name;
        case Qt::DecorationRole: return iconForDataCategory(dc);
        case RoleSection:        return int(SectionData);
        case RoleDataCategory:   return int(dc);
        case RoleRow:            return row;
        case RoleObjectRef:
            return QVariant::fromValue(
                SWMMObjectRef{refTypeForDataCategory(dc), name});
        default:                 return {};
        }
    }

    return {};
}

bool SWMMObjectTreeModel::setData(const QModelIndex &index,
                                   const QVariant &value, int role)
{
    if (!index.isValid() || !m_layer || role != Qt::CheckStateRole)
        return false;

    const auto id = index.internalId();
    // Slice BM.0 — data-section rows do not carry a visibility check; the
    // map canvas only paints spatial objects, so toggling here would be a
    // no-op. Block the write rather than silently no-op so the view's
    // check state doesn't drift from the model.
    if (id == kDataCategoryId || id == kSeparatorId) return false;
    const bool isLeafInDataSection = (id < kSeparatorId) &&
        (sectionAtTopRow(int(id)) == SectionData);
    if (isLeafInDataSection) return false;

    const bool checked = value.toInt() == Qt::Checked;
    const bool isLeaf  = (id != kCategoryId);

    if (!isLeaf) {
        // Category header → bulk toggle. One signal emission covers the
        // entire child range.
        const auto cat = categoryAtTopRow(index.row());
        if (cat == SWMMModelLayer::NumCategories) return false;
        m_layer->setCategoryVisible(cat, checked);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        const int count = m_layer->categoryCount(cat);
        if (count > 0) {
            const QModelIndex first = this->index(0,         0, index);
            const QModelIndex last  = this->index(count - 1, 0, index);
            emit dataChanged(first, last, {Qt::CheckStateRole});
        }
        return true;
    }

    // Leaf toggle — update the leaf and its parent category (tri-state
    // check display) in two dataChanged emissions.
    const auto cat = categoryAtTopRow(int(id));
    if (cat == SWMMModelLayer::NumCategories) return false;
    const int row = index.row();
    m_layer->setObjectVisibleAt(cat, row, checked);
    emit dataChanged(index, index, {Qt::CheckStateRole});
    const QModelIndex parentIdx = this->parent(index);
    if (parentIdx.isValid())
        emit dataChanged(parentIdx, parentIdx, {Qt::CheckStateRole});
    return true;
}

Qt::ItemFlags SWMMObjectTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        // Empty-space drops at the root are how the view signals
        // "append here"; enable drops for category reordering.
        return Qt::ItemIsDropEnabled;

    const auto id = index.internalId();

    // Slice BM.0 — separator row: visible but inert (no selection, no
    // checkbox, no drag/drop). Qt::NoItemFlags renders as a disabled,
    // non-selectable label — exactly what we want for the divider.
    if (id == kSeparatorId)
        return Qt::NoItemFlags;

    // Slice BM.0 — data-objects category header: enabled, selectable
    // (for right-click "Add New …" / "Sort A→Z" context-menu), but NOT
    // draggable / drop-target / checkable. The Slice T.2 drag-drop
    // reorder contract is reserved for spatial categories.
    if (id == kDataCategoryId)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;

    const bool isLeaf = (id != kCategoryId);
    if (isLeaf) {
        // Discriminate spatial-section vs data-section leaves.
        if (sectionAtTopRow(int(id)) == SectionData) {
            // Data-object leaf — selectable + draggable later but not
            // visibility-checkable (no map paint to toggle).
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        }
        // Spatial leaf — selectable + checkable + DRAGGABLE
        // (Slice T.3 lets users reorder within a category). Drops on
        // a leaf fall through to the parent category's child range.
        return Qt::ItemIsEnabled
             | Qt::ItemIsSelectable
             | Qt::ItemIsUserCheckable
             | Qt::ItemIsDragEnabled;
    }

    // Spatial category rows — draggable (Slice T.2) and drop-enabled
    // as both sibling drop targets (category reorder) and child-range
    // drop targets (object reorder within the category).
    return Qt::ItemIsEnabled
         | Qt::ItemIsUserCheckable
         | Qt::ItemIsDragEnabled
         | Qt::ItemIsDropEnabled;
}

QVariant SWMMObjectTreeModel::headerData(int section,
                                          Qt::Orientation orientation,
                                          int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section != 0)
        return {};
    return tr("Object");
}

// ---------------------------------------------------------------------------
// Drag-and-drop — category reordering (Slice T.2)
// ---------------------------------------------------------------------------

Qt::DropActions SWMMObjectTreeModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QStringList SWMMObjectTreeModel::mimeTypes() const
{
    return {QStringLiteral("application/x-swmm-category-row"),
            QStringLiteral("application/x-swmm-leaf-row")};
}

QMimeData *SWMMObjectTreeModel::mimeData(const QModelIndexList &indexes) const
{
    if (indexes.isEmpty()) return nullptr;

    const QModelIndex src = indexes.first();
    if (!src.isValid()) return nullptr;

    auto *mime = new QMimeData;
    QByteArray buf;
    QDataStream ds(&buf, QIODevice::WriteOnly);

    if (src.internalId() == kCategoryId) {
        // Category row drag (Slice T.2).
        const auto cat = categoryAtTopRow(src.row());
        if (cat == SWMMModelLayer::NumCategories) { delete mime; return nullptr; }
        ds << qint32(cat);
        mime->setData(QStringLiteral("application/x-swmm-category-row"), buf);
        return mime;
    }

    // Leaf row drag (Slice T.3) — encode parent category + visible row.
    const auto cat = categoryAtTopRow(int(src.internalId()));
    if (cat == SWMMModelLayer::NumCategories) { delete mime; return nullptr; }
    ds << qint32(cat) << qint32(src.row());
    mime->setData(QStringLiteral("application/x-swmm-leaf-row"), buf);
    return mime;
}

bool SWMMObjectTreeModel::canDropMimeData(const QMimeData *data,
                                           Qt::DropAction action,
                                           int /*row*/, int /*column*/,
                                           const QModelIndex &parent) const
{
    if (action != Qt::MoveAction) return false;

    const bool hasCat  = data->hasFormat(QStringLiteral("application/x-swmm-category-row"));
    const bool hasLeaf = data->hasFormat(QStringLiteral("application/x-swmm-leaf-row"));
    if (!hasCat && !hasLeaf) return false;

    if (hasCat) {
        // Category drops: only at the top level — reject drops that
        // would make a category a child of another.
        if (parent.isValid() && parent.internalId() != kCategoryId)
            return false;
        return true;
    }

    // Leaf drops: must land inside a category's child range. The drop
    // target parent must be a category row (or a sibling leaf inside
    // one — Qt treats "drop above leaf X" as parent = leaf X's parent
    // which is the category). Empty-space (invalid parent) leaf drops
    // are rejected — they'd imply moving the leaf to the root.
    if (!parent.isValid()) return false;
    if (parent.internalId() == kCategoryId) return true;   // child-range drop
    // Dropped onto a sibling leaf — accepted; we'll re-route through
    // the leaf's parent category in dropMimeData.
    return true;
}

bool SWMMObjectTreeModel::dropMimeData(const QMimeData *data,
                                        Qt::DropAction action,
                                        int row, int /*column*/,
                                        const QModelIndex &parent)
{
    if (!canDropMimeData(data, action, row, 0, parent)) return false;
    if (!m_layer) return false;

    // --- Leaf drop (Slice T.3 — intra-category reorder) ---------------
    if (data->hasFormat(QStringLiteral("application/x-swmm-leaf-row"))) {
        QByteArray buf = data->data(QStringLiteral("application/x-swmm-leaf-row"));
        QDataStream ds(&buf, QIODevice::ReadOnly);
        qint32 srcCatInt = -1, srcRow = -1;
        ds >> srcCatInt >> srcRow;
        if (srcCatInt < 0 || srcCatInt >= int(SWMMModelLayer::NumCategories))
            return false;
        const auto srcCat = static_cast<SWMMModelLayer::Category>(srcCatInt);

        // Determine target category + insertion row.
        SWMMModelLayer::Category tgtCat = SWMMModelLayer::NumCategories;
        int tgtRow = -1;
        if (parent.isValid() && parent.internalId() == kCategoryId) {
            tgtCat = categoryAtTopRow(parent.row());
            tgtRow = row;   // row within the category
        } else if (parent.isValid() && parent.internalId() != kCategoryId) {
            // Dropped onto / near a sibling leaf — re-route to its
            // parent category.
            tgtCat = categoryAtTopRow(int(parent.internalId()));
            tgtRow = parent.row() + 1;   // after that sibling
        }
        if (tgtCat == SWMMModelLayer::NumCategories) return false;
        if (tgtCat != srcCat) return false;   // cross-category not supported

        const int n = m_layer->categoryCount(srcCat);
        if (srcRow < 0 || srcRow >= n) return false;
        if (tgtRow < 0 || tgtRow > n) tgtRow = n;   // drop-past-end

        // Starting permutation: use the existing override if one is
        // installed (preserves prior drags), otherwise the default SoA
        // bucket for this category.
        QVector<int> order = m_layer->objectOrder(srcCat);
        if (order.size() != n)
            order = m_layer->defaultObjectOrder(srcCat);
        if (order.size() != n) return false;
        if (srcRow >= order.size()) return false;

        // Adjust tgtRow after the source is removed.
        int adjusted = tgtRow;
        if (adjusted > srcRow) --adjusted;
        if (adjusted == srcRow) return false;
        order.move(srcRow, adjusted);
        m_layer->setObjectOrder(srcCat, order);
        return true;
    }

    // --- Category drop (Slice T.2 — reorder headers) ------------------
    if (data->hasFormat(QStringLiteral("application/x-swmm-category-row"))) {
        QByteArray buf = data->data(QStringLiteral("application/x-swmm-category-row"));
        QDataStream ds(&buf, QIODevice::ReadOnly);
        qint32 srcCatInt = -1;
        ds >> srcCatInt;
        if (srcCatInt < 0 || srcCatInt >= int(SWMMModelLayer::NumCategories))
            return false;
        const auto srcCat = static_cast<SWMMModelLayer::Category>(srcCatInt);

        QVector<SWMMModelLayer::Category> order = m_layer->categoryOrder();
        const int srcPos = order.indexOf(srcCat);
        if (srcPos < 0) return false;

        SWMMModelLayer::Category targetCat;
        if (parent.isValid() && parent.internalId() == kCategoryId) {
            targetCat = categoryAtTopRow(parent.row());
        } else if (row >= 0 && row < m_visible.size()) {
            targetCat = m_visible[row];
        } else {
            order.move(srcPos, order.size() - 1);
            m_layer->setCategoryOrder(order);
            return true;
        }

        int dstPos = order.indexOf(targetCat);
        if (dstPos < 0) return false;
        if (srcPos < dstPos) --dstPos;
        if (dstPos == srcPos) return false;

        order.move(srcPos, dstPos);
        m_layer->setCategoryOrder(order);
        return true;
    }

    return false;
}
