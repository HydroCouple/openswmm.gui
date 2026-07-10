/*!
 * \file   swmmvisprojectwindow.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "swmmvisprojectwindow.h"
#include "map/mapcanvas.h"
#include "layers/gisrasterlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmresultslayer.h"        // Slice QA.2 — registry hookup
#include "layers/swmm2dresultslayer.h"      // active 2D analysis layer
#include "mesh/meshenginesync.h"            // push mesh-layer edits into the engine before save
#include "output/outputstatsregistry.h"     // Slice QA.2 — owns the registry
#include "project/openswmmvisworkspace.h"
#include "project/projectserializer.h"      // Slice RB.1 — sidecar auto-create
#include "map/tools/maptoolpan.h"
#include "map/tools/maptoolzoom.h"
#include "map/tools/maptoolselect.h"
#include "map/tools/maptoolselectpolygon.h"
#include "map/tools/maptoolmeasure.h"
#include "map/tools/maptoolselectprofile.h"
#include "map/tools/maptooladdnode.h"
#include "map/tools/maptooladdlink.h"
#include "map/tools/maptooladdgage.h"
#include "map/tools/maptooladdsubcatchment.h"
#include "map/tools/maptooladdtext.h"
#include "map/tools/maptoolpick2dcells.h"
#include "map/tools/maptoolmeshprofile.h"
#include "map/tools/maptoolmeshselectvertex.h"
#include "map/tools/maptoolmeshselectedge.h"
#include "layers/annotationlayer.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "ui/dialogs/crsselectiondialog.h"

#include "core/openswmmvislogmessage.h"
#include "core/preferencesmanager.h"
#include "core/unitsystem.h"
#include "map/openswmmvisscene.h"
#include "plugins/filefilterregistry.h"
#include "selection/selectionmanager.h"
#include "mesh/meshobjectref.h"             // MeshCell ref parsing for cell highlight

#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include "core/measurementunitmanager.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

SWMMVisProjectWindow::SWMMVisProjectWindow(OpenSWMMVisWorkspace *workspace,
                                           const QString &filePath,
                                           QWidget *parent)
    : QMdiSubWindow(parent),
      mWorkspace(workspace)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumSize(200, 150);
    // NOTE: do NOT call setWindowFlags here. In TabbedView mode (the
    // .ui's default) re-flagging a QMdiSubWindow detaches it from the
    // tab stack and floats it as a free-standing window — not what we
    // want. The tab bar already renders the close X (tabsClosable=true)
    // and doesn't expose minimize/maximize on tabs, so the desired
    // "close-only, non-minimizable" affordance is the default.

    // Per-project services (parented to this window so they die with it)
    mUnits            = new UnitSystem(this);
    mSelectionManager = new SelectionManager(this);
    // Slice QA.2 — per-project output-identity registry. Layers wire
    // themselves in / out below via the MapCanvas layerAdded /
    // layerRemoved signals.
    mStatsRegistry    = new openswmmvis::OutputStatsRegistry(this);

    // Drive the 2D-results cell highlight from selection, keyed to the active
    // 2D results layer (results-analysis demarcation, Part E). Cell picks made
    // with MapToolPick2DCells land in the SelectionManager as MeshCell refs;
    // we mirror them onto the active 2D results layer only — never onto every
    // results layer (which is what the old mesh-toolbar path did).
    connect(mSelectionManager, &SelectionManager::selectionChanged, this,
            [this](const QSet<SWMMObjectRef> &, const QSet<SWMMObjectRef> &,
                   const QSet<SWMMObjectRef> &) { refreshActive2DCellHighlight(); });

    // Engine version — start from the persisted default (Preferences →
    // General → Default engine mode). The status-bar engine picker still
    // overrides per-project.
    {
        const QString defaultEngine =
            PreferencesManager::instance()->defaultEngineMode();
        if (!defaultEngine.isEmpty())
            mEngineVersion = defaultEngine;
    }

    // Canvas
    mCanvas = new MapCanvas(this);
    setWidget(mCanvas);

    // Slice QA.2 — keep the stats registry in lockstep with the
    // canvas's SWMMResultsLayer set. layerAdded fires after the layer
    // is in the canvas's layer list; layerRemoved fires before removal
    // completes. The registry's register/unregister are idempotent so
    // either ordering is safe.
    connect(mCanvas, &MapCanvas::layerAdded, this,
            [this](OpenSWMMVisLayer *layer) {
        if (auto *rl = qobject_cast<SWMMResultsLayer *>(layer)) {
            mStatsRegistry->registerLayer(rl, rl->resultsFilePath());
        }
    });
    connect(mCanvas, &MapCanvas::layerRemoved, this,
            [this](OpenSWMMVisLayer *layer) {
        if (auto *rl = qobject_cast<SWMMResultsLayer *>(layer)) {
            mStatsRegistry->unregisterLayer(rl);
        }
    });

    // Model layer
    mModelLayer = new SWMMModelLayer(filePath, workspace);
    mModelLayer->setName(filePath.isEmpty()
                             ? QStringLiteral("Untitled")
                             : QFileInfo(filePath).baseName());
    mModelLayer->setVisible(!filePath.isEmpty());

    // Mirror the prefs' link colours into the layer's per-link-type
    // symbol structs. The painter / GL renderers read the full QPen
    // straight from PreferencesManager so cap/join/style edits are
    // honoured end-to-end (and outlets pick up their own pen, not the
    // conduit fallback); this mirror exists only for the QSG renderer
    // and SWMMResultsLayer, which still read fillColor / size off
    // conduitSymbol() etc.
    auto applyLinkColorsFromPreferences = [this]() {
        auto *prefs = PreferencesManager::instance();

        auto conduit = mModelLayer->conduitSymbol();
        conduit.fillColor = prefs->linkColor(QStringLiteral("conduit"));
        mModelLayer->setConduitSymbol(conduit);

        auto pump = mModelLayer->pumpSymbol();
        pump.fillColor = prefs->linkColor(QStringLiteral("pump"));
        mModelLayer->setPumpSymbol(pump);

        auto orifice = mModelLayer->orificeSymbol();
        orifice.fillColor = prefs->linkColor(QStringLiteral("orifice"));
        mModelLayer->setOrificeSymbol(orifice);

        auto weir = mModelLayer->weirSymbol();
        weir.fillColor = prefs->linkColor(QStringLiteral("weir"));
        mModelLayer->setWeirSymbol(weir);
    };
    applyLinkColorsFromPreferences();

    // Mirror per-node-type pen / brush / size into the layer's symbol
    // structs. The painter / GL / QSG paths all read fill, outline, and
    // size off the SWMMElementSymbol so one push here covers every
    // renderer. PreferencesManager::nodePen / nodeBrush / nodeSize
    // normalise their argument internally, so the lower-case kind keys
    // below match the canonical "Junction" / "Outfall" / … forms.
    auto applyNodeStyleFromPreferences = [this]() {
        auto *prefs = PreferencesManager::instance();
        const QString kinds[4] = {
            QStringLiteral("junction"),
            QStringLiteral("outfall"),
            QStringLiteral("storage"),
            QStringLiteral("divider"),
        };
        SWMMElementSymbol syms[4] = {
            mModelLayer->junctionSymbol(),
            mModelLayer->outfallSymbol(),
            mModelLayer->storageSymbol(),
            mModelLayer->dividerSymbol(),
        };
        for (int i = 0; i < 4; ++i) {
            const QBrush fill    = prefs->nodeBrush(kinds[i]);
            const QPen   outline = prefs->nodePen(kinds[i]);
            const double sizePx  = prefs->nodeSize(kinds[i]);
            syms[i].fillColor    = fill.color();
            syms[i].outlineColor = outline.color();
            syms[i].outlineWidth = outline.widthF();
            syms[i].size         = sizePx;
        }
        mModelLayer->setJunctionSymbol(syms[0]);
        mModelLayer->setOutfallSymbol(syms[1]);
        mModelLayer->setStorageSymbol(syms[2]);
        mModelLayer->setDividerSymbol(syms[3]);
    };
    applyNodeStyleFromPreferences();

    connect(PreferencesManager::instance(), &PreferencesManager::preferenceChanged,
            this, [this, applyLinkColorsFromPreferences,
                   applyNodeStyleFromPreferences](const QString &group,
                                                  const QString &key) {
                if (group != QLatin1String("Rendering")) return;
                if (key.startsWith(QLatin1String("LinkPen/"))) {
                    applyLinkColorsFromPreferences();
                    // Painter renderers consume linkPen() directly —
                    // kick a repaint so width/cap/join edits land
                    // immediately even when the colour didn't change.
                    if (mModelLayer) emit mModelLayer->repaintRequested();
                }
                else if (key.startsWith(QLatin1String("NodePen/"))
                      || key.startsWith(QLatin1String("NodeBrush/"))
                      || key.startsWith(QLatin1String("NodeSize/"))) {
                    applyNodeStyleFromPreferences();
                    if (mModelLayer) emit mModelLayer->repaintRequested();
                }
            });

    // Hard-sync model-layer CRS → canvas CRS BEFORE addLayer. The "project
    // CRS" and the "SWMM model CRS" are conceptually a single thing;
    // whichever path mutates the SWMMModelLayer (LayerPropertiesDialog,
    // deserializer, direct setSRS during load) must drag the canvas with
    // it. Connecting before addLayer guarantees this lambda fires BEFORE
    // MapCanvas's per-layer srsChanged listener (added in addLayer) — so
    // the canvas SRS updates first, then the per-layer listener's
    // refreshScene + zoomToFullExtent runs against the correct canvas CRS
    // instead of stale geometry. Idempotent on same-authority repeats.
    connect(mModelLayer, &SWMMModelLayer::srsChanged, this,
            [this](SpatialReferenceSystem *layerSrs) {
                if (!layerSrs || !mCanvas) return;
                auto *canvasSrs = mCanvas->canvasSRS();
                if (canvasSrs && !canvasSrs->toAuthority().isEmpty()
                    && canvasSrs->toAuthority() == layerSrs->toAuthority())
                    return;
                // Geographic layer (lat/lon degrees): adopting it as the
                // canvas CRS would render Plate Carrée — vertically
                // compressed at mid-latitudes. Keep the projected canvas
                // CRS (Web Mercator by default) so reprojection happens
                // at the layer→canvas boundary and aspect stays correct.
                if (layerSrs->isGeographic()) {
                    mCanvas->zoomToFullExtent();
                    return;
                }
                mCanvas->setCanvasSRS(
                    new SpatialReferenceSystem(*layerSrs, mCanvas), true);
                // setCanvasSRS already fans out onCanvasCRSChanged to all
                // layers + emits canvasSRSChanged + refreshes the buffer;
                // refit so the user sees content at the new coordinates.
                mCanvas->zoomToFullExtent();
            });

    mCanvas->addLayer(mModelLayer, false);

    connect(mModelLayer, &SWMMModelLayer::modelLoaded,    this, &SWMMVisProjectWindow::modelLoaded);
    connect(mModelLayer, &SWMMModelLayer::modelLoadError, this, &SWMMVisProjectWindow::modelLoadError);

    // Tools — each tool is bound to mCanvas at construction
    mPanTool        = new OpenSWMMVisMapToolPan(mCanvas, this);
    mZoomInTool     = new OpenSWMMVisMapToolZoom(mCanvas, this);
    mZoomInTool->setZoomInMode(true);
    mZoomOutTool    = new OpenSWMMVisMapToolZoom(mCanvas, this);
    mZoomOutTool->setZoomInMode(false);
    mSelectTool        = new OpenSWMMVisMapToolSelect(mCanvas, this);
    mSelectPolygonTool = new OpenSWMMVisMapToolSelectPolygon(mCanvas, this);
    mMeasureTool       = new OpenSWMMVisMapToolMeasure(mCanvas, this);
    mSelectProfileTool = new OpenSWMMVisMapToolSelectProfile(mCanvas, this);
    // SWMM_NODE_JUNCTION=0, OUTFALL=1, STORAGE=2, DIVIDER=3
    // Pass element-kind keys so tools read the configurable prefix from PreferencesManager.
    mAddJunctionTool  = new OpenSWMMVisMapToolAddNode(mCanvas, 0, QStringLiteral("junction"),     this);
    mAddOutfallTool   = new OpenSWMMVisMapToolAddNode(mCanvas, 1, QStringLiteral("outfall"),      this);
    mAddStorageTool   = new OpenSWMMVisMapToolAddNode(mCanvas, 2, QStringLiteral("storage"),      this);
    mAddDividerTool   = new OpenSWMMVisMapToolAddNode(mCanvas, 3, QStringLiteral("divider"),      this);
    // SWMM_LINK: 0=Conduit, 1=Pump, 2=Orifice, 3=Weir, 4=Outlet
    mAddConduitTool   = new OpenSWMMVisMapToolAddLink(mCanvas, 0, QStringLiteral("conduit"),      this);
    mAddPumpTool      = new OpenSWMMVisMapToolAddLink(mCanvas, 1, QStringLiteral("pump"),         this);
    mAddOrificeTool   = new OpenSWMMVisMapToolAddLink(mCanvas, 2, QStringLiteral("orifice"),      this);
    mAddWeirTool      = new OpenSWMMVisMapToolAddLink(mCanvas, 3, QStringLiteral("weir"),         this);
    mAddOutletTool    = new OpenSWMMVisMapToolAddLink(mCanvas, 4, QStringLiteral("outlet"),       this);
    mAddGageTool      = new OpenSWMMVisMapToolAddGage(mCanvas, this);
    mAddSubcatchTool  = new OpenSWMMVisMapToolAddSubcatchment(mCanvas, this);
    // Annotation layer is lazy — created on first text placement or on
    // project restore — so projects with no annotations stay tidy in the
    // layer tree. The AddText tool is also lazy; activateAddTextTool()
    // wires both before flipping the tool active.
    mAnnotationLayer = nullptr;
    mAddTextTool     = nullptr;

    // Terrain Z readout: sample, convert to model vertical units, push to canvas.
    connect(mCanvas, &MapCanvas::cursorPositionChanged, this,
            [this](double mapX, double mapY) {
                if (!mActiveTerrain) {
                    mCanvas->setTerrainElevation({});
                    return;
                }
                bool ok = false;
                const double zRaw = mActiveTerrain->valueAt(mapX, mapY,
                                                             mCanvas->canvasSRS(),
                                                             1, &ok);
                // Convert from raster vertical unit to model vertical unit so
                // the displayed value and the node/link invert elevations are
                // in the same unit system.
                const double zModel = zRaw * mTerrainVertFactor;
                mCanvas->setTerrainElevation(ok ? std::optional<double>(zModel)
                                                : std::optional<double>{});
                mCanvas->setTerrainUnit(mUnits->depthLabel());
            });

    // Canvas label shows model vertical unit (Z is already converted above).
    mCanvas->setTerrainUnit(mUnits->depthLabel());

    // Recompute the vertical factor and update the canvas unit label whenever
    // the project's flow units change (e.g., user switches CFS ↔ CMS).
    connect(mUnits, &UnitSystem::unitsChanged, this,
            [this](swmm_FlowUnitsProperty) {
                const double rasterToSI = (mTerrainVertUnit == QLatin1String("ft"))
                                              ? 0.3048 : 1.0;
                const double modelToSI  = mUnits->isSI() ? 1.0 : 0.3048;
                mTerrainVertFactor = rasterToSI / modelToSI;
                mCanvas->setTerrainUnit(mUnits->depthLabel());
                // Re-propagate updated factor to map tools.
                for (auto *t : { mAddJunctionTool, mAddOutfallTool,
                                  mAddStorageTool,  mAddDividerTool })
                    if (t) t->setTerrain(mActiveTerrain, mTerrainNodeOffset,
                                         mTerrainVertFactor);
                for (auto *t : { mAddConduitTool, mAddPumpTool,
                                  mAddOrificeTool, mAddWeirTool, mAddOutletTool })
                    if (t) t->setTerrain(mActiveTerrain, mTerrainLinkOffset,
                                         mTerrainVertFactor);
            });

    // ---------------------------------------------------------------------------
    // Measure tool floating panel (child of mCanvas, shown/hidden by tool state)
    // ---------------------------------------------------------------------------
    {
        mMeasurePanel = new QFrame(mCanvas);
        mMeasurePanel->setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
        mMeasurePanel->setAutoFillBackground(true);

        QHBoxLayout *panelLayout = new QHBoxLayout(mMeasurePanel);
        panelLayout->setContentsMargins(6, 3, 6, 3);
        panelLayout->setSpacing(6);

        panelLayout->addWidget(new QLabel(QStringLiteral("Mode:"), mMeasurePanel));

        mMeasureModeCombo = new QComboBox(mMeasurePanel);
        mMeasureModeCombo->addItem(QStringLiteral("Distance"),
                                   QVariant::fromValue(static_cast<int>(MeasureMode::Distance)));
        mMeasureModeCombo->addItem(QStringLiteral("Area"),
                                   QVariant::fromValue(static_cast<int>(MeasureMode::Area)));
        panelLayout->addWidget(mMeasureModeCombo);

        panelLayout->addWidget(new QLabel(QStringLiteral("Units:"), mMeasurePanel));

        mMeasureUnitCombo = new QComboBox(mMeasurePanel);
        panelLayout->addWidget(mMeasureUnitCombo);

        mMeasureTotalLabel = new QLabel(QStringLiteral("0.00 m"), mMeasurePanel);
        mMeasureTotalLabel->setMinimumWidth(110);
        mMeasureTotalLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        panelLayout->addWidget(mMeasureTotalLabel);

        QPushButton *clearBtn = new QPushButton(QStringLiteral("Clear"), mMeasurePanel);
        clearBtn->setFixedWidth(54);
        panelLayout->addWidget(clearBtn);

        mMeasurePanel->adjustSize();
        mMeasurePanel->hide();

        // Mode combo → update tool + repopulate unit combo
        connect(mMeasureModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx)
        {
            if (!mMeasureTool) return;
            const auto mode = (idx == 0) ? MeasureMode::Distance : MeasureMode::Area;
            mMeasureTool->setMode(mode);
            updateMeasureUnitCombo();
        });

        // Unit combo → update tool
        connect(mMeasureUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx)
        {
            if (!mMeasureTool || idx < 0) return;
            if (mMeasureTool->mode() == MeasureMode::Distance)
                mMeasureTool->setDistanceUnit(
                    static_cast<MeasurementUnitManager::DistanceUnit>(idx));
            else
                mMeasureTool->setAreaUnit(
                    static_cast<MeasurementUnitManager::AreaUnit>(idx));
        });

        // Clear button → reset measurement
        connect(clearBtn, &QPushButton::clicked,
                mMeasureTool, &OpenSWMMVisMapToolMeasure::clearMeasurement);

        // Total label ← tool total
        connect(mMeasureTool, &OpenSWMMVisMapToolMeasure::totalChanged,
                this, [this](double total)
        {
            if (!mMeasurePanel->isVisible()) return;
            QString sym;
            if (mMeasureTool->mode() == MeasureMode::Distance)
                sym = MeasurementUnitManager::distanceUnitSymbol(mMeasureTool->distanceUnit());
            else
                sym = MeasurementUnitManager::areaUnitSymbol(mMeasureTool->areaUnit());
            mMeasureTotalLabel->setText(
                QStringLiteral("%1 %2").arg(total, 0, 'f', 2).arg(sym));
        });

        // Show/hide + initialise combos when the active tool changes
        connect(mCanvas, &MapCanvas::activeToolChanged,
                this, [this](OpenSWMMVisMapTool *tool)
        {
            const bool isMeasure = (tool == mMeasureTool);
            if (isMeasure)
            {
                // Sync mode combo to tool's current mode without triggering slots
                QSignalBlocker mb(mMeasureModeCombo);
                mMeasureModeCombo->setCurrentIndex(
                    mMeasureTool->mode() == MeasureMode::Distance ? 0 : 1);
                updateMeasureUnitCombo();
            }
            mMeasurePanel->setVisible(isMeasure);
            if (isMeasure)
                repositionMeasurePanel();
        });

        // Profile-session exit: when the user leaves the profile tool to
        // pick up the Select tool while an accepted path is still drawn,
        // arm a one-shot to clear the overlay on the next canvas click.
        // Any other transition cancels the arming (so toggling profile
        // back on, or switching to a different tool, keeps the path).
        connect(mCanvas, &MapCanvas::activeToolChanged,
                this, [this](OpenSWMMVisMapTool *tool)
        {
            if (!mSelectProfileTool || !mSelectTool) return;
            if (tool == mSelectTool
                && !mSelectProfileTool->acceptedPath().linkIds.isEmpty()) {
                mClearProfileOnNextCanvasClick = true;
            } else {
                mClearProfileOnNextCanvasClick = false;
            }
        });

        // Reposition when canvas resizes
        mCanvas->installEventFilter(this);
    }

    // Auto-length — initial value comes from the PreferencesManager default
    // (Preferences → General → "Auto-length conduits on edit"). The
    // canvas dynamic property is what map tools read; status-bar toggles
    // continue to update both via setAutoLengthEnabled().
    {
        mAutoLengthEnabled = PreferencesManager::instance()->autoLengthEnabled();
        mCanvas->setProperty("autoLength", mAutoLengthEnabled);
    }

    // Default tool is driven by the PreferencesManager's `defaultTool`
    // key (Slice V). Out-of-the-box default is "Select" — legacy EPA
    // SWMM matches, and it lets the user click objects immediately on
    // load. Unknown values fall back to Select.
    {
        const QString defTool = PreferencesManager::instance()->defaultTool();
        if (defTool == QStringLiteral("Pan"))       mCanvas->setActiveTool(mPanTool);
        else if (defTool == QStringLiteral("Zoom")) mCanvas->setActiveTool(mZoomInTool);
        else                                         mCanvas->setActiveTool(mSelectTool);
    }

    // Window title
    setWindowTitle(filePath.isEmpty()
                       ? QStringLiteral("Untitled")
                       : QFileInfo(filePath).baseName());
}

SWMMVisProjectWindow::~SWMMVisProjectWindow() = default;

MapCanvas        *SWMMVisProjectWindow::canvas()           const { return mCanvas; }
SWMMModelLayer   *SWMMVisProjectWindow::modelLayer()       const { return mModelLayer; }
UnitSystem       *SWMMVisProjectWindow::unitSystem()       const { return mUnits; }
SelectionManager *SWMMVisProjectWindow::selectionManager() const { return mSelectionManager; }

void SWMMVisProjectWindow::reloadElevationOffsetModeFromEngine()
{
    if (!mModelLayer || !mModelLayer->engine()) return;
    char buf[32] = {};
    if (swmm_options_get(mModelLayer->engine(), "LINK_OFFSETS",
                         buf, sizeof(buf)) == 0)
    {
        mElevationOffsetMode =
            QString(buf).trimmed().compare("ELEVATION", Qt::CaseInsensitive) == 0;
    }
}

void SWMMVisProjectWindow::setElevationOffsetMode(bool elevation)
{
    if (mElevationOffsetMode == elevation)
        return;
    mElevationOffsetMode = elevation;

    if (mModelLayer && mModelLayer->engine())
    {
        swmm_options_set(mModelLayer->engine(), "LINK_OFFSETS",
                         elevation ? "ELEVATION" : "DEPTH");
        setHasChanges(true);
    }

    emit offsetModeChanged(elevation);
}

void SWMMVisProjectWindow::convertLinkOffsets(bool toElevation)
{
    if (!mModelLayer)
        return;
    mModelLayer->convertLinkOffsets(toElevation);
    setHasChanges(true);
}

bool SWMMVisProjectWindow::loadModel(QList<QString> &warnings, QList<QString> &errors)
{
    if (!mModelLayer->loadModel(warnings, errors))
        return false;
    return finishModelLoad(warnings, errors);
}

void SWMMVisProjectWindow::loadModelAsync()
{
    // Engine create+open (the dominant load cost — full .inp parse) runs in a
    // worker thread so the GUI stays responsive and the status-bar busy
    // indicator actually animates. Everything that touches Qt state — SoA
    // adoption, CRS resolution (may open a dialog), canvas zoom — happens
    // back on the GUI thread in the watcher's finished handler.
    struct AsyncOpenOutcome {
        SWMM_Engine engine = nullptr;
        QString     errorDetail;
        qint64      openMs = 0;
    };

    const QString path = mModelLayer->modelFilePath();

    // QPointer guards window teardown during the open: if this window is
    // closed before the worker finishes, the handler destroys the orphaned
    // engine instead of touching dead widgets.
    QPointer<SWMMVisProjectWindow> self(this);
    auto *watcher = new QFutureWatcher<AsyncOpenOutcome>();
    QObject::connect(watcher, &QFutureWatcherBase::finished, watcher,
                     [watcher, self]() {
        const AsyncOpenOutcome outcome = watcher->result();
        watcher->deleteLater();

        if (!self) {
            if (outcome.engine)
                swmm_engine_destroy(outcome.engine);
            return;
        }

        QList<QString> warnings, errors;
        bool ok = false;
        if (!outcome.engine) {
            errors.append(outcome.errorDetail);
        } else {
            ok = self->mModelLayer->adoptOpenEngine(outcome.engine,
                                                    warnings, errors,
                                                    outcome.openMs)
                 && self->finishModelLoad(warnings, errors);
        }
        emit self->modelLoadFinished(ok, warnings, errors);
    });

    watcher->setFuture(QtConcurrent::run([path]() {
        AsyncOpenOutcome outcome;
        outcome.engine = SWMMModelLayer::openEngineForPath(
            path, &outcome.errorDetail, &outcome.openMs);
        return outcome;
    }));
}

bool SWMMVisProjectWindow::finishModelLoad(QList<QString> &warnings, QList<QString> &errors)
{
    Q_UNUSED(warnings);
    {
        mHasChanges = false;
        updateWindowTitle();
        mModelLayer->setVisible(true);

        // Sync per-project options from the engine.
        if (mModelLayer->engine())
        {
            mUnits->syncFromEngine(mModelLayer->engine());

            char buf[32] = {};
            if (swmm_options_get(mModelLayer->engine(), "LINK_OFFSETS",
                                 buf, sizeof(buf)) == 0)
            {
                mElevationOffsetMode =
                    QString(buf).trimmed().compare("ELEVATION", Qt::CaseInsensitive) == 0;
            }
        }

        // ---- SelectionManager bridge (Phase 1.4) -----------------------------
        // The map layer carries a name-only selection set; the SelectionManager
        // is the cross-view bus typed by SWMMObjectRef. Bridge in both
        // directions, guarding re-entrancy with a small flag captured in the
        // lambda so a manager-driven set doesn't bounce back and re-set the
        // manager (and vice versa).
        auto *busy = new bool(false);
        connect(mSelectionManager, &SelectionManager::selectionChanged, this,
                [this, busy](const QSet<SWMMObjectRef> &current,
                             const QSet<SWMMObjectRef> &, const QSet<SWMMObjectRef> &) {
                    if (*busy) return;
                    *busy = true;
                    // Typed hand-off: SWMM names are per-type namespaces, so
                    // each ref carries its kind into the layer — selecting a
                    // subcatchment in the Object Browser must not light up a
                    // same-named rain gage on the map.
                    QVector<SWMMModelLayer::SelectedElement> sel;
                    sel.reserve(current.size());
                    for (const SWMMObjectRef &r : current) {
                        quint8 kind = 0;
                        switch (r.objectType) {
                        case SWMMObjectRef::Node:
                            kind = SWMMModelLayer::kKindNode;  break;
                        case SWMMObjectRef::Link:
                            kind = SWMMModelLayer::kKindLink;  break;
                        case SWMMObjectRef::Subcatchment:
                            kind = SWMMModelLayer::kKindCatch; break;
                        case SWMMObjectRef::RainGage:
                            kind = SWMMModelLayer::kKindGage;  break;
                        default:
                            continue;
                        }
                        sel.append({r.name, kind});
                    }
                    mModelLayer->setSelectedElements(sel);
                    *busy = false;
                });
        connect(mModelLayer, &SWMMModelLayer::selectionChanged, this,
                [this, busy](const QStringList &) {
                    if (*busy) return;
                    *busy = true;
                    // Read the layer's TYPED selection rather than deriving a
                    // kind per name (objectTypeFor is a single-keyed hash that
                    // picks an arbitrary winner for names shared across kinds).
                    QSet<SWMMObjectRef> refs;
                    const auto &sel = mModelLayer->selectedElements();
                    refs.reserve(sel.size());
                    for (const auto &e : sel)
                    {
                        if (e.kinds & SWMMModelLayer::kKindNode)
                            refs.insert({SWMMObjectRef::Node, e.name});
                        if (e.kinds & SWMMModelLayer::kKindLink)
                            refs.insert({SWMMObjectRef::Link, e.name});
                        if (e.kinds & SWMMModelLayer::kKindCatch)
                            refs.insert({SWMMObjectRef::Subcatchment, e.name});
                        if (e.kinds & SWMMModelLayer::kKindGage)
                            refs.insert({SWMMObjectRef::RainGage, e.name});
                    }
                    mSelectionManager->select(refs, SelectionManager::Replace);
                    *busy = false;
                });
        // Lifetime: `busy` lives as long as the window; lambdas hold the
        // pointer by value but they're destroyed with the window via
        // QObject parent ownership, so leak risk is bounded.
        connect(this, &QObject::destroyed, [busy]() { delete busy; });

        // Adopt the model's CRS. If the .inp didn't carry one, the layer
        // falls back to LOCAL_CS["Untitled"]. A valid CRS is required for
        // on-the-fly reprojection of basemaps and feature layers, so loop
        // the picker until the user chooses one. If they explicitly abort,
        // push a loader error and return false so the caller closes the
        // project window.
        if (!mCanvasCRSAdopted)
        {
            SpatialReferenceSystem *modelSRS = mModelLayer->srs();
            // Only prompt when the CRS is truly unknown ("Untitled (Local)").
            // Auto-generated local CRS ("Local (ft)" / "Local (m)") already
            // has correct units — no user intervention required.
            const bool isUntitledLocal = modelSRS
                && modelSRS->toAuthority() == QStringLiteral("Local")
                && modelSRS->description() == QStringLiteral("Untitled (Local)");

            if (isUntitledLocal)
            {
                while (true)
                {
                    CRSSelectionDialog dlg(this);
                    dlg.setWindowTitle(tr("Coordinate Reference System"));
                    if (dlg.exec() == QDialog::Accepted)
                    {
                        if (SpatialReferenceSystem *picked = dlg.selectedSRS())
                        {
                            mModelLayer->setSRS(picked, true);
                            modelSRS = picked;
                            break;
                        }
                        // Accepted with no selection — treat as cancel.
                    }

                    QMessageBox mb(this);
                    mb.setIcon(QMessageBox::Warning);
                    mb.setWindowTitle(tr("CRS Required"));
                    mb.setText(tr("A coordinate reference system is required to open this SWMM model."));
                    mb.setInformativeText(tr("Choose a CRS to continue, or abort opening the project."));
                    QPushButton *chooseBtn = mb.addButton(tr("Choose CRS…"), QMessageBox::AcceptRole);
                    // Local-projected shortcut: matches the model's flow-unit
                    // system (ft vs. m) so 2D mesh generation has a usable
                    // linear unit without forcing the user through the picker.
                    const QString lenLabel = (mUnits && mUnits->isSI())
                                                 ? QStringLiteral("m")
                                                 : QStringLiteral("ft");
                    QPushButton *localBtn  = mb.addButton(
                        tr("Use local projected (%1)").arg(lenLabel),
                        QMessageBox::AcceptRole);
                    QPushButton *abortBtn  = mb.addButton(tr("Abort Open"),  QMessageBox::RejectRole);
                    mb.setDefaultButton(chooseBtn);
                    mb.exec();
                    if (mb.clickedButton() == abortBtn)
                    {
                        errors.append(tr("Project open cancelled: no CRS selected."));
                        mModelLayer->setVisible(false);
                        return false;
                    }
                    if (mb.clickedButton() == localBtn)
                    {
                        const QString mapUnits = (mUnits && mUnits->isSI())
                                                     ? QStringLiteral("METERS")
                                                     : QStringLiteral("FEET");
                        if (SpatialReferenceSystem *local =
                                SpatialReferenceSystem::localFromMapUnits(mapUnits))
                        {
                            mModelLayer->setSRS(local, true);
                            modelSRS = local;
                            break;
                        }
                    }
                    // else (Choose CRS…) loop back and re-open the picker.
                    Q_UNUSED(chooseBtn);
                }
            }

            if (modelSRS && !modelSRS->isGeographic())
            {
                mCanvas->setCanvasSRS(new SpatialReferenceSystem(*modelSRS, mCanvas), true);
                mCanvasCRSAdopted = true;
            }
            else if (modelSRS && modelSRS->isGeographic())
            {
                // Geographic model: keep the projected canvas CRS (Web
                // Mercator by default) so the layer→canvas reprojection
                // pipeline kicks in. Adopting EPSG:4326 here would render
                // Plate Carrée and squash the N-S axis by cos(centre lat).
                mCanvasCRSAdopted = true;
            }
        }

        const MapExtent ext = mModelLayer->extent();
        const QString diag = QStringLiteral(
            "[loadModel] %1 | extent valid=%2 [%3,%4 -> %5,%6] | canvas %7x%8 | SRS=%9")
            .arg(QFileInfo(mModelLayer->modelFilePath()).fileName())
            .arg(ext.isValid() ? "yes" : "no")
            .arg(ext.xMin(), 0, 'g', 6).arg(ext.yMin(), 0, 'g', 6)
            .arg(ext.xMax(), 0, 'g', 6).arg(ext.yMax(), 0, 'g', 6)
            .arg(mCanvas->width()).arg(mCanvas->height())
            .arg(mCanvas->canvasSRS() ? mCanvas->canvasSRS()->toAuthority() : "none");
        qDebug() << diag;
        // Mirror to the in-app log so the user can see it without a terminal.
        if (auto *mw = window())
            QMetaObject::invokeMethod(mw, "onLogMessage", Qt::QueuedConnection,
                                      Q_ARG(QString, diag),
                                      Q_ARG(OpenSWMMVisLogMessage::LogMessageType,
                                            OpenSWMMVisLogMessage::LogMessageType::Information));

        // Reliable "default = zoom to project extent" on load. The canvas may
        // still be 0×0 right after loadModel returns (the MDI subwindow hasn't
        // finished its show + resize cycle yet), so we self-reschedule until
        // the canvas has real dimensions. Capped at ~1 s of retries.
        auto *attempt = new QTimer(mCanvas);
        attempt->setSingleShot(true);
        attempt->setInterval(50);
        auto *attemptCount = new int(0);
        QObject::connect(attempt, &QTimer::timeout, mCanvas, [this, attempt, attemptCount, mw = window()]() {
            if (mCanvas->width() <= 0 || mCanvas->height() <= 0) {
                if (++*attemptCount < 20) {           // ≤ 1 s total
                    attempt->start();
                    return;
                }
                // Give up; canvas never sized — release allocations.
                delete attemptCount;
                attempt->deleteLater();
                return;
            }
            delete attemptCount;
            attempt->deleteLater();

            mCanvas->zoomToFullExtent();
            // Force scene repopulation immediately rather than waiting for the
            // 50 ms Scene-channel debounce of the new invalidate() API — the
            // diagnostic below samples scene items 50 ms later so we'd race.
            // The legacy refreshLayerItems() entry is preserved precisely for
            // callers that need synchronous behavior; everything else should
            // go through MapCanvas::invalidate().
            mCanvas->refreshLayerItems();

            // Sample scene state after the synchronous repopulation.
            QTimer::singleShot(50, mCanvas, [this, mw]() {
                const MapExtent canvasExt = mCanvas->extent();
                const int sceneItems = mCanvas->mapScene() ? mCanvas->mapScene()->items().count() : -1;
                const QString d2 = QStringLiteral(
                    "[postZoom] canvas %1x%2 | extent [%3,%4 -> %5,%6] | scene items=%7")
                    .arg(mCanvas->width()).arg(mCanvas->height())
                    .arg(canvasExt.xMin(), 0, 'g', 6).arg(canvasExt.yMin(), 0, 'g', 6)
                    .arg(canvasExt.xMax(), 0, 'g', 6).arg(canvasExt.yMax(), 0, 'g', 6)
                    .arg(sceneItems);
                qDebug() << d2;
                if (mw)
                    QMetaObject::invokeMethod(mw, "onLogMessage", Qt::QueuedConnection,
                                              Q_ARG(QString, d2),
                                              Q_ARG(OpenSWMMVisLogMessage::LogMessageType,
                                                    OpenSWMMVisLogMessage::LogMessageType::Information));
            });
        });
        attempt->start();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Dirty state
// ---------------------------------------------------------------------------

void SWMMVisProjectWindow::setHasChanges(bool dirty)
{
    if (mHasChanges == dirty)
        return;
    mHasChanges = dirty;
    updateWindowTitle();
    emit hasChangesChanged(dirty);
}

void SWMMVisProjectWindow::setEditSessionActive(bool active)
{
    if (mEditSessionActive == active)
        return;
    mEditSessionActive = active;
    emit editSessionChanged(active);
}

void SWMMVisProjectWindow::updateWindowTitle()
{
    // Untitled wins over modelFilePath — even when an untitled window is
    // backed by a temp .inp, surfacing the temp filename to the user is
    // confusing. The window stays "Untitled" until Save As replaces it.
    QString base = (mUntitled || !mModelLayer ||
                    mModelLayer->modelFilePath().isEmpty())
                       ? QStringLiteral("Untitled")
                       : QFileInfo(mModelLayer->modelFilePath()).baseName();
    setWindowTitle(mHasChanges ? (base + QStringLiteral(" *")) : base);
}

void SWMMVisProjectWindow::markUntitled(const QString &tempInpPath)
{
    mUntitled = true;
    mTempInpPath = tempInpPath;
    updateWindowTitle();
}

// ---------------------------------------------------------------------------
// Save / Save As
// ---------------------------------------------------------------------------

bool SWMMVisProjectWindow::save(QString *errorOut)
{
    if (!mModelLayer || mModelLayer->modelFilePath().isEmpty() || mUntitled)
    {
        // Untitled projects don't have a real path yet — fall through to
        // Save As. The error string is informational; the caller (SWMMVis::
        // onSaveProject) reads it and routes to the dialog.
        if (errorOut) *errorOut = tr("No file path set; use Save As.");
        return false;
    }
    return saveAs(mModelLayer->modelFilePath(), errorOut);
}

bool SWMMVisProjectWindow::saveAs(const QString &newPath, QString *errorOut)
{
    if (!mModelLayer || !mModelLayer->engine())
    {
        if (errorOut) *errorOut = tr("No model loaded.");
        return false;
    }

    // AA-3.3 — pick the writer plugin by matching the path's extension
    // against FileFilterRegistry's InputRead entries (built-in `.inp`
    // writer + any plugin-supplied writers like GeoPackage).  Empty
    // pluginId == built-in `.inp` writer, so an unmatched extension
    // falls through to legacy behaviour without a separate code path.
    auto pluginIdForExt = [](const QString &ext) -> QString {
        auto *registry = openswmmvis::FileFilterRegistry::instance();
        for (const auto &entry :
                registry->entriesFor(openswmmvis::FilterKind::InputRead)) {
            if (!entry.canWrite || !entry.enabled) continue;
            for (const QString &pat : entry.patterns) {
                QString patExt = pat;
                if (patExt.startsWith(QStringLiteral("*.")))
                    patExt = patExt.mid(2);
                if (QString::compare(patExt, ext, Qt::CaseInsensitive) == 0)
                    return entry.pluginId;
            }
        }
        return {};
    };

    const QString pluginId = pluginIdForExt(QFileInfo(newPath).suffix());

    // Mesh edits live on the SWMM2DMeshLayer (its own MeshResult / BC SoA),
    // not in the engine that the writer below serialises. Push them into the
    // engine's in-memory 2D mesh first so vertex-Z / conveyance / BC edits are
    // saved. The engine remains the source of truth for everything the GUI
    // mesh model does not carry (coupling maps, Manning's n, units, options).
    if (canvas()) {
        for (OpenSWMMVisLayer *l : canvas()->layers()) {
            auto *meshLayer = qobject_cast<SWMM2DMeshLayer *>(l);
            if (!meshLayer || meshLayer->mesh().vertices.isEmpty()) continue;
            QStringList syncWarnings;
            mesh::pushMeshEditsToEngine(mModelLayer->engine(), meshLayer->mesh(),
                                        meshLayer->edgeBCs(), &syncWarnings);
            for (const QString &w : syncWarnings)
                qWarning().noquote() << w;
        }
    }

    QByteArray utf8 = newPath.toUtf8();
    QByteArray idUtf8 = pluginId.toUtf8();
    int rc = swmm_model_write_with_plugin(
        mModelLayer->engine(),
        utf8.constData(),
        pluginId.isEmpty() ? nullptr : idUtf8.constData());
    if (rc != 0)
    {
        if (errorOut) *errorOut =
            pluginId.isEmpty()
              ? tr("swmm_model_write_with_plugin (built-in) failed (code %1)").arg(rc)
              : tr("swmm_model_write_with_plugin (\"%1\") failed (code %2)")
                    .arg(pluginId).arg(rc);
        return false;
    }
    // If saved to a new path, point the layer at it so subsequent Save targets the new file.
    if (newPath != mModelLayer->modelFilePath())
        mModelLayer->setModelFilePath(newPath);
    // First successful Save As of an untitled project — promote it. Delete
    // the temp .inp the dialog wrote earlier; the engine has already read
    // it into memory so the file is no longer needed.
    if (mUntitled)
    {
        if (!mTempInpPath.isEmpty() && QFile::exists(mTempInpPath))
            QFile::remove(mTempInpPath);
        mTempInpPath.clear();
        mUntitled = false;
        updateWindowTitle();
    }
    setHasChanges(false);

    // Slice RB.1+2 — sidecar auto-create. Every successful built-in .inp
    // write also produces a sibling .oswp project file. Plugin-driven
    // writes (non-empty pluginId, e.g. GeoPackage export) are deliberately
    // skipped — those are standalone exports per Slice AA-3.5 contract.
    // RB.2: log a one-line "Creating sibling project file:" sentinel the
    // first time the sidecar appears on disk so the user sees the
    // auto-create. Subsequent saves of an existing .oswp are silent.
    if (pluginId.isEmpty())
    {
        const QString oswpPath = ProjectSerializer::sidecarPathFor(newPath);
        if (!oswpPath.isEmpty())
        {
            const bool sidecarPreExisted = QFile::exists(oswpPath);
            QString sidecarErr;
            if (!ProjectSerializer::saveToFile(oswpPath, this, &sidecarErr))
            {
                qWarning() << "ProjectSerializer::saveToFile failed:" << sidecarErr;
                // Non-fatal — the .inp already saved, project is recoverable.
            }
            else if (!sidecarPreExisted)
            {
                qInfo().noquote()
                    << QStringLiteral("Creating sibling project file: %1").arg(oswpPath);
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Close event — prompt if unsaved changes
// ---------------------------------------------------------------------------
// Measure panel helpers
// ---------------------------------------------------------------------------

bool SWMMVisProjectWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == mCanvas
        && event->type() == QEvent::Resize
        && mMeasurePanel
        && mMeasurePanel->isVisible())
    {
        repositionMeasurePanel();
    }
    // Armed by the activeToolChanged listener when the user leaves
    // profile mode to pick up Select. Fires once on the next canvas
    // mouse press, then disarms — so a single click on the map ends
    // the profile session, but parking the Select tool without
    // clicking leaves the prior selection in place.
    if (watched == mCanvas
        && event->type() == QEvent::MouseButtonPress
        && mClearProfileOnNextCanvasClick
        && mSelectProfileTool)
    {
        mClearProfileOnNextCanvasClick = false;
        mSelectProfileTool->clearSelection();
    }
    return QMdiSubWindow::eventFilter(watched, event);
}

void SWMMVisProjectWindow::repositionMeasurePanel()
{
    if (!mMeasurePanel || !mCanvas)
        return;
    const int margin    = 8;
    const QSize sz      = mMeasurePanel->sizeHint();
    const int x         = (mCanvas->width()  - sz.width())  / 2;
    const int y         =  mCanvas->height() - sz.height()  - margin;
    mMeasurePanel->setGeometry(x, y, sz.width(), sz.height());
    mMeasurePanel->raise();
}

void SWMMVisProjectWindow::updateMeasureUnitCombo()
{
    if (!mMeasureUnitCombo || !mMeasureTool)
        return;

    QSignalBlocker blocker(mMeasureUnitCombo);
    mMeasureUnitCombo->clear();

    if (mMeasureTool->mode() == MeasureMode::Distance)
    {
        for (const QString &name : MeasurementUnitManager::distanceUnitNames())
            mMeasureUnitCombo->addItem(name);
        mMeasureUnitCombo->setCurrentIndex(
            static_cast<int>(mMeasureTool->distanceUnit()));
    }
    else
    {
        for (const QString &name : MeasurementUnitManager::areaUnitNames())
            mMeasureUnitCombo->addItem(name);
        mMeasureUnitCombo->setCurrentIndex(
            static_cast<int>(mMeasureTool->areaUnit()));
    }
}

// ---------------------------------------------------------------------------

void SWMMVisProjectWindow::closeEvent(QCloseEvent *event)
{
    if (mHasChanges)
    {
        const QString name = mUntitled
            ? QStringLiteral("Untitled")
            : QFileInfo(mModelLayer->modelFilePath()).baseName();
        QMessageBox::StandardButton btn = QMessageBox::question(
            this, tr("Save changes?"),
            tr("The model \"%1\" has unsaved changes. Save before closing?")
                .arg(name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (btn == QMessageBox::Cancel)
        {
            event->ignore();
            return;
        }
        if (btn == QMessageBox::Save)
        {
            QString err;
            if (!save(&err))
            {
                // Untitled → save() returns false with the "use Save As"
                // hint; the closeEvent can't open a Save As dialog
                // synchronously (would block the close path), so we cancel
                // the close and let the user trigger Save As manually.
                if (mUntitled)
                {
                    event->ignore();
                    QMessageBox::information(this, tr("Save As required"),
                        tr("Untitled projects need an explicit Save As — "
                           "use File → Save As before closing."));
                    return;
                }
                QMessageBox::critical(this, tr("Save failed"), err);
                event->ignore();
                return;
            }
        }
    }
    // Discard / no-changes path: clean up the temp .inp owned by an
    // untitled project so we don't leak it.
    if (mUntitled && !mTempInpPath.isEmpty() && QFile::exists(mTempInpPath))
        QFile::remove(mTempInpPath);
    // Final commit point — emit before the Qt teardown chain runs so
    // observers (profile-plot dialog, etc.) can still touch our model
    // layer / canvas / results layers in their handlers.
    emit aboutToClose();
    QMdiSubWindow::closeEvent(event);
}

void SWMMVisProjectWindow::changeEvent(QEvent *event)
{
    // NOTE: deliberately a pass-through. QMdiArea's TabbedView uses
    // Qt::WindowMinimized internally to hide inactive sub-windows —
    // blocking the minimize state causes every tab's content to stack on
    // top of the active one. There is no user-visible minimize affordance
    // in TabbedView anyway (tabs don't expose one), so denying minimize
    // is unnecessary.
    QMdiSubWindow::changeEvent(event);
}

void SWMMVisProjectWindow::activatePanTool()         { mCanvas->setActiveTool(mPanTool); }
void SWMMVisProjectWindow::activateZoomInTool()      { mCanvas->setActiveTool(mZoomInTool); }
void SWMMVisProjectWindow::activateZoomOutTool()     { mCanvas->setActiveTool(mZoomOutTool); }
void SWMMVisProjectWindow::activateSelectTool()      { mCanvas->setActiveTool(mSelectTool); }
void SWMMVisProjectWindow::activateSelectByPolygonTool() { mCanvas->setActiveTool(mSelectPolygonTool); }

void SWMMVisProjectWindow::activatePick2DCellsTool()
{
    if (!mPick2DCellsTool) {
        mPick2DCellsTool = new MapToolPick2DCells(mCanvas, mSelectionManager, this);
        // Forward the cellsPicked signal up to SWMMVis (parent path).
        QObject::connect(mPick2DCellsTool, &MapToolPick2DCells::cellsPicked,
                         this,             &SWMMVisProjectWindow::pick2DCellsPicked);
    }
    mCanvas->setActiveTool(mPick2DCellsTool);
}

void SWMMVisProjectWindow::activateMeshSelectVertexTool()
{
    if (!mMeshSelectVertexTool) {
        mMeshSelectVertexTool = new MapToolMeshSelectVertex(mCanvas, mSelectionManager, this);
        QObject::connect(mMeshSelectVertexTool, &MapToolMeshSelectVertex::plotVertexSeriesRequested,
                         this,                  &SWMMVisProjectWindow::meshVertexSeriesRequested);
    }
    mCanvas->setActiveTool(mMeshSelectVertexTool);
}

void SWMMVisProjectWindow::activateMeshSelectEdgeTool()
{
    if (!mMeshSelectEdgeTool) {
        mMeshSelectEdgeTool = new MapToolMeshSelectEdge(mCanvas, mSelectionManager, this);
        QObject::connect(mMeshSelectEdgeTool, &MapToolMeshSelectEdge::plotEdgeFluxRequested,
                         this,                &SWMMVisProjectWindow::meshEdgeFluxRequested);
    }
    mCanvas->setActiveTool(mMeshSelectEdgeTool);
}
void SWMMVisProjectWindow::activateMeasureTool()
{
    if (mCanvas->activeTool() == mMeasureTool)
        mCanvas->setActiveTool(mSelectTool);
    else
        mCanvas->setActiveTool(mMeasureTool);
}
void SWMMVisProjectWindow::activateSelectProfileTool()
{
    if (mCanvas->activeTool() == mSelectProfileTool)
        mCanvas->setActiveTool(mSelectTool);
    else
        mCanvas->setActiveTool(mSelectProfileTool);
}

void SWMMVisProjectWindow::activateMeshProfileTool()
{
    if (mMeshProfileTool && mCanvas->activeTool() == mMeshProfileTool) {
        mCanvas->setActiveTool(mSelectTool);
        return;
    }
    if (!mMeshProfileTool) {
        mMeshProfileTool = new MapToolMeshProfile(mCanvas, this);
        // Forward the finished polyline up to SWMMVis (parent path) on the
        // bed-only channel (US.A1).
        QObject::connect(mMeshProfileTool, &MapToolMeshProfile::profilePathTraced,
                         this,             &SWMMVisProjectWindow::meshProfileTraced);
    }
    mCanvas->setActiveTool(mMeshProfileTool);
}

void SWMMVisProjectWindow::activateAnalysisMeshProfileTool()
{
    // Slice US.A1 — Analysis-toolbar 2D surface profile. A second, independent
    // MapToolMeshProfile so this tool's checked state tracks actionPlotProfile
    // while the mesh-toolbar tool tracks actionMeshProfile.
    if (mAnalysisMeshProfileTool && mCanvas->activeTool() == mAnalysisMeshProfileTool) {
        mCanvas->setActiveTool(mSelectTool);
        return;
    }
    if (!mAnalysisMeshProfileTool) {
        mAnalysisMeshProfileTool = new MapToolMeshProfile(mCanvas, this);
        QObject::connect(mAnalysisMeshProfileTool, &MapToolMeshProfile::profilePathTraced,
                         this, &SWMMVisProjectWindow::analysisMeshProfileTraced);
    }
    mCanvas->setActiveTool(mAnalysisMeshProfileTool);
}

bool SWMMVisProjectWindow::hasModelLayer() const
{
    if (!mCanvas) return false;
    for (OpenSWMMVisLayer *l : mCanvas->layers())
        if (qobject_cast<SWMMModelLayer *>(l)) return true;
    return false;
}

bool SWMMVisProjectWindow::hasMeshLayer() const
{
    if (!mCanvas) return false;
    for (OpenSWMMVisLayer *l : mCanvas->layers())
        if (qobject_cast<SWMM2DMeshLayer *>(l)) return true;
    return false;
}
void SWMMVisProjectWindow::activateAddJunctionTool()    { mCanvas->setActiveTool(mAddJunctionTool); }
void SWMMVisProjectWindow::activateAddOutfallTool()     { mCanvas->setActiveTool(mAddOutfallTool); }
void SWMMVisProjectWindow::activateAddStorageTool()     { mCanvas->setActiveTool(mAddStorageTool); }
void SWMMVisProjectWindow::activateAddDividerTool()     { mCanvas->setActiveTool(mAddDividerTool); }
void SWMMVisProjectWindow::activateAddConduitTool()     { mCanvas->setActiveTool(mAddConduitTool); }
void SWMMVisProjectWindow::activateAddPumpTool()        { mCanvas->setActiveTool(mAddPumpTool); }
void SWMMVisProjectWindow::activateAddOrificeTool()     { mCanvas->setActiveTool(mAddOrificeTool); }
void SWMMVisProjectWindow::activateAddWeirTool()        { mCanvas->setActiveTool(mAddWeirTool); }
void SWMMVisProjectWindow::activateAddOutletTool()      { mCanvas->setActiveTool(mAddOutletTool); }
void SWMMVisProjectWindow::activateAddGageTool()        { mCanvas->setActiveTool(mAddGageTool); }
void SWMMVisProjectWindow::activateAddSubcatchmentTool(){ mCanvas->setActiveTool(mAddSubcatchTool); }
void SWMMVisProjectWindow::activateAddTextTool()
{
    ensureAnnotationLayer();
    if (!mAddTextTool)
        mAddTextTool = new OpenSWMMVisMapToolAddText(mCanvas, mAnnotationLayer, this);
    mCanvas->setActiveTool(mAddTextTool);
}

OpenSWMMVisAnnotationLayer *SWMMVisProjectWindow::ensureAnnotationLayer()
{
    if (mAnnotationLayer)
        return mAnnotationLayer;
    mAnnotationLayer = new OpenSWMMVisAnnotationLayer(tr("Annotations"), nullptr);
    // pushUndo=false: the layer creation isn't itself an undoable user
    // action; it's a side-effect of the first AddAnnotationCommand, which
    // is what shows up in the undo stack.
    mCanvas->addLayer(mAnnotationLayer, /*pushUndo=*/false);
    return mAnnotationLayer;
}

void SWMMVisProjectWindow::zoomToFullExtent()        { mCanvas->zoomToFullExtent(); }

QHash<OpenSWMMVisMapTool *, QString> SWMMVisProjectWindow::toolActionKeys() const
{
    return {
        { mPanTool,            QStringLiteral("actionPan")            },
        { mZoomInTool,         QStringLiteral("actionZoomIn")         },
        { mZoomOutTool,        QStringLiteral("actionZoomOut")        },
        { mSelectTool,         QStringLiteral("actionSelect")         },
        { mSelectPolygonTool,  QStringLiteral("actionSelectByPolygon")},
        { mMeasureTool,        QStringLiteral("actionMeasure")        },
        { mSelectProfileTool,  QStringLiteral("actionPlotProfile")    },
        { mAddJunctionTool,    QStringLiteral("actionAddJunction")    },
        { mAddOutfallTool,     QStringLiteral("actionAddOutfall")     },
        { mAddStorageTool,     QStringLiteral("actionAddStorage")     },
        { mAddDividerTool,     QStringLiteral("actionAddFlowDivider") },
        { mAddConduitTool,     QStringLiteral("actionAddPipe")        },
        { mAddPumpTool,        QStringLiteral("actionAddPump")        },
        { mAddOrificeTool,     QStringLiteral("actionAddOrifice")     },
        { mAddWeirTool,        QStringLiteral("actionAddWeir")        },
        { mAddOutletTool,      QStringLiteral("actionAddOutlet")      },
        { mAddGageTool,        QStringLiteral("actionRainGauge")      },
        { mAddSubcatchTool,    QStringLiteral("actionAddSubcatchment")},
        // Lazy tools — null entries are tolerated by the caller's `if (tool)`
        // guards; the key reservation keeps the action's checkable state
        // tracked once the tool first instantiates.
        { mAddTextTool,          QStringLiteral("actionAddText")          },
        { mPick2DCellsTool,      QStringLiteral("actionPick2DCells")      },
        { mMeshProfileTool,      QStringLiteral("actionMeshProfile")      },
        // US.A1 — analysis mesh-profile tool shares the Plot Profile action's
        // checked state with the network select-profile tool (two tools per
        // key is safe in the checked-state sync).
        { mAnalysisMeshProfileTool, QStringLiteral("actionPlotProfile")   },
        // Step G — mesh-toolbar vertex/edge selectors join the canvas-level
        // active-tool radio so the general-purpose Select / 2D-cells picks
        // visually uncheck them (and vice versa). objectNames come from
        // MeshEditingToolbar::ctor.
        { mMeshSelectVertexTool, QStringLiteral("actionMeshSelectVertex") },
        { mMeshSelectEdgeTool,   QStringLiteral("actionMeshSelectEdge")   },
    };
}

void SWMMVisProjectWindow::setAutoLengthEnabled(bool enabled)
{
    if (mAutoLengthEnabled == enabled)
        return;
    mAutoLengthEnabled = enabled;
    if (mCanvas)
        mCanvas->setProperty("autoLength", enabled);
    PreferencesManager::instance()->setAutoLengthEnabled(enabled);
    emit autoLengthChanged(enabled);
}

void SWMMVisProjectWindow::setEngineVersion(const QString &version)
{
    mEngineVersion = version;
    setHasChanges(true);
}

// ── Terrain editing ───────────────────────────────────────────────────────────

QString SWMMVisProjectWindow::activeTerrainLayerPath() const
{
    return mActiveTerrain ? mActiveTerrain->filePath() : QString();
}

SWMMResultsLayer *SWMMVisProjectWindow::activeResultsLayer() const
{
    return mActiveResultsLayer;
}

SWMM2DResultsLayer *SWMMVisProjectWindow::active2DResultsLayer() const
{
    return mActive2DResultsLayer;
}

void SWMMVisProjectWindow::setActiveResultsLayer(SWMMResultsLayer *layer)
{
    // Reject a layer that isn't on this window's canvas (defensive — a stale
    // pointer from another tab must never become this tab's active layer).
    if (layer && (!mCanvas || !mCanvas->layers().contains(layer)))
        return;
    if (mActiveResultsLayer == layer)
        return;
    mActiveResultsLayer = layer;
    emit activeResultsLayerChanged(layer);
}

void SWMMVisProjectWindow::setActive2DResultsLayer(SWMM2DResultsLayer *layer)
{
    if (layer && (!mCanvas || !mCanvas->layers().contains(layer)))
        return;
    if (mActive2DResultsLayer == layer)
        return;
    mActive2DResultsLayer = layer;

    // Re-point the 2D cell-pick tool so graphical analysis selection targets
    // the active layer rather than the first-found one.
    if (mPick2DCellsTool)
        mPick2DCellsTool->setTargetLayer(layer);

    // Move the cell highlight to the newly-active layer (and clear it off any
    // other 2D results layer).
    refreshActive2DCellHighlight();

    emit active2DResultsLayerChanged(layer);
}

void SWMMVisProjectWindow::refreshActive2DCellHighlight()
{
    if (!mCanvas) return;

    // Collect the currently-selected MeshCell triangle indices.
    QSet<int> cellSet;
    if (mSelectionManager) {
        for (const SWMMObjectRef &ref : mSelectionManager->selection()) {
            if (ref.objectType != SWMMObjectRef::MeshCell) continue;
            QString lk; int tri = -1;
            if (mesh::MeshObjectRef::parseCell(ref, &lk, &tri) && tri >= 0)
                cellSet.insert(tri);
        }
    }

    // Apply the highlight to the active 2D results layer only; clear every
    // other 2D results layer so a stale highlight never lingers on a
    // de-activated run (fixes the old unkeyed push-to-all-layers behaviour).
    for (OpenSWMMVisLayer *l : mCanvas->layers()) {
        auto *res = qobject_cast<SWMM2DResultsLayer *>(l);
        if (!res) continue;
        if (res == mActive2DResultsLayer)
            res->highlightCells(cellSet);
        else
            res->highlightCells({});
    }
}

void SWMMVisProjectWindow::setActiveTerrain(GISRasterLayer *layer)
{
    if (mActiveTerrain == layer) return;
    mActiveTerrain = layer;

    // Propagate to every add-node and add-link tool (include vertical factor).
    const auto nodeTools = { mAddJunctionTool, mAddOutfallTool,
                              mAddStorageTool,  mAddDividerTool };
    for (auto *t : nodeTools)
        if (t) t->setTerrain(layer, mTerrainNodeOffset, mTerrainVertFactor);

    const auto linkTools = { mAddConduitTool, mAddPumpTool,
                              mAddOrificeTool, mAddWeirTool, mAddOutletTool };
    for (auto *t : linkTools)
        if (t) t->setTerrain(layer, mTerrainLinkOffset, mTerrainVertFactor);

    // Reset Z readout when terrain is cleared.
    if (!layer && mCanvas)
        mCanvas->setTerrainElevation({});

    emit activeTerrainChanged(layer);
    setHasChanges(true);
}

void SWMMVisProjectWindow::setTerrainNodeOffset(double offset)
{
    if (mTerrainNodeOffset == offset) return;
    mTerrainNodeOffset = offset;

    const auto nodeTools = { mAddJunctionTool, mAddOutfallTool,
                              mAddStorageTool,  mAddDividerTool };
    for (auto *t : nodeTools)
        if (t) t->setTerrain(mActiveTerrain, offset, mTerrainVertFactor);

    setHasChanges(true);
}

void SWMMVisProjectWindow::setTerrainLinkOffset(double offset)
{
    if (mTerrainLinkOffset == offset) return;
    mTerrainLinkOffset = offset;

    const auto linkTools = { mAddConduitTool, mAddPumpTool,
                              mAddOrificeTool, mAddWeirTool, mAddOutletTool };
    for (auto *t : linkTools)
        if (t) t->setTerrain(mActiveTerrain, offset, mTerrainVertFactor);

    setHasChanges(true);
}

void SWMMVisProjectWindow::setTerrainVerticalUnit(const QString &unit)
{
    const QString newUnit = unit.isEmpty() ? QStringLiteral("m") : unit;

    // Recompute conversion factor: rasterUnit → modelUnit. Must happen even
    // when the unit string is unchanged because the project-construction
    // default (`mTerrainVertFactor = 1.0`) doesn't reflect the model's
    // FLOW_UNITS — so the first DEM selection in a US-customary project
    // would otherwise display raw metres labelled as feet until the user
    // manually toggled the unit combo.
    const double rasterToSI = (newUnit == QLatin1String("ft")) ? 0.3048 : 1.0;
    const double modelToSI  = mUnits->isSI() ? 1.0 : 0.3048;
    const double newFactor  = rasterToSI / modelToSI;

    if (mTerrainVertUnit == newUnit && mTerrainVertFactor == newFactor)
        return;

    mTerrainVertUnit   = newUnit;
    mTerrainVertFactor = newFactor;

    // Canvas label shows the model unit since Z is converted before display.
    if (mCanvas)
        mCanvas->setTerrainUnit(mUnits->depthLabel());

    // Propagate the new factor to map tools (offset stays in model units;
    // the raw Z is multiplied by this factor before adding the offset).
    const auto nodeTools = { mAddJunctionTool, mAddOutfallTool,
                              mAddStorageTool,  mAddDividerTool };
    for (auto *t : nodeTools)
        if (t) t->setTerrain(mActiveTerrain, mTerrainNodeOffset, mTerrainVertFactor);

    const auto linkTools = { mAddConduitTool, mAddPumpTool,
                              mAddOrificeTool, mAddWeirTool, mAddOutletTool };
    for (auto *t : linkTools)
        if (t) t->setTerrain(mActiveTerrain, mTerrainLinkOffset, mTerrainVertFactor);

    // Vertical-unit change also affects the conversion factor profile
    // dialogs need — re-fire activeTerrainChanged so they re-sample.
    emit activeTerrainChanged(mActiveTerrain);
    setHasChanges(true);
}

void SWMMVisProjectWindow::restoreTerrainState(const QString &absoluteLayerPath,
                                                double nodeOffset,
                                                double linkOffset,
                                                const QString &vertUnit)
{
    mTerrainNodeOffset = nodeOffset;
    mTerrainLinkOffset = linkOffset;

    // Match the saved path against currently loaded raster layers.
    GISRasterLayer *found = nullptr;
    if (!absoluteLayerPath.isEmpty() && mCanvas) {
        for (OpenSWMMVisLayer *l : mCanvas->layers()) {
            auto *raster = qobject_cast<GISRasterLayer *>(l);
            if (raster && raster->filePath() == absoluteLayerPath) {
                found = raster;
                break;
            }
        }
    }

    setActiveTerrain(found);

    // Restore or auto-detect vertical unit.
    const QString unit = vertUnit.isEmpty()
                             ? (found ? found->detectVerticalUnit() : QStringLiteral("m"))
                             : vertUnit;
    setTerrainVerticalUnit(unit);
}
