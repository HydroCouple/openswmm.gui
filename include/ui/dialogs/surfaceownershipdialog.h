// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      surfaceownershipdialog.h
 * @brief     Review and editing of surface ownership assignments.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_SURFACEOWNERSHIPDIALOG_H
#define OPENSWMMVIS_SURFACEOWNERSHIPDIALOG_H
#include "assignment/surfaceownership.h"
#include <QDialog>
#include <QPointer>
class SWMMModelLayer;
class SWMM2DMeshLayer;
class MapCanvas;
class SelectionManager;
class QComboBox;
class QCheckBox;
class QLineEdit;
class QTableWidget;
class QPushButton;
class QLabel;
namespace openswmmvis::ui
{
class SurfaceOwnershipDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] model Optional borrowed guarded model; capture current engine identity once,
     *   caller preserves active engine throughout synchronous review/reentrant events.
     * @param[in] mesh Optional borrowed guarded GUI mesh; must match engine geometry/cells, no
     *   ownership transfer.
     * @param[in] canvas Optional borrowed guarded canvas; nonnull undo stack required by
     *   contextValid/application.
     * @param[in] selection Optional borrowed guarded central selection manager, only needed for
     *   selected scope and inspection actions.
     * @param[in] parent Optional QWidget owner; caller manages parentless window-modal dialog.
     * @brief Build geometric ownership-review tabs, default subcatchment-weather representation and
     *   explicit-uniform checkbox unchecked.
     * @note Capture area-display units from engine flow units once: US divides square metres by
     *   acre factor, SI by 10000 for hectares; query failure falls back initialized US branch.
     *   Receiver table always square metres and displayed cells one-based. No preview/write at
     *   construction; model edit/close/geometry and mesh edit signals invalidate draft,
     *   selection/CRS changes have no dedicated subscription here.
     */
    SurfaceOwnershipDialog(SWMMModelLayer *model, SWMM2DMeshLayer *mesh, MapCanvas *canvas,
                           SelectionManager *selection, QWidget *parent = nullptr);
public slots:
    /**
     * @brief Ignore rejection while synchronous preview is busy, otherwise delegate QDialog
     *   rejection.
     * @note Does not cancel calculation, clear review or reverse applied records. Progress dialog
     *   Cancel requests cooperative preview cancellation instead. Owners must still preserve
     *   dialog/model/mesh during processEvents.
     */
    void reject() override;
signals:
    /**
     * @brief Notify successful reviewed-record replacement after undo registration, model
     *   dirty/options notification and mesh ownership refresh.
     * @note No arguments/ownership transfer; does not close dialog or start hydraulic session.
     *   Unchanged/stale/failed apply emit nothing.
     */
    void applied();
    /**
     * @brief Request host open existing groundwater/soil/ET editor; no editor construction, source
     *   mutation or automatic dialog closure here.
     */
    void groundwaterEditorRequested();
    /**
     * @brief Request existing source editor after manager closes, using current reviewed object-row
     *   name.
     * @note Unnamed QString argument is copied UTF-8 subcatchment name from m_review; host resolves
     *   current object and preserves signal delivery lifetime. No model edit or guard that review
     *   remains current before emission.
     */
    void sourceEditorRequested(const QString &);
private slots:
    /**
     * @brief Synchronously review current/proposed ownership for selected scope with cancellable
     *   progress, without replacing records.
     * @note Busy is no-op. Validate model/mesh/undo/CRS context and require explicit uniform
     *   checkbox only for subcatchment-weather representation. First preview all existing rows to
     *   obtain source metadata, then selected/tag/all scope IDs; empty scope refuses. Proposed list
     *   removes scope IDs then appends them only for representation index1, sorted. Disabled
     *   unavailable index2, if set programmatically, follows unreviewed removal path rather than
     *   mesh conversion.
     * @note Disable tabs/Preview while running, restoring via scope guard. Progress
     *   updates/processEvents no more often than 50 ms; cancellation predicate also checks
     *   model/engine identity, but owners must keep dialog/mesh/canvas alive through reentry. All
     *   competing footprints are reviewed, including outside edit scope.
     * @note Success stores review, mesh geometry revision and both CRS WKT strings, populates
     *   project-area source table and square-metre receiver budgets/options text. Enable Apply only
     *   valid review with before!=after. Invalid review error retained for display; unavailable
     *   runtime budget is not inferred from geometric area. No engine authoring replacement/undo in
     *   this method.
     */
    void preview();
    /**
     * @brief For ready idle review, revalidate context/mesh revision/CRS and atomically request
     *   reviewed ownership replacement using preview token.
     * @note Stale/context/write failure disables readiness with inline error. After successful
     *   replacement obtain installed token, push first-redo-skipping OwnershipCommand on required
     *   canvas stack, mark model edited, emit optionsChanged and refresh mesh ownership, disable
     *   Apply and emit applied. No post-replacement installed-preview validity gate here.
     * @note Suppress local invalidation only during command registration/notifications, not initial
     *   replacement. Stores authoring records only, preserving hydraulic mapping/soil settings; no
     *   simulation/interval recharge execution. Undo later rejects changed engine/CRS/mesh/token
     *   context. Does not close form or snapshot whole unrelated project state.
     */
    void apply();
    /**
     * @brief Unless committing, clear ready flag, disable Apply and display changed-input status.
     * @note Does not clear tables/review/token, cancel busy computation, recapture engine identity
     *   or refresh scope. Invalidation during synchronous preview does not carry a generation flag;
     *   eventual engine token/context revalidation supplies apply protection.
     */
    void invalidate();
    /**
     * @brief Replace central selection with current reviewed source subcatchment and every share
     *   receiver cell for it.
     * @note Missing selection/mesh/valid current source row no-op. Cell identity uses current mesh
     *   sourcePath and zero-based cell indices, not displayed one-based number. Does not require
     *   ready/current review, change geometry/ownership, zoom or guard topology beyond retained
     *   review.
     */
    void selectReceivers();
    /**
     * @brief Replace selection with current reviewed receiver cell plus every contributing source
     *   subcatchment from share rows.
     * @note Missing manager/mesh/valid current receiver row no-op; cells keyed by current mesh path
     *   and zero-based index. Uses retained review even after invalidation, no geometry/ownership
     *   writes or zoom.
     */
    void selectCellSources();

private:
    /**
     * @param[out] error Required nonnull diagnostic pointer; set on unavailable context,
     *   geographic/mismatching CRS or mesh check failure, unchanged on successful checks.
     * @return True only for live model/mesh/canvas/undo, same captured engine identity and
     *   surfaceOwnershipMeshMatches.
     * @note Reject assigned geographic model CRS, and differing exact model/mesh WKT when assigned
     *   model SRS and both present. Unassigned/missing CRS skips those particular checks. No
     *   projection, engine lifecycle transition, ownership write or source identity rebinding.
     */
    bool contextValid(QString *error) const;
    /**
     * @param[in] review Read-only reviewed source objects, each supplies subcatch index/name/tag.
     * @return Source indices in review order for All, current central selected subcatchment names,
     *   or exact case-sensitive tag versus trimmed tag editor; unknown scope/absent selection
     *   yields none. No deduplication, layer filtering, readiness validation or model writes.
     */
    QVector<int> scope(const assignment::SurfaceOwnershipReview &review) const;
    QPointer<SWMMModelLayer> m_model;
    QPointer<SWMM2DMeshLayer> m_mesh;
    QPointer<MapCanvas> m_canvas;
    QPointer<SelectionManager> m_selection;
    SWMM_Engine m_engine = nullptr;
    assignment::SurfaceOwnershipReview m_review;
    quint64 m_geometry = 0;
    QString m_crs, m_areaUnit;
    double m_areaFactor = 1;
    bool m_busy = false, m_committing = false, m_ready = false;
    QComboBox *m_scope = nullptr, *m_representation = nullptr;
    QLineEdit *m_tag = nullptr;
    QCheckBox *m_uniform = nullptr;
    QTableWidget *m_objects = nullptr, *m_receivers = nullptr;
    QPushButton *m_preview = nullptr, *m_apply = nullptr;
    QLabel *m_status = nullptr, *m_parameters = nullptr;
};
}
#endif
