/*!
 * \file   rastersymbologypanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Symbology tab content for a GISRasterLayer — the GIS-style raster
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

namespace OpenSWMM::Render {
class GraduatedRasterRenderer;
class MultiBandColorRenderer;
class PalettedRasterRenderer;
}

namespace openswmmvis::ui {

class ClassificationEditor;

class RasterSymbologyPanel : public QWidget
{
    Q_OBJECT
public:
    /*! Renderer families offered by the combo; the value doubles as the
     *  QStackedWidget page index. */
    enum class Kind : int { Graduated = 0, Paletted = 1, MultiBand = 2 };

    explicit RasterSymbologyPanel(GISRasterLayer *layer, QWidget *parent = nullptr);

    /*! Family of the layer's LIVE renderer (not the combo selection). */
    [[nodiscard]] Kind currentKind() const;

public slots:
    /*! Re-read every control from the layer / its renderer. */
    void refreshFromModel();

private:
    [[nodiscard]] QGroupBox *buildSourceGroup();
    [[nodiscard]] QWidget   *buildGraduatedPage();
    [[nodiscard]] QWidget   *buildPalettedPage();
    [[nodiscard]] QWidget   *buildMultiBandPage();
    [[nodiscard]] QGroupBox *buildHillshadeGroup();

    void onKindComboChanged(int index);
    /*! Install a fresh renderer of \p kind seeded from the dataset. */
    void installKind(Kind kind);

    void rebuildPalettedTable();
    void pushPalettedTable();
    void pushMultiBand();
    void pushHillshade();

    [[nodiscard]] OpenSWMM::Render::GraduatedRasterRenderer *graduated() const;
    [[nodiscard]] OpenSWMM::Render::PalettedRasterRenderer  *paletted() const;
    [[nodiscard]] OpenSWMM::Render::MultiBandColorRenderer  *multiband() const;

    QPointer<GISRasterLayer> m_layer;

    QComboBox      *m_kindCombo   = nullptr;
    QStackedWidget *m_stack       = nullptr;

    QGroupBox *m_sourceBox   = nullptr;
    QSpinBox  *m_bandSpin    = nullptr;
    QLabel    *m_nodataLabel = nullptr;

    ClassificationEditor *m_classEditor = nullptr;
    QCheckBox            *m_clipCheck   = nullptr;

    QComboBox          *m_paletteCombo = nullptr;
    QTableView         *m_palTable     = nullptr;
    QStandardItemModel *m_palModel     = nullptr;
    QPushButton        *m_classifyBtn  = nullptr;
    QPushButton        *m_loadTableBtn = nullptr;

    QSpinBox *m_redSpin   = nullptr;
    QSpinBox *m_greenSpin = nullptr;
    QSpinBox *m_blueSpin  = nullptr;
    QSpinBox *m_alphaSpin = nullptr;

    QGroupBox      *m_hsBox      = nullptr;
    QCheckBox      *m_hsEnable   = nullptr;
    QDoubleSpinBox *m_hsAzimuth  = nullptr;
    QDoubleSpinBox *m_hsAltitude = nullptr;
    QDoubleSpinBox *m_hsZFactor  = nullptr;
    QDoubleSpinBox *m_hsStrength = nullptr;

    bool m_suppress = false;   // true while the panel itself writes controls
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_RASTERSYMBOLOGYPANEL_H
