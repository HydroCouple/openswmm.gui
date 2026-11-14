// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      modelcheckdialog.h
 * @brief     Check Model window (review G-09): a summary and a table of every error and warning
 *            the engine reports when it opens and initializes a copy of the model.
 * @details   The controller supplies how to write the copy and the live model engine (to link
 *            messages to objects); the window runs the check off the GUI thread and reports an
 *            activated object through objectActivated(). The copy's folder is deleted when the
 *            check ends, even if the window was closed meanwhile.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include "simulation/modelcheck.h"

#include <QAbstractTableModel>
#include <QDialog>
#include <QFutureWatcher>

#include <functional>

class QLabel;
class QProgressBar;
class QPushButton;
class QTableView;

namespace openswmmvis::ui
{

/*! Read-only rows of a model check. */
class ModelCheckTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { SeverityColumn, CodeColumn, ObjectColumn, MessageColumn, ColumnCount };

    using QAbstractTableModel::QAbstractTableModel;

    /** @brief Replace every row. */
    void setIssues(const QList<simulation::ModelCheckIssue> &issues);
    [[nodiscard]] const QList<simulation::ModelCheckIssue> &issues() const { return m_issues; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    /** @return "Node J1"-style label for an object type and name; empty when unlinked. */
    static QString objectLabel(SWMMObjectRef::ObjectType type, const QString &name);

private:
    QList<simulation::ModelCheckIssue> m_issues;
};

/*! Non-modal Check Model window. */
class ModelCheckDialog : public QDialog
{
    Q_OBJECT

public:
    /// Writes the copy to check; returns its model path, or empty with *error set. GUI thread.
    using SnapshotWriter = std::function<QString(QString *error)>;
    /// The live model engine used to link messages to objects; may return null.
    using EngineSource = std::function<SWMM_Engine()>;

    /**
     * @param[in] writer Copies the model, unsaved edits included.
     * @param[in] liveEngine Supplies the open model's engine on the GUI thread.
     * @param[in] parent Optional owner.
     */
    ModelCheckDialog(SnapshotWriter writer, EngineSource liveEngine, QWidget *parent = nullptr);
    ~ModelCheckDialog() override;

    /** @brief Write a copy and check it; does nothing while a check runs. */
    void startCheck();
    [[nodiscard]] bool isChecking() const;
    [[nodiscard]] ModelCheckTableModel *issueModel() const { return m_model; }
    [[nodiscard]] QString summaryText() const;

signals:
    /**
     * @brief A check ended (also when the copy could not be written).
     * @param summary The summary line shown at the top.
     * @param starts True when a run of the model would start (no errors).
     */
    void checkFinished(const QString &summary, bool starts);
    /** @brief The user asked to go to the object a row names (double-click or Enter). */
    void objectActivated(const SWMMObjectRef &ref);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onFinished();
    void activateRow(int row);
    void setBusy(bool busy);
    void finish(const QString &summary, bool starts);

    SnapshotWriter m_writer;
    EngineSource m_liveEngine;
    ModelCheckTableModel *m_model = nullptr;
    QLabel *m_summary = nullptr;
    QProgressBar *m_busy = nullptr;
    QTableView *m_view = nullptr;
    QPushButton *m_again = nullptr;
    bool m_checking = false; ///< from startCheck() until the result is shown
    QFutureWatcher<simulation::ModelCheckResult> m_watcher;
};

/**
 * @brief Remove a Check Model copy's folder (`.../.swmmvis/check/<id>/`) and the `check` and
 *   `.swmmvis` folders above it when they are left empty.
 * @param[in] snapshotModelPath The copy's model file. Other folder layouts are left alone.
 * @return True when the copy's folder was removed.
 */
bool removeModelCheckSnapshot(const QString &snapshotModelPath);

} // namespace openswmmvis::ui
