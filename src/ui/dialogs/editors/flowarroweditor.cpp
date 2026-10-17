/*!
 * \file   flowarroweditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/editors/flowarroweditor.h"

#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "render/sublayers/feature/featuresublayer.h"
#include "render/sublayers/feature/featuresublayerstyle.h"
#include "ui/widgets/colorbutton.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSignalBlocker>

namespace openswmmvis::ui {

namespace {

/*! The link kinds that carry a flow direction at all. Nodes and
 *  subcatchments have no upstream/downstream, so the group stays hidden. */
bool isLinkKind(OpenSWMMVis::SwmmCategory c)
{
    using namespace OpenSWMMVis;
    return c == CatConduits || c == CatPumps || c == CatOrifices
        || c == CatWeirs    || c == CatOutlets;
}

OpenSWMM::Render::LineFeatureSublayerStyle *
resultsLineStyle(SWMMResultsLayer *layer, OpenSWMMVis::SwmmCategory c)
{
    if (!layer) return nullptr;
    auto *sub = layer->featureSublayer(c);
    return sub ? qobject_cast<OpenSWMM::Render::LineFeatureSublayerStyle *>(sub->featureStyle())
               : nullptr;
}

} // namespace

FlowArrowEditor::FlowArrowEditor(OpenSWMMVisLayer *hostLayer,
                                 OpenSWMMVis::SwmmCategory category,
                                 QWidget *parent)
    : QGroupBox(tr("Flow direction arrows"), parent)
    , m_modelLayer(qobject_cast<SWMMModelLayer *>(hostLayer))
    , m_resultsLayer(qobject_cast<SWMMResultsLayer *>(hostLayer))
    , m_category(category)
{
    auto *aForm = new QFormLayout(this);
    m_showChk = new QCheckBox(tr("Show flow arrows"), this);
    aForm->addRow(QString(), m_showChk);
    m_sizeSpin = new QDoubleSpinBox(this);
    m_sizeSpin->setRange(2.0, 60.0);
    m_sizeSpin->setDecimals(1);
    m_sizeSpin->setSingleStep(0.5);
    m_sizeSpin->setSuffix(tr(" px"));
    aForm->addRow(tr("&Length:"), m_sizeSpin);
    m_widSpin = new QDoubleSpinBox(this);
    m_widSpin->setRange(2.0, 60.0);
    m_widSpin->setDecimals(1);
    m_widSpin->setSingleStep(0.5);
    m_widSpin->setSuffix(tr(" px"));
    m_widSpin->setToolTip(tr("Arrowhead width across the link — "
                             "independent of its length."));
    aForm->addRow(tr("&Width:"), m_widSpin);
    m_colorBtn = new ColorButton(this);
    aForm->addRow(tr("Colou&r:"), m_colorBtn);

    connect(m_showChk, &QCheckBox::toggled, this, [this](bool) { onChanged(); });
    connect(m_sizeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double) { onChanged(); });
    connect(m_widSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double) { onChanged(); });
    connect(m_colorBtn, &ColorButton::colorChanged,
            this, [this](const QColor &) { onChanged(); });

    refreshFromModel();
}

bool FlowArrowEditor::hasChannel() const
{
    if (!isLinkKind(m_category)) return false;
    return m_modelLayer || resultsLineStyle(m_resultsLayer, m_category);
}

void FlowArrowEditor::refreshFromModel()
{
    bool   show = false;
    double size = 10.0;
    double width = 12.0;
    QColor color(34, 34, 34);
    const bool have = hasChannel();

    if (have && m_modelLayer) {
        show  = m_modelLayer->linkArrowsEnabled(m_category);
        size  = m_modelLayer->linkArrowSize(m_category);
        width = m_modelLayer->linkArrowWidth(m_category);
        color = m_modelLayer->linkArrowColor(m_category);
    } else if (auto *ls = have ? resultsLineStyle(m_resultsLayer, m_category) : nullptr) {
        show  = ls->showFlowArrows();
        size  = ls->arrowLengthPx();
        width = ls->arrowWidthPx();
        color = ls->arrowColor();
    }

    setVisible(have);
    if (!have) return;

    QSignalBlocker b1(m_showChk), b2(m_sizeSpin), b3(m_colorBtn), b4(m_widSpin);
    m_showChk->setChecked(show);
    m_sizeSpin->setValue(size);
    m_widSpin->setValue(width);
    m_colorBtn->setColor(color);
    m_sizeSpin->setEnabled(show);
    m_widSpin->setEnabled(show);
    m_colorBtn->setEnabled(show);
}

void FlowArrowEditor::onChanged()
{
    const bool   show  = m_showChk->isChecked();
    const double size  = m_sizeSpin->value();
    const double width = m_widSpin->value();
    const QColor color = m_colorBtn->color();
    m_sizeSpin->setEnabled(show);
    m_widSpin->setEnabled(show);
    m_colorBtn->setEnabled(show);

    if (m_modelLayer) {
        // The per-kind setters resync the persistent symbol adapter and emit
        // repaintRequested, so the single-symbol editor and Cancel rollback
        // see the same edit this group just made.
        m_modelLayer->setLinkArrowsEnabled(m_category, show);
        m_modelLayer->setLinkArrowSize(m_category, size);
        m_modelLayer->setLinkArrowWidth(m_category, width);
        m_modelLayer->setLinkArrowColor(m_category, color);
    } else if (auto *ls = resultsLineStyle(m_resultsLayer, m_category)) {
        ls->setShowFlowArrows(show);
        ls->setArrowLengthPx(size);
        ls->setArrowWidthPx(width);
        ls->setArrowColor(color);
    }
}

} // namespace openswmmvis::ui
