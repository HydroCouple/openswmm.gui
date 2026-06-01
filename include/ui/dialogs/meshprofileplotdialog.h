/*!
 * \file   meshprofileplotdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Host dialog for the 2D-mesh longitudinal profile.  Builds a
 *         MeshProfile from a traced polyline, hosts a MeshProfilePlotWidget,
 *         and drives the animated depth column from the 2D results layer's
 *         frame changes (which the global AnimationController advances).
 *
 *         Mirrors ProfilePlotDialog's lifetime model: a top-level window
 *         parented to its project window, WA_DeleteOnClose, that severs
 *         external connections on the project's aboutToClose so a multi-
 *         window teardown can't deliver into a half-destroyed dialog.
 */
#ifndef MESH_PROFILE_PLOT_DIALOG_H
#define MESH_PROFILE_PLOT_DIALOG_H

#include "plot/meshprofilesampler.h"

#include <QDateTime>
#include <QDialog>
#include <QPointer>
#include <QPointF>
#include <QVector>

class AnimationController;
class MeshProfilePlotOptions;
class MeshProfilePlotWidget;
class SWMM2DMeshLayer;
class SWMM2DResultsLayer;
class SWMMVisProjectWindow;

class MeshProfilePlotDialog : public QDialog
{
    Q_OBJECT
public:
    MeshProfilePlotDialog(SWMM2DMeshLayer        *mesh,
                          SWMM2DResultsLayer     *results,    // may be null
                          AnimationController    *anim,
                          const QVector<QPointF> &scenePolyline,
                          SWMMVisProjectWindow   *projectWindow,
                          QWidget                *parent = nullptr);
    ~MeshProfilePlotDialog() override;

private:
    void buildLayout();
    void rebuildProfile();          // full resample (geometry + envelope)
    void refreshCurrentDepths();    // per-frame depth-column update
    void openDisplayOptions();

    QPointer<SWMM2DMeshLayer>     m_mesh;
    QPointer<SWMM2DResultsLayer>  m_results;
    QPointer<AnimationController> m_anim;
    QPointer<SWMMVisProjectWindow> m_projectWindow;
    QVector<QPointF>              m_scenePolyline;

    MeshProfilePlotWidget        *m_plot    = nullptr;
    MeshProfilePlotOptions       *m_options = nullptr;

    // Cached profile — geometry/envelope stay static across animation frames;
    // only the depth column is re-sampled per tick (cheap vs a full rebuild).
    MeshProfileSampler::MeshProfile m_profile;
};

#endif // MESH_PROFILE_PLOT_DIALOG_H
