/**
 * @file      channelburndialog.h
 * @brief     Staged editing of channel burn geometry and generation settings.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_CHANNELBURNDIALOG_H
#define OPENSWMMVIS_CHANNELBURNDIALOG_H

#include "mesh/channelburnmesh.h"
#include "mesh/channelburnexport.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QPointer>
#include <atomic>
#include <memory>

class SWMM2DMeshLayer;
class SWMMModelLayer;
class QCloseEvent;
class SWMMVisProjectWindow;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QSpinBox;
class QTableWidget;

class ChannelBurnDialog final : public QDialog
{
    Q_OBJECT
    friend class TestChannelBurnDialog;
    friend class TestMeshTerrainPipeline;

public:
    /**
     * @brief Build a staged channel-burn workflow bound to the current eligible mesh.
     * @param[in] project Borrowed nullable project window stored through QPointer; supplies
     * model, canvas, units and saved settings.
     * @param[in] parent Optional Qt owner of the dialog and child controls.
     * @details Chooses exactly one active mesh, or the sole mesh when none is active;
     * ambiguous/missing selection disables Burn. Loads previous UI settings but performs no burn
     * at construction. GUI-thread widgets launch copied worker inputs later; model-unit lengths
     * and SI-metre geometry tolerance are labeled separately.
     */
    explicit ChannelBurnDialog(SWMMVisProjectWindow *project, QWidget *parent = nullptr);
    /**
     * @brief Request cooperative worker cancellation before member/Qt destruction.
     * @details Sets the shared atomic flag when present; does not synchronously wait for the
     * background job. Worker closures own their value snapshots and cancellation token rather
     * than dereferencing dialog widgets.
     */
    ~ChannelBurnDialog() override;

protected:
    /**
     * @brief Invalidate any pending adoption, request cancellation and reject immediately.
     * @details Marks the context invalid and sets the cooperative atomic flag before
     * QDialog::reject(); does not wait for worker completion or undo previously adopted
     * commands.
     */
    void reject() override;
    /**
     * @brief Invalidate pending results and cancel before delegating the window close event.
     * @param[in] event Non-null borrowed close event passed unchanged to QDialog::closeEvent().
     * @details Does not synchronously wait for the worker; generated live mesh/network adoption
     * is refused for an invalidated owner.
     */
    void closeEvent(QCloseEvent *) override;

private slots:
    /**
     * @brief Snapshot validated model/mesh inputs and launch one cooperative background burn.
     * @details Busy calls do nothing. collectInputs() validates topology replacement, CRS,
     * source mesh and usable channels; optional raster output additionally requires existing
     * saved-project/DEM paths. Persists current project settings before work, so cancellation
     * can leave those settings updated. Captures model/revision guards, copies the mesh and
     * moves inputs/export request into QtConcurrent; the worker owns values/shared cancellation
     * and reports progress through QPromise without GUI/model dereferences. Optional export
     * prepares staged artifact files; final project publication waits for save. Startup/worker
     * exceptions become visible errors, and no live mesh/network replacement occurs until
     * finishBurn().
     */
    void startBurn();
    /**
     * @brief Review a finished worker result and adopt a current successful burn through one
     * undo command.
     * @details GUI-thread finished handler requires the corresponding future to be complete.
     * Captures cancellation status, rejects changed project/model/mesh context and clears
     * temporary guards. A successful result builds an owned detached replacement layer,
     * transfers staged artifacts/display/CRS/BC state, prepares reversible 1D surgery and pushes
     * ChannelMeshAdoptionCommand on the live canvas undo stack. Preparation pumps non-user-input
     * GUI events for progress. Failed/canceled/stale results are discarded; an exception after
     * command adoption reports that the burn remains applied and undoable even if display
     * refresh failed. Successful adoption disables another burn from this dialog and displays
     * quality summaries; all paths release busy state.
     */
    void finishBurn();
    /**
     * @brief Request cooperative cancellation while busy, or reject an idle dialog.
     * @details Busy cancellation sets the atomic flag, disables repeated cancel clicks and
     * changes status/progress text while the worker winds down; does not block or discard the
     * future synchronously. No cancellation token/busy work delegates to reject().
     */
    void cancelBurn();

private:
    /**
     * @brief Gather independent channel/profile/network snapshots after authoring-context
     * checks.
     * @param[out] inputs Non-null destination expected to be default-constructed/empty; fills
     * options, profiles, boundary slots, node coordinates, network references and warnings. Can
     * be partially populated/appended on failure and must be discarded then.
     * @param[out] error Optional failure-text destination, written on refusal and not cleared on
     * success.
     * @return True when a current eligible mesh, live model, complete edge slots,
     * topology-compatible saved sources, usable planar CRS and at least one accepted
     * open-channel profile are available; false with an explanation otherwise.
     * @details Reads engine/caches and checks saved/imported/generated mesh files synchronously
     * on the GUI thread without changing their topology. Projects coordinates when allowed; 1D
     * interval replacement requires equal model/mesh CRS. Converts model-length controls/section
     * dimensions to mesh XY and mesh vertical units, records SI scale factors, keeps tolerance
     * in metres and routing step in seconds. Snapshots external inflows/impacts to preserve
     * referenced nodes and report unsupported replacement links. Destination contents are owned
     * values with no borrowed engine/GUI pointers retained for the worker.
     */
    bool collectInputs(mesh::ChannelMeshBurnInputs *inputs, QString *error);
    /**
     * @brief Update local activity state and editor/progress/button availability.
     * @param[in] busy True resets/shows progress and disables mutable options/Burn; false
     * re-enables options from current mesh/export/replacement choices.
     * @details Does not start/stop work, check current owner validity or wait for completion.
     * Cancel stays enabled; finishBurn() additionally disables Burn after an adopted result.
     */
    void setBusy(bool busy);
    /**
     * @brief Capture owner identity/revisions and subscribe to changes that cancel pending
     * adoption.
     * @details Requires live project/model/engine/mesh/canvas already validated by
     * collectInputs(). Disconnects earlier guards, clears invalidation, snapshots engine
     * pointer/model path/edit revision and mesh geometry/attribute/BC revisions, then listens
     * for project closure, model changes, layer membership/active mesh and CRS/mesh edits.
     * Notifications set invalidated and the shared cancellation flag when present.
     */
    void beginGuard();
    /**
     * @brief Disconnect and forget this operation's temporary owner-change subscriptions.
     * @details Does not reset captured identities, invalidation, busy state or the worker
     * cancellation flag; safe for an empty connection list.
     */
    void clearGuard();
    /**
     * @brief Compare captured owner identity and revisions before adopting worker output.
     * @return True only when not invalidated, project/model/mesh/canvas remain live/open,
     * model/engine/path/edit revision match, mesh stays on the canvas and geometry/attribute/BC
     * revisions match. Read-only check; this function does not itself reselect a new active mesh
     * or stop work.
     */
    bool ownerIsCurrent() const;

    QPointer<SWMMModelLayer> m_model;
    void *m_engine = nullptr;
    quint64 m_modelRevision = 0, m_geomRevision = 0, m_attrRevision = 0, m_bcRevision = 0;
    QString m_modelPath;
    QList<QMetaObject::Connection> m_connections;
    bool m_invalidated = false;
    bool m_busy = false;

    QPointer<SWMMVisProjectWindow> m_project;
    QPointer<SWMM2DMeshLayer> m_mesh;
    struct Result {
        mesh::ChannelMeshBurnResult burn;
        mesh::ChannelBurnExportResult output;
    };
    QFutureWatcher<Result> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    QComboBox *m_selectionMode = nullptr;
    QLineEdit *m_query = nullptr;
    QLineEdit *m_ids = nullptr;
    QDoubleSpinBox *m_cellSize = nullptr;
    QDoubleSpinBox *m_aspect = nullptr;
    QDoubleSpinBox *m_halfWidth = nullptr;
    QDoubleSpinBox *m_connectorLength = nullptr;
    QCheckBox *m_quads = nullptr;
    QCheckBox *m_removeFromModel = nullptr;
    QDoubleSpinBox *m_maxIncision = nullptr;
    QDoubleSpinBox *m_tolerance = nullptr;
    QSpinBox *m_maxCells = nullptr;
    QCheckBox *m_exportRaster = nullptr;
    QLineEdit *m_demPath = nullptr;
    QLineEdit *m_outputDirectory = nullptr;
    QDoubleSpinBox *m_demZToSI = nullptr;
    QWidget *m_exportOptions = nullptr;
    QTableWidget *m_quality = nullptr;
    QLabel *m_status = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_apply = nullptr;
    QPushButton *m_cancel = nullptr;
};

#endif
