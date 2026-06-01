/*!
 * \file   swmm2dresultsstylepanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  O2-1 — SWMM2DResultsLayer layer-level display controls.
 */
#include "ui/dialogs/swmm2dresultsstylepanel.h"

#include "layers/swmm2dresultslayer.h"
#include "ui/widgets/colorbutton.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace openswmmvis::ui {

Swmm2DResultsStylePanel::Swmm2DResultsStylePanel(SWMM2DResultsLayer *layer, QWidget *parent)
    : QWidget(parent), m_layer(layer)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    // ── Depth heatmap ───────────────────────────────────────────────────
    auto *depthBox  = new QGroupBox(tr("Depth heatmap"), this);
    auto *depthForm = new QFormLayout(depthBox);

    m_dryDepth = new QDoubleSpinBox(depthBox);
    m_dryDepth->setRange(0.0, 1000.0); m_dryDepth->setDecimals(3);
    m_dryDepth->setSingleStep(0.01); m_dryDepth->setSuffix(tr(" m"));
    depthForm->addRow(tr("Dry depth:"), m_dryDepth);

    m_maxDepth = new QDoubleSpinBox(depthBox);
    m_maxDepth->setRange(0.0, 100000.0); m_maxDepth->setDecimals(3);
    m_maxDepth->setSingleStep(0.1); m_maxDepth->setSuffix(tr(" m"));
    depthForm->addRow(tr("Max depth:"), m_maxDepth);

    m_rampStyle = new QComboBox(depthBox);
    m_rampStyle->addItem(tr("Smooth"),    int(SWMM2DResultsLayer::ColorRampStyle::Smooth));
    m_rampStyle->addItem(tr("Graduated"), int(SWMM2DResultsLayer::ColorRampStyle::Graduated));
    depthForm->addRow(tr("Ramp style:"), m_rampStyle);

    m_classes = new QSpinBox(depthBox);
    m_classes->setRange(2, 64);
    depthForm->addRow(tr("Classes:"), m_classes);
    root->addWidget(depthBox);

    // ── Filled contours ─────────────────────────────────────────────────
    auto *bandBox  = new QGroupBox(tr("Filled contours"), this);
    auto *bandForm = new QFormLayout(bandBox);
    m_bandsOn = new QCheckBox(tr("Show filled contour bands"), bandBox);
    bandForm->addRow(QString(), m_bandsOn);
    m_bandLevels = new QSpinBox(bandBox); m_bandLevels->setRange(2, 64);
    bandForm->addRow(tr("Levels:"), m_bandLevels);
    m_bandOpacity = new QDoubleSpinBox(bandBox);
    m_bandOpacity->setRange(0.0, 1.0); m_bandOpacity->setDecimals(2);
    m_bandOpacity->setSingleStep(0.05);
    bandForm->addRow(tr("Opacity:"), m_bandOpacity);
    root->addWidget(bandBox);

    // ── Isolines ─────────────────────────────────────────────────────────
    auto *isoBox  = new QGroupBox(tr("Iso-depth lines"), this);
    auto *isoForm = new QFormLayout(isoBox);
    m_isoOn = new QCheckBox(tr("Show iso-depth lines"), isoBox);
    isoForm->addRow(QString(), m_isoOn);
    m_isoLevels = new QSpinBox(isoBox); m_isoLevels->setRange(1, 64);
    isoForm->addRow(tr("Levels:"), m_isoLevels);
    m_isoColor = new ColorButton(isoBox); m_isoColor->setShowAlpha(true);
    isoForm->addRow(tr("Colour:"), m_isoColor);
    m_isoWidth = new QDoubleSpinBox(isoBox);
    m_isoWidth->setRange(0.25, 20.0); m_isoWidth->setDecimals(2);
    m_isoWidth->setSingleStep(0.25); m_isoWidth->setSuffix(tr(" px"));
    isoForm->addRow(tr("Width:"), m_isoWidth);
    root->addWidget(isoBox);

    // ── Velocity arrows ──────────────────────────────────────────────────
    auto *velBox  = new QGroupBox(tr("Velocity arrows"), this);
    auto *velForm = new QFormLayout(velBox);
    m_velOn = new QCheckBox(tr("Show velocity arrows"), velBox);
    velForm->addRow(QString(), m_velOn);
    m_velOpacity = new QDoubleSpinBox(velBox);
    m_velOpacity->setRange(0.0, 1.0); m_velOpacity->setDecimals(2);
    m_velOpacity->setSingleStep(0.05);
    velForm->addRow(tr("Opacity:"), m_velOpacity);
    m_velScale = new QDoubleSpinBox(velBox);
    m_velScale->setRange(1.0, 500.0); m_velScale->setDecimals(1);
    velForm->addRow(tr("Arrow scale:"), m_velScale);
    m_velMax = new QDoubleSpinBox(velBox);
    m_velMax->setRange(0.0, 1000.0); m_velMax->setDecimals(3);
    m_velMax->setSingleStep(0.1); m_velMax->setSuffix(tr(" m/s"));
    velForm->addRow(tr("Max velocity:"), m_velMax);
    root->addWidget(velBox);
    root->addStretch();

    if (!m_layer) return;
    auto *L = m_layer.data();

    using RS = SWMM2DResultsLayer::ColorRampStyle;
    connect(m_dryDepth, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [L](double v) { L->setDryDepth(v); });
    connect(m_maxDepth, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [L](double v) { L->setMaxDepth(v); });
    connect(m_rampStyle, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, L](int i) { L->setColorRampStyle(static_cast<RS>(m_rampStyle->itemData(i).toInt())); });
    connect(m_classes, qOverload<int>(&QSpinBox::valueChanged), this,
            [L](int n) { L->setColorClasses(n); });

    connect(m_bandsOn, &QCheckBox::toggled, this, [L](bool on) { L->setFilledContours(on); });
    connect(m_bandLevels, qOverload<int>(&QSpinBox::valueChanged), this,
            [L](int n) { L->setFilledContoursLevels(n); });
    connect(m_bandOpacity, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [L](double a) { L->setFilledContoursOpacity(a); });

    connect(m_isoOn, &QCheckBox::toggled, this, [L](bool on) { L->setIsolines(on); });
    connect(m_isoLevels, qOverload<int>(&QSpinBox::valueChanged), this,
            [L](int n) { L->setIsolinesLevels(n); });
    connect(m_isoColor, &ColorButton::colorChanged, this,
            [L](const QColor &c) { L->setIsolinesColor(c); });
    connect(m_isoWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [L](double w) { L->setIsolinesWidth(w); });

    connect(m_velOn, &QCheckBox::toggled, this, [L](bool on) { L->setVelocityVectorsVisible(on); });
    connect(m_velOpacity, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [L](double a) { L->setVelocityOpacity(a); });
    connect(m_velScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [L](double s) { L->setVelocityArrowScale(s); });
    connect(m_velMax, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [L](double v) { L->setMaxVelocity(v); });

    refreshFromLayer();
}

void Swmm2DResultsStylePanel::refreshFromLayer()
{
    if (!m_layer) return;
    auto *L = m_layer.data();
    QSignalBlocker b0(m_dryDepth), b1(m_maxDepth), b2(m_rampStyle), b3(m_classes),
        b4(m_bandsOn), b5(m_bandLevels), b6(m_bandOpacity),
        b7(m_isoOn), b8(m_isoLevels), b9(m_isoColor), b10(m_isoWidth),
        b11(m_velOn), b12(m_velOpacity), b13(m_velScale), b14(m_velMax);
    m_dryDepth->setValue(L->dryDepth());
    m_maxDepth->setValue(L->maxDepth());
    m_rampStyle->setCurrentIndex(m_rampStyle->findData(int(L->colorRampStyle())));
    m_classes->setValue(L->colorClasses());
    m_bandsOn->setChecked(L->filledContours());
    m_bandLevels->setValue(L->filledContoursLevels());
    m_bandOpacity->setValue(L->filledContoursOpacity());
    m_isoOn->setChecked(L->isolines());
    m_isoLevels->setValue(L->isolinesLevels());
    m_isoColor->setColor(L->isolinesColor());
    m_isoWidth->setValue(L->isolinesWidth());
    m_velOn->setChecked(L->velocityVectorsVisible());
    m_velOpacity->setValue(L->velocityOpacity());
    m_velScale->setValue(L->velocityArrowScale());
    m_velMax->setValue(L->maxVelocity());
}

} // namespace openswmmvis::ui
