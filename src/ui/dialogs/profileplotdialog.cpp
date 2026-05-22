/*!
 * \file   profileplotdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/profileplotdialog.h"

#include "animation/animationcontroller.h"
#include "layers/gisrasterlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "plot/profilenetworkadapter.h"
#include "plot/profileplotoptions.h"
#include "plot/profilesourcefetcher.h"
#include "swmmvisprojectwindow.h"
#include "core/unitsystem.h"
#include "ui/dialogs/profileoptionsdialog.h"
#include "ui/dialogs/timeseriesplotdialog.h"
#include "ui/widgets/profilelayerpanel.h"
#include "selection/selectionmanager.h"

#include <ogr_spatialref.h>

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
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>

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
    setModal(false);
    // Single options object — shared by the layer panel (visibility
    // checkboxes), the plot widget (theming + legend), and the property-
    // model-driven Display Options dialog.  Any setter on it propagates
    // through the `changed()` signal.
    m_options = new ProfilePlotOptions(this);
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
                   | Qt::WindowStaysOnTopHint);
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

    // Lifetime: this dialog is a top-level window parented to nullptr (so
    // it gets its own dock icon on macOS), but it holds raw pointers to
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
    toolbar->setIconSize(QSize(18, 18));
    // Explicit Select (Identify) toggle: the default plot mode, but
    // implicit "no button checked = identify" was confusing for users.
    // Listed first so it reads as the home / resting state.
    auto *actSelect  = toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Select")),
                                          tr("Select"));
    auto *actZoomIn  = toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/ZoomIn")),
                                          tr("Zoom In"));
    auto *actZoomOut = toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/ZoomOut")),
                                          tr("Zoom Out"));
    auto *actFit     = toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Extent")),
                                          tr("Fit to Path"));
    toolbar->addSeparator();
    auto *actPan     = toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Move")),
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
    m_sourceButton->setIcon(QIcon(QStringLiteral(":/swmmvis/Chartpie")));
    m_sourceButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_sourceButton->setPopupMode(QToolButton::InstantPopup);
    m_sourceMenu = new QMenu(m_sourceButton);
    m_sourceButton->setMenu(m_sourceMenu);
    toolbar->addWidget(m_sourceButton);
    toolbar->addSeparator();
    auto *actExport  = toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/SaveAs")),
                                          tr("Export PNG…"));
    toolbar->addSeparator();
    auto *actOptions = toolbar->addAction(QIcon(QStringLiteral(":/swmmvis/Settings")),
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
    centre->addWidget(m_plot, /*stretch=*/1);

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
    auto togglesFromOptions = [](ProfilePlotOptions *o) {
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
        t.useTerrainGround = o->useTerrainGround();
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
        o->setUseTerrainGround(t.useTerrainGround);
    };

    Q_UNUSED(applyTogglesToOptions);  // panel-side sync removed; kept the
                                       // lambda for future use.

    // Initial push: options → plot widget (visibility / labels / terrain).
    m_plot->setLayerToggles(togglesFromOptions(m_options));

    // Options → plot.  Drives visibility, label rendering, and the
    // terrain re-sample whenever the user toggles "Use terrain DEM".
    // Also reruns rebindSources so per-output visibility flips propagate
    // to the series list (the widget reads visibility from series, not
    // from LayerToggles).
    connect(m_options, &ProfilePlotOptions::changed, this,
            [this, togglesFromOptions]() {
        const auto t = togglesFromOptions(m_options);
        const bool terrainChanged =
            (t.useTerrainGround != m_plot->layerToggles().useTerrainGround);
        m_plot->setLayerToggles(t);
        if (terrainChanged) {
            rebuildTerrainSamples();
            m_plot->setPath(m_pathStatic);
        }
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
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowFlags(dlg->windowFlags()
                            | Qt::Tool
                            | Qt::WindowStaysOnTopHint);
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
        QPixmap pix(m_plot->size());
        pix.fill(Qt::white);
        m_plot->render(&pix);
        pix.save(path, "PNG");
    });

    // Plot click → main-map selection.  Each click replaces the model
    // layer's selection with the single object.
    connect(m_plot, &ProfilePlotWidget::nodeClicked,
            this,   [this](int pathNodeIdx) {
        if (!m_model) return;
        if (pathNodeIdx < 0 || pathNodeIdx >= m_pathStatic.nodes.size()) return;
        m_model->setSelectedElementNames(
            QStringList{ m_pathStatic.nodes[pathNodeIdx].name });
    });
    connect(m_plot, &ProfilePlotWidget::linkClicked,
            this,   [this](int pathLinkIdx) {
        if (!m_model) return;
        if (pathLinkIdx < 0 || pathLinkIdx >= m_pathStatic.links.size()) return;
        m_model->setSelectedElementNames(
            QStringList{ m_pathStatic.links[pathLinkIdx].name });
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
    }

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
        m_model->setSelectedElementNames(
            QStringList{ m_pathStatic.nodes[idx].name });
    };
    auto selectLink = [this](int idx) {
        if (!m_model) return;
        if (idx < 0 || idx >= m_pathStatic.links.size()) return;
        m_model->setSelectedElementNames(
            QStringList{ m_pathStatic.links[idx].name });
    };
    // Open the existing TimeSeriesPlotDialog for the given object.  Looks
    // up the active .out path from the project window's canvas (same
    // strategy as SWMMVis::openTimeSeriesPlotFor); shows a message box
    // when no results are loaded so the user knows to run a simulation
    // or add a `.out` layer.
    auto openTimeSeriesPlotForRef = [this](const SWMMObjectRef &ref) {
        if (ref.objectType == SWMMObjectRef::Unknown || ref.name.isEmpty())
            return;
        QString outPath;
        if (m_projectWindow && m_projectWindow->canvas()) {
            for (OpenSWMMVisLayer *l : m_projectWindow->canvas()->layers()) {
                if (l->layerType() != OpenSWMMVisLayer::SWMMResultsLayer) continue;
                if (auto *rl = qobject_cast<SWMMResultsLayer *>(l)) {
                    outPath = rl->resultsFilePath();
                    break;
                }
            }
        }
        if (outPath.isEmpty()) {
            QMessageBox::information(this, tr("No results loaded"),
                tr("Run a simulation first (toolbar's Execute button) "
                   "or add a SWMM Output (.out) layer."));
            return;
        }
        auto *dlg = new TimeSeriesPlotDialog(outPath, ref, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    };
    auto plotTimeSeriesForNode = [this, openTimeSeriesPlotForRef](int idx) {
        if (idx < 0 || idx >= m_pathStatic.nodes.size()) return;
        SWMMObjectRef ref;
        ref.objectType = SWMMObjectRef::Node;
        ref.name       = m_pathStatic.nodes[idx].name;
        openTimeSeriesPlotForRef(ref);
    };
    auto plotTimeSeriesForLink = [this, openTimeSeriesPlotForRef](int idx) {
        if (idx < 0 || idx >= m_pathStatic.links.size()) return;
        SWMMObjectRef ref;
        ref.objectType = SWMMObjectRef::Link;
        ref.name       = m_pathStatic.links[idx].name;
        openTimeSeriesPlotForRef(ref);
    };

    connect(m_plot, &ProfilePlotWidget::nodeRightClicked, this,
            [this, zoomToNode, selectNode, openOptionsDialog,
             plotTimeSeriesForNode](int idx, const QPoint &globalPos) {
        QMenu menu(this);
        QAction *zoomAct = menu.addAction(tr("Zoom to on map"));
        QAction *plotAct = menu.addAction(tr("Plot Time Series…"));
        QAction *propAct = menu.addAction(tr("Properties…"));
        QAction *chosen  = menu.exec(globalPos);
        if (chosen == zoomAct) { selectNode(idx); zoomToNode(idx); }
        else if (chosen == plotAct) { selectNode(idx); plotTimeSeriesForNode(idx); }
        else if (chosen == propAct) { openOptionsDialog(); }
    });
    connect(m_plot, &ProfilePlotWidget::linkRightClicked, this,
            [this, zoomToLink, selectLink, openOptionsDialog,
             plotTimeSeriesForLink](int idx, const QPoint &globalPos) {
        QMenu menu(this);
        QAction *zoomAct = menu.addAction(tr("Zoom to on map"));
        QAction *plotAct = menu.addAction(tr("Plot Time Series…"));
        QAction *propAct = menu.addAction(tr("Properties…"));
        QAction *chosen  = menu.exec(globalPos);
        if (chosen == zoomAct) { selectLink(idx); zoomToLink(idx); }
        else if (chosen == plotAct) { selectLink(idx); plotTimeSeriesForLink(idx); }
        else if (chosen == propAct) { openOptionsDialog(); }
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
    if (!m_anim) {
        m_sourceButton->setText(tr("Sources"));
        return;
    }

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
    {
        const auto allLayers = m_anim->allLayers();
        for (SWMMResultsLayer *layer : allLayers) {
            if (!layer) continue;
            layerByFile.insert(layer->resultsFilePath(), layer);
        }
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

    const QList<SWMMResultsLayer *> layers = m_anim->allLayers();
    int total = 0;
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
    m_sourceButton->setText(tr("Sources (%1/%1)").arg(total));
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
        }
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void ProfilePlotDialog::invalidateSourceCacheFor(SWMMResultsLayer *layer)
{
    if (!layer) return;
    m_sourceCache.remove(layer);
}

void ProfilePlotDialog::ensureCacheInvalidationWired(SWMMResultsLayer *layer)
{
    if (!layer) return;
    if (m_cacheWired.contains(layer)) return;
    m_cacheWired.insert(layer);

    // New `.out` opened on this layer (re-run, "Open Results", etc.) →
    // the cached SourceDerived is now stale.
    connect(layer, &SWMMResultsLayer::resultsOpened,
            this, [this, layer]() { invalidateSourceCacheFor(layer); });
    // Layer pointed at a different results file.
    connect(layer, &SWMMResultsLayer::resultsFilePathChanged,
            this, [this, layer]() { invalidateSourceCacheFor(layer); });
    // Layer destroyed → drop the cache entry AND the wired flag so the
    // hash never holds a dangling key.
    connect(layer, &QObject::destroyed,
            this, [this, layer]() {
                m_sourceCache.remove(layer);
                m_cacheWired.remove(layer);
            });
}

// ---------------------------------------------------------------------------
// Animation-cursor sync
// ---------------------------------------------------------------------------

void ProfilePlotDialog::rebuildTerrainSamples()
{
    m_pathStatic.terrainSamples.clear();
    if (!m_options || !m_options->useTerrainGround()) return;
    if (!m_projectWindow || !m_model)                                   return;
    if (m_routerPath.linkIds.size() + 1 != m_routerPath.nodes.size())   return;
    if (m_pathStatic.chainage.size() != m_pathStatic.nodes.size())      return;

    // Live-look up the *current* terrain + vertical factor + canvas SRS
    // from the project window so the ground line always reflects the
    // toolbar's latest selection — even when the user added/changed the
    // terrain after this dialog was opened.
    GISRasterLayer *terrain     = m_projectWindow->activeTerrain();
    if (!terrain) return;
    const double terrainFactor  = m_projectWindow->terrainVertFactor();
    const SpatialReferenceSystem *canvasSRS =
        m_canvas ? m_canvas->canvasSRS() : nullptr;

    constexpr int    kMaxSamplesPerSegment = 20;
    constexpr double kSampleStepHint       = 5.0;  // model units

    // The model's cached node / polyline coords live in the *model layer*
    // CRS.  GISRasterLayer::valueAt expects coords in the *canvas* CRS
    // (it does its own canvas→raster transform internally).  When the
    // two differ we have to project each model-CRS sample into canvas-CRS
    // first, otherwise we'd sample the raster at the wrong pixel and the
    // ground line ends up misaligned with the network.
    SpatialReferenceSystem *modelSRS = m_model->srs();
    OGRCoordinateTransformation *modelToCanvas = nullptr;
    const bool needTransform =
        (modelSRS && canvasSRS && !modelSRS->equals(*canvasSRS));
    if (needTransform)
        modelToCanvas = modelSRS->createTransformationTo(*canvasSRS);

    auto sampleZ = [terrain, canvasSRS, modelToCanvas](double mx, double my, bool &ok) {
        double cx = mx, cy = my;
        if (modelToCanvas) {
            double tx = mx, ty = my;
            if (modelToCanvas->Transform(1, &tx, &ty)) {
                cx = tx; cy = ty;
            }
        }
        return terrain->valueAt(cx, cy, canvasSRS, /*band=*/1, &ok);
    };

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
                bool ok = false;
                const double zRaw = sampleZ(x, y, ok);
                if (!ok || !std::isfinite(zRaw)) continue;   // outside DEM
                // Convert raster vertical unit → model vertical unit so
                // the ground line lines up with the node inverts / rims.
                const double z = zRaw * terrainFactor;
                m_pathStatic.terrainSamples.push_back(
                    QPointF(chainForPoly(pd), z));
            }
        }
    }

    if (modelToCanvas) OGRCoordinateTransformation::DestroyCT(modelToCanvas);
}

void ProfilePlotDialog::onAnimationTimeChanged(const QDateTime &dt)
{
    if (!m_anim) return;
    auto *primary = m_anim->primaryLayer();
    if (!primary) return;
    const int period = primary->periodIndexForDateTime(dt);
    m_plot->setCurrentPeriod(/*sourceIdx=*/0, period);
    m_plot->setCurrentDateTime(dt);
}
