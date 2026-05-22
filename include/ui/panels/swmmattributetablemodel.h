/*!
 * \file   swmmattributetablemodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice Z.1 — Per-category tabular model over the active SWMMModelLayer.
 *
 * One instance is bound to one (layer, category) pair via setSource().
 * Rows correspond 1:1 to objects in that category in the layer's stable
 * ordering (matches Object Browser's row order); the column schema is
 * fixed per category and currently sourced from
 * `SWMMModelLayer::identifyByName()` for parity with the Identify tool.
 *
 * The model is intentionally NOT a generic QAbstractItemModel<Category>
 * template — keeping it a concrete class avoids template-in-header
 * compile-time overhead and lets `AttributeTablePanel` swap the bound
 * category in O(1) without rebuilding the model object.
 */

#ifndef SWMMATTRIBUTETABLEMODEL_H
#define SWMMATTRIBUTETABLEMODEL_H

#include <QAbstractTableModel>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

class QUndoStack;

#include "layers/swmmmodellayer.h"

namespace openswmmvis {

/*! Slice Z.5.2 — How a column is edited (or not). */
enum class EditorKind {
    ReadOnly,   ///< Default; flags() omits ItemIsEditable.
    Numeric,    ///< NumericDelegate (QDoubleSpinBox).
    Integer,    ///< IntegerDelegate (QSpinBox).
    Enum,       ///< EnumDelegate (QComboBox) with pre-baked pairs.
    Text,       ///< Plain QLineEdit (default delegate); used for Name column.
};

/*! Round-4 follow-up 2026-05-12 — semantic unit class.  Resolves to
 *  a string at render time via `UnitSystem::instance()` so the
 *  Attribute Table header reflects the current flow-units system
 *  (US-customary vs SI).  Columns without a physical unit set
 *  `UnitKind::None`; the column header then carries no suffix. */
enum class UnitKind {
    None,         ///< No physical unit (counts, ids, names, etc.)
    Length,       ///< ft / m  (invert, max depth, offsets, crest height)
    Area,         ///< ft² / m²  (ponded area, cross-section)
    SubcatchArea, ///< ac / ha  (subcatchment area; SWMM convention)
    Volume,       ///< ft³ / m³
    Velocity,     ///< ft/s / m/s
    FlowRate,     ///< CFS / CMS — uses UnitSystem::flowUnitLabel
    Depression,   ///< in / mm  (depression storage, rainfall)
    Percent,      ///< % — always
    Rate,         ///< in/hr / mm/hr (infiltration, seepage)
};

/*! Column metadata.  Drives both the read path (identify-map key
 *  for display) and the edit path (engine setter for commit).
 *
 *  `setter` is the name of a tag used to dispatch to an engine
 *  setter via a function-pointer table in the .cpp.  Empty means
 *  read-only — `flags(idx)` will not return ItemIsEditable. */
struct ColumnSpec {
    QString      key;          ///< identifyByName-map key (display)
    QString      label;        ///< Header label (without unit suffix)
    EditorKind   editor   = EditorKind::ReadOnly;
    QString      setter;       ///< Setter tag (empty == read-only)
    QVariantList enumValues;   ///< For Enum: list of {label, data} pairs
    double       minValue = -std::numeric_limits<double>::infinity();
    double       maxValue =  std::numeric_limits<double>::infinity();
    int          decimals = 4;
    UnitKind     unit = UnitKind::None;  ///< Semantic unit (resolved at render)
};

} // namespace openswmmvis

class SWMMAttributeTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit SWMMAttributeTableModel(QObject *parent = nullptr);

    /*! Bind the model to a (layer, category) pair.  Rebuilds the
     *  column schema, then `beginResetModel` / `endResetModel`.  Pass
     *  a null layer to clear. */
    void setSource(SWMMModelLayer *layer, SWMMModelLayer::Category category);

    [[nodiscard]] SWMMModelLayer *layer() const noexcept { return m_layer; }
    [[nodiscard]] SWMMModelLayer::Category category() const noexcept
    { return m_category; }

    /*! Object name for a row, or empty string if out of range. */
    [[nodiscard]] QString objectNameAt(int row) const;

    /*! Inverse — find the model row for a given object name in the
     *  current category.  Returns -1 if not found. */
    [[nodiscard]] int rowForName(const QString &name) const;

    /*! Column-zero header is always "Name"; column 0 returns the
     *  object's name unmodified.  The remaining columns map to keys
     *  from the layer's identify map; column count is fixed per
     *  category. */
    QStringList columnKeys() const { return m_columnKeys; }

    /*! Slice Z.5.2 — column metadata (delegate kind, setter, range).
     *  Returns the full spec list for the bound category so callers
     *  (e.g. AttributeTablePanel) can install delegates. */
    QList<openswmmvis::ColumnSpec> columnSpecs() const { return m_columnSpecs; }

    /*! Slice Z.5.5 — optional QUndoStack for cell-edit commands.
     *  When set, `setData()` wraps each commit in an `EditCommand`
     *  pushed onto this stack so Ctrl+Z / Ctrl+Y round-trip.  Pass
     *  nullptr to disable undo (edits commit immediately). */
    void setUndoStack(QUndoStack *stack) { m_undoStack = stack; }
    [[nodiscard]] QUndoStack *undoStack() const { return m_undoStack; }

    /*! Direct-commit setter — bypasses the undo stack.  Used by
     *  `EditCommand::redo` / `undo` so re-applying a command
     *  doesn't push another command. */
    bool commitValueDirect(const QModelIndex &index, const QVariant &value);

    // QAbstractTableModel ----------------------------------------------------
    int      rowCount(const QModelIndex &parent = {}) const override;
    int      columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool     setData(const QModelIndex &index, const QVariant &value,
                     int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

public slots:
    /*! Force a full reload (e.g. after the underlying SoA changed). */
    void reload();

    /*! Round-4 follow-up 2026-05-12 — refresh a single row by object
     *  name.  Invalidates the row cache so the next data() read
     *  re-queries the engine, and emits dataChanged across the row
     *  so the view repaints.  Used by the panel to mirror property-
     *  browser edits without doing a full reset. */
    void refreshObject(const QString &name);

signals:
    /*! Emitted after a successful direct-edit commit (Attribute
     *  Table cell editor → engine setter).  Listeners (e.g. the
     *  Property Browser dock) use this to refresh their view of
     *  the same object.  Distinct from QAbstractItemModel::
     *  dataChanged because it fires only on user-initiated edits,
     *  not on synthetic refreshes — preventing feedback loops. */
    void objectEdited(const QString &name);

private:
    void rebuildColumnSchema();
    QVariantMap rowData(int row) const;

    QPointer<SWMMModelLayer>           m_layer;
    SWMMModelLayer::Category           m_category = SWMMModelLayer::CatJunctions;
    QStringList                        m_columnKeys;     ///< Map keys from identifyByName
    QStringList                        m_columnLabels;   ///< User-facing header labels
    QList<openswmmvis::ColumnSpec>     m_columnSpecs;    ///< Per-column metadata (Z.5.2)
    QUndoStack                        *m_undoStack = nullptr; ///< Z.5.5 — cell-edit undo

    // Per-row cache: name → row-data map.  Invalidated by reload().
    // The cache is mutable so const data() can populate it on first
    // access without hammering identifyByName() on every paint.
    mutable QVector<QVariantMap> m_rowCache;
    mutable QVector<bool>        m_rowCacheValid;
};

#endif // SWMMATTRIBUTETABLEMODEL_H
