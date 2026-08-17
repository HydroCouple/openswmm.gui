/*!
 * \file   swmmelementsymboleditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/editors/swmmelementsymboleditor.h"

#include "layers/swmmelementsymboladapter.h"
#include "ui/widgets/colorbutton.h"
#include "ui/widgets/stylepreviewswatch.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace openswmmvis::ui {

SwmmElementSymbolEditor::SwmmElementSymbolEditor(SwmmElementSymbolAdapter *adapter,
                                                  QWidget *parent)
    : IStyleEditorWidget(parent), m_adapter(adapter)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    // ── Symbology group ────────────────────────────────────────────────
    auto *symBox = new QGroupBox(tr("Symbology"), this);
    auto *symForm = new QFormLayout(symBox);

    m_fillBtn = new ColorButton(this);
    symForm->addRow(tr("F&ill colour:"), m_fillBtn);

    m_outlineBtn = new ColorButton(this);
    symForm->addRow(tr("&Outline colour:"), m_outlineBtn);

    m_outlineWSpin = new QDoubleSpinBox(this);
    m_outlineWSpin->setRange(0.0, 20.0);
    m_outlineWSpin->setDecimals(2);
    m_outlineWSpin->setSingleStep(0.25);
    m_outlineWSpin->setSuffix(tr(" px"));
    symForm->addRow(tr("O&utline width:"), m_outlineWSpin);

    m_sizeSpin = new QDoubleSpinBox(this);
    m_sizeSpin->setRange(0.5, 60.0);
    m_sizeSpin->setDecimals(1);
    m_sizeSpin->setSingleStep(0.5);
    m_sizeSpin->setSuffix(tr(" px"));
    symForm->addRow(tr("Si&ze / line width:"), m_sizeSpin);

    root->addWidget(symBox);

    // ── Labels group ───────────────────────────────────────────────────
    auto *labelBox = new QGroupBox(tr("Labels"), this);
    auto *labelForm = new QFormLayout(labelBox);

    m_showLabelBox = new QCheckBox(tr("Show element labels"), this);
    labelForm->addRow(QString(), m_showLabelBox);

    m_labelFontCombo = new QFontComboBox(this);
    labelForm->addRow(tr("Fo&nt:"), m_labelFontCombo);

    m_labelColorBtn = new ColorButton(this);
    labelForm->addRow(tr("Co&lour:"), m_labelColorBtn);

    root->addWidget(labelBox);

    // ── Flow arrows group (only meaningful for link kinds, but we surface
    // the controls unconditionally; the adapter's writer is a no-op when
    // the underlying kind doesn't use them). ───────────────────────────
    auto *arrowBox = new QGroupBox(tr("Flow direction arrows"), this);
    auto *arrowForm = new QFormLayout(arrowBox);

    m_showArrowsBox = new QCheckBox(tr("Show flow arrows"), this);
    arrowForm->addRow(QString(), m_showArrowsBox);

    m_arrowSizeSpin = new QDoubleSpinBox(this);
    m_arrowSizeSpin->setRange(2.0, 60.0);
    m_arrowSizeSpin->setDecimals(1);
    m_arrowSizeSpin->setSingleStep(0.5);
    m_arrowSizeSpin->setSuffix(tr(" px"));
    arrowForm->addRow(tr("&Length:"), m_arrowSizeSpin);

    m_arrowWidSpin = new QDoubleSpinBox(this);
    m_arrowWidSpin->setRange(2.0, 60.0);
    m_arrowWidSpin->setDecimals(1);
    m_arrowWidSpin->setSingleStep(0.5);
    m_arrowWidSpin->setSuffix(tr(" px"));
    m_arrowWidSpin->setToolTip(tr("Arrowhead width across the link — "
                                  "independent of its length."));
    arrowForm->addRow(tr("&Width:"), m_arrowWidSpin);

    m_arrowColorBtn = new ColorButton(this);
    arrowForm->addRow(tr("Colou&r:"), m_arrowColorBtn);

    m_arrowsFlowPosBox = new QCheckBox(tr("Only when flow > 0"), this);
    arrowForm->addRow(QString(), m_arrowsFlowPosBox);

    root->addWidget(arrowBox);

    // ── Live preview ───────────────────────────────────────────────────
    auto *previewBox = new QGroupBox(tr("Preview"), this);
    auto *previewLay = new QVBoxLayout(previewBox);
    m_preview = new StylePreviewSwatch(previewBox);
    m_preview->setKind(StylePreviewSwatch::PointKind);
    previewLay->addWidget(m_preview);
    root->addWidget(previewBox);

    // ── Bindings ───────────────────────────────────────────────────────
    connect(m_fillBtn, &ColorButton::colorChanged,
            this, [this](const QColor &c) { m_adapter->setFillColor(c); });
    connect(m_outlineBtn, &ColorButton::colorChanged,
            this, [this](const QColor &c) { m_adapter->setOutlineColor(c); });
    connect(m_outlineWSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_adapter->setOutlineWidth(v); });
    connect(m_sizeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_adapter->setSize(v); });

    connect(m_showLabelBox, &QCheckBox::toggled,
            this, [this](bool v) { m_adapter->setShowLabel(v); });
    connect(m_labelFontCombo, &QFontComboBox::currentFontChanged,
            this, [this](const QFont &f) { m_adapter->setLabelFont(f); });
    connect(m_labelColorBtn, &ColorButton::colorChanged,
            this, [this](const QColor &c) { m_adapter->setLabelColor(c); });

    connect(m_showArrowsBox, &QCheckBox::toggled,
            this, [this](bool v) { m_adapter->setShowArrows(v); });
    connect(m_arrowSizeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_adapter->setArrowSize(v); });
    connect(m_arrowWidSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_adapter->setArrowWidth(v); });
    connect(m_arrowColorBtn, &ColorButton::colorChanged,
            this, [this](const QColor &c) { m_adapter->setArrowColor(c); });
    connect(m_arrowsFlowPosBox, &QCheckBox::toggled,
            this, [this](bool v) { m_adapter->setArrowOnlyWhenFlowPos(v); });

    connect(m_adapter, &SwmmElementSymbolAdapter::symbolChanged,
            this, &SwmmElementSymbolEditor::refreshFromModel,
            Qt::UniqueConnection);

    refreshFromModel();
}

void SwmmElementSymbolEditor::refreshFromModel()
{
    if (!m_adapter) return;
    QSignalBlocker b1(m_fillBtn), b2(m_outlineBtn), b3(m_outlineWSpin),
                   b4(m_sizeSpin), b5(m_showLabelBox), b6(m_labelFontCombo),
                   b7(m_labelColorBtn), b8(m_showArrowsBox), b9(m_arrowSizeSpin),
                   b10(m_arrowColorBtn), b11(m_arrowsFlowPosBox),
                   b12(m_arrowWidSpin);

    m_fillBtn->setColor(m_adapter->fillColor());
    m_outlineBtn->setColor(m_adapter->outlineColor());
    m_outlineWSpin->setValue(m_adapter->outlineWidth());
    m_sizeSpin->setValue(m_adapter->size());

    m_showLabelBox->setChecked(m_adapter->showLabel());
    m_labelFontCombo->setCurrentFont(m_adapter->labelFont());
    m_labelColorBtn->setColor(m_adapter->labelColor());

    m_showArrowsBox->setChecked(m_adapter->showArrows());
    m_arrowSizeSpin->setValue(m_adapter->arrowSize());
    m_arrowWidSpin->setValue(m_adapter->arrowWidth());
    m_arrowColorBtn->setColor(m_adapter->arrowColor());
    m_arrowsFlowPosBox->setChecked(m_adapter->arrowOnlyWhenFlowPos());

    updatePreview();
}

void SwmmElementSymbolEditor::updatePreview()
{
    if (!m_preview || !m_adapter) return;
    m_preview->setColor(m_adapter->fillColor());
    QPen outline(m_adapter->outlineColor());
    outline.setWidthF(m_adapter->outlineWidth());
    m_preview->setStrokePen(outline);
    m_preview->setMarkerSizePx(m_adapter->size());
    m_preview->setLineWidthPx(m_adapter->size());
    m_preview->setShowArrows(m_adapter->showArrows());
}

// Registry
REGISTER_STYLE_EDITOR(
    SwmmElementSymbolAdapter,
    [](QObject *obj, QWidget *parent) -> IStyleEditorWidget * {
        if (auto *a = qobject_cast<SwmmElementSymbolAdapter *>(obj))
            return new SwmmElementSymbolEditor(a, parent);
        return nullptr;
    })

} // namespace openswmmvis::ui
