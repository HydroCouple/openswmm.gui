/*!
 * \file   meshgenerationdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * Slice AU.4 — see meshgenerationdialog.h for design.
 */
#include "ui/dialogs/meshgenerationdialog.h"

#include "swmmvisprojectwindow.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "layers/swmmmodellayer.h"
#include "layers/gisrasterlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"

#include "mesh/meshgenerator.h"
#include "mesh/meshresult.h"
#include "mesh/dtmsampler.h"
#include "mesh/inpmeshwriter.h"

#include "layers/gisvectorlayer.h"

#include <gdal_priv.h>
#include <ogr_feature.h>
#include <ogr_geometry.h>
#include <ogrsf_frmts.h>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

MeshGenerationDialog::MeshGenerationDialog(SWMMVisProjectWindow *pw,
                                           QWidget *parent)
    : QDialog(parent),
      m_pw(pw)
{
    setWindowTitle(tr("Generate 2D Mesh"));
    resize(560, 620);
    buildUi();
    seedDefaults();
}

void MeshGenerationDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);

    auto *header = new QLabel(tr(
        "Generate a 2D triangular mesh from the active SWMM network and a "
        "Digital Terrain Model. Junctions / outfalls / storage become Steiner "
        "vertices tagged with their node id; conduits become constraint "
        "segments; subcatchments become triangle regions tagged with their id "
        "(Slice AU.3 tagging rules) — the resulting mesh aligns with 1D "
        "coupling points by construction."), this);
    header->setWordWrap(true);
    outer->addWidget(header);

    // Body lives inside a scroll area so the dialog stays at a sane
    // height as more sections are added — instead of stretching every
    // group over the available space.
    auto *scroll  = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *body    = new QVBoxLayout(content);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    // ── Sources ─────────────────────────────────────────────────────
    auto *sourcesGroup = new QGroupBox(tr("Sources"), content);
    auto *sourcesForm  = new QFormLayout(sourcesGroup);

    m_dtmCombo = new QComboBox(sourcesGroup);
    m_dtmCombo->setToolTip(tr("DTM raster used to sample vertex elevations. "
                               "Add via View → Add Raster Data first."));
    sourcesForm->addRow(tr("&DTM raster:"), m_dtmCombo);

    m_domainLabel = new QLabel(sourcesGroup);
    m_domainLabel->setStyleSheet(QStringLiteral("color: gray;"));
    sourcesForm->addRow(tr("Domain:"), m_domainLabel);

    body->addWidget(sourcesGroup);

    // ── Auxiliary feature-layer constraints (optional) ──────────────
    // Boundary is single-select (one polygon defines the domain).
    // Constraining points + lines are multi-select via checkable list
    // widgets — multiple layers can contribute Steiner points / segments
    // to the same mesh (e.g. survey pins + monitoring stations + a
    // building-footprint shapefile).
    auto *auxGroup = new QGroupBox(tr("Auxiliary feature layers (optional)"), content);
    auto *auxLay   = new QVBoxLayout(auxGroup);

    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("&Boundary polygon:"), auxGroup));
        m_boundaryLayerCombo = new QComboBox(auxGroup);
        m_boundaryLayerCombo->setToolTip(tr(
            "Polygon vector layer whose first feature defines the meshing "
            "boundary. When (none), the mesh domain falls back to the SWMM "
            "model's bounding rectangle + 5%."));
        row->addWidget(m_boundaryLayerCombo, 1);
        auxLay->addLayout(row);
    }

    auxLay->addWidget(new QLabel(tr("Constraining &points (check to include):"), auxGroup));
    m_pointLayersList = new QListWidget(auxGroup);
    m_pointLayersList->setToolTip(tr(
        "Multi-select. Every feature in each checked point layer is added "
        "as a Steiner point the mesh must include (e.g. survey pins, "
        "monitoring stations)."));
    m_pointLayersList->setMaximumHeight(110);
    m_pointLayersList->setSelectionMode(QAbstractItemView::NoSelection);
    auxLay->addWidget(m_pointLayersList);

    auxLay->addWidget(new QLabel(tr("Constraining &lines (check to include):"), auxGroup));
    m_lineLayersList = new QListWidget(auxGroup);
    m_lineLayersList->setToolTip(tr(
        "Multi-select. Every feature in each checked line layer becomes a "
        "constraint segment the mesh must honour as edges (e.g. levees, "
        "kerbs, building footprints)."));
    m_lineLayersList->setMaximumHeight(110);
    m_lineLayersList->setSelectionMode(QAbstractItemView::NoSelection);
    auxLay->addWidget(m_lineLayersList);

    body->addWidget(auxGroup);

    // ── Constraints ─────────────────────────────────────────────────
    auto *constraintsGroup = new QGroupBox(tr("Constraints (1D ↔ 2D coupling)"), content);
    auto *constraintsLay   = new QVBoxLayout(constraintsGroup);

    m_includeJunctions = new QCheckBox(tr("Include junctions / outfalls / storage as Steiner points (TAG = node id)"), constraintsGroup);
    m_includeConduits  = new QCheckBox(tr("Include conduits as constraint segments (segment marker = conduit id)"), constraintsGroup);
    m_includeSubcatch  = new QCheckBox(tr("Include subcatchments as triangle regions (TAG = subcatchment id)"), constraintsGroup);
    constraintsLay->addWidget(m_includeJunctions);
    constraintsLay->addWidget(m_includeConduits);
    constraintsLay->addWidget(m_includeSubcatch);

    body->addWidget(constraintsGroup);

    // ── Quality ─────────────────────────────────────────────────────
    auto *qualityGroup = new QGroupBox(tr("Mesh quality"), content);
    // Cap the natural height — without this the form layout grows to
    // fill any spare space when other sections collapse.
    qualityGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *qualityForm  = new QFormLayout(qualityGroup);
    qualityForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

    m_maxAreaSpin = new QDoubleSpinBox(qualityGroup);
    m_maxAreaSpin->setRange(0.0, 1e12);
    m_maxAreaSpin->setDecimals(2);
    m_maxAreaSpin->setSpecialValueText(tr("(no cap)"));
    m_maxAreaSpin->setToolTip(tr("Upper bound on triangle area (map units²). "
                                  "0 = no cap; lets Triangle's quality switch "
                                  "alone control density."));
    qualityForm->addRow(tr("Max triangle area:"), m_maxAreaSpin);

    m_minAngleSpin = new QDoubleSpinBox(qualityGroup);
    m_minAngleSpin->setRange(0.0, 33.0);
    m_minAngleSpin->setDecimals(1);
    m_minAngleSpin->setSuffix(QStringLiteral(" °"));
    m_minAngleSpin->setToolTip(tr("Minimum triangle angle. 0–33° reliable; "
                                   "above 33° Triangle may not terminate."));
    qualityForm->addRow(tr("Min angle:"), m_minAngleSpin);

    m_maxSteinerSpin = new QSpinBox(qualityGroup);
    m_maxSteinerSpin->setRange(-1, 10'000'000);
    m_maxSteinerSpin->setSpecialValueText(tr("(unlimited)"));
    m_maxSteinerSpin->setToolTip(tr("Hard cap on Steiner refinement vertices. "
                                     "-1 = unlimited."));
    qualityForm->addRow(tr("Max Steiner points:"), m_maxSteinerSpin);

    m_allowSteiner = new QCheckBox(tr("Allow Steiner refinement on boundary"), qualityGroup);
    qualityForm->addRow(QString(), m_allowSteiner);

    body->addWidget(qualityGroup);

    // ── Roughness ───────────────────────────────────────────────────
    auto *roughnessGroup = new QGroupBox(tr("Roughness (Manning's n)"), content);
    roughnessGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *roughnessLay   = new QVBoxLayout(roughnessGroup);

    auto *roughGroup = new QButtonGroup(roughnessGroup);
    m_manningsConstant    = new QRadioButton(tr("Constant value:"), roughnessGroup);
    m_manningsCategorical = new QRadioButton(tr("Categorical raster (planned — Slice AU follow-up)"), roughnessGroup);
    m_manningsField       = new QRadioButton(tr("Shapefile field (planned — Slice AU follow-up)"), roughnessGroup);
    roughGroup->addButton(m_manningsConstant);
    roughGroup->addButton(m_manningsCategorical);
    roughGroup->addButton(m_manningsField);
    m_manningsCategorical->setEnabled(false);
    m_manningsField->setEnabled(false);

    auto *constRow = new QHBoxLayout;
    m_manningsValueSpin = new QDoubleSpinBox(roughnessGroup);
    m_manningsValueSpin->setRange(0.001, 1.0);
    m_manningsValueSpin->setDecimals(4);
    m_manningsValueSpin->setSingleStep(0.005);
    constRow->addWidget(m_manningsConstant);
    constRow->addWidget(m_manningsValueSpin);
    constRow->addStretch();
    roughnessLay->addLayout(constRow);
    roughnessLay->addWidget(m_manningsCategorical);
    roughnessLay->addWidget(m_manningsField);

    body->addWidget(roughnessGroup);

    // ── Output ──────────────────────────────────────────────────────
    auto *outputGroup = new QGroupBox(tr("Output"), content);
    outputGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *outputLay   = new QVBoxLayout(outputGroup);

    m_outputExternal = new QRadioButton(tr("External .2dm (default — referenced via [2D_MESH_FILE])"), outputGroup);
    m_outputInline   = new QRadioButton(tr("Inline in .inp"), outputGroup);
    auto *outGroup = new QButtonGroup(outputGroup);
    outGroup->addButton(m_outputExternal);
    outGroup->addButton(m_outputInline);
    outputLay->addWidget(m_outputExternal);
    outputLay->addWidget(m_outputInline);

    auto *pathRow = new QHBoxLayout;
    m_meshPathEdit = new QLineEdit(outputGroup);
    m_meshPathEdit->setPlaceholderText(tr("(default: <project>.2dm)"));
    m_browseMeshBtn = new QPushButton(tr("Browse…"), outputGroup);
    pathRow->addWidget(new QLabel(tr("Mesh file:")));
    pathRow->addWidget(m_meshPathEdit, 1);
    pathRow->addWidget(m_browseMeshBtn);
    outputLay->addLayout(pathRow);

    body->addWidget(outputGroup);

    // Trailing stretch so the groups stack at their natural sizes
    // instead of distributing leftover space.
    body->addStretch(1);

    // ── Progress bar (sits below scroll, above buttons) ─────────────
    m_progressLabel = new QLabel(this);
    m_progressLabel->setVisible(false);
    m_progressLabel->setStyleSheet(QStringLiteral("color: gray;"));
    outer->addWidget(m_progressLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setVisible(false);
    outer->addWidget(m_progressBar);

    // ── Buttons ─────────────────────────────────────────────────────
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                     this);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Generate"));
    outer->addWidget(bb);

    connect(m_browseMeshBtn, &QPushButton::clicked, this,
            &MeshGenerationDialog::onBrowseMeshPath);
    connect(bb, &QDialogButtonBox::accepted, this,
            &MeshGenerationDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto refreshPathEnable = [this]() {
        const bool ext = m_outputExternal->isChecked();
        m_meshPathEdit->setEnabled(ext);
        m_browseMeshBtn->setEnabled(ext);
    };
    connect(m_outputExternal, &QRadioButton::toggled, this, refreshPathEnable);
    connect(m_outputInline,   &QRadioButton::toggled, this, refreshPathEnable);
    refreshPathEnable();
}

void MeshGenerationDialog::seedDefaults()
{
    m_includeJunctions->setChecked(true);
    m_includeConduits->setChecked(true);
    m_includeSubcatch->setChecked(true);

    m_maxAreaSpin->setValue(0.0);          // no global cap by default
    m_minAngleSpin->setValue(28.0);
    m_maxSteinerSpin->setValue(-1);
    m_allowSteiner->setChecked(true);

    m_manningsConstant->setChecked(true);
    m_manningsValueSpin->setValue(0.035);

    m_outputExternal->setChecked(true);

    populateRasterCombo();

    // Domain summary — read the active model layer's extent (layer CRS).
    if (m_pw && m_pw->modelLayer())
    {
        const MapExtent ext = m_pw->modelLayer()->extent();
        if (ext.isValid())
            m_domainLabel->setText(tr("model extent [%1, %2 → %3, %4]")
                                       .arg(ext.xMin(), 0, 'f', 2)
                                       .arg(ext.yMin(), 0, 'f', 2)
                                       .arg(ext.xMax(), 0, 'f', 2)
                                       .arg(ext.yMax(), 0, 'f', 2));
        else
            m_domainLabel->setText(tr("(model extent not available)"));
    }

    // Default output mesh path = <inpDir>/<basename>.2dm
    if (m_pw && m_pw->modelLayer())
    {
        const QString inp = m_pw->modelLayer()->modelFilePath();
        if (!inp.isEmpty())
        {
            const QFileInfo fi(inp);
            m_meshPathEdit->setText(
                fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".2dm")));
        }
    }
}

void MeshGenerationDialog::populateRasterCombo()
{
    if (!m_pw || !m_pw->canvas()) return;

    // ── DTM raster combo ───────────────────────────────────────────
    m_dtmCombo->clear();
    const auto &layers = m_pw->canvas()->layers();
    for (auto *layer : layers)
    {
        if (auto *r = qobject_cast<GISRasterLayer *>(layer))
            m_dtmCombo->addItem(r->name(), QVariant::fromValue<void *>(r));
    }
    if (m_dtmCombo->count() == 0)
        m_dtmCombo->addItem(
            tr("(no raster layers loaded — add one via View → Add Raster Data)"),
            QVariant::fromValue<void *>(nullptr));

    // ── Auxiliary feature-layer widgets ────────────────────────────
    // Boundary stays a single-select combo. Three flavours:
    //   1. "(none)" → use SWMM model bbox + 5% (default fallback).
    //   2. "Use SWMM subcatchment polygons" → take union of all
    //      subcatchment polygons in the active model. The sentinel is
    //      stored as the special pointer value 0x1 in itemData.
    //   3. Any GISVectorLayer → first polygon feature wins.
    // Point + line lists are checkable (multi-select).
    m_boundaryLayerCombo->clear();
    m_boundaryLayerCombo->addItem(tr("(none)"),
                                   QVariant::fromValue<void *>(nullptr));
    m_boundaryLayerCombo->addItem(tr("Use SWMM subcatchment polygons"),
                                   QVariant::fromValue<void *>(
                                       reinterpret_cast<void *>(0x1)));
    for (auto *layer : layers)
        if (auto *v = qobject_cast<GISVectorLayer *>(layer))
            m_boundaryLayerCombo->addItem(v->name(),
                                           QVariant::fromValue<void *>(v));

    auto fillVectorList = [&](QListWidget *list) {
        list->clear();
        for (auto *layer : layers)
        {
            if (auto *v = qobject_cast<GISVectorLayer *>(layer))
            {
                auto *item = new QListWidgetItem(v->name(), list);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Unchecked);
                item->setData(Qt::UserRole, QVariant::fromValue<void *>(v));
            }
        }
        if (list->count() == 0)
            list->addItem(tr("(no vector layers loaded — add via View → Add Vector Data)"));
    };
    fillVectorList(m_pointLayersList);
    fillVectorList(m_lineLayersList);
}

void MeshGenerationDialog::onBrowseMeshPath()
{
    const QString picked = QFileDialog::getSaveFileName(
        this, tr("Choose Mesh File"),
        m_meshPathEdit->text(),
        tr("OpenSWMM 2D Mesh (*.2dm);;All files (*)"));
    if (!picked.isEmpty())
        m_meshPathEdit->setText(picked);
}

void MeshGenerationDialog::onAccept()
{
    QString err;
    if (!runPipeline(&err))
    {
        QMessageBox::critical(this, tr("Mesh generation failed"),
            err.isEmpty() ? tr("Unknown error.") : err);
        return;
    }
    accept();
}

void MeshGenerationDialog::onApply()
{
    QString err;
    if (!runPipeline(&err))
    {
        QMessageBox::critical(this, tr("Mesh generation failed"),
            err.isEmpty() ? tr("Unknown error.") : err);
    }
}

bool MeshGenerationDialog::runPipeline(QString *errorOut)
{
    auto fail = [&](const QString &m) -> bool {
        if (errorOut) *errorOut = m;
        if (m_progressBar)   m_progressBar->setVisible(false);
        if (m_progressLabel) m_progressLabel->setVisible(false);
        return false;
    };

    auto progress = [&](int pct, const QString &msg) {
        if (!m_progressBar) return;
        m_progressBar->setVisible(true);
        m_progressLabel->setVisible(true);
        m_progressLabel->setText(msg);
        m_progressBar->setValue(pct);
        QApplication::processEvents();
    };

    if (!m_pw || !m_pw->modelLayer() || !m_pw->modelLayer()->engine())
        return fail(tr("No active SWMM project."));
    SWMMModelLayer *layer = m_pw->modelLayer();

    const QString inpPath = layer->modelFilePath();
    if (inpPath.isEmpty())
        return fail(tr("Save the project to a real .inp first (File → Save As)."));

    const MapExtent modelExt = layer->extent();
    if (!modelExt.isValid())
        return fail(tr("Model has no spatial extent — add at least one node first."));

    progress(5, tr("Building input PSLG…"));

    // ── Domain ──────────────────────────────────────────────────────
    // Three options driven by the boundary combo's stored pointer:
    //   nullptr (sentinel "(none)")         → model bbox + 5% margin
    //   reinterpret_cast<void*>(0x1)        → union of SWMM subcatchments
    //                                         (may yield several disjoint
    //                                         polygons → all kept)
    //   GISVectorLayer*                     → every polygon / multi-
    //                                         polygon feature in the layer
    QVector<QPolygonF> domains;
    void *boundaryPtr = m_boundaryLayerCombo->currentData().value<void *>();
    void * const kSubcatchSentinel = reinterpret_cast<void *>(0x1);

    auto pushOgrPolygon = [&](const OGRPolygon *poly) {
        if (!poly) return;
        const OGRLinearRing *ring = poly->getExteriorRing();
        if (!ring || ring->getNumPoints() < 3) return;
        QPolygonF qp;
        qp.reserve(ring->getNumPoints());
        for (int i = 0; i < ring->getNumPoints(); ++i)
            qp << QPointF(ring->getX(i), ring->getY(i));
        domains.append(qp);
    };
    auto walkOgrGeom = [&](const OGRGeometry *g) {
        if (!g) return;
        const auto gt = wkbFlatten(g->getGeometryType());
        if (gt == wkbPolygon)
            pushOgrPolygon(g->toPolygon());
        else if (gt == wkbMultiPolygon)
        {
            const auto *mp = g->toMultiPolygon();
            for (int i = 0; i < mp->getNumGeometries(); ++i)
                pushOgrPolygon(mp->getGeometryRef(i)->toPolygon());
        }
    };

    if (boundaryPtr == kSubcatchSentinel)
    {
        // Build an OGRMultiPolygon from every cached subcatchment, then
        // optionally run UnaryUnion to merge touching / overlapping
        // polygons into clean disjoint regions. UnaryUnion requires GEOS
        // support in the linked GDAL — when GEOS is missing or the
        // operation fails (degenerate polygons, etc.) we fall back to
        // emitting the raw subcatchment polygons as independent boundary
        // rings. Triangle handles the disjoint-ring case natively;
        // overlapping subcatchments still work as long as their boundaries
        // share endpoints (the generator's snap-and-dedupe collapses
        // coincident vertices).
        QVector<QVector<QPointF>> rawSubcatches;
        OGRMultiPolygon mp;
        for (int i = 0; i < layer->cachedSubcatchCount(); ++i)
        {
            auto verts = layer->cachedSubcatchVertices(i);
            if (verts.size() < 3) continue;
            rawSubcatches.append(verts);

            OGRPolygon poly;
            OGRLinearRing ring;
            for (const QPointF &p : verts)
                ring.addPoint(p.x(), p.y());
            if (verts.first() != verts.last())
                ring.addPoint(verts.first().x(), verts.first().y());
            poly.addRing(&ring);
            mp.addGeometry(&poly);
        }
        if (rawSubcatches.isEmpty())
            return fail(tr("No subcatchment polygons found in the model."));

        OGRGeometry *unioned = mp.UnaryUnion();
        if (unioned)
        {
            walkOgrGeom(unioned);
            OGRGeometryFactory::destroyGeometry(unioned);
        }
        if (domains.isEmpty())
        {
            // GEOS unavailable / failed — fall back to raw polygons.
            // Each subcatchment becomes its own boundary ring. The
            // mesh generator's snap-and-dedupe (1e-7 tolerance) merges
            // coincident vertices where adjacent subcatchments share
            // edges, so the joint boundary still triangulates cleanly.
            for (const auto &v : rawSubcatches)
                domains.append(QPolygonF(v));
        }
    }
    else if (auto *boundaryLayer = static_cast<GISVectorLayer *>(boundaryPtr))
    {
        // Walk EVERY polygon / multi-polygon feature in the layer — a
        // single shapefile can carry multiple disjoint catchment polygons.
        if (auto *ol = boundaryLayer->ogrLayer())
        {
            ol->ResetReading();
            OGRFeature *f = nullptr;
            while ((f = ol->GetNextFeature()) != nullptr)
            {
                walkOgrGeom(f->GetGeometryRef());
                OGRFeature::DestroyFeature(f);
            }
        }
    }
    if (domains.isEmpty())
    {
        const double margin = 0.05;
        const double dx     = modelExt.width()  * margin;
        const double dy     = modelExt.height() * margin;
        QPolygonF box;
        box << QPointF(modelExt.xMin() - dx, modelExt.yMin() - dy)
            << QPointF(modelExt.xMax() + dx, modelExt.yMin() - dy)
            << QPointF(modelExt.xMax() + dx, modelExt.yMax() + dy)
            << QPointF(modelExt.xMin() - dx, modelExt.yMax() + dy);
        domains.append(box);
    }

    // ── Generator setup ─────────────────────────────────────────────
    mesh::MeshGenerator g;
    g.setDomains(domains);

    // Each tagged input gets a unique marker int (Triangle preserves it
    // through to output). 0 is reserved (no tag); 1 is the boundary
    // marker the generator sets internally; user markers start at 100.
    int nextMarker = 100;
    QHash<int, QString> nodeMarkerToTag;   // for [2D_VERTEX_NODE_MAP]
    QHash<int, QString> edgeMarkerToTag;   // for future [2D_BOUNDARY_CONDITIONS]

    // Steiner vertices — junctions / outfalls / storage / dividers.
    if (m_includeJunctions->isChecked())
    {
        for (int c = SWMMModelLayer::CatJunctions; c <= SWMMModelLayer::CatDividers; ++c)
        {
            const auto cat = static_cast<SWMMModelLayer::Category>(c);
            for (int row = 0; row < layer->categoryCount(cat); ++row)
            {
                const QString name = layer->objectNameAt(cat, row);
                if (name.isEmpty()) continue;
                const int idx = layer->nodeIndex(name);
                if (idx < 0) continue;
                double x = 0, y = 0;
                if (!layer->cachedNodeCoord(idx, &x, &y)) continue;
                mesh::SteinerPoint sp;
                sp.xy     = QPointF(x, y);
                sp.marker = nextMarker;
                sp.tag    = name;
                g.addSteinerPoint(sp);
                nodeMarkerToTag.insert(nextMarker, name);
                ++nextMarker;
            }
        }
    }

    // Constraint segments — conduits (and other link types) as polylines.
    if (m_includeConduits->isChecked())
    {
        for (int c = SWMMModelLayer::CatConduits; c <= SWMMModelLayer::CatOutlets; ++c)
        {
            const auto cat = static_cast<SWMMModelLayer::Category>(c);
            for (int row = 0; row < layer->categoryCount(cat); ++row)
            {
                const QString name = layer->objectNameAt(cat, row);
                if (name.isEmpty()) continue;
                const int idx = layer->linkIndex(name);
                if (idx < 0) continue;
                const QVector<QPointF> path = layer->cachedLinkPolyline(idx);
                if (path.size() < 2) continue;
                mesh::ConstraintSegment cs;
                cs.path   = path;
                cs.marker = nextMarker;
                cs.tag    = name;
                g.addConstraintSegment(cs);
                edgeMarkerToTag.insert(nextMarker, name);
                ++nextMarker;
            }
        }
    }

    progress(20, tr("Adding constraining points…"));

    // ── Auxiliary feature-layer constraints ─────────────────────────
    // Multi-layer iteration: every checked layer in the points list
    // contributes its features as Steiner points, every checked layer
    // in the lines list contributes constraint segments. No SWMM tag
    // on these — they're user-supplied auxiliary geometry, not 1D
    // coupling glue.
    for (int i = 0; i < m_pointLayersList->count(); ++i)
    {
        auto *item = m_pointLayersList->item(i);
        if (!item || item->checkState() != Qt::Checked) continue;
        auto *vp = static_cast<GISVectorLayer *>(
            item->data(Qt::UserRole).value<void *>());
        if (!vp || !vp->ogrLayer()) continue;

        OGRLayer *ol = vp->ogrLayer();
        ol->ResetReading();
        OGRFeature *f = nullptr;
        while ((f = ol->GetNextFeature()) != nullptr)
        {
            if (auto *geom = f->GetGeometryRef())
            {
                const auto gt = wkbFlatten(geom->getGeometryType());
                if (gt == wkbPoint)
                {
                    auto *p = geom->toPoint();
                    mesh::SteinerPoint sp;
                    sp.xy     = QPointF(p->getX(), p->getY());
                    sp.marker = 0;
                    g.addSteinerPoint(sp);
                }
                else if (gt == wkbMultiPoint)
                {
                    const auto *mp = geom->toMultiPoint();
                    for (int j = 0; j < mp->getNumGeometries(); ++j)
                    {
                        const auto *pp = mp->getGeometryRef(j)->toPoint();
                        mesh::SteinerPoint sp;
                        sp.xy     = QPointF(pp->getX(), pp->getY());
                        sp.marker = 0;
                        g.addSteinerPoint(sp);
                    }
                }
            }
            OGRFeature::DestroyFeature(f);
        }
    }

    progress(22, tr("Adding constraining lines…"));

    for (int i = 0; i < m_lineLayersList->count(); ++i)
    {
        auto *item = m_lineLayersList->item(i);
        if (!item || item->checkState() != Qt::Checked) continue;
        auto *vl = static_cast<GISVectorLayer *>(
            item->data(Qt::UserRole).value<void *>());
        if (!vl || !vl->ogrLayer()) continue;

        OGRLayer *ol = vl->ogrLayer();
        ol->ResetReading();
        OGRFeature *f = nullptr;
        while ((f = ol->GetNextFeature()) != nullptr)
        {
            auto pushLineString = [&](const OGRLineString *ls) {
                if (!ls || ls->getNumPoints() < 2) return;
                mesh::ConstraintSegment cs;
                cs.path.reserve(ls->getNumPoints());
                for (int j = 0; j < ls->getNumPoints(); ++j)
                    cs.path.append(QPointF(ls->getX(j), ls->getY(j)));
                cs.marker = 0;
                g.addConstraintSegment(cs);
            };
            if (auto *gg = f->GetGeometryRef())
            {
                const auto gt = wkbFlatten(gg->getGeometryType());
                if (gt == wkbLineString)
                    pushLineString(gg->toLineString());
                else if (gt == wkbMultiLineString)
                {
                    const auto *ml = gg->toMultiLineString();
                    for (int j = 0; j < ml->getNumGeometries(); ++j)
                        pushLineString(ml->getGeometryRef(j)->toLineString());
                }
            }
            OGRFeature::DestroyFeature(f);
        }
    }

    // Region markers — subcatchments (tag triangles whose centroid sits
    // inside the polygon's seed point). We use the first-vertex of the
    // subcatchment polygon as a safe interior seed; Triangle propagates
    // the region attribute to every triangle reachable from that seed.
    QHash<int, QString> regionIdToTag;
    if (m_includeSubcatch->isChecked())
    {
        const auto cat = SWMMModelLayer::CatSubcatchments;
        for (int row = 0; row < layer->categoryCount(cat); ++row)
        {
            const QString name = layer->objectNameAt(cat, row);
            if (name.isEmpty()) continue;
            const MapExtent ce = layer->objectExtent(name);
            if (!ce.isValid()) continue;
            mesh::RegionMarker rm;
            rm.xy        = QPointF((ce.xMin() + ce.xMax()) * 0.5,
                                   (ce.yMin() + ce.yMax()) * 0.5);
            rm.attribute = nextMarker;
            rm.tag       = QStringLiteral("subcatch_%1").arg(name);
            g.addRegion(rm);
            regionIdToTag.insert(nextMarker, name);  // raw subcatch id
            ++nextMarker;
        }
    }

    // ── Quality ─────────────────────────────────────────────────────
    mesh::GenerationOptions opts;
    opts.maxArea            = m_maxAreaSpin->value();
    opts.minAngle           = m_minAngleSpin->value();
    opts.maxSteinerPoints   = m_maxSteinerSpin->value();
    opts.allowSteiner       = m_allowSteiner->isChecked();
    opts.quiet              = true;
    g.setOptions(opts);

    progress(40, tr("Running Triangle…"));

    // ── Run ─────────────────────────────────────────────────────────
    mesh::MeshResult result = g.generate();
    if (!result.ok)
        return fail(tr("Triangle returned no mesh: %1").arg(result.errorMsg));

    progress(70, tr("Sampling DTM elevations…"));

    // ── DTM elevation ───────────────────────────────────────────────
    auto *dtmLayer = static_cast<GISRasterLayer *>(
        m_dtmCombo->currentData().value<void *>());
    const QString dtmPath = dtmLayer ? dtmLayer->filePath() : QString();
    if (!dtmPath.isEmpty())
    {
        mesh::DTMSampler s;
        if (s.open(dtmPath))
        {
            for (auto &v : result.vertices)
                v.z = s.sample(v.xy.x(), v.xy.y());
        }
        else
        {
            return fail(tr("Could not open DTM raster: %1").arg(s.errorMsg()));
        }
    }
    // No DTM → leave z = 0 (engine accepts but the user is warned).

    // ── CouplingMap ─────────────────────────────────────────────────
    mesh::CouplingMap coupling;
    for (int i = 0; i < result.vertices.size(); ++i)
    {
        const QString tag = nodeMarkerToTag.value(result.vertices[i].marker);
        if (!tag.isEmpty())
            coupling.vertexToNode.insert(i, tag);
    }
    // Triangle → subcatchment outlet. We only populate when a subcatchment
    // owns the triangle (region tag set). The mapping value is the raw
    // subcatchment name (which is also its outlet target — the engine's
    // [2D_TRIANGLE_NODE_MAP] resolves to the subcatchment's outlet node).
    for (int i = 0; i < result.triangles.size(); ++i)
    {
        const QString &tag = result.triangles[i].tag;
        if (!tag.isEmpty() && tag.startsWith(QStringLiteral("subcatch_")))
        {
            const QString subName = tag.mid(int(qstrlen("subcatch_")));
            coupling.triangleToNode.insert(i, subName);
        }
    }
    // Constant Manning's n only for first cut — categorical raster /
    // shapefile field land via mesh::ManningsSampler in a follow-up.
    const double mannings = m_manningsValueSpin->value();

    // ── Write ───────────────────────────────────────────────────────
    const auto mode = m_outputExternal->isChecked()
                          ? mesh::MeshOutputMode::External
                          : mesh::MeshOutputMode::Inline;
    QString meshPath;
    if (mode == mesh::MeshOutputMode::External)
        meshPath = m_meshPathEdit->text().trimmed();

    progress(85, tr("Writing mesh file…"));

    QString writeErr;
    if (!mesh::InpMeshWriter::write(mode, inpPath, meshPath,
                                     result, coupling, mannings, &writeErr))
        return fail(tr("Writing mesh failed: %1").arg(writeErr));

    progress(95, tr("Adding mesh layer to canvas…"));

    // ── Add the generated mesh as a canvas layer ────────────────────
    // Multiple mesh candidates can coexist; the user picks one as
    // active via the Mesh tab in Sim Options (Set Active patches
    // [2D_MESH_FILE] in the .inp). The fresh layer is marked active
    // here because the writer just patched [2D_MESH_FILE] to point at
    // it, so it is the active mesh by definition right now.
    if (auto *canvas = m_pw->canvas())
    {
        // Demote any previously-active mesh layer so only one shows the
        // "active" cyan styling at a time.
        for (auto *L : canvas->layers())
            if (auto *m = qobject_cast<SWMM2DMeshLayer *>(L))
                m->setActiveMesh(false);

        auto *meshLayer = new SWMM2DMeshLayer(result, meshPath);
        meshLayer->setActiveMesh(mode == mesh::MeshOutputMode::External);
        meshLayer->setName(meshPath.isEmpty()
                               ? QStringLiteral("Mesh (inline)")
                               : QFileInfo(meshPath).fileName());
        canvas->addLayer(meshLayer, /*pushUndo=*/true);
    }

    m_pw->setHasChanges(true);
    progress(100, tr("Mesh ready."));
    return true;
}
