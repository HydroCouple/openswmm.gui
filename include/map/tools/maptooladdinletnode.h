/**
 * @file      maptooladdinletnode.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Click-a-street-conduit tool that inserts a configured inlet junction.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef MAPTOOLADDINLETNODE_H
#define MAPTOOLADDINLETNODE_H

#include "map/tools/conduitsplitpick.h"
#include "map/tools/maptool.h"

#include <QPointer>
#include <QString>

class SWMMModelLayer;
namespace openswmmvis::ui
{
class InletJunctionSetupDialog;
}

/*!
 * \class OpenSWMMVisMapToolAddInletNode
 * \brief Left-click a STREET conduit to split it and insert an inlet junction
 *        at the picked point.
 * \details Structurally the virtual-junction tool
 *          (OpenSWMMVisMapToolAddVirtualNode) with two additions:
 *
 *          - the hit must be a STREET conduit. An inlet junction models a
 *            gutter inlet, so a pipe is not a legal host; clicking one emits a
 *            status hint instead of splitting. (Drop inlets on RECT_OPEN /
 *            TRAPEZOIDAL channels are an inlet-USAGE case, edited from the
 *            conduit's Inlets row — not a node insertion.)
 *          - the insertion is configured BEFORE it happens (decision D-G6):
 *            a NON-MODAL InletJunctionSetupDialog collects the inlet design
 *            and capture node (the capture node can be picked on the map),
 *            because the engine requires both for the node to validate. The
 *            click is armed on press and the dialog shown from the RELEASE;
 *            the split itself runs from the dialog's accepted() slot, never
 *            inside a mouse handler (see maptoolpick2dcells.h for why).
 *
 *          Each insertion pushes an InsertInletJunctionCommand (engine-side
 *          `swmm_conduit_split_inlet`; undo re-fuses and drops the usage row).
 */
class OpenSWMMVisMapToolAddInletNode : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    /**
     * @brief Create inactive network drawing tool without installing it or changing engine.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events; no ownership
     *   transfer.
     * @param[in] parent Optional QObject owner; caller owns tool if null.
     * @note GUI-thread only. Current raw model is chosen from canvas list during use;
     *   model/terrain/hover lifetimes and topology coherence remain caller responsibility. Undo
     *   stack required for actual creation; absent stack deletes commands without executing.
     */
    explicit OpenSWMMVisMapToolAddInletNode(MapCanvas *canvas, QObject *parent = nullptr);
    /**
     * @brief Close any guarded pending setup panel (WA_DeleteOnClose), then destroy tool
     *   connections. No rollback of accepted split.
     */
    ~OpenSWMMVisMapToolAddInletNode() override;

    /*! The setup dialog currently waiting for the user, if any (test seam).
     * @return Borrowed guarded current setup panel or null after deletion; not owned by caller, may
     *   remain open while tool inactive.
     */
    [[nodiscard]] openswmmvis::ui::InletJunctionSetupDialog *pendingDialog() const;

    /**
     * @return Crosshair independent of current snap or drawing phase; no widget cursor change.
     */
    [[nodiscard]] QCursor cursor() const override;

    /**
     * @brief Clear hover/armed state, activate base and emit street-conduit instruction. Existing
     *   pending dialog is retained.
     */
    void activate() override;
    /**
     * @brief Clear hover/armed state/status and deactivate base, retaining pending nonmodal setup
     *   panel.
     * @note Panel can still accept and commit while tool inactive; owner/destruction closes it only
     *   on tool destruction or later replacement click.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed press; left+canvas arms, other presses disarm.
     * @brief Arm release-based setup without opening panel or editing model. No event acceptance.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed release; left+armed+canvas needed, no drag-distance
     *   threshold.
     * @brief Fresh-pick STREET conduit and open nonmodal setup panel before mutating network.
     * @note Invalid/nonstreet hit only emits guidance. Valid hit closes previous panel, excludes
     *   current conduit endpoint node indices in new panel, marks delete-on-close and captures
     *   guarded canvas/model plus link NAME and original split fraction. Clears hover and
     *   invalidates Overlay after show/raise/activate.
     * @note On accepted panel, only link-name existence is rechecked before command; engine split
     *   revalidates topology/type. Missing stack deletes command without applying. Found generated
     *   node then gets optional nonautomatic placement as SEPARATE undo step, typed selection and
     *   inletJunctionAdded/status. Scene/Overlay invalidated after attempt. Rejection emits
     *   cancellation status; inactive tool may still accept panel.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed move when canvas lives.
     * @brief Refresh shared conduit hover and invalidate Overlay; nonstreet conduit still retained
     *   for refusal preview. Raw hover model must survive paint.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] painter Optional active widget-pixel painter; null/invalid hover no-op.
     * @param[in] extent Unused current canvas extent.
     * @param[in] srs Unused borrowed canvas CRS.
     * @brief Paint model-coloured7pixeldiamond: solid+10pixelcross for street, dotted outline only
     *   otherwise.
     * @note Stored native closest point layer→canvas projection status ignored; painter
     *   saved/restored, no DPR/transform reset. Caller preserves raw hover model. Preview point not
     *   recomputed at clamped split fraction.
     */
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

signals:
    /**
     * @param[in] nodeName Generated node ID found after attempted split; role not independently
     *   revalidated by signal.
     * @param[in] splitLinkName Original upstream conduit ID.
     * @param[in] newLinkName Generated downstream ID, not separately checked.
     * @note Emitted after optional placement edit push and typed selection from accepted panel
     *   callback; no guarantee all later setters accepted.
     */
    void inletJunctionAdded(const QString &nodeName, const QString &splitLinkName,
                            const QString &newLinkName);
    /**
     * @param[in] message Translated instruction/refusal/result/cancellation or empty deactivate
     *   status.
     * @note Synchronous GUI signal; accepted/rejected pending dialog can emit while tool is
     *   inactive.
     */
    void statusMessageChanged(const QString &message);

private:
    ConduitSplitPick::ConduitHit m_hover; ///< live preview of the split point
    bool m_armed = false;
    QPointer<openswmmvis::ui::InletJunctionSetupDialog> m_pendingDialog;
};

#endif // MAPTOOLADDINLETNODE_H
