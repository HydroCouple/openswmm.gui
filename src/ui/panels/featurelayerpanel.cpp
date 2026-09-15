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
#include <QItemSelectionModel>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QShortcut>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace openswmmvis::feature;
using openswmmvis::map::AddFieldCommand;
using openswmmvis::map::RemoveFieldCommand;
using openswmmvis::map::ResampleZCommand;

namespace openswmmvis::ui {

namespace {

enum FieldColumn { ColName = 0, ColType = 1, ColDescription = 2, ColCount };

//! Columns of the vertex grid.
enum VertexColumn { VColPart = 0, VColRing, VColIndex, VColX, VColY, VColZ,
                    VColCount };

// The (part, ring, vertex) address rides on each row's first item, so a
// rebuild or a sort cannot desynchronise row number from vertex — the same
// guarantee the feature grid gets from stashing the id on column 0.
constexpr int kPartRole  = Qt::UserRole;
constexpr int kRingRole  = Qt::UserRole + 1;
constexpr int kIndexRole = Qt::UserRole + 2;

/*! Fixed-point with enough digits for projected metres (sub-micron) and for
 *  degrees (~0.1 mm). Display only — an untouched coordinate is never written
 *  back from its formatted text, so this rounding cannot creep into the
 *  geometry. */
QString formatOrdinate(double v)
{
    return QString::number(v, 'f', 6);
}

/*! Above this the grid refuses to populate; see refreshVertexTable(). */
constexpr int kMaxVertexRows = 10000;

/*!
 * \brief Copy Z back from \p from into \p to everywhere except one vertex.
 *
 * \details FeatureLayer::sampleZ rewrites EVERY vertex of the feature, but the
 *          Z-policy checkbox promises to re-sample "when a vertex moves" —
 *          singular. Without this, nudging one X would silently discard every
 *          Z the user had typed into the other rows. Only the vertex that
 *          actually moved is over new ground. Structure is identical because
 *          the caller samples with densify = false, so no vertices appear.
 */
void restoreZExcept(FeatureGeometry &to, const FeatureGeometry &from,
                    int keepPart, int keepRing, int keepIndex)
{
    QVector<Part> &toParts = to.parts();
    const QVector<Part> &fromParts = from.parts();
    for (int p = 0; p < toParts.size() && p < fromParts.size(); ++p) {
        const int ringCount =
            std::min(toParts[p].holes.size(), fromParts.at(p).holes.size()) + 1;
        for (int r = 0; r < ringCount; ++r) {
            Ring &dst = (r == 0) ? toParts[p].exterior : toParts[p].holes[r - 1];
            const Ring &src = (r == 0) ? fromParts.at(p).exterior
                                       : fromParts.at(p).holes.at(r - 1);
            if (!src.hasZ() || !dst.hasZ() || src.z.size() != dst.z.size())
                continue;
            for (int i = 0; i < dst.z.size(); ++i) {
                if (p == keepPart && r == keepRing && i == keepIndex) continue;
                dst.z[i] = src.z.at(i);
            }
        }
    }
}

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

    // ----- Feature grid --------------------------------------------------
    // The attribute table for THIS layer. The main Attribute Table dock is
    // built around SWMMAttributeTableModel's compile-time ColumnSpec and a
    // SWMMModelLayer, so it cannot show a runtime-authored schema; this grid
    // lives with the layer that owns the schema instead.
    {
        auto *g = new QGroupBox(tr("Features"), page);
        auto *lay = new QVBoxLayout(g);

        m_featureTable = new QTableWidget(0, 1, g);
        m_featureTable->setObjectName(QStringLiteral("featureAttributeTable"));
        m_featureTable->verticalHeader()->setVisible(false);
        m_featureTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_featureTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_featureTable->setContextMenuPolicy(Qt::CustomContextMenu);
        m_featureTable->setMinimumHeight(140);
        m_featureTable->setToolTip(
            tr("Every feature in this layer. Selecting rows selects them on "
               "the map; open an edit session to change values or delete."));
        lay->addWidget(m_featureTable);

        auto *btns = new QHBoxLayout();
        m_deleteFeatBtn = new QPushButton(tr("Delete feature"), g);
        m_deleteFeatBtn->setToolTip(
            tr("Delete the selected features. Undoable — Del does the same."));
        btns->addWidget(m_deleteFeatBtn);
        btns->addStretch();
        m_featHintLabel = new QLabel(g);
        m_featHintLabel->setEnabled(false);
        btns->addWidget(m_featHintLabel);
        lay->addLayout(btns);

        vbox->addWidget(g);

        connect(m_featureTable, &QTableWidget::cellChanged,
                this, &FeatureLayerPanel::onFeatureCellChanged);
        connect(m_featureTable, &QTableWidget::itemSelectionChanged,
                this, &FeatureLayerPanel::onFeatureSelectionChanged);
        connect(m_featureTable, &QWidget::customContextMenuRequested,
                this, &FeatureLayerPanel::onFeatureTableContextMenu);
        connect(m_deleteFeatBtn, &QPushButton::clicked,
                this, &FeatureLayerPanel::onDeleteSelectedFeatureRows);

        // Del while the grid has focus. Scoped to the widget so it cannot
        // shadow the Features toolbar's own Del, which acts on the map
        // selection.
        auto *del = new QShortcut(QKeySequence::Delete, m_featureTable);
        del->setContext(Qt::WidgetWithChildrenShortcut);
        connect(del, &QShortcut::activated,
                this, &FeatureLayerPanel::onDeleteSelectedFeatureRows);
    }

    // ----- Vertex grid ----------------------------------------------------
    // Numeric coordinate entry for the selected feature. The map tools are
    // the fast way to move a vertex; this is the exact way. Both funnel
    // through EditFeatureGeometryCommand, so an edit made here is the same
    // undo step as the same edit made by dragging (CLAUDE.md §5.1).
    //
    // This reverses PLAN §4.4's "the property adapter shows per-vertex Z
    // read-only" at the user's request — see the 2026-09-14 amendment in
    // workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md.
    {
        auto *g = new QGroupBox(tr("Vertices"), page);
        auto *lay = new QVBoxLayout(g);

        m_vertexTable = new QTableWidget(0, VColCount, g);
        m_vertexTable->setObjectName(QStringLiteral("featureVertexTable"));
        m_vertexTable->verticalHeader()->setVisible(false);
        m_vertexTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_vertexTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_vertexTable->setMinimumHeight(140);
        m_vertexTable->setHorizontalHeaderLabels(
            {tr("Part"), tr("Ring"), tr("#"), tr("X"), tr("Y"), tr("Z")});
        m_vertexTable->setToolTip(
            tr("Coordinates of the selected feature, in the layer's own CRS. "
               "Ring 'exterior' is the outline; 'hole N' are interior rings. "
               "Open an edit session to type new values."));
        lay->addWidget(m_vertexTable);

        m_vertexHintLabel = new QLabel(g);
        m_vertexHintLabel->setWordWrap(true);
        m_vertexHintLabel->setEnabled(false);
        lay->addWidget(m_vertexHintLabel);

        vbox->addWidget(g);

        connect(m_vertexTable, &QTableWidget::cellChanged,
                this, &FeatureLayerPanel::onVertexCellChanged);
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
    // A feature write changes the counters AND the grid's contents.
    connect(m_active.data(), &FeatureLayer::featuresChanged,
            this, [this](const QVector<qint64> &) {
                refreshStatus();
                refreshFeatureTable();
            });
    // Opening or closing the session flips every value cell between
    // editable and read-only, so the grid is rebuilt rather than patched.
    connect(m_active.data(), &FeatureLayer::editingChanged,
            this, [this](bool) { refreshFeatureTable(); });
    // Map selection → grid rows. The inverse direction is
    // onFeatureSelectionChanged(); both funnel through setSelectedFeatureIds,
    // and the QSignalBlocker in each stops the pair from ping-ponging.
    connect(m_active.data(), &FeatureLayer::selectionChanged, this,
            [this](const QSet<long long> &) { onFeatureSelectionChangedFromLayer(); });
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
    m_featureTable->setEnabled(has);
    // The schema drives the grid's columns, so any refreshDetails (which is
    // what schemaChanged triggers) has to rebuild it too.
    refreshFeatureTable();

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

// ---------------------------------------------------------------------------
// Feature grid
// ---------------------------------------------------------------------------

void FeatureLayerPanel::refreshFeatureTable()
{
    // Every setItem below would otherwise come back through cellChanged and
    // be pushed as a user edit.
    const QSignalBlocker block(m_featureTable);
    m_suppressFeatureEdits = true;

    FeatureLayer *l = m_active.data();
    m_featureTable->clearContents();
    m_featureTable->setRowCount(0);

    if (!l) {
        m_featureTable->setColumnCount(1);
        m_featureTable->setHorizontalHeaderLabels({tr("Feature")});
        m_deleteFeatBtn->setEnabled(false);
        m_featHintLabel->clear();
        m_suppressFeatureEdits = false;
        // onFeatureSelectionChangedFromLayer() below is the usual route to the
        // vertex grid, and it bails with no layer — clear it here instead, or
        // it keeps showing the removed layer's coordinates.
        refreshVertexTable();
        return;
    }

    const Schema schema = l->schema();
    const bool editing = l->isEditing();

    // Column 0 is the feature id: identity, never editable.
    QStringList headers{tr("id")};
    for (const FieldDef &f : schema.fields()) headers << f.name;
    m_featureTable->setColumnCount(headers.size());
    m_featureTable->setHorizontalHeaderLabels(headers);
    m_featureTable->horizontalHeader()->setStretchLastSection(true);

    const QVector<FeatureId> ids = l->featureIds();
    m_featureTable->setRowCount(ids.size());
    for (int row = 0; row < ids.size(); ++row) {
        Feature f;
        if (!l->feature(ids.at(row), f)) continue;

        auto *idItem = new QTableWidgetItem(QString::number(f.id));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        // The id rides on the row so a sort or a partial refresh cannot
        // desynchronise row number from feature.
        idItem->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(f.id));
        m_featureTable->setItem(row, 0, idItem);

        for (int c = 0; c < schema.count(); ++c) {
            const FieldDef fd = schema.at(c);
            const QVariant v = f.attributes.value(fd.name);
            auto *item = new QTableWidgetItem(v.toString());
            if (fd.type == FieldType::Boolean) {
                item->setData(Qt::CheckStateRole,
                              v.toBool() ? Qt::Checked : Qt::Unchecked);
                item->setText(QString());
            }
            // Values are editable only inside an edit session.
            Qt::ItemFlags flags = item->flags();
            flags.setFlag(Qt::ItemIsEditable, editing);
            if (fd.type == FieldType::Boolean)
                flags.setFlag(Qt::ItemIsUserCheckable, editing);
            item->setFlags(flags);
            m_featureTable->setItem(row, c + 1, item);
        }
    }

    m_deleteFeatBtn->setEnabled(editing);
    m_featHintLabel->setText(editing ? QString()
                                     : tr("Read-only — turn on Edit Mode."));
    m_suppressFeatureEdits = false;

    // Reflect whatever is selected on the map into the grid.
    onFeatureSelectionChangedFromLayer();
}

void FeatureLayerPanel::onFeatureSelectionChangedFromLayer()
{
    FeatureLayer *l = m_active.data();
    if (!l || !m_featureTable) return;
    const QSignalBlocker block(m_featureTable);
    const QSet<long long> sel = l->selectedFeatureIds();
    m_featureTable->clearSelection();
    for (int row = 0; row < m_featureTable->rowCount(); ++row) {
        const QTableWidgetItem *idItem = m_featureTable->item(row, 0);
        if (idItem && sel.contains(idItem->data(Qt::UserRole).toLongLong()))
            m_featureTable->selectRow(row);
    }
    // The vertex grid shows whatever single feature is selected, so it
    // follows the same signal — from the map, from the grid, or from a
    // rebuild — instead of carrying a second notion of "current feature".
    refreshVertexTable();
}

void FeatureLayerPanel::onFeatureCellChanged(int row, int column)
{
    if (m_suppressFeatureEdits || column <= 0) return;   // 0 is the id
    FeatureLayer *l = m_active.data();
    if (!l || !m_canvas || !l->isEditing()) return;

    const QTableWidgetItem *idItem = m_featureTable->item(row, 0);
    QTableWidgetItem *cell = m_featureTable->item(row, column);
    if (!idItem || !cell) return;

    const auto id = static_cast<FeatureId>(idItem->data(Qt::UserRole).toLongLong());
    Feature f;
    if (!l->feature(id, f)) return;

    const Schema schema = l->schema();
    if (column - 1 >= schema.count()) return;
    const FieldDef fd = schema.at(column - 1);

    const QVariant typed =
        fd.type == FieldType::Boolean
            ? QVariant(cell->checkState() == Qt::Checked)
            : coerceToFieldType(QVariant(cell->text()), fd.type);

    QVariantMap next = f.attributes;
    next.insert(fd.name, typed);
    if (next == f.attributes) return;   // nothing actually moved

    auto *cmd = new map::EditFeatureAttributesCommand(l, id, f.attributes, next,
                                                 m_canvas.data());
    if (m_canvas->undoStack()) m_canvas->undoStack()->push(cmd);
    else                       { delete cmd; return; }
    if (!cmd->lastError().isEmpty()) emit message(cmd->lastError());

    // Re-read: coercion may have normalised what the user typed ("3.50" for
    // a Real becomes 3.5), and showing the raw text would be a lie.
    refreshFeatureTable();
}

void FeatureLayerPanel::onFeatureSelectionChanged()
{
    FeatureLayer *l = m_active.data();
    if (!l || m_suppressFeatureEdits) return;

    QSet<long long> ids;
    const auto rows = m_featureTable->selectionModel()
                          ? m_featureTable->selectionModel()->selectedRows()
                          : QModelIndexList();
    for (const QModelIndex &idx : rows)
        if (const QTableWidgetItem *item = m_featureTable->item(idx.row(), 0))
            ids.insert(item->data(Qt::UserRole).toLongLong());

    // setSelectedFeatureIds is the same entry point the Select map tool uses,
    // so the map highlight follows the grid without a second mechanism.
    l->setSelectedFeatureIds(ids);

    // selectionChanged only fires when the set actually changed; re-selecting
    // the same row must still repopulate the vertex grid.
    refreshVertexTable();
}

// ---------------------------------------------------------------------------
// Vertex grid
// ---------------------------------------------------------------------------

void FeatureLayerPanel::refreshVertexTable()
{
    if (!m_vertexTable) return;
    // A write from onVertexCellChanged() re-enters here through
    // featuresChanged while Qt is still delivering cellChanged. Rebuilding now
    // would destroy the item being committed; the handler queues one rebuild
    // when it returns instead.
    if (m_vertexEditInFlight) return;

    // Every setItem below would otherwise return through cellChanged as a
    // user edit, exactly as in refreshFeatureTable().
    const QSignalBlocker block(m_vertexTable);
    m_suppressVertexEdits = true;
    const auto unsuppress = qScopeGuard([this] { m_suppressVertexEdits = false; });

    // Keep the user's place: an edit rebuilds the grid, and a ring of 400
    // vertices is unusable if every keystroke scrolls back to the top.
    const qint64 previousId  = m_vertexFeatureId;
    const int    previousRow = m_vertexTable->currentRow();

    m_vertexTable->clearContents();
    m_vertexTable->setRowCount(0);
    m_vertexFeatureId = -1;

    FeatureLayer *l = m_active.data();
    if (!l) {
        m_vertexHintLabel->setText(tr("No feature layer selected."));
        return;
    }

    // Exactly one feature, or there is nothing unambiguous to show. The
    // selection is the single source of truth, so this works whether the
    // feature was picked in the grid above or on the map.
    const QSet<long long> sel = l->selectedFeatureIds();
    if (sel.size() != 1) {
        m_vertexHintLabel->setText(
            sel.isEmpty()
                ? tr("Select one feature to see its coordinates.")
                : tr("%n features selected — select exactly one to edit "
                     "coordinates.", nullptr, static_cast<int>(sel.size())));
        return;
    }

    const auto id = static_cast<FeatureId>(*sel.constBegin());
    Feature f;
    if (!l->feature(id, f)) {
        m_vertexHintLabel->setText(tr("Feature %1 could not be read.").arg(id));
        return;
    }
    m_vertexFeatureId = id;

    const bool editing  = l->isEditing();
    const bool threeD   = l->isThreeD();
    const QVector<Part> &parts = f.geometry.parts();

    // Six QTableWidgetItems per vertex: an imported boundary with tens of
    // thousands of vertices would freeze the dock for seconds to build a grid
    // nobody scrolls. Those are the geometries you edit on the map anyway.
    const int total = f.geometry.vertexCount();
    if (total > kMaxVertexRows) {
        m_vertexFeatureId = -1;
        m_vertexHintLabel->setText(
            tr("Feature %1 has %2 vertices — too many to list. Use the vertex "
               "tools on the map to edit it.").arg(id).arg(total));
        return;
    }

    m_vertexTable->setRowCount(total);
    int row = 0;
    for (int p = 0; p < parts.size(); ++p) {
        const Part &part = parts.at(p);
        // Ring 0 is the exterior; 1..n are the holes, in storage order.
        // NOTE: this is NOT the convention FeatureVertexRef uses in
        // include/map/tools/maptoolfeatureedit.h, where the exterior is -1 and
        // holes start at 0. The two addresses never meet — the grid resolves
        // its own rows — but do not copy one into the other.
        for (int r = 0; r <= part.holes.size(); ++r) {
            const Ring &ring = (r == 0) ? part.exterior : part.holes.at(r - 1);
            const bool zEditable = editing && threeD && ring.hasZ();

            for (int i = 0; i < ring.size(); ++i, ++row) {
                auto *partItem = new QTableWidgetItem(QString::number(p + 1));
                partItem->setFlags(partItem->flags() & ~Qt::ItemIsEditable);
                partItem->setData(kPartRole,  p);
                partItem->setData(kRingRole,  r);
                partItem->setData(kIndexRole, i);
                m_vertexTable->setItem(row, VColPart, partItem);

                auto *ringItem = new QTableWidgetItem(
                    r == 0 ? tr("exterior") : tr("hole %1").arg(r));
                ringItem->setFlags(ringItem->flags() & ~Qt::ItemIsEditable);
                m_vertexTable->setItem(row, VColRing, ringItem);

                auto *idxItem = new QTableWidgetItem(QString::number(i + 1));
                idxItem->setFlags(idxItem->flags() & ~Qt::ItemIsEditable);
                m_vertexTable->setItem(row, VColIndex, idxItem);

                const QPointF pt = ring.pts.at(i);
                auto *xItem = new QTableWidgetItem(formatOrdinate(pt.x()));
                Qt::ItemFlags xf = xItem->flags();
                xf.setFlag(Qt::ItemIsEditable, editing);
                xItem->setFlags(xf);
                m_vertexTable->setItem(row, VColX, xItem);

                auto *yItem = new QTableWidgetItem(formatOrdinate(pt.y()));
                Qt::ItemFlags yf = yItem->flags();
                yf.setFlag(Qt::ItemIsEditable, editing);
                yItem->setFlags(yf);
                m_vertexTable->setItem(row, VColY, yItem);

                // A 2D ring has no Z to show; an unsampled one has NaN, which
                // is displayed blank rather than as 0 — see FeatureLayer's
                // "NaN is the honest value" rule.
                const double zv = ring.zAt(i);
                QString zText = QStringLiteral("—");
                if (ring.hasZ()) zText = std::isnan(zv) ? QString() : formatOrdinate(zv);
                auto *zItem = new QTableWidgetItem(zText);
                Qt::ItemFlags zf = zItem->flags();
                zf.setFlag(Qt::ItemIsEditable, zEditable);
                zItem->setFlags(zf);
                m_vertexTable->setItem(row, VColZ, zItem);
            }
        }
    }
    m_vertexTable->setRowCount(row);
    m_vertexTable->horizontalHeader()->setStretchLastSection(true);

    if (previousId == m_vertexFeatureId) {
        // Same feature, so the rows mean the same thing: put the cursor back.
        if (previousRow >= 0 && previousRow < m_vertexTable->rowCount())
            m_vertexTable->setCurrentCell(previousRow, VColX,
                                          QItemSelectionModel::NoUpdate);
    } else {
        // A different feature: widths are re-fitted once, not on every edit,
        // so a column the user widened survives their typing.
        m_vertexTable->resizeColumnsToContents();
    }

    QStringList hints;
    if (row == 0)
        hints << tr("This feature has no vertices.");
    if (!editing)
        hints << tr("Read-only — turn on Edit Mode.");
    if (!threeD) {
        hints << tr("2D layer — no Z is stored.");
    } else {
        const ZPolicy zp = l->zPolicy();
        if (zp.source == ZPolicy::Source::Raster
            || zp.source == ZPolicy::Source::Mesh) {
            hints << tr("Z is sampled from this layer's Z source: a typed Z is "
                        "kept, but the next resample overwrites it.");
            if (zp.resampleOnEdit)
                hints << tr("Changing X or Y re-samples that vertex's Z.");
        }
    }
    m_vertexHintLabel->setText(hints.join(QLatin1Char(' ')));
}

void FeatureLayerPanel::onVertexCellChanged(int row, int column)
{
    if (m_suppressVertexEdits) return;
    if (column != VColX && column != VColY && column != VColZ) return;

    // Every path below ends by re-reading the geometry: the typed text may have
    // been rejected, rounded, or followed by a Z resample. The rebuild is
    // QUEUED, never immediate — clearContents() here would delete the item Qt
    // is still committing, which releases the open editor mid-flight and costs
    // the table its focus. The flag also parks the rebuild that the write's own
    // featuresChanged would trigger, so one edit means one rebuild.
    m_vertexEditInFlight = true;
    const auto finish = qScopeGuard([this] {
        m_vertexEditInFlight = false;
        QMetaObject::invokeMethod(this, &FeatureLayerPanel::refreshVertexTable,
                                  Qt::QueuedConnection);
    });

    FeatureLayer *l = m_active.data();
    if (!l || !m_canvas || !l->isEditing() || m_vertexFeatureId < 0) return;

    const QTableWidgetItem *addr = m_vertexTable->item(row, VColPart);
    const QTableWidgetItem *cell = m_vertexTable->item(row, column);
    if (!addr || !cell) return;

    const auto id = static_cast<FeatureId>(m_vertexFeatureId);
    Feature f;
    if (!l->feature(id, f)) return;

    const FeatureGeometry oldGeom = f.geometry;
    FeatureGeometry next = oldGeom;

    // Bounds-check the row's stashed address against the geometry we just
    // read, which a concurrent map edit may have shortened. (An insert that
    // kept the indices in range would address the wrong vertex, but any such
    // write emits featuresChanged and rebuilds this grid first.)
    const int p = addr->data(kPartRole).toInt();
    const int r = addr->data(kRingRole).toInt();
    const int i = addr->data(kIndexRole).toInt();
    if (p < 0 || p >= next.parts().size()) return;
    Part &part = next.parts()[p];
    if (r < 0 || r > part.holes.size())    return;
    Ring &ring = (r == 0) ? part.exterior : part.holes[r - 1];
    if (i < 0 || i >= ring.size())         return;

    const QString text = cell->text().trimmed();

    if (column == VColZ) {
        if (!l->isThreeD() || !ring.hasZ()) return;
        // Blank clears the sample back to NaN, the inverse of how an
        // unsampled Z is displayed.
        double zv = std::numeric_limits<double>::quiet_NaN();
        if (!text.isEmpty()) {
            bool ok = false;
            zv = text.toDouble(&ok);
            if (!ok) {
                emit message(tr("\"%1\" is not a number.").arg(text));
                return;
            }
        }
        const double cur = ring.z.at(i);
        if (std::isnan(zv) ? std::isnan(cur) : (zv == cur)) return;
        ring.z[i] = zv;
    } else {
        bool ok = false;
        const double v = text.toDouble(&ok);
        if (!ok) {
            emit message(tr("\"%1\" is not a number.").arg(text));
            return;
        }
        // The ordinate the user did NOT touch is read from the geometry, never
        // from its sibling cell: that cell holds a 6-decimal rendering, and
        // taking it back would quietly quantise a coordinate nobody edited.
        const QPointF cur = ring.pts.at(i);
        // Compare the edited ordinate exactly. QPointF::operator== is a FUZZY
        // compare, whose relative tolerance at a northing of 5e6 is ~5e-6 —
        // coarser than the 1e-6 this grid invites the user to type, so a real
        // edit would vanish with no message.
        if (v == ((column == VColX) ? cur.x() : cur.y())) return;

        ring.moveVertex(i, (column == VColX) ? QPointF(v, cur.y())
                                             : QPointF(cur.x(), v));

        // Moving a vertex can break the ring — self-intersection, or a hole
        // escaping its exterior. Same gate the draw tools apply on commit.
        // (Like them, orientation is left alone: validate() does not check it.)
        QString reason;
        if (!next.validate(l->geometryType(), &reason)) {
            emit message(tr("Cannot move that vertex: %1").arg(reason));
            return;                 // the queued rebuild restores the cell
        }

        // A moved vertex is over new ground, so its Z is re-sampled when the
        // policy says so — the same rule maptoolfeatureedit.cpp applies after a
        // drag. densify=false: no vertices are being added.
        if (l->isThreeD() && l->zPolicy().resampleOnEdit) {
            const FeatureGeometry typedZ = next;
            l->sampleZ(next, m_canvas.data(), /*densify=*/false);
            restoreZExcept(next, typedZ, p, r, i);
        }
    }

    // Not mergeable: a typed coordinate is a discrete edit that should stand
    // as its own undo step, unlike the drag it shares a command with.
    auto *cmd = new map::EditFeatureGeometryCommand(
        l, id, oldGeom, next,
        column == VColZ ? tr("Edit vertex Z") : tr("Edit vertex position"),
        /*mergeable=*/false, m_canvas.data());
    if (m_canvas->undoStack()) m_canvas->undoStack()->push(cmd);
    else                       { delete cmd; return; }
    if (!cmd->lastError().isEmpty()) emit message(cmd->lastError());
}

void FeatureLayerPanel::onDeleteSelectedFeatureRows()
{
    FeatureLayer *l = m_active.data();
    if (!l || !m_canvas) return;
    if (!l->isEditing()) {
        emit message(tr("Turn on Edit Mode before deleting features."));
        return;
    }

    QVector<FeatureId> ids;
    const auto rows = m_featureTable->selectionModel()
                          ? m_featureTable->selectionModel()->selectedRows()
                          : QModelIndexList();
    for (const QModelIndex &idx : rows)
        if (const QTableWidgetItem *item = m_featureTable->item(idx.row(), 0))
            ids.append(static_cast<FeatureId>(item->data(Qt::UserRole).toLongLong()));
    if (ids.isEmpty()) {
        emit message(tr("Select one or more rows to delete."));
        return;
    }
    std::sort(ids.begin(), ids.end());

    auto *cmd = new map::DeleteFeaturesCommand(l, ids, m_canvas.data());
    if (m_canvas->undoStack()) m_canvas->undoStack()->push(cmd);
    else                       { delete cmd; return; }
    if (!cmd->lastError().isEmpty()) emit message(cmd->lastError());
    else emit message(tr("Deleted %n feature(s).", nullptr, ids.size()));
}

void FeatureLayerPanel::onFeatureTableContextMenu(const QPoint &pos)
{
    FeatureLayer *l = m_active.data();
    if (!l) return;

    const bool haveRows =
        m_featureTable->selectionModel() &&
        !m_featureTable->selectionModel()->selectedRows().isEmpty();

    QMenu menu(this);
    QAction *del = menu.addAction(tr("Delete feature(s)"));
    del->setShortcut(QKeySequence::Delete);
    del->setEnabled(l->isEditing() && haveRows);
    if (!l->isEditing())
        menu.addAction(tr("(turn on Edit Mode to change this layer)"))
            ->setEnabled(false);

    if (menu.exec(m_featureTable->viewport()->mapToGlobal(pos)) == del)
        onDeleteSelectedFeatureRows();
}

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
