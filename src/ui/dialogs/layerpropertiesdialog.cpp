/*!
 * \file   layerpropertiesdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/layerpropertiesdialog.h"
#include "ui/dialogs/crsselectiondialog.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace {

const char *layerTypeLabel(int t)
{
    using L = OpenSWMMVisLayer;
    switch (t)
    {
    case L::SWMMModelLayer:               return "SWMM Model";
    case L::SWMMResultsLayer:             return "SWMM Results";
    case L::SWMMVectorLayer:
    case L::SWMMGISLayer:                 return "Vector";
    case L::SWMMRasterLayer:              return "Raster";
    case L::SWMMImageryLayer:             return "Imagery (basemap)";
    case L::SWMMWMSLayer:                 return "WMS service";
    case L::SWMMWMTSLayer:                return "WMTS / XYZ tiles";
    case L::SWMMTabularDataLayer:         return "Tabular";
    case L::SWMMTabularyTimeSeriesLayer:  return "Tabular time-series";
    case L::SWMMSubProjectLayer:          return "Sub-project";
    case L::SWMM2DMeshLayer:              return "2D Mesh";
    case L::SWMMDefaultLayer:
    default:                              return "Unknown";
    }
}

} // anonymous

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

LayerPropertiesDialog::LayerPropertiesDialog(OpenSWMMVisLayer *layer, QWidget *parent)
    : QDialog(parent),
      m_layer(layer)
{
    setWindowTitle(tr("Layer Properties"));
    resize(560, 480);
    buildUi();
    if (m_layer && m_layer->layerType() == OpenSWMMVisLayer::SWMM2DMeshLayer)
        buildMeshStatsTab();
    if (m_layer && m_layer->layerType() == OpenSWMMVisLayer::SWMM2DResultsLayer)
        buildResultsStylingTab();
    if (m_layer)
        readFromLayer();
}

LayerPropertiesDialog::~LayerPropertiesDialog() = default;

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void LayerPropertiesDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);
    root->addWidget(m_tabs, 1);

    // ── General ─────────────────────────────────────────────────────────
    {
        auto *page = new QWidget(m_tabs);
        auto *form = new QFormLayout(page);

        m_nameEdit  = new QLineEdit(page);
        form->addRow(tr("Name:"), m_nameEdit);

        m_typeLabel = new QLabel(page);
        m_typeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(tr("Type:"), m_typeLabel);

        // CRS row: read-only label + Change… button
        auto *crsRow    = new QWidget(page);
        auto *crsLay    = new QHBoxLayout(crsRow);
        crsLay->setContentsMargins(0, 0, 0, 0);
        m_crsLabel  = new QLabel(crsRow);
        m_crsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_crsButton = new QToolButton(crsRow);
        m_crsButton->setText(tr("Change…"));
        crsLay->addWidget(m_crsLabel, 1);
        crsLay->addWidget(m_crsButton);
        form->addRow(tr("CRS:"), crsRow);

        m_tabs->addTab(page, tr("General"));

        connect(m_crsButton, &QToolButton::clicked, this, &LayerPropertiesDialog::onPickCRS);
    }

    // ── Rendering ───────────────────────────────────────────────────────
    {
        auto *page = new QWidget(m_tabs);
        auto *vlay = new QVBoxLayout(page);

        m_visibleBox = new QCheckBox(tr("Visible"), page);
        vlay->addWidget(m_visibleBox);

        auto *opacityBox = new QGroupBox(tr("Opacity"), page);
        auto *opLay = new QHBoxLayout(opacityBox);

        m_opacitySlider = new QSlider(Qt::Horizontal, opacityBox);
        m_opacitySlider->setRange(0, 100);
        m_opacitySlider->setSingleStep(1);
        m_opacitySlider->setPageStep(10);

        m_opacitySpin = new QSpinBox(opacityBox);
        m_opacitySpin->setRange(0, 100);
        m_opacitySpin->setSuffix(QStringLiteral(" %"));

        opLay->addWidget(m_opacitySlider, 1);
        opLay->addWidget(m_opacitySpin);
        vlay->addWidget(opacityBox);
        vlay->addStretch();

        m_tabs->addTab(page, tr("Rendering"));

        connect(m_opacitySlider, &QSlider::valueChanged,
                this, &LayerPropertiesDialog::onOpacitySliderChanged);
        connect(m_opacitySpin, qOverload<int>(&QSpinBox::valueChanged),
                this, &LayerPropertiesDialog::onOpacitySpinChanged);
    }

    // ── Metadata ────────────────────────────────────────────────────────
    {
        auto *page = new QWidget(m_tabs);
        auto *vlay = new QVBoxLayout(page);

        m_metadataText = new QPlainTextEdit(page);
        m_metadataText->setReadOnly(true);
        m_metadataText->setLineWrapMode(QPlainTextEdit::NoWrap);
        vlay->addWidget(m_metadataText, 1);

        m_tabs->addTab(page, tr("Metadata"));
    }

    // ── Buttons ─────────────────────────────────────────────────────────
    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &LayerPropertiesDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &LayerPropertiesDialog::onApply);
}

// ---------------------------------------------------------------------------
// Mesh tab (SWMM2DMeshLayer only):
//   - Display group  : edges / nodes visibility checkboxes (Slice AZ.3.4)
//   - Statistics box : read-only mesh summary
// Slice BI.4 will reparent the Display group into the Styling Dock.
// ---------------------------------------------------------------------------

void LayerPropertiesDialog::buildMeshStatsTab()
{
    auto *page = new QWidget(m_tabs);
    auto *vlay = new QVBoxLayout(page);

    auto *displayBox = new QGroupBox(tr("Display"), page);
    auto *dlay       = new QVBoxLayout(displayBox);

    m_meshShowEdgesBox = new QCheckBox(tr("Show mesh edges (wireframe)"), displayBox);
    m_meshShowNodesBox = new QCheckBox(tr("Show mesh nodes"), displayBox);
    dlay->addWidget(m_meshShowEdgesBox);
    dlay->addWidget(m_meshShowNodesBox);
    vlay->addWidget(displayBox);

    // Slice AU.6.4-lite — tunable hillshade params. Defaults reproduce the
    // historic hardcoded constants (az=225°, alt=35.3°, zExag=3, minLit=0.15).
    auto *hsBox  = new QGroupBox(tr("Hillshade"), page);
    auto *hsForm = new QFormLayout(hsBox);

    m_meshHillshadeAzSpin = new QDoubleSpinBox(hsBox);
    m_meshHillshadeAzSpin->setRange(0.0, 360.0);
    m_meshHillshadeAzSpin->setDecimals(1);
    m_meshHillshadeAzSpin->setSingleStep(5.0);
    m_meshHillshadeAzSpin->setSuffix(QStringLiteral(" °"));
    m_meshHillshadeAzSpin->setWrapping(true);
    hsForm->addRow(tr("Azimuth (light from):"), m_meshHillshadeAzSpin);

    m_meshHillshadeAltSpin = new QDoubleSpinBox(hsBox);
    m_meshHillshadeAltSpin->setRange(0.0, 90.0);
    m_meshHillshadeAltSpin->setDecimals(1);
    m_meshHillshadeAltSpin->setSingleStep(5.0);
    m_meshHillshadeAltSpin->setSuffix(QStringLiteral(" °"));
    hsForm->addRow(tr("Altitude (sun angle):"), m_meshHillshadeAltSpin);

    m_meshHillshadeZExSpin = new QDoubleSpinBox(hsBox);
    m_meshHillshadeZExSpin->setRange(0.1, 100.0);
    m_meshHillshadeZExSpin->setDecimals(2);
    m_meshHillshadeZExSpin->setSingleStep(0.5);
    m_meshHillshadeZExSpin->setSuffix(QStringLiteral(" ×"));
    hsForm->addRow(tr("Vertical exaggeration:"), m_meshHillshadeZExSpin);

    m_meshHillshadeMinSpin = new QDoubleSpinBox(hsBox);
    m_meshHillshadeMinSpin->setRange(0.0, 1.0);
    m_meshHillshadeMinSpin->setDecimals(2);
    m_meshHillshadeMinSpin->setSingleStep(0.05);
    hsForm->addRow(tr("Shadow floor (min lit):"), m_meshHillshadeMinSpin);

    vlay->addWidget(hsBox);

    // Slice BJ.2-lite — bed-elevation contour lines (mesh isolines via
    // marching-triangles). Filled isobands + per-level ramp + labels ship
    // in BJ.2 full once BB ColorRamp + BI.2 LabelExpression land.
    auto *contourBox  = new QGroupBox(tr("Contours (bed elevation)"), page);
    auto *contourForm = new QFormLayout(contourBox);

    m_meshShowContoursBox = new QCheckBox(tr("Show contour lines"), contourBox);
    contourForm->addRow(m_meshShowContoursBox);

    m_meshContourIntervalsSpin = new QSpinBox(contourBox);
    m_meshContourIntervalsSpin->setRange(1, 200);
    m_meshContourIntervalsSpin->setSingleStep(1);
    contourForm->addRow(tr("Number of intervals:"), m_meshContourIntervalsSpin);

    auto *colorRow = new QWidget(contourBox);
    auto *colorLay = new QHBoxLayout(colorRow);
    colorLay->setContentsMargins(0, 0, 0, 0);
    m_meshContourColorBtn = new QToolButton(colorRow);
    m_meshContourColorBtn->setText(tr("Pick…"));
    m_meshContourColorBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_meshContourColorBtn->setMinimumWidth(110);
    colorLay->addWidget(m_meshContourColorBtn);
    colorLay->addStretch();
    contourForm->addRow(tr("Line colour:"), colorRow);

    m_meshContourWidthSpin = new QDoubleSpinBox(contourBox);
    m_meshContourWidthSpin->setRange(0.25, 10.0);
    m_meshContourWidthSpin->setDecimals(2);
    m_meshContourWidthSpin->setSingleStep(0.25);
    m_meshContourWidthSpin->setSuffix(QStringLiteral(" px"));
    contourForm->addRow(tr("Line width:"), m_meshContourWidthSpin);

    // Slice BJ.2-filled — iso-band fills (Viridis palette until BB ColorRamp).
    m_meshFilledContoursBox = new QCheckBox(
        tr("Filled bands (Viridis palette)"), contourBox);
    contourForm->addRow(m_meshFilledContoursBox);

    m_meshFilledOpacitySpin = new QDoubleSpinBox(contourBox);
    m_meshFilledOpacitySpin->setRange(0.0, 1.0);
    m_meshFilledOpacitySpin->setDecimals(2);
    m_meshFilledOpacitySpin->setSingleStep(0.05);
    contourForm->addRow(tr("Fill opacity:"), m_meshFilledOpacitySpin);

    vlay->addWidget(contourBox);

    connect(m_meshContourColorBtn, &QToolButton::clicked,
            this, &LayerPropertiesDialog::onPickContourColor);

    auto *statsBox = new QGroupBox(tr("Statistics"), page);
    auto *slay     = new QVBoxLayout(statsBox);
    m_statsText    = new QPlainTextEdit(statsBox);
    m_statsText->setReadOnly(true);
    m_statsText->setLineWrapMode(QPlainTextEdit::NoWrap);
    slay->addWidget(m_statsText);
    vlay->addWidget(statsBox, 1);

    m_tabs->addTab(page, tr("Mesh"));
}

// ---------------------------------------------------------------------------
// 2D Results tab (SWMM2DResultsLayer only, Slice CF.MVP-fix.3)
// ---------------------------------------------------------------------------

void LayerPropertiesDialog::buildResultsStylingTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    // ----- Color ramp group -------------------------------------------------
    auto *rampBox = new QGroupBox(tr("Color ramp"), page);
    auto *rampForm = new QFormLayout(rampBox);

    m_resColorStyleCombo = new QComboBox(rampBox);
    m_resColorStyleCombo->addItem(tr("Smooth (continuous)"));
    m_resColorStyleCombo->addItem(tr("Graduated (discrete bins)"));
    rampForm->addRow(tr("Style:"), m_resColorStyleCombo);

    m_resColorClassesSpin = new QSpinBox(rampBox);
    m_resColorClassesSpin->setRange(2, 64);
    m_resColorClassesSpin->setSingleStep(1);
    rampForm->addRow(tr("Classes:"), m_resColorClassesSpin);
    vlay->addWidget(rampBox);

    // ----- Filled isobands group -------------------------------------------
    auto *filledBox = new QGroupBox(tr("Filled bands (Viridis palette)"), page);
    auto *filledForm = new QFormLayout(filledBox);

    m_resFilledBox = new QCheckBox(tr("Show filled bands"), filledBox);
    filledForm->addRow(m_resFilledBox);

    m_resFilledLevelsSpin = new QSpinBox(filledBox);
    m_resFilledLevelsSpin->setRange(2, 32);
    filledForm->addRow(tr("Number of levels:"), m_resFilledLevelsSpin);

    m_resFilledOpacitySpin = new QDoubleSpinBox(filledBox);
    m_resFilledOpacitySpin->setRange(0.0, 1.0);
    m_resFilledOpacitySpin->setDecimals(2);
    m_resFilledOpacitySpin->setSingleStep(0.05);
    filledForm->addRow(tr("Fill opacity:"), m_resFilledOpacitySpin);
    vlay->addWidget(filledBox);

    // ----- Iso-lines group --------------------------------------------------
    auto *isoBox = new QGroupBox(tr("Iso-depth contour lines"), page);
    auto *isoForm = new QFormLayout(isoBox);

    m_resIsolinesBox = new QCheckBox(tr("Show contour lines"), isoBox);
    isoForm->addRow(m_resIsolinesBox);

    m_resIsolinesLevelsSpin = new QSpinBox(isoBox);
    m_resIsolinesLevelsSpin->setRange(1, 32);
    isoForm->addRow(tr("Number of intervals:"), m_resIsolinesLevelsSpin);

    auto *colRow = new QWidget(isoBox);
    auto *colLay = new QHBoxLayout(colRow);
    colLay->setContentsMargins(0, 0, 0, 0);
    m_resIsolinesColorBtn = new QToolButton(colRow);
    m_resIsolinesColorBtn->setText(tr("Pick…"));
    m_resIsolinesColorBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_resIsolinesColorBtn->setMinimumWidth(110);
    colLay->addWidget(m_resIsolinesColorBtn);
    colLay->addStretch(1);
    isoForm->addRow(tr("Line color:"), colRow);

    m_resIsolinesWidthSpin = new QDoubleSpinBox(isoBox);
    m_resIsolinesWidthSpin->setRange(0.25, 10.0);
    m_resIsolinesWidthSpin->setDecimals(2);
    m_resIsolinesWidthSpin->setSingleStep(0.25);
    m_resIsolinesWidthSpin->setSuffix(QStringLiteral(" px"));
    isoForm->addRow(tr("Line width:"), m_resIsolinesWidthSpin);
    vlay->addWidget(isoBox);

    vlay->addStretch(1);

    connect(m_resIsolinesColorBtn, &QToolButton::clicked,
            this, &LayerPropertiesDialog::onPickResultsIsolinesColor);

    m_tabs->addTab(page, tr("2D Results"));
}

// ---------------------------------------------------------------------------
// Layer ↔ widgets
// ---------------------------------------------------------------------------

void LayerPropertiesDialog::readFromLayer()
{
    if (!m_layer) return;

    QSignalBlocker b1(m_nameEdit), b2(m_visibleBox),
                   b3(m_opacitySlider), b4(m_opacitySpin);

    m_nameEdit->setText(m_layer->name());
    m_typeLabel->setText(QString::fromLatin1(layerTypeLabel(m_layer->layerType())));

    QString crsText = tr("(none)");
    if (auto *srs = m_layer->srs())
    {
        const QString auth = srs->toAuthority();
        crsText = auth.isEmpty() ? tr("(local)") : auth;
    }
    m_crsLabel->setText(crsText);
    m_pendingCRSAuthority.clear();

    m_visibleBox->setChecked(m_layer->isVisible());
    const int opacity = qRound(m_layer->opacity() * 100);
    m_opacitySlider->setValue(opacity);
    m_opacitySpin->setValue(opacity);

    m_metadataText->setPlainText(metadataSummary(m_layer));

    // Populate mesh display checkboxes + hillshade spinboxes + statistics
    // when this is a SWMM2DMeshLayer.
    if (auto *ml = qobject_cast<SWMM2DMeshLayer *>(m_layer))
    {
        if (m_meshShowEdgesBox) {
            QSignalBlocker b(m_meshShowEdgesBox);
            m_meshShowEdgesBox->setChecked(ml->showEdges());
        }
        if (m_meshShowNodesBox) {
            QSignalBlocker b(m_meshShowNodesBox);
            m_meshShowNodesBox->setChecked(ml->showMeshNodes());
        }
        if (m_meshHillshadeAzSpin) {
            QSignalBlocker b(m_meshHillshadeAzSpin);
            m_meshHillshadeAzSpin->setValue(ml->hillshadeAzimuth());
        }
        if (m_meshHillshadeAltSpin) {
            QSignalBlocker b(m_meshHillshadeAltSpin);
            m_meshHillshadeAltSpin->setValue(ml->hillshadeAltitude());
        }
        if (m_meshHillshadeZExSpin) {
            QSignalBlocker b(m_meshHillshadeZExSpin);
            m_meshHillshadeZExSpin->setValue(ml->hillshadeZExag());
        }
        if (m_meshHillshadeMinSpin) {
            QSignalBlocker b(m_meshHillshadeMinSpin);
            m_meshHillshadeMinSpin->setValue(ml->hillshadeMinLit());
        }

        // BJ.2-lite — contour controls
        if (m_meshShowContoursBox) {
            QSignalBlocker b(m_meshShowContoursBox);
            m_meshShowContoursBox->setChecked(ml->showContours());
        }
        if (m_meshContourIntervalsSpin) {
            QSignalBlocker b(m_meshContourIntervalsSpin);
            m_meshContourIntervalsSpin->setValue(ml->contourIntervalCount());
        }
        if (m_meshContourWidthSpin) {
            QSignalBlocker b(m_meshContourWidthSpin);
            m_meshContourWidthSpin->setValue(ml->contourLineWidth());
        }
        m_pendingContourColor = ml->contourColor();
        if (m_meshContourColorBtn) {
            QPixmap sw(20, 20);
            sw.fill(m_pendingContourColor);
            m_meshContourColorBtn->setIcon(QIcon(sw));
            m_meshContourColorBtn->setText(m_pendingContourColor.name(QColor::HexArgb));
        }
        // BJ.2-filled
        if (m_meshFilledContoursBox) {
            QSignalBlocker b(m_meshFilledContoursBox);
            m_meshFilledContoursBox->setChecked(ml->filledContours());
        }
        if (m_meshFilledOpacitySpin) {
            QSignalBlocker b(m_meshFilledOpacitySpin);
            m_meshFilledOpacitySpin->setValue(ml->filledContoursOpacity());
        }
    }

    // CF.MVP-fix.3 — read 2D-Results styling widgets when this layer kind.
    if (auto *rl = qobject_cast<SWMM2DResultsLayer *>(m_layer))
    {
        if (m_resColorStyleCombo) {
            QSignalBlocker b(m_resColorStyleCombo);
            m_resColorStyleCombo->setCurrentIndex(
                rl->colorRampStyle() == SWMM2DResultsLayer::ColorRampStyle::Graduated
                    ? 1 : 0);
        }
        if (m_resColorClassesSpin) {
            QSignalBlocker b(m_resColorClassesSpin);
            m_resColorClassesSpin->setValue(rl->colorClasses());
        }
        if (m_resFilledBox) {
            QSignalBlocker b(m_resFilledBox);
            m_resFilledBox->setChecked(rl->filledContours());
        }
        if (m_resFilledLevelsSpin) {
            QSignalBlocker b(m_resFilledLevelsSpin);
            m_resFilledLevelsSpin->setValue(rl->filledContoursLevels());
        }
        if (m_resFilledOpacitySpin) {
            QSignalBlocker b(m_resFilledOpacitySpin);
            m_resFilledOpacitySpin->setValue(rl->filledContoursOpacity());
        }
        if (m_resIsolinesBox) {
            QSignalBlocker b(m_resIsolinesBox);
            m_resIsolinesBox->setChecked(rl->isolines());
        }
        if (m_resIsolinesLevelsSpin) {
            QSignalBlocker b(m_resIsolinesLevelsSpin);
            m_resIsolinesLevelsSpin->setValue(rl->isolinesLevels());
        }
        m_pendingIsolinesColor = rl->isolinesColor();
        if (m_resIsolinesColorBtn) {
            QPixmap sw(20, 20);
            sw.fill(m_pendingIsolinesColor);
            m_resIsolinesColorBtn->setIcon(QIcon(sw));
            m_resIsolinesColorBtn->setText(m_pendingIsolinesColor.name(QColor::HexArgb));
        }
        if (m_resIsolinesWidthSpin) {
            QSignalBlocker b(m_resIsolinesWidthSpin);
            m_resIsolinesWidthSpin->setValue(rl->isolinesWidth());
        }
    }

    if (m_statsText)
    {
        if (auto *ml = qobject_cast<SWMM2DMeshLayer *>(m_layer))
        {
            const auto &mesh = ml->mesh();
            const int nVerts  = mesh.vertices.size();
            const int nTris   = mesh.triangles.size();
            const int nEdges  = mesh.boundaryEdges.size();

            int taggedVerts = 0;
            for (const auto &v : mesh.vertices)
                if (!v.tag.isEmpty()) ++taggedVerts;

            int taggedTris = 0;
            for (const auto &t : mesh.triangles)
                if (!t.tag.isEmpty()) ++taggedTris;

            double totalArea = 0.0;
            double zMin = std::numeric_limits<double>::max();
            double zMax = std::numeric_limits<double>::lowest();
            for (const auto &v : mesh.vertices)
            {
                if (v.z < zMin) zMin = v.z;
                if (v.z > zMax) zMax = v.z;
            }
            for (const auto &t : mesh.triangles)
            {
                if (t.v0 < 0 || t.v0 >= nVerts) continue;
                if (t.v1 < 0 || t.v1 >= nVerts) continue;
                if (t.v2 < 0 || t.v2 >= nVerts) continue;
                const auto &a = mesh.vertices[t.v0].xy;
                const auto &b = mesh.vertices[t.v1].xy;
                const auto &c = mesh.vertices[t.v2].xy;
                totalArea += 0.5 * std::abs(
                    (b.x()-a.x())*(c.y()-a.y()) - (c.x()-a.x())*(b.y()-a.y()));
            }

            QStringList lines;
            lines << QStringLiteral("Vertices:           %1").arg(nVerts);
            lines << QStringLiteral("Triangles:          %1").arg(nTris);
            lines << QStringLiteral("Boundary edges:     %1").arg(nEdges);
            lines << QStringLiteral("Tagged vertices:    %1  (1D-coupled nodes)").arg(taggedVerts);
            lines << QStringLiteral("Tagged triangles:   %1  (subcatchment regions)").arg(taggedTris);
            lines << QStringLiteral("Total mesh area:    %1  (map units²)")
                         .arg(totalArea, 0, 'g', 8);
            if (nVerts > 0)
            {
                lines << QStringLiteral("Min elevation:      %1").arg(zMin, 0, 'f', 3);
                lines << QStringLiteral("Max elevation:      %1").arg(zMax, 0, 'f', 3);
            }
            lines << QStringLiteral("Active mesh:        %1")
                         .arg(ml->isActiveMesh() ? "yes" : "no");
            if (!ml->sourcePath().isEmpty())
                lines << QStringLiteral("Source file:        %1").arg(ml->sourcePath());

            m_statsText->setPlainText(lines.join('\n'));
        }
    }
}

void LayerPropertiesDialog::writeToLayer()
{
    if (!m_layer) return;

    if (m_nameEdit->text() != m_layer->name())
        m_layer->setName(m_nameEdit->text());

    if (m_visibleBox->isChecked() != m_layer->isVisible())
        m_layer->setVisible(m_visibleBox->isChecked());

    const double newOpacity = m_opacitySpin->value() / 100.0;
    if (!qFuzzyCompare(newOpacity, m_layer->opacity()))
        m_layer->setOpacity(newOpacity);

    // Mesh tab — Slices AZ.3.4 (display) + AU.6.4-lite (hillshade)
    if (auto *ml = qobject_cast<SWMM2DMeshLayer *>(m_layer)) {
        if (m_meshShowEdgesBox && m_meshShowEdgesBox->isChecked() != ml->showEdges())
            ml->setShowEdges(m_meshShowEdgesBox->isChecked());
        if (m_meshShowNodesBox && m_meshShowNodesBox->isChecked() != ml->showMeshNodes())
            ml->setShowMeshNodes(m_meshShowNodesBox->isChecked());
        if (m_meshHillshadeAzSpin)
            ml->setHillshadeAzimuth(m_meshHillshadeAzSpin->value());
        if (m_meshHillshadeAltSpin)
            ml->setHillshadeAltitude(m_meshHillshadeAltSpin->value());
        if (m_meshHillshadeZExSpin)
            ml->setHillshadeZExag(m_meshHillshadeZExSpin->value());
        if (m_meshHillshadeMinSpin)
            ml->setHillshadeMinLit(m_meshHillshadeMinSpin->value());

        // BJ.2-lite — contour writes
        if (m_meshShowContoursBox && m_meshShowContoursBox->isChecked() != ml->showContours())
            ml->setShowContours(m_meshShowContoursBox->isChecked());
        if (m_meshContourIntervalsSpin && m_meshContourIntervalsSpin->value() != ml->contourIntervalCount())
            ml->setContourIntervalCount(m_meshContourIntervalsSpin->value());
        if (m_meshContourWidthSpin)
            ml->setContourLineWidth(m_meshContourWidthSpin->value());
        if (m_pendingContourColor.isValid() && m_pendingContourColor != ml->contourColor())
            ml->setContourColor(m_pendingContourColor);
        // BJ.2-filled
        if (m_meshFilledContoursBox && m_meshFilledContoursBox->isChecked() != ml->filledContours())
            ml->setFilledContours(m_meshFilledContoursBox->isChecked());
        if (m_meshFilledOpacitySpin)
            ml->setFilledContoursOpacity(m_meshFilledOpacitySpin->value());
    }

    // CF.MVP-fix.3 — copy 2D-Results styling widgets back into the layer.
    if (auto *rl = qobject_cast<SWMM2DResultsLayer *>(m_layer))
    {
        if (m_resColorStyleCombo) {
            const auto target = (m_resColorStyleCombo->currentIndex() == 1)
                ? SWMM2DResultsLayer::ColorRampStyle::Graduated
                : SWMM2DResultsLayer::ColorRampStyle::Smooth;
            if (target != rl->colorRampStyle())
                rl->setColorRampStyle(target);
        }
        if (m_resColorClassesSpin && m_resColorClassesSpin->value() != rl->colorClasses())
            rl->setColorClasses(m_resColorClassesSpin->value());
        if (m_resFilledBox && m_resFilledBox->isChecked() != rl->filledContours())
            rl->setFilledContours(m_resFilledBox->isChecked());
        if (m_resFilledLevelsSpin && m_resFilledLevelsSpin->value() != rl->filledContoursLevels())
            rl->setFilledContoursLevels(m_resFilledLevelsSpin->value());
        if (m_resFilledOpacitySpin)
            rl->setFilledContoursOpacity(m_resFilledOpacitySpin->value());
        if (m_resIsolinesBox && m_resIsolinesBox->isChecked() != rl->isolines())
            rl->setIsolines(m_resIsolinesBox->isChecked());
        if (m_resIsolinesLevelsSpin && m_resIsolinesLevelsSpin->value() != rl->isolinesLevels())
            rl->setIsolinesLevels(m_resIsolinesLevelsSpin->value());
        if (m_pendingIsolinesColor.isValid() && m_pendingIsolinesColor != rl->isolinesColor())
            rl->setIsolinesColor(m_pendingIsolinesColor);
        if (m_resIsolinesWidthSpin)
            rl->setIsolinesWidth(m_resIsolinesWidthSpin->value());
    }

    // CRS already applied during onPickCRS (a CRSChangeDialog round-trip would
    // be required to reproject coordinates — that's the canvas-CRS button's
    // job). Here we treat layer SRS as a metadata change.
}

// ---------------------------------------------------------------------------
// Static metadata summary (also used by tests)
// ---------------------------------------------------------------------------

QString LayerPropertiesDialog::metadataSummary(const OpenSWMMVisLayer *layer)
{
    if (!layer) return {};

    QStringList lines;
    lines << QStringLiteral("ID:        %1").arg(layer->layerId());
    lines << QStringLiteral("Type:      %1").arg(layerTypeLabel(layer->layerType()));
    lines << QStringLiteral("Visible:   %1").arg(layer->isVisible() ? "yes" : "no");
    lines << QStringLiteral("Opacity:   %1 %").arg(qRound(layer->opacity() * 100));

    if (auto *srs = layer->srs())
    {
        const QString auth = srs->toAuthority();
        lines << QStringLiteral("CRS:       %1").arg(auth.isEmpty() ? "(local)" : auth);
    }
    else
    {
        lines << QStringLiteral("CRS:       (none)");
    }

    const MapExtent ext = layer->extent();
    if (ext.isValid())
    {
        lines << QStringLiteral("Extent:    [%1, %2] → [%3, %4]")
                     .arg(ext.xMin(), 0, 'g', 8)
                     .arg(ext.yMin(), 0, 'g', 8)
                     .arg(ext.xMax(), 0, 'g', 8)
                     .arg(ext.yMax(), 0, 'g', 8);
    }
    else
    {
        lines << QStringLiteral("Extent:    (invalid / not yet computed)");
    }

    if (!layer->children().isEmpty())
        lines << QStringLiteral("Children:  %1").arg(layer->children().size());

    return lines.join('\n');
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void LayerPropertiesDialog::onPickCRS()
{
    CRSSelectionDialog dlg(this);
    dlg.setCurrentCRS(m_layer ? m_layer->srs() : nullptr);
    if (dlg.exec() != QDialog::Accepted)
        return;
    SpatialReferenceSystem *srs = dlg.selectedSRS();
    if (!srs)
        return;

    // Apply to the layer immediately (consistent with the canvas CRS button's
    // behaviour). For coordinate reprojection use the canvas-CRS button on
    // the status bar — this dialog only updates the layer's stored SRS.
    if (m_layer)
        m_layer->setSRS(srs, true);
    m_crsLabel->setText(srs->toAuthority());
    m_pendingCRSAuthority = srs->toAuthority();
}

void LayerPropertiesDialog::onOpacitySliderChanged(int v)
{
    if (m_opacitySpin->value() != v)
    {
        QSignalBlocker b(m_opacitySpin);
        m_opacitySpin->setValue(v);
    }
}

void LayerPropertiesDialog::onOpacitySpinChanged(int v)
{
    if (m_opacitySlider->value() != v)
    {
        QSignalBlocker b(m_opacitySlider);
        m_opacitySlider->setValue(v);
    }
}

void LayerPropertiesDialog::onPickContourColor()
{
    // Honour the alpha channel so the user can tune transparency too.
    const QColor seed = m_pendingContourColor.isValid()
                            ? m_pendingContourColor
                            : QColor(0x1a, 0x1a, 0x1a, 200);
    const QColor picked = QColorDialog::getColor(
        seed, this, tr("Contour line colour"),
        QColorDialog::ShowAlphaChannel);
    if (!picked.isValid()) return;

    m_pendingContourColor = picked;
    QPixmap sw(20, 20);
    sw.fill(picked);
    m_meshContourColorBtn->setIcon(QIcon(sw));
    m_meshContourColorBtn->setText(picked.name(QColor::HexArgb));
}

void LayerPropertiesDialog::onPickResultsIsolinesColor()
{
    const QColor seed = m_pendingIsolinesColor.isValid()
                            ? m_pendingIsolinesColor
                            : QColor(0x14, 0x14, 0x14, 230);
    const QColor picked = QColorDialog::getColor(
        seed, this, tr("Contour line colour"),
        QColorDialog::ShowAlphaChannel);
    if (!picked.isValid()) return;

    m_pendingIsolinesColor = picked;
    QPixmap sw(20, 20);
    sw.fill(picked);
    m_resIsolinesColorBtn->setIcon(QIcon(sw));
    m_resIsolinesColorBtn->setText(picked.name(QColor::HexArgb));
}

void LayerPropertiesDialog::onApply()
{
    writeToLayer();
    if (m_layer)
        readFromLayer();   // re-reads committed values + refreshes all tabs
}

void LayerPropertiesDialog::onAccept()
{
    writeToLayer();
    accept();
}
