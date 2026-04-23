/*!
 * \file   layerpropertiesdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * Slice D-2 — General / Rendering / Metadata for any OpenSWMMVisLayer.
 * Symbology and Labels tabs land with Phase 7 (Theming).
 */
#ifndef LAYERPROPERTIESDIALOG_H
#define LAYERPROPERTIESDIALOG_H

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QSlider;
class QSpinBox;
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
    void onAccept();
    void onApply();

private:
    void buildUi();
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

    QTabWidget  *m_tabs = nullptr;
};

#endif // LAYERPROPERTIESDIALOG_H
