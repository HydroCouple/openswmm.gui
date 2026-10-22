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
     *                      its button. */
    Remap1D2DDialog(SWMMModelLayer *model, SWMM2DMeshLayer *mesh, MapCanvas *canvas,
                    QVector<QPair<QString, QPointF>> nodes, const QStringList &mapSelection,
                    const QString &extraInfo, std::function<void()> ownershipReview,
                    QWidget *parent = nullptr);

    /*! The plan the current options and staged node edits would apply. */
    const mesh::RemapPlan &plan() const { return m_plan; }
    NodeCouplingTableModel *nodeModel() const { return m_model; }
    void setClearAndRebuild(bool on);
    void setMapOnlyCoupleable(bool on);
    /*! Apply the staged node edits and the plan as one undo step. */
    bool apply(QString *error = nullptr);

private:
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
