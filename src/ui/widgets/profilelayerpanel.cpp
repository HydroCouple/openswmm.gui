/*!
 * \file   profilelayerpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/widgets/profilelayerpanel.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

ProfileLayerPanel::ProfileLayerPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(8);

    auto *header = new QLabel(tr("<b>Layers</b>"), this);
    outer->addWidget(header);

    // ── Ground ────────────────────────────────────────────────────────────
    auto *gnd = new QGroupBox(tr("Ground"), this);
    auto *gndLay = new QVBoxLayout(gnd);
    gndLay->setContentsMargins(8, 6, 8, 6);
    m_useTerrain = new QCheckBox(tr("Use terrain DEM for ground line"), gnd);
    m_useTerrain->setChecked(false);
    m_useTerrain->setToolTip(
        tr("When enabled, the ground line is sampled from the active "
           "terrain raster at the DEM's resolution.  Node rim elevations "
           "(invert + max depth) are unaffected."));
    gndLay->addWidget(m_useTerrain);
    outer->addWidget(gnd);

    // ── Current Lines ─────────────────────────────────────────────────────
    auto *cur = new QGroupBox(tr("Current (animated)"), this);
    auto *curLay = new QVBoxLayout(cur);
    curLay->setContentsMargins(8, 6, 8, 6);
    // HGL is shown via two independent toggles — line and fill — sitting
    // side-by-side under the Current group. EGL keeps its single
    // checkbox below them.
    auto *hglRow = new QHBoxLayout();
    hglRow->setContentsMargins(0, 0, 0, 0);
    hglRow->setSpacing(12);
    m_currentHglLine = new QCheckBox(tr("HGL line"), cur);
    m_currentHglFill = new QCheckBox(tr("HGL fill"), cur);
    m_currentHglLine->setChecked(true);
    m_currentHglFill->setChecked(true);
    hglRow->addWidget(m_currentHglLine);
    hglRow->addWidget(m_currentHglFill);
    hglRow->addStretch(1);
    curLay->addLayout(hglRow);
    m_currentEgl = new QCheckBox(tr("EGL"), cur);
    m_currentEgl->setChecked(false);
    curLay->addWidget(m_currentEgl);
    outer->addWidget(cur);

    // ── Max HGL ───────────────────────────────────────────────────────────
    auto *hgl = new QGroupBox(tr("Max HGL"), this);
    auto *hglLay = new QVBoxLayout(hgl);
    hglLay->setContentsMargins(8, 6, 8, 6);
    m_maxHgl = new QCheckBox(tr("Show"), hgl);
    m_maxHgl->setChecked(true);
    hglLay->addWidget(m_maxHgl);
    m_hglEnvelope  = new QRadioButton(tr("Min↔max band"),    hgl);
    m_hglInvertMax = new QRadioButton(tr("Invert→max fill"), hgl);
    m_hglEnvelope->setChecked(true);
    m_hglGroup = new QButtonGroup(this);
    m_hglGroup->addButton(m_hglEnvelope,  0);
    m_hglGroup->addButton(m_hglInvertMax, 1);
    hglLay->addWidget(m_hglEnvelope);
    hglLay->addWidget(m_hglInvertMax);
    outer->addWidget(hgl);

    // EGL has no physically-meaningful fill: the velocity-head band sits
    // above the HGL but doesn't represent a real water region. The Max-EGL
    // *line* still has a valid use (it stays editable through the styles
    // tree in ProfileOptionsDialog) — just nothing to surface here.

    // ── Labels ───────────────────────────────────────────────────────────
    auto *lbl = new QGroupBox(tr("Labels (secondary axis)"), this);
    auto *lblLay = new QVBoxLayout(lbl);
    lblLay->setContentsMargins(8, 6, 8, 6);
    m_nodeLabels   = new QCheckBox(tr("Node names"), lbl);
    m_linkLabels   = new QCheckBox(tr("Link names"), lbl);
    m_inlineLabels = new QCheckBox(tr("Inline node IDs (over glyphs)"), lbl);
    m_nodeLabels  ->setChecked(false);
    m_linkLabels  ->setChecked(false);
    m_inlineLabels->setChecked(false);
    lblLay->addWidget(m_nodeLabels);
    lblLay->addWidget(m_linkLabels);
    lblLay->addWidget(m_inlineLabels);

    auto *orientLabel = new QLabel(tr("Orientation:"), lbl);
    lblLay->addWidget(orientLabel);
    m_orientVertical   = new QRadioButton(tr("Vertical (90°)"),  lbl);
    m_orientDiagonal   = new QRadioButton(tr("Diagonal"),        lbl);
    m_orientHorizontal = new QRadioButton(tr("Horizontal (0°)"), lbl);
    m_orientVertical->setChecked(true);
    m_orientGroup = new QButtonGroup(this);
    m_orientGroup->addButton(m_orientVertical,
        static_cast<int>(ProfilePlotWidget::LayerToggles::Vertical));
    m_orientGroup->addButton(m_orientDiagonal,
        static_cast<int>(ProfilePlotWidget::LayerToggles::Diagonal));
    m_orientGroup->addButton(m_orientHorizontal,
        static_cast<int>(ProfilePlotWidget::LayerToggles::Horizontal));
    lblLay->addWidget(m_orientVertical);

    // Diagonal row: radio + custom-angle spinbox side-by-side.
    auto *diagRow = new QHBoxLayout;
    diagRow->setContentsMargins(0, 0, 0, 0);
    diagRow->addWidget(m_orientDiagonal);
    m_angleSpin = new QSpinBox(lbl);
    m_angleSpin->setRange(1, 89);
    m_angleSpin->setValue(45);
    m_angleSpin->setSuffix(QStringLiteral("°"));
    m_angleSpin->setFixedWidth(60);
    diagRow->addWidget(m_angleSpin);
    diagRow->addStretch(1);
    lblLay->addLayout(diagRow);

    lblLay->addWidget(m_orientHorizontal);
    outer->addWidget(lbl);

    outer->addStretch(1);

    // Mode radios are gated by their group's checkbox.
    auto gate = [](QCheckBox *box, QRadioButton *a, QRadioButton *b) {
        const bool on = box->isChecked();
        a->setEnabled(on);
        b->setEnabled(on);
    };
    gate(m_maxHgl, m_hglEnvelope, m_hglInvertMax);

    // Orientation radios are gated when at least one label row is on.
    // The angle spinbox is additionally gated on the Diagonal radio.
    auto gateOrient = [this]() {
        const bool on = m_nodeLabels->isChecked() || m_linkLabels->isChecked();
        m_orientVertical  ->setEnabled(on);
        m_orientDiagonal  ->setEnabled(on);
        m_orientHorizontal->setEnabled(on);
        m_angleSpin       ->setEnabled(on && m_orientDiagonal->isChecked());
    };
    gateOrient();

    auto wire = [this, gate, gateOrient]() {
        gate(m_maxHgl, m_hglEnvelope, m_hglInvertMax);
        gateOrient();
        emitTogglesChanged();
    };
    connect(m_currentHglLine, &QCheckBox::toggled, this, wire);
    connect(m_currentHglFill, &QCheckBox::toggled, this, wire);
    connect(m_currentEgl,     &QCheckBox::toggled, this, wire);
    connect(m_maxHgl,       &QCheckBox::toggled, this, wire);
    connect(m_nodeLabels,   &QCheckBox::toggled, this, wire);
    connect(m_linkLabels,   &QCheckBox::toggled, this, wire);
    connect(m_inlineLabels, &QCheckBox::toggled, this, wire);
    connect(m_useTerrain,   &QCheckBox::toggled, this, wire);
    connect(m_hglGroup, &QButtonGroup::idToggled,
            this, [this](int, bool) { emitTogglesChanged(); });
    connect(m_orientGroup, &QButtonGroup::idToggled,
            this, [this, gateOrient](int, bool) {
        gateOrient();
        emitTogglesChanged();
    });
    connect(m_angleSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int) { emitTogglesChanged(); });
}

ProfilePlotWidget::LayerToggles ProfileLayerPanel::toggles() const
{
    using LO = ProfilePlotWidget::LayerToggles;
    LO t;
    t.currentHglLine = m_currentHglLine->isChecked();
    t.currentHglFill = m_currentHglFill->isChecked();
    t.currentEgl     = m_currentEgl->isChecked();
    t.maxHglBand     = m_maxHgl->isChecked();
    t.showNodeLabels   = m_nodeLabels->isChecked();
    t.showLinkLabels   = m_linkLabels->isChecked();
    t.inlineNodeLabels = m_inlineLabels->isChecked();
    t.useTerrainGround = m_useTerrain->isChecked();
    if      (m_orientHorizontal->isChecked()) t.labelOrientation = LO::Horizontal;
    else if (m_orientDiagonal->isChecked())   t.labelOrientation = LO::Diagonal;
    else                                       t.labelOrientation = LO::Vertical;
    t.labelAngleDeg = m_angleSpin->value();
    return t;
}

void ProfileLayerPanel::setToggles(const ProfilePlotWidget::LayerToggles &t)
{
    using LO = ProfilePlotWidget::LayerToggles;
    QSignalBlocker b1(m_currentHglLine);
    QSignalBlocker b1f(m_currentHglFill);
    QSignalBlocker b2(m_currentEgl);
    QSignalBlocker b3(m_maxHgl);
    QSignalBlocker b5(m_hglGroup);
    QSignalBlocker b7(m_nodeLabels);
    QSignalBlocker b8(m_linkLabels);
    QSignalBlocker b9(m_orientGroup);
    QSignalBlocker b10(m_inlineLabels);
    QSignalBlocker b11(m_angleSpin);
    QSignalBlocker b12(m_useTerrain);
    m_currentHglLine->setChecked(t.currentHglLine);
    m_currentHglFill->setChecked(t.currentHglFill);
    m_currentEgl->setChecked(t.currentEgl);
    m_maxHgl->setChecked(t.maxHglBand);
    m_nodeLabels  ->setChecked(t.showNodeLabels);
    m_linkLabels  ->setChecked(t.showLinkLabels);
    m_inlineLabels->setChecked(t.inlineNodeLabels);
    m_useTerrain  ->setChecked(t.useTerrainGround);
    switch (t.labelOrientation) {
    case LO::Horizontal: m_orientHorizontal->setChecked(true); break;
    case LO::Diagonal:   m_orientDiagonal  ->setChecked(true); break;
    case LO::Vertical:   m_orientVertical  ->setChecked(true); break;
    }
    m_angleSpin->setValue(std::clamp(t.labelAngleDeg, 1, 89));
    emit togglesChanged(t);
}

void ProfileLayerPanel::emitTogglesChanged()
{
    emit togglesChanged(toggles());
}
