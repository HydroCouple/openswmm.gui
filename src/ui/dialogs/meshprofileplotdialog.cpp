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
#include "plot/meshprofiletrackswidget.h"
#include "plot/meshprofileserieseditor.h"
#include "map/spatialreferencesystem.h"
#include <QEvent>
#include <QUuid>
#include <QScrollArea>
#include <QSplitter>
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
#include <QPushButton>
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

    m_definition.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_definition.title = tr("2D section");
    m_definition.scenePolyline = m_scenePolyline;
    auto *sceneSrs = m_projectWindow && m_projectWindow->canvas() ? m_projectWindow->canvas()->canvasSRS() : (m_results ? m_results->srs() : (m_mesh ? m_mesh->srs() : nullptr));
    if (sceneSrs) m_definition.sceneCRS = sceneSrs->toWkt();
    // Existing terrain/water samples are expressed in mesh coordinate units.
    auto *units = m_projectWindow ? m_projectWindow->unitSystem() : UnitSystem::instance();
    m_mapUnitsPerMetre = m_results ? m_results->depthToMeshUnits()
        : ((m_mesh && m_mesh->meshUnitsSI()) || (units && units->isSI()) ? 1.0 : 1.0/0.3048);
    const double toMap = m_mapUnitsPerMetre;
    m_definition.elevationUnits = qAbs(toMap - 1.0/0.3048) < 1e-5 ? QStringLiteral("ft") : QStringLiteral("m");
    m_definition.horizontalUnits = sceneSrs ? sceneSrs->linearUnitsName() : m_definition.elevationUnits;
    if (m_results) {
        m_definition.primarySourceId = sourceId(m_results);
        m_definition.sources.append({m_definition.primarySourceId, m_results->source() ? m_results->source()->sourcePath() : QString(), {}});
    } else if (m_mesh) {
        m_definition.primarySourceId = QStringLiteral("mesh:") + m_mesh->layerId();
        m_definition.sources.append({m_definition.primarySourceId,m_mesh->sourcePath(),{}});
    }
    buildLayout();
    setResultSources(m_results ? QList<SWMM2DResultsLayer *>{m_results.data()} : QList<SWMM2DResultsLayer *>{});
    rebuildProfile();
    setupMapOverlay();

    // Animate the depth column off the 2D results layer's frame changes —
    // the global AnimationController advances the layer (for visible layers),
    // which emits currentTimeChanged / currentDateTimeChanged.
    if (m_results) {
        connect(m_results, &SWMM2DResultsLayer::currentDateTimeChanged,
                this, [this](const QDateTime &dt) { m_plot->setCurrentDateTime(dt); });
        // The temporal maximum is fitted by the layer, in the background when
        // its worker is on. Ask for it whenever frames arrive or change and
        // apply it when the layer reports it complete.
        connect(m_results, &SWMM2DResultsLayer::envelopeReady,
                this, &MeshProfilePlotDialog::applyEnvelope);
        connect(m_results, &SWMM2DResultsLayer::timeRangeChanged, this, [this] {
            // A replaced or lost source changes the stations themselves; new
            // frames on the same geometry only extend the maximum.
            if (m_results && m_profile.geometryRevision != m_results->geomRevision())
                rebuildProfile();
            else
                requestEnvelope();
        });
        connect(m_results, &SWMM2DResultsLayer::waterDisplayPolicyChanged,
                this, &MeshProfilePlotDialog::rebuildProfile);
        connect(m_results, &SWMM2DResultsLayer::currentTimeChanged,
                this, [this](int index) {
            if (!m_settingTimeFromAnimation && m_results && m_results->source()) m_requestedTime = m_results->source()->simTimeAt(index);
            refreshCurrentDepths();
            // A replacement live frame may reduce the maximum without
            // extending the time range. Refresh that envelope as well.
            if (m_results->source() && m_results->source()->isLive()) requestEnvelope();
        });

        // Drive our own layer from the global animation clock so the profile
        // animates even when the layer is hidden. The canvas only advances
        // VISIBLE 2D layers per frame (swmmvis.cpp), so a profile on a hidden
        // layer would otherwise freeze. setCurrentSimTime snaps to the nearest
        // frame and re-emits currentTimeChanged (consumed above); it's a no-op
        // when the layer is already on that frame, so this can't double-work.
        if (m_anim) {
            connect(m_anim, &AnimationController::currentTimeChanged,
                    this, [this](const QDateTime &dt) {
                m_requestedTime = dt;
                if (isMinimized()) return;   // caught up in changeEvent on restore
                m_settingTimeFromAnimation = true;
                m_seriesRefreshed = false;
                if (m_results) m_results->setCurrentSimTimeAsOf(dt);
                m_settingTimeFromAnimation = false;
                // A frame change above has refreshed the series already.
                if (!m_seriesRefreshed) refreshSectionSeries();
            });
        }

        // Initial timestamp + depths for the frame already showing.
        if (auto *src = m_results->source()) {
            const int t = m_results->currentTimeIndex();
            if (t >= 0) { m_plot->setCurrentDateTime(src->simTimeAt(t)); m_requestedTime = src->simTimeAt(t); refreshSectionSeries(); }
        }
    }

    // Lifetime: drop external references before any project teardown begins.
    if (m_projectWindow) {
        connect(m_projectWindow, &SWMMVisProjectWindow::aboutToClose,
                this, [this] {
            if (m_anim)    disconnect(m_anim.data(),    nullptr, this, nullptr);
            if (m_results) disconnect(m_results.data(), nullptr, this, nullptr);
            m_contextValid = false;
            for (const auto &source : m_sectionSources) if (source) disconnect(source.data(),nullptr,this,nullptr);
            m_sectionSources.clear(); m_results.clear(); m_mesh.clear();
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
    actFit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Home));
    actFit->setShortcutContext(Qt::WidgetWithChildrenShortcut);
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
    auto *actSectionSettings = toolbar->addAction(
        openswmmvis::ui::IconFactory::icon(QStringLiteral("ResultSources")),
        tr("Section settings"));
    // Match the 1D profile's named, persisted show/hide toolbar actions.
    actSectionSettings->setObjectName(QStringLiteral("showSectionSettings"));
    actSectionSettings->setToolTip(tr("Show or hide section title, series, styles and samples"));
    actSectionSettings->setCheckable(true);
    actSectionSettings->setChecked(false);
    root->addWidget(toolbar);

    m_figure = new QWidget(this);
    auto *figureLayout = new QVBoxLayout(m_figure); figureLayout->setContentsMargins(0,0,0,0);
    m_plot = new MeshProfilePlotWidget(m_figure);
    m_plot->setOptions(m_options); figureLayout->addWidget(m_plot,1);
    m_tracks = new MeshProfileTracksWidget(m_figure); figureLayout->addWidget(m_tracks);
    auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setWidget(m_figure);
    scroll->setAccessibleName(tr("Elevation and scalar section figure"));
    root->addWidget(scroll, /*stretch=*/1);
    auto *sectionSettings = new QWidget(this);
    sectionSettings->setObjectName(QStringLiteral("sectionSettings"));
    auto *sectionLayout = new QVBoxLayout(sectionSettings);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    buildSectionControls(sectionLayout);
    root->addWidget(sectionSettings);
    sectionSettings->setVisible(actSectionSettings->isChecked());
    connect(actSectionSettings, &QAction::toggled, sectionSettings, &QWidget::setVisible);
    connect(m_plot,&MeshProfilePlotWidget::viewRangeChanged,m_tracks,&MeshProfileTracksWidget::setViewRange);
    connect(m_plot,&MeshProfilePlotWidget::cursorChainageChanged,m_tracks,&MeshProfileTracksWidget::setCursorChainage);
    connect(m_tracks,&MeshProfileTracksWidget::cursorChainageChanged,this,[this](double station) { m_plot->setCursorChainage(station); emit m_plot->cursorChainageChanged(station); });

    // Close row, as ProfilePlotDialog has. Wired to close() so every exit
    // (button, Esc, title bar) takes the same path.
    auto *closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    closeBox->setObjectName(QStringLiteral("meshprof_closeBox"));
    auto *closeBtn = closeBox->button(QDialogButtonBox::Close);
    closeBtn->setObjectName(QStringLiteral("meshprof_closeBtn"));
    closeBtn->setAutoDefault(false);
    closeBtn->setDefault(false);
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::close);
    root->addWidget(closeBox);

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
    if (!m_mesh && !m_results) { m_profile = {}; m_plot->setProfile(m_profile); refreshSectionSeries(); return; }
    m_profile = MeshProfileSampler::buildMeshProfile(
        m_mesh.data(), m_results.data(), m_scenePolyline, 0.0, false);
    const double toMap = m_results ? m_results->depthToMeshUnits() : m_mapUnitsPerMetre;
    const double toAxis = m_definition.elevationUnits == QLatin1String("ft") ? 1.0/0.3048 : 1.0;
    m_verticalScale = toMap > 0 ? toAxis/toMap : 1.0;
    for (auto &sample : m_profile.samples) {
        sample.ground *= m_verticalScale; sample.depthNow *= m_verticalScale; sample.maxDepth *= m_verticalScale;
        sample.signedDepthNow *= m_verticalScale; sample.signedMaxDepth *= m_verticalScale;
    }
    for (auto &crossing : m_profile.crossings) crossing.ground *= m_verticalScale;
    m_rebuildingProfile = true;
    requestEnvelope();   // applied right here when it is already complete
    m_rebuildingProfile = false;
    refreshSectionSeries();
}

void MeshProfilePlotDialog::requestEnvelope()
{
    if (!m_results || !m_profile.exactWaterGeometry) return;
    // Complete already (always so without the worker): sample it now.
    // Otherwise the layer builds it and envelopeReady() applies it.
    if (m_results->envelopeComplete()) applyEnvelope();
    else m_results->requestEnvelope();
}

void MeshProfilePlotDialog::applyEnvelope()
{
    if (!m_results || !m_profile.exactWaterGeometry
        || m_profile.geometryRevision != m_results->geomRevision()) return;
    MeshProfileSampler::applyMaximum(m_profile, m_results.data(), m_verticalScale);
    if (!m_rebuildingProfile) refreshSectionSeries();
}

void MeshProfilePlotDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    // Animation ticks are ignored while minimized; show the requested time.
    if (event->type() == QEvent::WindowStateChange && !isMinimized()
        && m_results && m_requestedTime.isValid()) {
        m_settingTimeFromAnimation = true;
        m_results->setCurrentSimTimeAsOf(m_requestedTime);
        m_settingTimeFromAnimation = false;
        refreshSectionSeries();
    }
}

void MeshProfilePlotDialog::refreshCurrentDepths()
{
    if (!m_results || m_profile.samples.isEmpty()) return;
    if (m_profile.exactWaterGeometry && m_profile.geometryRevision != m_results->geomRevision()) {
        rebuildProfile();
        return;
    }
    QVector<double> depths, signedDepths;
    QVector<bool> hasSurface;
    const double scale = m_results->depthToMeshUnits() * m_verticalScale;
    for (auto& s : m_profile.samples) {
        s.signedDepthNow = m_profile.exactWaterGeometry
            ? MeshProfileSampler::signedWaterDepth(m_results,s.displayTriIdx,s.boundaryTriIdx,s.scenePt)*scale
            : m_results->depthAtCellInterp(s.triIdx,s.scenePt)*scale;
        s.depthNow = std::max(0.0,s.signedDepthNow);
        s.cellHasSurface = m_profile.exactWaterGeometry
            ? std::isfinite(s.signedDepthNow) : m_results->cellHasSurface(s.triIdx);
        depths.push_back(s.depthNow);
        signedDepths.push_back(s.signedDepthNow);
        hasSurface.push_back(s.cellHasSurface);
    }
    m_plot->setCurrentDepths(depths,hasSurface,signedDepths);
    refreshSectionSeries();
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
