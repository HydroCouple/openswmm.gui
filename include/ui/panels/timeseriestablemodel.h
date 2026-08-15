/*!
 * \file   timeseriestablemodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.3.4 — QAbstractTableModel over N TimeseriesProvider
 *         siblings; drives the editor dialog's left-pane grid.
 *
 * Layout modes (auto-selected from the bound providers' time grids):
 *
 *   - **Shared grid** (every provider's `points()` array shares the same
 *     count + same QDateTime at every index): wide table with
 *     `[Time | V0 | V1 | ... | V(N-1)]`. One row per shared time stamp;
 *     each value column edits a separate provider. This is the layout the
 *     user asked for (one CSV → multiple sibling Tseries).
 *
 *   - **Divergent grid** (any provider's time vector differs): the model
 *     binds to provider 0 only and exposes `[Time | Value]`. The dialog
 *     surfaces a status hint when this happens. A future sub-phase can
 *     extend with a union-of-times pivot.
 *
 * Mutations go through `QUndoCommand` subclasses pushed to the optional
 * undo stack (`setUndoStack`). When no stack is bound, mutations apply
 * directly to the provider — still subject to the strict-monotone-time
 * invariant; rejections surface via the provider's `mutationRejected`
 * signal.
 *
 * Read-only when **any** bound provider is in ExternalFile source mode
 * (per Phase 6.7.3.4 spec — "grid greys out and the user is prompted to
 * click Convert-to-Inline").
 */
#ifndef OPENSWMMVIS_UI_PANELS_TIMESERIESTABLEMODEL_H
#define OPENSWMMVIS_UI_PANELS_TIMESERIESTABLEMODEL_H

#include <QAbstractTableModel>
#include <QPointer>
#include <QVector>

class QUndoStack;

namespace openswmmvis::timeseries {
class TimeseriesProvider;
}

namespace openswmmvis::ui {

class TimeseriesTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum class LayoutMode {
        SharedGrid,     ///< All providers share a common time vector.
        Divergent       ///< Falls back to single-provider view.
    };
    Q_ENUM(LayoutMode)

    explicit TimeseriesTableModel(QObject *parent = nullptr);
    ~TimeseriesTableModel() override;

    /*! \brief Bind to a set of sibling providers. Empty list clears.
     *  Auto-detects layout mode from current grids. */
    void setProviders(QVector<openswmmvis::timeseries::TimeseriesProvider *> providers);

    /*! \brief Currently-bound providers. In Divergent mode this still
     *  contains every passed provider; only `provider(0)` is editable. Any
     *  provider deleted out from under the model appears as nullptr. */
    QVector<openswmmvis::timeseries::TimeseriesProvider *> providers() const;

    LayoutMode layoutMode() const noexcept { return m_layout; }

    /*! \brief Set the undo stack used for cell mutations / header renames.
     *  Pass nullptr to apply changes directly. */
    void setUndoStack(QUndoStack *stack) { m_undoStack = stack; }
    QUndoStack *undoStack() const noexcept { return m_undoStack; }

    // ── QAbstractTableModel ────────────────────────────────────────────────

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    bool setHeaderData(int section, Qt::Orientation orientation,
                       const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private slots:
    void onProviderPointsChanged_(int firstIndex, int count);
    void onProviderPointsInserted_(int at, int count);
    void onProviderPointsRemoved_(int at, int count);
    void onProviderMetadataChanged_();
    void onProviderSourceModeChanged_();

private:
    QVector<QPointer<openswmmvis::timeseries::TimeseriesProvider>>  m_providers;
    LayoutMode                                                       m_layout = LayoutMode::SharedGrid;
    QUndoStack                                                      *m_undoStack = nullptr;

    /*! \brief Walks providers; returns true iff all share the same time grid. */
    void detectLayout_();

    /*! \brief Connect / disconnect the provider's signal set to our slots. */
    void connectProvider_(openswmmvis::timeseries::TimeseriesProvider *p);
    void disconnectProvider_(openswmmvis::timeseries::TimeseriesProvider *p);

    /*! \brief Returns index of \a provider in m_providers, or -1. */
    int providerIndex_(const openswmmvis::timeseries::TimeseriesProvider *p) const;

    /*! \brief True when at least one bound provider is in ExternalFile mode. */
    bool anyExternal_() const;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_TIMESERIESTABLEMODEL_H
