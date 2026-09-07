/*!
 * \file   featurelayerpanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * The Features dock: pick the edit target, edit the schema, edit the Z policy
 * (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §5.3).
 *
 * This is a VIEW in the CLAUDE.md §5.1 sense. It never touches OGR and never
 * calls a FeatureLayer setter that bypasses the undo stack: schema edits go
 * through AddFieldCommand / RemoveFieldCommand and the resample goes through
 * ResampleZCommand, so the same edit is undoable whether it was made here, on
 * the map, or in the attribute table. It rebuilds itself from the layer's
 * featuresChanged / schemaChanged / zPolicyChanged signals and from the
 * canvas's layerAdded / layerRemoved.
 *
 * The selected layer is the EDIT TARGET: the drawing and editing tools all
 * read it from here, which is why selecting a layer is a first-class action
 * rather than a side effect of the layer tree's selection.
 */

#ifndef FEATURELAYERPANEL_H
#define FEATURELAYERPANEL_H

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QToolButton;

class FeatureLayer;
class MapCanvas;

namespace openswmmvis::ui {

class FeatureLayerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit FeatureLayerPanel(QWidget *parent = nullptr);

    /*! \brief Bind to \p canvas. Passing nullptr detaches and clears. */
    void setCanvas(MapCanvas *canvas);
    [[nodiscard]] MapCanvas *canvas() const { return m_canvas.data(); }

    /*! \brief The layer the drawing / editing tools should write into. */
    [[nodiscard]] FeatureLayer *activeLayer() const { return m_active.data(); }

public slots:
    /*! \brief Re-read the layer list from the canvas, preserving the selection
     *         where the layer still exists. */
    void refreshLayerList();
    /*! \brief Select \p layer if it is in the list. */
    void selectLayer(FeatureLayer *layer);

signals:
    /*! \brief The edit target changed; the tools re-target on this. */
    void activeLayerChanged(FeatureLayer *layer);
    /*! \brief The user asked to create a layer. The host owns the dialog and
     *         the project-GeoPackage path, so the panel only asks. */
    void newLayerRequested();
    void importRequested(FeatureLayer *into);
    void exportRequested(FeatureLayer *layer);
    /*! \brief A non-fatal message for the status bar. */
    void message(const QString &text);

private slots:
    void onLayerRowChanged(int row);
    void onAddField();
    void onRemoveField();
    void onResampleZ();
    void onZPolicyEdited();
    void onRemoveLayer();

private:
    void buildUi();
    /*! Rebuild the schema table, Z controls and status line from m_active. */
    void refreshDetails();
    void refreshStatus();
    /*! Connect / disconnect the per-layer signals as the target changes. */
    void bindActiveLayer(FeatureLayer *layer);
    void populateZSourceLayers();
    [[nodiscard]] bool zEditsSuppressed() const { return m_suppressZEdits; }

    QPointer<MapCanvas>    m_canvas;
    QPointer<FeatureLayer> m_active;

    // Layers
    QListWidget *m_layerList     = nullptr;
    QPushButton *m_newBtn        = nullptr;
    QPushButton *m_importBtn     = nullptr;
    QPushButton *m_exportBtn     = nullptr;
    QPushButton *m_removeBtn     = nullptr;

    // Schema
    QTableWidget *m_fieldTable   = nullptr;
    QPushButton  *m_addFieldBtn  = nullptr;
    QPushButton  *m_removeFieldBtn = nullptr;

    // Z
    QComboBox      *m_zSourceCombo  = nullptr;
    QComboBox      *m_zLayerCombo   = nullptr;
    QSpinBox       *m_zBandSpin     = nullptr;
    QDoubleSpinBox *m_zConstantSpin = nullptr;
    QDoubleSpinBox *m_zScaleSpin    = nullptr;
    QDoubleSpinBox *m_zDensifySpin  = nullptr;
    QCheckBox      *m_zResampleBox  = nullptr;
    QPushButton    *m_resampleBtn   = nullptr;

    QLabel *m_statusLabel = nullptr;

    /*! Guards the Z widgets while refreshDetails() populates them, so
     *  programmatic setValue calls do not look like user edits. */
    bool m_suppressZEdits = false;
};

}   // namespace openswmmvis::ui

#endif // FEATURELAYERPANEL_H
