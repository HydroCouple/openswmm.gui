/*!
 * \file   newfeaturelayerdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/dialogs/newfeaturelayerdialog.h"

#include "layers/gisrasterlayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"
#include "ui/uiscrollhelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace openswmmvis::feature;

namespace openswmmvis::ui {

namespace {

/*! Column order of the schema table. */
enum FieldColumn { ColName = 0, ColType = 1, ColDefault = 2, ColDescription = 3,
                   ColCount };

}   // namespace

NewFeatureLayerDialog::NewFeatureLayerDialog(MapCanvas *canvas, QWidget *parent)
    : QDialog(parent)
    , m_canvas(canvas)
{
    setWindowTitle(tr("New Feature Layer"));
    setObjectName(QStringLiteral("NewFeatureLayerDialog"));
    buildUi();
    populateZSources();
    applyRoleTemplate(FeatureLayerRole::General);
    onZSourceChanged(m_zSourceCombo->currentIndex());
    resize(620, 560);
}

void NewFeatureLayerDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    auto *page = new QWidget(this);
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(8, 8, 8, 8);

    // ----- Identity ------------------------------------------------------
    {
        auto *g = new QGroupBox(tr("Layer"), page);
        auto *f = new QFormLayout(g);

        m_nameEdit = new QLineEdit(g);
        m_nameEdit->setPlaceholderText(tr("e.g. Model domain"));
        f->addRow(tr("&Name:"), m_nameEdit);

        m_roleCombo = new QComboBox(g);
        for (auto r : {FeatureLayerRole::General,
                       FeatureLayerRole::DomainBoundary,
                       FeatureLayerRole::Breakline,
                       FeatureLayerRole::Region,
                       FeatureLayerRole::ParameterZone,
                       FeatureLayerRole::SwmmDelineation,
                       FeatureLayerRole::BoundaryCondition})
            m_roleCombo->addItem(featureLayerRoleLabel(r), static_cast<int>(r));
        m_roleCombo->setToolTip(
            tr("What the layer is for. A role only pre-fills the columns below "
               "and decides which \"Use as…\" shortcuts are offered — the layer "
               "is a plain table whatever you choose, so every consumer that "
               "accepts a vector layer accepts it."));
        f->addRow(tr("&Role:"), m_roleCombo);

        m_geomCombo = new QComboBox(g);
        for (auto t : {GeometryType::Point, GeometryType::LineString,
                       GeometryType::Polygon, GeometryType::MultiPoint,
                       GeometryType::MultiLineString, GeometryType::MultiPolygon})
            m_geomCombo->addItem(geometryTypeLabel(t), static_cast<int>(t));
        m_geomCombo->setCurrentIndex(m_geomCombo->findData(
            static_cast<int>(GeometryType::Polygon)));
        m_geomCombo->setToolTip(
            tr("Fixed once the layer exists: it decides the table's geometry "
               "type. Choose a multi-part type if features may have several "
               "parts — a single-part layer cannot be promoted later."));
        f->addRow(tr("&Geometry:"), m_geomCombo);

        m_crsLabel = new QLabel(g);
        m_crsLabel->setWordWrap(true);
        m_crsLabel->setText(srsWkt().isEmpty()
                                ? tr("None — coordinates are stored as drawn.")
                                : tr("Canvas CRS"));
        f->addRow(tr("CRS:"), m_crsLabel);

        vbox->addWidget(g);
    }

    // ----- Elevation -----------------------------------------------------
    {
        auto *g = new QGroupBox(tr("Elevation (Z)"), page);
        auto *f = new QFormLayout(g);

        m_zSourceCombo = new QComboBox(g);
        m_zSourceCombo->addItem(tr("None — 2D layer"),
                                static_cast<int>(ZPolicy::Source::None));
        m_zSourceCombo->addItem(tr("Constant value"),
                                static_cast<int>(ZPolicy::Source::Constant));
        m_zSourceCombo->addItem(tr("Sample a raster (DEM)"),
                                static_cast<int>(ZPolicy::Source::Raster));
        m_zSourceCombo->addItem(tr("Sample the 2D mesh"),
                                static_cast<int>(ZPolicy::Source::Mesh));
        m_zSourceCombo->setToolTip(
            tr("2D or 3D is fixed once the layer exists — it decides whether "
               "the table stores Z. The source itself can be changed later and "
               "followed by Resample Z."));
        f->addRow(tr("&Source:"), m_zSourceCombo);

        m_zLayerCombo = new QComboBox(g);
        f->addRow(tr("From &layer:"), m_zLayerCombo);

        m_zBandSpin = new QSpinBox(g);
        m_zBandSpin->setRange(1, 512);
        f->addRow(tr("Raster &band:"), m_zBandSpin);

        m_zConstantSpin = new QDoubleSpinBox(g);
        m_zConstantSpin->setRange(-1e9, 1e9);
        m_zConstantSpin->setDecimals(3);
        f->addRow(tr("&Value:"), m_zConstantSpin);

        m_zScaleSpin = new QDoubleSpinBox(g);
        m_zScaleSpin->setRange(-1e6, 1e6);
        m_zScaleSpin->setDecimals(6);
        m_zScaleSpin->setValue(1.0);
        m_zScaleSpin->setToolTip(
            tr("Multiplier applied to every sample — use it to convert the "
               "source's vertical unit to the model's (0.3048 for feet to "
               "metres)."));
        f->addRow(tr("Z &conversion (×):"), m_zScaleSpin);

        m_zDensifySpin = new QDoubleSpinBox(g);
        m_zDensifySpin->setRange(0.0, 1e9);
        m_zDensifySpin->setDecimals(3);
        m_zDensifySpin->setSpecialValueText(tr("(off)"));
        m_zDensifySpin->setToolTip(
            tr("Insert vertices this far apart before sampling, so a line "
               "drawn with a few clicks follows the terrain instead of cutting "
               "across it.\n\nKeep this at or above the mesh minimum cell size: "
               "the inserted vertices become constraint-segment endpoints, and "
               "sub-cell segments are exactly what the minimum-size enforcement "
               "then has to clean up."));
        f->addRow(tr("&Densify before sampling:"), m_zDensifySpin);

        m_zResampleBox = new QCheckBox(tr("Re-sample Z when a vertex is moved or inserted"), g);
        m_zResampleBox->setChecked(true);
        f->addRow(QString(), m_zResampleBox);

        m_zHint = new QLabel(g);
        m_zHint->setWordWrap(true);
        m_zHint->setEnabled(false);
        f->addRow(QString(), m_zHint);

        vbox->addWidget(g);
    }

    // ----- Schema --------------------------------------------------------
    {
        auto *g = new QGroupBox(tr("Attributes"), page);
        auto *lay = new QVBoxLayout(g);

        m_fieldTable = new QTableWidget(0, ColCount, g);
        m_fieldTable->setHorizontalHeaderLabels(
            {tr("Name"), tr("Type"), tr("Default"), tr("Description")});
        m_fieldTable->horizontalHeader()->setStretchLastSection(true);
        m_fieldTable->verticalHeader()->setVisible(false);
        m_fieldTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_fieldTable->setMinimumHeight(140);
        lay->addWidget(m_fieldTable);

        auto *btns = new QHBoxLayout();
        m_addFieldBtn    = new QPushButton(tr("Add column"), g);
        m_removeFieldBtn = new QPushButton(tr("Remove"), g);
        btns->addWidget(m_addFieldBtn);
        btns->addWidget(m_removeFieldBtn);
        btns->addStretch();
        lay->addLayout(btns);

        vbox->addWidget(g);
    }

    vbox->addStretch();
    root->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, this), 1);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted,
            this, &NewFeatureLayerDialog::validateAndAccept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_roleCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &NewFeatureLayerDialog::onRoleChanged);
    connect(m_zSourceCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &NewFeatureLayerDialog::onZSourceChanged);
    connect(m_addFieldBtn, &QPushButton::clicked,
            this, &NewFeatureLayerDialog::onAddField);
    connect(m_removeFieldBtn, &QPushButton::clicked,
            this, &NewFeatureLayerDialog::onRemoveField);
}

void NewFeatureLayerDialog::populateZSources()
{
    m_zLayerCombo->clear();
    if (!m_canvas) return;

    const ZPolicy::Source src = static_cast<ZPolicy::Source>(
        m_zSourceCombo->currentData().toInt());

    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (!l) continue;
        const bool wanted =
            (src == ZPolicy::Source::Raster && qobject_cast<GISRasterLayer *>(l))
         || (src == ZPolicy::Source::Mesh   && qobject_cast<SWMM2DMeshLayer *>(l));
        if (wanted) m_zLayerCombo->addItem(l->name(), l->layerId());
    }
}

void NewFeatureLayerDialog::onRoleChanged(int)
{
    const auto r = static_cast<FeatureLayerRole>(m_roleCombo->currentData().toInt());
    applyRoleTemplate(r);

    // Steer the geometry type to what the role implies, but do not lock it:
    // a user may legitimately want breaklines as points.
    const GeometryType g = featureLayerRoleGeometry(r);
    if (g != GeometryType::None) {
        const int idx = m_geomCombo->findData(static_cast<int>(g));
        if (idx >= 0) m_geomCombo->setCurrentIndex(idx);
    }

    // A breakline layer is only useful in 3D; nudge, don't force.
    if (r == FeatureLayerRole::Breakline
        && m_zSourceCombo->currentData().toInt()
               == static_cast<int>(ZPolicy::Source::None)) {
        const int idx = m_zSourceCombo->findData(
            static_cast<int>(ZPolicy::Source::Raster));
        if (idx >= 0) m_zSourceCombo->setCurrentIndex(idx);
    }
}

void NewFeatureLayerDialog::onZSourceChanged(int)
{
    const auto src = static_cast<ZPolicy::Source>(m_zSourceCombo->currentData().toInt());

    const bool isRaster   = src == ZPolicy::Source::Raster;
    const bool isMesh     = src == ZPolicy::Source::Mesh;
    const bool isConstant = src == ZPolicy::Source::Constant;
    const bool is3D       = src != ZPolicy::Source::None;

    m_zLayerCombo->setEnabled(isRaster || isMesh);
    m_zBandSpin->setEnabled(isRaster);
    m_zConstantSpin->setEnabled(isConstant);
    m_zScaleSpin->setEnabled(is3D);
    m_zDensifySpin->setEnabled(is3D);
    m_zResampleBox->setEnabled(is3D);

    populateZSources();

    if (!is3D) {
        m_zHint->setText(tr("The layer will store 2D coordinates."));
    } else if ((isRaster || isMesh) && m_zLayerCombo->count() == 0) {
        m_zHint->setText(tr("No layer of that kind is loaded. The table will "
                            "still be created 3D; add the source and use "
                            "Resample Z afterwards."));
    } else {
        m_zHint->setText(tr("Vertices outside the source's coverage are left "
                            "unsampled rather than set to zero, and counted in "
                            "the Features panel."));
    }
}

void NewFeatureLayerDialog::applyRoleTemplate(FeatureLayerRole r)
{
    writeSchemaTable(featureLayerRoleTemplate(r));
}

void NewFeatureLayerDialog::writeSchemaTable(const Schema &s)
{
    m_fieldTable->setRowCount(0);
    for (const FieldDef &f : s.fields()) {
        const int row = m_fieldTable->rowCount();
        m_fieldTable->insertRow(row);
        m_fieldTable->setItem(row, ColName, new QTableWidgetItem(f.name));

        auto *typeCombo = new QComboBox(m_fieldTable);
        for (auto t : {FieldType::Text, FieldType::Integer,
                       FieldType::Real, FieldType::Boolean})
            typeCombo->addItem(fieldTypeLabel(t), static_cast<int>(t));
        typeCombo->setCurrentIndex(typeCombo->findData(static_cast<int>(f.type)));
        m_fieldTable->setCellWidget(row, ColType, typeCombo);

        m_fieldTable->setItem(row, ColDefault,
                              new QTableWidgetItem(f.defaultValue.toString()));
        m_fieldTable->setItem(row, ColDescription, new QTableWidgetItem(f.description));
    }
}

Schema NewFeatureLayerDialog::readSchemaTable() const
{
    Schema s;
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        FieldDef f;
        const QTableWidgetItem *nameItem = m_fieldTable->item(row, ColName);
        f.name = sanitizeFieldName(nameItem ? nameItem->text() : QString());
        if (f.name.isEmpty()) continue;

        if (auto *combo = qobject_cast<QComboBox *>(m_fieldTable->cellWidget(row, ColType)))
            f.type = static_cast<FieldType>(combo->currentData().toInt());

        if (const QTableWidgetItem *d = m_fieldTable->item(row, ColDefault))
            if (!d->text().isEmpty())
                f.defaultValue = coerceToFieldType(d->text(), f.type);

        if (const QTableWidgetItem *desc = m_fieldTable->item(row, ColDescription))
            f.description = desc->text();

        s.append(f);   // duplicates are dropped by Schema::append
    }
    return s;
}

void NewFeatureLayerDialog::onAddField()
{
    const int row = m_fieldTable->rowCount();
    m_fieldTable->insertRow(row);
    m_fieldTable->setItem(row, ColName, new QTableWidgetItem(QString()));

    auto *typeCombo = new QComboBox(m_fieldTable);
    for (auto t : {FieldType::Text, FieldType::Integer,
                   FieldType::Real, FieldType::Boolean})
        typeCombo->addItem(fieldTypeLabel(t), static_cast<int>(t));
    m_fieldTable->setCellWidget(row, ColType, typeCombo);

    m_fieldTable->setItem(row, ColDefault, new QTableWidgetItem(QString()));
    m_fieldTable->setItem(row, ColDescription, new QTableWidgetItem(QString()));
    m_fieldTable->editItem(m_fieldTable->item(row, ColName));
}

void NewFeatureLayerDialog::onRemoveField()
{
    const int row = m_fieldTable->currentRow();
    if (row >= 0) m_fieldTable->removeRow(row);
}

QString NewFeatureLayerDialog::layerName() const
{
    const QString n = m_nameEdit->text().trimmed();
    return n.isEmpty() ? tr("Features") : n;
}

GeometryType NewFeatureLayerDialog::geometryType() const
{
    return static_cast<GeometryType>(m_geomCombo->currentData().toInt());
}

Schema NewFeatureLayerDialog::schema() const
{
    return readSchemaTable();
}

FeatureLayerRole NewFeatureLayerDialog::role() const
{
    return static_cast<FeatureLayerRole>(m_roleCombo->currentData().toInt());
}

ZPolicy NewFeatureLayerDialog::zPolicy() const
{
    ZPolicy p;
    p.source         = static_cast<ZPolicy::Source>(m_zSourceCombo->currentData().toInt());
    p.sourceLayerId  = m_zLayerCombo->currentData().toString();
    p.rasterBand     = m_zBandSpin->value();
    p.constant       = m_zConstantSpin->value();
    p.zScale         = m_zScaleSpin->value();
    p.densifySpacing = m_zDensifySpin->value();
    p.resampleOnEdit = m_zResampleBox->isChecked();
    return p;
}

QString NewFeatureLayerDialog::srsWkt() const
{
    if (!m_canvas) return {};
    const SpatialReferenceSystem *srs = m_canvas->canvasSRS();
    if (!srs) return {};
    return srs->toWkt();
}

void NewFeatureLayerDialog::validateAndAccept()
{
    if (m_nameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Name required"),
                             tr("Give the layer a name."));
        m_nameEdit->setFocus();
        return;
    }
    if (geometryType() == GeometryType::None) {
        QMessageBox::warning(this, tr("Geometry required"),
                             tr("Choose a geometry type."));
        return;
    }

    // A named column the sanitiser could not use is a silent data loss if we
    // just drop it, so say which one and stop.
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        const QTableWidgetItem *item = m_fieldTable->item(row, ColName);
        const QString raw = item ? item->text().trimmed() : QString();
        if (raw.isEmpty()) continue;
        if (sanitizeFieldName(raw).isEmpty()) {
            QMessageBox::warning(this, tr("Invalid column name"),
                tr("\"%1\" cannot be used as a column name. Use letters, "
                   "digits and underscores.").arg(raw));
            return;
        }
    }

    accept();
}

}   // namespace openswmmvis::ui
