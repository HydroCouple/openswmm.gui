/*!
 * \file   tabulardatatablemodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
    explicit TabularDataTableModel(QObject *parent = nullptr);

    /*! Bind to a tabular layer.  Pass nullptr to clear.  The model
     *  emits `modelReset` so any attached view rebuilds its layout. */
    void setLayer(TabularDataLayer *layer);

    [[nodiscard]] TabularDataLayer *layer() const noexcept { return m_layer; }

    /*! Convenience — returns the column header at @p col, or empty
     *  string when out of range.  Lets the proxy's query evaluator
     *  build a key→value map per row. */
    [[nodiscard]] QStringList columnHeaders() const { return m_headers; }

    // QAbstractTableModel ----------------------------------------------------
    int      rowCount(const QModelIndex &parent = {}) const override;
    int      columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

public slots:
    /*! Refresh from the bound layer.  Hooked to `dataLoaded()` so
     *  reloads pick up automatically. */
    void reload();

private:
    QPointer<TabularDataLayer> m_layer;
    QStringList                m_headers;
    int                        m_rowCount = 0;
};

#endif // TABULARDATATABLEMODEL_H
