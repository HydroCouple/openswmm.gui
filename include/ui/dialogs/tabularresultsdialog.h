/**
 * @file      tabularresultsdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BF — by-object / by-variable tabular results panel.
 *
 * Two view modes:
 *   - **By object** — pick an object kind (Node / Link / Subcatch) + a
 *     variable; one row per object, one column per period.
 *   - **By variable** — pick an object; one row per period, one column
 *     per variable in that object's class.
 *
 * CSV / TSV export from the menu. Sortable on every column.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_TABULARRESULTSDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_TABULARRESULTSDIALOG_H

#include <QDialog>
#include <QPointer>

class QTableView;
class QStandardItemModel;
class QComboBox;
class QRadioButton;
class SWMMResultsLayer;

namespace openswmmvis::ui
{

class TabularResultsDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] layer Optional borrowed guarded results source; valid readable output required to
     *   fill table, no ownership transfer.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Build by-object defaults and immediately fill selectors/table from current output.
     * @note GUI-thread synchronous reads, no live run-change subscription/progress/cancel or engine
     *   mutation. Deleting/closing output later leaves prior cached table until successful rebuild.
     *   Table items are ordinary editable local QStandardItems; editing does not change output
     *   file.
     */
    explicit TabularResultsDialog(SWMMResultsLayer *layer, QWidget *parent = nullptr);
    /**
     * @brief Destroy owned controls/model without closing borrowed output, exporting cache or
     *   persisting preferences.
     */
    ~TabularResultsDialog() override;

private slots:
    /**
     * @brief For live output, replace Variable/Object selector with current kind fixed variables or
     *   current object IDs, then rebuild selected mode.
     * @note Reset choice to first item; temporarily block selector signals then unconditionally
     *   unblock rather than restore previous blocked state. Null source/handle leaves prior
     *   selector/table unchanged. Kind-combo changes call onSelectionChanged rather than this, so
     *   do not automatically repopulate its choice list. No source mutation or actual-time
     *   conversion.
     */
    void onModeChanged();
    /**
     * @brief Rebuild currently selected table mode using CURRENT kind and existing selector item
     *   data.
     * @note Does not repopulate variable/object choices when kind changes, so old kind codes/object
     *   indices can be interpreted for new kind until mode repopulation. No
     *   validation/normalization or source write.
     */
    void onSelectionChanged();
    /**
     * @brief Choose CSV destination and export current table with comma separator; cancellation
     *   no-op. No fresh output read, protected-source check or explicit overwrite transaction in
     *   dialog.
     */
    void onExportCsvClicked();
    /**
     * @brief Choose TSV destination and export current table with tab separator; cancellation
     *   no-op. No reevaluation or source modification beyond requested destination file write.
     */
    void onExportTsvClicked();

private:
    /**
     * @brief Create owned by-object/by-variable controls, three-kind selector, sortable locally
     *   editable result model and export/close actions.
     * @note Does not populate variable/object choices or read output here. Both radio toggles
     *   invoke mode handler; kind/secondary choice only rebuild current table. Close accepts
     *   without persisting edits to source.
     */
    void buildUi();
    /**
     * @brief Replace table with one row per current kind object, Object column plus zero-based
     *   report-period columns, using current variable code.
     * @note Null source/handle leaves old table. Require nonnegative valid period count before
     *   vector allocation; all periods read synchronously. Failed object series still adds ID row
     *   with blank/missing numeric cells, no error message. Raw output-file units/no display
     *   conversion, timestamps, species catalog or live animation lookup. Numeric EditRole stores
     *   full float→double values for sorting; initial three-decimal text is not a precision
     *   guarantee once Qt item roles share data.
     */
    void rebuildByObject();
    /**
     * @brief Replace table with one row per period and fixed variables for current kind at selected
     *   stored object index.
     * @note Null source/handle, periods<=0 or negative selected data returns leaving prior table;
     *   invalid combo data converts to zero. No current-name/index revalidation. Failed
     *   variable-series read fills ENTIRE column with zero, indistinguishable from recorded zero;
     *   no missing status/error report.
     * @note Node six/link five/subcatch eight fixed variables, no pollutant/reaction species.
     *   Periods are zero-based indices, numeric raw output units retained without
     *   conversion/labels. Standard item numeric roles support sorting/local editing; no source
     *   writes.
     */
    void rebuildByVariable();
    /**
     * @param[in] path Raw destination opened WriteOnly/Text, truncating existing file; no
     *   aliases/protected-path/safe-stage validation.
     * @param[in] delim Raw delimiter inserted between cells, normally comma or tab.
     * @brief Write current headers and DisplayRole cell strings in current model/sort order,
     *   including local user edits.
     * @note Open failure silently returns; quote/delimiter/newline escaping, source reevaluation,
     *   unit conversion and write-status checking absent. Standard QTextStream encoding/formatting
     *   applies; closes file on return. No engine/output source changes except if caller chooses an
     *   alias destination.
     */
    void exportDelimited(const QString &path, QChar delim);

    QPointer<SWMMResultsLayer> m_layer;

    QRadioButton *m_byObjectRadio = nullptr;
    QRadioButton *m_byVariableRadio = nullptr;
    QComboBox *m_kindCombo = nullptr;     // by-object: which kind
    QComboBox *m_varOrObjCombo = nullptr; // by-object: variable | by-variable: object name
    QTableView *m_table = nullptr;
    QStandardItemModel *m_model = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_TABULARRESULTSDIALOG_H
