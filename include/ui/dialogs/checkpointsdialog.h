/* SPDX-License-Identifier: GPL-3.0-or-later */

/**
 * @file      checkpointsdialog.h
 * @brief     Inspection and management of simulation checkpoints.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef CHECKPOINTSDIALOG_H
#define CHECKPOINTSDIALOG_H
#include <QDialog>
#include <functional>
class CheckpointModel;
class QTableView;
class QTextBrowser;
class QLabel;
class QComboBox;
class QSpinBox;
class QPushButton;
class QCloseEvent;

class CheckpointsDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @brief Create the model-backed checkpoint inspection/run/export view.
     * @param model Borrowed non-null GUI-thread model that must outlive this dialog
     * and its nested file/name prompts; ownership is not transferred.
     * @param parent Optional QWidget owner; null leaves the dialog caller-owned.
     * Builds and connects owned controls, hydrates profile/history and refreshes
     * metadata/retention. Profile changes update model context immediately. Import
     * registers a file; Inspect/Resume/Start invoke preparation first. Save requests a
     * new checkpoint at an accepted boundary; export uses a new destination path.
     * Show files delegates to the desktop, Forget retains files, Stop cancels the run,
     * and closing cancels IO while leaving a run active. Signal callbacks use this
     * dialog as connection context; no owned engine handle or automatic file deletion.
     */
    explicit CheckpointsDialog(CheckpointModel *model, QWidget *parent = nullptr);
    /**
     * @brief Replace the retained synchronous preparation hook.
     * @param prepare Moved callable receiving true when inspect/start/resume requires
     * a frozen saved-model context, false for bundle export. Return false to stop the
     * requested action. An empty callable permits preparation unconditionally.
     * The dialog owns the callable value; caller-managed captures must remain valid
     * while invoked. Exceptions are not caught here and no validation occurs on assignment.
     */
    void setPreparation(std::function<bool(bool)> prepare) { m_prepare = std::move(prepare); }

protected:
    /**
     * @brief Cancel active inspection/export IO before delegating ordinary dialog closure.
     * @param event Borrowed non-null Qt close event valid during dispatch.
     * Does not cancel a running simulation, destroy the borrowed model or delete files;
     * QDialog determines the eventual close result.
     */
    void closeEvent(QCloseEvent *event) override;

private:
    CheckpointModel *m_model;
    QTableView *m_list;
    QTextBrowser *m_details, *m_files, *m_storage;
    QLabel *m_status, *m_notice;
    QComboBox *m_profile, *m_history, *m_retention;
    QSpinBox *m_keep;
    QPushButton *m_inspect, *m_resume, *m_start, *m_save, *m_export, *m_cancel, *m_stop;
    std::function<bool(bool)> m_prepare;
    /**
     * @brief Return the table current row independently of its selection set.
     * @return Zero-based current model row, or -1 when currentIndex is invalid.
     * Requires constructed controls; does not validate that a model entry still exists.
     */
    int row() const;
    /**
     * @brief Invoke the retained preparation callback if present.
     * @param frozenModel True for inspect/start/resume, false for export; default true.
     * @return Callback result, or true when no callback exists. Executes synchronously
     * on the GUI thread and permits callback side effects; exceptions propagate.
     */
    bool prepare(bool frozenModel = true);
    /**
     * @brief Ask for an existing parent and one trimmed new run/bundle label.
     * @param title Borrowed localized prompt title, copied into both dialogs.
     * @return Owned joined target path, or empty on canceled/empty/invalid label.
     * Rejects dot/dot-dot, slash, backslash and NUL; other platform filename rules and
     * existing-target checks remain the model operation responsibility. Uses the
     * model stagingRoot as initial chooser directory, reports invalid names in the
     * notice label and does not create the returned directory. Requires the borrowed
     * model/dialog to remain live across nested modal event loops.
     */
    QString newFolder(const QString &title);
    /**
     * @brief Synchronize capability gates, selected-entry text and retention estimates from the
     * borrowed model.
     * Blocks profile/history signals while hydrating; assumes their five/three-item
     * QStandardItemModel structure remains intact. Enables actions from native/output/
     * history capabilities and IO/run state, formats saved coverage separately from
     * current file sizes and computes deduplicated storage/retention suggestions.
     * Retention is a preview and never deletes files. Calls model metadata/storage
     * queries synchronously; does not inspect a checkpoint or start a run itself.
     */
    void refresh();
};
#endif
