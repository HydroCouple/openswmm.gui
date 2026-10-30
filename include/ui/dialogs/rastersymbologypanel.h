/**
 * @file      rastersymbologypanel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Symbology tab content for a GISRasterLayer — the GIS-style raster
 *         renderer chooser (QGIS "Singleband pseudocolor" / "Paletted" /
 *         "Multiband colour") mounted by LayerStyleDialog.
 *
 *           ┌ Renderer: [ Singleband pseudocolor ▾ ] ─────────────────┐
 *           │ Source     Render band [1]   NoData: -9999               │
 *           │ ┌ page ──────────────────────────────────────────────┐   │
 *           │ │ Graduated: shared ClassificationEditor (Continuous │   │
 *           │ │   / Classified, ramp + invert, method, classes,    │   │
 *           │ │   custom range, class table, Auto-classify)        │   │
 *           │ │   [ ] Clip out-of-range values                     │   │
 *           │ │ Paletted: palette combo, Value/Label/Colour table, │   │
 *           │ │   Classify unique values · Load colour table · + − │   │
 *           │ │ Multiband: R / G / B / A band spins                │   │
 *           │ └────────────────────────────────────────────────────┘   │
 *           │ Hillshade relief (graduated only)                        │
 *           └──────────────────────────────────────────────────────────┘
 *
 *         Every control writes live into the layer's IRasterRenderer and
 *         calls GISRasterLayer::notifyRasterRendererEdited() (or installs
 *         a fresh renderer via setRasterRenderer), so the canvas + legend
 *         repaint immediately. The constructor is read-only: the dialog
 *         snapshots the layer before building tabs, and Cancel / undo
 *         restore through StyleFileIO — the panel re-syncs from
 *         rasterRendererChanged (queued, so an edit never re-enters the
 *         editor that pushed it).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_RASTERSYMBOLOGYPANEL_H
#define OPENSWMMVIS_UI_DIALOGS_RASTERSYMBOLOGYPANEL_H

#include <QPointer>
#include <QWidget>

class GISRasterLayer;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QStandardItemModel;
class QTableView;

namespace OpenSWMM::Render
{
class GraduatedRasterRenderer;
class MultiBandColorRenderer;
class PalettedRasterRenderer;
}

namespace openswmmvis::ui
{

class ClassificationEditor;

class RasterSymbologyPanel : public QWidget
{
    Q_OBJECT
public:
    /*! Renderer families offered by the combo; the value doubles as the
     *  QStackedWidget page index. */
    enum class Kind : int { Graduated = 0, Paletted = 1, MultiBand = 2 };

    /**
     * @brief Build a GUI-thread live raster renderer editor and hydrate it without changing
     * renderer state.
     * @param[in,out] layer Nullable borrowed raster layer, guarded by QPointer; no ownership
     * transfer. Renderer/band notifications queue a refresh after active edit handlers.
     * @param[in] parent Optional Qt widget owner.
     * Offers RGB switching initially only for at least three Byte bands; external RGB restoration
     * can add the choice. Changes replace renderer state or call normal renderer/hillshade
     * notifications. This panel holds no cancel/undo snapshot.
     */
    explicit RasterSymbologyPanel(GISRasterLayer *layer, QWidget *parent = nullptr);

    /*! Family of the layer's LIVE renderer (not the combo selection). */
    /**
     * @brief Identify the live renderer family independently of the combo selection.
     * @return Paletted or MultiBand when the dynamic type matches, otherwise Graduated, including a
     * null/unknown renderer.
     */
    [[nodiscard]] Kind currentKind() const;

public slots:
    /*! Re-read every control from the layer / its renderer. */
    /**
     * @brief Hydrate all relevant controls from the surviving layer under edit suppression and
     * targeted signal blockers.
     * No-op for a destroyed/null layer. Updates band bounds, source/relief visibility, palette
     * table or classification controls; control clamping does not write values back.
     */
    void refreshFromModel();

private:
    /**
     * @brief Create the source band/NoData group and connect one-based band edits to the surviving
     * layer.
     * @return New panel-owned group; band range is [1,max(1,bandCount)], NoData is display-only.
     */
    [[nodiscard]] QGroupBox *buildSourceGroup();
    /**
     * @brief Create the shared live classification editor with an owned RasterSchemeBinding.
     * @return New panel-owned page; includes the below-minimum hiding/above-maximum color policy
     * text.
     */
    [[nodiscard]] QWidget *buildGraduatedPage();
    /**
     * @brief Create palette/class-table controls with live renderer notifications.
     * Palette changes recolor every class while retaining values/labels; classify uses decimated
     * unique values, load replaces classes from the embedded table, add chooses the next
     * nonnegative integer, remove deletes selected rows in reverse order.
     * @return New panel-owned page with owned model/delegate; no local undo.
     */
    [[nodiscard]] QWidget *buildPalettedPage();
    /**
     * @brief Create one-based RGB and optional alpha-band controls.
     * @return New panel-owned page; upper bound max(1,bandCount), alpha zero means absent. Writes
     * all four channels together.
     */
    [[nodiscard]] QWidget *buildMultiBandPage();
    /**
     * @brief Create live relief controls initialized later by refreshFromModel.
     * @return New panel-owned group: azimuth [0,360] degrees, altitude [0,90] degrees, Z factor
     * [0,100], strength [0,1]; enable writes independently.
     */
    [[nodiscard]] QGroupBox *buildHillshadeGroup();

    /**
     * @brief Install a newly selected renderer family unless suppressed, layerless,
     * negative-indexed or already active.
     * @param[in] index Combo row whose item data encodes Kind; normally an existing offered row.
     */
    void onKindComboChanged(int index);
    /*! Install a fresh renderer of \p kind seeded from the dataset. */
    /**
     * @brief Replace the layer renderer with a fresh owned family instance and refresh the
     * controls.
     * @param[in] kind Supported renderer family; callers provide a valid enum.
     * Graduated uses finite band range and samples; paletted uses the embedded table or sampled
     * unique values; RGB starts at bands 1/2/3 with alpha 4 when present. Existing renderer
     * configuration is discarded; no-op without a layer.
     */
    void installKind(Kind kind);

    /**
     * @brief Replace model-owned rows from live classes, or clear them without a paletted renderer.
     * Caller must hold m_suppress to prevent itemChanged writes; model signals remain enabled so
     * the view observes inserted rows.
     */
    void rebuildPalettedTable();
    /**
     * @brief Replace live classes from all three populated table cells per row and notify renderer
     * edits.
     * Requires the builder-created complete row structure. Value text uses QString::toInt without
     * validation (invalid text becomes zero); labels and background-role colors are copied. No-op
     * without a layer/paletted renderer.
     */
    void pushPalettedTable();
    /**
     * @brief Write all four channel spin values to the current multiband renderer and notify edits.
     * No-op without a layer or matching renderer; UI bounds supply one-based RGB and
     * zero-or-one-based alpha indices.
     */
    void pushMultiBand();
    /**
     * @brief Write the four current relief spin values through setHillshadeParams on a surviving
     * layer; no-op without it.
     */
    void pushHillshade();

    /**
     * @brief Borrow the live renderer as its graduated dynamic type.
     * @return Layer-owned renderer or nullptr; invalidated by renderer replacement/layer
     * destruction.
     */
    [[nodiscard]] OpenSWMM::Render::GraduatedRasterRenderer *graduated() const;
    /**
     * @brief Borrow the live renderer as its paletted dynamic type.
     * @return Layer-owned renderer or nullptr; invalidated by renderer replacement/layer
     * destruction.
     */
    [[nodiscard]] OpenSWMM::Render::PalettedRasterRenderer *paletted() const;
    /**
     * @brief Borrow the live renderer as its multiband dynamic type.
     * @return Layer-owned renderer or nullptr; invalidated by renderer replacement/layer
     * destruction.
     */
    [[nodiscard]] OpenSWMM::Render::MultiBandColorRenderer *multiband() const;

    QPointer<GISRasterLayer> m_layer;

    QComboBox *m_kindCombo = nullptr;
    QStackedWidget *m_stack = nullptr;

    QGroupBox *m_sourceBox = nullptr;
    QSpinBox *m_bandSpin = nullptr;
    QLabel *m_nodataLabel = nullptr;

    ClassificationEditor *m_classEditor = nullptr;

    QComboBox *m_paletteCombo = nullptr;
    QTableView *m_palTable = nullptr;
    QStandardItemModel *m_palModel = nullptr;
    QPushButton *m_classifyBtn = nullptr;
    QPushButton *m_loadTableBtn = nullptr;

    QSpinBox *m_redSpin = nullptr;
    QSpinBox *m_greenSpin = nullptr;
    QSpinBox *m_blueSpin = nullptr;
    QSpinBox *m_alphaSpin = nullptr;

    QGroupBox *m_hsBox = nullptr;
    QCheckBox *m_hsEnable = nullptr;
    QDoubleSpinBox *m_hsAzimuth = nullptr;
    QDoubleSpinBox *m_hsAltitude = nullptr;
    QDoubleSpinBox *m_hsZFactor = nullptr;
    QDoubleSpinBox *m_hsStrength = nullptr;

    bool m_suppress = false; // true while the panel itself writes controls
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_RASTERSYMBOLOGYPANEL_H
