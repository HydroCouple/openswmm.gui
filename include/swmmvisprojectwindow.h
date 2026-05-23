/*!
 * \file   swmmvisprojectwindow.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * MDI sub-window that hosts a single SWMM model session.
 * Each opened .inp file gets its own window with a dedicated MapCanvas and tools.
 */
#ifndef SWMMVISPROJECTWINDOW_H
#define SWMMVISPROJECTWINDOW_H

#include <QHash>
#include <QMdiSubWindow>
#include <QList>
#include <QString>

class OpenSWMMVisWorkspace;
class MapCanvas;
class SWMMModelLayer;
class SelectionManager;
class UnitSystem;
class OpenSWMMVisMapTool;
class OpenSWMMVisMapToolPan;
class OpenSWMMVisMapToolZoom;
class OpenSWMMVisMapToolSelect;
class OpenSWMMVisMapToolMeasure;
class OpenSWMMVisMapToolAddNode;
class OpenSWMMVisMapToolAddLink;
class OpenSWMMVisMapToolAddGage;
class OpenSWMMVisMapToolAddSubcatchment;
class GISRasterLayer;
class QComboBox;
class QLabel;
class QFrame;

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

    /** Whether an editing session is active (gate for all geometry mutations). */
    bool isEditSessionActive() const { return mEditSessionActive; }
    void setEditSessionActive(bool active);

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
    void activateSelectProfileTool();

    /*! Slice CF.3 — activate the Pick 2D Cells tool on this canvas.
     *  Lazy-creates the tool on first call. No-op when no 2D results
     *  layer is loaded. */
    void activatePick2DCellsTool();

    /*! Direct access to the Select tool so the main window can wire its
     *  context-menu `plotTimeSeriesRequested` into the same chart
     *  dialog that Object Browser right-clicks use. */
    class OpenSWMMVisMapToolSelect *selectTool() const { return mSelectTool; }

    /*! Same direct-access pattern for Slice BC's profile tool — lets
     *  SWMMVis connect `profilePathSelected` to the ProfilePlotDialog
     *  spawner. */
    class OpenSWMMVisMapToolSelectProfile *selectProfileTool() const
    { return mSelectProfileTool; }

    /*! Slice CF.3 — Pick 2D mesh cells tool (box + lasso). Returns null
     *  until a SWMM2DResultsLayer exists on the canvas (created lazily
     *  on first access via activatePick2DCellsTool). */
    class MapToolPick2DCells *pick2DCellsTool() const { return mPick2DCellsTool; }

    /*! Maps each tool pointer to a stable action-object-name key so the
     *  main window can sync toolbar checked states via activeToolChanged. */
    QHash<class OpenSWMMVisMapTool *, QString> toolActionKeys() const;
    void activateAddJunctionTool();
    void activateAddOutfallTool();
    void activateAddStorageTool();
    void activateAddDividerTool();
    void activateAddConduitTool();
    void activateAddPumpTool();
    void activateAddOrificeTool();
    void activateAddWeirTool();
    void activateAddOutletTool();
    void activateAddGageTool();
    void activateAddSubcatchmentTool();
    void zoomToFullExtent();

    /** Auto-length recalculates conduit length from polyline on every
     *  endpoint / vertex edit. Per-project, persisted to QSettings. */
    bool isAutoLengthEnabled() const { return mAutoLengthEnabled; }
    void setAutoLengthEnabled(bool enabled);

    /** Engine version selector (e.g., "5.3.0", "6.0.0", "6.0.0-alpha.1"). Per-project, persisted to project file. */
    QString engineVersion() const { return mEngineVersion; }
    void setEngineVersion(const QString &version);

    /** Rich-text notes mirrored to the engine's [TITLE] section as plain text.
     *  HTML form is preserved in the .oswp sidecar; the engine only ever sees
     *  the flattened plain text. */
    QString notesHtml() const { return mNotesHtml; }
    void setNotesHtml(const QString &html) { mNotesHtml = html; }

    // ── Terrain editing ──────────────────────────────────────────────────────

    /*! Returns the file path of the active terrain raster (empty if none). */
    [[nodiscard]] QString activeTerrainLayerPath() const;

    /*! Direct (non-owning) accessor to the active terrain raster.  Used by
     *  Slice BC's profile plot when the user wants the ground line to
     *  follow a DEM rather than the rim formula (invert + maxDepth). */
    [[nodiscard]] GISRasterLayer *activeTerrain() const { return mActiveTerrain; }

    /*! Multiplier that converts a raw raster sample (in the DEM's native
     *  vertical unit) into the model's vertical unit.  Returns 1.0 when
     *  no terrain is active.  Stays in sync with FLOW_UNITS changes via
     *  `setTerrainVerticalUnit` + the unitsChanged handler. */
    [[nodiscard]] double terrainVertFactor() const { return mTerrainVertFactor; }

    /*! Signed node invert offset relative to terrain Z (model vertical units). */
    [[nodiscard]] double terrainNodeOffset() const { return mTerrainNodeOffset; }

    /*! Signed link endpoint invert offset relative to terrain Z. */
    [[nodiscard]] double terrainLinkOffset() const { return mTerrainLinkOffset; }

    /*!
     * \brief Sets the active terrain raster and propagates it to all add-node
     *        and add-link tools owned by this window.
     * \param layer  Raster layer to sample; nullptr = no terrain assistance.
     */
    void setActiveTerrain(GISRasterLayer *layer);

    /*! Sets node offset and forwards it to all add-node tools. */
    void setTerrainNodeOffset(double offset);

    /*! Sets link offset and forwards it to all add-link tools. */
    void setTerrainLinkOffset(double offset);

    /*! Vertical unit of the active terrain raster ("m" or "ft"). */
    [[nodiscard]] QString terrainVerticalUnit() const { return mTerrainVertUnit; }

    /*!
     * \brief Sets the terrain raster's vertical unit and updates the conversion
     *        factor applied when computing node/link invert elevations.
     */
    void setTerrainVerticalUnit(const QString &unit);

    /*!
     * \brief Restores terrain state from a persisted .oswp session.
     *        Called by ProjectSerializer::applySession after all layers are loaded.
     * \param absoluteLayerPath  Absolute path of the saved raster (may be empty).
     * \param nodeOffset         Saved node invert offset.
     * \param linkOffset         Saved link invert offset.
     * \param vertUnit           Saved vertical unit ("m" or "ft").
     */
    void restoreTerrainState(const QString &absoluteLayerPath,
                             double nodeOffset,
                             double linkOffset,
                             const QString &vertUnit = QString());

signals:
    void modelLoaded();
    void modelLoadError(const QString &msg);
    void hasChangesChanged(bool dirty);
    void editSessionChanged(bool active);
    void offsetModeChanged(bool elevation);
    void autoLengthChanged(bool enabled);

    /*! Slice CF.3 — forwards MapToolPick2DCells::cellsPicked up to the
     *  main window so it can open the Comparison Plot Dialog. */
    void pick2DCellsPicked(class SWMM2DResultsLayer *layer,
                            const QVector<int> &triIdxList);

    /*! Slice BC — fires whenever the active terrain raster changes
     *  (set / cleared / swapped) or its vertical-unit conversion
     *  factor is updated.  The profile-plot dialog listens so the
     *  ground line stays in sync with the toolbar's terrain selection. */
    void activeTerrainChanged(GISRasterLayer *newTerrain);

    /*! Fires once during closeEvent, AFTER the user-prompt save / cancel
     *  decision is committed but BEFORE the QMdiSubWindow teardown has
     *  begun.  All owned QObjects (model layer, results layers, canvas,
     *  etc.) are still alive and safe to dereference.  External windows
     *  that hold raw back-pointers to this project (e.g. ProfilePlotDialog)
     *  connect here so they can drop their references gracefully — using
     *  `QObject::destroyed` instead is unsafe because by the time it
     *  fires the children are already gone. */
    void aboutToClose();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateWindowTitle();
    void repositionMeasurePanel();
    void updateMeasureUnitCombo();

    OpenSWMMVisWorkspace *mWorkspace          = nullptr;
    MapCanvas            *mCanvas             = nullptr;
    SWMMModelLayer       *mModelLayer         = nullptr;
    UnitSystem           *mUnits              = nullptr;
    SelectionManager     *mSelectionManager   = nullptr;
    bool                 mCanvasCRSAdopted    = false;  // true after first successful loadModel
    bool                 mHasChanges          = false;
    bool                 mEditSessionActive   = false;
    bool                 mElevationOffsetMode = false;  // OPTIONS LINK_OFFSETS = ELEVATION
    bool                 mUntitled            = false;  // Slice Y — never saved
    QString              mTempInpPath;                  // owned temp .inp (untitled only)
    QString              mEngineVersion       = "6.0.0";  // Default to newest version
    QString              mNotesHtml;                      // [TITLE] notes (rich HTML)

    OpenSWMMVisMapToolPan         *mPanTool           = nullptr;
    OpenSWMMVisMapToolZoom        *mZoomInTool        = nullptr;
    OpenSWMMVisMapToolZoom        *mZoomOutTool       = nullptr;
    OpenSWMMVisMapToolSelect      *mSelectTool        = nullptr;
    OpenSWMMVisMapToolMeasure     *mMeasureTool       = nullptr;
    class OpenSWMMVisMapToolSelectProfile *mSelectProfileTool = nullptr;
    OpenSWMMVisMapToolAddNode     *mAddJunctionTool   = nullptr;
    OpenSWMMVisMapToolAddNode     *mAddOutfallTool    = nullptr;
    OpenSWMMVisMapToolAddNode     *mAddStorageTool    = nullptr;
    OpenSWMMVisMapToolAddNode     *mAddDividerTool    = nullptr;
    OpenSWMMVisMapToolAddLink     *mAddConduitTool    = nullptr;
    OpenSWMMVisMapToolAddLink     *mAddPumpTool       = nullptr;
    OpenSWMMVisMapToolAddLink     *mAddOrificeTool    = nullptr;
    OpenSWMMVisMapToolAddLink     *mAddWeirTool       = nullptr;
    OpenSWMMVisMapToolAddLink     *mAddOutletTool     = nullptr;
    OpenSWMMVisMapToolAddGage     *mAddGageTool       = nullptr;
    OpenSWMMVisMapToolAddSubcatchment *mAddSubcatchTool = nullptr;
    class MapToolPick2DCells          *mPick2DCellsTool = nullptr;  ///< CF.3 — lazy

    // Measure tool floating panel (child of mCanvas)
    QFrame    *mMeasurePanel       = nullptr;
    QComboBox *mMeasureModeCombo   = nullptr;
    QComboBox *mMeasureUnitCombo   = nullptr;
    QLabel    *mMeasureTotalLabel  = nullptr;

    bool                           mAutoLengthEnabled = false;

    // Set when the user leaves profile mode (profile tool → Select tool)
    // while an accepted profile path is still on screen. The next
    // mouse-press on the canvas with the Select tool active clears the
    // profile overlay and resets the path — matching the user's mental
    // model that "Select + click elsewhere" exits the profile session.
    // Cleared if the user switches to any tool other than Select before
    // clicking, so toggling back to profile keeps the prior path.
    bool                           mClearProfileOnNextCanvasClick = false;

    // Terrain editing state (per-project, persisted to .oswp)
    GISRasterLayer *mActiveTerrain      = nullptr;
    double          mTerrainNodeOffset  = 0.0;
    double          mTerrainLinkOffset  = 0.0;
    QString         mTerrainVertUnit    = QStringLiteral("m"); // raster vertical unit
    double          mTerrainVertFactor  = 1.0;                 // rasterUnit → modelUnit
};

#endif // SWMMVISPROJECTWINDOW_H
