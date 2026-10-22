/**
 * @file      calibrationdatadialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BK — Calibration data registration dialog.
 *
 * Maintains a table of (object, attribute, observed-CSV path) bindings.
 * Persisted with its owning project using stable kind and attribute identifiers. When the user opens the Comparison Plot Dialog
 * for a calibrated object, the matching observed CSV is automatically
 * added as an Observed RunSource via BL-Polish.2's ObservedCsvRunLayer.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_CALIBRATIONDATADIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_CALIBRATIONDATADIALOG_H

#include "plot/plotattribute.h"

#include <QDialog>
#include <QPointer>
#include <QString>

class QTableWidget;
class SWMMVisProjectWindow;

namespace openswmmvis::ui
{

struct CalibrationEntry {
    QString objectName;
    QString objectKind; // "Node" / "Link" / "Subcatch"
    openswmmvis::plot::PlotAttribute attribute = openswmmvis::plot::PlotAttribute::Unknown;
    QString observedPath;
    QString observedColumn; // column name in the CSV
};

class CalibrationDataDialog : public QDialog
{
    Q_OBJECT
public:
    /** @brief Build a staged table from the owning project's calibration bindings.
     * @param projectWindow Optional guarded owner; null opens an empty unsaved table.
     * @param parent Optional widget owner.
     * CSV files are not opened until a comparison requests them. */
    explicit CalibrationDataDialog(SWMMVisProjectWindow *projectWindow, QWidget *parent = nullptr);
    /**
     * @brief Destroy owned staged controls without saving unaccepted edits or closing
     *   project/results sources.
     */
    ~CalibrationDataDialog() override;

    /** @brief Read the owning project's bindings; null returns an empty list.
     * Stable identifiers are independent of translated labels. Depth resolves
     * according to object kind. Paths have already been restored by ProjectSerializer. */
    static QVector<CalibrationEntry> entriesFromProject(SWMMVisProjectWindow *pw);

private slots:
    /**
     * @brief Append blank staged binding with Node/Depth first choices and observed column col_1.
     * @note Kind and attribute combos are independent; incompatible combinations remain allowed. No
     *   engine, CSV read or persistence.
     */
    void onAddClicked();
    /**
     * @brief Remove current staged row when one exists; no confirmation or immediate settings/model
     *   write.
     */
    void onRemoveClicked();
    /**
     * @brief For current row, choose observed CSV/TSV/TSF/DAT via timeseries chooser and copy
     *   nonempty path into its staged file cell.
     * @note Missing current/cancel no-op. Does not inspect headers, infer units/column, normalize
     *   relative path or validate parseability. Current row index captured across modal chooser;
     *   host must preserve table/dialog.
     */
    void onBrowseClicked();
    /**
     * @brief Save current staged rows when project guard lives, then accept regardless of storage
     *   availability/errors.
     * @note No validation of blank identities, path, attribute-kind compatibility, duplicates or
     *   observed columns. Marks a changed project dirty without saving a file; null/destroyed project
     *   still accepts without persistence.
     */
    void accept() override;

private:
    /**
     * @brief Create owned five-column staged table with Add/Remove/Browse and OK/Cancel actions.
     * @note Does not load entries or validate bindings; accepted button calls virtual accept to
     *   persist, Cancel discards table edits. No observed data import.
     */
    void buildUi();
    /** @brief Append the owning project's bindings to the staged table. */
    void loadFromProject();
    /** @brief Replace the owning project's bindings with stable staged values.
     * Marks the project dirty only when bindings change; no immediate disk write. */
    void saveToProject();

    QPointer<SWMMVisProjectWindow> m_projectWindow;
    QTableWidget *m_table = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_CALIBRATIONDATADIALOG_H
