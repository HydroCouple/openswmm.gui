/*!
 * \file   layerpropertiesdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#include "ui/dialogs/layerpropertiesdialog.h"
#include "ui/dialogs/crsselectiondialog.h"
#include "layers/openswmmvislayer.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

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

void LayerPropertiesDialog::onApply()
{
    writeToLayer();
    // Refresh metadata summary so e.g. opacity changes are reflected.
    if (m_layer)
        m_metadataText->setPlainText(metadataSummary(m_layer));
}

void LayerPropertiesDialog::onAccept()
{
    writeToLayer();
    accept();
}
