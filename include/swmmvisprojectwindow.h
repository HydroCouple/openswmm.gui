/*!
 * \file   swmmvisprojectwindow.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * MDI sub-window that hosts a single SWMM model session.
 * Each opened .inp file gets its own window with a dedicated MapCanvas and tools.
 */
#ifndef SWMMVISPROJECTWINDOW_H
#define SWMMVISPROJECTWINDOW_H

#include <QMdiSubWindow>
#include <QList>

class OpenSWMMVisWorkspace;
class MapCanvas;
class SWMMModelLayer;
class SelectionManager;
class UnitSystem;
class OpenSWMMVisMapToolPan;
class OpenSWMMVisMapToolZoom;
class OpenSWMMVisMapToolSelect;
class OpenSWMMVisMapToolMeasure;
class OpenSWMMVisMapToolMoveNode;
class OpenSWMMVisMapToolEditVertex;
class OpenSWMMVisMapToolAddNode;

/**
 * @brief MDI sub-window owning a MapCanvas, SWMMModelLayer, and map tools.
 *
 * One instance is created per opened .inp file.
 */
class SWMMVisProjectWindow : public QMdiSubWindow
{
    Q_OBJECT

public:
    explicit SWMMVisProjectWindow(
        OpenSWMMVisWorkspace *workspace,
        const QString &filePath = QString(),
        QWidget *parent = nullptr
    );
    
    ~SWMMVisProjectWindow() override;

    MapCanvas        *canvas()           const;
    SWMMModelLayer   *modelLayer()       const;
    UnitSystem       *unitSystem()       const;
    SelectionManager *selectionManager() const;

    /** Offset mode (LINK_OFFSETS option). True = ELEVATION, false = DEPTH. */
    bool isElevationOffsetMode() const { return mElevationOffsetMode; }
    void setElevationOffsetMode(bool elevation);

    /*! Re-read LINK_OFFSETS from the engine into the cached
     *  mElevationOffsetMode flag. Used by the main-window listener for
     *  SWMMModelLayer::optionsChanged so the status-bar checkbox
     *  refreshes after a Simulation Options Apply. */
    void reloadElevationOffsetModeFromEngine();

    bool loadModel(QList<QString> &warnings, QList<QString> &errors);

    /**
     * @brief Save the model to its current path or to a new path.
     * @return true on success.
     */
    bool save(QString *errorOut = nullptr);
    bool saveAs(const QString &newPath, QString *errorOut = nullptr);

    /** Whether the project has unsaved changes. */
    bool hasChanges() const { return mHasChanges; }

    /** Mark the project dirty/clean (also updates the title). */
    void setHasChanges(bool dirty);

    /*! Slice Y — flag a window as a fresh, never-saved project.
     *  Untitled windows skip recent-files registration, force the
     *  title to "Untitled[*]", and route Save through Save As. The
     *  associated `tempInpPath` is deleted on close-without-save and
     *  on first successful Save As. */
    bool isUntitled() const { return mUntitled; }
    void markUntitled(const QString &tempInpPath);

    void activatePanTool();
    void activateZoomInTool();
    void activateZoomOutTool();
    void activateSelectTool();
    void activateMeasureTool();

    /*! Direct access to the Select tool so the main window can wire its
     *  context-menu `plotTimeSeriesRequested` into the same chart
     *  dialog that Object Browser right-clicks use. */
    class OpenSWMMVisMapToolSelect *selectTool() const { return mSelectTool; }
    void activateMoveNodeTool();
    void activateEditVertexTool();
    void activateAddJunctionTool();
    void activateAddOutfallTool();
    void activateAddStorageTool();
    void activateAddDividerTool();
    void zoomToFullExtent();

    /** Auto-length recalculates conduit length from polyline on every
     *  endpoint / vertex edit. Per-project, persisted to QSettings. */
    bool isAutoLengthEnabled() const { return mAutoLengthEnabled; }
    void setAutoLengthEnabled(bool enabled);

signals:
    void modelLoaded();
    void modelLoadError(const QString &msg);
    void hasChangesChanged(bool dirty);
    void offsetModeChanged(bool elevation);
    void autoLengthChanged(bool enabled);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void updateWindowTitle();

    OpenSWMMVisWorkspace *mWorkspace          = nullptr;
    MapCanvas            *mCanvas             = nullptr;
    SWMMModelLayer       *mModelLayer         = nullptr;
    UnitSystem           *mUnits              = nullptr;
    SelectionManager     *mSelectionManager   = nullptr;
    bool                 mCanvasCRSAdopted    = false;  // true after first successful loadModel
    bool                 mHasChanges          = false;
    bool                 mElevationOffsetMode = false;  // OPTIONS LINK_OFFSETS = ELEVATION
    bool                 mUntitled            = false;  // Slice Y — never saved
    QString              mTempInpPath;                  // owned temp .inp (untitled only)

    OpenSWMMVisMapToolPan         *mPanTool           = nullptr;
    OpenSWMMVisMapToolZoom        *mZoomInTool        = nullptr;
    OpenSWMMVisMapToolZoom        *mZoomOutTool       = nullptr;
    OpenSWMMVisMapToolSelect      *mSelectTool        = nullptr;
    OpenSWMMVisMapToolMeasure     *mMeasureTool       = nullptr;
    OpenSWMMVisMapToolMoveNode    *mMoveNodeTool      = nullptr;
    OpenSWMMVisMapToolEditVertex  *mEditVertexTool    = nullptr;
    OpenSWMMVisMapToolAddNode     *mAddJunctionTool   = nullptr;
    OpenSWMMVisMapToolAddNode     *mAddOutfallTool    = nullptr;
    OpenSWMMVisMapToolAddNode     *mAddStorageTool    = nullptr;
    OpenSWMMVisMapToolAddNode     *mAddDividerTool    = nullptr;

    bool                           mAutoLengthEnabled = false;
};

#endif // SWMMVISPROJECTWINDOW_H
