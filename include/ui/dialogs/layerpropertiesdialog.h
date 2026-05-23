/*!
 * \file   layerpropertiesdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice D-2 — General / Rendering / Metadata for any OpenSWMMVisLayer.
 * Symbology and Labels tabs land with Phase 7 (Theming).
 */
#ifndef LAYERPROPERTIESDIALOG_H
#define LAYERPROPERTIESDIALOG_H

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QTabWidget;
class QPlainTextEdit;
class QToolButton;

class OpenSWMMVisLayer;

/*!
 * \class LayerPropertiesDialog
 * \brief Inspect and edit basic properties of a layer.
 *
 * Edits are applied to the layer **on Apply / OK** so the user can cancel
 * uncommitted changes. Opacity slider + spin box are kept in sync; CRS
 * picker delegates to the existing CRSSelectionDialog.
 */
class LayerPropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LayerPropertiesDialog(OpenSWMMVisLayer *layer, QWidget *parent = nullptr);
    ~LayerPropertiesDialog() override;

    /*! \brief Build the static "Metadata" summary string for a layer (UUID,
     *         type, extent, child count). Public so tests can assert on it. */
    [[nodiscard]] static QString metadataSummary(const OpenSWMMVisLayer *layer);

private slots:
    void onPickCRS();
    void onOpacitySliderChanged(int v);
    void onOpacitySpinChanged(int v);
    void onPickContourColor();
    void onPickResultsIsolinesColor();
    void onAccept();
    void onApply();

private:
    void buildUi();
    void buildMeshStatsTab();
    void buildResultsStylingTab();
    void readFromLayer();
    void writeToLayer();

    OpenSWMMVisLayer *m_layer = nullptr;

    // General tab
    QLineEdit   *m_nameEdit  = nullptr;
    QLabel      *m_typeLabel = nullptr;
    QLabel      *m_crsLabel  = nullptr;
    QToolButton *m_crsButton = nullptr;
    QString      m_pendingCRSAuthority;   // empty = unchanged

    // Rendering tab
    QCheckBox   *m_visibleBox = nullptr;
    QSlider     *m_opacitySlider = nullptr;
    QSpinBox    *m_opacitySpin   = nullptr;

    // Metadata tab
    QPlainTextEdit *m_metadataText = nullptr;

    // Mesh tab (added only for SWMM2DMeshLayer):
    //  - Display group   : Show edges + Show mesh nodes checkboxes (AZ.3.4)
    //  - Hillshade group : azimuth/altitude/z-exag/min-lit spinboxes (AU.6.4-lite)
    //  - Contours group  : show + interval count + colour + line width (BJ.2-lite)
    //  - Statistics group: pre-existing read-only summary
    QCheckBox      *m_meshShowEdgesBox      = nullptr;
    QCheckBox      *m_meshShowNodesBox      = nullptr;
    QDoubleSpinBox *m_meshHillshadeAzSpin   = nullptr;
    QDoubleSpinBox *m_meshHillshadeAltSpin  = nullptr;
    QDoubleSpinBox *m_meshHillshadeZExSpin  = nullptr;
    QDoubleSpinBox *m_meshHillshadeMinSpin  = nullptr;
    QCheckBox      *m_meshShowContoursBox   = nullptr;
    QSpinBox       *m_meshContourIntervalsSpin = nullptr;
    QToolButton    *m_meshContourColorBtn   = nullptr;
    QColor          m_pendingContourColor;   // mirrors button swatch
    QDoubleSpinBox *m_meshContourWidthSpin  = nullptr;
    // BJ.2-filled — iso-band fill controls
    QCheckBox      *m_meshFilledContoursBox = nullptr;
    QDoubleSpinBox *m_meshFilledOpacitySpin = nullptr;
    QPlainTextEdit *m_statsText             = nullptr;

    // 2D Results styling tab (added only for SWMM2DResultsLayer, CF.MVP-fix.3):
    //  - Color ramp     : style combo (Smooth / Graduated) + class-count spin
    //  - Filled bands   : enable + level count + opacity
    //  - Iso-line strokes: enable + level count + colour + width
    QComboBox      *m_resColorStyleCombo    = nullptr;
    QSpinBox       *m_resColorClassesSpin   = nullptr;
    QCheckBox      *m_resFilledBox          = nullptr;
    QSpinBox       *m_resFilledLevelsSpin   = nullptr;
    QDoubleSpinBox *m_resFilledOpacitySpin  = nullptr;
    QCheckBox      *m_resIsolinesBox        = nullptr;
    QSpinBox       *m_resIsolinesLevelsSpin = nullptr;
    QToolButton    *m_resIsolinesColorBtn   = nullptr;
    QColor          m_pendingIsolinesColor;
    QDoubleSpinBox *m_resIsolinesWidthSpin  = nullptr;

    QTabWidget  *m_tabs = nullptr;
};

#endif // LAYERPROPERTIESDIALOG_H
