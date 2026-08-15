/*!
 * \file   colorrampeditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/colorrampeditordialog.h"

#include "layers/swmmresultslayer.h"
#include "ui/theme/iconfactory.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace openswmmvis::ui {

ColorRampEditorDialog::ColorRampEditorDialog(SWMMResultsLayer *layer, QWidget *parent)
    : QDialog(parent), m_layer(layer)
{
    setWindowTitle(tr("Color Ramp Editor"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("ColorRampEditorDialog"));
    resize(520, 480);
    if (layer) m_ramp = layer->colorRamp();
    buildUi();
    rebuildSwatches();
    refreshGradientPreview();
}

ColorRampEditorDialog::ColorRampEditorDialog(const RasterColorRamp &initial, QWidget *parent)
    : QDialog(parent), m_layer(nullptr)
{
    setWindowTitle(tr("Color Ramp Editor"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("ColorRampEditorDialog"));
    resize(520, 480);
    m_ramp = initial;
    buildUi();
    rebuildSwatches();
    refreshGradientPreview();
}

ColorRampEditorDialog::~ColorRampEditorDialog() = default;

QString ColorRampEditorDialog::swatchStyleSheet(const QColor &c) const
{
    return QStringLiteral(
        "QPushButton { background-color: %1; border: 1px solid palette(mid); min-width: 36px; min-height: 24px; }")
        .arg(c.name());
}

void ColorRampEditorDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // ----- Min/Max row + Auto-stretch ---------------------------------------
    auto *rangeRow = new QHBoxLayout;
    rangeRow->addWidget(new QLabel(tr("Min:"), this));
    m_minSpin = new QDoubleSpinBox(this);
    m_minSpin->setRange(-1e9, 1e9);
    m_minSpin->setDecimals(3);
    m_minSpin->setValue(m_ramp.minValue);
    rangeRow->addWidget(m_minSpin);

    rangeRow->addWidget(new QLabel(tr("Max:"), this));
    m_maxSpin = new QDoubleSpinBox(this);
    m_maxSpin->setRange(-1e9, 1e9);
    m_maxSpin->setDecimals(3);
    m_maxSpin->setValue(m_ramp.maxValue);
    rangeRow->addWidget(m_maxSpin);

    m_autoBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("AutoStretch")),
                                tr("Auto-stretch from data"), this);
    rangeRow->addWidget(m_autoBtn);
    rangeRow->addStretch(1);
    root->addLayout(rangeRow);

    // ----- Preset combo -----------------------------------------------------
    auto *presetRow = new QHBoxLayout;
    presetRow->addWidget(new QLabel(tr("Preset:"), this));
    m_presetCombo = new QComboBox(this);
    m_presetCombo->addItem(tr("Viridis"),   QStringLiteral("viridis"));
    m_presetCombo->addItem(tr("Cividis"),   QStringLiteral("cividis"));
    m_presetCombo->addItem(tr("Plasma"),    QStringLiteral("plasma"));
    m_presetCombo->addItem(tr("Magma"),     QStringLiteral("magma"));
    m_presetCombo->addItem(tr("Turbo"),     QStringLiteral("turbo"));
    m_presetCombo->addItem(tr("Blue-Red (diverging)"), QStringLiteral("bluered"));
    m_presetCombo->addItem(tr("Grayscale"), QStringLiteral("grayscale"));
    m_presetCombo->addItem(tr("Custom"),    QStringLiteral("custom"));
    presetRow->addWidget(m_presetCombo);
    m_reverseCb  = new QCheckBox(tr("Reverse"),  this);
    m_discreteCb = new QCheckBox(tr("Discrete"), this);
    presetRow->addWidget(m_reverseCb);
    presetRow->addWidget(m_discreteCb);
    presetRow->addStretch(1);
    root->addLayout(presetRow);

    // ----- Interpolation colour-space (Slice BB-α) --------------------------
    auto *interpRow = new QHBoxLayout;
    interpRow->addWidget(new QLabel(tr("Interpolation:"), this));
    m_interpCombo = new QComboBox(this);
    m_interpCombo->addItem(tr("RGB"),              static_cast<int>(RampInterp::Rgb));
    m_interpCombo->addItem(tr("HSV (short arc)"),  static_cast<int>(RampInterp::HsvShort));
    m_interpCombo->addItem(tr("HSV (long arc)"),   static_cast<int>(RampInterp::HsvLong));
    m_interpCombo->setCurrentIndex(m_interpCombo->findData(static_cast<int>(m_ramp.interp)));
    interpRow->addWidget(m_interpCombo);
    interpRow->addStretch(1);
    root->addLayout(interpRow);

    // ----- Interval count + preview -----------------------------------------
    auto *intervalsRow = new QHBoxLayout;
    intervalsRow->addWidget(new QLabel(tr("Intervals:"), this));
    m_intervalsSpin = new QSpinBox(this);
    m_intervalsSpin->setRange(2, 12);
    m_intervalsSpin->setValue(5);
    intervalsRow->addWidget(m_intervalsSpin);
    intervalsRow->addStretch(1);
    root->addLayout(intervalsRow);

    m_gradientLabel = new QLabel(this);
    m_gradientLabel->setMinimumHeight(28);
    m_gradientLabel->setFrameShape(QFrame::StyledPanel);
    root->addWidget(m_gradientLabel);

    // ----- Per-interval swatches -------------------------------------------
    m_swatchHost = new QWidget(this);
    auto *swatchLayout = new QHBoxLayout(m_swatchHost);
    swatchLayout->setContentsMargins(0, 0, 0, 0);
    root->addWidget(m_swatchHost);

    // ----- OK / Cancel ------------------------------------------------------
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(bb);

    // Wiring
    connect(m_autoBtn,       &QPushButton::clicked, this, &ColorRampEditorDialog::onAutoStretchClicked);
    connect(m_presetCombo,   QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ColorRampEditorDialog::onPresetChanged);
    connect(m_intervalsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ColorRampEditorDialog::onIntervalsChanged);
    connect(m_reverseCb,     &QCheckBox::toggled, this, &ColorRampEditorDialog::onReverseToggled);
    connect(m_discreteCb,    &QCheckBox::toggled, this, &ColorRampEditorDialog::onDiscreteToggled);
    connect(m_minSpin,       QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ColorRampEditorDialog::onMinChanged);
    connect(m_maxSpin,       QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ColorRampEditorDialog::onMaxChanged);
    connect(m_interpCombo,   QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ColorRampEditorDialog::onInterpChanged);
}

void ColorRampEditorDialog::onInterpChanged(int /*index*/)
{
    const int code = m_interpCombo->currentData().toInt();
    switch (code)
    {
    case 1: m_ramp.interp = RampInterp::HsvShort; break;
    case 2: m_ramp.interp = RampInterp::HsvLong; break;
    default: m_ramp.interp = RampInterp::Rgb; break;
    }
    refreshGradientPreview();
}

void ColorRampEditorDialog::onAutoStretchClicked()
{
    if (m_layer) m_layer->autoStretchColorRamp();
    if (m_layer) m_ramp = m_layer->colorRamp();
    m_minSpin->setValue(m_ramp.minValue);
    m_maxSpin->setValue(m_ramp.maxValue);
    refreshGradientPreview();
}

void ColorRampEditorDialog::applyPreset(const QString &key)
{
    QGradientStops stops;
    if (key == QStringLiteral("viridis")) {
        stops = QGradientStops{
            {0.0, QColor( 68,   1,  84)}, {0.25, QColor( 59,  82, 139)},
            {0.5, QColor( 33, 145, 140)}, {0.75, QColor( 94, 201,  98)},
            {1.0, QColor(253, 231,  37)},
        };
    } else if (key == QStringLiteral("cividis")) {
        stops = QGradientStops{
            {0.0, QColor(  0,  32,  77)}, {0.5, QColor(124, 123, 120)},
            {1.0, QColor(255, 234,  70)},
        };
    } else if (key == QStringLiteral("plasma")) {
        stops = QGradientStops{
            {0.0, QColor( 13,   8, 135)}, {0.5, QColor(204,  71, 120)},
            {1.0, QColor(240, 249,  33)},
        };
    } else if (key == QStringLiteral("magma")) {
        stops = QGradientStops{
            {0.0, QColor(  0,   0,   4)}, {0.5, QColor(183,  55, 121)},
            {1.0, QColor(252, 253, 191)},
        };
    } else if (key == QStringLiteral("turbo")) {
        stops = QGradientStops{
            {0.0, QColor( 48,  18,  59)}, {0.25, QColor( 70, 134, 251)},
            {0.5, QColor( 53, 245, 110)}, {0.75, QColor(252, 199,  31)},
            {1.0, QColor(122,   4,   3)},
        };
    } else if (key == QStringLiteral("bluered")) {
        stops = QGradientStops{
            {0.0, QColor(  5,  48,  97)}, {0.5, QColor(247, 247, 247)},
            {1.0, QColor(103,   0,  31)},
        };
    } else if (key == QStringLiteral("grayscale")) {
        stops = QGradientStops{
            {0.0, QColor::fromRgb(  0,   0,   0)},
            {1.0, QColor::fromRgb(255, 255, 255)},
        };
    }
    if (!stops.isEmpty()) {
        m_ramp.stops = stops;
        if (m_reverseCb->isChecked()) {
            QGradientStops rev;
            rev.reserve(stops.size());
            for (auto it = stops.rbegin(); it != stops.rend(); ++it)
                rev.append({1.0 - it->first, it->second});
            m_ramp.stops = rev;
        }
    }
    rebuildSwatches();
    refreshGradientPreview();
}

void ColorRampEditorDialog::onPresetChanged(int index)
{
    if (index < 0) return;
    applyPreset(m_presetCombo->itemData(index).toString());
}

void ColorRampEditorDialog::onIntervalsChanged(int /*n*/)
{
    rebuildSwatches();
    refreshGradientPreview();
}

void ColorRampEditorDialog::onReverseToggled(bool /*on*/)
{
    onPresetChanged(m_presetCombo->currentIndex());
}

void ColorRampEditorDialog::onDiscreteToggled(bool /*on*/)
{
    refreshGradientPreview();
}

void ColorRampEditorDialog::onMinChanged(double v) { m_ramp.minValue = v; }
void ColorRampEditorDialog::onMaxChanged(double v) { m_ramp.maxValue = v; }

void ColorRampEditorDialog::rebuildSwatches()
{
    // Tear down old swatches.
    qDeleteAll(m_swatchButtons);
    m_swatchButtons.clear();
    if (!m_swatchHost->layout()) return;

    auto *layout = static_cast<QHBoxLayout*>(m_swatchHost->layout());
    while (auto *it = layout->takeAt(0)) {
        if (auto *w = it->widget()) w->deleteLater();
        delete it;
    }

    const int n = m_intervalsSpin->value();
    for (int i = 0; i < n; ++i) {
        const double p = (n > 1) ? double(i) / (n - 1) : 0.0;
        const QColor c = m_ramp.colorAt(p);
        auto *btn = new QPushButton(m_swatchHost);
        btn->setStyleSheet(swatchStyleSheet(c));
        btn->setProperty("intervalIdx", i);
        connect(btn, &QPushButton::clicked, this, &ColorRampEditorDialog::onSwatchClicked);
        layout->addWidget(btn);
        m_swatchButtons.append(btn);
    }
}

void ColorRampEditorDialog::onSwatchClicked()
{
    auto *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    const int idx = btn->property("intervalIdx").toInt();
    const int n   = m_intervalsSpin->value();
    if (n <= 0) return;
    const double p = (n > 1) ? double(idx) / (n - 1) : 0.0;
    const QColor cur = m_ramp.colorAt(p);
    const QColor c = QColorDialog::getColor(cur, this, tr("Interval colour"),
                                            QColorDialog::ShowAlphaChannel);
    if (!c.isValid()) return;

    // Insert / replace a stop at p with the picked colour. The ramp's
    // interpolation naturally smooths across remaining stops.
    QGradientStops s = m_ramp.stops;
    bool replaced = false;
    for (auto &stop : s) {
        if (qFuzzyCompare(stop.first + 1.0, p + 1.0)) {
            stop.second = c;
            replaced = true;
            break;
        }
    }
    if (!replaced) s.append({p, c});
    std::sort(s.begin(), s.end(),
              [](const auto &a, const auto &b){ return a.first < b.first; });
    m_ramp.stops = s;
    m_presetCombo->blockSignals(true);
    const int customIdx = m_presetCombo->findData(QStringLiteral("custom"));
    if (customIdx >= 0) m_presetCombo->setCurrentIndex(customIdx);
    m_presetCombo->blockSignals(false);

    // Slice BB-α — first user edit promotes the ramp to HSV-short-arc
    // interpolation (the QGIS / colorist default for hand-authored ramps;
    // built-in palettes keep RGB because their stops are RGB-tuned).
    if (m_ramp.interp == RampInterp::Rgb && m_interpCombo)
    {
        m_ramp.interp = RampInterp::HsvShort;
        m_interpCombo->blockSignals(true);
        const int hsvShortIdx = m_interpCombo->findData(static_cast<int>(RampInterp::HsvShort));
        if (hsvShortIdx >= 0) m_interpCombo->setCurrentIndex(hsvShortIdx);
        m_interpCombo->blockSignals(false);
    }

    btn->setStyleSheet(swatchStyleSheet(c));
    refreshGradientPreview();
}

void ColorRampEditorDialog::refreshGradientPreview()
{
    const int w = std::max(m_gradientLabel->width(), 200);
    const int h = std::max(m_gradientLabel->height(), 24);
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    if (m_discreteCb->isChecked()) {
        const int n = m_intervalsSpin->value();
        for (int i = 0; i < n; ++i) {
            const double pos = (n > 1) ? double(i) / (n - 1) : 0.5;
            const QColor c = m_ramp.colorAt(pos);
            const int x0 = (i * w) / n;
            const int x1 = ((i + 1) * w) / n;
            p.fillRect(QRect(x0, 0, x1 - x0, h), c);
        }
    } else {
        // Slice BB-α — sample the ramp per pixel so HSV interpolation is
        // visible. QLinearGradient blends in RGB unconditionally and would
        // mask the interp combo entirely.
        for (int x = 0; x < w; ++x) {
            const double t = (w > 1) ? double(x) / double(w - 1) : 0.0;
            p.setPen(m_ramp.colorAt(t));
            p.drawLine(x, 0, x, h - 1);
        }
    }
    p.end();
    m_gradientLabel->setPixmap(pm);
}

void ColorRampEditorDialog::accept()
{
    if (m_layer)
        m_layer->setColorRamp(m_ramp);
    QDialog::accept();
}

} // namespace openswmmvis::ui
