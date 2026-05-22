/*!
 * \file   seriesstyleeditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/seriesstyleeditor.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>

namespace openswmmvis::ui {

using namespace openswmmvis::plot;

SeriesStyleEditor::SeriesStyleEditor(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    pushStyleToControls();
}

QString SeriesStyleEditor::swatchStyleSheet(const QColor& c) const
{
    return QStringLiteral(
        "QPushButton { background-color: %1; border: 1px solid #555; min-width: 80px; }")
        .arg(c.name(QColor::HexArgb));
}

void SeriesStyleEditor::buildUi()
{
    auto *form = new QFormLayout(this);

    // ---- Colour ------------------------------------------------------------
    m_colorBtn = new QPushButton(this);
    m_colorBtn->setText(tr("Pick…"));
    connect(m_colorBtn, &QPushButton::clicked,
            this,       &SeriesStyleEditor::onColorClicked);
    form->addRow(tr("Colour:"), m_colorBtn);

    // ---- Line --------------------------------------------------------------
    auto *lineRow = new QHBoxLayout;
    m_lineVisible = new QCheckBox(tr("Visible"), this);
    m_lineWidth   = new QDoubleSpinBox(this);
    m_lineWidth->setRange(0.1, 10.0);
    m_lineWidth->setSingleStep(0.2);
    m_lineWidth->setSuffix(tr(" px"));
    m_dashCombo = new QComboBox(this);
    m_dashCombo->addItem(tr("Solid"),     QStringLiteral("solid"));
    m_dashCombo->addItem(tr("Dashed"),    QStringLiteral("dash"));
    m_dashCombo->addItem(tr("Dotted"),    QStringLiteral("dot"));
    m_dashCombo->addItem(tr("Dash-dot"),  QStringLiteral("dash-dot"));
    m_dashCombo->addItem(tr("Dash-dot-dot"), QStringLiteral("dash-dot-dot"));
    lineRow->addWidget(m_lineVisible);
    lineRow->addWidget(m_lineWidth);
    lineRow->addWidget(m_dashCombo);
    lineRow->addStretch(1);
    form->addRow(tr("Line:"), lineRow);

    // ---- Marker ------------------------------------------------------------
    auto *markerRow = new QHBoxLayout;
    m_markerVisible = new QCheckBox(tr("Visible"), this);
    m_shapeCombo    = new QComboBox(this);
    m_shapeCombo->addItem(tr("Circle"),   QStringLiteral("circle"));
    m_shapeCombo->addItem(tr("Square"),   QStringLiteral("square"));
    m_shapeCombo->addItem(tr("Triangle"), QStringLiteral("triangle"));
    m_shapeCombo->addItem(tr("Diamond"),  QStringLiteral("diamond"));
    m_shapeCombo->addItem(tr("Cross"),    QStringLiteral("cross"));
    m_shapeCombo->addItem(tr("Plus"),     QStringLiteral("plus"));
    m_markerSize = new QDoubleSpinBox(this);
    m_markerSize->setRange(1.0, 30.0);
    m_markerSize->setSingleStep(1.0);
    m_markerSize->setSuffix(tr(" px"));
    markerRow->addWidget(m_markerVisible);
    markerRow->addWidget(m_shapeCombo);
    markerRow->addWidget(m_markerSize);
    markerRow->addStretch(1);
    form->addRow(tr("Marker:"), markerRow);

    // ---- Opacity -----------------------------------------------------------
    auto *opacityRow = new QHBoxLayout;
    m_opacitySlider = new QSlider(Qt::Horizontal, this);
    m_opacitySlider->setRange(0, 100);
    m_opacityLabel  = new QLabel(QStringLiteral("100%"), this);
    m_opacityLabel->setMinimumWidth(45);
    opacityRow->addWidget(m_opacitySlider, 1);
    opacityRow->addWidget(m_opacityLabel);
    form->addRow(tr("Opacity:"), opacityRow);

    // ---- Legend ------------------------------------------------------------
    m_legendEdit = new QLineEdit(this);
    m_legendEdit->setPlaceholderText(tr("(auto)"));
    form->addRow(tr("Legend:"), m_legendEdit);

    // ---- Wire change signals ----------------------------------------------
    connect(m_lineVisible,   &QCheckBox::toggled,
            this, &SeriesStyleEditor::onAnyControlChanged);
    connect(m_lineWidth,     QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SeriesStyleEditor::onAnyControlChanged);
    connect(m_dashCombo,     QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SeriesStyleEditor::onAnyControlChanged);
    connect(m_markerVisible, &QCheckBox::toggled,
            this, &SeriesStyleEditor::onAnyControlChanged);
    connect(m_shapeCombo,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SeriesStyleEditor::onAnyControlChanged);
    connect(m_markerSize,    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SeriesStyleEditor::onAnyControlChanged);
    connect(m_opacitySlider, &QSlider::valueChanged,
            this, &SeriesStyleEditor::onAnyControlChanged);
    connect(m_legendEdit,    &QLineEdit::textChanged,
            this, &SeriesStyleEditor::onAnyControlChanged);
}

void SeriesStyleEditor::setStyle(const SeriesStyle& style)
{
    m_suppressSignals = true;
    m_style = style;
    pushStyleToControls();
    m_suppressSignals = false;
}

void SeriesStyleEditor::pushStyleToControls()
{
    if (!m_colorBtn) return;   // Called from ctor before buildUi finished? guard
    m_colorBtn->setStyleSheet(swatchStyleSheet(m_style.color));
    m_lineVisible  ->setChecked(m_style.showLine);
    m_lineWidth    ->setValue(m_style.lineWidth);
    {
        const QString key = penStyleToString(m_style.dash);
        const int idx = m_dashCombo->findData(key);
        if (idx >= 0) m_dashCombo->setCurrentIndex(idx);
    }
    m_markerVisible->setChecked(m_style.showMarkers);
    {
        const QString key = markerShapeToString(m_style.shape);
        const int idx = m_shapeCombo->findData(key);
        if (idx >= 0) m_shapeCombo->setCurrentIndex(idx);
    }
    m_markerSize   ->setValue(m_style.markerSize);
    m_opacitySlider->setValue(static_cast<int>(std::round(m_style.opacity * 100.0)));
    m_opacityLabel ->setText(QStringLiteral("%1%").arg(m_opacitySlider->value()));
    m_legendEdit   ->setText(m_style.legendName);
}

void SeriesStyleEditor::onColorClicked()
{
    QColor picked = QColorDialog::getColor(m_style.color, this,
                                            tr("Series colour"),
                                            QColorDialog::ShowAlphaChannel);
    if (!picked.isValid()) return;
    m_style.color = picked;
    m_colorBtn->setStyleSheet(swatchStyleSheet(m_style.color));
    if (!m_suppressSignals)
        emit styleChanged(m_style);
}

void SeriesStyleEditor::onAnyControlChanged()
{
    if (m_suppressSignals) return;

    m_style.showLine    = m_lineVisible->isChecked();
    m_style.lineWidth   = m_lineWidth->value();
    m_style.dash        = penStyleFromString(m_dashCombo->currentData().toString());
    m_style.showMarkers = m_markerVisible->isChecked();
    m_style.shape       = markerShapeFromString(m_shapeCombo->currentData().toString());
    m_style.markerSize  = m_markerSize->value();
    m_style.opacity     = m_opacitySlider->value() / 100.0;
    m_opacityLabel->setText(QStringLiteral("%1%").arg(m_opacitySlider->value()));
    m_style.legendName  = m_legendEdit->text();

    emit styleChanged(m_style);
}

} // namespace openswmmvis::ui
