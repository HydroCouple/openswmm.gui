/*!
 * \file   importfeaturelayerdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/import/importfeaturelayerdialog.h"
#include "ui/theme/themehelpers.h"

#include "layers/gisvectorlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/dialoglayoutpersistence.h"
#include "ui/dialogs/import/featurelayerimporter.h"
#include "ui/dialogs/import/importmappingmodel.h"
#include "ui/dialogs/import/importplanning.h"
#include "ui/dialogs/import/importpreviewmodel.h"

#include <ogr_core.h>
#include <ogrsf_frmts.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSplitter>
#include <QTableView>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace openswmmvis::import {

// ===========================================================================
// Construction
// ===========================================================================

ImportFeatureLayerDialog::ImportFeatureLayerDialog(
        SWMMVisProjectWindow *projectWindow, QWidget *parent)
    : QDialog(parent),
      m_projectWindow(projectWindow)
{
    if (m_projectWindow) {
        m_canvas     = m_projectWindow->canvas();
        m_modelLayer = m_projectWindow->modelLayer();
    }

    setObjectName(QStringLiteral("ImportFeatureLayerDialog"));
    setWindowTitle(tr("Import Feature Layer to SWMM Objects"));

    m_mappingModel = new ImportMappingModel(this);
    m_previewModel = new ImportPreviewModel(this);

    buildUi();

    connect(m_mappingModel, &ImportMappingModel::mappingEdited,
            this, &ImportFeatureLayerDialog::onMappingEdited);
    connect(&m_previewWatcher, &QFutureWatcher<PreviewResult>::finished,
            this, &ImportFeatureLayerDialog::onPreviewFinished);

    onKindChanged();   // populates source combo + mapping rows + presets

    // Iteration 2 (D2) — first-run default; the app-wide
    // DialogLayoutWatcher restores the saved layout on first Show.
    resize(860, 720);
    openswmmvis::ui::applyAlwaysOnTopPolicy(this);
}

ImportFeatureLayerDialog::~ImportFeatureLayerDialog()
{
    if (m_previewRunning)
        m_previewWatcher.waitForFinished();
}

// ===========================================================================
// UI
// ===========================================================================

void ImportFeatureLayerDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setObjectName(QStringLiteral("main"));
    root->addWidget(splitter, 1);

    // ---- top pane: configuration -------------------------------------
    auto *configWidget = new QWidget(splitter);
    auto *config = new QVBoxLayout(configWidget);
    config->setContentsMargins(0, 0, 0, 0);

    // Source ------------------------------------------------------------
    auto *sourceGroup = new QGroupBox(tr("Source Feature Layer"), this);
    auto *sourceLay = new QVBoxLayout(sourceGroup);
    auto *sourceRow = new QHBoxLayout;
    m_sourceCombo = new QComboBox(this);
    m_sourceCombo->setSizePolicy(QSizePolicy::Expanding,
                                 QSizePolicy::Preferred);
    sourceRow->addWidget(m_sourceCombo, 1);
    m_selectedOnly = new QCheckBox(tr("Selected features only"), this);
    sourceRow->addWidget(m_selectedOnly);
    sourceLay->addLayout(sourceRow);
    m_sourceInfo = new QLabel(this);
    m_sourceInfo->setWordWrap(true);
    sourceLay->addWidget(m_sourceInfo);
    config->addWidget(sourceGroup);

    // Target ------------------------------------------------------------
    auto *targetGroup = new QGroupBox(tr("Target SWMM Object Type"), this);
    auto *targetLay = new QHBoxLayout(targetGroup);
    m_kindCombo = new QComboBox(this);
    for (TargetKind k : ImportTargetRegistry::allKinds()) {
        if (k == TargetKind::Conduit)
            m_kindCombo->insertSeparator(m_kindCombo->count());
        m_kindCombo->addItem(ImportTargetRegistry::kindLabel(k),
                             static_cast<int>(k));
    }
    targetLay->addWidget(m_kindCombo, 1);
    config->addWidget(targetGroup);

    // Mapping -----------------------------------------------------------
    auto *mapGroup = new QGroupBox(tr("Attribute Mapping"), this);
    auto *mapLay = new QVBoxLayout(mapGroup);

    auto *presetRow = new QHBoxLayout;
    presetRow->addWidget(new QLabel(tr("Preset:"), this));
    m_presetCombo = new QComboBox(this);
    m_presetCombo->setSizePolicy(QSizePolicy::Expanding,
                                 QSizePolicy::Preferred);
    presetRow->addWidget(m_presetCombo, 1);
    m_presetSave = new QPushButton(tr("Save…"), this);
    presetRow->addWidget(m_presetSave);
    m_presetDelete = new QPushButton(tr("Delete"), this);
    presetRow->addWidget(m_presetDelete);
    m_autoMatchBtn = new QPushButton(tr("Auto-match"), this);
    m_autoMatchBtn->setToolTip(
        tr("Match unmapped attributes to identically named source columns."));
    presetRow->addWidget(m_autoMatchBtn);
    mapLay->addLayout(presetRow);

    m_mappingView = new QTableView(this);
    m_mappingView->setModel(m_mappingModel);
    m_mappingView->setItemDelegateForColumn(
        ImportMappingModel::SourceCol,
        new SourceFieldDelegate(m_mappingModel, m_mappingView));
    m_mappingView->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    m_mappingView->verticalHeader()->setVisible(false);
    m_mappingView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mappingView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    mapLay->addWidget(m_mappingView, 1);

    m_validation = new QLabel(this);
    m_validation->setWordWrap(true);
    m_validation->setStyleSheet(openswmmvis::ui::theme::errorTextStyle());
    mapLay->addWidget(m_validation);
    config->addWidget(mapGroup, 1);

    // Endpoints (link kinds) --------------------------------------------
    m_endpointGroup = new QGroupBox(tr("Link Endpoints"), this);
    auto *epLay = new QVBoxLayout(m_endpointGroup);
    m_epFields = new QCheckBox(
        tr("Use the mapped From Node / To Node columns"), this);
    epLay->addWidget(m_epFields);
    auto *snapRow = new QHBoxLayout;
    m_epSnap = new QCheckBox(
        tr("Snap endpoints to existing nodes within"), this);
    m_epSnap->setChecked(true);
    snapRow->addWidget(m_epSnap);
    m_epTolerance = new QDoubleSpinBox(this);
    m_epTolerance->setRange(0.0, 1e9);
    m_epTolerance->setDecimals(3);
    m_epTolerance->setValue(1.0);
    m_epTolerance->setSuffix(tr(" map units"));
    snapRow->addWidget(m_epTolerance);
    snapRow->addStretch(1);
    epLay->addLayout(snapRow);
    auto *autoRow = new QHBoxLayout;
    m_epAutoCreate = new QCheckBox(
        tr("Create junctions at unresolved endpoints, prefix:"), this);
    autoRow->addWidget(m_epAutoCreate);
    m_epPrefix = new QLineEdit(QStringLiteral("J_"), this);
    m_epPrefix->setMaximumWidth(120);
    autoRow->addWidget(m_epPrefix);
    autoRow->addStretch(1);
    epLay->addLayout(autoRow);
    config->addWidget(m_endpointGroup);

    // Conflicts ----------------------------------------------------------
    auto *conflictGroup = new QGroupBox(tr("Existing Objects"), this);
    auto *cfLay = new QVBoxLayout(conflictGroup);
    m_conflictSkip = new QRadioButton(
        tr("Skip — leave existing objects unchanged"), this);
    m_conflictSkip->setChecked(true);
    cfLay->addWidget(m_conflictSkip);
    m_conflictUpdate = new QRadioButton(
        tr("Update existing objects (matched by name)"), this);
    cfLay->addWidget(m_conflictUpdate);
    auto *cfSubRow = new QHBoxLayout;
    cfSubRow->addSpacing(24);
    m_updateAttrs = new QCheckBox(tr("Overwrite mapped attributes"), this);
    m_updateAttrs->setChecked(true);
    m_updateAttrs->setEnabled(false);
    cfSubRow->addWidget(m_updateAttrs);
    m_updateGeom = new QCheckBox(tr("Overwrite geometry"), this);
    m_updateGeom->setEnabled(false);
    cfSubRow->addWidget(m_updateGeom);
    cfSubRow->addStretch(1);
    cfLay->addLayout(cfSubRow);
    config->addWidget(conflictGroup);

    splitter->addWidget(configWidget);

    // ---- bottom pane: preview ----------------------------------------
    auto *previewWidget = new QWidget(splitter);
    auto *pv = new QVBoxLayout(previewWidget);
    pv->setContentsMargins(0, 0, 0, 0);
    m_previewView = new QTableView(this);
    m_previewView->setModel(m_previewModel);
    m_previewView->horizontalHeader()->setSectionResizeMode(
        ImportPreviewModel::DetailCol, QHeaderView::Stretch);
    m_previewView->verticalHeader()->setVisible(false);
    m_previewView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_previewView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pv->addWidget(m_previewView, 1);
    m_summaryLabel = new QLabel(this);
    pv->addWidget(m_summaryLabel);
    splitter->addWidget(previewWidget);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    // ---- buttons ------------------------------------------------------
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_previewBtn = new QPushButton(tr("Preview"), this);
    buttons->addWidget(m_previewBtn);
    m_importBtn = new QPushButton(tr("Import"), this);
    m_importBtn->setEnabled(false);
    buttons->addWidget(m_importBtn);
    m_closeBtn = new QPushButton(tr("Close"), this);
    buttons->addWidget(m_closeBtn);
    root->addLayout(buttons);

    // ---- connections --------------------------------------------------
    connect(m_kindCombo, &QComboBox::currentIndexChanged,
            this, &ImportFeatureLayerDialog::onKindChanged);
    connect(m_sourceCombo, &QComboBox::currentIndexChanged,
            this, &ImportFeatureLayerDialog::onSourceLayerChanged);
    connect(m_autoMatchBtn, &QPushButton::clicked, this, [this]() {
        m_mappingModel->autoMatch();
    });
    connect(m_presetSave, &QPushButton::clicked,
            this, &ImportFeatureLayerDialog::onSavePreset);
    connect(m_presetDelete, &QPushButton::clicked,
            this, &ImportFeatureLayerDialog::onDeletePreset);
    connect(m_presetCombo, &QComboBox::activated,
            this, &ImportFeatureLayerDialog::onLoadPreset);
    connect(m_previewBtn, &QPushButton::clicked,
            this, &ImportFeatureLayerDialog::onPreview);
    connect(m_importBtn, &QPushButton::clicked,
            this, &ImportFeatureLayerDialog::onImport);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);

    connect(m_conflictUpdate, &QRadioButton::toggled, this,
            [this](bool on) {
                m_updateAttrs->setEnabled(on);
                m_updateGeom->setEnabled(on);
            });

    // Any option edit invalidates the current plan.
    const auto invalidatePlan = [this]() {
        m_planIsCurrent = false;
        updateActionEnablement();
    };
    for (QCheckBox *cb : { m_selectedOnly, m_epFields, m_epSnap,
                           m_epAutoCreate, m_updateAttrs, m_updateGeom })
        connect(cb, &QCheckBox::toggled, this, invalidatePlan);
    connect(m_conflictSkip, &QRadioButton::toggled, this, invalidatePlan);
    connect(m_epTolerance, &QDoubleSpinBox::valueChanged, this,
            invalidatePlan);
    connect(m_epPrefix, &QLineEdit::textChanged, this, invalidatePlan);
}

// ===========================================================================
// State helpers
// ===========================================================================

TargetKind ImportFeatureLayerDialog::currentKind() const
{
    return static_cast<TargetKind>(
        m_kindCombo->currentData().toInt());
}

GISVectorLayer *ImportFeatureLayerDialog::currentSourceLayer() const
{
    return m_sourceCombo->currentData().value<GISVectorLayer *>();
}

void ImportFeatureLayerDialog::setSourceLayer(GISVectorLayer *layer)
{
    for (int i = 0; i < m_sourceCombo->count(); ++i) {
        if (m_sourceCombo->itemData(i).value<GISVectorLayer *>() == layer) {
            m_sourceCombo->setCurrentIndex(i);
            return;
        }
    }
}

void ImportFeatureLayerDialog::repopulateSourceCombo()
{
    const TargetKind kind = currentKind();
    GISVectorLayer *previous = currentSourceLayer();

    QSignalBlocker block(m_sourceCombo);
    m_sourceCombo->clear();

    if (!m_canvas) return;
    const bool wantLines = ImportTargetRegistry::isLinkKind(kind);

    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        auto *vec = qobject_cast<GISVectorLayer *>(l);
        if (!vec) continue;

        // Filter by geometry family when the OGR layer reports one;
        // layers with unknown geometry stay listed (the preview flags
        // per-feature mismatches).
        bool compatible = true;
        if (OGRLayer *ogr = vec->ogrLayer()) {
            const OGRwkbGeometryType t = wkbFlatten(ogr->GetGeomType());
            if (t != wkbUnknown && t != wkbNone) {
                const bool isLine  = (t == wkbLineString
                                      || t == wkbMultiLineString);
                const bool isPoint = (t == wkbPoint || t == wkbMultiPoint);
                compatible = wantLines ? isLine : isPoint;
            }
        }
        if (!compatible) continue;

        m_sourceCombo->addItem(vec->name(),
                               QVariant::fromValue(vec));
    }

    // Restore the previous choice when it survived the refilter.
    for (int i = 0; i < m_sourceCombo->count(); ++i) {
        if (m_sourceCombo->itemData(i).value<GISVectorLayer *>()
                == previous) {
            m_sourceCombo->setCurrentIndex(i);
            break;
        }
    }
}

void ImportFeatureLayerDialog::refreshSourceInfo()
{
    GISVectorLayer *src = currentSourceLayer();
    if (!src) {
        m_sourceInfo->setText(
            tr("No compatible feature layer in this project — add a "
               "vector layer first (Layer → Add Vector Layer…)."));
        return;
    }
    QString info = tr("%1 feature(s) — CRS: %2")
                       .arg(src->featureCount())
                       .arg(src->crsDescription().isEmpty()
                                ? tr("(unknown)") : src->crsDescription());
    if (m_modelLayer
        && src->crsDescription() != m_modelLayer->crsDescription())
        info += tr("  ⚠ differs from the model CRS (%1) — coordinates "
                   "will be transformed.")
                    .arg(m_modelLayer->crsDescription().isEmpty()
                             ? tr("unknown") : m_modelLayer->crsDescription());
    m_sourceInfo->setText(info);
}

void ImportFeatureLayerDialog::syncOptionsIntoMapping()
{
    ImportMapping &m = m_mappingModel->mappingRef();
    m.endpointsFromFields   = m_epFields->isChecked();
    m.endpointsSnap         = m_epSnap->isChecked();
    m.snapToleranceMapUnits = m_epTolerance->value();
    m.autoCreateJunctions   = m_epAutoCreate->isChecked();
    m.autoNodePrefix        = m_epPrefix->text().isEmpty()
                                  ? QStringLiteral("J_") : m_epPrefix->text();
    m.conflict = m_conflictUpdate->isChecked()
                     ? ImportMapping::Conflict::Update
                     : ImportMapping::Conflict::Skip;
    m.updateAttributes     = m_updateAttrs->isChecked();
    m.updateGeometry       = m_updateGeom->isChecked();
    m.selectedFeaturesOnly = m_selectedOnly->isChecked();
}

void ImportFeatureLayerDialog::updateActionEnablement()
{
    syncOptionsIntoMapping();
    const QString error = m_mappingModel->validationError();
    m_validation->setText(error);

    const bool ready = currentSourceLayer() && m_modelLayer
                       && error.isEmpty() && !m_previewRunning;
    m_previewBtn->setEnabled(ready);

    const ImportPlan &plan = m_previewModel->plan();
    m_importBtn->setEnabled(ready && m_planIsCurrent
                            && !m_previewModel->resultMode()
                            && (plan.createCount + plan.updateCount) > 0);
}

// ===========================================================================
// Slots
// ===========================================================================

void ImportFeatureLayerDialog::onKindChanged()
{
    const TargetKind kind = currentKind();
    m_endpointGroup->setVisible(ImportTargetRegistry::isLinkKind(kind));

    repopulateSourceCombo();
    GISVectorLayer *src = currentSourceLayer();
    m_mappingModel->reset(kind, src ? src->fieldNames() : QStringList());
    refreshSourceInfo();
    refreshPresetCombo();

    m_previewModel->clear();
    m_summaryLabel->clear();
    m_planIsCurrent = false;
    updateActionEnablement();
}

void ImportFeatureLayerDialog::onSourceLayerChanged()
{
    GISVectorLayer *src = currentSourceLayer();
    m_mappingModel->reset(currentKind(),
                          src ? src->fieldNames() : QStringList());
    refreshSourceInfo();
    m_previewModel->clear();
    m_summaryLabel->clear();
    m_planIsCurrent = false;
    updateActionEnablement();
}

void ImportFeatureLayerDialog::onMappingEdited()
{
    m_planIsCurrent = false;
    updateActionEnablement();
}

void ImportFeatureLayerDialog::onPreview()
{
    GISVectorLayer *src = currentSourceLayer();
    if (!src || !m_modelLayer || m_previewRunning) return;

    syncOptionsIntoMapping();
    const QString error = m_mappingModel->validationError();
    if (!error.isEmpty()) {
        m_validation->setText(error);
        return;
    }

    // Main-thread capture; worker gets plain data only.
    FeatureLayerImporter importer(src, m_modelLayer, m_canvas,
                                  m_mappingModel->mapping());
    const SourceSpec    spec     = importer.sourceSpec();
    const ModelSnapshot snapshot = importer.captureModelSnapshot();
    const ImportMapping mapping  = m_mappingModel->mapping();

    m_previewRunning = true;
    m_previewBtn->setText(tr("Previewing…"));
    updateActionEnablement();

    m_previewWatcher.setFuture(QtConcurrent::run(
        [spec, snapshot, mapping]() -> PreviewResult {
            PreviewResult r;
            const QVector<SourceFeature> features =
                FeatureLayerImporter::readSourceFeatures(spec, &r.error);
            if (!r.error.isEmpty())
                return r;
            r.featureCount = features.size();
            r.plan = buildImportPlan(mapping, snapshot, features);
            return r;
        }));
}

void ImportFeatureLayerDialog::onPreviewFinished()
{
    m_previewRunning = false;
    m_previewBtn->setText(tr("Preview"));

    const PreviewResult r = m_previewWatcher.result();
    if (!r.error.isEmpty()) {
        m_previewModel->clear();
        m_summaryLabel->setText(r.error);
        m_planIsCurrent = false;
        updateActionEnablement();
        return;
    }

    m_previewModel->setPlan(r.plan, /*resultMode*/ false);
    m_summaryLabel->setText(r.featureCount == 0
                                ? tr("The source layer yielded no features "
                                     "(check filter / selection).")
                                : m_previewModel->summaryText());
    m_planIsCurrent = true;
    updateActionEnablement();
}

void ImportFeatureLayerDialog::onImport()
{
    GISVectorLayer *src = currentSourceLayer();
    if (!src || !m_modelLayer || !m_planIsCurrent) return;

    const ImportPlan &plan = m_previewModel->plan();
    const int workCount = plan.createCount + plan.updateCount;
    if (workCount == 0) return;

    QProgressDialog progress(tr("Importing %1 object(s)…").arg(workCount),
                             tr("Stop"), 0, plan.items.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);

    FeatureLayerImporter importer(src, m_modelLayer, m_canvas,
                                  m_mappingModel->mapping());
    const ImportPlan result = importer.execute(
        plan, [&progress](int done, int total) -> bool {
            progress.setMaximum(total);
            progress.setValue(done);
            return !progress.wasCanceled();
        });
    progress.setValue(progress.maximum());

    m_previewModel->setPlan(result, /*resultMode*/ true);
    m_summaryLabel->setText(
        tr("Import finished — %1  (one undo step reverts everything)")
            .arg(m_previewModel->summaryText()));
    m_planIsCurrent = false;
    updateActionEnablement();
}

// ===========================================================================
// Presets
// ===========================================================================

QString ImportFeatureLayerDialog::presetSettingsGroup() const
{
    return QStringLiteral("ImportPresets/kind%1")
        .arg(static_cast<int>(currentKind()));
}

void ImportFeatureLayerDialog::refreshPresetCombo()
{
    QSignalBlocker block(m_presetCombo);
    m_presetCombo->clear();
    m_presetCombo->addItem(tr("(none)"));
    QSettings s;
    s.beginGroup(presetSettingsGroup());
    m_presetCombo->addItems(s.childKeys());
    s.endGroup();
}

void ImportFeatureLayerDialog::onSavePreset()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Save Mapping Preset"), tr("Preset name:"),
        QLineEdit::Normal,
        m_presetCombo->currentIndex() > 0 ? m_presetCombo->currentText()
                                          : QString(),
        &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    syncOptionsIntoMapping();
    const QJsonDocument doc(m_mappingModel->mapping().toJson());
    QSettings s;
    s.beginGroup(presetSettingsGroup());
    s.setValue(name, QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    s.endGroup();

    refreshPresetCombo();
    m_presetCombo->setCurrentText(name);
}

void ImportFeatureLayerDialog::onDeletePreset()
{
    if (m_presetCombo->currentIndex() <= 0) return;
    const QString name = m_presetCombo->currentText();
    if (QMessageBox::question(
            this, tr("Delete Preset"),
            tr("Delete mapping preset \"%1\"?").arg(name))
        != QMessageBox::Yes)
        return;
    QSettings s;
    s.beginGroup(presetSettingsGroup());
    s.remove(name);
    s.endGroup();
    refreshPresetCombo();
}

void ImportFeatureLayerDialog::onLoadPreset(int comboIndex)
{
    if (comboIndex <= 0) return;
    const QString name = m_presetCombo->itemText(comboIndex);
    QSettings s;
    s.beginGroup(presetSettingsGroup());
    const QString jsonStr = s.value(name).toString();
    s.endGroup();
    if (jsonStr.isEmpty()) return;

    QString error;
    const auto parsed = ImportMapping::fromJson(
        QJsonDocument::fromJson(jsonStr.toUtf8()).object(), &error);
    if (!parsed) {
        QMessageBox::warning(this, tr("Load Preset"),
                             tr("Preset \"%1\" could not be read: %2")
                                 .arg(name, error));
        return;
    }

    // Bindings by field name; fields absent from the current layer drop.
    QStringList dropped;
    m_mappingModel->applyBindings(parsed->bindings, &dropped);

    // Option flags.
    m_epFields->setChecked(parsed->endpointsFromFields);
    m_epSnap->setChecked(parsed->endpointsSnap);
    m_epTolerance->setValue(parsed->snapToleranceMapUnits);
    m_epAutoCreate->setChecked(parsed->autoCreateJunctions);
    m_epPrefix->setText(parsed->autoNodePrefix);
    (parsed->conflict == ImportMapping::Conflict::Update
         ? m_conflictUpdate : m_conflictSkip)->setChecked(true);
    m_updateAttrs->setChecked(parsed->updateAttributes);
    m_updateGeom->setChecked(parsed->updateGeometry);
    m_selectedOnly->setChecked(parsed->selectedFeaturesOnly);

    if (!dropped.isEmpty())
        m_summaryLabel->setText(
            tr("Preset loaded; %1 column(s) not present in this layer "
               "were unmapped: %2")
                .arg(dropped.size())
                .arg(dropped.join(QStringLiteral(", "))));

    m_planIsCurrent = false;
    updateActionEnablement();
}

} // namespace openswmmvis::import
