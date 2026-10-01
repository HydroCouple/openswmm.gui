/*!
 * \file   meshattributeassigndialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/meshattributeassigndialog.h"

#include "layers/gisrasterlayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/meshcommands.h"
#include "map/spatialreferencesystem.h"
#include "mesh/dtmsampler.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshcellstats.h"
#include "mesh/meshcellparams.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"

#include <gdal_priv.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPromise>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace openswmmvis::ui {

namespace {

constexpr int kFootprintStride=4;

// ---------------------------------------------------------------------------
// Lookup-table editor column layout
// ---------------------------------------------------------------------------

constexpr int kColKey1   = 0;
constexpr int kColKey2   = 1;
constexpr int kColMethod = 2;
constexpr int kColP0     = 3;
constexpr int kColDest   = 8;
constexpr int kLookupCols = 9;

} // namespace

// ===========================================================================
// Construction / UI
// ===========================================================================

MeshAttributeAssignDialog::MeshAttributeAssignDialog(
        SWMM2DMeshLayer *meshLayer, MapCanvas *canvas,
        SelectionManager *selection, Source initialSource,
        const QString &depthUnitLabel, QWidget *parent)
    : QDialog(parent),
      m_mesh(meshLayer),
      m_canvas(canvas),
      m_selection(selection),
      m_depthUnitLabel(depthUnitLabel)
{
    setWindowTitle(tr("Assign 2D Cell Data"));
    buildUi(initialSource, depthUnitLabel);
    populateLayerCombos();
    onModeChanged();
    onSourceChanged();
    updateButtons();
}

void MeshAttributeAssignDialog::fillParamCombo(QComboBox *combo,
                                               const QString &depthUnitLabel) const
{
    for (const mesh::CellParamSpec &s : mesh::cellParamSpecs()) {
        // The infiltration METHOD is an enumeration, not a number a raster can
        // carry; it is assigned through the classified-lookup mode instead.
        if (s.kind == mesh::CellParamSpec::Kind::Enum) continue;
        combo->addItem(mesh::cellParamLabel(s.key, depthUnitLabel), QVariant(s.key));
        const int row = combo->count() - 1;
        combo->setItemData(row, s.tooltip, Qt::ToolTipRole);
        if (!s.enabled) {
            if (auto *model = qobject_cast<QStandardItemModel *>(combo->model()))
                if (QStandardItem *item = model->item(row))
                    item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
    }
}

void MeshAttributeAssignDialog::buildTargetGroup(const QString &depthUnitLabel)
{
    // ---- Single numeric target (the original control) --------------------
    m_singleGroup = new QGroupBox(tr("Target parameter"), this);
    {
        auto *form = new QFormLayout(m_singleGroup);
        m_targetCombo = new QComboBox(m_singleGroup);
        fillParamCombo(m_targetCombo, depthUnitLabel);
        connect(m_targetCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &MeshAttributeAssignDialog::onTargetChanged);
        form->addRow(tr("Assign to:"), m_targetCombo);
    }

    // ---- Multiple numeric targets ----------------------------------------
    m_multiGroup = new QGroupBox(tr("Target parameters"), this);
    {
        auto *v = new QVBoxLayout(m_multiGroup);
        m_targetTable = new QTableWidget(0, 2, m_multiGroup);
        m_targetTable->setHorizontalHeaderLabels(
            {tr("Parameter"), tr("Band / Field")});
        m_targetTable->horizontalHeader()->setStretchLastSection(true);
        m_targetTable->verticalHeader()->setVisible(false);
        m_targetTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_targetTable->setMinimumHeight(110);
        m_targetTable->setToolTip(
            tr("One row per parameter: pick the mesh parameter and the raster "
               "band (or vector field) that supplies it. All rows are written "
               "as a single undoable assignment."));
        v->addWidget(m_targetTable);

        auto *row = new QHBoxLayout;
        m_addTargetBtn = new QPushButton(tr("Add"), m_multiGroup);
        m_delTargetBtn = new QPushButton(tr("Remove"), m_multiGroup);
        connect(m_addTargetBtn, &QPushButton::clicked,
                this, &MeshAttributeAssignDialog::onAddTargetRow);
        connect(m_delTargetBtn, &QPushButton::clicked,
                this, &MeshAttributeAssignDialog::onRemoveTargetRow);
        row->addWidget(m_addTargetBtn);
        row->addWidget(m_delTargetBtn);
        row->addStretch();
        v->addLayout(row);
    }
}

void MeshAttributeAssignDialog::buildLookupGroup()
{
    m_lookupGroup = new QGroupBox(tr("Classified infiltration lookup"), this);
    auto *v = new QVBoxLayout(m_lookupGroup);

    auto *keys = new QFormLayout;
    auto *k1Row = new QHBoxLayout;
    m_key1Combo    = new QComboBox(m_lookupGroup);
    m_key1BandSpin = new QSpinBox(m_lookupGroup);
    m_key1BandSpin->setRange(1, 512);
    k1Row->addWidget(m_key1Combo, 1);
    k1Row->addWidget(m_key1BandSpin);
    m_key1Label = new QLabel(tr("Key field:"), m_lookupGroup);
    keys->addRow(m_key1Label, k1Row);

    m_twoKeyCheck = new QCheckBox(
        tr("Second key (e.g. Curve Number by landuse × hydrologic soil group)"),
        m_lookupGroup);
    connect(m_twoKeyCheck, &QCheckBox::toggled,
            this, &MeshAttributeAssignDialog::onTwoKeyToggled);
    keys->addRow(QString(), m_twoKeyCheck);

    auto *k2Row = new QHBoxLayout;
    m_key2Combo    = new QComboBox(m_lookupGroup);
    m_key2BandSpin = new QSpinBox(m_lookupGroup);
    m_key2BandSpin->setRange(1, 512);
    m_key2BandSpin->setValue(2);
    k2Row->addWidget(m_key2Combo, 1);
    k2Row->addWidget(m_key2BandSpin);
    m_key2Label = new QLabel(tr("Second key field:"), m_lookupGroup);
    keys->addRow(m_key2Label, k2Row);
    v->addLayout(keys);

    m_lookupTable = new QTableWidget(0, kLookupCols, m_lookupGroup);
    m_lookupTable->setHorizontalHeaderLabels(
        {tr("Key"), tr("Key 2"), tr("Method"),
         tr("P1"), tr("P2"), tr("P3"), tr("P4"), tr("P5"), tr("Destination")});
    m_lookupTable->verticalHeader()->setVisible(false);
    m_lookupTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_lookupTable->setMinimumHeight(150);
    m_lookupTable->setToolTip(
        tr("Each row maps a key value to a complete infiltration row: method, "
           "its positional parameters (in project units, exactly as in "
           "[INFILTRATION]) and destination. The first row is the fallback "
           "applied to keys the table does not list."));
    v->addWidget(m_lookupTable);

    auto *row = new QHBoxLayout;
    auto *addBtn  = new QPushButton(tr("Add Row"), m_lookupGroup);
    auto *delBtn  = new QPushButton(tr("Remove Row"), m_lookupGroup);
    auto *loadBtn = new QPushButton(tr("Load CSV…"), m_lookupGroup);
    auto *saveBtn = new QPushButton(tr("Save CSV…"), m_lookupGroup);
    loadBtn->setToolTip(tr("Load an agency standard lookup table."));
    connect(addBtn,  &QPushButton::clicked, this, &MeshAttributeAssignDialog::onAddLookupRow);
    connect(delBtn,  &QPushButton::clicked, this, &MeshAttributeAssignDialog::onRemoveLookupRow);
    connect(loadBtn, &QPushButton::clicked, this, &MeshAttributeAssignDialog::onLoadLookupCsv);
    connect(saveBtn, &QPushButton::clicked, this, &MeshAttributeAssignDialog::onSaveLookupCsv);
    row->addWidget(addBtn);
    row->addWidget(delBtn);
    row->addStretch();
    row->addWidget(loadBtn);
    row->addWidget(saveBtn);
    v->addLayout(row);

    // Seed with the fallback row (row 0) so the table is never empty.
    mesh::InfilLookupTable seed;
    seed.entries.append(mesh::InfilLookupEntry{});
    applyLookupTable(seed);
}

void MeshAttributeAssignDialog::buildUi(Source initialSource,
                                        const QString &depthUnitLabel)
{
    auto *outer = new QVBoxLayout(this);

    // ---- Mapping mode ----------------------------------------------------
    {
        auto *form = new QFormLayout;
        m_modeCombo = new QComboBox(this);
        m_modeCombo->addItem(tr("Single numeric target"),
                             int(Mode::SingleNumeric));
        m_modeCombo->addItem(tr("Multiple numeric targets"),
                             int(Mode::MultiNumeric));
        m_modeCombo->addItem(tr("Classified infiltration lookup"),
                             int(Mode::ClassifiedInfil));
        m_modeCombo->setToolTip(
            tr("A single numeric field cannot express an infiltration method "
               "plus its parameters — that is what the classified lookup is "
               "for."));
        connect(m_modeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &MeshAttributeAssignDialog::onModeChanged);
        form->addRow(tr("Mapping mode:"), m_modeCombo);
        outer->addLayout(form);
    }

    buildTargetGroup(depthUnitLabel);
    outer->addWidget(m_singleGroup);
    outer->addWidget(m_multiGroup);
    buildLookupGroup();
    outer->addWidget(m_lookupGroup);

    // ---- Source ----------------------------------------------------------
    auto *srcGroup = new QGroupBox(tr("Source"), this);
    auto *srcVBox  = new QVBoxLayout(srcGroup);
    auto *srcButtons = new QButtonGroup(srcGroup);

    m_srcRaster = new QRadioButton(tr("Raster"), srcGroup);
    m_srcVector = new QRadioButton(tr("Vector layer"), srcGroup);
    srcButtons->addButton(m_srcRaster);
    srcButtons->addButton(m_srcVector);
    srcVBox->addWidget(m_srcRaster);

    {
        auto *form = new QFormLayout;
        form->setContentsMargins(20, 0, 0, 0);
        auto *rasterRow = new QHBoxLayout;
        m_rasterCombo = new QComboBox(srcGroup);
        m_rasterCombo->setMinimumWidth(220);
        m_browseBtn = new QPushButton(tr("Browse…"), srcGroup);
        connect(m_browseBtn, &QPushButton::clicked,
                this, &MeshAttributeAssignDialog::onBrowseRaster);
        rasterRow->addWidget(m_rasterCombo, 1);
        rasterRow->addWidget(m_browseBtn);
        form->addRow(tr("Raster:"), rasterRow);

        m_bandSpin = new QSpinBox(srcGroup);
        m_bandSpin->setRange(1, 512);
        form->addRow(tr("Band:"), m_bandSpin);

        m_scaleSpin = new QDoubleSpinBox(srcGroup);
        m_scaleSpin->setRange(-1e6, 1e6);
        m_scaleSpin->setDecimals(6);
        m_scaleSpin->setValue(1.0);
        m_scaleSpin->setToolTip(
            tr("Sampled value is multiplied by this before assignment "
               "(e.g. 0.01 for a depth raster stored in centimetres). "
               "Not applied to classified key values."));
        form->addRow(tr("Scale:"), m_scaleSpin);

        m_offsetSpin = new QDoubleSpinBox(srcGroup);
        m_offsetSpin->setRange(-1e6, 1e6);
        m_offsetSpin->setDecimals(6);
        m_offsetSpin->setValue(0.0);
        m_offsetSpin->setToolTip(tr("Added after scaling."));
        form->addRow(tr("Offset:"), m_offsetSpin);
        srcVBox->addLayout(form);
    }

    srcVBox->addWidget(m_srcVector);
    {
        auto *form = new QFormLayout;
        form->setContentsMargins(20, 0, 0, 0);
        m_vectorCombo = new QComboBox(srcGroup);
        m_vectorCombo->setMinimumWidth(220);
        connect(m_vectorCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) { refreshVectorFields(); updateButtons(); });
        form->addRow(tr("Layer:"), m_vectorCombo);

        m_fieldCombo = new QComboBox(srcGroup);
        m_fieldCombo->setMinimumWidth(220);
        form->addRow(tr("Field:"), m_fieldCombo);

        m_selectedOnly = new QCheckBox(tr("Use selected features only"), srcGroup);
        form->addRow(QString(), m_selectedOnly);
        srcVBox->addLayout(form);
    }
    connect(m_srcRaster, &QRadioButton::toggled,
            this, &MeshAttributeAssignDialog::onSourceChanged);

    // ---- Sampling --------------------------------------------------------
    {
        auto *form = new QFormLayout;
        m_samplingCombo = new QComboBox(srcGroup);
        m_samplingCombo->addItem(tr("Cell centroid (point sample)"),
                                 int(Sampling::Centroid));
        m_samplingCombo->addItem(tr("Overlay — automatic (by source type)"),
                                 int(Sampling::OverlayAuto));
        m_samplingCombo->addItem(tr("Overlay — area-weighted mean"),
                                 int(Sampling::AreaWeightedMean));
        m_samplingCombo->addItem(tr("Overlay — majority (largest share)"),
                                 int(Sampling::Majority));
        m_samplingCombo->addItem(tr("Natural neighbour"),
                                 int(Sampling::NaturalNeighbour));
        m_samplingCombo->setToolTip(tr(
            "Centroid: one point sample per cell — fastest, and what earlier "
            "versions did.\n"
            "Overlay: reads the whole cell footprint. Majority is correct for "
            "categorical sources, area-weighted mean for continuous ones; "
            "\"automatic\" picks by the band / field type and reports what it "
            "chose.\n"
            "Natural neighbour: for scattered point sources (soil samples, "
            "borehole logs) rather than coverages."));
        connect(m_samplingCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &MeshAttributeAssignDialog::onSamplingChanged);
        form->addRow(tr("Sampling:"), m_samplingCombo);

        m_nnVariantCombo = new QComboBox(srcGroup);
        m_nnVariantCombo->addItem(tr("Sibson (area-stealing)"), 0);
        m_nnVariantCombo->addItem(tr("Laplace (edge-ratio)"), 1);
        m_nnVariantCombo->setToolTip(tr(
            "Sibson: smooth area-based natural-neighbour coordinates.\n"
            "Laplace: faster non-Sibsonian edge/distance ratio.\n"
            "Both are undefined outside the seed convex hull; cells out there "
            "are left unchanged."));
        m_nnVariantLbl = new QLabel(tr("NN variant:"), srcGroup);
        form->addRow(m_nnVariantLbl, m_nnVariantCombo);
        srcVBox->addLayout(form);
    }
    outer->addWidget(srcGroup);

    // ---- Write target ----------------------------------------------------
    m_writeGroup = new QGroupBox(tr("Write as"), this);
    {
        auto *v = new QVBoxLayout(m_writeGroup);
        m_writeOverrides = new QRadioButton(tr("Per-cell overrides"), m_writeGroup);
        m_writeDefaults  = new QRadioButton(tr("Region defaults (by tag)"),
                                            m_writeGroup);
        auto *grp = new QButtonGroup(m_writeGroup);
        grp->addButton(m_writeOverrides);
        grp->addButton(m_writeDefaults);
        m_writeOverrides->setChecked(true);
        m_writeOverrides->setToolTip(
            tr("One [2D_INFILTRATION] row per cell. Correct when the source "
               "has no correspondence with the mesh's region tags."));
        m_writeDefaults->setToolTip(
            tr("One [2D_INFILTRATION_DEFAULTS] row per source key that names "
               "an existing region tag ([2D_TRIANGLES] TAG). Every cell in "
               "the region picks the row up by inheritance, so the "
               "assignment stays editable as regions afterwards instead of "
               "being frozen into N per-cell rows (engine D-I3). Source keys "
               "matching no region tag are reported and skipped."));
        v->addWidget(m_writeOverrides);
        v->addWidget(m_writeDefaults);

        m_keepInherited = new QCheckBox(
            tr("Leave cells that already inherit these values unchanged"),
            m_writeGroup);
        m_keepInherited->setChecked(true);
        m_keepInherited->setToolTip(
            tr("Preserves tag inheritance (engine D-I3): a cell whose region "
               "default already resolves to the row being assigned keeps "
               "tracking its region instead of being frozen into a per-cell "
               "copy, so a later region-level edit still reaches it."));
        v->addWidget(m_keepInherited);

        // Region-defaults writing never materialises an override, so the
        // "leave inheriting cells alone" guard has nothing to guard.
        connect(m_writeDefaults, &QRadioButton::toggled, m_keepInherited,
                [this](bool on) { m_keepInherited->setEnabled(!on); });
    }
    outer->addWidget(m_writeGroup);

    // ---- Scope -----------------------------------------------------------
    {
        auto *scopeGroup = new QGroupBox(tr("Apply to"), this);
        auto *row = new QHBoxLayout(scopeGroup);
        m_scopeAll      = new QRadioButton(tr("All cells"), scopeGroup);
        m_scopeSelected = new QRadioButton(tr("Selected cells"), scopeGroup);
        auto *grp = new QButtonGroup(scopeGroup);
        grp->addButton(m_scopeAll);
        grp->addButton(m_scopeSelected);
        row->addWidget(m_scopeAll);
        row->addWidget(m_scopeSelected);
        row->addStretch();
        m_scopeAll->setChecked(true);
        // "Selected cells" is only meaningful with a live cell selection —
        // count it directly (scopeTriangles() answers for the current radio,
        // which is not set yet).
        int nSelected = 0;
        if (m_selection) {
            for (const SWMMObjectRef &ref : m_selection->selection())
                if (ref.objectType == SWMMObjectRef::MeshCell) ++nSelected;
        }
        m_scopeSelected->setEnabled(nSelected > 0);
        m_scopeSelected->setText(tr("Selected cells (%1)").arg(nSelected));
        outer->addWidget(scopeGroup);
    }

    m_statusLbl = new QLabel(tr("Choose a source, then Preview."), this);
    m_statusLbl->setWordWrap(true);
    outer->addWidget(m_statusLbl);

    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    outer->addWidget(m_progress);

    auto *buttons = new QDialogButtonBox(this);
    m_previewBtn = buttons->addButton(tr("Preview"), QDialogButtonBox::ActionRole);
    m_applyBtn   = buttons->addButton(tr("Apply"),   QDialogButtonBox::AcceptRole);
    m_closeBtn   = buttons->addButton(QDialogButtonBox::Close);
    connect(m_previewBtn, &QPushButton::clicked,
            this, &MeshAttributeAssignDialog::onPreview);
    connect(m_applyBtn, &QPushButton::clicked,
            this, &MeshAttributeAssignDialog::onApply);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &MeshAttributeAssignDialog::onCancelOrClose);
    outer->addWidget(buttons);

    m_srcRaster->setChecked(initialSource == Source::Raster);
    m_srcVector->setChecked(initialSource == Source::Vector);
    resize(760, 700);
}

void MeshAttributeAssignDialog::populateLayerCombos()
{
    if (!m_canvas) return;
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (auto *r = qobject_cast<GISRasterLayer *>(l))
            m_rasterCombo->addItem(r->name(),
                                   QVariant::fromValue<quintptr>(
                                       reinterpret_cast<quintptr>(r)));
        else if (auto *v = qobject_cast<GISVectorLayer *>(l))
            m_vectorCombo->addItem(v->name(),
                                   QVariant::fromValue<quintptr>(
                                       reinterpret_cast<quintptr>(v)));
    }
    refreshVectorFields();
}

void MeshAttributeAssignDialog::refreshVectorFields()
{
    // Reachable from m_vectorCombo's currentIndexChanged, which is connected
    // before the rest of the source group exists.
    if (!m_fieldCombo || !m_key1Combo || !m_targetTable) return;
    const QString prevField = m_fieldCombo->currentText();
    const QString prevKey1  = m_key1Combo->currentText();
    const QString prevKey2  = m_key2Combo->currentText();
    m_fieldCombo->clear();
    m_key1Combo->clear();
    m_key2Combo->clear();
    if (!m_vectorCombo || m_vectorCombo->currentIndex() < 0) return;
    auto *v = reinterpret_cast<GISVectorLayer *>(
        m_vectorCombo->currentData().value<quintptr>());
    if (!v) return;
    const QStringList fields = v->fieldNames();
    m_fieldCombo->addItems(fields);
    m_key1Combo->addItems(fields);
    m_key2Combo->addItems(fields);
    auto restore = [](QComboBox *c, const QString &prev) {
        const int i = c->findText(prev);
        if (i >= 0) c->setCurrentIndex(i);
    };
    restore(m_fieldCombo, prevField);
    restore(m_key1Combo,  prevKey1);
    restore(m_key2Combo,  prevKey2);

    // Re-key the multi-target rows against the new field list.
    for (int row = 0; row < m_targetTable->rowCount(); ++row)
        if (auto *cb = qobject_cast<QComboBox *>(m_targetTable->cellWidget(row, 1))) {
            const QString prev = cb->currentText();
            cb->clear();
            cb->addItems(fields);
            restore(cb, prev);
        }
}

// ===========================================================================
// Mode / source / sampling wiring
// ===========================================================================

MeshAttributeAssignDialog::Mode MeshAttributeAssignDialog::currentMode() const
{
    return Mode(m_modeCombo->currentData().toInt());
}

MeshAttributeAssignDialog::Sampling
MeshAttributeAssignDialog::currentSampling() const
{
    return Sampling(m_samplingCombo->currentData().toInt());
}

void MeshAttributeAssignDialog::onModeChanged()
{
    const Mode m = currentMode();
    m_singleGroup->setVisible(m == Mode::SingleNumeric);
    m_multiGroup->setVisible(m == Mode::MultiNumeric);
    m_lookupGroup->setVisible(m == Mode::ClassifiedInfil);
    m_writeGroup->setVisible(m == Mode::ClassifiedInfil);

    if (m == Mode::MultiNumeric && m_targetTable->rowCount() == 0)
        onAddTargetRow();

    // Natural neighbour interpolates a continuous surface; interpolating a
    // class code is meaningless, so it is not offered for the lookup mode.
    if (auto *model = qobject_cast<QStandardItemModel *>(m_samplingCombo->model())) {
        const int nnRow = m_samplingCombo->findData(int(Sampling::NaturalNeighbour));
        const int awRow = m_samplingCombo->findData(int(Sampling::AreaWeightedMean));
        const bool allow = m != Mode::ClassifiedInfil;
        for (int row : {nnRow, awRow}) {
            if (row < 0) continue;
            if (QStandardItem *item = model->item(row))
                item->setFlags(allow ? (item->flags() | Qt::ItemIsEnabled)
                                     : (item->flags() & ~Qt::ItemIsEnabled));
        }
        if (!allow && (currentSampling() == Sampling::NaturalNeighbour
                       || currentSampling() == Sampling::AreaWeightedMean))
            m_samplingCombo->setCurrentIndex(
                m_samplingCombo->findData(int(Sampling::Centroid)));
    }
    onSamplingChanged();
    // The band / scale / field controls are gated on the mode too.
    if (m_srcRaster) onSourceChanged();
}

void MeshAttributeAssignDialog::onSamplingChanged()
{
    const bool nn = currentSampling() == Sampling::NaturalNeighbour;
    m_nnVariantCombo->setVisible(nn);
    m_nnVariantLbl->setVisible(nn);
    if (nn && m_srcRaster->isChecked()) {
        // Natural neighbour needs scattered points, which only a vector layer
        // can supply; fall back rather than silently sampling something else.
        m_srcVector->setChecked(true);
    }
    updateButtons();
}

void MeshAttributeAssignDialog::onSourceChanged()
{
    const bool raster = m_srcRaster->isChecked();
    const Mode mode   = currentMode();
    m_rasterCombo->setEnabled(raster);
    m_browseBtn->setEnabled(raster);
    m_bandSpin->setEnabled(raster && mode != Mode::ClassifiedInfil);
    m_scaleSpin->setEnabled(raster && mode != Mode::ClassifiedInfil);
    m_offsetSpin->setEnabled(raster && mode != Mode::ClassifiedInfil);
    m_vectorCombo->setEnabled(!raster);
    m_fieldCombo->setEnabled(!raster && mode == Mode::SingleNumeric);
    m_selectedOnly->setEnabled(!raster);

    // The classified key comes from a field for vectors and from a band for
    // rasters — show only the control that applies.
    m_key1Combo->setVisible(!raster);
    m_key2Combo->setVisible(!raster);
    m_key1BandSpin->setVisible(raster);
    m_key2BandSpin->setVisible(raster);
    m_key1Label->setText(raster ? tr("Key band:") : tr("Key field:"));
    m_key2Label->setText(raster ? tr("Second key band:")
                                : tr("Second key field:"));

    // Multi-target rows pair with a band (raster) or a field (vector), so the
    // second column's editor changes with the source.
    for (int row = 0; row < m_targetTable->rowCount(); ++row)
        setTargetRowSourceWidget(row);
    updateButtons();
}

void MeshAttributeAssignDialog::onTargetChanged() { updateButtons(); }

void MeshAttributeAssignDialog::onBrowseRaster()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select Raster"), QString(),
        tr("Raster files (*.tif *.tiff *.asc *.img *.vrt *.nc);;All files (*)"));
    if (path.isEmpty()) return;
    m_browsedRasterPath = path;
    m_rasterCombo->addItem(QFileInfo(path).fileName(), QVariant(path));
    m_rasterCombo->setCurrentIndex(m_rasterCombo->count() - 1);
    updateButtons();
}

void MeshAttributeAssignDialog::updateButtons()
{
    // The lookup-table editor is seeded during buildUi(), before the source
    // radios and the button box exist; it re-enters here through
    // onAddLookupRow(), so answer nothing until the UI is complete.
    if (!m_previewBtn || !m_srcRaster) return;
    if (m_watcher && m_watcher->isRunning()) return;

    const Mode mode = currentMode();
    bool targetOk = false;
    if (mode == Mode::SingleNumeric) {
        const QByteArray key = m_targetCombo->currentData().toByteArray();
        const mesh::CellParamSpec *spec = mesh::cellParamSpec(key);
        targetOk = spec && spec->enabled;
        if (spec && !spec->enabled) m_statusLbl->setText(spec->tooltip);
    } else if (mode == Mode::MultiNumeric) {
        targetOk = !collectTargetKeys().isEmpty();
    } else {
        targetOk = m_lookupTable->rowCount() > 1
                   && (m_srcRaster->isChecked() || m_key1Combo->currentIndex() >= 0);
    }

    const bool srcOk = m_srcRaster->isChecked()
                           ? m_rasterCombo->currentIndex() >= 0
                           : (m_vectorCombo->currentIndex() >= 0
                              && (mode != Mode::SingleNumeric
                                  || m_fieldCombo->currentIndex() >= 0));
    const bool ok = m_mesh && targetOk && srcOk;
    m_previewBtn->setEnabled(ok);
    m_applyBtn->setEnabled(ok);
}

// ===========================================================================
// Multi-target table
// ===========================================================================

void MeshAttributeAssignDialog::setTargetRowSourceWidget(int row)
{
    if (row < 0 || row >= m_targetTable->rowCount()) return;
    if (m_srcRaster->isChecked()) {
        auto *band = new QSpinBox(m_targetTable);
        band->setRange(1, 512);
        band->setValue(row + 1);
        m_targetTable->setCellWidget(row, 1, band);
    } else {
        auto *field = new QComboBox(m_targetTable);
        if (auto *v = reinterpret_cast<GISVectorLayer *>(
                m_vectorCombo->currentData().value<quintptr>()))
            field->addItems(v->fieldNames());
        m_targetTable->setCellWidget(row, 1, field);
    }
}

void MeshAttributeAssignDialog::onAddTargetRow()
{
    const int row = m_targetTable->rowCount();
    m_targetTable->insertRow(row);

    auto *param = new QComboBox(m_targetTable);
    fillParamCombo(param, m_depthUnitLabel);
    connect(param, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateButtons(); });
    m_targetTable->setCellWidget(row, 0, param);

    setTargetRowSourceWidget(row);
    updateButtons();
}

void MeshAttributeAssignDialog::onRemoveTargetRow()
{
    const int row = m_targetTable->currentRow();
    if (row < 0) return;
    m_targetTable->removeRow(row);
    updateButtons();
}

QVector<QByteArray> MeshAttributeAssignDialog::collectTargetKeys() const
{
    QVector<QByteArray> keys;
    for (int row = 0; row < m_targetTable->rowCount(); ++row) {
        auto *param = qobject_cast<QComboBox *>(m_targetTable->cellWidget(row, 0));
        if (!param) continue;
        const QByteArray key = param->currentData().toByteArray();
        const mesh::CellParamSpec *spec = mesh::cellParamSpec(key);
        if (!spec || !spec->enabled) continue;
        keys.append(key);
    }
    return keys;
}

// ===========================================================================
// Lookup-table editor
// ===========================================================================

void MeshAttributeAssignDialog::rebuildLookupColumns()
{
    const bool two = m_twoKeyCheck->isChecked();
    m_lookupTable->setColumnHidden(kColKey2, !two);
    m_key2Combo->setEnabled(two);
    m_key2BandSpin->setEnabled(two);
    m_key2Label->setEnabled(two);
}

void MeshAttributeAssignDialog::onTwoKeyToggled()
{
    rebuildLookupColumns();
    updateButtons();
}

void MeshAttributeAssignDialog::maskLookupRow(int row)
{
    auto *methodCombo = qobject_cast<QComboBox *>(
        m_lookupTable->cellWidget(row, kColMethod));
    if (!methodCombo) return;
    const auto method = mesh::InfilMethod(methodCombo->currentIndex()
                                          + int(mesh::InfilMethod::None));
    for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot) {
        QTableWidgetItem *item = m_lookupTable->item(row, kColP0 + slot);
        if (!item) continue;
        const bool used = mesh::infilUsesParam(method, slot);
        if (used) {
            item->setFlags(item->flags() | Qt::ItemIsEnabled | Qt::ItemIsEditable);
            item->setToolTip(mesh::infilParamLabel(method, slot));
            if (item->text() == QStringLiteral("—")) item->setText(QString());
        } else {
            item->setFlags(item->flags() & ~(Qt::ItemIsEnabled | Qt::ItemIsEditable));
            item->setText(QStringLiteral("—"));
            item->setToolTip(QString());
        }
    }
}

void MeshAttributeAssignDialog::onAddLookupRow()
{
    const int row = m_lookupTable->rowCount();
    m_lookupTable->insertRow(row);
    const bool fallback = row == 0;

    for (int col : {kColKey1, kColKey2}) {
        auto *item = new QTableWidgetItem(fallback ? tr("(unmatched)") : QString());
        if (fallback)
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_lookupTable->setItem(row, col, item);
    }
    if (fallback)
        m_lookupTable->item(row, kColKey1)->setToolTip(
            tr("Applied to every key the table does not list."));

    auto *method = new QComboBox(m_lookupTable);
    method->addItems(mesh::infilMethodLabels());
    // Look the row up through the widget rather than capturing the index —
    // removing a row above this one would make a captured index point at the
    // wrong row's parameter cells.
    connect(method, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this, method](int) {
                for (int rr = 0; rr < m_lookupTable->rowCount(); ++rr)
                    if (m_lookupTable->cellWidget(rr, kColMethod) == method) {
                        maskLookupRow(rr);
                        return;
                    }
            });
    m_lookupTable->setCellWidget(row, kColMethod, method);

    for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot)
        m_lookupTable->setItem(row, kColP0 + slot, new QTableWidgetItem(QString()));

    auto *dest = new QComboBox(m_lookupTable);
    dest->addItems(mesh::infilDestLabels());
    // Every destination is routed by the engine now; AQUIFER_2D additionally
    // needs a [2D_AQUIFER] in the model, and the engine says so by name at
    // resolve. The entries carry their hint instead of being disabled.
    if (auto *model = qobject_cast<QStandardItemModel *>(dest->model()))
        for (int d = int(mesh::InfilDest::Lost);
             d <= int(mesh::InfilDest::Aquifer2D); ++d)
            if (QStandardItem *item = model->item(d))
                item->setToolTip(mesh::infilDestHint(mesh::InfilDest(d)));
    m_lookupTable->setCellWidget(row, kColDest, dest);

    maskLookupRow(row);
    updateButtons();
}

void MeshAttributeAssignDialog::onRemoveLookupRow()
{
    const int row = m_lookupTable->currentRow();
    if (row <= 0) return;      // row 0 is the fallback and always exists
    m_lookupTable->removeRow(row);
    updateButtons();
}

mesh::InfilLookupTable MeshAttributeAssignDialog::collectLookupTable() const
{
    mesh::InfilLookupTable t;
    t.twoKey = m_twoKeyCheck->isChecked();
    t.key1Label = m_srcRaster->isChecked()
                      ? tr("Band %1").arg(m_key1BandSpin->value())
                      : m_key1Combo->currentText();
    if (t.twoKey)
        t.key2Label = m_srcRaster->isChecked()
                          ? tr("Band %1").arg(m_key2BandSpin->value())
                          : m_key2Combo->currentText();

    auto readRow = [this](int row) {
        mesh::InfilRow r;
        if (auto *cb = qobject_cast<QComboBox *>(
                m_lookupTable->cellWidget(row, kColMethod)))
            r.method = mesh::InfilMethod(cb->currentIndex()
                                         + int(mesh::InfilMethod::None));
        for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot) {
            if (!mesh::infilUsesParam(r.method, slot)) continue;
            if (QTableWidgetItem *item = m_lookupTable->item(row, kColP0 + slot)) {
                bool ok = false;
                const double v = item->text().trimmed().toDouble(&ok);
                if (ok) r.p[slot] = v;
            }
        }
        if (auto *cb = qobject_cast<QComboBox *>(
                m_lookupTable->cellWidget(row, kColDest)))
            r.dest = mesh::InfilDest(cb->currentIndex());
        return r;
    };

    for (int row = 0; row < m_lookupTable->rowCount(); ++row) {
        if (row == 0) { t.fallback = readRow(0); continue; }
        mesh::InfilLookupEntry e;
        if (QTableWidgetItem *item = m_lookupTable->item(row, kColKey1))
            e.key1 = item->text().trimmed();
        if (t.twoKey)
            if (QTableWidgetItem *item = m_lookupTable->item(row, kColKey2))
                e.key2 = item->text().trimmed();
        e.row = readRow(row);
        if (e.key1.isEmpty()) continue;
        t.entries.append(e);
    }
    return t;
}

void MeshAttributeAssignDialog::applyLookupTable(const mesh::InfilLookupTable &t)
{
    m_lookupTable->setRowCount(0);
    m_twoKeyCheck->setChecked(t.twoKey);
    rebuildLookupColumns();

    auto writeRow = [this](int row, const mesh::InfilRow &r) {
        if (auto *cb = qobject_cast<QComboBox *>(
                m_lookupTable->cellWidget(row, kColMethod)))
            cb->setCurrentIndex(int(r.method) - int(mesh::InfilMethod::None));
        for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot)
            if (QTableWidgetItem *item = m_lookupTable->item(row, kColP0 + slot))
                item->setText(std::isfinite(r.p[slot])
                                  ? QString::number(r.p[slot])
                                  : QString());
        if (auto *cb = qobject_cast<QComboBox *>(
                m_lookupTable->cellWidget(row, kColDest)))
            cb->setCurrentIndex(int(r.dest));
        maskLookupRow(row);
    };

    onAddLookupRow();                     // row 0 — fallback
    writeRow(0, t.fallback);
    for (const mesh::InfilLookupEntry &e : t.entries) {
        const int row = m_lookupTable->rowCount();
        onAddLookupRow();
        if (QTableWidgetItem *item = m_lookupTable->item(row, kColKey1))
            item->setText(e.key1);
        if (QTableWidgetItem *item = m_lookupTable->item(row, kColKey2))
            item->setText(e.key2);
        writeRow(row, e.row);
    }
    updateButtons();
}

void MeshAttributeAssignDialog::onLoadLookupCsv()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Infiltration Lookup Table"), QString(),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) return;
    mesh::InfilLookupTable t;
    QString err;
    if (!mesh::loadLookupTableCsv(&t, path, &err)) {
        m_statusLbl->setText(tr("Could not load %1: %2")
                                 .arg(QFileInfo(path).fileName(), err));
        return;
    }
    applyLookupTable(t);
    m_statusLbl->setText(tr("Loaded %n lookup row(s) from %1.", nullptr,
                            int(t.entries.size()))
                             .arg(QFileInfo(path).fileName()));
}

void MeshAttributeAssignDialog::onSaveLookupCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Infiltration Lookup Table"), QString(),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) return;
    const mesh::InfilLookupTable t = collectLookupTable();
    QString err;
    if (!mesh::saveLookupTableCsv(t, path, &err)) {
        m_statusLbl->setText(tr("Could not save %1: %2")
                                 .arg(QFileInfo(path).fileName(), err));
        return;
    }
    m_statusLbl->setText(tr("Saved %n lookup row(s) to %1.", nullptr,
                            int(t.entries.size()))
                             .arg(QFileInfo(path).fileName()));
}

// ===========================================================================
// Scope / job collection
// ===========================================================================

QVector<int> MeshAttributeAssignDialog::scopeTriangles() const
{
    QVector<int> out;
    if (!m_mesh) return out;
    const int nt = m_mesh->mesh().triangles.size();

    const bool selectedOnly = m_scopeSelected && m_scopeSelected->isChecked();
    if (!selectedOnly) {
        out.reserve(nt);
        for (int i = 0; i < nt; ++i) out.append(i);
        return out;
    }
    if (!m_selection) return out;
    const QString wantKey = mesh::MeshObjectRef::layerKey(m_mesh->sourcePath());
    for (const SWMMObjectRef &ref : m_selection->selection()) {
        if (ref.objectType != SWMMObjectRef::MeshCell) continue;
        QString lk; int tri = -1;
        if (!mesh::MeshObjectRef::parseCell(ref, &lk, &tri)) continue;
        if (lk != wantKey) continue;
        if (tri >= 0 && tri < nt) out.append(tri);
    }
    return out;
}

QVector<QPointF> MeshAttributeAssignDialog::centroidsFor(
        const QVector<int> &tris) const
{
    QVector<QPointF> out;
    if (!m_mesh) return out;
    const mesh::MeshResult &m = m_mesh->mesh();
    out.reserve(tris.size());
    for (int t : tris)
        out.append(mesh::cellCentroid(m, t));   // area centroid (cellGeom)
    return out;
}

bool MeshAttributeAssignDialog::collectJob(Job *job, QString *err) const
{
    if (!m_mesh) { *err = tr("No mesh layer."); return false; }

    job->mode     = currentMode();
    job->source   = m_srcRaster->isChecked() ? Source::Raster : Source::Vector;
    job->sampling = currentSampling();

    job->triangles = scopeTriangles();
    if (job->triangles.isEmpty()) {
        *err = tr("No cells are in scope.");
        return false;
    }
    job->centroids = centroidsFor(job->triangles);

    // Cell footprints are only needed by the overlay modes; a million-cell
    // mesh is 48 MB of vertices, so do not copy them otherwise.
    if (job->sampling == Sampling::OverlayAuto
        || job->sampling == Sampling::AreaWeightedMean
        || job->sampling == Sampling::Majority)
    {
        const mesh::MeshResult &m = m_mesh->mesh();
        job->triVerts.reserve(job->triangles.size() * kFootprintStride);
        for (int t : std::as_const(job->triangles)) {
            const mesh::MeshTriangle &tri = m.triangles[t];
            job->triVerts.append(m.vertices[tri.v0].xy);
            job->triVerts.append(m.vertices[tri.v1].xy);
            job->triVerts.append(m.vertices[tri.v2].xy);
            // Slot 3: the quad's fourth corner, or the first corner again
            // for a triangle (kFootprintStride).
            job->triVerts.append(tri.isQuad() ? m.vertices[tri.v3].xy
                                              : m.vertices[tri.v0].xy);
        }
    }

    if (SpatialReferenceSystem *srs = m_mesh->srs())
        job->meshCrsWkt = srs->toWkt();

    // ---- Targets ---------------------------------------------------------
    if (job->mode == Mode::SingleNumeric) {
        job->targetKeys.append(m_targetCombo->currentData().toByteArray());
    } else if (job->mode == Mode::MultiNumeric) {
        job->targetKeys = collectTargetKeys();
        if (job->targetKeys.isEmpty()) {
            *err = tr("Add at least one target parameter.");
            return false;
        }
    }
    for (const QByteArray &key : std::as_const(job->targetKeys)) {
        const mesh::CellParamSpec *spec = mesh::cellParamSpec(key);
        job->targetMin.append(spec ? spec->min : -std::numeric_limits<double>::max());
        job->targetMax.append(spec ? spec->max :  std::numeric_limits<double>::max());
    }

    // ---- Source ----------------------------------------------------------
    if (job->source == Source::Raster) {
        const QVariant data = m_rasterCombo->currentData();
        if (data.typeId() == QMetaType::QString) {
            job->rasterPath = data.toString();
        } else if (auto *layer = reinterpret_cast<GISRasterLayer *>(
                       data.value<quintptr>())) {
            job->rasterPath = layer->filePath();
        }
        if (job->rasterPath.isEmpty()) {
            *err = tr("The selected raster has no readable file path.");
            return false;
        }
        job->scale  = m_scaleSpin->value();
        job->offset = m_offsetSpin->value();
        if (job->mode == Mode::SingleNumeric) {
            job->bands.append(m_bandSpin->value());
        } else if (job->mode == Mode::MultiNumeric) {
            for (int row = 0; row < m_targetTable->rowCount(); ++row) {
                auto *param = qobject_cast<QComboBox *>(
                    m_targetTable->cellWidget(row, 0));
                const mesh::CellParamSpec *spec = param
                    ? mesh::cellParamSpec(param->currentData().toByteArray())
                    : nullptr;
                if (!spec || !spec->enabled) continue;
                auto *band = qobject_cast<QSpinBox *>(
                    m_targetTable->cellWidget(row, 1));
                job->bands.append(band ? band->value() : 1);
            }
        } else {
            job->keyBand1 = m_key1BandSpin->value();
            job->keyBand2 = m_twoKeyCheck->isChecked() ? m_key2BandSpin->value() : 0;
        }
    } else {
        auto *vec = reinterpret_cast<GISVectorLayer *>(
            m_vectorCombo->currentData().value<quintptr>());
        if (!vec) {
            *err = tr("Select a vector layer to read the field from.");
            return false;
        }
        job->vectorPath       = vec->filePath();
        job->vectorLayerName  = vec->ogrLayerName();
        job->vectorFilterExpr = vec->filterExpression();
        if (job->vectorPath.isEmpty()) {
            *err = tr("The selected vector layer has no readable file path.");
            return false;
        }
        job->filterBySelection = m_selectedOnly->isChecked();
        if (job->filterBySelection) job->selectedIds = vec->selectedFeatureIds();

        if (job->mode == Mode::SingleNumeric) {
            job->fields << m_fieldCombo->currentText();
            if (job->fields.first().isEmpty()) {
                *err = tr("Select the attribute field to assign.");
                return false;
            }
        } else if (job->mode == Mode::MultiNumeric) {
            for (int row = 0; row < m_targetTable->rowCount(); ++row) {
                auto *param = qobject_cast<QComboBox *>(
                    m_targetTable->cellWidget(row, 0));
                const mesh::CellParamSpec *spec = param
                    ? mesh::cellParamSpec(param->currentData().toByteArray())
                    : nullptr;
                if (!spec || !spec->enabled) continue;
                auto *field = qobject_cast<QComboBox *>(
                    m_targetTable->cellWidget(row, 1));
                job->fields << (field ? field->currentText() : QString());
            }
            if (job->fields.contains(QString())) {
                *err = tr("Every target row needs a source field.");
                return false;
            }
        } else {
            job->keyField1 = m_key1Combo->currentText();
            if (job->keyField1.isEmpty()) {
                *err = tr("Select the field that carries the classification key.");
                return false;
            }
            if (m_twoKeyCheck->isChecked()) {
                job->keyField2 = m_key2Combo->currentText();
                if (job->keyField2.isEmpty()) {
                    *err = tr("Select the second classification key field.");
                    return false;
                }
            }
        }
    }

    if (job->mode == Mode::ClassifiedInfil) {
        job->table = collectLookupTable();
        if (job->table.entries.isEmpty() && job->table.fallback.isNone()) {
            *err = tr("The lookup table is empty — add at least one row, or "
                      "load a CSV.");
            return false;
        }
    }

    job->nnVariant = m_nnVariantCombo->currentIndex() == 1
                         ? mesh::NaturalNeighbourInterpolator::Variant::Laplace
                         : mesh::NaturalNeighbourInterpolator::Variant::Sibson;
    return true;
}

// ===========================================================================
// Running
// ===========================================================================

void MeshAttributeAssignDialog::setRunning(bool running)
{
    m_progress->setVisible(running);
    m_previewBtn->setEnabled(!running);
    m_applyBtn->setEnabled(!running);
    m_modeCombo->setEnabled(!running);
    m_closeBtn->setText(running ? tr("Stop") : tr("Close"));
    if (!running) updateButtons();
}

void MeshAttributeAssignDialog::startSampling(bool apply)
{
    if (m_watcher && m_watcher->isRunning()) return;

    Job job;
    QString err;
    if (!collectJob(&job, &err)) {
        m_statusLbl->setText(err);
        return;
    }
    m_runJob       = job;
    m_applyPending = apply;

    m_progress->setRange(0, std::max(1, int(job.triangles.size())));
    m_progress->setValue(0);
    m_statusLbl->setText(apply ? tr("Sampling %n cell(s)…", nullptr,
                                    int(job.triangles.size()))
                               : tr("Previewing %n cell(s)…", nullptr,
                                    int(job.triangles.size())));
    setRunning(true);

    m_watcher = new QFutureWatcher<SampleResult>(this);
    connect(m_watcher, &QFutureWatcher<SampleResult>::progressValueChanged,
            m_progress, &QProgressBar::setValue);
    connect(m_watcher, &QFutureWatcher<SampleResult>::finished,
            this, &MeshAttributeAssignDialog::onSampleFinished);
    m_watcher->setFuture(QtConcurrent::run(assignment::runMeshAssignmentSampling, std::move(job)));
}

void MeshAttributeAssignDialog::onPreview() { startSampling(false); }
void MeshAttributeAssignDialog::onApply()   { startSampling(true); }

void MeshAttributeAssignDialog::onCancelOrClose() { reject(); }

void MeshAttributeAssignDialog::reject()
{
    if (m_watcher && m_watcher->isRunning()) {
        // Nothing has touched the mesh yet — every write happens in
        // onSampleFinished(), after a successful run — so a cancel here
        // leaves the mesh exactly as it was.
        m_watcher->cancel();
        m_closeBtn->setEnabled(false);
        m_statusLbl->setText(tr("Cancelling…"));
        return;
    }
    QDialog::reject();
}

void MeshAttributeAssignDialog::onSampleFinished()
{
    auto *watcher = m_watcher;
    m_watcher = nullptr;
    setRunning(false);
    m_closeBtn->setEnabled(true);
    if (!watcher) return;

    const bool cancelled = watcher->isCanceled();
    SampleResult r;
    if (!cancelled) {
        try {
            r = watcher->result();
        } catch (const std::exception &e) {
            r.error = tr("Sampling failed: %1").arg(QString::fromUtf8(e.what()));
        } catch (...) {
            r.error = tr("Sampling failed with an unknown error.");
        }
    }
    watcher->deleteLater();

    if (cancelled || r.cancelled) {
        m_statusLbl->setText(tr("Cancelled — the mesh was not modified."));
        return;
    }
    if (!r.error.isEmpty()) {
        m_statusLbl->setText(r.error);
        return;
    }
    if (!m_applyPending) {
        m_statusLbl->setText(summarise(r));
        return;
    }
    if (r.triangles.isEmpty()) {
        m_statusLbl->setText(tr("No cell received a value — nothing applied."));
        return;
    }
    applyResult(r);
}

// ===========================================================================
// Reporting / applying
// ===========================================================================

QString MeshAttributeAssignDialog::summarise(const SampleResult &r) const
{
    QStringList skipped;
    if (r.skippedNoData)
        skipped << tr("%1 no data / outside source").arg(r.skippedNoData);
    if (r.skippedNonNumeric)
        skipped << tr("%1 non-numeric").arg(r.skippedNonNumeric);
    if (r.skippedRange)
        skipped << tr("%1 out of range").arg(r.skippedRange);

    QString text = skipped.isEmpty()
        ? tr("%1 of %2 cells would receive a value.")
              .arg(r.triangles.size()).arg(r.scanned)
        : tr("%1 of %2 cells would receive a value (skipped: %3).")
              .arg(r.triangles.size()).arg(r.scanned)
              .arg(skipped.join(QStringLiteral(", ")));

    if (!r.resolvedSampling.isEmpty()) {
        text += QLatin1Char(' ');
        text += tr("Overlay resolved to %1.").arg(r.resolvedSampling);
    }
    if (r.unmatchedKeys) {
        text += QLatin1Char(' ');
        text += tr("%n cell(s) fell through to the unmatched row.", nullptr,
                   r.unmatchedKeys);
    }

    // Tag correspondence — the signal that this assignment could be expressed
    // as region defaults instead of per-cell rows (GUI plan §3.4 "Write as").
    if (currentMode() == Mode::ClassifiedInfil && m_mesh && !r.keys.isEmpty()) {
        QSet<QString> meshTags;
        for (const mesh::MeshTriangle &t : m_mesh->mesh().triangles)
            if (!t.tag.isEmpty()) meshTags.insert(t.tag);
        QSet<QString> matching;
        for (const QString &k : r.keys)
            if (meshTags.contains(k)) matching.insert(k);
        if (!matching.isEmpty()) {
            text += QLatin1Char(' ');
            text += tr("%n source key(s) match region tags.", nullptr,
                       int(matching.size()));
        }
    }
    return text;
}

void MeshAttributeAssignDialog::applyResult(const SampleResult &r)
{
    if (m_runJob.mode == Mode::ClassifiedInfil) applyInfilResult(r);
    else                                        applyNumericResult(r);
}

void MeshAttributeAssignDialog::applyNumericResult(const SampleResult &r)
{
    if (!m_mesh) return;      // layer closed while the worker ran
    MapUndoStack *stack = m_canvas ? m_canvas->undoStack() : nullptr;

    // Collect the per-target subsets first: a target with nothing to write
    // must not open an empty macro.
    struct Pending { QByteArray key; QVector<int> tris; QVector<double> vals; };
    QVector<Pending> pending;
    for (int k = 0; k < m_runJob.targetKeys.size() && k < r.values.size(); ++k) {
        Pending p;
        p.key = m_runJob.targetKeys[k];
        for (int i = 0; i < r.triangles.size() && i < r.values[k].size(); ++i) {
            if (!std::isfinite(r.values[k][i])) continue;
            p.tris.append(r.triangles[i]);
            p.vals.append(r.values[k][i]);
        }
        if (!p.tris.isEmpty()) pending.append(p);
    }
    if (pending.isEmpty()) {
        m_statusLbl->setText(tr("No cell received a value — nothing applied."));
        return;
    }

    const QString macroText = tr("Assign %n cell parameter(s) from GIS", nullptr,
                                 int(pending.size()));
    const bool macro = stack && pending.size() > 1;
    if (macro) stack->beginMacro(macroText);

    int changed = 0;
    for (const Pending &p : std::as_const(pending)) {
        const mesh::CellParamSpec *spec = mesh::cellParamSpec(p.key);
        const QString text = tr("Assign %1 to %n cell(s)", nullptr,
                                int(p.tris.size()))
                                 .arg(spec ? spec->label
                                           : QString::fromUtf8(p.key));
        changed += mesh::pushCellParamEdits(m_mesh, p.tris, p.vals, p.key,
                                            text, m_canvas);
    }
    if (macro) stack->endMacro();

    m_statusLbl->setText(
        tr("Applied %n value(s) across %1 cell(s).", nullptr, changed)
            .arg(r.triangles.size()));
}

void MeshAttributeAssignDialog::applyInfilDefaultsResult(const SampleResult &r)
{
    if (!m_mesh) return;

    // Region defaults are keyed by the mesh's own [2D_TRIANGLES] TAG values,
    // so only a source key that names an existing tag can be written: a row
    // for an unknown tag would reach no cell, which looks like a successful
    // assignment and is not one. Report and skip those instead.
    QSet<QString> meshTags;
    for (const mesh::MeshTriangle &t : m_mesh->mesh().triangles)
        if (!t.tag.isEmpty()) meshTags.insert(t.tag);

    QVector<mesh::InfilDefaultRow> rows;
    QSet<QString>                  seen;
    int                            unmatchedTags = 0;
    for (int i = 0; i < r.keys.size() && i < r.rows.size(); ++i) {
        const QString &k = r.keys.at(i);
        if (k.isEmpty() || seen.contains(k)) continue;   // one row per key
        seen.insert(k);
        // The row is a pure function of the key (the lookup table), so the
        // first cell carrying a key fixes that key's row for every other.
        if (meshTags.contains(k)) rows.append(mesh::InfilDefaultRow{k, r.rows.at(i)});
        else                      ++unmatchedTags;
    }

    if (rows.isEmpty()) {
        m_statusLbl->setText(
            tr("No source key names an existing region tag — nothing applied. "
               "Tag the cells first, or write per-cell overrides instead."));
        return;
    }

    // One command, one Ctrl+Z, however many tags moved.
    const int changed = mesh::pushInfilDefaultsEdit(m_mesh, rows, m_canvas);
    QString text = tr("Applied %n region default row(s); every cell in those "
                      "regions inherits them.", nullptr, changed);
    if (unmatchedTags) {
        text += QLatin1Char(' ');
        text += tr("%n source key(s) matched no region tag and were skipped.",
                   nullptr, unmatchedTags);
    }
    m_statusLbl->setText(text);
}

void MeshAttributeAssignDialog::applyInfilResult(const SampleResult &r)
{
    if (!m_mesh) return;
    // GUI plan §3.4 "Write as" — region defaults keep the assignment editable
    // as regions; per-cell overrides freeze it into N rows.
    if (m_writeDefaults && m_writeDefaults->isChecked()) {
        applyInfilDefaultsResult(r);
        return;
    }
    const mesh::MeshResult &mesh0 = m_mesh->mesh();
    const bool keepInherited = m_keepInherited && m_keepInherited->isChecked();

    // Group the cells by the row they resolved to: pushCellInfilEdit writes
    // ONE row to many cells, and it is the only path that snapshots
    // provenance, so undo can put an inheriting cell back to inheriting.
    QVector<mesh::InfilRow>  distinct;
    QVector<QVector<int>>    groups;
    int inheritedSkipped = 0;

    for (int i = 0; i < r.triangles.size() && i < r.rows.size(); ++i) {
        const mesh::InfilRow &row = r.rows[i];
        if (keepInherited) {
            // Engine D-I3: a cell whose region default already resolves to
            // this row keeps tracking its region rather than being frozen
            // into an identical per-cell override.
            const mesh::ResolvedInfil cur = mesh::resolveInfil(mesh0, r.triangles[i]);
            if (cur.isInherited() && cur.row == row) { ++inheritedSkipped; continue; }
        }
        int g = -1;
        for (int d = 0; d < distinct.size(); ++d)
            if (distinct[d] == row) { g = d; break; }
        if (g < 0) { distinct.append(row); groups.append(QVector<int>()); g = distinct.size() - 1; }
        groups[g].append(r.triangles[i]);
    }

    if (groups.isEmpty()) {
        m_statusLbl->setText(
            tr("Every cell already resolves to the assigned infiltration row "
               "— nothing applied (%n kept inheriting from its region).",
               nullptr, inheritedSkipped));
        return;
    }

    MapUndoStack *stack = m_canvas ? m_canvas->undoStack() : nullptr;
    const bool macro = stack && groups.size() > 1;
    if (macro)
        stack->beginMacro(tr("Assign infiltration to %n cell(s)", nullptr,
                             int(r.triangles.size())));
    int changed = 0;
    for (int g = 0; g < groups.size(); ++g)
        changed += mesh::pushCellInfilEdit(m_mesh, groups[g], distinct[g], m_canvas);
    if (macro) stack->endMacro();

    QString text = tr("Applied %n infiltration row(s) as per-cell overrides.",
                      nullptr, changed);
    if (inheritedSkipped) {
        text += QLatin1Char(' ');
        text += tr("%n cell(s) kept inheriting from their region.", nullptr,
                   inheritedSkipped);
    }
    m_statusLbl->setText(text);
}

} // namespace openswmmvis::ui
