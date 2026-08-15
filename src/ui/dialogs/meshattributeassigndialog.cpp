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
#include "map/meshcommands.h"
#include "map/spatialreferencesystem.h"
#include "mesh/dtmsampler.h"
#include "mesh/meshcellparams.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"

#include <ogr_spatialref.h>

#include <QApplication>
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
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <cmath>

namespace openswmmvis::ui {

namespace {

/*! Reproject \p pts from \p from to \p to in place. No-op when either CRS is
 *  missing or they already match — the common case (mesh authored in the
 *  project CRS, sampled against a raster in the same CRS). */
void reprojectPoints(QVector<QPointF> &pts,
                     const SpatialReferenceSystem *from,
                     OGRSpatialReference *to)
{
    if (pts.isEmpty() || !from || !to) return;
    OGRSpatialReference *src = from->ogrSpatialReference();
    if (!src || src->IsSame(to)) return;

    OGRCoordinateTransformation *ct = OGRCreateCoordinateTransformation(src, to);
    if (!ct) return;
    QVector<double> xs(pts.size()), ys(pts.size());
    for (int i = 0; i < pts.size(); ++i) { xs[i] = pts[i].x(); ys[i] = pts[i].y(); }
    ct->Transform(pts.size(), xs.data(), ys.data());
    for (int i = 0; i < pts.size(); ++i) pts[i] = QPointF(xs[i], ys[i]);
    OGRCoordinateTransformation::DestroyCT(ct);
}

} // namespace

MeshAttributeAssignDialog::MeshAttributeAssignDialog(
        SWMM2DMeshLayer *meshLayer, MapCanvas *canvas,
        SelectionManager *selection, Source initialSource,
        const QString &depthUnitLabel, QWidget *parent)
    : QDialog(parent),
      m_mesh(meshLayer),
      m_canvas(canvas),
      m_selection(selection)
{
    setWindowTitle(tr("Assign 2D Cell Data"));
    buildUi(initialSource, depthUnitLabel);
    populateLayerCombos();
    onSourceChanged();
    updateButtons();
}

void MeshAttributeAssignDialog::buildUi(Source initialSource,
                                        const QString &depthUnitLabel)
{
    auto *outer = new QVBoxLayout(this);

    // ---- Target parameter ------------------------------------------------
    {
        auto *form = new QFormLayout;
        m_targetCombo = new QComboBox(this);
        for (const mesh::CellParamSpec &s : mesh::cellParamSpecs()) {
            m_targetCombo->addItem(mesh::cellParamLabel(s.key, depthUnitLabel),
                                   QVariant(s.key));
            const int row = m_targetCombo->count() - 1;
            m_targetCombo->setItemData(row, s.tooltip, Qt::ToolTipRole);
            if (!s.enabled) {
                if (auto *model =
                        qobject_cast<QStandardItemModel *>(m_targetCombo->model()))
                    if (QStandardItem *item = model->item(row))
                        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            }
        }
        connect(m_targetCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &MeshAttributeAssignDialog::onTargetChanged);
        form->addRow(tr("Assign to:"), m_targetCombo);
        outer->addLayout(form);
    }

    // ---- Source ----------------------------------------------------------
    auto *srcGroup = new QGroupBox(tr("Source"), this);
    auto *srcVBox  = new QVBoxLayout(srcGroup);
    auto *srcButtons = new QButtonGroup(srcGroup);

    m_srcRaster = new QRadioButton(tr("Raster (sampled at cell centroids)"), srcGroup);
    m_srcVector = new QRadioButton(tr("Vector field (polygon containing the centroid)"),
                                   srcGroup);
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
               "(e.g. 0.01 for a depth raster stored in centimetres)."));
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
    outer->addWidget(srcGroup);

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

    auto *buttons = new QDialogButtonBox(this);
    m_previewBtn = buttons->addButton(tr("Preview"), QDialogButtonBox::ActionRole);
    m_applyBtn   = buttons->addButton(tr("Apply"),   QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(m_previewBtn, &QPushButton::clicked,
            this, &MeshAttributeAssignDialog::onPreview);
    connect(m_applyBtn, &QPushButton::clicked,
            this, &MeshAttributeAssignDialog::onApply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    m_srcRaster->setChecked(initialSource == Source::Raster);
    m_srcVector->setChecked(initialSource == Source::Vector);
    resize(520, 480);
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
    m_fieldCombo->clear();
    if (!m_vectorCombo || m_vectorCombo->currentIndex() < 0) return;
    auto *v = reinterpret_cast<GISVectorLayer *>(
        m_vectorCombo->currentData().value<quintptr>());
    if (!v) return;
    m_fieldCombo->addItems(v->fieldNames());
}

void MeshAttributeAssignDialog::onSourceChanged()
{
    const bool raster = m_srcRaster->isChecked();
    m_rasterCombo->setEnabled(raster);
    m_browseBtn->setEnabled(raster);
    m_bandSpin->setEnabled(raster);
    m_scaleSpin->setEnabled(raster);
    m_offsetSpin->setEnabled(raster);
    m_vectorCombo->setEnabled(!raster);
    m_fieldCombo->setEnabled(!raster);
    m_selectedOnly->setEnabled(!raster);
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
    const QByteArray key = m_targetCombo->currentData().toByteArray();
    const mesh::CellParamSpec *spec = mesh::cellParamSpec(key);
    const bool targetOk = spec && spec->enabled;
    const bool srcOk = m_srcRaster->isChecked()
                           ? m_rasterCombo->currentIndex() >= 0
                           : (m_vectorCombo->currentIndex() >= 0
                              && m_fieldCombo->currentIndex() >= 0);
    const bool ok = m_mesh && targetOk && srcOk;
    m_previewBtn->setEnabled(ok);
    m_applyBtn->setEnabled(ok);
    if (spec && !spec->enabled)
        m_statusLbl->setText(spec->tooltip);
}

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
    for (int t : tris) {
        const mesh::MeshTriangle &tri = m.triangles[t];
        const QPointF a = m.vertices[tri.v0].xy;
        const QPointF b = m.vertices[tri.v1].xy;
        const QPointF c = m.vertices[tri.v2].xy;
        out.append(QPointF((a.x() + b.x() + c.x()) / 3.0,
                           (a.y() + b.y() + c.y()) / 3.0));
    }
    return out;
}

MeshAttributeAssignDialog::SampleResult
MeshAttributeAssignDialog::sample(const QVector<int> &tris)
{
    return m_srcRaster->isChecked() ? sampleRaster(tris) : sampleVector(tris);
}

MeshAttributeAssignDialog::SampleResult
MeshAttributeAssignDialog::sampleRaster(const QVector<int> &tris)
{
    SampleResult r;
    r.scanned = tris.size();

    // Resolve the chosen raster to a path: canvas layers carry one, and the
    // Browse… entry stores the path directly.
    QString path;
    const QVariant data = m_rasterCombo->currentData();
    if (data.typeId() == QMetaType::QString) {
        path = data.toString();
    } else if (auto *layer = reinterpret_cast<GISRasterLayer *>(
                   data.value<quintptr>())) {
        path = layer->filePath();
    }
    if (path.isEmpty()) {
        r.error = tr("The selected raster has no readable file path.");
        return r;
    }

    // The sampler owns its own GDAL handle (opened here, on this thread).
    mesh::DTMSampler sampler;
    if (!sampler.open(path, m_bandSpin->value())) {
        r.error = tr("Could not open %1: %2")
                      .arg(QFileInfo(path).fileName(), sampler.errorMsg());
        return r;
    }

    QVector<QPointF> pts = centroidsFor(tris);
    // Centroids are in the mesh layer's CRS; the sampler expects raster CRS.
    if (!sampler.crsWkt().isEmpty()) {
        OGRSpatialReference rasterSrs;
        if (rasterSrs.importFromWkt(sampler.crsWkt().toUtf8().constData())
            == OGRERR_NONE) {
            rasterSrs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            reprojectPoints(pts, m_mesh->srs(), &rasterSrs);
        }
    }

    const QVector<double> raw = sampler.sampleBulk(pts);
    const QByteArray key = m_targetCombo->currentData().toByteArray();
    const mesh::CellParamSpec *spec = mesh::cellParamSpec(key);
    const double scale  = m_scaleSpin->value();
    const double offset = m_offsetSpin->value();

    r.triangles.reserve(tris.size());
    r.values.reserve(tris.size());
    for (int i = 0; i < tris.size() && i < raw.size(); ++i) {
        if (!std::isfinite(raw[i])) { ++r.skippedNoData; continue; }
        const double v = raw[i] * scale + offset;
        if (spec && (v < spec->min || v > spec->max)) { ++r.skippedRange; continue; }
        r.triangles.append(tris[i]);
        r.values.append(v);
    }
    return r;
}

MeshAttributeAssignDialog::SampleResult
MeshAttributeAssignDialog::sampleVector(const QVector<int> &tris)
{
    SampleResult r;
    r.scanned = tris.size();

    auto *vec = reinterpret_cast<GISVectorLayer *>(
        m_vectorCombo->currentData().value<quintptr>());
    if (!vec) {
        r.error = tr("Select a vector layer to read the field from.");
        return r;
    }
    const QString field = m_fieldCombo->currentText();
    if (field.isEmpty()) {
        r.error = tr("Select the attribute field to assign.");
        return r;
    }

    // identifyAt() works in canvas CRS, so bring the centroids there once.
    QVector<QPointF> pts = centroidsFor(tris);
    SpatialReferenceSystem *canvasSrs = m_canvas ? m_canvas->canvasSRS() : nullptr;
    if (canvasSrs && canvasSrs->ogrSpatialReference())
        reprojectPoints(pts, m_mesh->srs(), canvasSrs->ogrSpatialReference());

    const bool filterBySelection = m_selectedOnly->isChecked();
    const QSet<long long> selectedIds = filterBySelection
                                            ? vec->selectedFeatureIds()
                                            : QSet<long long>();

    const QByteArray key = m_targetCombo->currentData().toByteArray();
    const mesh::CellParamSpec *spec = mesh::cellParamSpec(key);

    r.triangles.reserve(tris.size());
    r.values.reserve(tris.size());
    for (int i = 0; i < tris.size() && i < pts.size(); ++i) {
        const QList<QVariantMap> hits =
            vec->identifyAt(pts[i].x(), pts[i].y(), canvasSrs, 0.0);
        bool assigned = false;
        for (const QVariantMap &f : hits) {
            if (filterBySelection) {
                const QVariant fid = f.value(QStringLiteral("fid"));
                if (!fid.isValid() || !selectedIds.contains(fid.toLongLong()))
                    continue;
            }
            if (!f.contains(field)) continue;
            bool ok = false;
            const double v = f.value(field).toDouble(&ok);
            if (!ok) { ++r.skippedNonNumeric; assigned = true; break; }
            if (spec && (v < spec->min || v > spec->max)) {
                ++r.skippedRange; assigned = true; break;
            }
            r.triangles.append(tris[i]);
            r.values.append(v);
            assigned = true;
            break;      // first containing polygon wins
        }
        if (!assigned) ++r.skippedNoData;   // centroid outside every polygon
    }
    return r;
}

void MeshAttributeAssignDialog::onPreview()
{
    if (!m_mesh) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const SampleResult r = sample(scopeTriangles());
    QApplication::restoreOverrideCursor();

    if (!r.error.isEmpty()) {
        m_statusLbl->setText(r.error);
        return;
    }
    QStringList skipped;
    if (r.skippedNoData)
        skipped << tr("%1 no data / outside source").arg(r.skippedNoData);
    if (r.skippedNonNumeric)
        skipped << tr("%1 non-numeric").arg(r.skippedNonNumeric);
    if (r.skippedRange)
        skipped << tr("%1 out of range").arg(r.skippedRange);

    m_statusLbl->setText(
        skipped.isEmpty()
            ? tr("%1 of %2 cells would receive a value.")
                  .arg(r.triangles.size()).arg(r.scanned)
            : tr("%1 of %2 cells would receive a value (skipped: %3).")
                  .arg(r.triangles.size()).arg(r.scanned)
                  .arg(skipped.join(QStringLiteral(", "))));
}

void MeshAttributeAssignDialog::onApply()
{
    if (!m_mesh) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const SampleResult r = sample(scopeTriangles());
    QApplication::restoreOverrideCursor();

    if (!r.error.isEmpty()) {
        m_statusLbl->setText(r.error);
        return;
    }
    if (r.triangles.isEmpty()) {
        m_statusLbl->setText(tr("No cell received a value — nothing applied."));
        return;
    }

    const QByteArray key = m_targetCombo->currentData().toByteArray();
    const mesh::CellParamSpec *spec = mesh::cellParamSpec(key);
    const QString text = tr("Assign %1 to %n cell(s)", nullptr,
                            int(r.triangles.size()))
                             .arg(spec ? spec->label : QString::fromUtf8(key));
    // One undo entry for the whole assignment.
    const int changed = mesh::pushCellParamEdits(m_mesh, r.triangles, r.values,
                                                 key, text, m_canvas);
    m_statusLbl->setText(tr("Applied to %1 cell(s) (%2 already had the value).")
                             .arg(changed)
                             .arg(r.triangles.size() - changed));
}

} // namespace openswmmvis::ui
