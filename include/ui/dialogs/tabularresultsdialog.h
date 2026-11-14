/**
 * @file      tabularresultsdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BF / V6 — by-object / by-variable tabular results panel.
 *
 * Two view modes:
 *   - **By object** — pick an object kind + a variable; one row per object,
 *     one column per report time.
 *   - **By variable** — pick an object; one row per report time, one column
 *     per variable of that kind.
 *
 * V6 (workplans/GW2D_AND_PIPE_REACTION_RESULTS_VISUALIZATION_PLAN_2026-10-14.md
 * D-V6): every value comes from `ResultDescriptor` + `IRunLayer` series —
 * `SwmmOutRunLayer` for the `.out` (fixed attributes, every catalog species,
 * R4 pollutant loads and cumulative loads) and `Mesh2DRunLayer` for the 2D
 * results (fixed cell attributes and every catalog variable). Kinds: Nodes,
 * Links, Subcatchments, System, "2D cells" (the cells selected on the map,
 * the same cells and values the map's selected-cell table reads) and "2D
 * regions" (the selection, each cell tag and the whole domain, aggregated by
 * Sum / Mean / Area-weighted mean / Mass). Missing values are blank, never 0,
 * with a Status column; CSV / TSV export carries a Units row and the status.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_TABULARRESULTSDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_TABULARRESULTSDIALOG_H

#include "plot/irunlayer.h"
#include "results/meshregionaggregate.h"
#include "results/resulttable.h"
#include "results/runspeciescatalog.h"

#include <QDialog>
#include <QPointer>

#include <memory>

class QTableView;
class QStandardItemModel;
class QComboBox;
class QLabel;
class QRadioButton;
class SWMMResultsLayer;
class SWMM2DResultsLayer;

namespace openswmmvis::plot
{
class SwmmOutRunLayer;
class Mesh2DRunLayer;
} // namespace openswmmvis::plot

namespace openswmmvis::ui
{

class TabularResultsDialog : public QDialog
{
    Q_OBJECT
public:
    /*! Object kinds offered (combo item data). */
    enum Kind { Nodes = 0, Links = 1, Subcatchments = 2, System = 3, Cells2D = 4, Regions2D = 5 };

    /**
     * @param[in] layer Optional borrowed guarded 1D results; no ownership transfer.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Build by-object defaults and fill the table from the current output.
     * @note GUI-thread synchronous reads; no live-run subscription.
     */
    explicit TabularResultsDialog(SWMMResultsLayer *layer, QWidget *parent = nullptr);
    /**
     * @param[in] layer Optional borrowed 1D results (may be null).
     * @param[in] mesh Optional borrowed 2D results (may be null); enables the 2D kinds.
     * @param[in] parent Optional QWidget owner.
     */
    TabularResultsDialog(SWMMResultsLayer *layer, SWMM2DResultsLayer *mesh,
                         QWidget *parent = nullptr);
    /**
     * @brief Destroy owned controls/models without closing borrowed results.
     */
    ~TabularResultsDialog() override;

    /*! \brief Per-cell tags of the mesh (engine cell order) for the "2D
     *  regions" kind; rebuilds the table. Ignored when the count does not
     *  match the 2D results' cell count. */
    void setCellTags(const QStringList &tags);
    /** @return The table as last built (values, units, status). */
    const results::ResultTable &table() const noexcept { return m_table; }
    /*! \brief Write the current table (header, Units row, rows, Status).
     * @return False with \p error set when the file cannot be written. */
    bool exportTo(const QString &path, QChar delimiter, QString *error = nullptr) const;

private slots:
    /**
     * @brief Repopulate the Variable / Object selector for the current mode and kind, then
     *   rebuild the table.
     */
    void onModeChanged();
    /**
     * @brief Rebuild the table from the current selector choices.
     */
    void onSelectionChanged();
    /** @brief Choose a CSV destination and export the current table. */
    void onExportCsvClicked();
    /** @brief Choose a TSV destination and export the current table. */
    void onExportTsvClicked();

private:
    /*! One "object" of the by-variable selector or one by-object row. */
    struct Row {
        QString label;
        plot::ObjectRef ref;          ///< 1D object / 2D cell
        int region = -1;              ///< index into regions() for 2D regions
    };

    void buildUi();
    void rebuildByObject();
    void rebuildByVariable();
    /** @return Descriptors the current run serves for \p kind. */
    QVector<plot::ResultDescriptor> descriptorsFor(int kind) const;
    /** @return Rows (objects / cells / regions) of \p kind. */
    QVector<Row> rowsFor(int kind) const;
    /** @return Selection (when any), tag and domain regions of the 2D run. */
    QVector<results::MeshRegion> regions() const;
    /** @return Engine cell indices selected on the map (sorted, unique). */
    std::vector<int> selectedCells() const;
    /** @return Column header for \p d (catalog label for species). */
    QString headerFor(const plot::ResultDescriptor &d) const;
    /** @return Units for \p d on \p kind (aggregate units for 2D regions). */
    QString unitsFor(const plot::ResultDescriptor &d, int kind) const;
    /** @return Series of \p rows for \p d (regions aggregated). */
    QVector<plot::SeriesData> fetch(int kind, const QVector<Row> &rows,
                                    const plot::ResultDescriptor &d) const;
    results::MeshAggregate currentAggregate() const;
    void exportDelimited(const QString &path, QChar delim);

    QPointer<SWMMResultsLayer> m_layer;
    QPointer<SWMM2DResultsLayer> m_mesh;
    std::unique_ptr<plot::SwmmOutRunLayer> m_run1D;
    std::unique_ptr<plot::Mesh2DRunLayer> m_run2D;
    QStringList m_cellTags;
    results::ResultTable m_table;
    QVector<plot::ResultDescriptor> m_comboDescriptors; ///< by object: parallel to the combo
    QVector<Row> m_comboRows;                           ///< by variable: parallel to the combo

    QRadioButton *m_byObjectRadio = nullptr;
    QRadioButton *m_byVariableRadio = nullptr;
    QComboBox *m_kindCombo = nullptr;     // which kind
    QComboBox *m_varOrObjCombo = nullptr; // by-object: variable | by-variable: object
    QLabel *m_aggregateLabel = nullptr;
    QComboBox *m_aggregateCombo = nullptr; // 2D regions: how cells combine
    QTableView *m_tableView = nullptr;
    QStandardItemModel *m_model = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_TABULARRESULTSDIALOG_H
