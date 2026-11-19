/**
 * @file      customreportdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BH — Custom report query builder.
 *
 * A table of clauses lets the user assemble
 * custom reports against the .out file. Each row is a query that
 * declares { label, kind, object filter, variable, aggregate, threshold }.
 * The dialog evaluates all clauses, renders results as a table, and
 * supports CSV export of the current result table.
 *
 * Aggregates: max, min, mean, sum, peak-time, time-at-threshold.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_CUSTOMREPORTDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_CUSTOMREPORTDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QString>
#include <QVector>

class QTableWidget;
class QTableView;
class QStandardItemModel;
class SWMMResultsLayer;

namespace openswmmvis::ui
{

class CustomReportDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] layer Optional borrowed guarded results layer; must have valid readable output for
     *   evaluation, no ownership transfer.
     * @param[in] parent Optional QWidget owner, caller manages parentless dialog.
     * @brief Build local clause/result tables with one default clause, without reading series or
     *   evaluating automatically.
     * @note GUI-thread synchronous report scans, no source-change subscription, progress/cancel,
     *   engine mutation or project/settings persistence. Deleting source nulls guard, existing
     *   displayed results remain.
     */
    explicit CustomReportDialog(SWMMResultsLayer *layer, QWidget *parent = nullptr);
    /**
     * @brief Destroy owned clause/result models/widgets without closing borrowed output, exporting
     *   or persisting draft.
     */
    ~CustomReportDialog() override;

private slots:
    /**
     * @brief Append default clauseN, Nodes, wildcard *, Depth, max and threshold 0.0 controls.
     * @note All variable labels remain available for all object kinds; evaluator maps
     *   unsupported/aliased selections to specific fallback engine codes rather than rejecting. No
     *   evaluation or source write.
     */
    void onAddClauseClicked();
    /**
     * @brief Delete current clause row if any; retain previously evaluated result table unchanged
     *   until next Run. No confirmation/source edits.
     */
    void onRemoveClauseClicked();
    /**
     * @brief Evaluate all current clauses synchronously against borrowed output, replacing result
     *   rows when handle exists. No async worker or model mutation.
     */
    void onRunClicked();
    /**
     * @brief Choose CSV path and overwrite with CURRENT cached result headers/cells, including any
     *   user edits/sort order.
     * @note No reevaluation, units conversion, quoting/escaping of commas/quotes/newlines, Markdown
     *   option or atomic replace. Cancellation/open failure silently no-op; stream/write errors not
     *   checked. Standard QTextStream text encoding/formatting applies and file closes on return.
     */
    void onExportClicked();

private:
    /**
     * @brief Create owned clause controls and sortable default-editable result table, export/close
     *   actions and seed one clause.
     * @note No period-filter or report-template persistence controls. Close accepts regardless of
     *   evaluation state; table edits remain local and exports use current data. Does not query
     *   output or run report.
     */
    void buildUi();
    /**
     * @brief Scan every available output period per matched object and clause, appending aggregate
     *   rows synchronously.
     * @note Null/deleted layer or missing output handle leaves previous results unchanged;
     *   otherwise clear/reset result model first. Require valid nonnegative period count/output
     *   metadata; no local guard before vector allocation. Failed object-series reads are silently
     *   skipped, with no missing-data/NaN filtering.
     * @note Trimmed exact wildcard * OR empty comma-split list means all; otherwise comma-separated
     *   case-insensitive exact object IDs, each token UNTRIMMED (spaces matter), no wildcard/regex
     *   expansion. Threshold parsed via QString::toDouble without ok, invalid becomes zero. Native
     *   output-file units retained for values and threshold, no display-unit conversion.
     * @note Node picker Depth/Head/Flow/Velocity/Runoff map to
     *   depth/head/total-inflow/overflow/lateral-inflow; remaining choices fall back depth. Link
     *   Head aliases Depth, Runoff maps capacity, remaining extra choices fall back Flow. Subcatch
     *   Runoff/GW flow/GW elevation/soil moisture use those codes; all other choices map Runoff.
     *   Variable picker CURRENT INDEX drives mapping, not its item data.
     * @note Max/min/mean/sum preserve native series units (sum is sample sum, not time
     *   integration). Peak time is first maximum index times report step in HOURS from first report
     *   sample, not absolute simulation clock. Time above threshold counts strictly greater samples
     *   times report step in hours. Numeric Value stored in EditRole for numeric sorting; Qt item
     *   display conversion determines exported representation. No engine writes/undo or independent
     *   result provenance/units column.
     */
    void evaluate();

    QPointer<SWMMResultsLayer> m_layer;

    QTableWidget *m_clauseTable = nullptr;
    QStandardItemModel *m_resultModel = nullptr;
    QTableView *m_resultTable = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_CUSTOMREPORTDIALOG_H
