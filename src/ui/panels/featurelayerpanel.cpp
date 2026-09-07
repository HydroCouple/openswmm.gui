/*!
 * \file   featurelayerpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/panels/featurelayerpanel.h"

#include "feature/featuretypes.h"
#include "layers/featurelayer.h"
#include "layers/gisrasterlayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/featurecommands.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "ui/uiscrollhelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace openswmmvis::feature;
using openswmmvis::map::AddFieldCommand;
using openswmmvis::map::RemoveFieldCommand;
using openswmmvis::map::ResampleZCommand;

namespace openswmmvis::ui {

namespace {

enum FieldColumn { ColName = 0, ColType = 1, ColDescription = 2, ColCount };

}   // namespace

FeatureLayerPanel::FeatureLayerPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("FeatureLayerPanel"));
    buildUi();
    refreshDetails();
}

void FeatureLayerPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    auto *page = new QWidget(this);
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(6, 6, 6, 6);

    // ----- Layers --------------------------------------------------------
    {
        auto *g = new QGroupBox(tr("Feature layers"), page);
        auto *lay = new QVBoxLayout(g);

        m_layerList = new QListWidget(g);
        m_layerList->setObjectName(QStringLiteral("featureLayerList"));
        m_layerList->setToolTip(
            tr("The selected layer is the one the drawing and editing tools "
               "write into."));
        m_layerList->setMinimumHeight(90);
        lay->addWidget(m_layerList);

        auto *btns = new QHBoxLayout();
        m_newBtn    = new QPushButton(tr("New…"),    g);
        m_importBtn = new QPushButton(tr("Import…"), g);
        m_exportBtn = new QPushButton(tr("Export…"), g);
        m_removeBtn = new QPushButton(tr("Remove"),  g);
        m_importBtn->setToolTip(tr("Copy an existing vector layer or a SWMM "
                                   "object class into this editable layer."));
        m_removeBtn->setToolTip(tr("Remove the layer from the map. The table "
                                   "stays in the project GeoPackage."));
        btns->addWidget(m_newBtn);
        btns->addWidget(m_importBtn);
        btns->addWidget(m_exportBtn);
        btns->addWidget(m_removeBtn);
        btns->addStretch();
        lay->addLayout(btns);

        vbox->addWidget(g);
    }

    // ----- Schema --------------------------------------------------------
    {
        auto *g = new QGroupBox(tr("Attributes"), page);
        auto *lay = new QVBoxLayout(g);

        m_fieldTable = new QTableWidget(0, ColCount, g);
        m_fieldTable->setObjectName(QStringLiteral("featureSchemaTable"));
        m_fieldTable->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("Description")});
        m_fieldTable->horizontalHeader()->setStretchLastSection(true);
        m_fieldTable->verticalHeader()->setVisible(false);
        m_fieldTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_fieldTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_fieldTable->setMinimumHeight(110);
        lay->addWidget(m_fieldTable);

        auto *btns = new QHBoxLayout();
        m_addFieldBtn    = new QPushButton(tr("Add column…"), g);
        m_removeFieldBtn = new QPushButton(tr("Remove column"), g);
        btns->addWidget(m_addFieldBtn);
        btns->addWidget(m_removeFieldBtn);
        btns->addStretch();
        lay->addLayout(btns);

        vbox->addWidget(g);
    }

    // ----- Z -------------------------------------------------------------
    {
        auto *g = new QGroupBox(tr("Elevation (Z)"), page);
        auto *f = new QFormLayout(g);

        m_zSourceCombo = new QComboBox(g);
        m_zSourceCombo->addItem(tr("None — 2D layer"),  static_cast<int>(ZPolicy::Source::None));
        m_zSourceCombo->addItem(tr("Constant value"),   static_cast<int>(ZPolicy::Source::Constant));
        m_zSourceCombo->addItem(tr("Raster (DEM)"),     static_cast<int>(ZPolicy::Source::Raster));
        m_zSourceCombo->addItem(tr("2D mesh"),          static_cast<int>(ZPolicy::Source::Mesh));
        f->addRow(tr("Source:"), m_zSourceCombo);

        m_zLayerCombo = new QComboBox(g);
        f->addRow(tr("From layer:"), m_zLayerCombo);

        m_zBandSpin = new QSpinBox(g);
        m_zBandSpin->setRange(1, 512);
        f->addRow(tr("Raster band:"), m_zBandSpin);

        m_zConstantSpin = new QDoubleSpinBox(g);
        m_zConstantSpin->setRange(-1e9, 1e9);
        m_zConstantSpin->setDecimals(3);
        f->addRow(tr("Value:"), m_zConstantSpin);

        m_zScaleSpin = new QDoubleSpinBox(g);
        m_zScaleSpin->setRange(-1e6, 1e6);
        m_zScaleSpin->setDecimals(6);
        f->addRow(tr("Z conversion (×):"), m_zScaleSpin);

        m_zDensifySpin = new QDoubleSpinBox(g);
        m_zDensifySpin->setRange(0.0, 1e9);
        m_zDensifySpin->setDecimals(3);
        m_zDensifySpin->setSpecialValueText(tr("(off)"));
        f->addRow(tr("Densify before sampling:"), m_zDensifySpin);

        m_zResampleBox = new QCheckBox(tr("Re-sample when a vertex moves"), g);
        f->addRow(QString(), m_zResampleBox);

        m_resampleBtn = new QPushButton(tr("Resample Z now"), g);
        m_resampleBtn->setToolTip(
            tr("Re-sample every feature. One undo step reverts the whole "
               "resample — a partial revert would leave the layer with mixed "
               "vintages of terrain."));
        f->addRow(QString(), m_resampleBtn);

        vbox->addWidget(g);
    }

    m_statusLabel = new QLabel(page);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setEnabled(false);
    vbox->addWidget(m_statusLabel);

    vbox->addStretch();
    root->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, this), 1);

    connect(m_layerList, &QListWidget::currentRowChanged,
            this, &FeatureLayerPanel::onLayerRowChanged);
    connect(m_newBtn,    &QPushButton::clicked,
            this, [this] { emit newLayerRequested(); });
    connect(m_importBtn, &QPushButton::clicked,
            this, [this] { emit importRequested(m_active.data()); });
    connect(m_exportBtn, &QPushButton::clicked,
            this, [this] { emit exportRequested(m_active.data()); });
    connect(m_removeBtn, &QPushButton::clicked,
            this, &FeatureLayerPanel::onRemoveLayer);
    connect(m_addFieldBtn,    &QPushButton::clicked,
            this, &FeatureLayerPanel::onAddField);
    connect(m_removeFieldBtn, &QPushButton::clicked,
            this, &FeatureLayerPanel::onRemoveField);
    connect(m_resampleBtn,    &QPushButton::clicked,
            this, &FeatureLayerPanel::onResampleZ);

    for (QWidget *w : {static_cast<QWidget *>(m_zSourceCombo),
                       static_cast<QWidget *>(m_zLayerCombo)}) {
        connect(qobject_cast<QComboBox *>(w), qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) { onZPolicyEdited(); });
    }
    connect(m_zBandSpin,     qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int)    { onZPolicyEdited(); });
    for (QDoubleSpinBox *s : {m_zConstantSpin, m_zScaleSpin, m_zDensifySpin})
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double) { onZPolicyEdited(); });
    connect(m_zResampleBox, &QCheckBox::toggled,
            this, [this](bool) { onZPolicyEdited(); });
}

// ---------------------------------------------------------------------------
// Canvas binding
// ---------------------------------------------------------------------------

void FeatureLayerPanel::setCanvas(MapCanvas *canvas)
{
    if (m_canvas == canvas) return;

    if (m_canvas) disconnect(m_canvas.data(), nullptr, this, nullptr);
    m_canvas = canvas;

    if (m_canvas) {
        // The list must follow the canvas, or a layer added from the ribbon
        // would not appear here until something else forced a refresh.
        connect(m_canvas.data(), &MapCanvas::layerAdded,
                this, [this](OpenSWMMVisLayer *) { refreshLayerList(); });
        connect(m_canvas.data(), &MapCanvas::layerRemoved,
                this, [this](OpenSWMMVisLayer *) { refreshLayerList(); });
    }
    refreshLayerList();
}

void FeatureLayerPanel::refreshLayerList()
{
    const QString keepId = m_active ? m_active->layerId() : QString();

    m_layerList->blockSignals(true);
    m_layerList->clear();

    if (m_canvas) {
        for (OpenSWMMVisLayer *l : m_canvas->layers()) {
            auto *fl = qobject_cast<FeatureLayer *>(l);
            if (!fl) continue;
            auto *item = new QListWidgetItem(
                QStringLiteral("%1  —  %2").arg(fl->name(),
                                                featureLayerRoleLabel(fl->role())));
            item->setData(Qt::UserRole, fl->layerId());
            m_layerList->addItem(item);
        }
    }
    m_layerList->blockSignals(false);

    // Restore the previous selection when the layer survived.
    int row = -1;
    for (int i = 0; i < m_layerList->count(); ++i) {
        if (m_layerList->item(i)->data(Qt::UserRole).toString() == keepId) { row = i; break; }
    }
    if (row < 0 && m_layerList->count() > 0) row = 0;
    m_layerList->setCurrentRow(row);
    if (row < 0) onLayerRowChanged(-1);
}

void FeatureLayerPanel::selectLayer(FeatureLayer *layer)
{
    if (!layer) return;
    for (int i = 0; i < m_layerList->count(); ++i) {
        if (m_layerList->item(i)->data(Qt::UserRole).toString() == layer->layerId()) {
            m_layerList->setCurrentRow(i);
            return;
        }
    }
}

void FeatureLayerPanel::onLayerRowChanged(int row)
{
    FeatureLayer *next = nullptr;
    if (row >= 0 && row < m_layerList->count() && m_canvas) {
        const QString id = m_layerList->item(row)->data(Qt::UserRole).toString();
        for (OpenSWMMVisLayer *l : m_canvas->layers())
            if (l && l->layerId() == id) { next = qobject_cast<FeatureLayer *>(l); break; }
    }
    if (next == m_active.data()) { refreshDetails(); return; }

    bindActiveLayer(next);
    refreshDetails();
    emit activeLayerChanged(next);
}

void FeatureLayerPanel::bindActiveLayer(FeatureLayer *layer)
{
    if (m_active) disconnect(m_active.data(), nullptr, this, nullptr);
    m_active = layer;
    if (!m_active) return;

    connect(m_active.data(), &FeatureLayer::schemaChanged,
            this, &FeatureLayerPanel::refreshDetails);
    connect(m_active.data(), &FeatureLayer::zPolicyChanged,
            this, &FeatureLayerPanel::refreshDetails);
    // Feature writes only change the counters, not the controls.
    connect(m_active.data(), &FeatureLayer::featuresChanged,
            this, [this](const QVector<qint64> &) { refreshStatus(); });
}

// ---------------------------------------------------------------------------
// Details
// ---------------------------------------------------------------------------

void FeatureLayerPanel::populateZSourceLayers()
{
    m_zLayerCombo->clear();
    if (!m_canvas || !m_active) return;

    const auto src = static_cast<ZPolicy::Source>(m_zSourceCombo->currentData().toInt());
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (!l) continue;
        const bool wanted =
            (src == ZPolicy::Source::Raster && qobject_cast<GISRasterLayer *>(l))
         || (src == ZPolicy::Source::Mesh   && qobject_cast<SWMM2DMeshLayer *>(l));
        if (wanted) m_zLayerCombo->addItem(l->name(), l->layerId());
    }
}

void FeatureLayerPanel::refreshDetails()
{
    FeatureLayer *l = m_active.data();
    const bool has = (l != nullptr);

    m_fieldTable->setEnabled(has);
    m_addFieldBtn->setEnabled(has);
    m_removeFieldBtn->setEnabled(has);
    m_exportBtn->setEnabled(has);
    m_importBtn->setEnabled(has);
    m_removeBtn->setEnabled(has);

    // Schema table
    m_fieldTable->setRowCount(0);
    if (has) {
        const Schema s = l->schema();
        for (const FieldDef &f : s.fields()) {
            const int row = m_fieldTable->rowCount();
            m_fieldTable->insertRow(row);
            m_fieldTable->setItem(row, ColName,        new QTableWidgetItem(f.name));
            m_fieldTable->setItem(row, ColType,        new QTableWidgetItem(fieldTypeLabel(f.type)));
            m_fieldTable->setItem(row, ColDescription, new QTableWidgetItem(f.description));
        }
    }

    // Z controls
    m_suppressZEdits = true;
    const ZPolicy p = has ? l->zPolicy() : ZPolicy{};
    const int srcIdx = m_zSourceCombo->findData(static_cast<int>(p.source));
    if (srcIdx >= 0) m_zSourceCombo->setCurrentIndex(srcIdx);
    populateZSourceLayers();
    const int layerIdx = m_zLayerCombo->findData(p.sourceLayerId);
    if (layerIdx >= 0) m_zLayerCombo->setCurrentIndex(layerIdx);
    m_zBandSpin->setValue(p.rasterBand);
    m_zConstantSpin->setValue(p.constant);
    m_zScaleSpin->setValue(p.zScale);
    m_zDensifySpin->setValue(p.densifySpacing);
    m_zResampleBox->setChecked(p.resampleOnEdit);
    m_suppressZEdits = false;

    const bool is3D = has && l->isThreeD();
    // The 2D/3D dimension is fixed at creation, so the source combo is
    // read-only for a 2D layer rather than offering a change that cannot be
    // honoured.
    m_zSourceCombo->setEnabled(is3D);
    m_zLayerCombo->setEnabled(is3D && (p.source == ZPolicy::Source::Raster
                                    || p.source == ZPolicy::Source::Mesh));
    m_zBandSpin->setEnabled(is3D && p.source == ZPolicy::Source::Raster);
    m_zConstantSpin->setEnabled(is3D && p.source == ZPolicy::Source::Constant);
    m_zScaleSpin->setEnabled(is3D);
    m_zDensifySpin->setEnabled(is3D);
    m_zResampleBox->setEnabled(is3D);
    m_resampleBtn->setEnabled(is3D);

    refreshStatus();
}

void FeatureLayerPanel::refreshStatus()
{
    FeatureLayer *l = m_active.data();
    if (!l) {
        m_statusLabel->setText(tr("No feature layer selected."));
        return;
    }

    const int n = l->count();
    QString text = tr("%n feature(s)", nullptr, n);
    if (l->isThreeD()) {
        const int unsampled = l->unsampledZCount();
        text += unsampled > 0
                    ? tr(" · %n vertex(es) with no elevation", nullptr, unsampled)
                    : tr(" · all vertices sampled");
    }
    text += tr(" · %1").arg(geometryTypeLabel(l->geometryType()));
    m_statusLabel->setText(text);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void FeatureLayerPanel::onAddField()
{
    FeatureLayer *l = m_active.data();
    if (!l || !m_canvas) return;

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Add column"), tr("Column name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    const QStringList typeLabels = {fieldTypeLabel(FieldType::Text),
                                    fieldTypeLabel(FieldType::Integer),
                                    fieldTypeLabel(FieldType::Real),
                                    fieldTypeLabel(FieldType::Boolean)};
    const QString chosen = QInputDialog::getItem(
        this, tr("Add column"), tr("Type:"), typeLabels, 0, false, &ok);
    if (!ok) return;

    FieldDef f;
    f.name = sanitizeFieldName(name);
    if (f.name.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid column name"),
            tr("\"%1\" cannot be used as a column name. Use letters, digits "
               "and underscores.").arg(name));
        return;
    }
    f.type = static_cast<FieldType>(typeLabels.indexOf(chosen));

    auto *cmd = new AddFieldCommand(l, f, m_canvas.data());
    if (m_canvas->undoStack()) m_canvas->undoStack()->push(cmd);
    else                       { delete cmd; return; }
    if (!cmd->lastError().isEmpty()) emit message(cmd->lastError());
}

void FeatureLayerPanel::onRemoveField()
{
    FeatureLayer *l = m_active.data();
    if (!l || !m_canvas) return;
    const int row = m_fieldTable->currentRow();
    if (row < 0) return;
    const QTableWidgetItem *item = m_fieldTable->item(row, ColName);
    if (!item) return;

    const QString name = item->text();
    const auto answer = QMessageBox::question(
        this, tr("Remove column"),
        tr("Remove the column \"%1\" and its values from every feature?").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    auto *cmd = new RemoveFieldCommand(l, name, m_canvas.data());
    if (m_canvas->undoStack()) m_canvas->undoStack()->push(cmd);
    else                       { delete cmd; return; }
    if (!cmd->lastError().isEmpty()) emit message(cmd->lastError());
}

void FeatureLayerPanel::onResampleZ()
{
    FeatureLayer *l = m_active.data();
    if (!l || !m_canvas) return;

    auto *cmd = new ResampleZCommand(l, {}, m_canvas.data());
    if (m_canvas->undoStack()) m_canvas->undoStack()->push(cmd);
    else                       { delete cmd; return; }

    if (!cmd->lastError().isEmpty()) {
        emit message(cmd->lastError());
    } else if (cmd->unsampledCount() > 0) {
        emit message(tr("Resampled. %n vertex(es) fell outside the source and "
                        "were left without an elevation.",
                        nullptr, cmd->unsampledCount()));
    } else {
        emit message(tr("Resampled; every vertex has an elevation."));
    }
    refreshStatus();
}

void FeatureLayerPanel::onZPolicyEdited()
{
    if (m_suppressZEdits) return;
    FeatureLayer *l = m_active.data();
    if (!l) return;

    ZPolicy p = l->zPolicy();
    p.source         = static_cast<ZPolicy::Source>(m_zSourceCombo->currentData().toInt());
    p.sourceLayerId  = m_zLayerCombo->currentData().toString();
    p.rasterBand     = m_zBandSpin->value();
    p.constant       = m_zConstantSpin->value();
    p.zScale         = m_zScaleSpin->value();
    p.densifySpacing = m_zDensifySpin->value();
    p.resampleOnEdit = m_zResampleBox->isChecked();

    // Not undoable: the policy is layer configuration, not model content, and
    // it changes nothing on disk until a resample runs (which IS undoable).
    l->setZPolicy(p);
}

void FeatureLayerPanel::onRemoveLayer()
{
    FeatureLayer *l = m_active.data();
    if (!l || !m_canvas) return;

    const auto answer = QMessageBox::question(
        this, tr("Remove layer"),
        tr("Remove \"%1\" from the map?\n\nThe table stays in the project "
           "GeoPackage and can be added again.").arg(l->name()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    // takeLayer is index-based and pushes its own RemoveLayerCommand, so the
    // removal is undoable and the layer must NOT be deleted here — the command
    // owns it while it is off the canvas.
    const int index = m_canvas->layers().indexOf(static_cast<OpenSWMMVisLayer *>(l));
    if (index < 0) return;
    m_canvas->takeLayer(index, /*pushUndo=*/true);
    refreshLayerList();
}

}   // namespace openswmmvis::ui
