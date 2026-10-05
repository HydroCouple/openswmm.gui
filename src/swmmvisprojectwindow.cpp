#include "map/mapundostack.h"
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <QInputDialog>
#include <QMessageBox>
#include <openswmm/engine/openswmm_infrastructure.h>
#include "layers/traceanalysislayer.h"
/*!
 * \file   swmmvisprojectwindow.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "swmmvisprojectwindow.h"
#include "project/profilesectionstore.h"
#include "project/groundwaterrecipestore.h"

#include "swmmvis.h"   // closeEvent's Save As hand-off (saveProjectWindowAs)
#include "map/mapcanvas.h"
#include "layers/gisrasterlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmresultslayer.h"        // Slice QA.2 — registry hookup
#include "layers/swmm2dresultslayer.h"      // active 2D analysis layer
#include "mesh/meshenginesync.h"            // push mesh-layer edits into the engine before save
#include "mesh/inpmeshreader.h"             // parse a browsed-for .2dm on import
#include "mesh/inpmeshwriter.h"             // convert an SMS 2DM import to section format
#include "mesh/meshcellgeom.h"
#include "mesh/sms2dmreader.h"              // SMS / Aquaveo 2DM cards (G5)
#include "mesh/meshcellgeom.h"              // edgeSlotCount for the BC SoA size check
#include "mesh/inpmeshwriter.h"             // retarget [2D_MESH_FILE] after save
#include "output/outputstatsregistry.h"     // Slice QA.2 — owns the registry
#include "project/openswmmvisworkspace.h"
#include "project/projectserializer.h"      // Slice RB.1 — sidecar auto-create
#include "project/projectsaveoutputs.h"
#include "project/generatedmeshartifacts.h"
#include "project/projectsavevalidation.h"
#include "map/tools/maptoolpan.h"
#include "map/tools/maptoolzoom.h"
#include "map/tools/maptoolselect.h"
#include "map/tools/maptoolselectpolygon.h"
#include "map/tools/maptoolmeasure.h"
#include "map/tools/maptoolselectprofile.h"
#include "map/tools/maptooladdnode.h"
#include "map/tools/maptooladdvirtualnode.h"
#include "map/tools/maptooladdinletnode.h"
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

#include "core/loadprogress.h"
#include "core/openswmmvislogmessage.h"
#include "core/preferencesmanager.h"
#include "core/unitsystem.h"
#include "map/openswmmvisscene.h"
#include "plugins/filefilterregistry.h"
#include "selection/gisselectionbridge.h"
#include "selection/selectionmanager.h"
#include "mesh/meshobjectref.h"             // MeshCell ref parsing for cell highlight

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QLoggingCategory>
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
#include <QScopeGuard>
#include <QSaveFile>
#include <QTextStream>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <filesystem>
#include <atomic>
#include <memory>
#include <exception>
#include <stdexcept>

#include "core/measurementunitmanager.h"

#include <openswmm/engine/openswmm_2d.h>
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
    connect(ProfileSectionStore::forOwner(this), &ProfileSectionStore::edited,
            this, [this] { setHasChanges(true); });
    connect(GroundwaterRecipeStore::forOwner(this), &GroundwaterRecipeStore::edited,
            this, [this] { setHasChanges(true); });
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

    // SVBC round B — GIS feature layers ↔ selection bus, both directions.
    // A small owned QObject rather than inline lambdas (the SWMM bridge
    // below) so it is testable with a bare canvas + manager fixture. It
    // subscribes to layerAdded/layerRemoved itself, covering layers loaded
    // at any point in the window's life.
    new GisSelectionBridge(mSelectionManager, mCanvas, this);

    // Slice QA.2 — keep the stats registry in lockstep with the
    // canvas's SWMMResultsLayer set. layerAdded fires after the layer
    // is in the canvas's layer list; layerRemoved fires before removal
    // completes. The registry's register/unregister are idempotent so
    // either ordering is safe.
    connect(mCanvas, &MapCanvas::layerAdded, this,
            [this](OpenSWMMVisLayer *layer) {
        if (auto *rl = qobject_cast<SWMMResultsLayer *>(layer)) {
            mStatsRegistry->registerLayer(rl, rl->resultsFilePath());
            if (!rl->property("traceStoredRunId").toString().isEmpty()) return;
            auto observe = [this, rl] {
                mStatsRegistry->contentOpened(rl, rl->resultsFilePath(), rl->isLive());
            };
            connect(rl, &SWMMResultsLayer::resultsOpened, this, observe);
            connect(rl, &SWMMResultsLayer::resultsFinalized, this, observe);
            connect(rl, &SWMMResultsLayer::resultsFilePathChanged, this, observe);
            observe();
        }
    });
    connect(mCanvas, &MapCanvas::layerAdded, this, [this](OpenSWMMVisLayer *layer) {
        if (auto *trace = qobject_cast<openswmmvis::trace::TraceAnalysisLayer*>(layer)) {
            connect(trace->traceStyle(), &OpenSWMM::Render::SublayerStyle::styleChanged,
                    this, [this] { setHasChanges(true); });
            for (auto *part : trace->sublayers())
                connect(part, &OpenSWMM::Render::ISublayer::invalidated,
                        this, [this] { setHasChanges(true); });
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
    // Undo host for mediated edits made outside a map tool (the property
    // panel's inlet-usage rows), so they land on the same stack as every
    // map edit rather than mutating the engine unrecoverably.
    mModelLayer->setEditCanvas(mCanvas);

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
        const QString kinds[6] = {
            QStringLiteral("junction"),
            QStringLiteral("outfall"),
            QStringLiteral("storage"),
            QStringLiteral("divider"),
            QStringLiteral("virtual_junction"),
            QStringLiteral("inlet_junction"),
        };
        SWMMElementSymbol syms[6] = {
            mModelLayer->junctionSymbol(),
            mModelLayer->outfallSymbol(),
            mModelLayer->storageSymbol(),
            mModelLayer->dividerSymbol(),
            mModelLayer->virtualJunctionSymbol(),
            mModelLayer->inletJunctionSymbol(),
        };
        for (int i = 0; i < 6; ++i) {
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
        mModelLayer->setVirtualJunctionSymbol(syms[4]);
        mModelLayer->setInletJunctionSymbol(syms[5]);
        // The dashed connector overlay follows the inlet-junction colour so
        // the relation and its node read as one thing (§3.2).
        auto connector = mModelLayer->inletConnectorSymbol();
        connector.fillColor    = syms[5].fillColor;
        connector.outlineColor = syms[5].fillColor;
        mModelLayer->setInletConnectorSymbol(connector);
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
                    mCanvas->zoomToFullExtent(/*pushUndo=*/false);
                    return;
                }
                mCanvas->setCanvasSRS(
                    new SpatialReferenceSystem(*layerSrs, mCanvas), true);
                // setCanvasSRS already fans out onCanvasCRSChanged to all
                // layers + emits canvasSRSChanged + refreshes the buffer;
                // refit so the user sees content at the new coordinates.
                // Part of the CRS change, not an undo step of its own.
                mCanvas->zoomToFullExtent(/*pushUndo=*/false);
            });

    mCanvas->addLayer(mModelLayer, false);

    connect(mModelLayer, &SWMMModelLayer::modelLoaded,    this, &SWMMVisProjectWindow::modelLoaded);
    connect(mModelLayer, &SWMMModelLayer::modelLoadError, this, &SWMMVisProjectWindow::modelLoadError);

    // Dirty tracking for engine-state edits. SWMMVis::onRunSimulation gates
    // its pre-run auto-save on hasChanges(), so anything that mutates the
    // engine without setting this flag makes the run execute the stale .inp
    // on disk. Before this block the flag only moved for a handful of dialogs
    // (flow units, simulation options, climatology, user flags, CRS, offsets,
    // mesh), which left every map and editor edit invisible to the gate.
    //
    // modelEdited() is the dedicated dirty channel: everything that writes the
    // engine outside the layer's own object-lifecycle signals routes through
    // SWMMModelLayer::markEdited(). That covers node / vertex / polygon moves,
    // curve, time-series and pattern content edits, property-browser and
    // attribute-table value edits, and the comprehensive editors' registry
    // flushes. The lifecycle signals below stay wired because they carry adds,
    // deletes, renames, rule and OPTIONS writes that predate that channel.
    //
    // View-only signals (selection, render kinds, category order) are
    // deliberately excluded so panning and selecting never force a re-save of
    // a large model. Anything emitted while the engine is being adopted is
    // cleared by finishModelLoad(), which resets the flag after the load.
    connect(mModelLayer, &SWMMModelLayer::modelEdited, this,
            [this]() { setHasChanges(true); });
    connect(mModelLayer, &SWMMModelLayer::geometryChanged, this,
            [this]() { setHasChanges(true); });
    connect(mModelLayer, &SWMMModelLayer::attributeChanged, this,
            [this](const QString &) { setHasChanges(true); });
    connect(mModelLayer, &SWMMModelLayer::optionsChanged, this,
            [this](const QStringList &) { setHasChanges(true); });
    connect(mModelLayer, &SWMMModelLayer::dataObjectsChanged, this,
            [this]() { setHasChanges(true); });
    connect(mModelLayer, &SWMMModelLayer::hydrographChanged, this,
            [this](const QString &) { setHasChanges(true); });
    connect(mModelLayer, &SWMMModelLayer::controlRulesChanged, this,
            [this](const QString &) { setHasChanges(true); });
    connect(mModelLayer, &SWMMModelLayer::transectChanged, this,
            [this](const QString &) { setHasChanges(true); });

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
    mAddVirtualJunctionTool = new OpenSWMMVisMapToolAddVirtualNode(mCanvas, this);
    mAddInletJunctionTool   = new OpenSWMMVisMapToolAddInletNode(mCanvas, this);
    mAddOutfallTool   = new OpenSWMMVisMapToolAddNode(mCanvas, 1, QStringLiteral("outfall"),      this);
    mAddStorageTool   = new OpenSWMMVisMapToolAddNode(mCanvas, 2, QStringLiteral("storage"),      this);
    mAddLidTool = new OpenSWMMVisMapToolAddNode(mCanvas, 2, QStringLiteral("storage"), this);
    connect(mAddLidTool, &OpenSWMMVisMapToolAddNode::nodeAdded, this,
            [this](const QString& name, int, double, double) {
        if (!mModelLayer || !mModelLayer->engine() || !mCanvas->undoStack()) return;
        const auto eng = mModelLayer->engine();
        const int node = swmm_node_index(eng, name.toUtf8().constData());
        int control = -1; double saturation = 0.0;
        if (swmm_node_get_lid(eng, node, &control, &saturation) != SWMM_OK || control < 0) return;
        QStringList destinations{tr("Do not add")};
        for (int i = 0; i < swmm_node_count(eng); ++i) {
            int otherControl = -1; double sat = 0;
            swmm_node_get_lid(eng, i, &otherControl, &sat);
            if (i != node && otherControl < 0) destinations.append(QString::fromUtf8(swmm_node_id(eng, i)));
        }
        if (destinations.size() < 2) return;
        int numbered = 0; SWMM_LidNodeLayer first{};
        swmm_lid_node_layer_get(eng, control, 0, &first);
        for (int i = 0; i < swmm_lid_node_layer_count(eng, control); ++i) {
            SWMM_LidNodeLayer row{};
            swmm_lid_node_layer_get(eng, control, i, &row);
            numbered += row.kind != 3;
        }
        for (int kind : {2, 3}) {
            bool ok = false;
            const auto target = QInputDialog::getItem(this, tr("LID outlet"),
                kind == 2 ? tr("Underdrain orifice destination") : tr("Overflow weir destination"), destinations, 0, false, &ok);
            if (!ok || target == destinations.first()) continue;
            const QString prefix = name + (kind == 2 ? QStringLiteral("_underdrain") : QStringLiteral("_overflow"));
            QString id = prefix;
            for (int suffix = 2; swmm_link_index(eng, id.toUtf8().constData()) >= 0; ++suffix) id = prefix + QString::number(suffix);
            auto* command = new AddLinkCommand(mModelLayer, id, kind, name, target, {}, mCanvas);
            command->setLidAnchor(kind == 2 ? numbered : 1, kind == 3 && first.kind != 0);
            mCanvas->undoStack()->push(command);
        }
    });
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
                                  mAddStorageTool, mAddLidTool, mAddDividerTool })
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

void SWMMVisProjectWindow::convertLinkOffsets(bool toElevation, bool convertValues)
{
    if (!mModelLayer)
        return;
    mModelLayer->convertLinkOffsets(toElevation, convertValues);
    setHasChanges(true);
}

namespace {
// Joins the openswmm.load.* family, so QT_LOGGING_RULES="openswmm.load.*=true"
// turns these on. Gated because the [postZoom] sample walks EVERY scene item
// (167k on a large model) purely to report a count.
Q_LOGGING_CATEGORY(lcLoadWindow, "openswmm.load.window")

// Do the engine's 2D mesh and this layer's agree on size? Used by the save
// path to confirm that a layer flagged "no unsaved mesh edits" really is still
// in step with the engine before skipping the (very expensive) re-push. Counts
// are the same discriminator pushMeshEditsToEngine itself uses to decide
// whether an index-wise push is meaningful, and both calls are O(1).
bool engineMeshMatches(SWMM_Engine engine, const mesh::MeshResult &m)
{
    if (!engine) return false;
    int nv = 0, nt = 0;
    if (swmm_2d_vertex_count(engine, &nv) != 0) return false;
    if (swmm_2d_triangle_count(engine, &nt) != 0) return false;
    return nv == m.vertices.size() && nt == m.triangles.size();
}
}  // namespace

bool SWMMVisProjectWindow::loadModel(QList<QString> &warnings, QList<QString> &errors)
{
    if (mAsyncLoadInProgress || mClosing || !mModelLayer) {
        errors.append(tr("Cannot replace the model while its project is opening or closing."));
        return false;
    }
    QString recoveryError, recoveryNotice;
    if (!ProjectSaveOutputs::recover(mModelLayer->modelFilePath(), &recoveryError, &recoveryNotice)) {
        errors.append(recoveryError);
        return false;
    }
    if (!recoveryNotice.isEmpty()) warnings.append(recoveryNotice);
    if (!mModelLayer->loadModel(warnings, errors))
        return false;
    return finishModelLoad(warnings, errors);
}

void SWMMVisProjectWindow::loadModelAsync(OpenProgressModel *progress)
{
    const auto refuse = [this](const QString &message) {
        QTimer::singleShot(0, this, [this, message] {
            emit modelLoadFinished(false, {}, {message});
        });
    };
    if (mAsyncLoadInProgress) {
        refuse(tr("This project is already opening a model."));
        return;
    }
    if (mClosing || !mModelLayer || mModelLayer->engine()) {
        refuse(tr("Open requires an available project with no model already loaded."));
        return;
    }

    // The worker owns only its engine and plain result data. It never reads or
    // fills a QObject-owned layer, even while the layer is hidden.
    struct AsyncOpenOutcome {
        SWMM_Engine engine = nullptr;
        QString errorDetail, recoveryNotice;
        qint64 openMs = 0;
        ~AsyncOpenOutcome() {
            if (engine) { swmm_engine_close(engine); swmm_engine_destroy(engine); }
        }
    };
    auto outcome = std::make_shared<AsyncOpenOutcome>();
    auto invalidated = std::make_shared<std::atomic_bool>(false);
    const QString path = mModelLayer->modelFilePath();
    const quint64 revision = mModelLayer->editRevision();
    const QPointer<SWMMModelLayer> layer(mModelLayer);
    const QPointer<SWMMVisProjectWindow> self(this);
    const QPointer<OpenProgressModel> progressGuard(progress);
    const bool wasVisible = layer->isVisible();
    const auto workerHook = mAsyncLoadWorkerTestHook;
    mAsyncLoadInProgress = true;
    layer->setVisible(false);
    auto *watcher = new QFutureWatcher<void>(qApp);
    const auto invalidate = [invalidated] { invalidated->store(true); };
    connect(this, &SWMMVisProjectWindow::aboutToClose, watcher, invalidate);
    connect(this, &QObject::destroyed, watcher, invalidate);
    connect(layer, &SWMMModelLayer::modelFilePathChanged, watcher, invalidate);
    connect(layer, &SWMMModelLayer::engineAboutToClose, watcher, invalidate);
    connect(layer, &SWMMModelLayer::geometryChanged, watcher, invalidate);
    connect(layer, &QObject::destroyed, watcher, invalidate);
    connect(layer, &QObject::destroyed, this, [this, original = layer.data()] {
        if (mModelLayer == original) mModelLayer = nullptr;
    });
    connect(watcher, &QFutureWatcherBase::progressValueChanged, watcher,
            [self, layer, progressGuard, invalidated](int packed) {
        if (!self || self->isClosing() || !layer || !progressGuard
            || invalidated->load() || packed < 0) return;
        const OpenStage stage = unpackLoadProgressStage(packed);
        progressGuard->setStage(stage, unpackLoadProgressPct(packed),
                                OpenProgressModel::stageLabel(stage));
    });
    connect(watcher, &QFutureWatcherBase::finished, watcher,
            [watcher, self, layer, path, revision, wasVisible,
             outcome, invalidated, progressGuard] {
        auto cleanup = qScopeGuard([watcher, self] {
            watcher->deleteLater();
            if (self) self->mAsyncLoadInProgress = false;
        });
        try { watcher->future().waitForFinished(); }
        catch (const std::exception &ex) {
            outcome->errorDetail = QObject::tr("Model open failed: %1").arg(QString::fromUtf8(ex.what()));
        } catch (...) {
            outcome->errorDetail = QObject::tr("Model open failed with an unexpected worker exception.");
        }
        if (!self || self->isClosing()) return;
        QList<QString> warnings, errors;
        if (!outcome->recoveryNotice.isEmpty()) warnings.append(outcome->recoveryNotice);
        const auto contextCurrent = [self, layer, path, revision, invalidated] {
            return self && !self->isClosing() && layer && self->mModelLayer == layer
                && !invalidated->load() && layer->modelFilePath() == path
                && layer->editRevision() == revision && !layer->engine();
        };
        bool ok = false;
        if (!contextCurrent()) {
            errors.append(QObject::tr("Model open discarded because its project or model changed."));
        } else if (!outcome->errorDetail.isEmpty() || !outcome->engine) {
            errors.append(outcome->errorDetail.isEmpty()
                ? QObject::tr("Model open did not produce a usable engine.") : outcome->errorDetail);
        } else {
            SWMM_Engine opened = outcome->engine;
            try {
                // This bounded safety correction deliberately performs SoA/cache
                // preparation on the GUI thread. A detached data-transfer API is
                // needed to move it back to a worker safely.
                qint64 soaMs = 0, geomMs = 0;
                if (progressGuard) progressGuard->setStage(OpenStage::SoaCopy, 0,
                    OpenProgressModel::stageLabel(OpenStage::SoaCopy));
                if (!contextCurrent()) throw std::runtime_error("The model changed during open.");
                layer->closeEngine(); // discard any prior failed, unadopted arrays
                layer->buildFromEngine(opened, &soaMs, &geomMs);
                if (progressGuard) {
                    progressGuard->finishStage(OpenStage::SoaCopy);
                    if (progressGuard) progressGuard->finishStage(OpenStage::GeomCache);
                }
                if (!contextCurrent()) throw std::runtime_error("The model changed during open.");
                // If an adoption callback closes/deletes the layer, ownership
                // must leave the result before that callback frees the engine.
                connect(layer, &SWMMModelLayer::engineAboutToClose, watcher,
                        [layer, outcome] {
                    if (layer && layer->engine() == outcome->engine) outcome->engine = nullptr;
                });
                ok = layer->adoptOpenEngine(opened, warnings, errors, outcome->openMs, soaMs, geomMs);
                if (layer && layer->engine() == opened) outcome->engine = nullptr;
                if (ok && self && layer && !self->isClosing()
                    && self->mModelLayer == layer && layer->engine() == opened
                    && layer->modelFilePath() == path && layer->editRevision() == revision)
                    ok = self->finishModelLoad(warnings, errors);
                else ok = false;
            } catch (const std::exception &ex) {
                ok = false;
                errors.append(QObject::tr("Model adoption failed: %1").arg(QString::fromUtf8(ex.what())));
            } catch (...) {
                ok = false;
                errors.append(QObject::tr("Model adoption failed with an unexpected exception."));
            }
            // Adoption may have taken ownership before a later GUI exception.
            if (layer && layer->engine() == opened) outcome->engine = nullptr;
            if (!ok && layer && !layer->engine()) layer->closeEngine();
        }
        if (!self || self->isClosing()) return;
        if (!ok && layer && !layer->engine()) layer->setVisible(wasVisible);
        if (!ok && errors.isEmpty()) errors.append(QObject::tr("Model open was cancelled."));
        // Allow a failure handler to repair/retry immediately.
        cleanup.dismiss();
        watcher->deleteLater();
        self->mAsyncLoadInProgress = false;
        emit self->modelLoadFinished(ok, warnings, errors);
    });
    watcher->setFuture(QtConcurrent::run([path, outcome, invalidated, workerHook](QPromise<void> &promise) {
        promise.setProgressRange(0, packLoadProgress(OpenStage::EngineParse, 100));
        try {
            if (invalidated->load()) return;
            promise.setProgressValue(packLoadProgress(OpenStage::EngineParse, 0));
            if (!ProjectSaveOutputs::recover(path, &outcome->errorDetail, &outcome->recoveryNotice)) return;
            if (invalidated->load()) return;
            outcome->engine = SWMMModelLayer::openEngineForPath(path, &outcome->errorDetail, &outcome->openMs);
            if (workerHook) workerHook();
            promise.setProgressValue(packLoadProgress(OpenStage::EngineParse, 100));
        } catch (const std::exception &ex) {
            outcome->errorDetail = QObject::tr("Model open failed: %1").arg(QString::fromUtf8(ex.what()));
        } catch (...) {
            outcome->errorDetail = QObject::tr("Model open failed with an unexpected exception.");
        }
    }));
}


bool SWMMVisProjectWindow::finishModelLoad(QList<QString> &warnings, QList<QString> &errors)
{
    Q_UNUSED(warnings);
    const QPointer<SWMMVisProjectWindow> owner(this);
    const QPointer<SWMMModelLayer> model(mModelLayer);
    if (!model || mClosing) return false;
    const SWMM_Engine loadedEngine = model->engine();
    const QString loadedPath = model->modelFilePath();
    const quint64 loadedRevision = model->editRevision();
    const auto contextCurrent = [owner, model, loadedEngine, loadedPath, loadedRevision] {
        return owner && !owner->isClosing() && model && owner->modelLayer() == model
            && model->engine() == loadedEngine && model->modelFilePath() == loadedPath
            && model->editRevision() == loadedRevision;
    };
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
                    QPointer<CRSSelectionDialog> dlg = new CRSSelectionDialog(this);
                    const auto releaseDialog = qScopeGuard([dlg] { if (dlg) dlg->deleteLater(); });
                    dlg->setWindowTitle(tr("Coordinate Reference System"));
                    const int result = dlg->exec();
                    if (!contextCurrent() || !dlg) {
                        errors.append(QObject::tr("Project open cancelled because its model or owner changed."));
                        return false;
                    }
                    if (result == QDialog::Accepted)
                    {
                        if (SpatialReferenceSystem *picked = dlg->selectedSRS())
                        {
                            mModelLayer->setSRS(picked, true);
                            modelSRS = picked;
                            break;
                        }
                        // Accepted with no selection — treat as cancel.
                    }

                    QPointer<QMessageBox> mb = new QMessageBox(this);
                    const auto releaseMessage = qScopeGuard([mb] { if (mb) mb->deleteLater(); });
                    mb->setIcon(QMessageBox::Warning);
                    mb->setWindowTitle(tr("CRS Required"));
                    mb->setText(tr("A coordinate reference system is required to open this SWMM model."));
                    mb->setInformativeText(tr("Choose a CRS to continue, or abort opening the project."));
                    QPushButton *chooseBtn = mb->addButton(tr("Choose CRS…"), QMessageBox::AcceptRole);
                    // Local-projected shortcut: matches the model's flow-unit
                    // system (ft vs. m) so 2D mesh generation has a usable
                    // linear unit without forcing the user through the picker.
                    const QString lenLabel = (mUnits && mUnits->isSI())
                                                 ? QStringLiteral("m")
                                                 : QStringLiteral("ft");
                    QPushButton *localBtn  = mb->addButton(
                        tr("Use local projected (%1)").arg(lenLabel),
                        QMessageBox::AcceptRole);
                    QPushButton *abortBtn  = mb->addButton(tr("Abort Open"),  QMessageBox::RejectRole);
                    mb->setDefaultButton(chooseBtn);
                    mb->exec();
                    if (!contextCurrent() || !mb) {
                        errors.append(QObject::tr("Project open cancelled because its model or owner changed."));
                        return false;
                    }
                    if (mb->clickedButton() == abortBtn)
                    {
                        errors.append(tr("Project open cancelled: no CRS selected."));
                        mModelLayer->setVisible(false);
                        return false;
                    }
                    if (mb->clickedButton() == localBtn)
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
        qCDebug(lcLoadWindow).noquote() << diag;
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
        // Zoom NOW when the canvas already has a size, and fall back to the
        // retry timer only when it does not. The timer used to be started
        // unconditionally, so every open — including a 16 ms model — sat on a
        // blank canvas for at least 50 ms before the network appeared, purely
        // waiting for a check that would have passed immediately.
        auto zoomNow = [this]() {
            mCanvas->zoomToFullExtent();
            // Force scene repopulation immediately rather than waiting for the
            // 50 ms Scene-channel debounce of the new invalidate() API — the
            // diagnostic below samples scene items 50 ms later so we'd race.
            // The legacy refreshLayerItems() entry is preserved precisely for
            // callers that need synchronous behavior; everything else should
            // go through MapCanvas::invalidate().
            mCanvas->refreshLayerItems();

            // Diagnostic only. Skipped entirely unless the category is on:
            // items().count() walks every item in the scene — 167,000 of them
            // on West Whiteland — and this used to run on every open, 50 ms
            // after the model was already on screen, to report a number.
            if (!lcLoadWindow().isDebugEnabled())
                return;
            QTimer::singleShot(50, mCanvas, [this, mw = window()]() {
                const MapExtent canvasExt = mCanvas->extent();
                const int sceneItems = mCanvas->mapScene() ? mCanvas->mapScene()->items().count() : -1;
                const QString d2 = QStringLiteral(
                    "[postZoom] canvas %1x%2 | extent [%3,%4 -> %5,%6] | scene items=%7")
                    .arg(mCanvas->width()).arg(mCanvas->height())
                    .arg(canvasExt.xMin(), 0, 'g', 6).arg(canvasExt.yMin(), 0, 'g', 6)
                    .arg(canvasExt.xMax(), 0, 'g', 6).arg(canvasExt.yMax(), 0, 'g', 6)
                    .arg(sceneItems);
                qCDebug(lcLoadWindow).noquote() << d2;
                if (mw)
                    QMetaObject::invokeMethod(mw, "onLogMessage", Qt::QueuedConnection,
                                              Q_ARG(QString, d2),
                                              Q_ARG(OpenSWMMVisLogMessage::LogMessageType,
                                                    OpenSWMMVisLogMessage::LogMessageType::Information));
            });
        };

        if (mCanvas->width() > 0 && mCanvas->height() > 0) {
            zoomNow();
        } else {
            // Canvas not sized yet (the MDI subwindow has not finished its
            // show + resize cycle). Retry, capped at ~1 s as before.
            auto *attempt = new QTimer(mCanvas);
            attempt->setSingleShot(true);
            attempt->setInterval(50);
            auto *attemptCount = new int(0);
            QObject::connect(attempt, &QTimer::timeout, mCanvas,
                             [this, attempt, attemptCount, zoomNow]() {
                if (mCanvas->width() <= 0 || mCanvas->height() <= 0) {
                    if (++*attemptCount < 20) {           // ≤ 1 s total
                        attempt->start();
                        return;
                    }
                    delete attemptCount;                  // never sized — give up
                    attempt->deleteLater();
                    return;
                }
                delete attemptCount;
                attempt->deleteLater();
                zoomNow();
            });
            attempt->start();
        }
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

void SWMMVisProjectWindow::attachMeshLayer(SWMM2DMeshLayer *meshLayer, bool pristine)
{
    if (!meshLayer)
        return;
    // MapCanvas does not own its layers; undo/deferred geometry can outlive
    // the window. Abandon this project's pending disk artifacts on teardown
    // even if an orphaned layer remains alive.
    connect(this, &QObject::destroyed, meshLayer, [meshLayer] {
        meshLayer->setGeneratedArtifacts({});
    });
    // A layer parsed from the very file the engine opened already agrees with
    // the engine's in-memory mesh, so the save path can skip re-pushing it.
    // Every other origin (mesh generated or imported in-session) leaves the
    // engine holding the OLD mesh and must stay dirty — hence the default.
    if (pristine)
        meshLayer->setMeshEditsSaved();
    connect(meshLayer, &SWMM2DMeshLayer::activeMeshChanged, this,
            [this](bool) { setHasChanges(true); });
    // Per-element edits (vertex Z, edge BC/conveyance, cell Manning's n /
    // initial depth / tag) report through attributeChanged; bulk coupling
    // rewrites report through meshEditsChanged. Both are project data that
    // must reach the .inp, so both dirty the project — and both mean the
    // layer's mesh has diverged from the engine's, so both arm the re-push.
    connect(meshLayer, &SWMM2DMeshLayer::attributeChanged, this,
            [this, meshLayer](const QString &) {
                meshLayer->markMeshEdited();
                setHasChanges(true);
            });
    connect(meshLayer, &SWMM2DMeshLayer::meshEditsChanged, this,
            [this, meshLayer]() {
                meshLayer->markMeshEdited();
                setHasChanges(true);
            });
}

void SWMMVisProjectWindow::importMeshFileAsync(const QString &srcPath)
{
    const quint64 serial = ++mMeshImportSerial;
    auto fail = [this](const QString &msg) {
        emit meshImportFinished(false, msg, QString());
    };
    if (mClosing || !canvas() || !mModelLayer || !mModelLayer->engine()) {
        fail(tr("Open a project first — a 2D mesh attaches to a model."));
        return;
    }
    const QFileInfo srcFi(srcPath);
    if (!srcFi.exists() || !srcFi.isFile()) {
        fail(tr("Mesh file not found: %1").arg(srcPath));
        return;
    }

    const QString sourcePath = srcFi.absoluteFilePath();
    const QPointer<SWMMModelLayer> ownerModel(mModelLayer);
    const QPointer<MapCanvas> ownerCanvas(canvas());
    const auto ownerEngine = mModelLayer->engine();
    const QString modelPath = mModelLayer->modelFilePath();
    const quint64 revision = mModelLayer->editRevision();
    const bool needsSaveAsRebase = mUntitled || modelPath.isEmpty();
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    auto *operation = new QObject(this);
    const QPointer<QObject> operationGuard(operation);
    bool launched = false;
    const auto cleanup = qScopeGuard([&] {
        if (!launched && operationGuard) delete operationGuard.data();
    });
    connect(operation, &QObject::destroyed, [cancelled] { cancelled->store(true); });
    const auto invalidate = [cancelled] { cancelled->store(true); };
    QList<QMetaObject::Connection> guards;
    guards << connect(this, &SWMMVisProjectWindow::aboutToClose, operation, invalidate)
           << connect(ownerModel.data(), &SWMMModelLayer::modelLoaded, operation, invalidate)
           << connect(ownerModel.data(), &SWMMModelLayer::modelEdited, operation, invalidate)
           << connect(ownerModel.data(), &SWMMModelLayer::attributeChanged, operation, invalidate)
           << connect(ownerModel.data(), &SWMMModelLayer::geometryChanged, operation, invalidate)
           << connect(ownerModel.data(), &SWMMModelLayer::optionsChanged, operation, invalidate)
           << connect(ownerModel.data(), &SWMMModelLayer::dataObjectsChanged, operation, invalidate)
           << connect(ownerModel.data(), &SWMMModelLayer::hydrographChanged, operation, invalidate)
           << connect(ownerModel.data(), &SWMMModelLayer::controlRulesChanged, operation, invalidate)
           << connect(ownerModel.data(), &SWMMModelLayer::transectChanged, operation, invalidate)
           << connect(ownerModel.data(), &SWMMModelLayer::modelFilePathChanged, operation, invalidate)
           << connect(ownerCanvas.data(), &MapCanvas::layerAdded, operation, invalidate)
           << connect(ownerCanvas.data(), &MapCanvas::layerRemoved, operation, invalidate);
    for (auto *layer : ownerCanvas->layers()) {
        guards << connect(layer, &OpenSWMMVisLayer::srsChanged, operation, invalidate)
               << connect(layer, &OpenSWMMVisLayer::extentChanged, operation, invalidate);
        if (auto *meshLayer = qobject_cast<SWMM2DMeshLayer *>(layer))
            guards << connect(meshLayer, &SWMM2DMeshLayer::attributeChanged, operation, invalidate)
                   << connect(meshLayer, &SWMM2DMeshLayer::meshEditsChanged, operation, invalidate)
                   << connect(meshLayer, &SWMM2DMeshLayer::activeMeshChanged, operation, invalidate);
    }

    // Choose a logical destination only. Overwrite consent applies at Save;
    // neither a bad parse nor discarding the project may change that file.
    const QDir projectDir = QFileInfo(modelPath.isEmpty() ? sourcePath : modelPath).absoluteDir();
    QString meshPath = projectDir.absoluteFilePath(srcFi.fileName());
    bool requireAbsentDestination = meshPath != sourcePath;
    bool overwriteApproved = false;
    ProjectSaveOutputs::DestinationState promptDestination;
    const bool smsSource = mesh::Sms2dmReader::looksLikeSms2dm(sourcePath);
    if (smsSource && meshPath == sourcePath) {
        requireAbsentDestination = true;
        const QString base = srcFi.completeBaseName() + QStringLiteral("_imported");
        meshPath = projectDir.filePath(base + QStringLiteral(".2dm"));
        for (int n = 1; QFileInfo::exists(meshPath) || QFileInfo(meshPath).isSymLink(); ++n)
            meshPath = projectDir.filePath(QStringLiteral("%1_%2.2dm").arg(base).arg(n));
    } else if (meshPath != sourcePath
               && (QFileInfo::exists(meshPath) || QFileInfo(meshPath).isSymLink())) {
        QString captureError;
        if (!ProjectSaveOutputs::captureDestination(meshPath, &promptDestination, &captureError)) {
            fail(captureError);
            return;
        }
        const QPointer<SWMMVisProjectWindow> self(this);
        auto *box = new QMessageBox(QMessageBox::Question, tr("Import 2D Mesh"),
            tr("The project folder already contains a file named %1.").arg(srcFi.fileName()),
            QMessageBox::NoButton, this);
        const QPointer<QMessageBox> boxGuard(box);
        box->setInformativeText(tr("Replace it when the project is saved, or keep both?"));
        QPushButton *overwrite = box->addButton(tr("Overwrite"), QMessageBox::DestructiveRole);
        QPushButton *keepBoth = box->addButton(tr("Keep Both"), QMessageBox::AcceptRole);
        box->addButton(QMessageBox::Cancel);
        box->setDefaultButton(keepBoth);
        box->exec();
        if (!self || !boxGuard) return;
        QAbstractButton *clicked = box->clickedButton();
        box->deleteLater();
        if (clicked == keepBoth) {
            const QString base = srcFi.completeBaseName();
            const QString ext = srcFi.suffix();
            int n = 1;
            do {
                meshPath = projectDir.filePath(QStringLiteral("%1_%2.%3").arg(base).arg(n++).arg(ext));
            } while (QFileInfo::exists(meshPath) || QFileInfo(meshPath).isSymLink());
        } else if (clicked != overwrite) {
            fail(tr("2D mesh import cancelled."));
            return;
        } else {
            requireAbsentDestination = false;
            overwriteApproved = true;
        }
    }

    ProjectSaveOutputs::DestinationState approvedDestination;
    QString destinationError;
    if (!ProjectSaveOutputs::captureDestination(meshPath, &approvedDestination, &destinationError)) {
        fail(destinationError);
        return;
    }
    if (requireAbsentDestination && approvedDestination.fingerprint != QStringLiteral("absent")) {
        fail(tr("The import destination appeared while choosing a name: %1. Import again.").arg(meshPath));
        return;
    }
    if (overwriteApproved && (approvedDestination.resolvedPath != promptDestination.resolvedPath
                             || approvedDestination.fingerprint != promptDestination.fingerprint)) {
        fail(tr("The import destination changed while confirming replacement: %1. Import again.").arg(meshPath));
        return;
    }

    struct ImportOutcome {
        mesh::InpMeshReadResult read;
        std::shared_ptr<GeneratedMeshArtifacts> artifacts;
        QString errorMsg;
    };
    auto *watcher = new QFutureWatcher<ImportOutcome>(operation);
    connect(watcher, &QFutureWatcherBase::finished, operation,
            [this, watcher, operation, guards, cancelled, serial, ownerModel, ownerCanvas,
             ownerEngine, modelPath, revision, meshPath, needsSaveAsRebase]() {
        ImportOutcome out;
        try {
            auto future = watcher->future();
            out = future.takeResult();
        } catch (const std::exception &e) {
            out.errorMsg = tr("Reading the 2D mesh failed: %1").arg(QString::fromUtf8(e.what()));
        } catch (...) {
            out.errorMsg = tr("Reading the 2D mesh failed.");
        }
        for (const auto &connection : guards) disconnect(connection);
        operation->deleteLater();
        const bool current = !cancelled->load() && !mClosing && serial == mMeshImportSerial
            && ownerModel && ownerCanvas && mModelLayer == ownerModel && canvas() == ownerCanvas
            && ownerModel->engine() == ownerEngine && ownerModel->modelFilePath() == modelPath
            && ownerModel->editRevision() == revision;
        if (!current) {
            emit meshImportFinished(false,
                tr("2D mesh import discarded because the project changed, closed, or a newer import started."),
                QString());
            return;
        }
        if (!out.read.hasMesh || !out.artifacts) {
            emit meshImportFinished(false, out.errorMsg, QString());
            return;
        }

        // No QObject crosses the worker boundary. The future owns only data
        // and private files, so an abandoned result releases its whole job.
        std::unique_ptr<SWMM2DMeshLayer> prepared;
        try {
            prepared = std::make_unique<SWMM2DMeshLayer>(std::move(out.read.mesh), meshPath,
                                                        nullptr, /*deferHeavyGeometry=*/true);
            prepared->setExternalMesh(true);
            prepared->setMeshUnitsSI(mesh::unitsHeaderIsSI(out.read.unitsHeader));
            prepared->setOwnsGeneratedTopology(true);
            prepared->setPreservesImportedSections(true);
            prepared->setImportNeedsSaveAsRebase(needsSaveAsRebase);
            prepared->setGeneratedArtifacts(std::move(out.artifacts));
            prepared->setActiveMesh(true);
            prepared->setName(QFileInfo(meshPath).fileName());
            if (out.read.edgeBCs.size() == mesh::edgeSlotCount(prepared->triangleCount()))
                prepared->edgeBCsMutable() = std::move(out.read.edgeBCs);
            if (ownerModel->srs())
                prepared->setSRS(new SpatialReferenceSystem(*ownerModel->srs(), prepared.get()), true);
        } catch (const std::exception &e) {
            emit meshImportFinished(false, tr("Preparing the imported mesh failed: %1")
                .arg(QString::fromUtf8(e.what())), QString());
            return;
        } catch (...) {
            emit meshImportFinished(false, tr("Preparing the imported mesh failed."), QString());
            return;
        }
        auto *meshLayer = prepared.get();

        QList<SWMM2DMeshLayer *> stale;
        for (OpenSWMMVisLayer *layer : canvas()->layers()) {
            auto *mesh = qobject_cast<SWMM2DMeshLayer *>(layer);
            if (!mesh) continue;
            mesh->setActiveMesh(false);
            if (!mesh->sourcePath().isEmpty()
                && QFileInfo(mesh->sourcePath()).absoluteFilePath() == meshPath)
                stale.append(mesh);
        }
        for (auto *mesh : stale) {
            const int index = canvas()->layers().indexOf(mesh);
            if (index >= 0)
                if (auto *taken = canvas()->takeLayer(index, false)) taken->deleteLater();
        }
        canvas()->addLayer(meshLayer, true);
        prepared.release();
        attachMeshLayer(meshLayer);
        meshLayer->finishSceneGeometryAsync();
        setHasChanges(true);

        QString message = tr("Imported 2D mesh %1: %2 vertices, %3 cells. Files will be published when the project is saved.")
            .arg(QFileInfo(meshPath).fileName()).arg(meshLayer->vertexCount()).arg(meshLayer->triangleCount());
        if (!out.read.warning.isEmpty()) message += QStringLiteral(" ") + out.read.warning;
        emit meshImportFinished(true, message, meshPath);
    });

    launched = true;
    watcher->setFuture(QtConcurrent::run([sourcePath, meshPath, modelPath, cancelled, approvedDestination]() -> ImportOutcome {
        ImportOutcome out;
        const auto isCancelled = [cancelled] { return cancelled->load(); };
        const auto error = [](const char *message) {
            return QCoreApplication::translate("SWMMVisProjectWindow", message);
        };
        if (isCancelled()) {
            out.errorMsg = error("2D mesh import cancelled.");
            return out;
        }
        // A pathless project has no writable project directory yet. Never
        // require write access beside the user's source dataset just to import.
        const QString stagingOwner = modelPath.isEmpty()
            ? QDir(QDir::tempPath()).filePath(QStringLiteral("openswmm-untitled.inp")) : modelPath;
        auto artifacts = GeneratedMeshArtifacts::create(stagingOwner, &out.errorMsg);
        if (!artifacts) return out;
        const QString snapshot = artifacts->reserve(meshPath, QStringLiteral("import.2dm"),
                                                     &out.errorMsg, ProjectSaveOutputs::Mesh);
        if (snapshot.isEmpty()) return out;
        const auto &reservedDestination = artifacts->entries().last().destination;
        if (reservedDestination.resolvedPath != approvedDestination.resolvedPath
            || reservedDestination.fingerprint != approvedDestination.fingerprint) {
            out.errorMsg = error("The import destination changed before preparation: %1. Import again.").arg(meshPath);
            return out;
        }
        if (!artifacts->copySource(sourcePath, snapshot, &out.errorMsg, isCancelled)) return out;
        const bool sms = mesh::Sms2dmReader::looksLikeSms2dm(snapshot);
        if (sms) {
            if (sourcePath == meshPath) {
                out.errorMsg = error("The source format changed during import. Import the SMS mesh again.");
                return out;
            }
            auto mesh = mesh::Sms2dmReader::read(snapshot, false);
            if (!mesh.ok) {
                out.errorMsg = mesh.errorMsg;
                return out;
            }
            out.read.warning = error("SMS 2DM mesh converted to SWMMVis format (%1 triangles, %2 quads).")
                .arg(mesh.triangles.size() - mesh.quadCount()).arg(mesh.quadCount());
            QSaveFile converted(snapshot);
            converted.setDirectWriteFallback(false);
            const QByteArray text = mesh::InpMeshWriter::buildSectionText(mesh, mesh::CouplingMap{}, 0.035).toUtf8();
            if (!converted.open(QIODevice::WriteOnly) || converted.write(text) != text.size()
                || !converted.flush() || !converted.commit()) {
                out.errorMsg = error("Could not prepare the converted mesh snapshot: %1").arg(converted.errorString());
                return out;
            }
            out.read.mesh = std::move(mesh);
            out.read.hasMesh = true;
            out.read.edgeBCs.resize(mesh::edgeSlotCount(out.read.mesh.triangles.size()));
        } else {
            // These known file directives cannot travel with a standalone
            // snapshot: relative paths would gain a different base directory.
            QFile input(snapshot);
            if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
                out.errorMsg = error("Could not read the mesh snapshot: %1").arg(input.errorString());
                return out;
            }
            QString section;
            QTextStream inputText(&input);
            while (!inputText.atEnd()) {
                const QString line = inputText.readLine().section(';', 0, 0).trimmed();
                if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
                    section = QLatin1Char('[') + line.mid(1, line.size() - 2).trimmed().toUpper() + QLatin1Char(']');
                const QString keyword = line.simplified().section(QLatin1Char(' '), 0, 0)
                    .remove(QLatin1Char('"')).remove(QLatin1Char('\'')).toUpper();
                const bool qualityFile = keyword == QStringLiteral("FILE")
                    && (section == QStringLiteral("[GW_INITIAL_QUALITY]")
                        || section == QStringLiteral("[INITIAL_QUALITY]")
                        || section == QStringLiteral("[2D_INITIAL_QUALITY]"));
                const bool outputFile = section == QStringLiteral("[2D_OPTIONS]")
                    && keyword == QStringLiteral("OUTPUT_FILE");
                if (section == QStringLiteral("[2D_MESH_FILE]") || qualityFile || outputFile) {
                    out.errorMsg = error("Cannot import %1 with a nested file reference. Import a standalone mesh with inline data and configure file dependencies in the project.")
                        .arg(section + (line.startsWith(QLatin1Char('[')) ? QString() : QStringLiteral(" ") + keyword));
                    return out;
                }
            }
            if (inputText.status() != QTextStream::Ok || input.error() != QFileDevice::NoError) {
                out.errorMsg = error("Could not read the mesh snapshot: %1").arg(input.errorString());
                return out;
            }
            input.close();
            out.read = mesh::InpMeshReader::read(snapshot);
        }
        if (!out.read.hasMesh) {
            out.errorMsg = out.read.errorMsg.isEmpty()
                ? error("%1 does not contain a SWMMVis 2D mesh.").arg(QFileInfo(sourcePath).fileName())
                : out.read.errorMsg;
            return out;
        }
        if (isCancelled()) {
            out.errorMsg = error("2D mesh import cancelled.");
            return out;
        }
        artifacts->protectInput(sourcePath, !sms && sourcePath == meshPath ? ProjectSaveOutputs::Mesh : -1);
        if (!artifacts->seal(&out.errorMsg)) return out;
        out.artifacts = std::move(artifacts);
        return out;
    }));
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

QString SWMMVisProjectWindow::modelFilePath() const
{
    // Untitled windows are backed by a temp .inp; surfacing that path would be
    // as confusing as surfacing its name, which updateWindowTitle() already
    // refuses to do.
    if (mUntitled || !mModelLayer)
        return {};
    return mModelLayer->modelFilePath();
}

void SWMMVisProjectWindow::markUntitled()
{
    mUntitled = true;
    updateWindowTitle();
}

bool SWMMVisProjectWindow::initializeBlankModel(
    const SWMMModelLayer::NewProjectSpec &spec,
    QList<QString> &warnings, QList<QString> &errors)
{
    if (!mModelLayer || mAsyncLoadInProgress || mClosing) {
        errors.append(tr("No available model layer to initialize, or the project is opening or closing."));
        return false;
    }
    if (!mModelLayer->adoptNewEngine(spec, warnings, errors))
        return false;
    return finishModelLoad(warnings, errors);
}

// ---------------------------------------------------------------------------
// Save / Save As
// ---------------------------------------------------------------------------

bool SWMMVisProjectWindow::save(QString *errorOut)
{
    if (!mModelLayer || mModelLayer->modelFilePath().isEmpty() || mUntitled)
    {
        // Untitled projects don't have a real path yet — fall through to
        // Save As. The main window routes pathless/untitled projects to
        // that dialog before calling save().
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

    if (errorOut) errorOut->clear();
    if (!corridorRecipeLoadError().isEmpty()) {
        if (errorOut) *errorOut = tr("The mesh corridor recipe could not be loaded: %1 "
                                    "Restore a supported recipe or explicitly regenerate its replacement before saving.")
                                    .arg(corridorRecipeLoadError());
        setHasChanges(true);
        return false;
    }
    mLastSaveWarnings.clear();
    ProjectSaveOutputs saveOutputs;
    // The GIS recipe remains after generated artifacts are released and after
    // reopening a project. Every Save must protect its original dependencies.
    for (const auto &source : corridorSources()) {
        saveOutputs.protect(source.path, -1);
        for (const auto &file : source.sourceFiles) saveOutputs.protect(file, -1);
    }
    bool modelWriteStarted = false;
    const auto failSave = [&](const QString &operation, const QString &path,
                              const QString &reason) {
        QString message = tr("Could not %1 at %2: %3.").arg(operation, path, reason);
        if (modelWriteStarted || saveOutputs.recoveryRequired())
            message += tr(" Model files may already have been updated.");
        message += tr(" The project remains unsaved. Keep it open, correct the problem, "
                      "and retry Save.");
        if (errorOut) *errorOut = message;
        qWarning().noquote() << message;
        setHasChanges(true);
        return false;
    };

    // Save-path perf breakdown — QT_LOGGING_RULES="openswmm.save.perf=true".
    // `stage` is restarted at every boundary; `total` runs for the whole call.
    // dmReads/dmWrites count full-file passes over the external .2dm sidecar,
    // so the write amplification is measured rather than asserted.
    QElapsedTimer total, stage;
    total.start();
    stage.start();
    qint64 meshSyncMs = 0, meshReadMs = 0, engineWriteMs = 0;
    qint64 meshPatchMs = 0, meshRefMs = 0, inlinePatchMs = 0,
           oswpMs = 0, validationMs = 0;
    int dmReads = 0, dmWrites = 0;

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

    // One model has one simulation mesh. Resolve ownership before any
    // engine mutation or file write; a sidecar stores display state only,
    // so it cannot preserve edits on a second, inactive mesh.
    QVector<SWMM2DMeshLayer *> meshLayers;
    SWMM2DMeshLayer *chosenMesh = nullptr;
    if (canvas()) {
        for (OpenSWMMVisLayer *l : canvas()->layers()) {
            auto *ml = qobject_cast<SWMM2DMeshLayer *>(l);
            if (!ml) continue;
            meshLayers.append(ml);
            if (ml->isActiveMesh()) {
                if (chosenMesh)
                    return failSave(tr("choose the active mesh"), newPath,
                                    tr("more than one mesh is active; select one mesh "
                                       "in the mesh editing toolbar and retry"));
                chosenMesh = ml;
            }
        }
    }
    // Older inline projects may not carry an active flag. A sole layer is
    // unambiguous; multiple unflagged layers must never depend on draw order.
    if (!chosenMesh && meshLayers.size() == 1) chosenMesh = meshLayers.front();
    if (!chosenMesh && !meshLayers.isEmpty())
        return failSave(tr("choose the active mesh"), newPath,
                        tr("select one mesh in the mesh editing toolbar and retry"));
    for (auto *ml : meshLayers) {
        if (ml != chosenMesh && ml->hasUnsavedMeshEdits())
            return failSave(tr("save edits to an inactive mesh"), ml->sourcePath(),
                            tr("%1 has unsaved mesh edits. Project Save currently "
                               "supports one edited mesh at a time; it cannot preserve "
                               "edits on inactive meshes. Keep this project open to "
                               "retain those edits").arg(ml->name()));
    }

    // An imported mesh owns an immutable source snapshot until its first Save.
    // An untitled project has no final directory yet; bind that destination
    // here, without changing its draft identity if any part of Save fails.
    const GeneratedMeshArtifacts::Entry *pendingImportedMesh = nullptr;
    if (chosenMesh && chosenMesh->generatedArtifacts()
        && !chosenMesh->generatedArtifacts()->isPublished()) {
        for (const auto &entry : chosenMesh->generatedArtifacts()->entries())
            if (entry.role == ProjectSaveOutputs::Mesh && chosenMesh->generatedArtifacts()->isPendingEntry(entry))
                pendingImportedMesh = &entry;
    }
    GeneratedMeshArtifacts::Entry rebasedImport;
    const GeneratedMeshArtifacts::Entry *importDestinationOverride = nullptr;
    const QString originalMeshPath = chosenMesh ? chosenMesh->sourcePath() : QString();
    bool saveSucceeded = false;
    const auto restoreImportPath = qScopeGuard([&] {
        if (!saveSucceeded && importDestinationOverride && chosenMesh)
            chosenMesh->setSourcePath(originalMeshPath);
    });
    if (pendingImportedMesh && chosenMesh->importNeedsSaveAsRebase()) {
        const QFileInfo original(originalMeshPath);
        const QDir destinationDirectory = QFileInfo(newPath).absoluteDir();
        const QString suffix = original.suffix().isEmpty() ? QStringLiteral("2dm") : original.suffix();
        QString candidate = destinationDirectory.filePath(original.completeBaseName() + QLatin1Char('.') + suffix);
        int index = 1;
        while (QFileInfo::exists(candidate) || QFileInfo(candidate).isSymLink())
            candidate = destinationDirectory.filePath(QStringLiteral("%1_%2.%3")
                .arg(original.completeBaseName()).arg(index++).arg(suffix));
        rebasedImport = *pendingImportedMesh;
        rebasedImport.finalPath = candidate;
        if (!ProjectSaveOutputs::captureDestination(candidate, &rebasedImport.destination, errorOut))
            return failSave(tr("prepare the imported mesh destination"), candidate,
                            errorOut ? *errorOut : tr("the destination cannot be inspected"));
        if (rebasedImport.destination.fingerprint != QStringLiteral("absent"))
            return failSave(tr("prepare the imported mesh destination"), candidate,
                            tr("a file appeared at the new destination; retry Save As"));
        importDestinationOverride = &rebasedImport;
        chosenMesh->setSourcePath(candidate);
    }

    // Distinct writer roles must never share a destination. Compare file
    // identity as well as paths: aliases through symlinks/hard links can
    // otherwise let the model writer truncate a mesh or project sidecar.
    // Do this before synchronization too, so rejection preserves engine state.
    if (pluginId.isEmpty()) {
        const auto nativePath = [](const QString &path) {
#ifdef Q_OS_WIN
            return std::filesystem::path(path.toStdWString());
#else
            return std::filesystem::path(path.toUtf8().constData());
#endif
        };
        const auto sameFile = [&](const QString &a, const QString &b) {
            if (a.isEmpty() || b.isEmpty()) return false;
            std::error_code ec;
            if (std::filesystem::equivalent(nativePath(a), nativePath(b), ec)) return true;
            // weakly_canonical also resolves existing ancestor symlinks when
            // the final file does not exist yet. The lexical fallback covers
            // identical destinations even if the filesystem cannot resolve one.
            const auto ca = std::filesystem::weakly_canonical(nativePath(a), ec);
            if (!ec) {
                const auto cb = std::filesystem::weakly_canonical(nativePath(b), ec);
                if (!ec && ca == cb) return true;
            }
            return QDir::cleanPath(QFileInfo(a).absoluteFilePath())
                == QDir::cleanPath(QFileInfo(b).absoluteFilePath());
        };
        struct SaveFileRole { QString label; QString path; };
        QVector<SaveFileRole> outputs = {
            {tr("model output"), newPath},
            {tr("project settings output"), ProjectSerializer::sidecarPathFor(newPath)}
        };
        if (chosenMesh && chosenMesh->isExternalMesh()
            && QFileInfo(newPath).suffix().compare(QStringLiteral("inp"), Qt::CaseInsensitive) == 0)
            outputs.append({tr("active mesh output"), chosenMesh->sourcePath()});
        const auto conflict = [&](const SaveFileRole &a, const SaveFileRole &b) {
            return failSave(tr("save files with conflicting paths"), newPath,
                            tr("%1 (%2) and %3 (%4) refer to the same file; "
                               "choose separate file locations")
                                .arg(a.label, a.path, b.label, b.path));
        };
        for (int i = 0; i < outputs.size(); ++i)
            for (int j = 0; j < i; ++j)
                if (sameFile(outputs[i].path, outputs[j].path))
                    return conflict(outputs[i], outputs[j]);
        const SaveFileRole sourceModel{tr("current model"), mModelLayer->modelFilePath()};
        const SaveFileRole sourceSettings{tr("current project settings"),
                                          ProjectSerializer::sidecarPathFor(sourceModel.path)};
        for (int i = 0; i < outputs.size(); ++i) {
            // A normal Save may replace its own model/settings, never the
            // other role's source. Save As must preserve those source files.
            if (i != 0 && sameFile(outputs[i].path, sourceModel.path))
                return conflict(outputs[i], sourceModel);
            if (i != 1 && sameFile(outputs[i].path, sourceSettings.path))
                return conflict(outputs[i], sourceSettings);
            // Clean layers may be different views of the selected mesh file.
            // The active layer owns that mesh write (Phase 10). Protect other
            // mesh references from model/settings writers, not from their
            // explicitly selected active view of the same resource.
            if (i >= 2) continue;
            for (auto *ml : meshLayers) {
                if (ml == chosenMesh || !ml->isExternalMesh()) continue;
                const SaveFileRole inactive{tr("inactive mesh %1").arg(ml->name()), ml->sourcePath()};
                if (sameFile(outputs[i].path, inactive.path))
                    return conflict(outputs[i], inactive);
            }
        }
    }

    if (chosenMesh && (chosenMesh->ownsGeneratedTopology() || chosenMesh->generatedArtifacts())
        && (!pluginId.isEmpty()
            || QFileInfo(newPath).suffix().compare(QStringLiteral("inp"), Qt::CaseInsensitive) != 0))
        return failSave(tr("save generated mesh topology"), newPath,
                        tr("save this generated mesh as a built-in INP project first"));

    if (chosenMesh && chosenMesh->mesh().vertices.isEmpty())
        return failSave(tr("save the active mesh"), chosenMesh->sourcePath(),
                        tr("the selected mesh has no vertices"));

    if (chosenMesh && chosenMesh->edgeBCs().size()
            != mesh::edgeSlotCount(chosenMesh->mesh().triangles.size()))
        return failSave(tr("save mesh boundary conditions"), chosenMesh->sourcePath(),
                        tr("the boundary-condition data does not match the mesh; "
                           "wait for mesh loading to finish before retrying"));

    if (chosenMesh) {
        const QString invalid = mesh::validateMeshSaveData(chosenMesh->mesh(), chosenMesh->edgeBCs());
        if (!invalid.isEmpty())
            return failSave(tr("validate mesh data"), chosenMesh->sourcePath(), invalid);
    }

    // Mesh edits live on the SWMM2DMeshLayer (its own MeshResult / BC SoA),
    // not in the engine that the writer below serialises. Push them into the
    // engine's in-memory 2D mesh first so vertex-Z / conveyance / BC edits are
    // saved. The engine remains the source of truth for everything the GUI
    // mesh model does not carry (coupling maps, Manning's n, units, options).
    // Inline mesh layers whose per-cell attributes could NOT reach the engine
    // (its mesh no longer matches the layer's — e.g. a mesh generated or
    // replaced this session). The engine write below would drop those edits,
    // so they are re-emitted straight into the written .inp afterwards.
    // Pushing an UNEDITED mesh is pure waste and is the single biggest cost of
    // saving a large-mesh model: the push is O(nVertices x nTriangles) inside
    // the engine (each swmm_2d_set_vertex_z rescans every triangle), which is
    // minutes at a million cells. A layer only diverges from the engine when
    // something edited it, or when it was generated/imported this session —
    // both of which leave hasUnsavedMeshEdits() true.
    QVector<SWMM2DMeshLayer *> inlineNeedsAttrPatch;
    // Counts alone do not certify that geometry or boundary group labels
    // reached the engine. Serialize the selected inline layer on EVERY Save,
    // including clean repeats, because the engine still owns its old XY and
    // connectivity. Only the engine push retains the clean-layer fast path.
    if (chosenMesh && !chosenMesh->isExternalMesh() && pluginId.isEmpty()
        && QFileInfo(newPath).suffix().compare(QStringLiteral("inp"), Qt::CaseInsensitive) == 0)
        inlineNeedsAttrPatch.append(chosenMesh);
    QVector<SWMM2DMeshLayer *> meshLayersPushed;
    int meshLayersSkipped = 0;
    if (canvas()) {
        for (SWMM2DMeshLayer *meshLayer : {chosenMesh}) {
            if (!meshLayer) continue;
            if (meshLayer->ownsGeneratedTopology()) {
                // Indices can refer to entirely different vertices/cells,
                // even when counts match. The layer owns full serialization
                // until the engine is reloaded, including clean repeat Saves.
                meshLayersPushed.append(meshLayer);
                continue;
            }
            // Switching to a clean layer still changes which attributes own
            // the single engine mesh. Do not skip that push on count equality.
            if (meshLayers.size() == 1 && !meshLayer->hasUnsavedMeshEdits()
                && engineMeshMatches(mModelLayer->engine(), meshLayer->mesh())) {
                ++meshLayersSkipped;
                continue;
            }
            QStringList syncWarnings;
            bool trianglesSynced = false;
            bool writeRejected = false;
            mesh::pushMeshEditsToEngine(mModelLayer->engine(), meshLayer->mesh(),
                                        meshLayer->edgeBCs(), &syncWarnings,
                                        &trianglesSynced, &writeRejected);
            if (writeRejected)
                return failSave(tr("synchronize mesh edits"), meshLayer->sourcePath(),
                                syncWarnings.join(QLatin1String("; ")));
            for (const QString &w : syncWarnings)
                qWarning().noquote() << w;
            // GG0a — per-cell infiltration has no engine push path yet (the
            // C API binding lands in GG0f), so the engine writes the
            // [2D_INFILTRATION*] sections from whatever it parsed at open.
            // An inline mesh carrying GUI infiltration edits therefore has to
            // be re-patched even when its triangles DID reach the engine,
            // otherwise every save silently reverts them.
            const mesh::MeshResult &lm = meshLayer->mesh();
            const bool carriesInfil = !lm.infilDefaults.isEmpty()
                                   || !lm.infilOverrides.isEmpty()
                                   || lm.infilOptions.infilStep > 0.0;
            if ((!trianglesSynced || carriesInfil) && !meshLayer->isExternalMesh()
                && !inlineNeedsAttrPatch.contains(meshLayer))
                inlineNeedsAttrPatch.append(meshLayer);
            meshLayersPushed.append(meshLayer);
        }
    }
    meshSyncMs = stage.restart();

    // The layer's CRS is the one the user assigned — via the CRS picker on
    // open, the Simulation Options page, a canvas reprojection, or the .oswp.
    // Push it into the engine's [OPTIONS] CRS so the written .inp declares
    // it, whichever path set it (the reprojection path used to reach only
    // the engine's spatial frame, which the .inp writer did not read).
    // Only an authority-coded CRS is pushed: it is a single token the
    // [OPTIONS] reader round-trips verbatim, whereas re-serialising a
    // WKT-only CRS could alter the original string. A CRS the open merely
    // DEFAULTED (from [MAP] UNITS or the preferences) is never written — a
    // model that carried no CRS must stay that way; see crsAssigned().
    const SpatialReferenceSystem *srs = mModelLayer->srs();
    if (srs && mModelLayer->crsAssigned()) {
        const QString auth = srs->toAuthority();
        if (!srs->isLocal() && !auth.isEmpty() && auth != QStringLiteral("Local")) {
            char cur[512] = {};
            const bool same =
                swmm_get_crs(mModelLayer->engine(), cur, sizeof cur) == 0
                && QString::fromUtf8(cur) == auth;
            if (!same)
                mModelLayer->setOption(QByteArrayLiteral("CRS"), auth);
        }
    }

    // Preflight the chosen external file before writing the model.
    // The engine writes inline during this Save so it
    // cannot overwrite another mesh through a stale or rebased reference.
    QString          extMeshPath;
    QByteArray externalSnapshot;
    SWMM2DMeshLayer *extMeshLayer = nullptr;
    if (canvas() && pluginId.isEmpty()
        && QFileInfo(newPath).suffix().compare(QStringLiteral("inp"),
                                               Qt::CaseInsensitive) == 0)
    {
        if (chosenMesh && chosenMesh->isExternalMesh())
        {
            extMeshPath = chosenMesh->sourcePath();
            if (extMeshPath.isEmpty())
                return failSave(tr("read the external mesh"), extMeshPath,
                                tr("the mesh destination is empty"));
            extMeshPath = QFileInfo(extMeshPath).absoluteFilePath();
            const bool newGeneratedDestination = chosenMesh->ownsGeneratedTopology()
                && !QFileInfo::exists(extMeshPath);
            if (!pendingImportedMesh && !newGeneratedDestination) {
                if (!QFileInfo(extMeshPath).isFile())
                    return failSave(tr("read the external mesh"), extMeshPath,
                                    tr("the mesh file is missing or is not a regular file"));
                QFile mf(extMeshPath);
                if (!mf.open(QIODevice::ReadOnly))
                    return failSave(tr("read the external mesh"), extMeshPath, mf.errorString());
                externalSnapshot = mf.readAll();
                ++dmReads;
                if (mf.error() != QFileDevice::NoError)
                    return failSave(tr("read the external mesh"), extMeshPath, mf.errorString());
                if (externalSnapshot.isEmpty() && !chosenMesh->ownsGeneratedTopology())
                    return failSave(tr("read the external mesh"), extMeshPath,
                                    tr("the mesh file is empty"));
            }
            extMeshLayer = chosenMesh;
        }
    }
    meshReadMs = stage.restart();

    QString stagedModelPath = newPath;
    QString stagedMeshPath = extMeshPath;
    const QString finalSettingsPath = ProjectSerializer::sidecarPathFor(newPath);
    QString stagedSettingsPath = finalSettingsPath;
    if (pluginId.isEmpty()) {
        if (chosenMesh && chosenMesh->generatedArtifacts())
            chosenMesh->generatedArtifacts()->protectInputs(saveOutputs);
        saveOutputs.protect(mModelLayer->modelFilePath(), ProjectSaveOutputs::Model);
        saveOutputs.protect(ProjectSerializer::sidecarPathFor(mModelLayer->modelFilePath()),
                            ProjectSaveOutputs::Settings);
        for (auto *ml : meshLayers) {
            if (ml->isExternalMesh())
                saveOutputs.protect(ml->sourcePath(), ProjectSaveOutputs::Mesh);
        }
        stagedModelPath = saveOutputs.stage(newPath, ProjectSaveOutputs::Model);
        if (stagedModelPath.isEmpty())
            return failSave(tr("stage the model"), newPath, saveOutputs.error());
        if (!finalSettingsPath.isEmpty()) {
            stagedSettingsPath = saveOutputs.stage(finalSettingsPath, ProjectSaveOutputs::Settings);
            if (stagedSettingsPath.isEmpty())
                return failSave(tr("stage project settings"), finalSettingsPath, saveOutputs.error());
        }
        if (!extMeshPath.isEmpty() && !pendingImportedMesh) {
            stagedMeshPath = saveOutputs.stage(extMeshPath, ProjectSaveOutputs::Mesh);
            if (stagedMeshPath.isEmpty())
                return failSave(tr("stage the external mesh"), extMeshPath, saveOutputs.error());
            QFile stagedMesh(stagedMeshPath);
            if (!stagedMesh.open(QIODevice::WriteOnly)
                || stagedMesh.write(externalSnapshot) != externalSnapshot.size()
                || !stagedMesh.flush())
                return failSave(tr("stage the external mesh"), extMeshPath, stagedMesh.errorString());
            stagedMesh.close();
            ++dmWrites;
        }
    }
    externalSnapshot.clear();
    if (chosenMesh && chosenMesh->generatedArtifacts()) {
        QString artifactError;
        if (!chosenMesh->generatedArtifacts()->prepareSave(saveOutputs, &artifactError, importDestinationOverride))
            return failSave(tr("prepare pending mesh and terrain outputs"), newPath, artifactError);
        if (pendingImportedMesh) {
            stagedMeshPath.clear();
            for (const auto &output : saveOutputs.preparedOutputs())
                if (output.role == ProjectSaveOutputs::Mesh && output.finalPath == extMeshPath)
                    stagedMeshPath = output.stagedPath;
            if (stagedMeshPath.isEmpty())
                return failSave(tr("prepare the imported mesh"), extMeshPath,
                                tr("the import snapshot was not staged for this destination"));
            ++dmWrites;
        }
    }

    QByteArray utf8 = newPath.toUtf8();
    QByteArray idUtf8 = pluginId.toUtf8();
    // The engine's warning list is cumulative, so the count taken here brackets
    // exactly what THIS write appends. The writer reports data loss through it
    // ("embedded [REACTION_*] sections are ... lost from this save", engine
    // 7d43a1ff) — before that fix the sink was never wired and the loss was
    // silent all the way to the user; reading the delta here is the GUI half.
    const int engineWarnsBefore =
        swmm_get_warning_count(mModelLayer->engine());
    // Built-in INP serialization can otherwise rewrite an external mesh at
    // its OLD reference (or at an unrelated same-named Save As destination).
    // Temporarily request inline output; the GUI owns the selected external
    // file and publishes its reference below. Plugin behavior is separate.
    const bool guiOwnsMeshOutput = chosenMesh && pluginId.isEmpty()
        && QFileInfo(newPath).suffix().compare(QStringLiteral("inp"), Qt::CaseInsensitive) == 0;
    QByteArray previousMeshReference(32768, '\0');
    if (guiOwnsMeshOutput) {
        const int readRc = swmm_options_get_ext(mModelLayer->engine(), "MESH_FILE",
                                                previousMeshReference.data(), previousMeshReference.size());
        const int length = int(qstrlen(previousMeshReference.constData()));
        if (readRc != 0 || length == previousMeshReference.size() - 1)
            return failSave(tr("read the engine mesh reference"), newPath,
                            tr("the reference could not be read completely (code %1)").arg(readRc));
        previousMeshReference.resize(length);
        const int detachRc = swmm_options_set_ext(mModelLayer->engine(), "MESH_FILE", "");
        if (detachRc != 0)
            return failSave(tr("isolate mesh output"), newPath,
                            tr("the engine rejected inline serialization (code %1)").arg(detachRc));
    }
    // The engine enumerates every physical output, including component
    // configs and engine-owned meshes, through this synchronous mapper.
    struct MapperContext { ProjectSaveOutputs *outputs; QByteArray path; } mapper{&saveOutputs, {}};
    const auto mapOutput = [](void *data, const char *finalPath, int kind) -> const char * {
        auto &context = *static_cast<MapperContext *>(data);
        if (kind < ProjectSaveOutputs::Model || kind > ProjectSaveOutputs::Component)
            return nullptr;
        context.path = context.outputs->stage(QString::fromUtf8(finalPath),
            static_cast<ProjectSaveOutputs::Role>(kind), kind == ProjectSaveOutputs::Model).toUtf8();
        return context.path.isEmpty() ? nullptr : context.path.constData();
    };
    modelWriteStarted = !pluginId.isEmpty();
    const int rc = pluginId.isEmpty()
        ? swmm_model_write_staged(mModelLayer->engine(), utf8.constData(), mapOutput, &mapper)
        : swmm_model_write_with_plugin(mModelLayer->engine(), utf8.constData(), idUtf8.constData());
    if (guiOwnsMeshOutput) {
        const int restoreRc = swmm_options_set_ext(mModelLayer->engine(), "MESH_FILE",
                                                   previousMeshReference.constData());
        if (restoreRc != 0)
            return failSave(tr("restore the engine mesh reference"), newPath,
                            tr("the engine rejected the reference (code %1)").arg(restoreRc));
    }
    engineWriteMs = stage.restart();
    if (rc == 0)
    {
        const int engineWarnsAfter =
            swmm_get_warning_count(mModelLayer->engine());
        for (int i = engineWarnsBefore; i < engineWarnsAfter; ++i)
            mLastSaveWarnings.append(QString::fromUtf8(
                swmm_get_warning_at(mModelLayer->engine(), i)).trimmed());
    }
    if (rc != 0) {
        // The engine forwards its writer diagnostics (the failing file and
        // OS reason) to the warning list on failure too; surface them.
        QStringList engineReasons;
        const int engineWarnsAfter = swmm_get_warning_count(mModelLayer->engine());
        for (int i = engineWarnsBefore; i < engineWarnsAfter; ++i)
            engineReasons.append(QString::fromUtf8(
                swmm_get_warning_at(mModelLayer->engine(), i)).trimmed());
        QString reason = pluginId.isEmpty()
            ? tr("the built-in writer failed (code %1)").arg(rc)
            : tr("writer %1 failed (code %2)").arg(pluginId).arg(rc);
        if (!saveOutputs.error().isEmpty()) engineReasons.append(saveOutputs.error());
        if (!engineReasons.isEmpty())
            reason += QStringLiteral(": ") + engineReasons.join(QStringLiteral("; "));
        return failSave(tr("write the model"), newPath, reason);
    }
    // Prepare GUI-owned attributes and boundaries on the staged mesh. The
    // engine's inline output may contain stale topology and is not its source.
    if (!extMeshPath.isEmpty())
    {
        QString patchErr;
        mesh::InpMeshWriter::UnitInfo units;
        units.linearUnitName = extMeshLayer->meshUnitsSI() ? QStringLiteral("SI (m)")
                                                          : QStringLiteral("project units");
        const bool replacement = extMeshLayer->ownsGeneratedTopology();
        if (replacement && !mesh::InpMeshWriter::validateTopologyReplacement(stagedModelPath, &patchErr))
            return failSave(tr("replace mesh topology"), newPath, patchErr);
        const bool meshWritten = replacement && !extMeshLayer->preservesImportedSections()
            ? mesh::InpMeshWriter::replaceMeshSections(stagedMeshPath, extMeshLayer->mesh(),
                                                       extMeshLayer->edgeBCs(), &patchErr, 0.035, &units)
            : mesh::InpMeshWriter::patchMeshSections(stagedMeshPath, extMeshLayer->mesh(),
                                                     extMeshLayer->edgeBCs(), &patchErr);
        if (!meshWritten)
            return failSave(tr("save the external mesh"), extMeshPath, patchErr);
        ++dmReads;
        ++dmWrites;
        meshPatchMs = stage.restart();
        QString meshErr;
        if (!mesh::InpMeshWriter::writePreparedMeshFileRef(stagedModelPath, extMeshPath, stagedMeshPath, &meshErr))
            return failSave(tr("save the mesh reference"), newPath, meshErr);
        meshRefMs = stage.restart();
    }

    // Write the inline layer's complete GUI-owned payload, even when the
    // engine accepted its indexed attributes. Geometry and BC group labels
    // have no corresponding synchronization setters. Counts must still match:
    // replacement/remapping of other mesh-indexed model data is separate work.
    for (SWMM2DMeshLayer *ml : inlineNeedsAttrPatch)
    {
        mesh::InpMeshWriter::UnitInfo units;
        units.linearUnitName = ml->meshUnitsSI() ? QStringLiteral("SI (m)")
                                               : QStringLiteral("project units");
        QString patchErr;
        const bool meshWritten = ml->ownsGeneratedTopology()
            ? mesh::InpMeshWriter::replaceMeshSections(stagedModelPath, ml->mesh(), ml->edgeBCs(),
                                                       &patchErr, 0.035, &units)
            : mesh::InpMeshWriter::patchMeshSections(stagedModelPath, ml->mesh(), ml->edgeBCs(),
                                                     &patchErr, 0.035, &units);
        if (!meshWritten)
            return failSave(tr("save the inline mesh"), newPath, patchErr);
    }
    inlinePatchMs = stage.restart();

    // Slice RB.1+2 — sidecar auto-create. Every successful built-in .inp
    // write also produces a sibling .oswp project file. Plugin-driven
    // writes (non-empty pluginId, e.g. GeoPackage export) are deliberately
    // skipped — those are standalone exports per Slice AA-3.5 contract.
    // RB.2: log a one-line "Creating sibling project file:" sentinel the
    // first time the sidecar appears on disk so the user sees the
    // auto-create. Subsequent saves of an existing .oswp are silent.
    if (pluginId.isEmpty())
    {
        const QString oswpPath = finalSettingsPath;
        if (!oswpPath.isEmpty())
        {
            QString sidecarErr;
            bool sidecarSaved = false;
            {
                // Serialize paths relative to the proposed Save As location,
                // but publish the new identity only after the sidecar commits.
                const QSignalBlocker blocker(mModelLayer);
                const QString originalPath = mModelLayer->modelFilePath();
                const QString originalMeshPath = chosenMesh ? chosenMesh->sourcePath() : QString();
                const auto restorePath = qScopeGuard([&] {
                    mModelLayer->setModelFilePath(originalPath);
                    if (chosenMesh) chosenMesh->setSourcePath(originalMeshPath);
                });
                mModelLayer->setModelFilePath(newPath);
                if (chosenMesh && !chosenMesh->isExternalMesh()) chosenMesh->setSourcePath(newPath);
                sidecarSaved = ProjectSerializer::saveToFile(stagedSettingsPath, this, &sidecarErr);
            }
            if (!sidecarSaved)
            {
                return failSave(tr("stage project settings"), oswpPath, sidecarErr);
            }
        }
    }
    oswpMs = stage.restart();

    if (pluginId.isEmpty()) {
        QString validationError;
        QStringList validationWarnings;
        const auto *expectedMesh = chosenMesh ? &chosenMesh->mesh() : nullptr;
        if (!ProjectSaveValidation::validate(saveOutputs, mModelLayer->engine(),
                &validationError, &validationWarnings,
                expectedMesh ? expectedMesh->vertices.size() : -1,
                expectedMesh ? expectedMesh->triangles.size() : -1,
                expectedMesh ? expectedMesh->quadCount() : -1))
            return failSave(tr("validate the prepared project"), newPath, validationError);
        mLastSaveWarnings.append(validationWarnings);
        validationMs = stage.restart();
        const bool settingsExisted = QFile::exists(finalSettingsPath);
        if (!saveOutputs.publish())
            return failSave(tr("publish the prepared project"), newPath, saveOutputs.error());
        if (chosenMesh && chosenMesh->generatedArtifacts())
            chosenMesh->generatedArtifacts()->markPublished(extMeshPath);
        if (!extMeshPath.isEmpty()) { ++dmReads; ++dmWrites; }
        if (!finalSettingsPath.isEmpty() && !settingsExisted)
            qInfo().noquote() << QStringLiteral("Creating sibling project file: %1").arg(finalSettingsPath);
    }

    if (guiOwnsMeshOutput) {
        const QByteArray finalReference = chosenMesh->isExternalMesh()
            ? QFileInfo(chosenMesh->sourcePath()).absoluteFilePath().toUtf8() : QByteArray();
        const int refRc = swmm_options_set_ext(mModelLayer->engine(), "MESH_FILE",
                                               finalReference.constData());
        if (refRc != 0)
            return failSave(tr("adopt the saved mesh reference"), newPath,
                            tr("the engine rejected the reference (code %1)").arg(refRc));
    }

    if (chosenMesh && !chosenMesh->isExternalMesh()) chosenMesh->setSourcePath(newPath);

    // If saved to a new path, point the layer at it so subsequent Save targets the new file.
    if (newPath != mModelLayer->modelFilePath())
        mModelLayer->setModelFilePath(newPath);
    // First successful Save As of an untitled project — promote it. The
    // model lived only in memory until now, so there is nothing on disk to
    // clean up; adopt the file's base name so the Layers panel drops
    // "Untitled" too.
    if (mUntitled)
    {
        mUntitled = false;
        mModelLayer->setName(QFileInfo(newPath).baseName());
        updateWindowTitle();
    }
    saveSucceeded = true;
    if (chosenMesh) chosenMesh->setImportNeedsSaveAsRebase(false);
    setHasChanges(false);
    // Only now that the write is known to have succeeded: a failed save must
    // leave the layers dirty so the next attempt re-pushes them.
    for (SWMM2DMeshLayer *ml : meshLayersPushed) {
        ml->setMeshEditsSaved();
        ml->setGeneratedArtifacts({});
    }

    qCInfo(lcSavePerf).nospace()
        << "[save][stages] meshPushed=" << meshLayersPushed.size()
        << " meshSkipped=" << meshLayersSkipped
        << " meshSync=" << meshSyncMs
        << " meshRead=" << meshReadMs
        << " engineWrite=" << engineWriteMs
        << " meshPatch=" << meshPatchMs
        << " meshRef=" << meshRefMs
        << " inlinePatch=" << inlinePatchMs
        << " oswp=" << oswpMs
        << " validation=" << validationMs
        << " dmReads=" << dmReads
        << " dmWrites=" << dmWrites
        << " total=" << total.elapsed() << " ms";

    // Emitted last so subscribers see the save fully settled (sidecars
    // written, mesh references patched). Every save path funnels through
    // here — Save, Save As, auto-save-before-run, the 2D OUTPUT_FILE
    // default — so one connection in SWMMVis covers them all.
    if (!mLastSaveWarnings.isEmpty())
        emit saveCompletedWithEngineWarnings(mLastSaveWarnings);
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
    // A modal prompt below runs a nested event loop; a re-entrant close
    // (e.g. app-quit walking the sub-windows while the prompt is open)
    // must not stack a second prompt.
    if (mClosePromptActive)
    {
        event->ignore();
        return;
    }

    // Untitled projects live only in memory, so closing one always prompts
    // — even with zero edits, the whole project vanishes on close.
    if (mHasChanges || mUntitled)
    {
        mClosePromptActive = true;
        const auto promptGuard = qScopeGuard([this] { mClosePromptActive = false; });

        if (mUntitled)
        {
            QMessageBox box(this);
            box.setIcon(QMessageBox::Question);
            box.setWindowTitle(tr("Save project?"));
            box.setText(mHasChanges
                ? tr("\"Untitled\" has never been saved and has unsaved changes.")
                : tr("\"Untitled\" has never been saved."));
            box.setInformativeText(tr("Save it to a file, or discard it?"));
            QPushButton *saveAsBtn  = box.addButton(tr("Save As…"),
                                                    QMessageBox::AcceptRole);
            QPushButton *discardBtn = box.addButton(QMessageBox::Discard);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(saveAsBtn);
            box.exec();

            if (box.clickedButton() == saveAsBtn)
            {
                // Real Save As from the prompt — hand off to the main
                // window's shared path-picking flow (filter normalization,
                // last-filter memory). A canceled/failed dialog keeps the
                // window open.
                bool saved = false;
                if (auto *vis = qobject_cast<SWMMVis *>(window()))
                    saved = vis->saveProjectWindowAs(this);
                if (!saved)
                {
                    event->ignore();
                    return;
                }
            }
            else if (box.clickedButton() != discardBtn)
            {
                event->ignore();   // Cancel / Esc
                return;
            }
        }
        else
        {
            const QString name =
                QFileInfo(mModelLayer->modelFilePath()).baseName();
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
                    QMessageBox::critical(this, tr("Save failed"), err);
                    event->ignore();
                    return;
                }
            }
        }
    }
    // Final commit point — emit before the Qt teardown chain runs so
    // observers (profile-plot dialog, etc.) can still touch our model
    // layer / canvas / results layers in their handlers.
    mClosing = true;
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
void SWMMVisProjectWindow::activateSelectTool()
{
    if (mCanvas->activeTool() == mSelectProfileTool && mSelectProfileTool)
        mSelectProfileTool->clearSelection();
    mCanvas->setActiveTool(mSelectTool);
}
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
        QObject::connect(mMeshSelectEdgeTool, &MapToolMeshSelectEdge::statusMessageChanged,
                         this,                &SWMMVisProjectWindow::meshEdgeStatusMessage);
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

void SWMMVisProjectWindow::activateTerrainProfileTool()
{
    // Terrain-toolbar DEM profile. MapToolMeshProfile only captures geometry
    // (it emits a scene polyline and never touches a layer), so it is reused
    // verbatim here; a third instance keeps this tool's checked state on
    // actionTerrainProfile, independent of the two mesh profile actions.
    if (mTerrainProfileTool && mCanvas->activeTool() == mTerrainProfileTool) {
        mCanvas->setActiveTool(mSelectTool);
        return;
    }
    if (!mTerrainProfileTool) {
        mTerrainProfileTool = new MapToolMeshProfile(mCanvas, this);
        QObject::connect(mTerrainProfileTool, &MapToolMeshProfile::profilePathTraced,
                         this, &SWMMVisProjectWindow::terrainProfileTraced);
    }
    mCanvas->setActiveTool(mTerrainProfileTool);
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
void SWMMVisProjectWindow::activateAddVirtualJunctionTool() { mCanvas->setActiveTool(mAddVirtualJunctionTool); }
void SWMMVisProjectWindow::activateAddInletJunctionTool()   { mCanvas->setActiveTool(mAddInletJunctionTool); }
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
        { mAddVirtualJunctionTool, QStringLiteral("actionAddVirtualJunction") },
        { mAddInletJunctionTool,   QStringLiteral("actionAddInletJunction")   },
        { mAddOutfallTool,     QStringLiteral("actionAddOutfall")     },
        { mAddStorageTool,     QStringLiteral("actionAddStorage")     },
        { mAddLidTool,         QStringLiteral("actionAddLidNode")     },
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
        // Analysis "Plot 2D Profile" — its own action since the 1D / 2D
        // profile entries were split apart.
        { mAnalysisMeshProfileTool, QStringLiteral("actionPlotProfile2D") },
        // Terrain-toolbar DEM profile-trace — its own action, so picking Select
        // (or any other canvas tool) unchecks it like the mesh variants.
        { mTerrainProfileTool,   QStringLiteral("actionTerrainProfile")   },
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

void SWMMVisProjectWindow::setEngineVersion(const QString &version, bool markDirty)
{
    if (mEngineVersion == version) return;
    mEngineVersion = version;
    if (markDirty) setHasChanges(true);
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

void SWMMVisProjectWindow::setActiveTerrain(GISRasterLayer *layer, bool markDirty)
{
    if (mActiveTerrain == layer) return;
    mActiveTerrain = layer;

    // Propagate to every add-node and add-link tool (include vertical factor).
    const auto nodeTools = { mAddJunctionTool, mAddOutfallTool,
                              mAddStorageTool, mAddLidTool, mAddDividerTool };
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
    if (markDirty) setHasChanges(true);
}

void SWMMVisProjectWindow::setTerrainNodeOffset(double offset)
{
    if (mTerrainNodeOffset == offset) return;
    mTerrainNodeOffset = offset;

    const auto nodeTools = { mAddJunctionTool, mAddOutfallTool,
                              mAddStorageTool, mAddLidTool, mAddDividerTool };
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

void SWMMVisProjectWindow::setTerrainVerticalUnit(const QString &unit, bool markDirty)
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
                              mAddStorageTool, mAddLidTool, mAddDividerTool };
    for (auto *t : nodeTools)
        if (t) t->setTerrain(mActiveTerrain, mTerrainNodeOffset, mTerrainVertFactor);

    const auto linkTools = { mAddConduitTool, mAddPumpTool,
                              mAddOrificeTool, mAddWeirTool, mAddOutletTool };
    for (auto *t : linkTools)
        if (t) t->setTerrain(mActiveTerrain, mTerrainLinkOffset, mTerrainVertFactor);

    // Vertical-unit change also affects the conversion factor profile
    // dialogs need — re-fire activeTerrainChanged so they re-sample.
    emit activeTerrainChanged(mActiveTerrain);
    if (markDirty) setHasChanges(true);
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

    setActiveTerrain(found, false);

    // Restore or auto-detect vertical unit.
    const QString unit = vertUnit.isEmpty()
                             ? (found ? found->detectVerticalUnit() : QStringLiteral("m"))
                             : vertUnit;
    setTerrainVerticalUnit(unit, false);
}

void SWMMVisProjectWindow::activateAddLidTool()
{
    if (!mModelLayer || !mModelLayer->engine()) return;
    auto eng = mModelLayer->engine();
    QStringList controls;
    for (int i = 0; i < swmm_lid_count(eng); ++i) {
        int type = -1; swmm_lid_get_type(eng, i, &type);
        if (type == 0 || type == 1 || type == 3 || type == 4 || type == 8)
            controls.append(QString::fromUtf8(swmm_lid_id(eng, i)));
    }
    if (controls.isEmpty()) {
        QMessageBox::information(this, tr("Add LID Node"), tr("Create an LID control in the LID Controls editor first. Choose Storage Node to define any number of ordered layers."));
        return;
    }
    bool ok = false;
    const auto control = QInputDialog::getItem(this, tr("Add LID Node"), tr("LID control"), controls, 0, false, &ok);
    if (!ok) return;
    mAddLidTool->setLidControl(control);
    mCanvas->setActiveTool(mAddLidTool);
}
