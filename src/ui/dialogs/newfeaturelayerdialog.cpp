/*!
 * \file   newfeaturelayerdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/dialogs/newfeaturelayerdialog.h"

#include "core/unitsystem.h"
#include "feature/featureroles.h"
#include "layers/gisrasterlayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"
#include "ui/dialogs/featurefieldeditor.h"
#include "ui/uiscrollhelpers.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

using namespace openswmmvis::feature;

namespace openswmmvis::ui {

namespace {

/*! Columns of the field table. */
enum FieldColumn { ColField = 0, ColType = 1, ColDefault = 2, ColDescription = 3,
                   ColCount };

constexpr int kCustomRole   = Qt::UserRole;       ///< bool: a custom field row
constexpr int kFieldJsonRole = Qt::UserRole + 1;  ///< QJsonObject: custom FieldDef

QString typeText(const FieldDef &f)
{
    if (f.choiceSource == ChoiceSource::Fixed) return NewFeatureLayerDialog::tr("Choice");
    return fieldTypeLabel(f.type);
}

QString lengthUnit()
{
    return UnitSystem::instance() ? UnitSystem::instance()->lengthLabel() : QString();
}

/*! The default editor for a role field: a combo for Choice / Yes-No, a spin
 *  with the unit for numbers, a line edit for text. */
QWidget *defaultEditorFor(const FieldDef &f, QWidget *parent)
{
    QWidget *w = nullptr;
    if (f.type == FieldType::Boolean) {
        auto *c = new QComboBox(parent);
        c->addItem(NewFeatureLayerDialog::tr("(none)"), QVariant());
        c->addItem(NewFeatureLayerDialog::tr("Yes"), true);
        c->addItem(NewFeatureLayerDialog::tr("No"), false);
        const int i = f.defaultValue.isValid() ? c->findData(f.defaultValue.toBool()) : 0;
        c->setCurrentIndex(i >= 0 ? i : 0);
        w = c;
    } else if (f.choiceSource == ChoiceSource::Fixed) {
        auto *c = new QComboBox(parent);
        if (!f.required) c->addItem(NewFeatureLayerDialog::tr("(none)"), QString());
        for (const FieldChoice &ch : f.choices) c->addItem(ch.displayLabel(), ch.value);
        const int i = c->findData(f.defaultValue.toString());
        c->setCurrentIndex(i >= 0 ? i : 0);
        w = c;
    } else if (f.type == FieldType::Real) {
        auto *s = new QDoubleSpinBox(parent);
        s->setRange(-1.0e12, 1.0e12);
        s->setDecimals(4);
        if (f.unit == FieldUnit::Length && !lengthUnit().isEmpty())
            s->setSuffix(QLatin1Char(' ') + lengthUnit());
        s->setValue(f.defaultValue.toDouble());
        w = s;
    } else if (f.type == FieldType::Integer) {
        auto *s = new QSpinBox(parent);
        s->setRange(-2147483647, 2147483647);
        s->setValue(f.defaultValue.toInt());
        w = s;
    } else {
        auto *e = new QLineEdit(parent);
        e->setPlaceholderText(NewFeatureLayerDialog::tr("(none)"));
        e->setText(f.defaultValue.toString());
        w = e;
    }
    w->setObjectName(QStringLiteral("newFeatureDefault_") + f.name);
    return w;
}

/*! Read a default back from \p w for a field of \p f's kind. */
QVariant defaultFrom(const QWidget *w, const FieldDef &f)
{
    if (auto *c = qobject_cast<const QComboBox *>(w)) {
        const QVariant d = c->currentData();
        if (f.type == FieldType::Boolean) return d.isValid() ? QVariant(d.toBool()) : QVariant();
        return d.toString().isEmpty() ? QVariant() : QVariant(d.toString());
    }
    if (auto *s = qobject_cast<const QDoubleSpinBox *>(w)) return s->value();
    if (auto *s = qobject_cast<const QSpinBox *>(w))       return s->value();
    if (auto *e = qobject_cast<const QLineEdit *>(w)) {
        const QString t = e->text().trimmed();
        return t.isEmpty() ? QVariant() : QVariant(t);
    }
    return f.defaultValue;
}

}   // namespace

NewFeatureLayerDialog::NewFeatureLayerDialog(MapCanvas *canvas, QWidget *parent)
    : QDialog(parent)
    , m_canvas(canvas)
{
    setWindowTitle(tr("New Feature Layer"));
    setObjectName(QStringLiteral("NewFeatureLayerDialog"));
    buildUi();
    populateZSources();
    onRoleChanged(m_roleCombo->currentIndex());
    onZSourceChanged(m_zSourceCombo->currentIndex());
    resize(640, 600);
}

void NewFeatureLayerDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    auto *page = new QWidget(this);
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(8, 8, 8, 8);

    // ----- Role, name, geometry, CRS ------------------------------------
    {
        auto *f = new QFormLayout();

        m_roleCombo = new QComboBox(page);
        m_roleCombo->setObjectName(QStringLiteral("newFeatureRoleCombo"));
        for (const FeatureRoleSpec &s : featureRoleSpecs())
            m_roleCombo->addItem(s.label, static_cast<int>(s.role));
        m_roleCombo->setToolTip(
            tr("What the layer is for. The role decides the fields and geometry "
               "offered below; the layer stays a plain table, so anything that "
               "accepts a vector layer accepts it."));
        f->addRow(tr("What is it &for?"), m_roleCombo);

        m_roleSummary = new QLabel(page);
        m_roleSummary->setWordWrap(true);
        m_roleSummary->setEnabled(false);
        f->addRow(QString(), m_roleSummary);
        m_roleNote = new QLabel(page);
        m_roleNote->setWordWrap(true);
        m_roleNote->setObjectName(QStringLiteral("newFeatureRoleNote"));
        f->addRow(QString(), m_roleNote);

        m_nameEdit = new QLineEdit(page);
        m_nameEdit->setObjectName(QStringLiteral("newFeatureNameEdit"));
        m_nameEdit->setPlaceholderText(tr("e.g. Regions"));
        f->addRow(tr("&Name:"), m_nameEdit);

        m_geomBox = new QWidget(page);
        auto *gl = new QHBoxLayout(m_geomBox);
        gl->setContentsMargins(0, 0, 0, 0);
        m_geomGroup = new QButtonGroup(this);
        m_geomBox->setToolTip(
            tr("Fixed once the layer exists: it decides the table's geometry "
               "type. Choose a multi-part type if features may have several "
               "parts — a single-part layer cannot be promoted later."));
        f->addRow(tr("Geometry:"), m_geomBox);

        m_crsLabel = new QLabel(page);
        m_crsLabel->setWordWrap(true);
        m_crsLabel->setText(srsWkt().isEmpty()
                                ? tr("None — coordinates are stored as drawn.")
                                : tr("Canvas CRS"));
        f->addRow(tr("CRS:"), m_crsLabel);
        vbox->addLayout(f);
    }

    // ----- Fields ---------------------------------------------------------
    {
        auto *g = new QGroupBox(tr("Fields"), page);
        auto *lay = new QVBoxLayout(g);

        m_fieldTable = new QTableWidget(0, ColCount, g);
        m_fieldTable->setObjectName(QStringLiteral("newFeatureFieldTable"));
        m_fieldTable->setHorizontalHeaderLabels(
            {tr("Field"), tr("Type"), tr("Default"), tr("Description")});
        m_fieldTable->horizontalHeader()->setStretchLastSection(true);
        m_fieldTable->verticalHeader()->setVisible(false);
        m_fieldTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_fieldTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_fieldTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_fieldTable->setMinimumHeight(160);
        lay->addWidget(m_fieldTable);

        auto *btns = new QHBoxLayout();
        m_addFieldBtn    = new QPushButton(tr("Add custom field…"), g);
        m_removeFieldBtn = new QPushButton(tr("Remove custom field"), g);
        m_addFieldBtn->setObjectName(QStringLiteral("newFeatureAddCustomField"));
        btns->addWidget(m_addFieldBtn);
        btns->addWidget(m_removeFieldBtn);
        btns->addStretch();
        lay->addLayout(btns);
        vbox->addWidget(g);
    }

    // ----- Elevation (collapsible) ---------------------------------------
    {
        m_zToggle = new QToolButton(page);
        m_zToggle->setObjectName(QStringLiteral("newFeatureZToggle"));
        m_zToggle->setCheckable(true);
        m_zToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_zToggle->setAutoRaise(true);
        vbox->addWidget(m_zToggle);

        m_zBody = new QWidget(page);
        auto *f = new QFormLayout(m_zBody);

        m_zSourceCombo = new QComboBox(m_zBody);
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

        m_zLayerCombo = new QComboBox(m_zBody);
        f->addRow(tr("From &layer:"), m_zLayerCombo);

        m_zBandSpin = new QSpinBox(m_zBody);
        m_zBandSpin->setRange(1, 512);
        f->addRow(tr("Raster &band:"), m_zBandSpin);

        m_zConstantSpin = new QDoubleSpinBox(m_zBody);
        m_zConstantSpin->setRange(-1e9, 1e9);
        m_zConstantSpin->setDecimals(3);
        f->addRow(tr("&Value:"), m_zConstantSpin);

        m_zScaleSpin = new QDoubleSpinBox(m_zBody);
        m_zScaleSpin->setRange(-1e6, 1e6);
        m_zScaleSpin->setDecimals(6);
        m_zScaleSpin->setValue(1.0);
        m_zScaleSpin->setToolTip(
            tr("Multiplier applied to every sample — use it to convert the "
               "source's vertical unit to the model's (0.3048 for feet to "
               "metres)."));
        f->addRow(tr("Z &conversion (×):"), m_zScaleSpin);

        m_zDensifySpin = new QDoubleSpinBox(m_zBody);
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

        m_zResampleBox = new QCheckBox(tr("Re-sample Z when a vertex is moved or inserted"),
                                       m_zBody);
        m_zResampleBox->setChecked(true);
        f->addRow(QString(), m_zResampleBox);

        m_zHint = new QLabel(m_zBody);
        m_zHint->setWordWrap(true);
        m_zHint->setEnabled(false);
        f->addRow(QString(), m_zHint);

        vbox->addWidget(m_zBody);
    }

    vbox->addStretch();
    root->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, this), 1);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    root->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted,
            this, &NewFeatureLayerDialog::validateAndAccept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_roleCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &NewFeatureLayerDialog::onRoleChanged);
    connect(m_zSourceCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &NewFeatureLayerDialog::onZSourceChanged);
    connect(m_zToggle, &QToolButton::toggled, this, &NewFeatureLayerDialog::setZExpanded);
    connect(m_addFieldBtn, &QPushButton::clicked,
            this, &NewFeatureLayerDialog::onAddCustomField);
    connect(m_removeFieldBtn, &QPushButton::clicked,
            this, &NewFeatureLayerDialog::onRemoveCustomField);
}

// ---------------------------------------------------------------------------
// Role
// ---------------------------------------------------------------------------

void NewFeatureLayerDialog::onRoleChanged(int)
{
    const FeatureRoleSpec &spec = featureRoleSpec(role());
    m_roleSummary->setText(spec.summary);
    m_roleNote->setText(spec.note);
    m_roleNote->setVisible(!spec.note.isEmpty());

    rebuildGeometryChoices();
    rebuildFieldRows();

    // A breakline layer is only useful in 3D; nudge, don't force.
    if (spec.role == FeatureLayerRole::Breakline
        && m_zSourceCombo->currentData().toInt()
               == static_cast<int>(ZPolicy::Source::None)) {
        const int idx = m_zSourceCombo->findData(static_cast<int>(ZPolicy::Source::Raster));
        if (idx >= 0) m_zSourceCombo->setCurrentIndex(idx);
    }
    setZExpanded(spec.wantsZ);
}

void NewFeatureLayerDialog::setRole(FeatureLayerRole r)
{
    const int i = m_roleCombo->findData(static_cast<int>(r));
    if (i >= 0) m_roleCombo->setCurrentIndex(i);
}

FeatureLayerRole NewFeatureLayerDialog::role() const
{
    return static_cast<FeatureLayerRole>(m_roleCombo->currentData().toInt());
}

void NewFeatureLayerDialog::rebuildGeometryChoices()
{
    const GeometryType previous = geometryType();
    for (QAbstractButton *b : m_geomGroup->buttons()) {
        m_geomGroup->removeButton(b);
        delete b;
    }

    const FeatureRoleSpec &spec = featureRoleSpec(role());
    auto *lay = static_cast<QHBoxLayout *>(m_geomBox->layout());
    while (QLayoutItem *item = lay->takeAt(0)) delete item;   // the old stretch
    for (GeometryType t : spec.geometries) {
        auto *rb = new QRadioButton(geometryTypeLabel(t), m_geomBox);
        rb->setObjectName(QStringLiteral("newFeatureGeometry_%1").arg(static_cast<int>(t)));
        m_geomGroup->addButton(rb, static_cast<int>(t));
        lay->addWidget(rb);
    }
    lay->addStretch();

    // The role's default; otherwise keep what was picked when the role allows
    // it; otherwise Polygon, then the first kind.
    GeometryType pick = spec.defaultGeometry;
    if (pick == GeometryType::None || !spec.allowsGeometry(pick))
        pick = spec.allowsGeometry(previous) ? previous
             : spec.allowsGeometry(GeometryType::Polygon) ? GeometryType::Polygon
             : spec.geometries.value(0, GeometryType::None);
    setGeometryType(pick);
}

QVector<GeometryType> NewFeatureLayerDialog::offeredGeometries() const
{
    QVector<GeometryType> out;
    for (QAbstractButton *b : m_geomGroup->buttons())
        out.append(static_cast<GeometryType>(m_geomGroup->id(b)));
    return out;
}

void NewFeatureLayerDialog::setGeometryType(GeometryType t)
{
    if (QAbstractButton *b = m_geomGroup->button(static_cast<int>(t)))
        b->setChecked(true);
}

GeometryType NewFeatureLayerDialog::geometryType() const
{
    const int id = m_geomGroup ? m_geomGroup->checkedId() : -1;
    return id < 0 ? GeometryType::None : static_cast<GeometryType>(id);
}

// ---------------------------------------------------------------------------
// Fields
// ---------------------------------------------------------------------------

void NewFeatureLayerDialog::rebuildFieldRows()
{
    // Keep the custom rows; replace the role rows.
    QVector<QPair<FieldDef, bool>> custom;   // field, ticked
    for (int r = 0; r < m_fieldTable->rowCount(); ++r) {
        const QTableWidgetItem *it = m_fieldTable->item(r, ColField);
        if (!it || !it->data(kCustomRole).toBool()) continue;
        custom.append({FieldDef::fromJson(it->data(kFieldJsonRole).toJsonObject()),
                       it->checkState() == Qt::Checked});
    }
    m_fieldTable->setRowCount(0);

    for (const FieldDef &f : featureRoleSpec(role()).fields)
        appendFieldRow(f, /*custom=*/false);
    for (const auto &c : custom) {
        // A custom field whose name the new role uses would be a duplicate.
        if (rowOf(c.first.name) >= 0) continue;
        appendFieldRow(c.first, /*custom=*/true);
        m_fieldTable->item(m_fieldTable->rowCount() - 1, ColField)
            ->setCheckState(c.second ? Qt::Checked : Qt::Unchecked);
    }
    m_fieldTable->resizeColumnsToContents();
    m_fieldTable->horizontalHeader()->setStretchLastSection(true);
}

void NewFeatureLayerDialog::appendFieldRow(const FieldDef &f, bool custom)
{
    const int row = m_fieldTable->rowCount();
    m_fieldTable->insertRow(row);

    auto *name = new QTableWidgetItem(f.name);
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (custom || !f.required) flags |= Qt::ItemIsUserCheckable;
    name->setFlags(flags);
    name->setCheckState(Qt::Checked);
    name->setData(kCustomRole, custom);
    if (custom) name->setData(kFieldJsonRole, f.toJson());
    name->setToolTip(f.required ? tr("Required by this role — always created.")
                     : custom   ? tr("Custom field. Untick to leave it out.")
                                : tr("Optional. Untick to leave it out."));
    m_fieldTable->setItem(row, ColField, name);

    auto *type = new QTableWidgetItem(typeText(f));
    type->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    m_fieldTable->setItem(row, ColType, type);

    if (custom) {
        QString d = f.defaultValue.isValid() ? f.defaultValue.toString() : QString();
        if (f.choiceSource == ChoiceSource::Fixed && f.defaultValue.isValid())
            d = f.choiceLabel(f.defaultValue.toString());
        if (f.type == FieldType::Boolean && f.defaultValue.isValid())
            d = f.defaultValue.toBool() ? tr("Yes") : tr("No");
        auto *def = new QTableWidgetItem(d);
        def->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_fieldTable->setItem(row, ColDefault, def);
    } else {
        m_fieldTable->setCellWidget(row, ColDefault, defaultEditorFor(f, m_fieldTable));
    }

    auto *desc = new QTableWidgetItem(f.description);
    desc->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    desc->setToolTip(f.description);
    m_fieldTable->setItem(row, ColDescription, desc);
}

int NewFeatureLayerDialog::rowOf(const QString &name) const
{
    for (int r = 0; r < m_fieldTable->rowCount(); ++r)
        if (const QTableWidgetItem *it = m_fieldTable->item(r, ColField))
            if (it->text().compare(name, Qt::CaseInsensitive) == 0) return r;
    return -1;
}

QStringList NewFeatureLayerDialog::fieldNames() const
{
    QStringList out;
    for (int r = 0; r < m_fieldTable->rowCount(); ++r)
        if (const QTableWidgetItem *it = m_fieldTable->item(r, ColField))
            out << it->text();
    return out;
}

bool NewFeatureLayerDialog::setFieldIncluded(const QString &name, bool on)
{
    const int r = rowOf(name);
    if (r < 0) return false;
    QTableWidgetItem *it = m_fieldTable->item(r, ColField);
    if (it->flags().testFlag(Qt::ItemIsUserCheckable))
        it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    return it->checkState() == Qt::Checked;
}

bool NewFeatureLayerDialog::addCustomField(const FieldDef &f)
{
    if (!FeatureFieldEditor::validate(f, f.name, fieldNames()).isEmpty())
        return false;
    appendFieldRow(f, /*custom=*/true);
    return true;
}

void NewFeatureLayerDialog::onAddCustomField()
{
    FeatureFieldEditor editor(this);
    editor.setWindowTitle(tr("Add custom field"));
    editor.setExistingNames(fieldNames());
    if (editor.exec() != QDialog::Accepted) return;
    addCustomField(editor.field());
}

void NewFeatureLayerDialog::onRemoveCustomField()
{
    const int r = m_fieldTable->currentRow();
    if (r < 0) return;
    const QTableWidgetItem *it = m_fieldTable->item(r, ColField);
    if (it && it->data(kCustomRole).toBool()) m_fieldTable->removeRow(r);
}

Schema NewFeatureLayerDialog::schema() const
{
    Schema s;
    const FeatureRoleSpec &spec = featureRoleSpec(role());
    for (int r = 0; r < m_fieldTable->rowCount(); ++r) {
        const QTableWidgetItem *it = m_fieldTable->item(r, ColField);
        if (!it || it->checkState() != Qt::Checked) continue;
        if (it->data(kCustomRole).toBool()) {
            s.append(FieldDef::fromJson(it->data(kFieldJsonRole).toJsonObject()));
            continue;
        }
        const FieldDef *reg = spec.field(it->text());
        if (!reg) continue;
        FieldDef f = *reg;
        f.defaultValue = defaultFrom(m_fieldTable->cellWidget(r, ColDefault), f);
        s.append(f);
    }
    return s;
}

// ---------------------------------------------------------------------------
// Elevation
// ---------------------------------------------------------------------------

void NewFeatureLayerDialog::setZExpanded(bool on)
{
    if (m_zToggle->isChecked() != on) {
        const QSignalBlocker block(m_zToggle);
        m_zToggle->setChecked(on);
    }
    m_zToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    m_zBody->setVisible(on);
    refreshZSummary();
}

bool NewFeatureLayerDialog::zSectionExpanded() const
{
    return m_zToggle->isChecked();
}

void NewFeatureLayerDialog::refreshZSummary()
{
    const auto src = static_cast<ZPolicy::Source>(m_zSourceCombo->currentData().toInt());
    QString what;
    switch (src) {
    case ZPolicy::Source::None:     what = tr("2D"); break;
    case ZPolicy::Source::Constant: what = tr("3D, constant"); break;
    case ZPolicy::Source::Raster:   what = tr("3D, from a raster"); break;
    case ZPolicy::Source::Mesh:     what = tr("3D, from the 2D mesh"); break;
    }
    m_zToggle->setText(tr("Elevation (Z) — %1").arg(what));
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
    refreshZSummary();
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

// ---------------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------------

QString NewFeatureLayerDialog::layerName() const
{
    const QString n = m_nameEdit->text().trimmed();
    return n.isEmpty() ? tr("Features") : n;
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
    accept();
}

}   // namespace openswmmvis::ui
