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

    void activatePanTool();
    void activateZoomInTool();
    void activateZoomOutTool();
    void activateSelectTool();
    void activateMeasureTool();
    void zoomToFullExtent();

signals:
    void modelLoaded();
    void modelLoadError(const QString &msg);
    void hasChangesChanged(bool dirty);
    void offsetModeChanged(bool elevation);

protected:
    void closeEvent(QCloseEvent *event) override;

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

    OpenSWMMVisMapToolPan     *mPanTool     = nullptr;
    OpenSWMMVisMapToolZoom    *mZoomInTool  = nullptr;
    OpenSWMMVisMapToolZoom    *mZoomOutTool = nullptr;
    OpenSWMMVisMapToolSelect  *mSelectTool  = nullptr;
    OpenSWMMVisMapToolMeasure *mMeasureTool = nullptr;
};

#endif // SWMMVISPROJECTWINDOW_H
