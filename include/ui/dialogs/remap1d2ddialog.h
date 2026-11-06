/**
 * @file      remap1d2ddialog.h
 * @brief     Remap 1D↔2D (1D2D fine-scale coupling plan §7.3, D8): the node coupling selector with
 *            each node's current and proposed mesh coupling, a dry run that updates as nodes are
 *            switched, Preserve existing / Clear & rebuild, and one undoable Apply covering the
 *            node flags and the mesh rows.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Remap 1D↔2D (1D2D fine-scale coupling plan §7.3, D8): the node coupling
 * selector with each node's current and proposed mesh coupling, a dry run
 * that updates as nodes are switched, Preserve existing / Clear & rebuild,
 * and one undoable Apply covering the node flags and the mesh rows.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_REMAP1D2DDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_REMAP1D2DDIALOG_H

#include "mesh/meshnodemapper.h"

#include <QDialog>
#include <QPair>
#include <QPointF>
#include <QPointer>
#include <QVector>

#include <functional>

class MapCanvas;
class NodeCouplingTableModel;
class QCheckBox;
class QLabel;
class QRadioButton;
class SWMM2DMeshLayer;
class SWMMModelLayer;

class Remap1D2DDialog : public QDialog
{
    Q_OBJECT
public:
    /*! \param nodes        Node ids + map coordinates (mesh CRS).
     *  \param mapSelection Node ids selected on the map.
     *  \param extraInfo    Read-only text shown under the summary (the 2D
     *                      aquifer preview).
     *  \param ownershipReview Opens the surface-ownership review; null hides
     *                      its button.
     * @param[in] model Optional borrowed RAW model used for staged table load/current rim
     *   reads/apply; must outlive dialog, no destruction guard.
     * @param[in] mesh Optional borrowed mesh guarded by QPointer, current mesh geometry read on
     *   refresh.
     * @param[in] canvas Optional borrowed RAW canvas, must outlive callbacks/apply; supplied undo
     *   stack should be same edit canvas used by model for one combined macro.
     * @param[in] parent Optional QWidget owner, caller manages parentless dialog.
     * @brief Stage node coupling edits and preview remapping from copied node positions, with
     *   Clear/rebuild and only-coupleable defaults true.
     * @note Capture coupling-law Cd/area authoring policy once at construction; coordinate pairs
     *   and policy do not automatically refresh after source geometry/options changes. Refresh
     *   triggered by staged model counts/options, not live mesh/model signal subscriptions.
     *   ownershipReview callback copied into button connection; button rejects before invoking it
     *   and callback resources/lifetime belong to host. Extra info only displayed; no source
     *   mutation until apply. GUI-thread engine/mesh use.
 */
    Remap1D2DDialog(SWMMModelLayer *model, SWMM2DMeshLayer *mesh, MapCanvas *canvas,
                    QVector<QPair<QString, QPointF>> nodes, const QStringList &mapSelection,
                    const QString &extraInfo, std::function<void()> ownershipReview,
                    QWidget *parent = nullptr);

    /*! The plan the current options and staged node edits would apply.
     * @return Borrowed last computed plan, valid until next refresh or dialog destruction; may be
     *   stale after external mesh/model changes. No recomputation, mutation or ownership transfer.
 */
    const mesh::RemapPlan &plan() const { return m_plan; }
    /**
     * @return Borrowed dialog-owned staging model, valid until destruction. Edits affect staged
     *   flags/types/parameters and trigger preview through countsChanged; no direct engine write
     *   until apply.
     */
    NodeCouplingTableModel *nodeModel() const { return m_model; }
    /**
     * @param[in] on True selects Clear/rebuild, false Preserve existing; radio selection emits
     *   refresh when changed.
     * @brief Change remap preservation mode without applying records. Rebuild rematches nodes but
     *   mapper retains matching authored Cd/area under its contract; no source geometry change
     *   here.
     */
    void setClearAndRebuild(bool on);
    /**
     * @param[in] on True restricts known staged table nodes to coupleable state, false passes all
     *   supplied coordinate pairs.
     * @brief Change filter and let toggled refresh preview; unknown node names remain eligible even
     *   when true. No engine writes or node-list refresh.
     */
    void setMapOnlyCoupleable(bool on);
    /*! Apply the staged node edits and the plan as one undo step.
     * @param[out] error Optional diagnostic forwarded to model node-coupling edits; unavailable
     *   model/mesh returns false without setting it, successful path does not clear old text.
     * @return Result of pushNodeCoupling2DEdits only; true after mesh commands/direct calls
     *   attempted, not independent confirmation every engine write succeeded.
     * @note Recompute current plan from CURRENT mesh plus FROZEN input positions/policy, then
     *   optionally begin supplied canvas undo macro. Model helper chooses ITS edit-canvas stack;
     *   caller aligns both stacks for advertised single undo step. On accepted staged-node writes,
     *   push vertex/cell remap commands or directly apply coupled-node/Cd/area/authored flags and
     *   cell rows without stack.
     * @note End macro even when node edit reports failure. No transaction/rollback of partial
     *   direct/model child writes or stale source identity/token guard in dialog. Does not close
     *   when called directly; button accepts on true and warns on false. Mesh/layer/canvas
     *   raw/guarded lifetimes and engine access must remain coordinated on GUI thread.
 */
    bool apply(QString *error = nullptr);

private:
    /**
     * @brief For live mesh, recompute mapping using staged coupleability and copied coordinate
     *   pairs, then update current/proposed/rim-gap columns and summary.
     * @note Null mesh leaves previous plan/table unchanged. Unknown names in staging model are
     *   allowed; clear/preserve and cached coupling-law flag passed to planRemap. Current engine
     *   rim uses invert+maxDepth; mapped bed uses matched vertex or arithmetic mean of cell vertex
     *   elevations, including quad count. Display absolute rim-bed gap to two decimals in common
     *   authored vertical units, without datum/unit conversion.
     * @note No source topology validation/reload or writes. Summary includes first eight unmatched
     *   names and shared-cell count. External changes are not subscribed here; apply invokes
     *   refresh again, still using frozen node positions.
     */
    void refreshPlan();

    SWMMModelLayer *m_layer = nullptr;
    QPointer<SWMM2DMeshLayer> m_mesh;
    MapCanvas *m_canvas = nullptr;
    QVector<QPair<QString, QPointF>> m_nodes;
    NodeCouplingTableModel *m_model = nullptr;
    QRadioButton *m_rebuild = nullptr;
    QRadioButton *m_preserve = nullptr;
    QCheckBox *m_onlyCoupleable = nullptr;
    QLabel *m_summary = nullptr;
    bool m_authorCdArea = true;
    mesh::RemapPlan m_plan;
};

#endif // OPENSWMMVIS_UI_DIALOGS_REMAP1D2DDIALOG_H
