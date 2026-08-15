/*!
 * \file   labelstab.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/labelstab.h"

#include "layers/gisvectorlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "ui/uiscrollhelpers.h"
#include "ui/widgets/colorbutton.h"
#include "ui/theme/iconfactory.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using OpenSWMM::Render::LabelConfig;

namespace {

/*! VS.10 — LabelConfig now lives on the OpenSWMMVisLayer base, so the tab
 *  reads / writes it uniformly for every layer kind (model, vector, results,
 *  2D, mesh). setLabelConfig() is virtual; subclasses that need extra
 *  bookkeeping (SWMMModelLayer, GISVectorLayer) override it. */
LabelConfig readLabelConfig(OpenSWMMVisLayer *layer)
{
    return layer ? layer->labelConfig() : LabelConfig{};
}

void writeLabelConfig(OpenSWMMVisLayer *layer, const LabelConfig &cfg)
{
    if (layer) layer->setLabelConfig(cfg);
}

} // namespace

// ---------------------------------------------------------------------------

LabelsTab::LabelsTab(OpenSWMMVisLayer *layer, QWidget *parent)
    : QWidget(parent), m_layer(layer)
{
    // Readable floor: when hosted in a scroll area (LayerStyleDialog wraps this
    // tab), a narrower dialog scrolls rather than collapsing the form's combos
    // and spin boxes. The label column + a min-width control column need
    // roughly this much to stay legible.
    setMinimumWidth(OpenSWMM::Ui::kComboMinWidthPx + 240);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(8);

    // ── Master enable + field ──────────────────────────────────────────
    auto *masterBox = new QGroupBox(tr("Labels"), this);
    auto *masterLay = new QFormLayout(masterBox);

    m_enabledChk = new QCheckBox(tr("Show labels"), masterBox);
    masterLay->addRow(QString(), m_enabledChk);

    // Field control: combo for GIS vector (pre-populated from OGR fields),
    // free-text line edit for SWMM (which has no schema — labels track the
    // element name implicitly when the field is left blank).
    if (auto *vec = qobject_cast<GISVectorLayer *>(layer)) {
        m_fieldCombo = new QComboBox(masterBox);
        m_fieldCombo->setEditable(true);
        m_fieldCombo->addItem(tr("(feature ID)"), QString());
        for (const QString &name : vec->ogrFieldNames())
            m_fieldCombo->addItem(name, name);
        masterLay->addRow(tr("&Field:"), m_fieldCombo);
    } else {
        m_fieldEdit = new QLineEdit(masterBox);
        m_fieldEdit->setPlaceholderText(tr("Leave blank to use element name"));
        masterLay->addRow(tr("F&ield:"), m_fieldEdit);
    }
    outer->addWidget(masterBox);

    // ── Font ───────────────────────────────────────────────────────────
    auto *fontBox = new QGroupBox(tr("Font"), this);
    auto *fontLay = new QFormLayout(fontBox);

    m_fontCombo = new QFontComboBox(fontBox);
    fontLay->addRow(tr("F&amily:"), m_fontCombo);

    auto *sizeRow = new QHBoxLayout;
    m_fontSizeSpin = new QDoubleSpinBox(fontBox);
    m_fontSizeSpin->setRange(4.0, 72.0);
    m_fontSizeSpin->setDecimals(1);
    m_fontSizeSpin->setSuffix(QStringLiteral(" pt"));
    m_fontSizeSpin->setValue(9.0);
    sizeRow->addWidget(m_fontSizeSpin);

    m_boldBtn = new QToolButton(fontBox);
    m_boldBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Bold")));
    m_boldBtn->setCheckable(true);
    m_boldBtn->setToolTip(tr("Bold"));
    m_boldBtn->setAccessibleName(tr("Bold"));
    sizeRow->addWidget(m_boldBtn);

    m_italicBtn = new QToolButton(fontBox);
    m_italicBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Italic")));
    m_italicBtn->setCheckable(true);
    m_italicBtn->setToolTip(tr("Italic"));
    m_italicBtn->setAccessibleName(tr("Italic"));
    sizeRow->addWidget(m_italicBtn);
    sizeRow->addStretch();
    fontLay->addRow(tr("&Size:"), sizeRow);

    m_colorBtn = new ColorButton(fontBox);
    m_colorBtn->setColor(QColor(20, 20, 20));
    fontLay->addRow(tr("Colo&ur:"), m_colorBtn);

    outer->addWidget(fontBox);

    // ── Halo ───────────────────────────────────────────────────────────
    auto *haloBox = new QGroupBox(tr("Halo (outline around text)"), this);
    auto *haloLay = new QFormLayout(haloBox);

    m_haloChk = new QCheckBox(tr("Enable halo"), haloBox);
    haloLay->addRow(QString(), m_haloChk);

    m_haloColorBtn = new ColorButton(haloBox);
    m_haloColorBtn->setColor(Qt::white);
    haloLay->addRow(tr("Colou&r:"), m_haloColorBtn);

    m_haloRadiusSpin = new QDoubleSpinBox(haloBox);
    m_haloRadiusSpin->setRange(0.5, 8.0);
    m_haloRadiusSpin->setSingleStep(0.25);
    m_haloRadiusSpin->setDecimals(2);
    m_haloRadiusSpin->setSuffix(QStringLiteral(" px"));
    m_haloRadiusSpin->setValue(1.5);
    haloLay->addRow(tr("Ra&dius:"), m_haloRadiusSpin);

    outer->addWidget(haloBox);

    // ── Placement ──────────────────────────────────────────────────────
    auto *placeBox = new QGroupBox(tr("Placement"), this);
    auto *placeLay = new QFormLayout(placeBox);
    m_placementCombo = new QComboBox(placeBox);
    m_placementCombo->addItem(tr("Auto"),   int(LabelConfig::AutoPlacement));
    m_placementCombo->addItem(tr("Above"),  int(LabelConfig::Above));
    m_placementCombo->addItem(tr("Below"),  int(LabelConfig::Below));
    m_placementCombo->addItem(tr("Left"),   int(LabelConfig::Left));
    m_placementCombo->addItem(tr("Right"),  int(LabelConfig::Right));
    m_placementCombo->addItem(tr("Centre"), int(LabelConfig::Centre));
    placeLay->addRow(tr("&Position:"), m_placementCombo);
    outer->addWidget(placeBox);

    // ── Scale window ───────────────────────────────────────────────────
    auto *scaleBox = new QGroupBox(tr("Visibility scale window"), this);
    auto *scaleLay = new QFormLayout(scaleBox);
    m_minScaleSpin = new QDoubleSpinBox(scaleBox);
    m_minScaleSpin->setRange(0.0, 1e9);
    m_minScaleSpin->setDecimals(0);
    m_minScaleSpin->setSingleStep(1000.0);
    m_minScaleSpin->setSpecialValueText(tr("none"));
    m_minScaleSpin->setToolTip(tr("Hide labels when zoomed OUT past this scale denominator (0 = no limit)."));
    scaleLay->addRow(tr("&Hide when 1: ≥"), m_minScaleSpin);

    m_maxScaleSpin = new QDoubleSpinBox(scaleBox);
    m_maxScaleSpin->setRange(0.0, 1e9);
    m_maxScaleSpin->setDecimals(0);
    m_maxScaleSpin->setSingleStep(1000.0);
    m_maxScaleSpin->setSpecialValueText(tr("none"));
    m_maxScaleSpin->setToolTip(tr("Hide labels when zoomed IN past this scale denominator (0 = no limit)."));
    scaleLay->addRow(tr("Hid&e when 1: ≤"), m_maxScaleSpin);
    outer->addWidget(scaleBox);

    // ── Background frame (Slice X.24) ──────────────────────────────────
    auto *bgBox = new QGroupBox(tr("Background frame"), this);
    auto *bgLay = new QFormLayout(bgBox);
    m_bgChk = new QCheckBox(tr("Draw background behind labels"), bgBox);
    bgLay->addRow(QString(), m_bgChk);
    m_bgColorBtn = new ColorButton(bgBox);
    m_bgColorBtn->setColor(QColor(255, 255, 255, 200));
    bgLay->addRow(tr("Colour:"), m_bgColorBtn);
    m_bgPaddingSpin = new QDoubleSpinBox(bgBox);
    m_bgPaddingSpin->setRange(0.0, 16.0);
    m_bgPaddingSpin->setDecimals(1);
    m_bgPaddingSpin->setSuffix(QStringLiteral(" px"));
    m_bgPaddingSpin->setValue(2.0);
    bgLay->addRow(tr("Paddin&g:"), m_bgPaddingSpin);
    m_bgRadiusSpin = new QDoubleSpinBox(bgBox);
    m_bgRadiusSpin->setRange(0.0, 16.0);
    m_bgRadiusSpin->setDecimals(1);
    m_bgRadiusSpin->setSuffix(QStringLiteral(" px"));
    m_bgRadiusSpin->setValue(3.0);
    bgLay->addRow(tr("Corner radius:"), m_bgRadiusSpin);
    outer->addWidget(bgBox);

    // ── Per-feature priority (Slice X.24) ──────────────────────────────
    auto *prioBox = new QGroupBox(tr("Per-feature priority"), this);
    auto *prioLay = new QFormLayout(prioBox);
    m_priorityEdit = new QLineEdit(prioBox);
    m_priorityEdit->setPlaceholderText(tr("Attribute name (higher = drawn first)"));
    m_priorityEdit->setToolTip(tr(
        "Used by future collision avoidance to drop lower-priority "
        "labels first.  Persisted today; collision logic lands in a "
        "follow-up."));
    prioLay->addRow(tr("Field:"), m_priorityEdit);
    outer->addWidget(prioBox);

    outer->addStretch();

    // ── Bindings ───────────────────────────────────────────────────────
    auto push = [this] { pushToModel(); };
    connect(m_enabledChk, &QCheckBox::toggled, this, push);
    if (m_fieldCombo) {
        connect(m_fieldCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [push](int){ push(); });
        connect(m_fieldCombo, &QComboBox::editTextChanged, this, push);
    }
    if (m_fieldEdit)
        connect(m_fieldEdit, &QLineEdit::editingFinished, this, push);

    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [push](const QFont &){ push(); });
    connect(m_fontSizeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [push](double){ push(); });
    connect(m_boldBtn,    &QToolButton::toggled, this, push);
    connect(m_italicBtn,  &QToolButton::toggled, this, push);
    connect(m_colorBtn,   &ColorButton::colorChanged, this, [push](const QColor &){ push(); });

    connect(m_haloChk,       &QCheckBox::toggled, this, push);
    connect(m_haloColorBtn,  &ColorButton::colorChanged, this, [push](const QColor &){ push(); });
    connect(m_haloRadiusSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [push](double){ push(); });

    connect(m_placementCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [push](int){ push(); });

    connect(m_minScaleSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [push](double){ push(); });
    connect(m_maxScaleSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [push](double){ push(); });

    connect(m_bgChk,         &QCheckBox::toggled, this, push);
    connect(m_bgColorBtn,    &ColorButton::colorChanged, this, [push](const QColor &){ push(); });
    connect(m_bgPaddingSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [push](double){ push(); });
    connect(m_bgRadiusSpin,  qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [push](double){ push(); });
    connect(m_priorityEdit,  &QLineEdit::editingFinished, this, push);

    // MVC: refresh when the model changes elsewhere (legend dock,
    // status-bar checkbox, …) so this tab never goes stale. VS.10 — the
    // signal lives on the base, so one connection covers every layer kind.
    if (m_layer)
        connect(m_layer, &OpenSWMMVisLayer::labelConfigChanged,
                this, &LabelsTab::refreshFromModel);

    refreshFromModel();
}

void LabelsTab::refreshFromModel()
{
    if (!m_layer) return;
    m_suppress = true;

    const LabelConfig cfg = readLabelConfig(m_layer);
    QSignalBlocker b1(m_enabledChk), b2(m_fontCombo), b3(m_fontSizeSpin),
        b4(m_boldBtn), b5(m_italicBtn), b6(m_colorBtn),
        b7(m_haloChk), b8(m_haloColorBtn), b9(m_haloRadiusSpin),
        b10(m_placementCombo), b11(m_minScaleSpin), b12(m_maxScaleSpin);

    m_enabledChk->setChecked(cfg.enabled);
    if (m_fieldCombo) {
        QSignalBlocker bf(m_fieldCombo);
        const int idx = m_fieldCombo->findData(cfg.fieldName);
        if (idx >= 0) m_fieldCombo->setCurrentIndex(idx);
        else          m_fieldCombo->setEditText(cfg.fieldName);
    }
    if (m_fieldEdit) {
        QSignalBlocker bf(m_fieldEdit);
        m_fieldEdit->setText(cfg.fieldName);
    }

    m_fontCombo   ->setCurrentFont(cfg.font);
    m_fontSizeSpin->setValue(cfg.fontSizePt);
    m_boldBtn     ->setChecked(cfg.font.bold());
    m_italicBtn   ->setChecked(cfg.font.italic());
    m_colorBtn    ->setColor(cfg.color);

    m_haloChk       ->setChecked(cfg.haloEnabled);
    m_haloColorBtn  ->setColor(cfg.haloColor);
    m_haloRadiusSpin->setValue(cfg.haloRadiusPx);

    {
        const int i = m_placementCombo->findData(int(cfg.placement));
        m_placementCombo->setCurrentIndex(i >= 0 ? i : 0);
    }

    m_minScaleSpin->setValue(cfg.minScale);
    m_maxScaleSpin->setValue(cfg.maxScale);

    {
        QSignalBlocker b13(m_bgChk), b14(m_bgColorBtn),
            b15(m_bgPaddingSpin), b16(m_bgRadiusSpin),
            b17(m_priorityEdit);
        m_bgChk        ->setChecked(cfg.backgroundEnabled);
        m_bgColorBtn   ->setColor(cfg.backgroundColor);
        m_bgPaddingSpin->setValue(cfg.backgroundPaddingPx);
        m_bgRadiusSpin ->setValue(cfg.backgroundRadiusPx);
        m_priorityEdit ->setText(cfg.priorityField);
    }

    m_suppress = false;
}

void LabelsTab::pushToModel()
{
    if (m_suppress || !m_layer) return;

    LabelConfig cfg = readLabelConfig(m_layer);
    cfg.enabled = m_enabledChk->isChecked();
    if (m_fieldCombo) {
        const QVariant v = m_fieldCombo->currentData();
        cfg.fieldName = v.isValid() ? v.toString() : m_fieldCombo->currentText();
    } else if (m_fieldEdit) {
        cfg.fieldName = m_fieldEdit->text();
    }

    QFont f = m_fontCombo->currentFont();
    f.setBold(m_boldBtn->isChecked());
    f.setItalic(m_italicBtn->isChecked());
    cfg.font          = f;
    cfg.fontSizePt    = m_fontSizeSpin->value();
    cfg.color         = m_colorBtn->color();

    cfg.haloEnabled   = m_haloChk->isChecked();
    cfg.haloColor     = m_haloColorBtn->color();
    cfg.haloRadiusPx  = m_haloRadiusSpin->value();

    cfg.placement     = static_cast<LabelConfig::Placement>(
        m_placementCombo->currentData().toInt());

    cfg.minScale      = m_minScaleSpin->value();
    cfg.maxScale      = m_maxScaleSpin->value();

    cfg.backgroundEnabled    = m_bgChk->isChecked();
    cfg.backgroundColor      = m_bgColorBtn->color();
    cfg.backgroundPaddingPx  = m_bgPaddingSpin->value();
    cfg.backgroundRadiusPx   = m_bgRadiusSpin->value();
    cfg.priorityField        = m_priorityEdit->text();

    writeLabelConfig(m_layer, cfg);
}

} // namespace openswmmvis::ui
