/*!
 * \file   importfeaturelayerdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * FEATURE_LAYER_TO_SWMM_IMPORT — the "Import Feature Layer → SWMM
 * Objects" dialog (view). Programmatic buildUi (aboutdialog pattern),
 * geometry persisted via dialoglayoutpersistence. The dry run executes
 * on a QtConcurrent worker (MeshGenerationDialog idiom); the import
 * itself runs on the GUI thread as one undo macro.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTFEATURELAYERDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTFEATURELAYERDIALOG_H

#include "ui/dialogs/import/importmapping.h"
#include "ui/dialogs/import/importplan.h"

#include <QDialog>
#include <QFutureWatcher>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableView;

class GISVectorLayer;
class MapCanvas;
class SWMMModelLayer;
class SWMMVisProjectWindow;

namespace openswmmvis::import {

class ImportMappingModel;
class ImportPreviewModel;

class ImportFeatureLayerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImportFeatureLayerDialog(SWMMVisProjectWindow *projectWindow,
                                      QWidget *parent = nullptr);
    ~ImportFeatureLayerDialog() override;

    /*! Pre-select \p layer in the source combo (context-menu launch). */
    void setSourceLayer(GISVectorLayer *layer);

protected:

private slots:
    void onKindChanged();
    void onSourceLayerChanged();
    void onMappingEdited();
    void onPreview();
    void onPreviewFinished();
    void onImport();
    void onSavePreset();
    void onDeletePreset();
    void onLoadPreset(int comboIndex);

private:
    struct PreviewResult {
        ImportPlan plan;
        QString    error;
        int        featureCount = 0;
    };

    void buildUi();
    void repopulateSourceCombo();
    void refreshSourceInfo();
    void refreshPresetCombo();
    void syncOptionsIntoMapping();
    void updateActionEnablement();
    [[nodiscard]] TargetKind currentKind() const;
    [[nodiscard]] GISVectorLayer *currentSourceLayer() const;
    [[nodiscard]] QString presetSettingsGroup() const;

    SWMMVisProjectWindow *m_projectWindow = nullptr;
    MapCanvas            *m_canvas        = nullptr;
    SWMMModelLayer       *m_modelLayer    = nullptr;

    ImportMappingModel   *m_mappingModel  = nullptr;
    ImportPreviewModel   *m_previewModel  = nullptr;

    // Source group
    QComboBox *m_sourceCombo   = nullptr;
    QCheckBox *m_selectedOnly  = nullptr;
    QLabel    *m_sourceInfo    = nullptr;

    // Target group
    QComboBox *m_kindCombo     = nullptr;

    // Mapping group
    QTableView  *m_mappingView  = nullptr;
    QPushButton *m_autoMatchBtn = nullptr;
    QComboBox   *m_presetCombo  = nullptr;
    QPushButton *m_presetSave   = nullptr;
    QPushButton *m_presetDelete = nullptr;
    QLabel      *m_validation   = nullptr;

    // Endpoint group (link kinds only)
    QGroupBox      *m_endpointGroup = nullptr;
    QCheckBox      *m_epFields      = nullptr;
    QCheckBox      *m_epSnap        = nullptr;
    QDoubleSpinBox *m_epTolerance   = nullptr;
    QCheckBox      *m_epAutoCreate  = nullptr;
    QLineEdit      *m_epPrefix      = nullptr;

    // Conflict group
    QRadioButton *m_conflictSkip    = nullptr;
    QRadioButton *m_conflictUpdate  = nullptr;
    QCheckBox    *m_updateAttrs     = nullptr;
    QCheckBox    *m_updateGeom      = nullptr;

    // Preview / results
    QTableView  *m_previewView   = nullptr;
    QLabel      *m_summaryLabel  = nullptr;
    QPushButton *m_previewBtn    = nullptr;
    QPushButton *m_importBtn     = nullptr;
    QPushButton *m_closeBtn      = nullptr;

    QFutureWatcher<PreviewResult> m_previewWatcher;
    bool m_previewRunning = false;
    bool m_planIsCurrent  = false;   ///< false after any edit → must re-preview
};

} // namespace openswmmvis::import

#endif // OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTFEATURELAYERDIALOG_H
