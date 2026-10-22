/**
 * @file      tabulardatatablemodel.h
 * @brief     QAbstractTableModel adapter over a TabularDataLayer so the existing Attribute Table
 *            view can display loaded CSV / TSV data alongside SWMM categories.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice Z.4.3 — QAbstractTableModel adapter over a TabularDataLayer
 * so the existing Attribute Table view can display loaded CSV / TSV
 * data alongside SWMM categories.
 *
 * Read-only by design — tabular layers are observation data; engine
 * setters don't apply.  Selection ops + cross-view selection are
 * no-ops when this model is active (the layer has no SWMM object
 * refs).  The Z.2 query bar still works because the proxy's
 * predicate evaluator runs against any QAbstractTableModel rows.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef TABULARDATATABLEMODEL_H
#define TABULARDATATABLEMODEL_H

#include <QAbstractTableModel>
#include <QPointer>
#include <QStringList>

class TabularDataLayer;

class TabularDataTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QObject owner, caller owns parentless model.
     * @brief Create unbound GUI-thread model, without loading files or mutating engine/providers.
     * @note Qt model notifications and all bound-provider operations must be serialized in owner
     *   thread; no locking. Binding and editor/undo ownership configured separately.
     */
    explicit TabularDataTableModel(QObject *parent = nullptr);

    /*! Bind to a tabular layer.  Pass nullptr to clear.  The model
     *  emits `modelReset` so any attached view rebuilds its layout.
     * @param[in] layer Optional borrowed tabular layer guarded by QPointer, null clears.
     * @note Equality no-op; replace dataLoaded connection and reload with full model reset. No
     *   parsing/file loading or ownership transfer. Layer destruction has no reset hook; cached
     *   row/header counts can remain until explicit reload, while data returns invalid.
     */
    void setLayer(TabularDataLayer *layer);

    /**
     * @return Borrowed guarded layer or null, no lifetime pin/ownership.
     */
    [[nodiscard]] TabularDataLayer *layer() const noexcept { return m_layer; }

    /*! Convenience — returns all cached column headers.  Lets the proxy's query evaluator
     *  build a key→value map per row.
     * @return Copy of ALL cached header strings in column order, not a single-column lookup; no
     *   parameter required. Cache refreshed only by reload/binding.
     */
    [[nodiscard]] QStringList columnHeaders() const { return m_headers; }

    // QAbstractTableModel ----------------------------------------------------
    /**
     * @param[in] parent Valid parent yields 0for flat model, invalid root exposes cache.
     * @return cached row count, possibly stale after layer destruction until reload. No query to
     *   live layer.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] parent Valid parent yields 0for flat model, invalid root exposes cache.
     * @return cached header count, possibly stale after layer destruction until reload. No query to
     *   live layer.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] index Expected current model cell, checks cached row/header bounds and live layer.
     * @param[in] role Display/Edit/ToolTip returns layer row value; custom attribute value/sort
     *   roles wrap recursive DisplayRole result.
     * @return QVariant from live row keyed by cached header, invalid on absent layer/bad
     *   cell/unsupported standard role. Custom roles return AttributeValue wrapper even for
     *   missing/invalid cell. Read-only: EditRole data does not make setData available. No
     *   parsing/conversion of physical units.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @param[in] section Horizontal bounds checked; vertical section+1returned without range check.
     * @param[in] orientation Horizontal cached column name or vertical row number.
     * @param[in] role Only DisplayRole supported.
     * @return Cached string/vertical integer or invalid, no layer lookup/mutation.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

public slots:
    /*! Refresh from the bound layer.  Hooked to `dataLoaded()` so
     *  reloads pick up automatically.
     * @brief Reset row/header cache from currently bound layer or clear when absent.
     * @note Always begin/endResetModel even unchanged. Does not reload source file, copy row data,
     *   change selection or emit per-cell updates.
     */
    void reload();

private:
    QPointer<TabularDataLayer> m_layer;
    QStringList m_headers;
    int m_rowCount = 0;
};

#endif // TABULARDATATABLEMODEL_H
