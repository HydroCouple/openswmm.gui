/*!
 * \file   customreportdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BH — Custom report query builder.
 *
 * A small DSL surfaced as a tree-of-clauses lets the user assemble
 * custom reports against the .out file. Each row is a query that
 * declares { kind, object filter, variable, aggregate, period filter }.
 * The dialog evaluates all clauses, renders results as a table, and
 * supports CSV / Markdown export.
 *
 * Aggregates: max, min, mean, sum, peak-time, time-at-threshold.
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

namespace openswmmvis::ui {

class CustomReportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CustomReportDialog(SWMMResultsLayer *layer,
                                 QWidget *parent = nullptr);
    ~CustomReportDialog() override;

private slots:
    void onAddClauseClicked();
    void onRemoveClauseClicked();
    void onRunClicked();
    void onExportClicked();

private:
    void buildUi();
    void evaluate();

    QPointer<SWMMResultsLayer> m_layer;

    QTableWidget       *m_clauseTable = nullptr;
    QStandardItemModel *m_resultModel = nullptr;
    QTableView         *m_resultTable = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_CUSTOMREPORTDIALOG_H
