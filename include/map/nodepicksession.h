/*!
 * \file   nodepicksession.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  "Pick a node on the map" for dialogs and property editors.
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
    /*! Starts picking immediately on \p canvas (no-op, inactive, when null). */
    explicit NodePickSession(MapCanvas *canvas, QObject *parent = nullptr);
    ~NodePickSession() override;

    /*! True while the pick tool is the canvas's active tool. */
    [[nodiscard]] bool isActive() const;

    /*! Restore the previous tool. Idempotent; the destructor calls it. */
    void finish();

signals:
    /*! The user clicked node \p name (index \p nodeIdx) of \p layer. The
     *  session stays active until finish() — reject and keep waiting, or
     *  accept and finish. */
    void nodePicked(SWMMModelLayer *layer, const QString &name, int nodeIdx);
    /*! Escape, or the user switched to another canvas tool. Already finished. */
    void cancelled();

private:
    QPointer<MapCanvas>                  m_canvas;
    QPointer<OpenSWMMVisMapTool>         m_previous;
    QPointer<OpenSWMMVisMapToolPickNode> m_tool;
    bool                                 m_finished = false;
};

#endif // NODEPICKSESSION_H
