/**
 * @file      timeseriestablemodel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.3.4 — QAbstractTableModel over N TimeseriesProvider
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PANELS_TIMESERIESTABLEMODEL_H
#define OPENSWMMVIS_UI_PANELS_TIMESERIESTABLEMODEL_H

#include <QAbstractTableModel>
#include <QPointer>
#include <QVector>

class QUndoStack;

namespace openswmmvis::timeseries
{
class TimeseriesProvider;
}

namespace openswmmvis::ui
{

class TimeseriesTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum class LayoutMode {
        SharedGrid, ///< All providers share a common time vector.
        Divergent   ///< Falls back to single-provider view.
    };
    Q_ENUM(LayoutMode)

    /**
     * @param[in] parent Optional QObject owner, caller owns parentless model.
     * @brief Create unbound GUI-thread model, without loading files or mutating engine/providers.
     * @note Qt model notifications and all bound-provider operations must be serialized in owner
     *   thread; no locking. Binding and editor/undo ownership configured separately.
     */
    explicit TimeseriesTableModel(QObject *parent = nullptr);
    /**
     * @brief Destroy model-owned connections/cache; borrowed providers/registry and undo stack
     *   remain externally owned. Explicitly disconnect surviving providers first.
     */
    ~TimeseriesTableModel() override;

    /*! \brief Bind to a set of sibling providers. Empty list clears.
     *  Auto-detects layout mode from current grids.
     * @param[in] providers Copied/moved container of borrowed pointers, stored as QPointers;
     *   null/duplicate entries allowed but duplicate connects can repeat notifications.
     * @note ALWAYS full reset, disconnect all old provider→this hooks, replace/connect and detect
     *   exact current time-grid layout. Keeps optional raw undo stack. No source-mode conversion,
     *   provider ownership or destroyed→layout reset hook.
     */
    void setProviders(QVector<openswmmvis::timeseries::TimeseriesProvider *> providers);

    /*! \brief Currently-bound providers. In Divergent mode this still
     *  contains every passed provider; only `provider(0)` is editable. Any
     *  provider deleted out from under the model appears as nullptr.
     * @return New pointer vector in original supplied order, deleted providers null. Retains every
     *   provider even Divergent layout; no ownership/lifetime pin.
     */
    QVector<openswmmvis::timeseries::TimeseriesProvider *> providers() const;

    /**
     * @return Last detected grid classification, potentially stale after point-time edits or
     *   provider destruction (those paths do not redetect). No live grid scan.
     */
    LayoutMode layoutMode() const noexcept { return m_layout; }

    /*! \brief Set the undo stack used for cell mutations / header renames.
     *  Pass nullptr to apply changes directly.
     * @param[in] stack Optional borrowed RAW stack; must outlive all editing calls while bound,
     *   null direct edits.
     * @note Assignment only, no model reset/notification, ownership or deletion guard. Existing
     *   commands unchanged.
     */
    void setUndoStack(QUndoStack *stack) { m_undoStack = stack; }
    /**
     * @return Borrowed raw currently assigned stack, possibly null; no liveness check.
     */
    QUndoStack *undoStack() const noexcept { return m_undoStack; }

    // ── QAbstractTableModel ────────────────────────────────────────────────

    /**
     * @param[in] parent Valid parent yields 0.
     * @return First provider current point count,0if no providers/first deleted, in BOTH layouts.
     *   Does not verify sibling alignment.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] parent Valid parent yields 0.
     * @return 0 for empty binding, 2 for Divergent, otherwise 1+bound slot count including null
     *   slots. No live layout redetection.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] index Expected valid in-range root cell; column 0time, others value.
     * @param[in] role Only Display/Edit supported.
     * @return Time column uses first provider formatted SWMM MM/dd/yyyy HH:mm for Display and FULL
     *   QDateTime for Edit (seconds retained); values raw provider double with no units/conversion.
     *   Bad/missing row/provider invalid. Divergent any nonzero column maps provider 0 without
     *   strict column bound, so callers use declared schema.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @param[in] index Expected current schema/valid row. Shared time edits skip invalid rows;
     *   Divergent time path reads pointAt without row guard, so stale row unsafe.
     * @param[in] value Time column converts valid QDateTime; value columns convertible double,
     *   provider performs semantic validation.
     * @param[in] role Only EditRole accepted.
     * @return False for model/conversion/source-mode checks. True once commands/direct setters
     *   attempted, even if provider rejects, all shared rows skipped, or values unchanged. No
     *   confirmed-success/atomicity guarantee.
     * @note ANY external provider makes whole grid read-only. Shared time edit loops all live
     *   providers and groups MovePointCommands in macro if stack exists; NO preflight neighbour
     *   validation despite old source comment, later rejection can leave partial alignment.
     *   Divergent time edits provider 0 only. Values edit mapped provider via
     *   SetPointValueCommand/direct setter. Provider owns monotonic/finite checks and rejection
     *   signal; result ignored here, no engine flush.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @param[in] section Time 0, value >= 1; Shared bounds checked, Divergent assumes nonempty
     *   provider vector and maps ANY nonzero section to first.
     * @param[in] orientation Vertical 1 based integer, horizontal title/name.
     * @param[in] role Display/Edit supported.
     * @return Translated Time or provider name, invalid when missing/out-of-range shared provider.
     *   Divergent empty vector violates first() precondition, host must respect current
     *   columnCount. No rename or live grid detection.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    /**
     * @param[in] section Positive value-column index, Shared bounds checked; Divergent any positive
     *   maps provider 0.
     * @param[in] orientation Only Horizontal accepted.
     * @param[in] value toString name, empty rejected WITHOUT trimming; whitespace-only passed to
     *   provider/command.
     * @param[in] role Only EditRole accepted.
     * @return True after rename command/direct setName attempt, not success confirmation; false for
     *   checks/provider absence. Does not go through registry uniqueness validation and
     *   external-file mode DOES NOT block header rename. Undo stack optional, header refresh relies
     *   on provider metadata signals.
     */
    bool setHeaderData(int section, Qt::Orientation orientation, const QVariant &value,
                       int role = Qt::EditRole) override;
    /**
     * @param[in] index Any supplied Qt index, passed to base without local validity/range guard.
     * @return Base flags plus Editable whenever no external provider, even invalid/unbound index.
     *   Source-mode capability only, no mutation acceptance/liveness guarantee.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private slots:
    /**
     * @param[in] firstIndex Provider-reported first changed row, no clamping.
     * @param[in] count Provider-reported changed count, nonpositive not explicitly ignored.
     * @note Resolve sender first slot, emit Display/Edit dataChanged for VALUE column only
     *   (Divergent column 1 even for hidden siblings). Does NOT notify time column or redetect
     *   layout when a timestamp moved; existing consumers may require explicit reset. Unknown
     *   sender no-op.
     */
    void onProviderPointsChanged_(int firstIndex, int count);
    /**
     * @param[in] at Reported start row, used for debug log only.
     * @param[in] count Reported count, used for debug log only.
     * @note Always reset whole model and redetect grids after provider insertion, with optional
     *   timing logs. No own row mutation, sender validation, incremental row notification or
     *   selection preservation.
     */
    void onProviderPointsInserted_(int at, int count);
    /**
     * @param[in] at Reported start row, used for debug log only.
     * @param[in] count Reported count, used for debug log only.
     * @note Always reset whole model and redetect grids after provider removal, with optional
     *   timing logs. No own row mutation, sender validation, incremental row notification or
     *   selection preservation.
     */
    void onProviderPointsRemoved_(int at, int count);
    /**
     * @note Known sender emits headerDataChanged for its Shared value column or Divergent column 1.
     *   No layout detection/reset/name validation; unknown sender no-op.
     */
    void onProviderMetadataChanged_();
    /**
     * @note If nonzero rows/columns, emit dataChanged over whole table with empty role list so
     *   views reevaluate read-only appearance. Does not reset/re-detect/convert external source,
     *   clear selection or explicitly notify flags via separate API.
     */
    void onProviderSourceModeChanged_();

private:
    QVector<QPointer<openswmmvis::timeseries::TimeseriesProvider>> m_providers;
    LayoutMode m_layout = LayoutMode::SharedGrid;
    QUndoStack *m_undoStack = nullptr;

    /*! \brief Walks providers; returns true iff all share the same time grid.
     * @brief Store SharedGrid by default, switch Divergent on later missing provider, differing
     *   count or ANY unequal QDateTime.
     * @note < 2 slots or first null returns SharedGrid. Values ignored, timestamp comparison uses
     *   QDateTime equality, no interpolation/time normalization. No notifications; caller reset
     *   scopes own them. Updates state only, O(providers×points) worst-case.
     */
    void detectLayout_();

    /*! \brief Connect / disconnect the provider's signal set to our slots.
     * @param[in] p Required live borrowed provider for connect; disconnect accepts null per Qt
     *   semantics but internal callers use live.
     * @note Connect points-change/insert/remove,metadata,source-mode signals to this model, no
     *   unique-connection guard. No value reads/ownership transfer or destroyed reset connection.
     */
    void connectProvider_(openswmmvis::timeseries::TimeseriesProvider *p);
    /**
     * @param[in] p Required live borrowed provider for connect; disconnect accepts null per Qt
     *   semantics but internal callers use live.
     * @note Disconnect EVERY signal from provider to this model, including externally installed
     *   hooks. No value reads/ownership transfer or destroyed reset connection.
     */
    void disconnectProvider_(openswmmvis::timeseries::TimeseriesProvider *p);

    /*! \brief Returns index of \a provider in m_providers, or -1.
     * @param[in] p Pointer identity, null can match a cleared/null slot.
     * @return First matching binding index or -1. No ownership/liveness check or dereference.
     */
    int providerIndex_(const openswmmvis::timeseries::TimeseriesProvider *p) const;

    /*! \brief True when at least one bound provider is in ExternalFile mode.
     * @return True if ANY surviving bound provider uses ExternalFile, including hidden Divergent
     *   siblings; null slots ignored. Does not read source file or mutate layout.
     */
    bool anyExternal_() const;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_TIMESERIESTABLEMODEL_H
