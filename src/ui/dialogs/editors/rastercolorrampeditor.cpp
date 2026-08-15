/*!
 * \file   rastercolorrampeditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/editors/rastercolorrampeditor.h"

#include "layers/gisrasterlayer.h"
#include "ui/widgets/colorrampcombobox.h"
#include "ui/theme/iconfactory.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace openswmmvis::ui {

RasterColorRampEditor::RasterColorRampEditor(GISRasterLayer *layer, QWidget *parent)
    : IStyleEditorWidget(parent), m_layer(layer)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    // ── Source / band group ────────────────────────────────────────────
    auto *srcBox = new QGroupBox(tr("Source"), this);
    auto *srcForm = new QFormLayout(srcBox);

    m_bandSpin = new QSpinBox(this);
    m_bandSpin->setRange(1, std::max(1, m_layer ? m_layer->bandCount() : 1));
    srcForm->addRow(tr("R&ender band:"), m_bandSpin);

    m_nodataLabel = new QLabel(this);
    m_nodataLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    srcForm->addRow(tr("&NoData value:"), m_nodataLabel);

    root->addWidget(srcBox);

    // ── Colour ramp group ──────────────────────────────────────────────
    auto *rampBox = new QGroupBox(tr("Colour ramp"), this);
    auto *rampForm = new QFormLayout(rampBox);

    m_rampCombo = new ColorRampComboBox(this);
    rampForm->addRow(tr("R&amp:"), m_rampCombo);

    auto *rangeRow = new QWidget(this);
    auto *rangeLay = new QHBoxLayout(rangeRow);
    rangeLay->setContentsMargins(0, 0, 0, 0);
    m_minSpin = new QDoubleSpinBox(this);
    m_minSpin->setRange(-1e12, 1e12);
    m_minSpin->setDecimals(4);
    m_maxSpin = new QDoubleSpinBox(this);
    m_maxSpin->setRange(-1e12, 1e12);
    m_maxSpin->setDecimals(4);
    rangeLay->addWidget(m_minSpin, 1);
    rangeLay->addWidget(new QLabel(tr("→"), rangeRow));
    rangeLay->addWidget(m_maxSpin, 1);
    rampForm->addRow(tr("Range:"), rangeRow);

    m_autoBtn = new QToolButton(this);
    m_autoBtn->setText(tr("Auto-stretch from data"));
    m_autoBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("AutoStretch")));
    m_autoBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    rampForm->addRow(QString(), m_autoBtn);

    root->addWidget(rampBox);

    // ── Hillshade / relief group (VS.6, surfaced in R-2) ────────────────
    auto *hsBox  = new QGroupBox(tr("Hillshade relief"), this);
    auto *hsForm = new QFormLayout(hsBox);

    m_hsEnable = new QCheckBox(tr("Enable relief shading"), this);
    hsForm->addRow(QString(), m_hsEnable);

    m_hsAzimuth = new QDoubleSpinBox(this);
    m_hsAzimuth->setRange(0.0, 360.0); m_hsAzimuth->setSuffix(tr("°"));
    hsForm->addRow(tr("A&zimuth:"), m_hsAzimuth);

    m_hsAltitude = new QDoubleSpinBox(this);
    m_hsAltitude->setRange(0.0, 90.0); m_hsAltitude->setSuffix(tr("°"));
    hsForm->addRow(tr("A&ltitude:"), m_hsAltitude);

    m_hsZFactor = new QDoubleSpinBox(this);
    m_hsZFactor->setRange(0.0, 100.0); m_hsZFactor->setDecimals(2);
    m_hsZFactor->setSingleStep(0.1);
    hsForm->addRow(tr("Z &factor:"), m_hsZFactor);

    m_hsStrength = new QDoubleSpinBox(this);
    m_hsStrength->setRange(0.0, 1.0); m_hsStrength->setDecimals(2);
    m_hsStrength->setSingleStep(0.05);
    hsForm->addRow(tr("&Strength:"), m_hsStrength);

    root->addWidget(hsBox);
    root->addStretch();

    // ── Bindings ───────────────────────────────────────────────────────
    connect(m_bandSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v) { m_layer->setRenderBand(v); });

    connect(m_rampCombo, &ColorRampComboBox::rampChanged,
            this, [this](const RasterColorRamp &r) {
                // Preserve the existing min/max — ramp picker swaps stops only.
                RasterColorRamp updated = r;
                updated.minValue = m_layer->colorRamp().minValue;
                updated.maxValue = m_layer->colorRamp().maxValue;
                m_layer->setColorRamp(updated);
            });

    connect(m_minSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                RasterColorRamp r = m_layer->colorRamp();
                r.minValue = v;
                m_layer->setColorRamp(r);
            });
    connect(m_maxSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                RasterColorRamp r = m_layer->colorRamp();
                r.maxValue = v;
                m_layer->setColorRamp(r);
            });
    connect(m_autoBtn, &QToolButton::clicked,
            this, &RasterColorRampEditor::onAutoStretch);

    // Hillshade bindings — enable toggle + each parameter pushes the full set.
    connect(m_hsEnable, &QCheckBox::toggled, this, [this](bool on) {
        if (m_layer) m_layer->setHillshadeEnabled(on);
    });
    for (QDoubleSpinBox *sb : { m_hsAzimuth, m_hsAltitude, m_hsZFactor, m_hsStrength })
        connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double) { pushHillshade(); });

    if (m_layer) {
        connect(m_layer, &GISRasterLayer::renderBandChanged,
                this, &RasterColorRampEditor::refreshFromModel,
                Qt::UniqueConnection);
        connect(m_layer, &GISRasterLayer::colorRampChanged,
                this, &RasterColorRampEditor::refreshFromModel,
                Qt::UniqueConnection);
    }

    refreshFromModel();
}

void RasterColorRampEditor::refreshFromModel()
{
    if (!m_layer) return;
    QSignalBlocker b1(m_bandSpin), b2(m_rampCombo), b3(m_minSpin), b4(m_maxSpin);
    QSignalBlocker b5(m_hsEnable), b6(m_hsAzimuth), b7(m_hsAltitude),
        b8(m_hsZFactor), b9(m_hsStrength);

    m_bandSpin->setValue(m_layer->renderBand());
    m_nodataLabel->setText(QString::number(m_layer->noDataValue(), 'g', 8));

    const RasterColorRamp &r = m_layer->colorRamp();
    m_minSpin->setValue(r.minValue);
    m_maxSpin->setValue(r.maxValue);
    // ColorRampComboBox doesn't expose direct setRamp(stops) — leave selection
    // alone; the user's pick wins. Future BB-β extension can match-by-stops.

    m_hsEnable->setChecked(m_layer->hillshadeEnabled());
    m_hsAzimuth->setValue(m_layer->hillshadeAzimuthDeg());
    m_hsAltitude->setValue(m_layer->hillshadeAltitudeDeg());
    m_hsZFactor->setValue(m_layer->hillshadeZFactor());
    m_hsStrength->setValue(m_layer->hillshadeStrength());
}

void RasterColorRampEditor::pushHillshade()
{
    if (!m_layer) return;
    m_layer->setHillshadeParams(m_hsAzimuth->value(), m_hsAltitude->value(),
                                m_hsZFactor->value(), m_hsStrength->value());
}

void RasterColorRampEditor::onAutoStretch()
{
    if (!m_layer) return;
    m_layer->autoStretchColorRamp();
    // colorRampChanged signal will fire refreshFromModel.
}

REGISTER_STYLE_EDITOR(
    GISRasterLayer,
    [](QObject *obj, QWidget *parent) -> IStyleEditorWidget * {
        if (auto *l = qobject_cast<GISRasterLayer *>(obj))
            return new RasterColorRampEditor(l, parent);
        return nullptr;
    })

} // namespace openswmmvis::ui
