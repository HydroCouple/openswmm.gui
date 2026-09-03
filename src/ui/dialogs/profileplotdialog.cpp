/*!
 * \file   profileplotdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/profileplotdialog.h"
#include "ui/theme/iconfactory.h"
#include "ui/dialogs/dialoglayoutpersistence.h"

#include "animation/animationcontroller.h"
#include "layers/gisrasterlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "plot/profileattributesampler.h"
#include "plot/profileattributetrackoptions.h"
#include "plot/profileattributetrackswidget.h"
#include "plot/profilenetworkadapter.h"
#include "plot/profileplotoptions.h"
#include "plot/profilesourcefetcher.h"
#include "swmmvisprojectwindow.h"
#include "core/unitsystem.h"
#include "plot/plotattribute.h"
#include "ui/dialogs/profileoptionsdialog.h"
#include "ui/dialogs/profileresultsources.h"
#include "ui/widgets/attributepickermenu.h"
#include "ui/widgets/profilelayerpanel.h"
#include "selection/selectionmanager.h"

#include <ogr_spatialref.h>

#include <algorithm>
#include <limits>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QFutureWatcher>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QSettings>
#include <QToolButton>
#include <QtConcurrent>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
/*! QSettings group holding the attribute-tracks selection + styling. One
 *  app-wide group (not per-model): which attributes an engineer inspects is
 *  a personal working preference, like the profile's layer toggles. */
const char *const kTrackSettingsGroup = "ProfilePlot/AttributeTracks";

/*! Smallest height the attribute-tracks pane can be dragged to, and the
 *  threshold below which a shown pane counts as "not really visible" and is
 *  re-expanded. Roughly one track row plus its axis. */
constexpr int kTracksPaneMinHeightPx = 72;
} // namespace

namespace {

// Tiny color-chip swatch for the sources-panel rows.
QIcon chipIcon(const QColor &c, int sizePx = 14)
{
    QPixmap p(sizePx, sizePx);
    p.fill(Qt::transparent);
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing, true);
    g.setBrush(c);
    g.setPen(QPen(c.darker(140), 1));
    g.drawEllipse(1, 1, sizePx - 2, sizePx - 2);
    return QIcon(p);
}

} // namespace

ProfilePlotDialog::ProfilePlotDialog(SWMMModelLayer              *model,
                                     AnimationController         *anim,
                                     const ProfileRouter::Path   &path,
                                     SWMMVisProjectWindow        *projectWindow,
                                     QWidget                     *parent)
    : QDialog(parent),
      m_model(model),
      m_canvas(projectWindow ? projectWindow->canvas() : nullptr),
      m_projectWindow(projectWindow),
      m_anim(anim),
      m_routerPath(path)
{
    setWindowTitle(tr("Profile Plot"));
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("ProfilePlotDialog"));
    setModal(false);
    // Single options object — shared by the layer panel (visibility
    // checkboxes), the plot widget (theming + legend), and the property-
    // model-driven Display Options dialog.  Any setter on it propagates
    // through the `changed()` signal.
    m_options = new ProfilePlotOptions(this);
    // Attribute-tracks options — restored from settings before buildLayout()
    // so the menu checks and pane visibility come up as the user left them.
    // (readFrom emits changed() but nothing is connected yet — harmless.)
    m_trackOptions = new ProfileAttributeTrackOptions(this);
    {
        QSettings s;
        s.beginGroup(QLatin1String(kTrackSettingsGroup));
        m_trackOptions->readFrom(s);
        s.endGroup();
    }
    // Promote the dialog to a regular top-level window so the OS gives it
    // minimize / maximize / zoom controls instead of the macOS "panel"
    // treatment that QDialog's default flags trigger.
    // WindowStaysOnTopHint keeps the dialog visible while the user
    // interacts with the main window's animation toolbar.
    setWindowFlags(Qt::Window
                   | Qt::WindowSystemMenuHint
                   | Qt::WindowTitleHint
                   | Qt::WindowMinMaxButtonsHint
                   | Qt::WindowCloseButtonHint
                   | openswmmvis::ui::stayAboveAppFlags());
    resize(960, 560);

    // Materialize static path geometry from the model.  Terrain sampling
    // is opt-in (driven by the layer panel's "Use terrain" toggle); see
    // `rebuildTerrainSamples()`, which queries the project window
    // dynamically so the ground line follows whichever terrain raster is
    // active right now — not whichever one was active at construction.
    m_pathStatic = ProfileNetworkAdapter::buildPathStaticFromModel(model, path);

    buildLayout();
    populateSourcesPanel();
    rebindSources();

    // Initial cursor from the controller.
    if (m_anim) {
        connect(m_anim, &AnimationController::currentTimeChanged,
                this,   &ProfilePlotDialog::onAnimationTimeChanged);
        // Push the current time so the lines render on dialog open.
        if (auto *primary = m_anim->primaryLayer())
            onAnimationTimeChanged(primary->currentDateTime());
    }
    if (m_canvas) {
        auto refreshForResultLayerChange = [this](OpenSWMMVisLayer *layer) {
            if (!qobject_cast<SWMMResultsLayer *>(layer)) return;
            populateSourcesPanel();
            rebindSources();
        };
        connect(m_canvas, &MapCanvas::layerAdded,
                this, refreshForResultLayerChange);
        connect(m_canvas, &MapCanvas::layerRemoved,
                this, refreshForResultLayerChange);
    }

    // Lifetime: this dialog is a top-level window parented to its project
    // sub-window (see SWMMVis::openProfilePlotFor — parentage keeps it in
    // the Qt object tree so it closes with its document), and it holds
    // raw pointers to
    // the primary project's model layer, animation controller, project
    // window, plus result-layer pointers belonging to *other* projects
    // when the user opts into multi-source comparison.  Each owning
    // SWMMVisProjectWindow emits `aboutToClose()` from its closeEvent
    // BEFORE Qt's teardown chain begins — connecting there lets us drop
    // references while everything is still alive.  Subscribing to
    // `destroyed()` is too late: the model layer / canvas / results
    // layers are already gone by then, so any handler that touches them
    // would crash.  Subscriptions for secondary-source projects are
    // added in populateSourcesPanel() when each layer is discovered.
    subscribeProjectClose(m_projectWindow);
}

ProfilePlotDialog::~ProfilePlotDialog()
{
    // Defensive teardown — when this dialog dies during a multi-window
    // close cascade (e.g. main window closeEvent walks topLevelWidgets
    // and closes every ProfilePlotDialog while the closing project
    // window is still emitting signals in the same event-loop tick),
    // Qt's automatic disconnect-on-destruction has a small window where
    // a signal already in the queue can still be delivered to a moved-
    // from receiver.  Disconnect every external sender up-front so any
    // late delivery is a no-op against this object.
    if (m_anim)          disconnect(m_anim.data(),          nullptr, this, nullptr);
    if (m_model)         disconnect(m_model.data(),         nullptr, this, nullptr);
    if (m_canvas)        disconnect(m_canvas.data(),        nullptr, this, nullptr);
    if (m_projectWindow) disconnect(m_projectWindow.data(), nullptr, this, nullptr);
    for (auto *pw : std::as_const(m_observedProjects)) {
        if (pw && pw != m_projectWindow.data())
            disconnect(pw, nullptr, this, nullptr);
    }
    m_observedProjects.clear();
    // Drop layer references in the menu hash and pre-emptively cut every
    // signal from the layer side so a late `destroyed()` delivery cannot
    // fire our removeAction lambda against a half-destroyed dialog.
    for (auto it = m_actionLayer.begin(); it != m_actionLayer.end(); ++it) {
        if (auto *layer = it.value().data())
            disconnect(layer, nullptr, this, nullptr);
    }
    m_actionLayer.clear();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void ProfilePlotDialog::buildLayout()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ── Toolbar ──────────────────────────────────────────────────────────
    // Re-use the main app's SVG icons (defined in resources/swmmvis.qrc) so
    // the dialog matches the look of the map toolbar.
    auto *toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(20, 20));
    // Explicit Select (Identify) toggle: the default plot mode, but
    // implicit "no button checked = identify" was confusing for users.
    // Listed first so it reads as the home / resting state.
    auto *actSelect  = toolbar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("Select")),
                                          tr("Select"));
    auto *actZoomIn  = toolbar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("ZoomIn")),
                                          tr("Zoom In"));
    auto *actZoomOut = toolbar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("ZoomOut")),
                                          tr("Zoom Out"));
    auto *actFit     = toolbar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("Extent")),
                                          tr("Fit to Path"));
    toolbar->addSeparator();
    auto *actPan     = toolbar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("Move")),
                                          tr("Pan"));
    actSelect ->setCheckable(true);
    actSelect ->setChecked(true);       // default mode
    actSelect ->setToolTip(tr("Select  —  click an element to identify / select"));
    actZoomIn ->setCheckable(true);
    actZoomOut->setCheckable(true);
    actPan    ->setCheckable(true);
    actZoomIn ->setToolTip(tr("Zoom In  —  click or drag a rectangle"));
    actZoomOut->setToolTip(tr("Zoom Out  —  click or drag a rectangle"));
    actFit    ->setShortcut(QKeySequence(Qt::Key_Home));
    // Mutually-exclusive mode group — Qt handles the un-checking so our
    // toggled() handlers don't cross-call each other and recurse into a
    // stack overflow when the user switches modes.
    auto *modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addAction(actSelect);
    modeGroup->addAction(actZoomIn);
    modeGroup->addAction(actZoomOut);
    modeGroup->addAction(actPan);
    toolbar->addSeparator();
    // Sources drop-down — replaces the old right-side sources panel.
    // Each result layer in the project's AnimationController gets a
    // checkable QAction; the button's text shows a (selected/total)
    // summary, updated whenever the popup is dismissed.
    m_sourceButton = new QToolButton(toolbar);
    m_sourceButton->setText(tr("Sources"));
    m_sourceButton->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("ResultSources")));
    m_sourceButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_sourceButton->setPopupMode(QToolButton::InstantPopup);
    m_sourceMenu = new QMenu(m_sourceButton);
    m_sourceButton->setMenu(m_sourceMenu);
    toolbar->addWidget(m_sourceButton);
    // Quick toggle for the 2D inundation overlay; mirrors the
    // show2DInundation option (also in Display Options).
    m_actShow2D = toolbar->addAction(
        openswmmvis::ui::IconFactory::icon(QStringLiteral("Inundation2D")),
        tr("2D Inundation"));
    m_actShow2D->setObjectName(QStringLiteral("show2DInundation"));
    m_actShow2D->setToolTip(tr("Overlay the active 2D results layer's water "
                               "surface (mesh bed + interpolated depth) along "
                               "the profile, animated with the cursor."));
    m_actShow2D->setCheckable(true);
    m_actShow2D->setChecked(m_options->show2DInundation());
    connect(m_actShow2D, &QAction::toggled, this, [this](bool on) {
        m_options->setShow2DInundation(on);   // options.changed → rebuild
    });
    toolbar->addSeparator();
    auto *actExport  = toolbar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("ExportImage")),
                                          tr("Export PNG…"));
    toolbar->addSeparator();
    auto *actOptions = toolbar->addAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("ChartProperties")),
                                          tr("Display Options…"));
    root->addWidget(toolbar);

    // Header banner with the path summary.
    QString header = tr("Path: %1 nodes, %2 links")
                         .arg(m_pathStatic.nodes.size())
                         .arg(m_pathStatic.links.size());
    if (!m_pathStatic.nodes.isEmpty() && !m_pathStatic.chainage.isEmpty()) {
        header += tr("  ·  Length: %1")
                      .arg(m_pathStatic.chainage.last(), 0, 'f', 1);
    }
    auto *headerLabel = new QLabel(header, this);
    root->addWidget(headerLabel);

    // Centre row: plot + right column.
    auto *centre = new QHBoxLayout;
    centre->setSpacing(6);
    root->addLayout(centre, /*stretch=*/1);

    m_plot = new ProfilePlotWidget(this);
    m_plot->setPath(m_pathStatic);
    m_plot->setOptions(m_options);

    // Attribute-tracks pane: the profile and the tracks share a vertical
    // splitter. The splitter's objectName is load-bearing — the app-wide
    // DialogLayoutWatcher persists named splitter state (incl. the
    // collapsed position) under Dialogs/ProfilePlotDialog/splitter/….
    m_profileSplit = new QSplitter(Qt::Vertical, this);
    m_profileSplit->setObjectName(QStringLiteral("profileSplit"));
    m_profileSplit->setChildrenCollapsible(false);
    // The plot goes in through a holder whose right margin absorbs the tracks
    // scroll area's vertical scrollbar. Both panes map x as
    // `left + frac * (width - leftGutter - rightGutter)`, so column alignment
    // holds only while their widths agree — and `setWidgetResizable(true)`
    // shrinks the tracks widget to the VIEWPORT, i.e. by the scrollbar width
    // the moment the pane has to scroll (which is exactly what the scroll area
    // is here for). Without this the last node sits a full scrollbar-width
    // off between the panes. Zero on styles with transient/overlay scrollbars.
    m_plotHolder = new QWidget(m_profileSplit);
    auto *plotHolderLayout = new QHBoxLayout(m_plotHolder);
    plotHolderLayout->setContentsMargins(0, 0, 0, 0);
    plotHolderLayout->setSpacing(0);
    plotHolderLayout->addWidget(m_plot);
    m_profileSplit->addWidget(m_plotHolder);
    m_tracks = new ProfileAttributeTracksWidget;
    m_tracks->setOptions(m_trackOptions);
    // Scroll container: each track demands a fixed minimum height, and all
    // 11 attributes at once would otherwise force the DIALOG taller than
    // the screen. Inside a scroll area the pane scrolls instead.
    m_tracksScroll = new QScrollArea(m_profileSplit);
    m_tracksScroll->setWidgetResizable(true);
    m_tracksScroll->setFrameShape(QFrame::NoFrame);
    m_tracksScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tracksScroll->setWidget(m_tracks);
    m_profileSplit->addWidget(m_tracksScroll);
    // The scrollbar comes and goes as tracks are added / the pane is dragged;
    // the tracks widget resizes exactly when it does, so watch that.
    m_tracks->installEventFilter(this);
    // Neither pane collapses by dragging. The tracks pane used to be
    // collapsible, with the master toggle unchecked once it hit zero — but
    // a mere click on the handle grip could snap the pane to zero and hide
    // it, and a persisted zero-height split made "Show tracks" appear to do
    // nothing. The toggle is now the only hide affordance; the drag floor is
    // the pane's minimum height.
    m_profileSplit->setCollapsible(0, false);
    m_profileSplit->setCollapsible(1, false);
    m_tracksScroll->setMinimumHeight(kTracksPaneMinHeightPx);
    m_profileSplit->setStretchFactor(0, 3);
    m_profileSplit->setStretchFactor(1, 1);
    centre->addWidget(m_profileSplit, /*stretch=*/1);

    buildAttributeTracksUi(toolbar);

    // Distance / elevation axis labels — pulled from the active project's
    // UnitSystem so the suffix matches the rest of the GUI.  Re-applied
    // whenever FLOW_UNITS changes so the chart never lies about its units.
    auto applyAxisLabels = [this]() {
        auto *us = m_projectWindow ? m_projectWindow->unitSystem()
                                   : UnitSystem::instance();
        const QString unit = us ? us->lengthLabel() : QString();
        const QString xLab = unit.isEmpty() ? tr("Distance")
                                            : tr("Distance (%1)").arg(unit);
        const QString yLab = unit.isEmpty() ? tr("Elevation")
                                            : tr("Elevation (%1)").arg(unit);
        m_plot->setAxisLabels(xLab, yLab);
    };
    applyAxisLabels();
    if (auto *us = m_projectWindow ? m_projectWindow->unitSystem()
                                   : UnitSystem::instance()) {
        connect(us, &UnitSystem::unitsChanged,
                this, [applyAxisLabels](swmm_FlowUnitsProperty) {
                    applyAxisLabels();
                });
    }

    // The right column used to host a "Sources" GroupBox + the per-source
    // visibility checkboxes.  Sources are now driven by a drop-down on
    // the toolbar (m_sourceButton, populated below), and every other
    // toggle lives in the Display Options dialog backed by QPropertyModel.
    // So the plot widget takes the full width.

    // Helpers: marshall LayerToggles ⇄ ProfilePlotOptions so the panel,
    // the plot widget, and the property-model-driven Display Options
    // dialog stay in sync.
    auto togglesFromOptions = [this](ProfilePlotOptions *o) {
        ProfilePlotWidget::LayerToggles t;
        t.currentHglLine   = o->currentHglLine();
        t.currentHglFill   = o->currentHglFill();
        t.currentEgl       = o->currentEgl();
        t.maxHglBand       = o->maxHglBand();
        t.maxHglLine       = o->maxHglLine();
        t.maxEglLine       = o->maxEglLine();
        t.showNodeLabels   = o->showNodeLabels();
        t.showLinkLabels   = o->showLinkLabels();
        t.inlineNodeLabels = o->inlineNodeLabels();
        t.labelOrientation = static_cast<ProfilePlotWidget::LayerToggles::LabelOrientation>(o->labelOrientation());
        t.labelAngleDeg    = o->labelAngleDeg();
        // The widget draws `terrainSamples` as the ground whenever this is
        // set — DEM or 2D mesh alike; only NodeRims falls back to rims.
        t.useTerrainGround = (resolvedGroundSource() != ProfilePlotOptions::NodeRims);
        return t;
    };
    auto applyTogglesToOptions = [](ProfilePlotOptions *o,
                                    const ProfilePlotWidget::LayerToggles &t) {
        o->setCurrentHglLine  (t.currentHglLine);
        o->setCurrentHglFill  (t.currentHglFill);
        o->setCurrentEgl      (t.currentEgl);
        o->setMaxHglBand      (t.maxHglBand);
        o->setMaxHglLine      (t.maxHglLine);
        o->setMaxEglLine      (t.maxEglLine);
        o->setShowNodeLabels  (t.showNodeLabels);
        o->setShowLinkLabels  (t.showLinkLabels);
        o->setInlineNodeLabels(t.inlineNodeLabels);
        o->setLabelOrientation(static_cast<ProfilePlotOptions::LabelOrientation>(t.labelOrientation));
        o->setLabelAngleDeg   (t.labelAngleDeg);
        o->setGroundSource    (t.useTerrainGround ? ProfilePlotOptions::TerrainDEM
                                                  : ProfilePlotOptions::Auto);
    };

    Q_UNUSED(applyTogglesToOptions);  // panel-side sync removed; kept the
                                       // lambda for future use.

    // Initial ground line: Auto samples the 2D mesh when the project has
    // one, so the first paint already shows the mesh surface between nodes.
    rebuildTerrainSamples();
    m_plot->setPath(m_pathStatic);
    // Initial push: options → plot widget (visibility / labels / terrain).
    m_plot->setLayerToggles(togglesFromOptions(m_options));
    // Record the plot-level styles as they stand now, so the FIRST edit is
    // recognised as an edit. Seeding deliberately restyles nothing — opening
    // the dialog must not stamp defaults over each source's saved style.
    pushEditedPlotStylesToSources();

    // Options → plot.  Drives visibility, label rendering, and the
    // ground re-sample whenever the resolved ground source changes.
    // Also reruns rebindSources so per-output visibility flips propagate
    // to the series list (the widget reads visibility from series, not
    // from LayerToggles).
    connect(m_options, &ProfilePlotOptions::changed, this,
            [this, togglesFromOptions]() {
        const auto t = togglesFromOptions(m_options);
        // Re-sample when the RESOLVED source moves (rims ⇄ mesh ⇄ DEM) —
        // mesh and DEM both set useTerrainGround, so compare the source.
        const bool terrainChanged = (resolvedGroundSource() != m_lastGroundSource);
        m_plot->setLayerToggles(t);
        if (terrainChanged) {
            rebuildTerrainSamples();
            m_plot->setPath(m_pathStatic);
            // setPath rebuilt the virtual-chainage table — re-share it.
            syncTracksAxes();
        }
        // 2D inundation overlay follows its option (Display Options tree or
        // the toolbar toggle — both write the same property).
        const bool want2D = m_options->show2DInundation();
        if (m_actShow2D && m_actShow2D->isChecked() != want2D) {
            QSignalBlocker b(m_actShow2D);
            m_actShow2D->setChecked(want2D);
        }
        if (want2D != (m_surface2DLayer != nullptr))
            rebuildSurface2DStations();
        // A plot-level line style the user just edited has to reach the
        // sources before the series are rebuilt from them — otherwise the
        // edit is invisible (see pushEditedPlotStylesToSources).
        pushEditedPlotStylesToSources();
        rebindSources();
    });

    // Toolbar actions → plot.  ZoomIn / ZoomOut / Pan are mutually-exclusive
    // mode toggles.  A second click on the active mode toggles back to
    // Identify (default).
    // Single triggered() handler reads which group member is now checked
    // and propagates the mode to the plot widget.  Using triggered() (not
    // toggled()) and the action group's automatic un-checking eliminates
    // the recursion that the four-way cross-toggling used to cause.
    connect(modeGroup, &QActionGroup::triggered, this,
            [this, actSelect, actZoomIn, actZoomOut, actPan](QAction *act) {
        ProfilePlotWidget::Mode want = ProfilePlotWidget::Mode::Identify;
        if      (act == actZoomIn)  want = ProfilePlotWidget::Mode::ZoomIn;
        else if (act == actZoomOut) want = ProfilePlotWidget::Mode::ZoomOut;
        else if (act == actPan)     want = ProfilePlotWidget::Mode::Pan;
        else                        want = ProfilePlotWidget::Mode::Identify;
        Q_UNUSED(actSelect);
        m_plot->setMode(want);
    });
    connect(actFit, &QAction::triggered, m_plot, &ProfilePlotWidget::fitToExtent);
    // Display Options dialog — non-modal, QPropertyModel-backed editor
    // for visibility / theming / legend / labels.  Owned by `this` so it
    // disappears with the plot dialog, and given Qt::Tool + StaysOnTop
    // window flags so it floats above the profile plot dialog (which
    // itself stays on top of the main window).  Reusable from the
    // toolbar action and the plot's right-click context menu.
    auto openOptionsDialog = [this]() {
        auto *dlg = new ProfileOptionsDialog(m_options,
                                             m_anim.data(),
                                             m_projectWindow.data(),
                                             this);
        dlg->setTrackOptions(m_trackOptions);   // adds the Attribute Tracks tab
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowFlags(dlg->windowFlags()
                            | openswmmvis::ui::floatingPanelFlags());
        connect(dlg, &ProfileOptionsDialog::sourcesChanged,
                this, [this]() {
            populateSourcesPanel();
            rebindSources();
        });
        connect(dlg, &ProfileOptionsDialog::addOutputFileRequested,
                this, [this, dlg](const QString &path) {
            if (path.isEmpty() || !m_model || !m_anim || !m_projectWindow)
                return;
            // De-dup: a layer for this absolute path may already exist on
            // the project canvas (added previously via "Add SWMM Results"
            // or another profile-dialog session).  Re-use it rather than
            // creating a duplicate that the project tree would show twice.
            const QString canon = QFileInfo(path).absoluteFilePath();
            SWMMResultsLayer *layer = nullptr;
            if (m_canvas) {
                for (OpenSWMMVisLayer *l : m_canvas->layers()) {
                    auto *existing = qobject_cast<SWMMResultsLayer *>(l);
                    if (!existing) continue;
                    if (QFileInfo(existing->resultsFilePath()).absoluteFilePath()
                        == canon) {
                        layer = existing;
                        break;
                    }
                }
            }
            const bool isNew = (layer == nullptr);
            if (isNew) {
                layer = new SWMMResultsLayer(path, m_model.data());
                layer->setName(QFileInfo(path).fileName());
                if (m_canvas) m_canvas->addLayer(layer, /*pushUndo=*/true);
            }
            QList<QString> warnings, errors;
            if (!layer->openResults(warnings, errors)) {
                if (isNew) {
                    if (m_canvas) {
                        const int idx = m_canvas->layers().indexOf(layer);
                        if (idx >= 0) m_canvas->takeLayer(idx, /*pushUndo=*/false);
                    }
                    delete layer;
                }
                return;
            }
            if (!m_anim->allLayers().contains(layer))
                m_anim->addSecondaryLayer(layer);
            populateSourcesPanel();
            rebindSources();
            if (dlg) dlg->refreshSources();
        });
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    };
    connect(actOptions, &QAction::triggered, this, openOptionsDialog);
    connect(actExport,  &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export Profile Plot"),
            QString(), tr("PNG image (*.png)"));
        if (path.isEmpty()) return;
        // Render the splitter contents — profile plus (when visible) the
        // attribute tracks — so the export matches what the user sees.
        QWidget *target = (m_tracks && m_tracks->isVisible())
                              ? static_cast<QWidget *>(m_profileSplit)
                              : static_cast<QWidget *>(m_plot);
        QPixmap pix(target->size());
        pix.fill(Qt::white);
        target->render(&pix);
        pix.save(path, "PNG");
    });

    // Plot click → main-map selection.  Each click replaces the model
    // layer's selection with the single object.
    connect(m_plot, &ProfilePlotWidget::nodeClicked,
            this,   [this](int pathNodeIdx) {
        if (!m_model) return;
        if (pathNodeIdx < 0 || pathNodeIdx >= m_pathStatic.nodes.size()) return;
        m_model->setSelectedElements(
            { { m_pathStatic.nodes[pathNodeIdx].name,
                SWMMModelLayer::kKindNode } });
    });
    connect(m_plot, &ProfilePlotWidget::linkClicked,
            this,   [this](int pathLinkIdx) {
        if (!m_model) return;
        if (pathLinkIdx < 0 || pathLinkIdx >= m_pathStatic.links.size()) return;
        m_model->setSelectedElements(
            { { m_pathStatic.links[pathLinkIdx].name,
                SWMMModelLayer::kKindLink } });
    });
    // Blank-area click in the plot clears the selection on the bound model
    // layer, mirroring the deselect behaviour of an empty-area map click.
    connect(m_plot, &ProfilePlotWidget::backgroundClicked,
            this,   [this]() {
        if (!m_model) return;
        m_model->setSelectedElementNames({});
    });

    // Reverse direction: main-map (or any other surface) selection →
    // highlight the matching glyphs in the profile plot.
    if (m_model) {
        connect(m_model, &SWMMModelLayer::selectionChanged,
                m_plot,  &ProfilePlotWidget::setSelectedElementNames);
        m_plot->setSelectedElementNames(m_model->selectedElementNames());
    }

    // Live terrain refresh — when the user loads, swaps, or changes
    // vertical unit of the active terrain raster, re-sample and repaint.
    if (m_projectWindow) {
        connect(m_projectWindow, &SWMMVisProjectWindow::activeTerrainChanged,
                this, [this](GISRasterLayer *) {
            rebuildTerrainSamples();
            m_plot->setPath(m_pathStatic);
        });
        // 2D inundation overlay follows the Analysis toolbar's active 2D
        // results layer (swap / clear / load-after-open all re-sample).
        connect(m_projectWindow, &SWMMVisProjectWindow::active2DResultsLayerChanged,
                this, [this](SWMM2DResultsLayer *) {
            rebuildSurface2DStations();
        });
    }
    // A mesh layer appearing / disappearing flips what `Auto` resolves to
    // (and what an explicit Mesh2D can sample): re-derive the ground line.
    if (m_canvas) {
        auto onLayersChanged = [this, togglesFromOptions](OpenSWMMVisLayer *l) {
            if (!qobject_cast<SWMM2DMeshLayer *>(l)) return;
            rebuildTerrainSamples();
            m_plot->setLayerToggles(togglesFromOptions(m_options));
            m_plot->setPath(m_pathStatic);
            syncTracksAxes();
            rebuildSurface2DStations();
        };
        connect(m_canvas, &MapCanvas::layerAdded,   this, onLayersChanged);
        connect(m_canvas, &MapCanvas::layerRemoved, this, onLayersChanged);
    }
    rebuildSurface2DStations();

    // Double-click in the plot → zoom the main map to that element.
    auto zoomCanvasToBounds = [this](double xMin, double yMin,
                                     double xMax, double yMax) {
        if (!m_canvas) return;
        if (xMax <= xMin || yMax <= yMin) return;
        const double padX = (xMax - xMin) * 0.30 + 1.0;
        const double padY = (yMax - yMin) * 0.30 + 1.0;
        m_canvas->setExtent(MapExtent(xMin - padX, yMin - padY,
                                      xMax + padX, yMax + padY));
    };
    connect(m_plot, &ProfilePlotWidget::nodeDoubleClicked,
            this,   [this, zoomCanvasToBounds](int idx) {
        if (!m_model) return;
        if (idx < 0 || idx >= m_routerPath.nodes.size()) return;
        double x = 0, y = 0;
        if (!m_model->cachedNodeCoord(m_routerPath.nodes[idx], &x, &y)) return;
        zoomCanvasToBounds(x, y, x, y);
    });
    connect(m_plot, &ProfilePlotWidget::linkDoubleClicked,
            this,   [this, zoomCanvasToBounds](int idx) {
        if (!m_model) return;
        if (idx < 0 || idx >= m_routerPath.linkIds.size()) return;
        const auto poly = m_model->cachedLinkPolyline(m_routerPath.linkIds[idx]);
        if (poly.isEmpty()) return;
        double xMin =  std::numeric_limits<double>::infinity();
        double yMin =  std::numeric_limits<double>::infinity();
        double xMax = -std::numeric_limits<double>::infinity();
        double yMax = -std::numeric_limits<double>::infinity();
        for (const QPointF &v : poly) {
            xMin = std::min(xMin, v.x()); yMin = std::min(yMin, v.y());
            xMax = std::max(xMax, v.x()); yMax = std::max(yMax, v.y());
        }
        zoomCanvasToBounds(xMin, yMin, xMax, yMax);
    });

    // Right-click on a node or link glyph → context menu with "Zoom to on
    // map" + "Properties…".  Both actions select the element on the model
    // layer; "Zoom to" additionally re-centers the main map canvas.  The
    // attribute-panel dock watches the model's selectionChanged signal and
    // populates itself automatically, so "Properties…" is just a select.
    auto zoomToNode = [this, zoomCanvasToBounds](int idx) {
        if (!m_model) return;
        if (idx < 0 || idx >= m_routerPath.nodes.size()) return;
        double x = 0, y = 0;
        if (!m_model->cachedNodeCoord(m_routerPath.nodes[idx], &x, &y)) return;
        zoomCanvasToBounds(x, y, x, y);
    };
    auto zoomToLink = [this, zoomCanvasToBounds](int idx) {
        if (!m_model) return;
        if (idx < 0 || idx >= m_routerPath.linkIds.size()) return;
        const auto poly = m_model->cachedLinkPolyline(m_routerPath.linkIds[idx]);
        if (poly.isEmpty()) return;
        double xMin =  std::numeric_limits<double>::infinity();
        double yMin =  std::numeric_limits<double>::infinity();
        double xMax = -std::numeric_limits<double>::infinity();
        double yMax = -std::numeric_limits<double>::infinity();
        for (const QPointF &v : poly) {
            xMin = std::min(xMin, v.x()); yMin = std::min(yMin, v.y());
            xMax = std::max(xMax, v.x()); yMax = std::max(yMax, v.y());
        }
        zoomCanvasToBounds(xMin, yMin, xMax, yMax);
    };
    auto selectNode = [this](int idx) {
        if (!m_model) return;
        if (idx < 0 || idx >= m_pathStatic.nodes.size()) return;
        m_model->setSelectedElements(
            { { m_pathStatic.nodes[idx].name, SWMMModelLayer::kKindNode } });
    };
    auto selectLink = [this](int idx) {
        if (!m_model) return;
        if (idx < 0 || idx >= m_pathStatic.links.size()) return;
        m_model->setSelectedElements(
            { { m_pathStatic.links[idx].name, SWMMModelLayer::kKindLink } });
    };
    // Resolve the project's current flow-units enum into the
    // `openswmmvis::plot::UnitSystem` value the picker menu uses so the
    // attribute labels (ft³/s vs m³/s, etc.) match the rest of the GUI.
    auto resolvePlotUnitSystem = [this]() {
        auto *us = m_projectWindow ? m_projectWindow->unitSystem()
                                   : UnitSystem::instance();
        return us
            ? openswmmvis::plot::unitSystemFromFlowUnits(us->flowUnits())
            : openswmmvis::plot::UnitSystem::US;
    };
    // Y2b-2 follow-up (amendment D-Y4): the species offered in the picker
    // come from the project's ACTIVE 1D results layer — the same layer the
    // overlay ComparisonPlotDialog will plot against — read live at menu
    // time so a run swap between right-clicks stays honest.
    auto speciesForMenu = [this]() -> QStringList {
        if (!m_projectWindow) return {};
        if (auto *rl = m_projectWindow->activeResultsLayer())
            return rl->speciesNames();
        return {};
    };

    // Right-click "Plot Time Series…" mirrors the map view: instead of a
    // flat action we expose the same attribute-picker submenu (depth,
    // flow, velocity, … plus an "All attributes" sentinel). The picked
    // attribute is forwarded via plotAttributeRequested, which SWMMVis
    // routes into the ComparisonPlotDialog the same way as map clicks.
    connect(m_plot, &ProfilePlotWidget::nodeRightClicked, this,
            [this, zoomToNode, selectNode, openOptionsDialog,
             resolvePlotUnitSystem, speciesForMenu](int idx, const QPoint &globalPos) {
        if (idx < 0 || idx >= m_pathStatic.nodes.size()) return;
        QMenu menu(this);
        QAction *zoomAct = menu.addAction(tr("Zoom to on map"));
        QMenu *plotSubmenu = openswmmvis::ui::AttributePickerMenu::createForObjectKind(
            openswmmvis::plot::ObjectRef::Kind::Node,
            resolvePlotUnitSystem(), &menu, speciesForMenu());
        if (plotSubmenu) {
            plotSubmenu->setTitle(tr("Plot Time Series…"));
            plotSubmenu->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Chart")));
            menu.addMenu(plotSubmenu);
        }
        QAction *propAct = menu.addAction(tr("Properties…"));
        QAction *chosen  = menu.exec(globalPos);
        if (!chosen) return;
        if (chosen == zoomAct) { selectNode(idx); zoomToNode(idx); }
        else if (chosen == propAct) { openOptionsDialog(); }
        else if (plotSubmenu && chosen->parent() == plotSubmenu) {
            selectNode(idx);
            SWMMObjectRef ref;
            ref.objectType = SWMMObjectRef::Node;
            ref.name       = m_pathStatic.nodes[idx].name;
            // descriptorFrom tells fixed / species / sentinel apart —
            // attributeFrom would read a species action as the sentinel.
            emit plotAttributeRequested(
                ref, openswmmvis::ui::AttributePickerMenu::descriptorFrom(chosen));
        }
    });
    connect(m_plot, &ProfilePlotWidget::linkRightClicked, this,
            [this, zoomToLink, selectLink, openOptionsDialog,
             resolvePlotUnitSystem, speciesForMenu](int idx, const QPoint &globalPos) {
        if (idx < 0 || idx >= m_pathStatic.links.size()) return;
        QMenu menu(this);
        QAction *zoomAct = menu.addAction(tr("Zoom to on map"));
        QMenu *plotSubmenu = openswmmvis::ui::AttributePickerMenu::createForObjectKind(
            openswmmvis::plot::ObjectRef::Kind::Link,
            resolvePlotUnitSystem(), &menu, speciesForMenu());
        if (plotSubmenu) {
            plotSubmenu->setTitle(tr("Plot Time Series…"));
            plotSubmenu->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Chart")));
            menu.addMenu(plotSubmenu);
        }
        QAction *propAct = menu.addAction(tr("Properties…"));
        QAction *chosen  = menu.exec(globalPos);
        if (!chosen) return;
        if (chosen == zoomAct) { selectLink(idx); zoomToLink(idx); }
        else if (chosen == propAct) { openOptionsDialog(); }
        else if (plotSubmenu && chosen->parent() == plotSubmenu) {
            selectLink(idx);
            SWMMObjectRef ref;
            ref.objectType = SWMMObjectRef::Link;
            ref.name       = m_pathStatic.links[idx].name;
            // descriptorFrom tells fixed / species / sentinel apart —
            // attributeFrom would read a species action as the sentinel.
            emit plotAttributeRequested(
                ref, openswmmvis::ui::AttributePickerMenu::descriptorFrom(chosen));
        }
    });
    // Right-click on blank profile background still shows a context menu
    // — Properties remains available, but the element-specific Zoom item
    // is greyed out since there's no element under the cursor.
    connect(m_plot, &ProfilePlotWidget::backgroundRightClicked, this,
            [this, openOptionsDialog](const QPoint &globalPos) {
        QMenu menu(this);
        QAction *zoomAct = menu.addAction(tr("Zoom to on map"));
        QAction *propAct = menu.addAction(tr("Properties…"));
        zoomAct->setEnabled(false);
        QAction *chosen = menu.exec(globalPos);
        if (chosen == propAct) { openOptionsDialog(); }
    });

    // Indeterminate loading bar shown while a worker thread fetches
    // per-source NodeHead / NodeDepth / LinkVelocity series from each
    // open `.out` file.  Hidden when idle.
    m_loadingBar = new QProgressBar(this);
    m_loadingBar->setRange(0, 0);              // busy indicator
    m_loadingBar->setTextVisible(true);
    m_loadingBar->setFormat(tr("Loading source data…"));
    m_loadingBar->setVisible(false);
    root->addWidget(m_loadingBar);

    // Close button.
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    root->addWidget(buttons);

    // Source-toggle wiring happens per-action inside populateSourcesPanel().
}

// ---------------------------------------------------------------------------
// Sources panel
// ---------------------------------------------------------------------------

void ProfilePlotDialog::populateSourcesPanel()
{
    if (!m_sourceMenu) return;
    m_sourceMenu->clear();
    m_actionLayer.clear();

    const QList<SWMMResultsLayer *> layers =
        openswmmvis::ui::profileResultSources(m_anim.data(),
                                              m_projectWindow.data(),
                                              m_canvas.data());

    // Read persisted per-layer visibility / colour / name / style from
    // QSettings.  Same key scheme the ProfileOptionsDialog Sources tab
    // uses so the two surfaces stay coherent — toggling either updates
    // the other after a populateSourcesPanel pass.
    //
    // We need per-layer access to the QSettings *array entry* (so the
    // per-source style helper can read its eight sub-keys), so build a
    // file → layer map first and apply style in-stream.  Visibility is
    // stashed in a parallel map and read out at action-creation time.
    QHash<QString, SWMMResultsLayer *> layerByFile;
    for (SWMMResultsLayer *layer : layers) {
        if (!layer) continue;
        layerByFile.insert(layer->resultsFilePath(), layer);
    }
    QHash<QString, bool> visibilityByFile;
    {
        QString projectKey = QStringLiteral("default");
        if (m_projectWindow && m_projectWindow->modelLayer()) {
            const QString p = m_projectWindow->modelLayer()->modelFilePath();
            if (!p.isEmpty()) projectKey = p;
        }
        QSettings settings;
        settings.beginGroup(QStringLiteral("ProfilePlot/Comparison/") + projectKey);
        const int n = settings.beginReadArray(QStringLiteral("sources"));
        for (int i = 0; i < n; ++i) {
            settings.setArrayIndex(i);
            const QString fp = settings.value(QStringLiteral("file")).toString();
            if (fp.isEmpty()) continue;
            visibilityByFile.insert(
                fp, settings.value(QStringLiteral("visible"), true).toBool());
            SWMMResultsLayer *layer = layerByFile.value(fp, nullptr);
            if (!layer) continue;
            // Colour & name first so the per-source style helper sees the
            // final categorical colour if it needs to derive defaults.
            const QString colorName = settings.value(QStringLiteral("color")).toString();
            if (!colorName.isEmpty()) {
                QColor c(colorName);
                if (c.isValid()) layer->setProfileLineColor(c);
            }
            const QString name = settings.value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) layer->setScenarioName(name);
            // Per-source HGL/EGL/Max-HGL/Max-EGL pens & brushes.
            layer->readProfileStyle(settings);
        }
        settings.endArray();
        settings.endGroup();
    }

    int total = 0;
    int checked = 0;
    for (SWMMResultsLayer *layer : layers) {
        if (!layer) continue;
        // Visibility default = checked.
        const bool visibleInit =
            visibilityByFile.value(layer->resultsFilePath(), true);
        const QString label = layer->scenarioName().isEmpty()
                                  ? QFileInfo(layer->resultsFilePath()).completeBaseName()
                                  : layer->scenarioName();
        auto *act = m_sourceMenu->addAction(chipIcon(layer->profileLineColor()),
                                            label);
        act->setCheckable(true);
        act->setChecked(visibleInit);
        if (visibleInit) ++checked;
        act->setToolTip(layer->resultsFilePath());
        m_actionLayer.insert(act, layer);
        connect(act, &QAction::toggled,
                this, &ProfilePlotDialog::onSourceActionToggled);
        // Walk the parent chain to find the SWMMVisProjectWindow that
        // owns this layer (Layer → ModelLayer → ProjectWindow).  This
        // may be the dialog's primary or a foreign project — either way
        // we want to drop the source if that project closes.
        for (QObject *o = layer; o; o = o->parent()) {
            if (auto *pw = qobject_cast<SWMMVisProjectWindow *>(o)) {
                subscribeProjectClose(pw);
                break;
            }
        }
        // Belt-and-suspenders: also watch the layer itself.  If it is
        // destroyed by some path other than a project-window close
        // (e.g. the user manually removes a result layer from the
        // project), drop its menu entry so the dialog never re-binds a
        // dead pointer.  Captured `act` is the menu-owned QAction.
        connect(layer, &QObject::destroyed, this, [this, act](QObject *) {
            m_actionLayer.remove(act);
            if (m_sourceMenu) m_sourceMenu->removeAction(act);
            delete act;
            if (m_sourceButton) {
                int t = 0, on = 0;
                for (auto it = m_actionLayer.begin(); it != m_actionLayer.end(); ++it) {
                    ++t;
                    if (it.key()->isChecked()) ++on;
                }
                m_sourceButton->setText(tr("Sources (%1/%2)").arg(on).arg(t));
            }
            rebindSources();
        });
        ++total;
    }
    m_sourceButton->setText(tr("Sources (%1/%2)").arg(checked).arg(total));

    // The source set defines which species the Tracks menu can offer.
    refreshTracksMenuSpecies();
}

void ProfilePlotDialog::subscribeProjectClose(SWMMVisProjectWindow *pw)
{
    if (!pw) return;
    if (m_observedProjects.contains(pw)) return;      // already wired
    m_observedProjects.insert(pw);
    connect(pw, &SWMMVisProjectWindow::aboutToClose,
            this, [this, pw] { onObservedProjectAboutToClose(pw); });
}

void ProfilePlotDialog::onObservedProjectAboutToClose(SWMMVisProjectWindow *pw)
{
    if (!pw) return;
    // Sever every connection between us and this project window so any
    // signals queued later in the project's teardown become no-ops.
    disconnect(pw, nullptr, this, nullptr);
    m_observedProjects.remove(pw);

    if (pw == m_projectWindow) {
        // The dialog's primary project is going away: every back-pointer
        // owned by that project (model layer, canvas) is about to die.
        // Drop them now and close — WA_DeleteOnClose will tear down the
        // dialog on the next event-loop iteration.
        m_projectWindow = nullptr;
        m_canvas        = nullptr;
        m_model         = nullptr;
        close();
        return;
    }

    // A secondary source project is closing.  Drop every menu action
    // whose layer descends from it, rebuild the button-summary text,
    // and rebind (which re-issues the worker thread fetch against the
    // remaining live sources).  QPointer in m_actionLayer auto-nulls
    // when the layer is freed; we use it here to compute ownership now,
    // while everything is still alive.
    QList<QAction *> toDrop;
    for (auto it = m_actionLayer.begin(); it != m_actionLayer.end(); ++it) {
        QPointer<SWMMResultsLayer> layer = it.value();
        if (!layer) { toDrop.push_back(it.key()); continue; }
        for (QObject *o = layer.data(); o; o = o->parent()) {
            if (o == pw) { toDrop.push_back(it.key()); break; }
        }
    }
    for (QAction *act : toDrop) {
        m_actionLayer.remove(act);
        if (m_sourceMenu) m_sourceMenu->removeAction(act);
        delete act;
    }
    if (m_sourceButton) {
        int total = 0, on = 0;
        for (auto it = m_actionLayer.begin(); it != m_actionLayer.end(); ++it) {
            ++total;
            if (it.key()->isChecked()) ++on;
        }
        m_sourceButton->setText(tr("Sources (%1/%2)").arg(on).arg(total));
    }
    rebindSources();
}

void ProfilePlotDialog::onSourceActionToggled()
{
    // Update the button's summary text.
    int total = 0, on = 0;
    for (auto it = m_actionLayer.begin(); it != m_actionLayer.end(); ++it) {
        ++total;
        if (it.key()->isChecked()) ++on;
    }
    if (m_sourceButton)
        m_sourceButton->setText(tr("Sources (%1/%2)").arg(on).arg(total));

    // Persist the new visibility set under the same key the Sources tab
    // reads, so opening the Display Options dialog now reflects the
    // toolbar's state and vice versa.
    QString projectKey = QStringLiteral("default");
    if (m_projectWindow && m_projectWindow->modelLayer()) {
        const QString p = m_projectWindow->modelLayer()->modelFilePath();
        if (!p.isEmpty()) projectKey = p;
    }
    QSettings settings;
    settings.beginGroup(QStringLiteral("ProfilePlot/Comparison/") + projectKey);
    settings.remove(QStringLiteral("sources"));
    settings.beginWriteArray(QStringLiteral("sources"));
    int idx = 0;
    for (auto it = m_actionLayer.begin(); it != m_actionLayer.end(); ++it) {
        QPointer<SWMMResultsLayer> layer = it.value();
        if (!layer) continue;
        const QString fp = layer->resultsFilePath();
        if (fp.isEmpty()) continue;
        settings.setArrayIndex(idx++);
        settings.setValue(QStringLiteral("file"),    fp);
        settings.setValue(QStringLiteral("visible"), it.key()->isChecked());
        settings.setValue(QStringLiteral("color"),   layer->profileLineColor().name());
        settings.setValue(QStringLiteral("name"),    layer->scenarioName());
        // Per-source HGL/EGL/Max-HGL/Max-EGL pens + brushes — same key
        // names the Display Options Sources tab writes, so the two
        // surfaces share the persistence record.
        layer->writeProfileStyle(settings);
    }
    settings.endArray();
    settings.endGroup();

    rebindSources();
}

// ---------------------------------------------------------------------------
// Re-bind / refresh
// ---------------------------------------------------------------------------

bool ProfilePlotDialog::pushEditedPlotStylesToSources()
{
    if (!m_options) return false;

    // First call after the options object is bound records the baseline; it
    // must not restyle anything, or simply opening the dialog would stamp the
    // plot-level defaults over every source's saved style.
    const bool seeding = !m_lastPlotStyle.seeded;

    struct Entry {
        const QPen   *pen;      // exactly one of pen / brush is set
        const QBrush *brush;
        QPen         *lastPen;
        QBrush       *lastBrush;
        void (SWMMResultsLayer::*setPen)(const QPen &);
        void (SWMMResultsLayer::*setBrush)(const QBrush &);
    };
    const QPen   hglPen    = m_options->hglLinePen();
    const QBrush hglBrush  = m_options->hglFillBrush();
    const QPen   eglPen    = m_options->eglLinePen();
    const QPen   mHglPen   = m_options->maxHglLinePen();
    const QBrush mHglBrush = m_options->maxHglFillBrush();
    const QPen   mEglPen   = m_options->maxEglLinePen();

    const Entry entries[] = {
        {&hglPen,  nullptr,    &m_lastPlotStyle.hglLinePen,    nullptr,
         &SWMMResultsLayer::setProfileHglLinePen,      nullptr},
        {nullptr,  &hglBrush,  nullptr,  &m_lastPlotStyle.hglFillBrush,
         nullptr, &SWMMResultsLayer::setProfileHglFillBrush},
        {&eglPen,  nullptr,    &m_lastPlotStyle.eglLinePen,    nullptr,
         &SWMMResultsLayer::setProfileEglLinePen,      nullptr},
        {&mHglPen, nullptr,    &m_lastPlotStyle.maxHglLinePen, nullptr,
         &SWMMResultsLayer::setProfileMaxHglLinePen,   nullptr},
        {nullptr,  &mHglBrush, nullptr,  &m_lastPlotStyle.maxHglFillBrush,
         nullptr, &SWMMResultsLayer::setProfileMaxHglFillBrush},
        {&mEglPen, nullptr,    &m_lastPlotStyle.maxEglLinePen, nullptr,
         &SWMMResultsLayer::setProfileMaxEglLinePen,   nullptr},
    };

    // Every source currently listed, checked or not — an unchecked source the
    // user re-enables later should come back with the style they last set.
    QVector<QPointer<SWMMResultsLayer>> layers;
    const auto actions = m_sourceMenu ? m_sourceMenu->actions() : QList<QAction *>();
    for (QAction *act : actions) {
        QPointer<SWMMResultsLayer> l = m_actionLayer.value(act);
        if (l) layers.push_back(l);
    }

    bool pushedAny = false;
    for (const Entry &e : entries) {
        const bool changed = e.pen ? (*e.lastPen != *e.pen)
                                   : (*e.lastBrush != *e.brush);
        if (e.pen) *e.lastPen   = *e.pen;
        else       *e.lastBrush = *e.brush;
        if (seeding || !changed) continue;

        for (const QPointer<SWMMResultsLayer> &l : layers) {
            if (!l) continue;
            if (e.pen) (l.data()->*e.setPen)(*e.pen);
            else       (l.data()->*e.setBrush)(*e.brush);
            pushedAny = true;
        }
    }
    m_lastPlotStyle.seeded = true;
    return pushedAny;
}

void ProfilePlotDialog::rebindSources()
{
    if (!m_sourceMenu) {
        m_plot->setSeries({});
        return;
    }

    // Collect per-layer job parameters on the GUI thread (safe to read
    // QObjects), hand the fetch + compute off to a worker thread.  Each
    // layer is fetched once; the resulting SourceDerived is shared across
    // every (layer × output) series for that layer.
    struct Job {
        QString sourceId;
        QColor  color;
        QPointer<SWMMResultsLayer> layer;
        QPen    hglLinePen;
        QBrush  hglFillBrush;
        QPen    eglLinePen;
        QPen    maxHglLinePen;
        QBrush  maxHglFillBrush;
        QPen    maxEglLinePen;
    };
    QVector<Job> jobs;
    const auto actions = m_sourceMenu->actions();
    for (QAction *act : actions) {
        if (!act || !act->isChecked()) continue;
        QPointer<SWMMResultsLayer> layer = m_actionLayer.value(act);
        if (!layer) continue;
        Job j;
        j.sourceId = layer->scenarioName().isEmpty()
                         ? QFileInfo(layer->resultsFilePath()).completeBaseName()
                         : layer->scenarioName();
        j.color    = layer->profileLineColor();
        j.layer    = layer;
        j.hglLinePen      = layer->profileHglLinePen();
        j.hglFillBrush    = layer->profileHglFillBrush();
        j.eglLinePen      = layer->profileEglLinePen();
        j.maxHglLinePen   = layer->profileMaxHglLinePen();
        j.maxHglFillBrush = layer->profileMaxHglFillBrush();
        j.maxEglLinePen   = layer->profileMaxEglLinePen();
        jobs.push_back(j);
    }
    if (jobs.isEmpty()) {
        m_plot->setSeries({});
        if (m_loadingBar) m_loadingBar->setVisible(false);
        return;
    }

    // Snapshot the user's per-output preferences from the options object
    // so the worker thread can build the right series subset.  Default
    // EGL visibility tracks `currentEgl`, Max HGL band tracks
    // `maxHglBand`, etc. — matches the pre-refactor visual behaviour.
    const bool optCurrentHglLine = m_options->currentHglLine();
    const bool optCurrentHglFill = m_options->currentHglFill();
    const bool optCurrentEgl     = m_options->currentEgl();
    const bool optMaxHglBand     = m_options->maxHglBand();
    const bool optMaxHglLine     = m_options->maxHglLine();
    const bool optMaxEglLine     = m_options->maxEglLine();

    // Increment the cookie so any in-flight watcher that returns after a
    // newer rebind is ignored (Sources panel toggles can fire rapidly).
    const int cookie = ++m_loadCookie;
    if (m_loadingBar) m_loadingBar->setVisible(true);

    const auto pathSnapshot = m_pathStatic;
    const double gravity    = ProfileBuilder::kGravityFps2;
    // Snapshot the cache to hand to the worker by value — shared_ptrs
    // refcount up so the worker safely reads the cached `SourceDerived`
    // even if the GUI thread evicts the entry mid-flight. Anything the
    // worker fetches anew is returned via `RebindResult::freshEntries`
    // and merged into the live cache on the GUI thread.
    const auto cacheSnapshot = m_sourceCache;

    struct RebindResult {
        QVector<ProfilePlotWidget::SeriesBinding> bindings;
        QHash<SWMMResultsLayer *,
              std::shared_ptr<ProfileBuilder::SourceDerived>> freshEntries;
    };

    auto future = QtConcurrent::run(
        [jobs, pathSnapshot, gravity, cacheSnapshot,
         optCurrentHglLine, optCurrentHglFill, optCurrentEgl,
         optMaxHglBand, optMaxHglLine,
         optMaxEglLine]()
            -> RebindResult
    {
        using K = ProfileBuilder::OutputKind;
        RebindResult result;
        result.bindings.reserve(jobs.size() * 4);
        for (const Job &j : jobs) {
            if (!j.layer) continue;
            // Cache hit → reuse the previously-computed derived series
            // without touching the .out file. Cache miss → fetch + compute
            // and return the new entry so the GUI thread can store it.
            std::shared_ptr<ProfileBuilder::SourceDerived> derived;
            const auto cacheIt = cacheSnapshot.constFind(j.layer.data());
            if (cacheIt != cacheSnapshot.constEnd() && *cacheIt) {
                derived = *cacheIt;
            } else {
                auto series = ProfileSourceFetcher::fetch(j.layer, pathSnapshot,
                                                            j.sourceId);
                derived = std::make_shared<ProfileBuilder::SourceDerived>(
                    ProfileBuilder::compute(pathSnapshot, series, gravity));
                result.freshEntries.insert(j.layer.data(), derived);
            }

            auto pushSeries = [&](K kind, const QString &suffix,
                                  bool visible,
                                  const QPen &pen, const QBrush &brush)
            {
                ProfilePlotWidget::SeriesBinding b;
                b.label   = j.sourceId + QStringLiteral(" — ") + suffix;
                b.color   = j.color;
                b.kind    = kind;
                b.derived = derived;
                b.pen     = pen;
                b.brush   = brush;
                b.visible = visible;
                result.bindings.push_back(b);
            };

            // HGL — split into independent line + fill toggles. Series
            // is visible whenever either is on (legend still shows one
            // HGL entry); the widget gates paintHglFill on
            // currentHglFill and the HGL branch of paintSeriesCurrentLine
            // on currentHglLine. Pen / brush are neutralised to NoPen /
            // NoBrush when their toggle is off so the legend swatch
            // reflects exactly what's drawn (paintLegend reads
            // pen.style() / brush.style()).
            {
                QPen   linePen   = j.hglLinePen;
                QBrush fillBrush = j.hglFillBrush;
                if (!optCurrentHglLine) linePen.setStyle(Qt::NoPen);
                if (!optCurrentHglFill) fillBrush.setStyle(Qt::NoBrush);
                pushSeries(K::HGL,
                           QObject::tr("HGL"),
                           optCurrentHglLine || optCurrentHglFill,
                           linePen, fillBrush);
            }
            // EGL — current line only.  No fill: the velocity-head band
            // sits above the HGL but isn't a physically-meaningful water
            // region, so EGL exposes no fill style and renders as line
            // only. NoBrush keeps the legend swatch line-only too.
            pushSeries(K::EGL,
                       QObject::tr("EGL"),
                       optCurrentEgl,
                       j.eglLinePen, QBrush());
            // Max HGL — envelope band + line.  Brush controls band fill;
            // pen controls outline.  When the user wants band-only, the
            // outline pen comes through as NoPen from settings; vice-
            // versa for line-only.
            {
                QPen   pen   = j.maxHglLinePen;
                QBrush brush = j.maxHglFillBrush;
                if (!optMaxHglLine) pen.setStyle(Qt::NoPen);
                if (!optMaxHglBand) brush.setStyle(Qt::NoBrush);
                pushSeries(K::MaxHGL,
                           QObject::tr("Max HGL"),
                           optMaxHglBand || optMaxHglLine,
                           pen, brush);
            }
            // Max EGL — line only; no fill option exposed (the band has
            // no physical meaning, so brush stays NoBrush always).
            {
                QPen pen = j.maxEglLinePen;
                if (!optMaxEglLine) pen.setStyle(Qt::NoPen);
                pushSeries(K::MaxEGL,
                           QObject::tr("Max EGL"),
                           optMaxEglLine,
                           pen, QBrush());
            }
        }
        return result;
    });

    auto *watcher = new QFutureWatcher<RebindResult>(this);
    connect(watcher, &QFutureWatcher<RebindResult>::finished,
            this, [this, watcher, cookie]() {
        if (cookie == m_loadCookie) {
            const RebindResult r = watcher->result();
            // Merge newly-fetched entries into the live cache and wire
            // invalidation signals for each so future option toggles
            // hit the cache instead of refetching.
            for (auto it = r.freshEntries.constBegin();
                 it != r.freshEntries.constEnd(); ++it) {
                m_sourceCache.insert(it.key(), it.value());
                ensureCacheInvalidationWired(it.key());
            }
            m_plot->setSeries(r.bindings);
            if (m_loadingBar) m_loadingBar->setVisible(false);
            // Re-push the current cursor so the animated line lands on
            // the right period.
            if (m_anim) {
                if (auto *primary = m_anim->primaryLayer())
                    onAnimationTimeChanged(primary->currentDateTime());
            }
            // setSeries recomputed the profile's bounds/virtual table —
            // re-share the axes and rebuild the tracks pane against the
            // (possibly changed) checked-source set.
            syncTracksAxes();
            rebuildTracks();
        }
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void ProfilePlotDialog::invalidateSourceCacheFor(SWMMResultsLayer *layer)
{
    if (!layer) return;
    m_sourceCache.remove(layer);
    // Attribute-tracks cache entries for this layer are equally stale.
    for (auto it = m_attrCache.begin(); it != m_attrCache.end();) {
        if (it.key().first == layer) it = m_attrCache.erase(it);
        else                         ++it;
    }
}

void ProfilePlotDialog::ensureCacheInvalidationWired(SWMMResultsLayer *layer)
{
    if (!layer) return;
    if (m_cacheWired.contains(layer)) return;
    m_cacheWired.insert(layer);

    // New `.out` opened on this layer (re-run, "Open Results", etc.) →
    // the cached SourceDerived is now stale — and the run's species list
    // may have changed, so the Tracks menu's species entries follow.
    connect(layer, &SWMMResultsLayer::resultsOpened,
            this, [this, layer]() {
                invalidateSourceCacheFor(layer);
                refreshTracksMenuSpecies();
            });
    // Layer pointed at a different results file.
    connect(layer, &SWMMResultsLayer::resultsFilePathChanged,
            this, [this, layer]() { invalidateSourceCacheFor(layer); });
    // Layer destroyed → drop the cache entry AND the wired flag so the
    // hash never holds a dangling key.
    connect(layer, &QObject::destroyed,
            this, [this, layer]() {
                invalidateSourceCacheFor(layer);   // source + attribute caches
                m_cacheWired.remove(layer);
                // Discard any in-flight fetch: its result hash is keyed by
                // this (now dangling) raw pointer, and merging it would both
                // cache a dead key and re-wire signals on a destroyed
                // object. The next rebind/rebuild starts clean.
                ++m_loadCookie;
                ++m_trackLoadCookie;
            });
}

// ---------------------------------------------------------------------------
// Animation-cursor sync
// ---------------------------------------------------------------------------

SWMM2DMeshLayer *ProfilePlotDialog::firstMeshLayer() const
{
    if (!m_canvas) return nullptr;
    for (OpenSWMMVisLayer *l : m_canvas->layers())
        if (auto *m = qobject_cast<SWMM2DMeshLayer *>(l)) return m;
    return nullptr;
}

ProfilePlotOptions::GroundSource ProfilePlotDialog::resolvedGroundSource() const
{
    using GS = ProfilePlotOptions::GroundSource;
    const GS s = m_options ? m_options->groundSource() : GS::Auto;
    if (s != GS::Auto) return s;
    return firstMeshLayer() ? GS::Mesh2D : GS::NodeRims;
}

void ProfilePlotDialog::rebuildTerrainSamples()
{
    using GS = ProfilePlotOptions::GroundSource;
    m_pathStatic.terrainSamples.clear();
    if (m_groundMesh)
        disconnect(m_groundMesh.data(), nullptr, this, nullptr);
    m_groundMesh = nullptr;
    if (!m_options || !m_projectWindow || !m_model) return;

    const GS source = resolvedGroundSource();
    m_lastGroundSource = source;

    if (source == GS::Mesh2D) {
        // Ground = 2D mesh vertex elevations, barycentrically interpolated at
        // every path station (same sampler as the 2D mesh profile). Stations
        // off the mesh are skipped, so partial coverage leaves the node row
        // to carry the rest.
        SWMM2DMeshLayer *mesh = firstMeshLayer();
        if (!mesh) return;
        forEachPathStationScene([&](double chain, const QPointF &sp) {
            const double z = mesh->sampleZAt(sp.x(), sp.y());
            if (std::isfinite(z))
                m_pathStatic.terrainSamples.push_back(QPointF(chain, z));
        });
        // Vertex-Z edits / remesh / deferred geometry → re-sample.
        m_groundMesh = mesh;
        auto resample = [this] {
            rebuildTerrainSamples();
            m_plot->setPath(m_pathStatic);
            syncTracksAxes();
        };
        connect(mesh, &SWMM2DMeshLayer::meshEditsChanged,   this, resample);
        connect(mesh, &SWMM2DMeshLayer::sceneGeometryReady, this, resample);
        connect(mesh, &SWMM2DMeshLayer::attributeChanged,   this,
                [resample](const QString &) { resample(); });
        return;
    }

    if (source != GS::TerrainDEM) return;   // NodeRims: no samples

    // Live-look up the *current* terrain + vertical factor + canvas SRS
    // from the project window so the ground line always reflects the
    // toolbar's latest selection — even when the user added/changed the
    // terrain after this dialog was opened.
    GISRasterLayer *terrain     = m_projectWindow->activeTerrain();
    if (!terrain) return;
    const double terrainFactor  = m_projectWindow->terrainVertFactor();
    const SpatialReferenceSystem *canvasSRS =
        m_canvas ? m_canvas->canvasSRS() : nullptr;

    // GISRasterLayer::valueAt expects coords in the *canvas* CRS (it does
    // its own canvas→raster transform internally); forEachPathStationScene
    // delivers canvas-CRS x and NEGATED y, so undo the flip here.
    forEachPathStationScene([&](double chain, const QPointF &sp) {
        bool ok = false;
        const double zRaw = terrain->valueAt(sp.x(), -sp.y(), canvasSRS, /*band=*/1, &ok);
        if (!ok || !std::isfinite(zRaw)) return;   // outside DEM
        // Convert raster vertical unit → model vertical unit so
        // the ground line lines up with the node inverts / rims.
        const double z = zRaw * terrainFactor;
        m_pathStatic.terrainSamples.push_back(QPointF(chain, z));
    });
}

void ProfilePlotDialog::forEachPathStationScene(
    const std::function<void(double, const QPointF &)> &fn) const
{
    if (!m_canvas || !m_model) return;
    // The model's cached node / polyline coords live in the *model layer*
    // CRS. Project each station into the canvas CRS when the two differ,
    // otherwise DEM / mesh samples land at the wrong place and the ground
    // line ends up misaligned with the network. The 2D scene is the canvas
    // CRS with Y negated (SWMM2DResultsLayer / SWMM2DMeshLayer convention).
    const SpatialReferenceSystem *canvasSRS = m_canvas->canvasSRS();
    SpatialReferenceSystem *modelSRS = m_model->srs();
    OGRCoordinateTransformation *modelToCanvas = nullptr;
    if (modelSRS && canvasSRS && !modelSRS->equals(*canvasSRS))
        modelToCanvas = modelSRS->createTransformationTo(*canvasSRS);

    forEachPathStation([&](double chain, double mx, double my) {
        double cx = mx, cy = my;
        if (modelToCanvas) {
            double tx = mx, ty = my;
            if (modelToCanvas->Transform(1, &tx, &ty)) { cx = tx; cy = ty; }
        }
        fn(chain, QPointF(cx, -cy));
    });
    if (modelToCanvas) OGRCoordinateTransformation::DestroyCT(modelToCanvas);
}

void ProfilePlotDialog::forEachPathStation(
    const std::function<void(double, double, double)> &fn) const
{
    if (!m_model) return;
    if (m_routerPath.linkIds.size() + 1 != m_routerPath.nodes.size())   return;
    if (m_pathStatic.chainage.size() != m_pathStatic.nodes.size())      return;

    constexpr int    kMaxSamplesPerSegment = 20;
    constexpr double kSampleStepHint       = 5.0;  // model units

    for (int li = 0; li < m_routerPath.linkIds.size(); ++li) {
        const int engLink = m_routerPath.linkIds[li];
        QVector<QPointF> poly = m_model->cachedLinkPolyline(engLink);
        if (poly.isEmpty()) continue;

        // Orient the polyline along path-traversal direction so chainage
        // accumulates monotonically.  Compare polyline endpoints to the
        // path-upstream node's coord.
        double ux = 0, uy = 0;
        if (m_model->cachedNodeCoord(m_routerPath.nodes[li], &ux, &uy)) {
            const double dStart = std::hypot(poly.first().x() - ux,
                                             poly.first().y() - uy);
            const double dEnd   = std::hypot(poly.last().x()  - ux,
                                             poly.last().y()  - uy);
            if (dEnd < dStart) std::reverse(poly.begin(), poly.end());
        }

        // We need the *path* chainage at each polyline vertex.  Use the
        // segment-length ratio along the polyline scaled to the link's
        // chainage span.
        double polyTotalLen = 0.0;
        QVector<double> cumPoly(poly.size(), 0.0);
        for (int v = 1; v < poly.size(); ++v) {
            const double dx = poly[v].x() - poly[v - 1].x();
            const double dy = poly[v].y() - poly[v - 1].y();
            polyTotalLen += std::hypot(dx, dy);
            cumPoly[v] = polyTotalLen;
        }
        if (polyTotalLen <= 0.0) continue;

        const double chainStart = m_pathStatic.chainage[li];
        const double chainEnd   = m_pathStatic.chainage[li + 1];
        const double chainSpan  = chainEnd - chainStart;

        auto chainForPoly = [&](double polyDist) {
            return chainStart + (polyDist / polyTotalLen) * chainSpan;
        };

        for (int v = 0; v + 1 < poly.size(); ++v) {
            const QPointF a = poly[v];
            const QPointF b = poly[v + 1];
            const double segLen = std::hypot(b.x() - a.x(), b.y() - a.y());
            if (segLen <= 0.0) continue;
            const int samples = std::clamp(
                static_cast<int>(segLen / kSampleStepHint), 1,
                kMaxSamplesPerSegment);
            for (int s = 0; s <= samples; ++s) {
                const double t  = double(s) / samples;
                const double x  = a.x() + t * (b.x() - a.x());
                const double y  = a.y() + t * (b.y() - a.y());
                const double pd = cumPoly[v] + t * segLen;
                fn(chainForPoly(pd), x, y);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 2D inundation overlay
// ---------------------------------------------------------------------------

void ProfilePlotDialog::rebuildSurface2DStations()
{
    m_surface2D.clear();
    if (m_surface2DLayer)
        disconnect(m_surface2DLayer.data(), nullptr, this, nullptr);
    m_surface2DLayer = nullptr;

    auto clearPlot = [this] { m_plot->setSurface2DSamples({}); };
    if (!m_options || !m_options->show2DInundation()) { clearPlot(); return; }
    if (!m_projectWindow || !m_model || !m_canvas)   { clearPlot(); return; }

    SWMM2DResultsLayer *results = m_projectWindow->active2DResultsLayer();
    if (!results || !results->source())               { clearPlot(); return; }

    // Bed elevation comes from the mesh layer's triangulation (the same
    // sampler the 2D mesh profile uses); the results layer has no z field.
    SWMM2DMeshLayer *mesh = firstMeshLayer();
    if (!mesh)                                        { clearPlot(); return; }

    forEachPathStationScene([&](double chain, const QPointF &sp) {
        const double bed = mesh->sampleZAt(sp.x(), sp.y());
        if (!std::isfinite(bed)) return;                 // off the mesh
        const int tri = results->pickCellAt(sp);
        if (tri < 0) return;
        Surface2DStation st;
        st.chainage = chain;
        st.scenePt  = sp;
        st.triIdx   = tri;
        st.bed      = bed;
        m_surface2D.push_back(st);
    });

    if (m_surface2D.isEmpty())                        { clearPlot(); return; }

    m_surface2DLayer = results;
    // Frame changes (canvas animation of a visible layer, or our own
    // setCurrentSimTimeAsOf from onAnimationTimeChanged) → re-read depths.
    connect(results, &SWMM2DResultsLayer::currentTimeChanged,
            this, [this](int) { refreshSurface2DDepths(); });
    connect(results, &QObject::destroyed, this, [this] {
        m_surface2D.clear();
        m_surface2DLayer = nullptr;
        m_plot->setSurface2DSamples({});
    });
    refreshSurface2DDepths();
}

void ProfilePlotDialog::refreshSurface2DDepths()
{
    QVector<ProfilePlotWidget::Surface2DSample> out;
    if (m_surface2DLayer && !m_surface2D.isEmpty()) {
        out.reserve(m_surface2D.size());
        for (const Surface2DStation &st : m_surface2D) {
            ProfilePlotWidget::Surface2DSample s;
            s.chainage = st.chainage;
            s.bed      = st.bed;
            // WSE = bed + barycentric depth, only where the cell carries a
            // valid free surface this frame; dry / no-data stations stay
            // NaN and render as gaps (same rule as the 2D mesh profile).
            if (m_surface2DLayer->cellHasSurface(st.triIdx)) {
                const double d = m_surface2DLayer->depthAtCellInterp(st.triIdx, st.scenePt);
                if (std::isfinite(d) && d > 0.0) s.wse = st.bed + d;
            }
            out.push_back(s);
        }
    }
    m_plot->setSurface2DSamples(out);
}

void ProfilePlotDialog::onAnimationTimeChanged(const QDateTime &dt)
{
    if (!m_anim) return;
    auto *primary = m_anim->primaryLayer();
    if (!primary) return;
    const int period = primary->periodIndexForDateTime(dt);
    m_plot->setCurrentPeriod(/*sourceIdx=*/0, period);
    m_plot->setCurrentDateTime(dt);
    if (m_tracks)
        m_tracks->setCurrentPeriod(period);
    // Advance the 2D layer ourselves: the canvas only steps VISIBLE 2D
    // layers, so a hidden layer's overlay would otherwise freeze. No-op
    // when already on that frame; currentTimeChanged → refreshSurface2DDepths.
    if (m_surface2DLayer)
        m_surface2DLayer->setCurrentSimTimeAsOf(dt);
}

// ---------------------------------------------------------------------------
// Attribute tracks (synced pane below the profile)
// ---------------------------------------------------------------------------

void ProfilePlotDialog::buildAttributeTracksUi(QToolBar *toolbar)
{
    using openswmmvis::plot::PlotAttribute;
    using openswmmvis::plot::labelFor;

    // ── Toolbar: "Tracks ▾" attribute picker + master show/hide toggle ──
    auto *tracksButton = new QToolButton(toolbar);
    tracksButton->setText(tr("Tracks"));
    tracksButton->setIcon(
        openswmmvis::ui::IconFactory::icon(QStringLiteral("ChartProperties")));
    tracksButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tracksButton->setPopupMode(QToolButton::InstantPopup);
    tracksButton->setToolTip(
        tr("Attribute Tracks — add profile charts of node/link attributes "
           "below the plot, x-axis synced to the profile"));
    m_tracksMenu = new QMenu(tracksButton);
    tracksButton->setMenu(m_tracksMenu);
    toolbar->addSeparator();
    toolbar->addWidget(tracksButton);

    // Checkable menu entries — one per trackable attribute, grouped.
    // QAction::setData carries the enum so one handler serves all.
    // (Species entries carry a QString token instead — see
    // refreshTracksMenuSpecies; the data's variant TYPE tells them apart.)
    auto addAttrActions = [this](const QString &sectionTitle,
                                 const QVector<PlotAttribute> &attrs) {
        QAction *section = m_tracksMenu->addSection(sectionTitle);
        for (PlotAttribute a : attrs) {
            QAction *act = m_tracksMenu->addAction(labelFor(a));
            act->setCheckable(true);
            act->setChecked(m_trackOptions->isAttributeVisible(a));
            act->setData(int(a));
            connect(act, &QAction::toggled, this, [this, a](bool on) {
                // Options object is the single source of truth; its
                // changed() drives the rebuild and menu re-sync.
                m_trackOptions->setAttributeVisible(a, on);
            });
        }
        return section;
    };
    addAttrActions(tr("Node attributes"),
                   openswmmvis::plot::nodePlotAttributes());
    m_tracksLinkSection =
        addAttrActions(tr("Link attributes"),
                       openswmmvis::plot::linkPlotAttributes());
    refreshTracksMenuSpecies();

    // Master show/hide toggle. Named ⇒ its checked state persists via the
    // DialogLayoutWatcher toggle group, like ComparisonPlotDialog's panel
    // toggles.
    m_actShowTracks = toolbar->addAction(
        openswmmvis::ui::IconFactory::icon(QStringLiteral("AttributeTracks")),
        tr("Show Attribute Tracks"));
    m_actShowTracks->setObjectName(QStringLiteral("showAttributeTracks"));
    m_actShowTracks->setCheckable(true);
    m_actShowTracks->setChecked(true);
    connect(m_actShowTracks, &QAction::toggled, this, [this](bool on) {
        if (!on && m_profileSplit && m_tracksScroll
            && m_tracksScroll->isVisible()) {
            // Remember the expanded proportions for the re-show.
            const QList<int> sizes = m_profileSplit->sizes();
            if (sizes.size() == 2 && sizes[1] >= kTracksPaneMinHeightPx)
                m_lastSplitSizes = sizes;
        }
        updateTracksPaneVisibility();   // re-show also re-expands the pane
    });

    // Remember the user's split so a hide/re-show round-trips it.
    connect(m_profileSplit, &QSplitter::splitterMoved, this, [this]() {
        if (!m_tracksScroll || !m_tracksScroll->isVisible()) return;
        const QList<int> sizes = m_profileSplit->sizes();
        if (sizes.size() == 2 && sizes[1] >= kTracksPaneMinHeightPx)
            m_lastSplitSizes = sizes;
    });

    // ── X-axis sync, both directions, one re-entrancy guard ────────────
    connect(m_plot, &ProfilePlotWidget::visibleXRangeChanged, this,
            [this](double vxMin, double vxMax) {
        if (m_syncingX) return;
        m_syncingX = true;
        m_tracks->setVisibleXRange(vxMin, vxMax);
        m_syncingX = false;
    });
    connect(m_tracks, &ProfileAttributeTracksWidget::visibleXRangeChanged,
            this, [this](double vxMin, double vxMax) {
        if (m_syncingX) return;
        m_syncingX = true;
        m_plot->setVisibleXRange(vxMin, vxMax);
        m_syncingX = false;
    });

    // ── Options changed → persist, re-sync menu checks, rebuild ────────
    connect(m_trackOptions, &ProfileAttributeTrackOptions::changed, this,
            [this]() {
        QSettings s;
        s.beginGroup(QLatin1String(kTrackSettingsGroup));
        m_trackOptions->writeTo(s);
        s.endGroup();
        // Menu checks follow the options object (the Display Options tree
        // edits the same instance) — block the actions' toggled() so this
        // re-sync can't loop back into setAttributeVisible.
        if (m_tracksMenu) {
            const auto acts = m_tracksMenu->actions();
            for (QAction *act : acts) {
                if (!act->isCheckable() || !act->data().isValid()) continue;
                QSignalBlocker block(act);
                // Species entries carry the scope-qualified token as a
                // QString; fixed attributes carry the enum as an int.
                if (act->data().typeId() == QMetaType::QString) {
                    const QString token = act->data().toString();
                    const bool nodeScope =
                        token.endsWith(QLatin1String("@node"));
                    act->setChecked(m_trackOptions->isSpeciesTrackVisible(
                        token.left(token.size() - 5), nodeScope));
                } else {
                    const auto a = PlotAttribute(act->data().toInt());
                    act->setChecked(m_trackOptions->isAttributeVisible(a));
                }
            }
        }
        rebuildTracks();
    });

    syncTracksAxes();
    updateTracksPaneVisibility();
    // Initial data load happens through rebindSources() → rebuildTracks().
}

void ProfilePlotDialog::refreshTracksMenuSpecies()
{
    // Y2b-2 follow-up (amendment D-Y4): one checkable entry per species ×
    // scope, sitting with its scope's fixed attributes. The offered set is
    // the union across the current source layers — a species only one
    // overlay run carries is still trackable (the other sources render an
    // empty row for it, same as any element they don't know).
    if (!m_tracksMenu || !m_trackOptions) return;

    for (QAction *act : std::as_const(m_speciesTrackActions)) {
        m_tracksMenu->removeAction(act);
        delete act;
    }
    m_speciesTrackActions.clear();

    QStringList species;
    const QList<SWMMResultsLayer *> layers =
        openswmmvis::ui::profileResultSources(m_anim.data(),
                                              m_projectWindow.data(),
                                              m_canvas.data());
    for (SWMMResultsLayer *l : layers) {
        if (!l) continue;
        for (const QString &sp : l->speciesNames())
            if (!sp.isEmpty() && !species.contains(sp))
                species.append(sp);
    }
    if (species.isEmpty()) return;

    auto addSpeciesAction = [this](const QString &sp, bool nodeScope,
                                   QAction *before) {
        const auto d = openswmmvis::plot::ResultDescriptor::forSpecies(sp);
        auto *act = new QAction(d.label(), m_tracksMenu);
        act->setCheckable(true);
        act->setChecked(m_trackOptions->isSpeciesTrackVisible(sp, nodeScope));
        act->setData(sp + (nodeScope ? QLatin1String("@node")
                                     : QLatin1String("@link")));
        connect(act, &QAction::toggled, this, [this, sp, nodeScope](bool on) {
            m_trackOptions->setSpeciesTrackVisible(sp, nodeScope, on);
        });
        if (before) m_tracksMenu->insertAction(before, act);
        else        m_tracksMenu->addAction(act);
        m_speciesTrackActions.push_back(act);
    };
    for (const QString &sp : std::as_const(species))
        addSpeciesAction(sp, /*nodeScope=*/true, m_tracksLinkSection);
    for (const QString &sp : std::as_const(species))
        addSpeciesAction(sp, /*nodeScope=*/false, nullptr);
}

void ProfilePlotDialog::updateTracksPaneVisibility()
{
    if (!m_tracksScroll || !m_profileSplit) return;
    const bool any  = m_trackOptions && m_trackOptions->anyAttributeVisible();
    const bool show = any && (!m_actShowTracks || m_actShowTracks->isChecked());
    if (m_actShowTracks) m_actShowTracks->setEnabled(any);
    // Hiding the pane also hides the splitter handle — with no attribute
    // selected the dialog looks exactly as it did before this feature.
    m_tracksScroll->setVisible(show);
    if (show) ensureTracksPaneExpanded();
    syncTracksGutter();   // hidden pane ⇒ give the plot its full width back
}

void ProfilePlotDialog::ensureTracksPaneExpanded()
{
    if (!m_profileSplit || !m_tracksScroll) return;
    QList<int> sizes = m_profileSplit->sizes();
    if (sizes.size() != 2) return;
    // A zero / sliver pane is what a persisted collapsed split (older
    // sessions could drag it to zero) or a fresh show() leaves behind, and
    // it reads as "the tracks never appeared". Restore the last good split,
    // else fall back to the 3:1 default.
    if (sizes[1] >= kTracksPaneMinHeightPx) return;
    if (m_lastSplitSizes.size() == 2 && m_lastSplitSizes[1] >= kTracksPaneMinHeightPx) {
        m_profileSplit->setSizes(m_lastSplitSizes);
        return;
    }
    const int total = std::max(sizes[0] + sizes[1], 4 * kTracksPaneMinHeightPx);
    const int tracks = std::max(kTracksPaneMinHeightPx, total / 4);
    m_profileSplit->setSizes({ total - tracks, tracks });
}

void ProfilePlotDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    // DialogLayoutWatcher restores the named splitter's state synchronously
    // during this same Show — possibly a zero-height tracks pane persisted
    // by an older session that could still drag-collapse it. The toggle is
    // the source of truth: if it says shown, make sure the pane actually
    // has height once the restore has settled.
    QTimer::singleShot(0, this, [this]() {
        if (!m_tracksScroll || !m_tracksScroll->isVisible()) return;
        ensureTracksPaneExpanded();
    });
}

bool ProfilePlotDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tracks && event->type() == QEvent::Resize)
        syncTracksGutter();
    return QDialog::eventFilter(watched, event);
}

void ProfilePlotDialog::syncTracksGutter()
{
    if (!m_plotHolder || !m_tracksScroll) return;
    // How much narrower the tracks widget is than the pane it sits in — the
    // vertical scrollbar, or 0 where the style draws it as an overlay. With
    // the pane hidden there is nothing to line up with, and the plot must get
    // the full width back (§3.1: no attribute selected ⇒ dialog looks exactly
    // as it did before this feature).
    const int deficit = m_tracksScroll->isVisible()
        ? std::max(0, m_tracksScroll->width()
                      - m_tracksScroll->viewport()->width())
        : 0;
    auto *lay = m_plotHolder->layout();
    if (!lay) return;
    const QMargins m = lay->contentsMargins();
    if (m.right() == deficit) return;
    lay->setContentsMargins(m.left(), m.top(), deficit, m.bottom());
}

void ProfilePlotDialog::syncTracksAxes()
{
    if (!m_tracks || !m_plot) return;
    syncTracksGutter();
    m_tracks->setVirtualChainage(m_plot->virtualChainageTable());
    m_tracks->setHorizontalMargins(ProfilePlotWidget::chartLeftMarginPx(),
                                   ProfilePlotWidget::chartRightMarginPx());
    m_tracks->setRealChainageMapper(
        [plot = QPointer<ProfilePlotWidget>(m_plot)](double vx) {
            return plot ? plot->virtualToRealChainage(vx) : vx;
        });
    const QRectF r = m_plot->visibleDataRange();
    m_tracks->setVisibleXRange(r.left(), r.right());

    auto *us = m_projectWindow ? m_projectWindow->unitSystem()
                               : UnitSystem::instance();
    const QString unit = us ? us->lengthLabel() : QString();
    m_tracks->setXLabel(unit.isEmpty() ? tr("Distance")
                                       : tr("Distance (%1)").arg(unit));
}

void ProfilePlotDialog::rebuildTracks()
{
    using openswmmvis::plot::PlotAttribute;
    if (!m_tracks || !m_trackOptions) return;

    updateTracksPaneVisibility();

    const QVector<PlotAttribute> attrs = m_trackOptions->visibleAttributes();
    const QVector<QPair<QString, bool>> speciesTracks =
        m_trackOptions->visibleSpeciesTracks();
    if ((attrs.isEmpty() && speciesTracks.isEmpty()) || !m_sourceMenu) {
        m_tracks->setTracks({});
        return;
    }

    // ── Collect checked sources (GUI thread — safe QObject reads) ──────
    struct TrackJob {
        QString sourceId;
        QColor  color;
        bool    primary = false;
        QPointer<SWMMResultsLayer> layer;
    };
    QVector<TrackJob> jobs;
    SWMMResultsLayer *primaryLayer = m_anim ? m_anim->primaryLayer() : nullptr;
    const auto actions = m_sourceMenu->actions();
    for (QAction *act : actions) {
        if (!act || !act->isChecked()) continue;
        QPointer<SWMMResultsLayer> layer = m_actionLayer.value(act);
        if (!layer) continue;
        TrackJob j;
        j.sourceId = layer->scenarioName().isEmpty()
                         ? QFileInfo(layer->resultsFilePath()).completeBaseName()
                         : layer->scenarioName();
        j.color   = layer->profileLineColor();
        j.primary = (layer.data() == primaryLayer);
        j.layer   = layer;
        jobs.push_back(j);
    }
    if (jobs.isEmpty()) {
        m_tracks->setTracks({});
        return;
    }
    // Envelopes are drawn for the primary source; if the animation primary
    // isn't among the checked sources, promote the first so the band still
    // has an owner.
    if (std::none_of(jobs.cbegin(), jobs.cend(),
                     [](const TrackJob &j) { return j.primary; }))
        jobs[0].primary = true;

    // ── Resolve titles/pens on the GUI thread ──────────────────────────
    namespace P = openswmmvis::plot;
    P::UnitSystem us = P::UnitSystem::US;
    if (primaryLayer)
        us = P::unitSystemFromFlowUnits(primaryLayer->flowUnits());
    struct TrackSpec {
        PlotAttribute attr = PlotAttribute::Unknown;
        QString species;            ///< empty = fixed attribute track
        bool    isNode = true;
        QString title;
        QPen    pen;
        QString cacheKey;
    };
    QVector<TrackSpec> specs;
    specs.reserve(attrs.size() + speciesTracks.size());
    for (PlotAttribute a : attrs) {
        TrackSpec spec;
        spec.attr     = a;
        spec.isNode   = ProfileAttributeSampler::isNodeAttribute(a);
        spec.title    = P::labelWithUnits(a, us);
        spec.pen      = m_trackOptions->penFor(a);
        spec.cacheKey = QStringLiteral("a:%1").arg(int(a));
        specs.push_back(spec);
    }
    // Species tracks (Y2b-2 follow-up) — name-keyed; a visible species no
    // checked source carries (e.g. persisted from another model) is
    // skipped rather than rendered as a permanently-empty track.
    for (const auto &st : speciesTracks) {
        const bool known = std::any_of(
            jobs.cbegin(), jobs.cend(), [&st](const TrackJob &j) {
                return j.layer && j.layer->speciesNames().contains(st.first);
            });
        if (!known) continue;
        const auto d = P::ResultDescriptor::forSpecies(st.first);
        TrackSpec spec;
        spec.species  = st.first;
        spec.isNode   = st.second;
        spec.title    = tr("%1 (%2) — %3")
                            .arg(d.label(), d.unitLabel(us),
                                 st.second ? tr("nodes") : tr("links"));
        spec.pen      = m_trackOptions->speciesTrackPenFor(st.first, st.second);
        spec.cacheKey = st.first + (st.second ? QLatin1String("@node")
                                              : QLatin1String("@link"));
        specs.push_back(spec);
    }
    if (specs.isEmpty()) {
        m_tracks->setTracks({});
        return;
    }

    // ── Fetch off-thread; cookie guards stale returns (rebindSources
    // pattern). Cache snapshot shares refcounted profiles with the worker.
    const int  cookie        = ++m_trackLoadCookie;
    const auto pathSnapshot  = m_pathStatic;
    const auto cacheSnapshot = m_attrCache;

    using TW = ProfileAttributeTracksWidget;
    struct TracksResult {
        QVector<TW::Track> tracks;
        QHash<QPair<SWMMResultsLayer *, QString>,
              std::shared_ptr<const ProfileAttributeSampler::AttributeProfile>>
            fresh;
    };

    auto future = QtConcurrent::run(
        [jobs, specs, pathSnapshot, cacheSnapshot]() -> TracksResult {
        TracksResult res;
        res.tracks.reserve(specs.size());
        for (const TrackSpec &spec : specs) {
            TW::Track t;
            t.attribute       = spec.attr;
            t.isNodeAttribute = spec.isNode;
            t.title           = spec.title;
            t.pen             = spec.pen;
            for (const TrackJob &j : jobs) {
                if (!j.layer) continue;
                const auto key = qMakePair(j.layer.data(), spec.cacheKey);
                std::shared_ptr<const ProfileAttributeSampler::AttributeProfile>
                    prof;
                if (const auto it = cacheSnapshot.constFind(key);
                    it != cacheSnapshot.constEnd() && *it) {
                    prof = *it;
                } else if (const auto ft = res.fresh.constFind(key);
                           ft != res.fresh.constEnd()) {
                    prof = *ft;
                } else {
                    prof = std::make_shared<
                        const ProfileAttributeSampler::AttributeProfile>(
                        spec.species.isEmpty()
                            ? ProfileAttributeSampler::fetch(
                                  j.layer, pathSnapshot, spec.attr)
                            : ProfileAttributeSampler::fetchSpecies(
                                  j.layer, pathSnapshot, spec.species,
                                  spec.isNode));
                    res.fresh.insert(key, prof);
                }
                TW::SourceProfile sp;
                sp.label   = j.sourceId;
                sp.color   = j.color;
                sp.primary = j.primary;
                sp.data    = prof;
                t.sources.push_back(sp);
            }
            res.tracks.push_back(t);
        }
        return res;
    });

    auto *watcher = new QFutureWatcher<TracksResult>(this);
    connect(watcher, &QFutureWatcher<TracksResult>::finished,
            this, [this, watcher, cookie]() {
        if (cookie == m_trackLoadCookie) {
            const TracksResult r = watcher->result();
            for (auto it = r.fresh.constBegin(); it != r.fresh.constEnd();
                 ++it) {
                m_attrCache.insert(it.key(), it.value());
                ensureCacheInvalidationWired(it.key().first);
            }
            m_tracks->setTracks(r.tracks);
            // Land the animated curve on the current period straight away.
            if (m_anim) {
                if (auto *primary = m_anim->primaryLayer())
                    m_tracks->setCurrentPeriod(primary->periodIndexForDateTime(
                        primary->currentDateTime()));
            }
        }
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}
