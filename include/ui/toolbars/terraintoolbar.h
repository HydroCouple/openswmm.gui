/**
 * @file      terraintoolbar.h
 * @brief     Toolbar for selecting an active DTM raster and setting node/link invert offsets used
 *            during interactive editing.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef TERRAINTOOLBAR_H
#define TERRAINTOOLBAR_H

#include <QPointer>
#include <QToolBar>

class GISRasterLayer;
class MapCanvas;
class OpenSWMMVisLayer;
class QAction;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

/*!
 * \class TerrainToolbar
 * \brief Toolbar for selecting an active DTM raster and setting node/link
 *        invert offsets used during interactive editing.
 *
 * The combo box lists every \c GISRasterLayer currently loaded in the active
 * project's \c MapCanvas.  A leading "(none)" entry makes the terrain optional.
 * Two spinboxes supply the signed vertical offsets applied when new nodes and
 * links are placed: node \c InvertElev = terrain_Z + nodeOffset; link endpoint
 * inverts are derived similarly and used to estimate the conduit slope.
 *
 * Call \c rebindCanvas() each time the active project window changes.
 */
class TerrainToolbar : public QToolBar
{
    Q_OBJECT

public:
    /**
     * @param[in] title Visible toolbar title, copied to QToolBar.
     * @param[in] parent Optional QWidget owner; caller owns parentless toolbar.
     * @brief Build owned terrain/unit/conversion/offset/profile controls and subscribe to global
     *   UnitSystem changes.
     * @note GUI-thread only. Initially no canvas/terrain, disabled editing controls, zero 3 decimal
     *   offsets bounded [-1e6,1e6], factor 6 decimals bounded [0.0001,10000]. Factor initial value
     *   reset from global model units. Does not sample a raster or mutate engine.
     */
    explicit TerrainToolbar(const QString &title, QWidget *parent = nullptr);

    /*! Returns the currently selected raster layer, or nullptr when "(none)".
     * @return Borrowed raw raster stored in current combo item, null for (none)/absent combo. Not
     *   guarded by QPointer; canvas/layer removal hooks must keep entries synchronized before layer
     *   destruction. Visibility/opacity not considered, no ownership transfer.
     */
    [[nodiscard]] GISRasterLayer *activeTerrain() const;

    /*! Signed vertical offset (model units) added to terrain Z for nodes.
     * @return Current signed node invert offset in global model depth units, 3 decimal rounded and
     *   clamped by spin box to [-1e6,1e6], zero if control absent. No terrain sample or engine
     *   lookup.
     */
    [[nodiscard]] double nodeOffset() const;

    /*! Signed vertical offset (model units) added to terrain Z for link endpoints.
     * @return Current signed link invert offset in global model depth units, 3 decimal rounded and
     *   clamped by spin box to [-1e6,1e6], zero if control absent. No terrain sample or engine
     *   lookup.
     */
    [[nodiscard]] double linkOffset() const;

    /*!
     * \brief The user-selected or auto-detected vertical unit of the active terrain.
     * \return \c "m" or \c "ft".  The previous selection is retained when no terrain is active.
     * @return Current combo data (m or ft), m only if combo absent. Selection persists when no
     *   terrain is active; no automatic fallback-to-m in that state. Does not resample metadata.
     */
    [[nodiscard]] QString verticalUnit() const;

    /*!
     * \brief Conversion factor to transform raw raster Z values into model
     *        vertical units (determined by UnitSystem flow units).
     *
     *  factor = (metres_per_raster_unit) / (metres_per_model_unit)
     *
     *  Examples:
     *  - raster "m", model SI  →  1.0
     *  - raster "m", model ft  →  3.28084
     *  - raster "ft", model SI →  0.3048
     *  - raster "ft", model ft →  1.0
     * @return Current EDITABLE factor control value, not necessarily nominal unit ratio. If control
     *   absent, nominal DEM-unit/global UnitSystem ratio with international foot 0.3048 m. No
     *   active terrain required, raster scale/offset not included.
     * @note Unit/terrain/global-unit refresh overwrites manual factor with nominal ratio using
     *   blocked spin signals; factor edits alone emit verticalUnitChanged so consumers reread this
     *   getter.
     */
    [[nodiscard]] double verticalToModelFactor() const;

    /*!
     * \brief Rebinds the toolbar to a new project canvas.
     * \details Disconnects the previous canvas's layer signals, connects the
     *          new one's, and repopulates the terrain combo.  Pass nullptr
     *          when no project is active (clears and disables the toolbar).
     * @param[in] canvas Optional borrowed canvas guarded by QPointer, null clears/disables
     *   controls.
     * @note Same nonnull canvas no-op; null→null still rebuilds stale combo after guarded
     *   destruction. Disconnect old layer/destroy hooks, connect new unique layer add/remove plus
     *   destroyed→rebind(null), enable controls based on canvas presence (not terrain
     *   availability), rebuild. Retains offsets/unit/manual-none state across projects. Clearing
     *   with null returns early in rebuild and does NOT emit activeTerrainChanged or refresh units.
     *   No ownership transfer or model write.
     */
    void rebindCanvas(MapCanvas *canvas);

    /*!
     * \brief Restores the toolbar controls from per-project saved state.
     * \param terrainLayerPath  Relative-or-absolute path of the saved terrain layer.
     * \param nodeOff           Saved node offset.
     * \param linkOff           Saved link offset.
     * \param vertUnit          Saved vertical unit ("m" or "ft").  Empty = auto-detect.
     * @note All control assignments signal-blocked; refreshes labels/nominal factor but emits no
     *   terrain/offset/unit notifications. Does not update deliberate-none flag, so later rebuild
     *   may auto-select first raster despite saved none. No deferred path matching for layers
     *   arriving later, no persistence write or engine mutation.
     */
    void restoreState(const QString &terrainLayerPath, double nodeOff, double linkOff,
                      const QString &vertUnit = QString());

    /*! \brief Add the terrain profile-plot action to the toolbar's own
     *  "Profile" ribbon group (iteration 4 — before the trailing spacer,
     *  so it left-packs with the other groups instead of floating right).
     * @param[in] action Optional borrowed QAction, null ignored; caller retains action lifetime.
     * @note Adds to owned embedded profile toolbar when available; action parent/ownership not
     *   changed and no trigger. Repeated action semantics delegated to QToolBar.
     */
    void addProfileAction(QAction *action);

signals:
    /*! Emitted when the selected terrain raster changes.  \p layer is nullptr
     *  when "(none)" is selected or when no project is active.
     * @param[in] layer Borrowed new raw raster or null on deliberate none/removal; not
     *   lifetime-pinned.
     * @note Selection and nonnull-canvas rebuild transitions emit, but null canvas clear path does
     *   not. Control/units already updated for ordinary selection callback; consumers need coherent
     *   layer lifetime.
     */
    void activeTerrainChanged(GISRasterLayer *layer);

    /*! Emitted when the node offset spinbox value changes.
     * @param[in] offset New node offset in global model depth units after spin normalization.
     * @note Directly forwards spin valueChanged; restoreState blocks these signals. No automatic
     *   engine edit or terrain sampling.
     */
    void nodeOffsetChanged(double offset);

    /*! Emitted when the link offset spinbox value changes.
     * @param[in] offset New link offset in global model depth units after spin normalization.
     * @note Directly forwards spin valueChanged; restoreState blocks these signals. No automatic
     *   engine edit or terrain sampling.
     */
    void linkOffsetChanged(double offset);

    /*! Emitted when the vertical unit combo changes ("m" or "ft").
     * @param[in] unit Current combo symbol, may be unchanged even when factor was manually edited.
     * @note Also emitted for factor edits and ordinary terrain-change callback. Not emitted by
     *   blocked restore, autodetection alone, or global UnitSystem-triggered updateUnitLabels;
     *   consumers may need reread verticalToModelFactor on other context changes.
     */
    void verticalUnitChanged(const QString &unit);

private slots:
    /**
     * @param[in] layer Optional borrowed added layer; only GISRasterLayer triggers rebuild.
     * @note Does not use visibility/opacity and may auto-select first raster if deliberate-none
     *   flag false. No engine edit.
     */
    void onLayerAdded(OpenSWMMVisLayer *layer);
    /**
     * @param[in] layer Optional borrowed removed layer, still live during signal; nonraster
     *   ignored.
     * @note If active, set none under blocker and emit activeTerrainChanged(null), then rebuild.
     *   Rebuild can auto-select another raster and emit second change if userChoseNone false. Does
     *   not delete layer or clear retained offsets.
     */
    void onLayerRemoved(OpenSWMMVisLayer *layer);
    /**
     * @param[in] index New combo index; 0 means deliberate none, all other values clear that flag.
     * @note Autodetect unit, overwrite nominal factor/update labels, then emit activeTerrainChanged
     *   and verticalUnitChanged, even if effective layer/unit repeats. No index validation beyond
     *   combo getter; no engine write.
     */
    void onComboIndexChanged(int index);

private:
    /**
     * @brief Replace combo list with none plus every GISRasterLayer on bound canvas, retaining
     *   previous identity if present.
     * @note Signal-blocked combo; no canvas returns after clearing without outward notification.
     *   Live canvas fallback selects first raster unless deliberate-none flag, otherwise none.
     *   Effective layer transition refreshes autodetected unit/factor and emits terrain/unit
     *   signals; unchanged identity leaves manual factor intact. Does not filter hidden rasters,
     *   guard raw item pointers or change offsets.
     */
    void rebuildCombo();
    /**
     * @brief Refresh depth-unit suffix/DEM→model arrow and ALWAYS overwrite factor with nominal
     *   ratio.
     * @note Reads global UnitSystem, not engine-specific project. DEM ft uses 0.3048 m, model SI 1
     *   else 0.3048; spin assignment signal-blocked, so manual override is lost silently on this
     *   call despite old comment. Does not convert stored numeric offsets or emit unit/offset
     *   notifications.
     */
    void updateUnitLabels();
    /**
     * @brief Ask active raster detectVerticalUnit and select matching unit combo entry under signal
     *   blocker.
     * @note No active terrain/absent combo/unknown detected string no-op, retaining prior unit.
     *   Does not update factor/labels or emit signal by itself; caller follows updateUnitLabels.
     */
    void autoDetectVerticalUnit();

    QComboBox *m_terrainCombo = nullptr;
    QComboBox *m_verticalUnitCombo = nullptr;
    QLabel *m_conversionLabel = nullptr;    // "m → ft" unit arrow
    QDoubleSpinBox *m_factorSpin = nullptr; // user-editable conversion factor
    QDoubleSpinBox *m_nodeOffsetSpin = nullptr;
    QDoubleSpinBox *m_linkOffsetSpin = nullptr;
    QLabel *m_nodeUnitLabel = nullptr;
    QLabel *m_linkUnitLabel = nullptr;
    QToolBar *m_profileBar = nullptr; // mini bar in the Profile group
    QPointer<MapCanvas> m_canvas;     // canvas dies with its project window; we outlive it
    bool m_userChoseNone = false;     // deliberate "(none)" pick
};

#endif // TERRAINTOOLBAR_H
