/**
 * @file      resultvariableexportdialog.h
 * @brief     Export of immutable copied result variables in a worker.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "io/mesh2dvariablegisexport.h"
#include <QDialog>
#include <memory>
#include <atomic>
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QProgressBar;
namespace openswmmvis::ui
{
// The worker receives only immutable copied result data. Closing the owner
// requests cancellation without retaining any model/source QObject pointer.
class ResultVariableExportDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] snapshot Required NONNULL immutable shared owning geometry/scalar snapshot, moved
     *   into dialog; no source QObject/engine pointers. Read immediately without null guard.
     * @param[in] parent Optional QWidget owner. WA_DeleteOnClose enabled: allocate/manage as a heap
     *   Qt window, do not rely on stack lifetime after close.
     * @brief Build GeoPackage/GeoTIFF export form from captured variable/time metadata without
     *   recapturing current frame or starting work.
     * @note Captured native scalar units retained. Raster pixel size is declared projected CRS
     *   native linear units, editor [1e-6,1e12]/six decimals/default1 and enabled for GeoTIFF only;
     *   exporter validates CRS/geometry later. GUI-thread construction, worker reads shared
     *   snapshot only.
     */
    explicit ResultVariableExportDialog(io::Mesh2DVariableGisSnapshotPtr snapshot,
                                        QWidget *parent = nullptr);
    /**
     * @brief Request cooperative cancellation through shared atomic flag, then release dialog
     *   references/owned controls.
     * @note Does not join worker or undo an already published export. Worker retains
     *   snapshot/options/cancellation/progress independently of destroyed dialog; context-bound
     *   completion callbacks disappear.
     */
    ~ResultVariableExportDialog() override;
public slots:
    /**
     * @brief Request cancellation if a job flag exists, then reject/close according to Qt
     *   delete-on-close policy.
     * @note Cooperative export checks can prevent publication; completed publication is not
     *   removed. Does not block waiting for worker, reset form or emit exportFinished for preflight
     *   cancellation itself.
     */
    void reject() override;
    /**
     * @brief Capture trimmed destination/options, confirm existing-file replacement with
     *   unchanged-identity recheck, then dispatch one immutable-snapshot export worker.
     * @note Running job is a no-op. Empty/un-capturable destination displays inline error;
     *   cancel/replaced fingerprint during modal confirmation aborts before dispatch. Captures
     *   resolved path/fingerprint before worker; exporter stages/verifies and publishes only
     *   unchanged destination, rejecting source aliases/companions. No layer/engine access on
     *   worker.
     * @note Disable format/pixel/path/browse/start while running; 100ms GUI timer reads shared
     *   integer progress. Cooperative cancellation flag is shared by worker. Completion reenables
     *   controls, shows result and emits exportFinished on GUI thread, success sets progress100; no
     *   automatic accept or adding exported layer. Destruction disconnects callbacks without
     *   joining.
     * @note Raw form options are not reread during worker; native values/status bands and
     *   containing-cell raster policy come from exporter. Repeated finished jobs can be started
     *   again; progress is not explicitly zeroed at dispatch until timer first reads new zero
     *   atomic.
     */
    void startExport();
signals:
    /**
     * @param[in] success Exporter result: true only after successful publication, false for
     *   cancellation/validation/IO/destination race failure.
     * @param[in] message Copied completion text or exporter diagnostic, also displayed in status
     *   label.
     * @note Emitted on surviving GUI dialog after worker completion and controls restored, not for
     *   preflight refusal/cancel/empty destination. No ownership transfer/automatic close; worker
     *   can finish without signal if dialog destroyed.
     */
    void exportFinished(bool success, const QString &message);

private:
    io::Mesh2DVariableGisSnapshotPtr snapshot_;
    QComboBox *format_;
    QDoubleSpinBox *pixel_;
    QLineEdit *path_;
    QLabel *status_;
    QPushButton *export_;
    QPushButton *browse_;
    QProgressBar *progress_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    bool running_ = false;
};
}
