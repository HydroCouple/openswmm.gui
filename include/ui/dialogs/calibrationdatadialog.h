/**
 * @file      calibrationdatadialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BK — Calibration data registration dialog.
 *
 * Maintains a table of (object, attribute, observed-CSV path) bindings.
 * Persisted in default-application QSettings under the shared
 * "project/calibrationData" group. When the user opens the Comparison Plot Dialog
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
    /**
     * @param[in] projectWindow Optional borrowed guarded project; live nonnull pointer only gates
     *   saving, storage key is not scoped by its identity.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Build staged calibration binding table and append entries from default-application
     *   QSettings.
     * @note No project-file read, CSV open, engine/provider lookup or live synchronization. Null
     *   project still loads same global entries but acceptance does not save. GUI-thread controls
     *   only.
     */
    explicit CalibrationDataDialog(SWMMVisProjectWindow *projectWindow, QWidget *parent = nullptr);
    /**
     * @brief Destroy owned staged controls without saving unaccepted edits or closing
     *   project/results sources.
     */
    ~CalibrationDataDialog() override;

    /*! \brief Static helper to read the calibration entries persisted in
     *  the shared default-application settings group.
     * @param[in] pw Ignored borrowed project parameter (implementation unnamed); null is permitted
     *   and reads same application-wide entries.
     * @return Owned entries from default QSettings project/calibrationData/entries array in stored
     *   order, including blank/duplicate/malformed rows. Not loaded from an .oswp file or
     *   project-specific group.
     * @note Attribute inferred by case-sensitive English substring priority Depth, Head, Flow,
     *   Velocity, Runoff, otherwise Unknown; ignores object kind when mapping. Stored translated
     *   labels may not round-trip outside matching English text. No CSV column/path/object/unit
     *   validation, relative-path resolution or source writes.
 */
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
     *   observed columns. No direct engine/model dirty flag or file save; null/destroyed project
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
    /**
     * @brief Append globally stored calibration entries to existing table (does not clear),
     *   creating owned row combos/items.
     * @note Kind matched to current translated labels, unmatched remains Node; attribute label
     *   matched to fixed current choices, unmatched remains Depth. No case/unit conversion,
     *   project-identity scoping, source file access or live refresh. Repeated calls duplicate
     *   rows.
     */
    void loadFromProject();
    /**
     * @brief When guarded project lives, erase default QSettings project/calibrationData group and
     *   replace entries array with staged text.
     * @note This is APPLICATION-wide storage despite helper name; all projects share it. Persist
     *   raw translated kind/attribute labels, raw name/path/column strings with no trim/validation.
     *   Rows missing kind/attribute widgets skipped using original row indices, potentially leaving
     *   array holes. No sync/error check, .oswp write, undo or project dirty notification.
     */
    void saveToProject();

    QPointer<SWMMVisProjectWindow> m_projectWindow;
    QTableWidget *m_table = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_CALIBRATIONDATADIALOG_H
