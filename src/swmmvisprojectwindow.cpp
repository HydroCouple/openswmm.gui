/*!
 * \file   swmmvisprojectwindow.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
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
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "ui/dialogs/crsselectiondialog.h"

#include "core/openswmmvislogmessage.h"
#include "core/unitsystem.h"
#include "map/openswmmvisscene.h"
#include "selection/selectionmanager.h"

#include <QCloseEvent>
#include <QDebug>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

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
    mAddJunctionTool  = new OpenSWMMVisMapToolAddNode(mCanvas, 0, QStringLiteral("J"), this);
    mAddOutfallTool   = new OpenSWMMVisMapToolAddNode(mCanvas, 1, QStringLiteral("O"), this);
    mAddStorageTool   = new OpenSWMMVisMapToolAddNode(mCanvas, 2, QStringLiteral("S"), this);
    mAddDividerTool   = new OpenSWMMVisMapToolAddNode(mCanvas, 3, QStringLiteral("D"), this);

    // Auto-length — last-used value from QSettings, seeded into the canvas
    // dynamic property so map tools can read it without a back-pointer to
    // this project window.
    {
        QSettings settings;
        mAutoLengthEnabled = settings.value(QStringLiteral("SWMMVis/autoLength"), false).toBool();
        mCanvas->setProperty("autoLength", mAutoLengthEnabled);
    }

    // Default tool
    mCanvas->setActiveTool(mPanTool);

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
    QString base = mModelLayer && !mModelLayer->modelFilePath().isEmpty()
                       ? QFileInfo(mModelLayer->modelFilePath()).baseName()
                       : QStringLiteral("Untitled");
    setWindowTitle(mHasChanges ? (base + QStringLiteral(" *")) : base);
}

// ---------------------------------------------------------------------------
// Save / Save As
// ---------------------------------------------------------------------------

bool SWMMVisProjectWindow::save(QString *errorOut)
{
    if (!mModelLayer || mModelLayer->modelFilePath().isEmpty())
    {
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
    QByteArray utf8 = newPath.toUtf8();
    int rc = swmm_model_write(mModelLayer->engine(), utf8.constData());
    if (rc != 0)
    {
        if (errorOut) *errorOut = tr("swmm_model_write failed (code %1)").arg(rc);
        return false;
    }
    // If saved to a new path, point the layer at it so subsequent Save targets the new file.
    if (newPath != mModelLayer->modelFilePath())
        mModelLayer->setModelFilePath(newPath);
    setHasChanges(false);
    return true;
}

// ---------------------------------------------------------------------------
// Close event — prompt if unsaved changes
// ---------------------------------------------------------------------------

void SWMMVisProjectWindow::closeEvent(QCloseEvent *event)
{
    if (mHasChanges)
    {
        QMessageBox::StandardButton btn = QMessageBox::question(
            this, tr("Save changes?"),
            tr("The model \"%1\" has unsaved changes. Save before closing?")
                .arg(QFileInfo(mModelLayer->modelFilePath()).baseName()),
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
    QMdiSubWindow::closeEvent(event);
}

void SWMMVisProjectWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange
            && (windowState() & Qt::WindowMinimized))
    {
        setWindowState(windowState() & ~Qt::WindowMinimized);
        event->ignore();
        return;
    }
    QMdiSubWindow::changeEvent(event);
}

void SWMMVisProjectWindow::activatePanTool()         { mCanvas->setActiveTool(mPanTool); }
void SWMMVisProjectWindow::activateZoomInTool()      { mCanvas->setActiveTool(mZoomInTool); }
void SWMMVisProjectWindow::activateZoomOutTool()     { mCanvas->setActiveTool(mZoomOutTool); }
void SWMMVisProjectWindow::activateSelectTool()      { mCanvas->setActiveTool(mSelectTool); }
void SWMMVisProjectWindow::activateMeasureTool()     { mCanvas->setActiveTool(mMeasureTool); }
void SWMMVisProjectWindow::activateMoveNodeTool()    { mCanvas->setActiveTool(mMoveNodeTool); }
void SWMMVisProjectWindow::activateEditVertexTool()  { mCanvas->setActiveTool(mEditVertexTool); }
void SWMMVisProjectWindow::activateAddJunctionTool() { mCanvas->setActiveTool(mAddJunctionTool); }
void SWMMVisProjectWindow::activateAddOutfallTool()  { mCanvas->setActiveTool(mAddOutfallTool); }
void SWMMVisProjectWindow::activateAddStorageTool()  { mCanvas->setActiveTool(mAddStorageTool); }
void SWMMVisProjectWindow::activateAddDividerTool()  { mCanvas->setActiveTool(mAddDividerTool); }
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
