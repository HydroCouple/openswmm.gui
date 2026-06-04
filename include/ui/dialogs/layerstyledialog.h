/*!
 * \file   layerstyledialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Unified, multitab styling dialog for every OpenSWMMVisLayer.
 *
 *         Slice X.1 — refactored to a QGIS-aligned fixed tab set with a
 *         vertical sidebar (`tabPosition = West`).  The dialog hosts six
 *         tabs in canonical QGIS order — Information / Source / Symbology
 *         / Labels / Rendering / Metadata — and shows only the ones the
 *         current layer type supports per `layerCapabilities()`.
 *
 *         Apply / OK / Cancel buttons buffer edits — Cancel rolls back
 *         every subject and the General/Rendering snapshot.
 *
 *         Entry points:
 *           - Layer-tree right-click → Properties        (any layer)
 *           - Legend dock right-click → Edit Style       (sublayer rows)
 *           - Kind-row right-click → Properties          (focused on kind)
 *
 *         The optional \p initialRoutingId focuses the Symbology tab on
 *         the matching kind / sublayer when launched from a sub-row
 *         context menu.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_LAYERSTYLEDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_LAYERSTYLEDIALOG_H

#include <QDialog>
#include <QFlags>
#include <QJsonObject>
#include <QPointer>
#include <QString>

#include <memory>
#include <vector>

class QLineEdit;
class QLabel;
class QToolButton;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QSlider;
class QSpinBox;
class QPlainTextEdit;
class QTabWidget;

class OpenSWMMVisLayer;
class QUndoStack;   // #36 — optional app-level undo for symbology edits

namespace openswmmvis::ui {

class ILayerStyleSubject;

/*!
 * \enum  LayerCapability
 * \brief Which tabs apply to a given layer type.
 *
 *        Information / Rendering / Metadata are universal (every layer
 *        type has them).  Source applies when the layer has an editable
 *        CRS or filter expression.  Symbology and Labels are gated per
 *        the §X.3.1 table.  The dialog skips any tab whose capability
 *        flag is false — no empty placeholders.
 */
enum class LayerCapability : unsigned int
{
    Information = 1u << 0,
    Source      = 1u << 1,
    Symbology   = 1u << 2,
    Labels      = 1u << 3,
    Rendering   = 1u << 4,
    Metadata    = 1u << 5,
    // VS.5 — the deferred-feature tabs (Temporal / Mask / AuxStorage /
    // Joins / Diagrams) were removed from the dialog for simplicity. Their
    // capability bits are retired; the standalone tab widget classes are
    // scheduled for stale-file cleanup in VS.11.
};
Q_DECLARE_FLAGS(LayerCapabilities, LayerCapability)

/*! Return the capability mask for a layer based on its layerType + class. */
[[nodiscard]] LayerCapabilities layerCapabilities(OpenSWMMVisLayer *layer);

// ---------------------------------------------------------------------------

class LayerStyleDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LayerStyleDialog(OpenSWMMVisLayer *layer,
                              QString initialRoutingId = {},
                              QWidget *parent = nullptr,
                              QUndoStack *undoStack = nullptr);   // #36 — optional
    ~LayerStyleDialog() override;

private slots:
    void onApply();
    void onAccept();
    void onCancel();
    void onPickCRS();
    void onOpacitySliderChanged(int v);
    void onOpacitySpinChanged(int v);
    /*! Slice X.23 — Import / Export buttons in the dialog bar.  Round-
     *  trip the active layer's full styling via the StyleFileIO helper
     *  (native .swmm-style.json + minimal QGIS .qml import). */
    void onImportStyle();
    void onExportStyle();

    /*! Slice X.26 — refresh the Basemap-adjustments group from the
     *  layer's current `basemapRenderParams()` (e.g. after style
     *  import or another view mutated it). */
    void onBasemapRenderParamsChanged();

private:
    void buildTabs();
    void buildInformationTab();
    void buildSourceTab();
    void buildSymbologyTab();
    void buildLabelsTab();
    void buildRenderingTab();
    void buildMetadataTab();
    void readFromLayer();
    void writeGeneralRenderingToLayer();
    void snapshotSubjects();
    void restoreSubjectsFromSnapshot();
    void focusInitialSubject();

    QPointer<OpenSWMMVisLayer> m_layer;
    LayerCapabilities          m_caps;

    // Subjects are owned by the dialog. Populated from layer->styleSubjects()
    // and rendered into the Symbology / Labels tabs as appropriate.
    std::vector<std::unique_ptr<ILayerStyleSubject>> m_subjects;
    std::vector<QJsonObject> m_subjectSnapshots;

    // #36 — app-level undo. m_undoBaseline is the subject state at dialog
    // open; on OK, if it changed, push an EditLayerStyleCommand(before,after)
    // onto m_undoStack so the symbology edit is undoable after the dialog
    // closes. Null stack → no command (back-compat for callers without one).
    QUndoStack              *m_undoStack = nullptr;
    std::vector<QJsonObject>  m_undoBaseline;

    QString  m_initialRoutingId;

    // Vertical sidebar (tabPosition = West).
    QTabWidget *m_tabs = nullptr;

    // Information tab widgets.
    QLineEdit *m_nameEdit  = nullptr;
    QLabel    *m_typeLabel = nullptr;
    QPlainTextEdit *m_infoText = nullptr;

    // Source tab widgets.
    QLabel      *m_crsLabel  = nullptr;
    QToolButton *m_crsButton = nullptr;
    QString      m_pendingCRSAuthority;

    // Rendering tab widgets.
    QCheckBox *m_visibleBox    = nullptr;
    QSlider   *m_opacitySlider = nullptr;
    QSpinBox  *m_opacitySpin   = nullptr;

    // Slice X.26 — basemap render adjustments (only created when the
    // active layer is a basemap kind: XYZ / WMTS / WMS / WCS).
    QGroupBox      *m_basemapAdjustBox  = nullptr;
    QSlider        *m_brightnessSlider  = nullptr;
    QSlider        *m_contrastSlider    = nullptr;
    QSlider        *m_saturationSlider  = nullptr;
    QComboBox      *m_resamplingCombo   = nullptr;

    // Metadata tab.
    QPlainTextEdit *m_metadataText = nullptr;

    // General / Rendering snapshot for Cancel rollback.
    QString m_snapshotName;
    bool    m_snapshotVisible = true;
    double  m_snapshotOpacity = 1.0;
};

} // namespace openswmmvis::ui

Q_DECLARE_OPERATORS_FOR_FLAGS(openswmmvis::ui::LayerCapabilities)

#endif // OPENSWMMVIS_UI_DIALOGS_LAYERSTYLEDIALOG_H
