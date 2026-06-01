/*!
 * \file   meshprofileplotdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */
#include "ui/dialogs/meshprofileplotdialog.h"

#include "animation/animationcontroller.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "plot/meshprofileplotoptions.h"
#include "plot/meshprofileplotwidget.h"
#include "plot/meshprofilesampler.h"
#include "swmmvisprojectwindow.h"
#include "core/unitsystem.h"

#ifdef HAVE_QPROPERTYMODEL
#include <qpropertymodel.h>
#include <qpropertyitemdelegate.h>
#endif

#include <QActionGroup>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>

MeshProfilePlotDialog::MeshProfilePlotDialog(SWMM2DMeshLayer        *mesh,
                                             SWMM2DResultsLayer     *results,
                                             AnimationController    *anim,
                                             const QVector<QPointF> &scenePolyline,
                                             SWMMVisProjectWindow   *projectWindow,
                                             QWidget                *parent)
    : QDialog(parent),
      m_mesh(mesh),
      m_results(results),
      m_anim(anim),
      m_projectWindow(projectWindow),
      m_scenePolyline(scenePolyline)
{
    setWindowTitle(tr("2D Mesh Profile"));
    setModal(false);
    m_options = new MeshProfilePlotOptions(this);
    setWindowFlags(Qt::Window
                   | Qt::WindowSystemMenuHint
                   | Qt::WindowTitleHint
                   | Qt::WindowMinMaxButtonsHint
                   | Qt::WindowCloseButtonHint
                   | Qt::WindowStaysOnTopHint);
    resize(900, 520);

    buildLayout();
    rebuildProfile();

    // Animate the depth column off the 2D results layer's frame changes —
    // the global AnimationController advances the layer (for visible layers),
    // which emits currentTimeChanged / currentDateTimeChanged.
    if (m_results) {
        connect(m_results, &SWMM2DResultsLayer::currentTimeChanged,
                this, [this](int) { refreshCurrentDepths(); });
        connect(m_results, &SWMM2DResultsLayer::currentDateTimeChanged,
                this, [this](const QDateTime &dt) { m_plot->setCurrentDateTime(dt); });
        // Recompute the max-depth envelope when more frames stream in (live).
        connect(m_results, &SWMM2DResultsLayer::timeRangeChanged,
                this, [this](int, int) { rebuildProfile(); });

        // Initial timestamp + depths for the frame already showing.
        if (auto *src = m_results->source()) {
            const int t = m_results->currentTimeIndex();
            if (t >= 0) m_plot->setCurrentDateTime(src->simTimeAt(t));
        }
    }

    // Lifetime: drop external references before any project teardown begins.
    if (m_projectWindow) {
        connect(m_projectWindow, &SWMMVisProjectWindow::aboutToClose,
                this, [this] {
            if (m_anim)    disconnect(m_anim.data(),    nullptr, this, nullptr);
            if (m_results) disconnect(m_results.data(), nullptr, this, nullptr);
            close();
        });
    }
}

MeshProfilePlotDialog::~MeshProfilePlotDialog()
{
    if (m_results) disconnect(m_results.data(), nullptr, this, nullptr);
    if (m_anim)    disconnect(m_anim.data(),    nullptr, this, nullptr);
}

void MeshProfilePlotDialog::buildLayout()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto *toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(18, 18));
    auto *actSelect  = toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Select")),  tr("Select"));
    auto *actZoomIn  = toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/ZoomIn")),  tr("Zoom In"));
    auto *actZoomOut = toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/ZoomOut")), tr("Zoom Out"));
    auto *actFit     = toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Extent")),  tr("Fit"));
    toolbar->addSeparator();
    auto *actPan     = toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Move")),    tr("Pan"));
    actSelect->setCheckable(true);  actSelect->setChecked(true);
    actZoomIn->setCheckable(true);
    actZoomOut->setCheckable(true);
    actPan->setCheckable(true);
    actFit->setShortcut(QKeySequence(Qt::Key_Home));
    auto *modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addAction(actSelect);
    modeGroup->addAction(actZoomIn);
    modeGroup->addAction(actZoomOut);
    modeGroup->addAction(actPan);
    toolbar->addSeparator();
    auto *actOptions = toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Settings")),
                                          tr("Display Options…"));
    root->addWidget(toolbar);

    m_plot = new MeshProfilePlotWidget(this);
    m_plot->setOptions(m_options);
    root->addWidget(m_plot, /*stretch=*/1);

    // Axis labels from the project unit system.
    auto *us = m_projectWindow ? m_projectWindow->unitSystem() : UnitSystem::instance();
    const QString unit = us ? us->lengthLabel() : QString();
    m_plot->setAxisLabels(unit.isEmpty() ? tr("Distance")  : tr("Distance (%1)").arg(unit),
                          unit.isEmpty() ? tr("Elevation") : tr("Elevation (%1)").arg(unit));

    connect(actSelect,  &QAction::triggered, this, [this] { m_plot->setMode(MeshProfilePlotWidget::Mode::Identify); });
    connect(actZoomIn,  &QAction::triggered, this, [this] { m_plot->setMode(MeshProfilePlotWidget::Mode::ZoomIn); });
    connect(actZoomOut, &QAction::triggered, this, [this] { m_plot->setMode(MeshProfilePlotWidget::Mode::ZoomOut); });
    connect(actPan,     &QAction::triggered, this, [this] { m_plot->setMode(MeshProfilePlotWidget::Mode::Pan); });
    connect(actFit,     &QAction::triggered, this, [this] { m_plot->fitToExtent(); });
    connect(actOptions, &QAction::triggered, this, &MeshProfilePlotDialog::openDisplayOptions);
}

void MeshProfilePlotDialog::rebuildProfile()
{
    if (!m_mesh) return;
    m_profile = MeshProfileSampler::buildMeshProfile(
        m_mesh.data(), m_results.data(), m_scenePolyline);
    m_plot->setProfile(m_profile);
}

void MeshProfilePlotDialog::refreshCurrentDepths()
{
    if (!m_results || m_profile.samples.isEmpty()) return;
    // Re-sample only the depth column at the layer's now-current frame using
    // the cached sample scene points — avoids recomputing the (expensive)
    // max-depth envelope on every animation tick.
    QVector<double> depths;
    depths.reserve(m_profile.samples.size());
    for (const auto &s : m_profile.samples)
        depths.push_back(m_results->depthAtSceneNow(s.scenePt));
    m_plot->setCurrentDepths(depths);
}

void MeshProfilePlotDialog::openDisplayOptions()
{
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Profile Display Options"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowFlags(dlg->windowFlags() | Qt::WindowStaysOnTopHint);
    auto *lay = new QVBoxLayout(dlg);
    auto *tree = new QTreeView(dlg);
    tree->setAlternatingRowColors(true);
    tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
    lay->addWidget(tree, /*stretch=*/1);

#ifdef HAVE_QPROPERTYMODEL
    auto *pm = new QPropertyModel(dlg);
    auto *delegate = new QPropertyItemDelegate(dlg);
    tree->setModel(pm);
    tree->setItemDelegate(delegate);
    tree->header()->setDefaultSectionSize(180);
    pm->setData(QVariant::fromValue<QObject *>(m_options));
    tree->expandAll();
    connect(m_options, &MeshProfilePlotOptions::changed, pm,
            [pm] { pm->refreshValues(); });
#else
    lay->addWidget(new QLabel(
        tr("Property editor unavailable (QPropertyModel not built in)."), dlg));
#endif

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    connect(bb, &QDialogButtonBox::accepted, dlg, &QDialog::close);
    lay->addWidget(bb);
    dlg->resize(360, 480);
    dlg->show();
}
