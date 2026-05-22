/*!
 * \file   terraintoolbar.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#ifndef TERRAINTOOLBAR_H
#define TERRAINTOOLBAR_H

#include <QToolBar>

class GISRasterLayer;
class MapCanvas;
class OpenSWMMVisLayer;
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
    explicit TerrainToolbar(const QString &title, QWidget *parent = nullptr);

    /*! Returns the currently selected raster layer, or nullptr when "(none)". */
    [[nodiscard]] GISRasterLayer *activeTerrain() const;

    /*! Signed vertical offset (model units) added to terrain Z for nodes. */
    [[nodiscard]] double nodeOffset() const;

    /*! Signed vertical offset (model units) added to terrain Z for link endpoints. */
    [[nodiscard]] double linkOffset() const;

    /*!
     * \brief The user-selected or auto-detected vertical unit of the active terrain.
     * \return \c "m" or \c "ft".  "m" when no terrain is active.
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
     */
    [[nodiscard]] double verticalToModelFactor() const;

    /*!
     * \brief Rebinds the toolbar to a new project canvas.
     * \details Disconnects the previous canvas's layer signals, connects the
     *          new one's, and repopulates the terrain combo.  Pass nullptr
     *          when no project is active (clears and disables the toolbar).
     */
    void rebindCanvas(MapCanvas *canvas);

    /*!
     * \brief Restores the toolbar controls from per-project saved state.
     * \param terrainLayerPath  Relative-or-absolute path of the saved terrain layer.
     * \param nodeOff           Saved node offset.
     * \param linkOff           Saved link offset.
     * \param vertUnit          Saved vertical unit ("m" or "ft").  Empty = auto-detect.
     */
    void restoreState(const QString &terrainLayerPath,
                      double nodeOff,
                      double linkOff,
                      const QString &vertUnit = QString());

signals:
    /*! Emitted when the selected terrain raster changes.  \p layer is nullptr
     *  when "(none)" is selected or when no project is active. */
    void activeTerrainChanged(GISRasterLayer *layer);

    /*! Emitted when the node offset spinbox value changes. */
    void nodeOffsetChanged(double offset);

    /*! Emitted when the link offset spinbox value changes. */
    void linkOffsetChanged(double offset);

    /*! Emitted when the vertical unit combo changes ("m" or "ft"). */
    void verticalUnitChanged(const QString &unit);

private slots:
    void onLayerAdded(OpenSWMMVisLayer *layer);
    void onLayerRemoved(OpenSWMMVisLayer *layer);
    void onComboIndexChanged(int index);

private:
    void rebuildCombo();
    void updateUnitLabels();
    void autoDetectVerticalUnit();

    QComboBox      *m_terrainCombo       = nullptr;
    QComboBox      *m_verticalUnitCombo  = nullptr;
    QLabel         *m_conversionLabel    = nullptr; // "m → ft" unit arrow
    QDoubleSpinBox *m_factorSpin         = nullptr; // user-editable conversion factor
    QDoubleSpinBox *m_nodeOffsetSpin     = nullptr;
    QDoubleSpinBox *m_linkOffsetSpin     = nullptr;
    QLabel         *m_nodeUnitLabel      = nullptr;
    QLabel         *m_linkUnitLabel      = nullptr;
    MapCanvas      *m_canvas             = nullptr;
};

#endif // TERRAINTOOLBAR_H
