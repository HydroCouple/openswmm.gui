/*!
 * \file   tabularresultsdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BF — by-object / by-variable tabular results panel.
 *
 * Two view modes:
 *   - **By object** — pick an object kind (Node / Link / Subcatch) + a
 *     variable; one row per object, one column per period.
 *   - **By variable** — pick an object; one row per period, one column
 *     per variable in that object's class.
 *
 * CSV / TSV export from the menu. Sortable on every column.
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

namespace openswmmvis::ui {

class TabularResultsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TabularResultsDialog(SWMMResultsLayer *layer,
                                   QWidget *parent = nullptr);
    ~TabularResultsDialog() override;

private slots:
    void onModeChanged();
    void onSelectionChanged();
    void onExportCsvClicked();
    void onExportTsvClicked();

private:
    void buildUi();
    void rebuildByObject();
    void rebuildByVariable();
    void exportDelimited(const QString &path, QChar delim);

    QPointer<SWMMResultsLayer> m_layer;

    QRadioButton       *m_byObjectRadio   = nullptr;
    QRadioButton       *m_byVariableRadio = nullptr;
    QComboBox          *m_kindCombo       = nullptr;   // by-object: which kind
    QComboBox          *m_varOrObjCombo   = nullptr;   // by-object: variable | by-variable: object name
    QTableView         *m_table           = nullptr;
    QStandardItemModel *m_model           = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_TABULARRESULTSDIALOG_H
