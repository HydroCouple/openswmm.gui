/*!
 * \file   swmmvisprojectwindow.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "swmmvisprojectwindow.h"
#include "map/mapcanvas.h"
#include "layers/swmmmodellayer.h"
#include "project/openswmmvisworkspace.h"
#include "map/tools/maptoolpan.h"
#include "map/tools/maptoolzoom.h"
#include "map/tools/maptoolselect.h"
#include "map/tools/maptoolmeasure.h"
#include "map/tools/maptoolmovenode.h"
#include "map/tools/maptooleditvertex.h"
#include "map/tools/maptooladdnode.h"
#include "map/tools/maptooladdlink.h"
#include "map/tools/maptooladdgage.h"
#include "map/tools/maptooladdsubcatchment.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "ui/dialogs/crsselectiondialog.h"

#include "core/openswmmvislogmessage.h"
#include "core/preferencesmanager.h"
#include "core/unitsystem.h"
#include "map/openswmmvisscene.h"
#include "plugins/filefilterregistry.h"
#include "selection/selectionmanager.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
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
    setMinimumSize(600, 400);
    // NOTE: do NOT call setWindowFlags here. In TabbedView mode (the
    // .ui's default) re-flagging a QMdiSubWindow detaches it from the
    // tab stack and floats it as a free-standing window — not what we
    // want. The tab bar already renders the close X (tabsClosable=true)
    // and doesn't expose minimize/maximize on tabs, so the desired
    // "close-only, non-minimizable" affordance is the default.

    // Per-project services (parented to this window so they die with it)
    mUnits            = new UnitSystem(this);
    mSelectionManager = new SelectionManager(this);

    // Canvas
    mCanvas = new MapCanvas(this);
    setWidget(mCanvas);

    // Model layer
    mModelLayer = new SWMMModelLayer(filePath, workspace);
    mModelLayer->setName(filePath.isEmpty()
                             ? QStringLiteral("Untitled")
                             : QFileInfo(filePath).baseName());
    mModelLayer->setVisible(!filePath.isEmpty());

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
    connect(PreferencesManager::instance(), &PreferencesManager::preferenceChanged,
            this, [applyLinkColorsFromPreferences](const QString &group,
                                                   const QString &key) {
                if (group == QLatin1String("Rendering")
                    && key.startsWith(QLatin1String("LinkColor/")))
                    applyLinkColorsFromPreferences();
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
    mSelectTool     = new OpenSWMMVisMapToolSelect(mCanvas, this);
    mMeasureTool    = new OpenSWMMVisMapToolMeasure(mCanvas, this);
    mMoveNodeTool     = new OpenSWMMVisMapToolMoveNode(mCanvas, this);
    mEditVertexTool   = new OpenSWMMVisMapToolEditVertex(mCanvas, this);
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

    // Auto-length — last-used value from QSettings, seeded into the canvas
    // dynamic property so map tools can read it without a back-pointer to
    // this project window.
    {
        QSettings settings;
        mAutoLengthEnabled = settings.value(QStringLiteral("SWMMVis/autoLength"), false).toBool();
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

bool SWMMVisProjectWindow::loadModel(QList<QString> &warnings, QList<QString> &errors)
{
    const bool ok = mModelLayer->loadModel(warnings, errors);
    if (ok)
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
                    QStringList names;
                    names.reserve(current.size());
                    for (const SWMMObjectRef &r : current)
                        names.append(r.name);
                    mModelLayer->setSelectedElementNames(names);
                    *busy = false;
                });
        connect(mModelLayer, &SWMMModelLayer::selectionChanged, this,
                [this, busy](const QStringList &names) {
                    if (*busy) return;
                    *busy = true;
                    QSet<SWMMObjectRef> refs;
                    refs.reserve(names.size());
                    for (const QString &n : names)
                    {
                        const int t = mModelLayer->objectTypeFor(n);
                        if (t == 0) continue;
                        refs.insert({static_cast<SWMMObjectRef::ObjectType>(t), n});
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
            const bool isLocal = modelSRS && modelSRS->toAuthority() == QStringLiteral("Local");

            if (isLocal)
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
                    QPushButton *abortBtn  = mb.addButton(tr("Abort Open"),  QMessageBox::RejectRole);
                    mb.setDefaultButton(chooseBtn);
                    mb.exec();
                    if (mb.clickedButton() == abortBtn)
                    {
                        errors.append(tr("Project open cancelled: no CRS selected."));
                        mModelLayer->setVisible(false);
                        return false;
                    }
                    // else loop back and re-open the picker.
                    Q_UNUSED(chooseBtn);
                }
            }

            if (modelSRS)
            {
                mCanvas->setCanvasSRS(new SpatialReferenceSystem(*modelSRS, mCanvas), true);
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
    return ok;
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
void SWMMVisProjectWindow::activateMeasureTool()
{
    if (mCanvas->activeTool() == mMeasureTool)
        mCanvas->setActiveTool(mSelectTool);
    else
        mCanvas->setActiveTool(mMeasureTool);
}
void SWMMVisProjectWindow::activateMoveNodeTool()    { mCanvas->setActiveTool(mMoveNodeTool); }
void SWMMVisProjectWindow::activateEditVertexTool()  { mCanvas->setActiveTool(mEditVertexTool); }
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
void SWMMVisProjectWindow::zoomToFullExtent()        { mCanvas->zoomToFullExtent(); }

void SWMMVisProjectWindow::setAutoLengthEnabled(bool enabled)
{
    if (mAutoLengthEnabled == enabled)
        return;
    mAutoLengthEnabled = enabled;
    if (mCanvas)
        mCanvas->setProperty("autoLength", enabled);
    QSettings().setValue(QStringLiteral("SWMMVis/autoLength"), enabled);
    emit autoLengthChanged(enabled);
}

void SWMMVisProjectWindow::setEngineVersion(const QString &version)
{
    mEngineVersion = version;
    setHasChanges(true);
}
