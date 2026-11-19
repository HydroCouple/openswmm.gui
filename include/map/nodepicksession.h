/**
 * @file      nodepicksession.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     "Pick a node on the map" for dialogs and property editors.
 *
 *         Creating a session saves the canvas's active tool, pushes a
 *         OpenSWMMVisMapToolPickNode and forwards its picks; finishing (or
 *         destroying) the session restores the previous tool. The consumer
 *         decides what to do with a pick — reject an ineligible node and keep
 *         waiting, or accept it and finish(). Switching to another canvas tool
 *         while the session is active counts as a cancel.
 *
 *         Parent the session to whatever must outlive the wait: a non-modal
 *         dialog, or — for a property-grid cell editor, which the grid closes
 *         the moment the map takes focus — the layer itself, with the result
 *         written through the layer rather than through the editor.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef NODEPICKSESSION_H
#define NODEPICKSESSION_H

#include <QObject>
#include <QPointer>
#include <QString>

class MapCanvas;
class OpenSWMMVisMapTool;
class OpenSWMMVisMapToolPickNode;
class SWMMModelLayer;

class NodePickSession : public QObject
{
    Q_OBJECT

public:
    /*! Starts picking immediately on \p canvas (no-op, inactive, when null).
     * @param[in] canvas Optional borrowed canvas, guarded by QPointer; null creates permanently
     *   inactive finished session.
     * @param[in] parent Optional QObject lifetime owner, typically stable layer/dialog rather than
     *   transient editor.
     * @note Immediately captures previous active tool, creates session-owned pick-node tool,
     *   forwards picks and installs it on GUI thread. A different tool switch finishes/cancels
     *   without restoring previous; Escape finishes/restores then emits cancelled. Canvas/tool
     *   deletion clears guarded pointers; session must outlive all consumer wait callbacks.
     */
    explicit NodePickSession(MapCanvas *canvas, QObject *parent = nullptr);
    /**
     * @brief Finish active session/restoring previous tool when appropriate, then destroy owned
     *   tool/connections through QObject.
     * @note Does not emit cancelled merely because consumer destroys session; previous tool may
     *   already be null/deleted. GUI-thread only.
     */
    ~NodePickSession() override;

    /*! True while the pick tool is the canvas's active tool.
     * @return Not finished and live guarded canvas/tool whose activeTool pointer still equals
     *   session tool. Does not mutate/restore a lost tool or emit cancellation.
     */
    [[nodiscard]] bool isActive() const;

    /*! Restore the previous tool. Idempotent; the destructor calls it.
     * @note Idempotently marks finished BEFORE changing tool, so restoration is not treated as
     *   cancellation. Restores previous guarded tool only if session tool remains current,
     *   otherwise leaves user tool unchanged. Schedules pick tool deleteLater, does not delete
     *   session or emit cancelled.
     */
    void finish();

signals:
    /*! The user clicked node \p name (index \p nodeIdx) of \p layer. The
     *  session stays active until finish() — reject and keep waiting, or
     *  accept and finish.
     * @param[in] layer Borrowed hit model layer, not owned by signal/session.
     * @param[in] name Exact picked node identifier.
     * @param[in] nodeIdx Global engine/cached node index at pick time; can renumber after topology
     *   edits.
     * @note Forwarded from temporary pick tool; session stays active until consumer accepts with
     *   finish(), so eligibility checks can reject and wait. Borrowed model must be revalidated
     *   before later asynchronous use.
     */
    void nodePicked(SWMMModelLayer *layer, const QString &name, int nodeIdx);
    /*! Escape, or the user switched to another canvas tool. Already finished.
     * @note Emitted after Escape restoration or switch to another canvas tool; session already
     *   marked finished. Neither ordinary finish() nor destruction alone emits it.
     */
    void cancelled();

private:
    QPointer<MapCanvas> m_canvas;
    QPointer<OpenSWMMVisMapTool> m_previous;
    QPointer<OpenSWMMVisMapToolPickNode> m_tool;
    bool m_finished = false;
};

#endif // NODEPICKSESSION_H
