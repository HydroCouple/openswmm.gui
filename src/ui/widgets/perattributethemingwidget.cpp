/*!
 * \file   perattributethemingwidget.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/perattributethemingwidget.h"

#include "animation/animationcontroller.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>

namespace openswmmvis::ui {

PerAttributeThemingWidget::PerAttributeThemingWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    populateCombos();
}

void PerAttributeThemingWidget::buildUi()
{
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(2, 0, 2, 0);
    h->setSpacing(4);

    m_layerLabel = new QLabel(tr("Theme:"), this);
    h->addWidget(m_layerLabel);

    h->addWidget(new QLabel(tr("N:"), this));
    m_nodeCombo = new QComboBox(this);
    m_nodeCombo->setToolTip(tr("Node attribute that drives node colour fill"));
    h->addWidget(m_nodeCombo);

    h->addWidget(new QLabel(tr("L:"), this));
    m_linkCombo = new QComboBox(this);
    m_linkCombo->setToolTip(tr("Link attribute that drives link colour fill"));
    h->addWidget(m_linkCombo);

    h->addWidget(new QLabel(tr("S:"), this));
    m_subCombo = new QComboBox(this);
    m_subCombo->setToolTip(tr("Subcatchment attribute that drives subcatch colour fill"));
    h->addWidget(m_subCombo);

    connect(m_nodeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PerAttributeThemingWidget::onNodeVarChanged);
    connect(m_linkCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PerAttributeThemingWidget::onLinkVarChanged);
    connect(m_subCombo,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PerAttributeThemingWidget::onSubVarChanged);
}

void PerAttributeThemingWidget::populateCombos()
{
    m_suppress = true;
    m_nodeCombo->clear();
    m_nodeCombo->addItem(tr("Depth"),    static_cast<int>(SWMMResultVariable::NodeDepth));
    m_nodeCombo->addItem(tr("Head"),     static_cast<int>(SWMMResultVariable::NodeHead));
    m_nodeCombo->addItem(tr("Volume"),   static_cast<int>(SWMMResultVariable::NodeVolume));
    m_nodeCombo->addItem(tr("Inflow"),   static_cast<int>(SWMMResultVariable::NodeInflow));
    m_nodeCombo->addItem(tr("Overflow"), static_cast<int>(SWMMResultVariable::NodeOverflow));

    m_linkCombo->clear();
    m_linkCombo->addItem(tr("Flow"),     static_cast<int>(SWMMResultVariable::LinkFlow));
    m_linkCombo->addItem(tr("Depth"),    static_cast<int>(SWMMResultVariable::LinkDepth));
    m_linkCombo->addItem(tr("Velocity"), static_cast<int>(SWMMResultVariable::LinkVelocity));
    m_linkCombo->addItem(tr("Capacity"), static_cast<int>(SWMMResultVariable::LinkCapacity));

    m_subCombo->clear();
    m_subCombo->addItem(tr("Runoff"),       static_cast<int>(SWMMResultVariable::SubcatchRunoff));
    m_subCombo->addItem(tr("Infiltration"), static_cast<int>(SWMMResultVariable::SubcatchInfiltration));
    m_subCombo->addItem(tr("Evaporation"),  static_cast<int>(SWMMResultVariable::SubcatchEvaporation));
    m_subCombo->addItem(tr("Snow Depth"),   static_cast<int>(SWMMResultVariable::SubcatchSnowDepth));
    m_suppress = false;
}

void PerAttributeThemingWidget::setAnimationController(AnimationController *ac)
{
    if (m_ac) {
        disconnect(m_ac.data(), nullptr, this, nullptr);
    }
    m_ac = ac;
    if (!ac) return;
    connect(ac, &AnimationController::primaryLayerChanged,
            this, &PerAttributeThemingWidget::onPrimaryLayerChanged);
}

void PerAttributeThemingWidget::setPrimaryLayer(SWMMResultsLayer *layer)
{
    if (m_layer.data() == layer) return;
    if (m_layer) disconnect(m_layer.data(), nullptr, this, nullptr);
    m_layer = layer;
    if (layer) {
        connect(layer, &SWMMResultsLayer::variableChanged,
                this, &PerAttributeThemingWidget::onLayerVariableChanged);
        pushLayerVariableToCombos();
    }
}

void PerAttributeThemingWidget::onPrimaryLayerChanged(SWMMResultsLayer *layer)
{
    setPrimaryLayer(layer);
}

void PerAttributeThemingWidget::onLayerVariableChanged(SWMMResultVariable var)
{
    Q_UNUSED(var);
    pushLayerVariableToCombos();
}

void PerAttributeThemingWidget::pushLayerVariableToCombos()
{
    if (!m_layer) return;
    m_suppress = true;
    const SWMMResultVariable v = m_layer->variable();
    auto selectByData = [&](QComboBox *cb) {
        const int idx = cb->findData(static_cast<int>(v));
        if (idx >= 0) cb->setCurrentIndex(idx);
    };
    selectByData(m_nodeCombo);
    selectByData(m_linkCombo);
    selectByData(m_subCombo);
    m_suppress = false;
}

void PerAttributeThemingWidget::onNodeVarChanged(int index)
{
    if (m_suppress || !m_layer || index < 0) return;
    m_layer->setVariable(
        static_cast<SWMMResultVariable>(m_nodeCombo->itemData(index).toInt()));
}

void PerAttributeThemingWidget::onLinkVarChanged(int index)
{
    if (m_suppress || !m_layer || index < 0) return;
    m_layer->setVariable(
        static_cast<SWMMResultVariable>(m_linkCombo->itemData(index).toInt()));
}

void PerAttributeThemingWidget::onSubVarChanged(int index)
{
    if (m_suppress || !m_layer || index < 0) return;
    m_layer->setVariable(
        static_cast<SWMMResultVariable>(m_subCombo->itemData(index).toInt()));
}

} // namespace openswmmvis::ui
