/*!
 * \file   rasterprofileplotdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */
#include "ui/dialogs/rasterprofileplotdialog.h"
#include "ui/theme/iconfactory.h"
#include "ui/dialogs/dialoglayoutpersistence.h"

#include "core/unitsystem.h"
#include "layers/gisrasterlayer.h"
#include "map/mapcanvas.h"
#include "map/meshprofileoverlay.h"
#include "map/tools/maptoolprofilemarker.h"
#include "plot/meshprofileplotoptions.h"
#include "plot/meshprofileplotwidget.h"
#include "plot/rasterprofilesampler.h"
#include "swmmvisprojectwindow.h"

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

RasterProfilePlotDialog::RasterProfilePlotDialog(GISRasterLayer         *raster,
                                                 double                  vertFactor,
                                                 const QVector<QPointF> &scenePolyline,
                                                 SWMMVisProjectWindow   *projectWindow,
                                                 QWidget                *parent)
    : QDialog(parent),
      m_raster(raster),
      m_projectWindow(projectWindow),
      m_scenePolyline(scenePolyline),
      m_vertFactor(vertFactor)
{
    setWindowTitle(tr("Terrain Profile"));
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("RasterProfilePlotDialog"));
    setModal(false);
    m_options = new MeshProfilePlotOptions(this);
    // A DEM section has no water and no mesh cells. The chart's water passes
    // are already gated on hasResults (false here), so nothing would render
    // either way — turning the toggles off keeps them from showing up as dead
    // switches in this dialog's Display Options tree.
    m_options->setShowDepthFill(false);
    m_options->setShowWseLine(false);
    m_options->setShowMaxEnvelopeFill(false);
    m_options->setShowMaxEnvelopeLine(false);
    m_options->setShowCellBoundaries(false);
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

    // Lifetime: drop external references before any project teardown begins.
    if (m_projectWindow) {
        connect(m_projectWindow, &SWMMVisProjectWindow::aboutToClose,
                this, [this] {
            removeOverlay();   // detach from the scene before it's torn down
            close();
        });
    }
}

RasterProfilePlotDialog::~RasterProfilePlotDialog()
{
    removeOverlay();
}

void RasterProfilePlotDialog::setVerticalFactor(double vertFactor)
{
    if (qFuzzyCompare(m_vertFactor, vertFactor)) return;
    m_vertFactor = vertFactor;
    rebuildProfile();
}

void RasterProfilePlotDialog::buildLayout()
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

    // Axis labels from the project unit system — the sampler already converted
    // raw DEM Z into model vertical units via the terrain factor.
    auto *us = m_projectWindow ? m_projectWindow->unitSystem() : UnitSystem::instance();
    const QString unit = us ? us->lengthLabel() : QString();
    m_plot->setAxisLabels(unit.isEmpty() ? tr("Distance")  : tr("Distance (%1)").arg(unit),
                          unit.isEmpty() ? tr("Elevation") : tr("Elevation (%1)").arg(unit));

    connect(actSelect,  &QAction::triggered, this, [this] { m_plot->setMode(MeshProfilePlotWidget::Mode::Identify); });
    connect(actZoomIn,  &QAction::triggered, this, [this] { m_plot->setMode(MeshProfilePlotWidget::Mode::ZoomIn); });
    connect(actZoomOut, &QAction::triggered, this, [this] { m_plot->setMode(MeshProfilePlotWidget::Mode::ZoomOut); });
    connect(actPan,     &QAction::triggered, this, [this] { m_plot->setMode(MeshProfilePlotWidget::Mode::Pan); });
    connect(actFit,     &QAction::triggered, this, [this] { m_plot->fitToExtent(); });
    connect(actMapMarker, &QAction::toggled, this, [this](bool on) { setMarkerToolActive(on); });
    connect(actOptions, &QAction::triggered, this, &RasterProfilePlotDialog::openDisplayOptions);
}

void RasterProfilePlotDialog::rebuildProfile()
{
    if (!m_raster) {
        // The DEM was removed from the canvas — blank the chart rather than
        // leave a stale ground line that no longer has a source.
        m_profile = {};
        m_plot->setProfile(m_profile);
        return;
    }
    const SpatialReferenceSystem *canvasSRS =
        (m_projectWindow && m_projectWindow->canvas())
            ? m_projectWindow->canvas()->canvasSRS()
            : nullptr;

    m_profile = RasterProfileSampler::buildRasterProfile(
        m_raster->filePath(), m_raster->renderBand(), canvasSRS,
        m_scenePolyline, m_vertFactor);
    m_plot->setProfile(m_profile);
}

void RasterProfilePlotDialog::setupMapOverlay()
{
    if (!m_projectWindow || m_scenePolyline.size() < 2) return;
    MapCanvas *canvas = m_projectWindow->canvas();
    if (!canvas) return;

    // Persistent profile line on the map (cleared when this dialog closes).
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

void RasterProfilePlotDialog::setMarkerToolActive(bool on)
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

void RasterProfilePlotDialog::removeOverlay()
{
    if (m_projectWindow && m_projectWindow->canvas()) {
        MapCanvas *canvas = m_projectWindow->canvas();
        // Restore the canvas tool if we left the marker tool active.
        if (m_markerTool && canvas->activeTool() == m_markerTool)
            canvas->setActiveTool(m_prevTool ? m_prevTool.data() : nullptr);
        // Detach from the canvas (which repaints) before the overlay is freed —
        // but only when the binding is still ours. The canvas holds one overlay
        // slot, so a later trace (a mesh profile, say) may already have
        // displaced us; clearing then would erase ITS line off the map.
        if (canvas->meshProfileOverlay() == m_overlay)
            canvas->setMeshProfileOverlay(nullptr);
    }
    if (m_markerTool) m_markerTool->setOverlay(nullptr);
    delete m_overlay;        // not a QObject — manual delete after detaching
    m_overlay = nullptr;
}

void RasterProfilePlotDialog::openDisplayOptions()
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
