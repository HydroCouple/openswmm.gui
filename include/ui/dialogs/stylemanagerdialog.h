/**
 * @file      stylemanagerdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Browse, apply, save, import, and export Rule Lists from a
 *         user-level style library (Slice Z.17c).
 *
 *         The library is a directory of `.swmm-rule.json` files; one
 *         file per saved Rule List. The dialog lists files in that
 *         directory, lets the user preview each one's contents,
 *         apply a selection to the active layer, save the active
 *         layer's current RuleList into the library under a new
 *         name, and import / export to arbitrary paths outside the
 *         library.
 *
 *         The library directory defaults to a writable per-user
 *         location (`QStandardPaths::AppLocalDataLocation` +
 *         "/styles"). The directory is created on first use.
 *
 *         "Active layer" is passed in by the caller. When null, the
 *         apply/save buttons are disabled but browse / preview /
 *         import / export still work — useful as a project-wide
 *         library editor when no layer is currently selected.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_STYLEMANAGERDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_STYLEMANAGERDIALOG_H

#include <QDialog>
#include <QString>
#include <QPointer>

class OpenSWMMVisLayer;
class QDialogButtonBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace openswmmvis::ui
{

class StyleManagerDialog : public QDialog
{
    Q_OBJECT
public:
    /*! Argument `activeLayer`:  Layer whose RuleList the Apply / Save Current
     *                      buttons target. May be nullptr — those buttons
     *                      then sit disabled.
     *  Argument `parent`:       Standard Qt parent. */
    /**
     * @brief Construct the GUI-thread style-library browser and resolve/create its per-user library
     * directory.
     * @param[in,out] activeLayer Nullable borrowed target layer; Apply/Save require its existing
     * rule list. The dialog tracks QObject destruction and never owns the layer.
     * @param[in] parent Optional Qt parent owning this dialog and its child controls.
     * Rescans native/QGIS style files and loads the selected preview synchronously. Close accepts
     * prior operations; the dialog has no global cancel/rollback transaction.
     */
    explicit StyleManagerDialog(OpenSWMMVisLayer *activeLayer, QWidget *parent = nullptr);
    /**
     * @brief Destroy dialog-owned controls/connections without deleting the target layer or undoing
     * filesystem/style operations.
     */
    ~StyleManagerDialog() override;

    /**
     * @brief Retarget Apply/Save and update selection-dependent enablement.
     * @param[in,out] layer Nullable borrowed target; connects destruction to clearing the target,
     * disconnecting the old target destruction route. Never transfers layer/rule-list ownership.
     */
    void setLayer(OpenSWMMVisLayer *layer);

private slots:
    /**
     * @brief Reload the selected file preview and gate Apply by selection/target rule list, and
     * Export/Delete by selection.
     */
    void onSelectionChanged();
    /**
     * @brief Load the selected style directly into the current layer rule list.
     * Missing target/list/selection is a no-op. Displays loader failure or skipped-rule
     * information; successful replacement triggers normal rule-list notifications and is not
     * wrapped in an undo command.
     */
    void onApply();
    /**
     * @brief Prompt for a library name and save the current layer rule list as native JSON.
     * Requires a live target/list, replaces path-special characters with underscores, confirms
     * overwriting and reports writer failure. Rescans after success; cancellation leaves the
     * library unchanged.
     */
    void onSaveCurrent();
    /**
     * @brief Prompt for a style file and copy its bytes into the library under the original
     * filename.
     * Does not parse/validate contents before copying. Confirmed overwrite removes the destination
     * before copy, so copy failure can leave the previous destination absent; shows failure and
     * rescans only after success.
     */
    void onImport();
    /**
     * @brief Prompt for a destination, load the selected entry and save through the destination
     * suffix dispatcher.
     * Converts native JSON/QGIS QML rather than copying bytes, displays load/save errors and saver
     * warnings, and cancels without work on an empty source/destination.
     */
    void onExport();
    /**
     * @brief Confirm permanent deletion of the selected library file, report removal failure and
     * rescan after success.
     * No selection or declined confirmation is a no-op; no undo record is created.
     */
    void onDelete();
    /**
     * @brief Request desktop opening of the resolved library folder when nonempty; ignores
     * desktop-service success/failure.
     */
    void onOpenLibraryFolder();

private:
    /*! Ensure the library directory exists and is writable.
     *  Returns the absolute path; empty on failure. */
    /**
     * @brief Resolve the per-user application-local styles directory and attempt to create it.
     * @return Base application-local data path plus /styles, or empty only when the base location
     * is empty. Directory creation/writability is not actually verified despite the historical
     * surrounding comment.
     */
    [[nodiscard]] QString resolveLibraryDir() const;

    /*! Rescan the library directory and rebuild the list. Preserves
     *  the previously-selected file when possible. */
    /**
     * @brief Rebuild the name-sorted native .swmm-rule.json/QGIS .qml file list while preserving
     * the selected full path when possible.
     * Selects the first entry otherwise; selection signals can synchronously refresh the preview.
     * An empty library root clears the list and returns.
     */
    void refreshLibrary();

    /*! Update the preview pane from the currently-selected list row. */
    /**
     * @brief Clear the preview and synchronously load the selected file into a temporary owned rule
     * list.
     * Shows a rule/warning summary on success or loader error text on failure; never modifies the
     * active layer.
     */
    void refreshPreview();

    /*! Convenience: full path to the .swmm-rule.json for the currently-
     *  selected row, or empty if no selection. */
    /**
     * @brief Read the selected entry full path from its item metadata.
     * @return Stored path or empty when list/selection is absent; no filesystem existence check.
     */
    [[nodiscard]] QString currentFilePath() const;

    QPointer<OpenSWMMVisLayer> m_layer;
    QString m_libraryDir;

    // Left pane — library list + folder shortcut.
    QListWidget *m_list = nullptr;
    QLabel *m_libDirLabel = nullptr;
    QPushButton *m_openDirBtn = nullptr;

    // Right pane — preview + action buttons.
    QPlainTextEdit *m_preview = nullptr;
    QPushButton *m_applyBtn = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_importBtn = nullptr;
    QPushButton *m_exportBtn = nullptr;
    QPushButton *m_deleteBtn = nullptr;

    QDialogButtonBox *m_btns = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_STYLEMANAGERDIALOG_H
