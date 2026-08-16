/*!
 * \file   labelconfigeditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/labelconfigeditor.h"

#include "ui/dialogs/labelexpressiondialog.h"
#include "ui/theme/iconfactory.h"
#include "ui/theme/themehelpers.h"
#include "ui/widgets/colorbutton.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QSignalBlocker>
#include <QToolButton>

namespace openswmmvis::ui {

using OpenSWMM::Render::LabelConfig;

LabelConfigEditor::LabelConfigEditor(QWidget *parent)
    : QWidget(parent)
{
    auto *form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);

    m_enabledChk = new QCheckBox(tr("Show labels"), this);
    form->addRow(QString(), m_enabledChk);

    // Field / priority are PICKERS, not free text: a mistyped name resolves
    // to an empty attribute and the labels just come out blank, with nothing
    // on screen to say why. Editable so an unadvertised legacy name survives.
    m_fieldCombo = new QComboBox(this);
    m_fieldCombo->setEditable(true);
    m_fieldCombo->setInsertPolicy(QComboBox::NoInsert);
    m_fieldCombo->lineEdit()->setPlaceholderText(
        tr("Leave blank to use element name"));
    form->addRow(tr("&Field:"), m_fieldCombo);

    // Expression + builder button share a row.
    auto *exprRow  = new QWidget(this);
    auto *exprLay  = new QHBoxLayout(exprRow);
    exprLay->setContentsMargins(0, 0, 0, 0);
    exprLay->setSpacing(4);
    m_exprEdit = new QLineEdit(exprRow);
    m_exprEdit->setPlaceholderText(tr("e.g. {name}: {depth} m"));
    m_exprEdit->setToolTip(tr("Template — {token} placeholders are replaced "
                              "with the feature's values; literal text is "
                              "kept. Overrides the field when set."));
    m_exprBuildBtn = new QToolButton(exprRow);
    m_exprBuildBtn->setText(QStringLiteral("…"));
    m_exprBuildBtn->setToolTip(tr("Open the expression builder — browse the "
                                  "available fields and preview the result"));
    exprLay->addWidget(m_exprEdit, 1);
    exprLay->addWidget(m_exprBuildBtn);
    form->addRow(tr("E&xpression:"), exprRow);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet(openswmmvis::ui::theme::hintStyle());
    m_hintLabel->setVisible(false);
    form->addRow(tr("Fields:"), m_hintLabel);

    // Font family + size + bold/italic on one row pair.
    m_fontCombo = new QFontComboBox(this);
    form->addRow(tr("F&amily:"), m_fontCombo);

    auto *sizeRow = new QHBoxLayout;
    m_fontSizeSpin = new QDoubleSpinBox(this);
    m_fontSizeSpin->setRange(4.0, 72.0);
    m_fontSizeSpin->setDecimals(1);
    m_fontSizeSpin->setSuffix(QStringLiteral(" pt"));
    m_fontSizeSpin->setValue(9.0);
    sizeRow->addWidget(m_fontSizeSpin);
    m_boldBtn = new QToolButton(this);
    m_boldBtn->setIcon(IconFactory::icon(QStringLiteral("Bold")));
    m_boldBtn->setCheckable(true);
    m_boldBtn->setToolTip(tr("Bold"));
    sizeRow->addWidget(m_boldBtn);
    m_italicBtn = new QToolButton(this);
    m_italicBtn->setIcon(IconFactory::icon(QStringLiteral("Italic")));
    m_italicBtn->setCheckable(true);
    m_italicBtn->setToolTip(tr("Italic"));
    sizeRow->addWidget(m_italicBtn);
    sizeRow->addStretch();
    form->addRow(tr("&Size:"), sizeRow);

    m_colorBtn = new ColorButton(this);
    form->addRow(tr("&Colour:"), m_colorBtn);

    // Halo.
    auto *haloRow = new QHBoxLayout;
    m_haloChk = new QCheckBox(tr("Halo"), this);
    haloRow->addWidget(m_haloChk);
    m_haloColorBtn = new ColorButton(this);
    haloRow->addWidget(m_haloColorBtn);
    m_haloRadSpin = new QDoubleSpinBox(this);
    m_haloRadSpin->setRange(0.1, 10.0);
    m_haloRadSpin->setDecimals(1);
    m_haloRadSpin->setSuffix(QStringLiteral(" px"));
    m_haloRadSpin->setValue(1.5);
    haloRow->addWidget(m_haloRadSpin);
    haloRow->addStretch();
    form->addRow(QString(), haloRow);

    m_placementCombo = new QComboBox(this);
    m_placementCombo->addItem(tr("Auto"),   int(LabelConfig::AutoPlacement));
    m_placementCombo->addItem(tr("Above"),  int(LabelConfig::Above));
    m_placementCombo->addItem(tr("Below"),  int(LabelConfig::Below));
    m_placementCombo->addItem(tr("Left"),   int(LabelConfig::Left));
    m_placementCombo->addItem(tr("Right"),  int(LabelConfig::Right));
    m_placementCombo->addItem(tr("Centre"), int(LabelConfig::Centre));
    form->addRow(tr("&Placement:"), m_placementCombo);

    // Scale visibility window (1:N denominators; 0 = unbounded).
    auto *scaleRow = new QHBoxLayout;
    auto makeScaleSpin = [this]() {
        auto *s = new QDoubleSpinBox(this);
        s->setRange(0.0, 1e9);
        s->setDecimals(0);
        s->setSpecialValueText(tr("(none)"));
        s->setKeyboardTracking(false);
        return s;
    };
    m_minScaleSpin = makeScaleSpin();
    m_minScaleSpin->setToolTip(tr("Hide when zoomed further OUT than 1:N (0 = no limit)"));
    m_maxScaleSpin = makeScaleSpin();
    m_maxScaleSpin->setToolTip(tr("Hide when zoomed further IN than 1:N (0 = no limit)"));
    scaleRow->addWidget(new QLabel(tr("Out 1:"), this));
    scaleRow->addWidget(m_minScaleSpin, 1);
    scaleRow->addWidget(new QLabel(tr("In 1:"), this));
    scaleRow->addWidget(m_maxScaleSpin, 1);
    form->addRow(tr("Scale &window:"), scaleRow);

    // Background frame.
    auto *bgRow = new QHBoxLayout;
    m_bgChk = new QCheckBox(tr("Background"), this);
    bgRow->addWidget(m_bgChk);
    m_bgColorBtn = new ColorButton(this);
    bgRow->addWidget(m_bgColorBtn);
    m_bgPadSpin = new QDoubleSpinBox(this);
    m_bgPadSpin->setRange(0.0, 20.0);
    m_bgPadSpin->setDecimals(1);
    m_bgPadSpin->setSuffix(QStringLiteral(" px"));
    m_bgPadSpin->setValue(2.0);
    bgRow->addWidget(m_bgPadSpin);
    bgRow->addStretch();
    form->addRow(QString(), bgRow);

    m_priorityCombo = new QComboBox(this);
    m_priorityCombo->setEditable(true);
    m_priorityCombo->setInsertPolicy(QComboBox::NoInsert);
    m_priorityCombo->lineEdit()->setPlaceholderText(
        tr("Attribute; higher values win collisions"));
    form->addRow(tr("P&riority field:"), m_priorityCombo);

    // Wiring — every edit re-emits the whole value.
    const auto changed = [this]() { emitChanged(); };
    connect(m_enabledChk, &QCheckBox::toggled, this, changed);
    connect(m_fieldCombo, &QComboBox::currentTextChanged, this, changed);
    connect(m_priorityCombo, &QComboBox::currentTextChanged, this, changed);
    connect(m_exprEdit, &QLineEdit::editingFinished, this, changed);
    connect(m_exprBuildBtn, &QToolButton::clicked, this,
            [this]() { openExpressionBuilder(); });
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this,
            [this](const QFont &) { emitChanged(); });
    connect(m_fontSizeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, changed);
    connect(m_boldBtn, &QToolButton::toggled, this, changed);
    connect(m_italicBtn, &QToolButton::toggled, this, changed);
    connect(m_colorBtn, &ColorButton::colorChanged, this,
            [this](const QColor &) { emitChanged(); });
    connect(m_haloChk, &QCheckBox::toggled, this, changed);
    connect(m_haloColorBtn, &ColorButton::colorChanged, this,
            [this](const QColor &) { emitChanged(); });
    connect(m_haloRadSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, changed);
    connect(m_placementCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, changed);
    connect(m_minScaleSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, changed);
    connect(m_maxScaleSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, changed);
    connect(m_bgChk, &QCheckBox::toggled, this, changed);
    connect(m_bgColorBtn, &ColorButton::colorChanged, this,
            [this](const QColor &) { emitChanged(); });
    connect(m_bgPadSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, changed);
}

void LabelConfigEditor::setConfig(const LabelConfig &cfg)
{
    m_suppress = true;
    m_cfg = cfg;
    m_enabledChk->setChecked(cfg.enabled);
    m_fieldCombo->setCurrentText(cfg.fieldName);
    m_exprEdit->setText(cfg.expression);
    m_fontCombo->setCurrentFont(cfg.font);
    m_fontSizeSpin->setValue(cfg.fontSizePt);
    m_boldBtn->setChecked(cfg.font.bold());
    m_italicBtn->setChecked(cfg.font.italic());
    m_colorBtn->setColor(cfg.color);
    m_haloChk->setChecked(cfg.haloEnabled);
    m_haloColorBtn->setColor(cfg.haloColor);
    m_haloRadSpin->setValue(cfg.haloRadiusPx);
    const int pIdx = m_placementCombo->findData(int(cfg.placement));
    m_placementCombo->setCurrentIndex(pIdx < 0 ? 0 : pIdx);
    m_minScaleSpin->setValue(cfg.minScale);
    m_maxScaleSpin->setValue(cfg.maxScale);
    m_bgChk->setChecked(cfg.backgroundEnabled);
    m_bgColorBtn->setColor(cfg.backgroundColor);
    m_bgPadSpin->setValue(cfg.backgroundPaddingPx);
    m_priorityCombo->setCurrentText(cfg.priorityField);
    m_suppress = false;
}

LabelConfig LabelConfigEditor::config() const
{
    LabelConfig cfg = m_cfg;   // preserve fields without controls (bg radius)
    cfg.enabled    = m_enabledChk->isChecked();
    cfg.fieldName  = m_fieldCombo->currentText().trimmed();
    cfg.expression = m_exprEdit->text();
    QFont f = m_fontCombo->currentFont();
    f.setBold(m_boldBtn->isChecked());
    f.setItalic(m_italicBtn->isChecked());
    cfg.font       = f;
    cfg.fontSizePt = m_fontSizeSpin->value();
    cfg.color      = m_colorBtn->color();
    cfg.haloEnabled  = m_haloChk->isChecked();
    cfg.haloColor    = m_haloColorBtn->color();
    cfg.haloRadiusPx = m_haloRadSpin->value();
    cfg.placement  = static_cast<LabelConfig::Placement>(
        m_placementCombo->currentData().toInt());
    cfg.minScale   = m_minScaleSpin->value();
    cfg.maxScale   = m_maxScaleSpin->value();
    cfg.backgroundEnabled   = m_bgChk->isChecked();
    cfg.backgroundColor     = m_bgColorBtn->color();
    cfg.backgroundPaddingPx = m_bgPadSpin->value();
    cfg.priorityField       = m_priorityCombo->currentText().trimmed();
    return cfg;
}

void LabelConfigEditor::populateFieldCombo(QComboBox *combo,
                                           bool numericOnly) const
{
    // Keep whatever the config put there — refilling must never silently
    // change the value, including a legacy name this layer no longer lists.
    const QString keep = combo->currentText();
    QSignalBlocker block(combo);
    combo->clear();
    combo->addItem(QString());          // blank = "use the element name"
    for (const auto &f : m_fields) {
        if (numericOnly && f.type == QMetaType::QString)
            continue;                   // priority is sorted on
        combo->addItem(f.displayName.isEmpty() ? f.name : f.displayName,
                       f.name);
    }
    const int row = combo->findData(keep);
    if (row >= 0) combo->setCurrentIndex(row);
    else          combo->setCurrentText(keep);
}

void LabelConfigEditor::setAvailableFields(
    const QVector<OpenSWMM::Render::AttributeField> &fields)
{
    m_fields = fields;
    populateFieldCombo(m_fieldCombo,    /*numericOnly=*/false);
    populateFieldCombo(m_priorityCombo, /*numericOnly=*/true);

    QStringList tokens{ QStringLiteral("{name}") };
    for (const auto &f : m_fields)
        tokens << QStringLiteral("{%1}").arg(f.name);
    const QString hint = tokens.join(QStringLiteral("  "));
    m_hintLabel->setText(hint);
    m_hintLabel->setVisible(!hint.isEmpty());
}

void LabelConfigEditor::openExpressionBuilder()
{
    LabelExpressionDialog dlg(m_fields, m_exprEdit->text(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    m_exprEdit->setText(dlg.expression());
    emitChanged();
}

void LabelConfigEditor::emitChanged()
{
    if (m_suppress) return;
    m_cfg = config();
    emit configChanged(m_cfg);
}

} // namespace openswmmvis::ui
