/*!
 * \file   meshprofileplotdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */
#include "ui/dialogs/meshprofileplotdialog.h"
#include "ui/theme/iconfactory.h"
#include "ui/dialogs/dialoglayoutpersistence.h"

#include "animation/animationcontroller.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "map/mapcanvas.h"
#include "map/meshprofileoverlay.h"
#include "map/tools/maptoolprofilemarker.h"
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
#include <QToolButton>
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
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("MeshProfilePlotDialog"));
    setModal(false);
    m_options = new MeshProfilePlotOptions(this);
    setWindowFlags(Qt::Window
                   | Qt::WindowSystemMenuHint
                   | Qt::WindowTitleHint
                   | Qt::WindowMinMaxButtonsHint
                   | Qt::WindowCloseButtonHint
                   | openswmmvis::ui::stayAboveAppFlags());
    resize(900, 520);

    buildLayout();
    rebuildProfile();
    setupMapOverlay();

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

        // Drive our own layer from the global animation clock so the profile
        // animates even when the layer is hidden. The canvas only advances
        // VISIBLE 2D layers per frame (swmmvis.cpp), so a profile on a hidden
        // layer would otherwise freeze. setCurrentSimTime snaps to the nearest
        // frame and re-emits currentTimeChanged (consumed above); it's a no-op
        // when the layer is already on that frame, so this can't double-work.
        if (m_anim) {
            connect(m_anim, &AnimationController::currentTimeChanged,
                    this, [this](const QDateTime &dt) {
                if (m_results) m_results->setCurrentSimTimeAsOf(dt);
            });
        }

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
            removeOverlay();   // detach from the scene before it's torn down
            close();
        });
    }
}

MeshProfilePlotDialog::~MeshProfilePlotDialog()
{
    removeOverlay();
    if (m_results) disconnect(m_results.data(), nullptr, this, nullptr);
    if (m_anim)    disconnect(m_anim.data(),    nullptr, this, nullptr);
}

void MeshProfilePlotDialog::buildLayout()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto *toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(20, 20));
    auto *actSelect  = toolbar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("Select")),  tr("Select"));
    auto *actZoomIn  = toolbar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("ZoomIn")),  tr("Zoom In"));
    auto *actZoomOut = toolbar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("ZoomOut")), tr("Zoom Out"));
    auto *actFit     = toolbar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("Extent")),  tr("Fit"));
    toolbar->addSeparator();
    auto *actPan     = toolbar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("Move")),    tr("Pan"));
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
    auto *actCells = toolbar->addAction(
        openswmmvis::ui::IconFactory::icon(QStringLiteral("CellBoundaries")), tr("Cell boundaries"));
    actCells->setCheckable(true);
    actCells->setObjectName(QStringLiteral("showCells"));
    actCells->setChecked(m_options->showCellBoundaries());
    actCells->setToolTip(tr("Show mesh-cell edge crossings as dots on the ground line"));
    auto *actMapMarker = toolbar->addAction(
        openswmmvis::ui::IconFactory::icon(QStringLiteral("ProfileMarker")), tr("Move marker on map"));
    actMapMarker->setCheckable(true);
    actMapMarker->setObjectName(QStringLiteral("showMapMarker"));
    actMapMarker->setToolTip(tr("Drag the position arrow along the profile on the map"));
    toolbar->addSeparator();
    auto *actOptions = toolbar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("ChartProperties")),
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
    connect(actCells,   &QAction::toggled,   this, [this](bool on) { m_options->setShowCellBoundaries(on); });
    connect(actMapMarker, &QAction::toggled, this, [this](bool on) { setMarkerToolActive(on); });
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
    QVector<bool>   hasSurface;
    depths.reserve(m_profile.samples.size());
    hasSurface.reserve(m_profile.samples.size());
    // Reuse each sample's cached containing cell (triIdx, captured by
    // buildMeshProfile) so per-frame animation skips the cell search entirely.
    // cellHasSurface is frame-dependent (it reads the per-vertex signed-depth
    // field), so it must travel with the depth column — it gates which dry
    // gaps the painter may bridge.
    // SI metres → mesh units, same factor buildMeshProfile applied to the
    // initial depth column (see MeshProfileSampler).
    const double dToMesh = m_results->depthToMeshUnits();
    for (const auto &s : m_profile.samples) {
        depths.push_back(m_results->depthAtCellInterp(s.triIdx, s.scenePt) * dToMesh);
        hasSurface.push_back(m_results->cellHasSurface(s.triIdx));
    }
    m_plot->setCurrentDepths(depths, hasSurface);
}

void MeshProfilePlotDialog::setupMapOverlay()
{
    if (!m_projectWindow || m_scenePolyline.size() < 2) return;
    MapCanvas *canvas = m_projectWindow->canvas();
    if (!canvas) return;

    // Persistent profile line on the map (cleared when this dialog closes).
    // Bound to the canvas, which paints it ABOVE the QSG flood-map mesh.
    m_overlay = new MeshProfileOverlay();
    m_overlay->setPolyline(m_scenePolyline);
    canvas->setMeshProfileOverlay(m_overlay);

    // Chart cursor → map arrow. setCursorChainage on the chart is display-only,
    // so this connection cannot echo back into the chart.
    connect(m_plot, &MeshProfilePlotWidget::cursorChainageChanged,
            this, [this](double chainage) {
        if (!m_overlay) return;
        m_overlay->setArrowChainage(chainage);
        if (m_projectWindow && m_projectWindow->canvas())
            m_projectWindow->canvas()->invalidate(
                MapCanvas::Overlay, QStringLiteral("mesh-profile-marker"));
    });

    // Map-drag tool → chart cursor (display-only; no echo).
    m_markerTool = new MapToolProfileMarker(canvas, this);
    m_markerTool->setOverlay(m_overlay);
    connect(m_markerTool, &MapToolProfileMarker::markerChainageChanged,
            this, [this](double chainage) { m_plot->setCursorChainage(chainage); });

    // Start the cursor mid-profile so the marker is immediately discoverable.
    const double mid = m_overlay->totalLength() * 0.5;
    m_plot->setCursorChainage(mid);
    m_overlay->setArrowChainage(mid);
    canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-profile-overlay"));
}

void MeshProfilePlotDialog::setMarkerToolActive(bool on)
{
    if (!m_projectWindow || !m_markerTool) return;
    MapCanvas *canvas = m_projectWindow->canvas();
    if (!canvas) return;
    if (on) {
        if (canvas->activeTool() == m_markerTool) return;
        m_prevTool = canvas->activeTool();
        canvas->setActiveTool(m_markerTool);
    } else if (canvas->activeTool() == m_markerTool) {
        canvas->setActiveTool(m_prevTool ? m_prevTool.data() : nullptr);
    }
}

void MeshProfilePlotDialog::removeOverlay()
{
    if (m_projectWindow && m_projectWindow->canvas()) {
        MapCanvas *canvas = m_projectWindow->canvas();
        // Restore the canvas tool if we left the marker tool active.
        if (m_markerTool && canvas->activeTool() == m_markerTool)
            canvas->setActiveTool(m_prevTool ? m_prevTool.data() : nullptr);
        // Detach from the canvas (which repaints) before the overlay is freed —
        // but only when the binding is still ours. The canvas holds one overlay
        // slot, so a later trace (another profile dialog) may already have
        // displaced us; clearing then would erase ITS line off the map.
        if (canvas->meshProfileOverlay() == m_overlay)
            canvas->setMeshProfileOverlay(nullptr);
    }
    if (m_markerTool) m_markerTool->setOverlay(nullptr);
    delete m_overlay;        // not a QObject — manual delete after detaching
    m_overlay = nullptr;
}

void MeshProfilePlotDialog::openDisplayOptions()
{
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Profile Display Options"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowFlags(dlg->windowFlags() | openswmmvis::ui::stayAboveAppFlags());
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
