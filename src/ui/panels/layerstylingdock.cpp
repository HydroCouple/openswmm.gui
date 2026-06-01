/*!
 * \file   layerstylingdock.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/panels/layerstylingdock.h"

#include "layers/openswmmvislayer.h"
#include "render/rulelist.h"
#include "ui/dialogs/rulesymbologytab.h"

#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace openswmmvis::ui {

// ---------------------------------------------------------------------------

LayerStylingDock::LayerStylingDock(QWidget *parent)
    : QDockWidget(tr("Layer Styling"), parent)
{
    setObjectName(QStringLiteral("LayerStylingDock"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable
              | QDockWidget::DockWidgetMovable
              | QDockWidget::DockWidgetFloatable);
    buildUi();
}

LayerStylingDock::~LayerStylingDock() = default;

// ---------------------------------------------------------------------------

void LayerStylingDock::buildUi()
{
    auto *root = new QWidget(this);
    auto *vbox = new QVBoxLayout(root);
    vbox->setContentsMargins(8, 8, 8, 8);
    vbox->setSpacing(6);

    m_header = new QLabel(root);
    m_header->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_header->setWordWrap(true);
    QFont hf = m_header->font();
    hf.setBold(true);
    m_header->setFont(hf);
    m_header->setText(headerTextForCurrent());
    vbox->addWidget(m_header);

    // Scrollable body — the symbology tab grows vertically with renderer
    // panel content; a scroll area keeps the dock usable even in a thin
    // window.
    auto *scroll = new QScrollArea(root);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_stack = new QStackedWidget(scroll);
    scroll->setWidget(m_stack);
    vbox->addWidget(scroll, 1);

    // Placeholder page (index 0). Shown when no layer is selected or
    // the active layer doesn't carry a RuleList.
    m_placeholder = new QWidget(m_stack);
    auto *plvbox = new QVBoxLayout(m_placeholder);
    plvbox->setContentsMargins(8, 24, 8, 8);
    auto *plLabel = new QLabel(
        tr("Select a layer in the Layers panel to edit its styling.\n\n"
           "Edits apply live to the canvas — no Apply button needed."),
        m_placeholder);
    plLabel->setWordWrap(true);
    plLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    plLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    plvbox->addWidget(plLabel);
    plvbox->addStretch();
    m_stack->addWidget(m_placeholder);

    setWidget(root);
}

// ---------------------------------------------------------------------------

QString LayerStylingDock::headerTextForCurrent() const
{
    if (!m_layer)
        return tr("Layer Styling — no layer selected");
    return tr("Layer Styling — %1").arg(m_layer->name());
}

// ---------------------------------------------------------------------------

void LayerStylingDock::setLayer(OpenSWMMVisLayer *layer)
{
    if (m_layer == layer)
        return;
    m_layer = layer;
    rebuild();
}

// ---------------------------------------------------------------------------

void LayerStylingDock::rebuild()
{
    // Tear down the prior tab. The QStackedWidget owns its children, so
    // explicit deletion through removeWidget + deleteLater is the
    // surgical path.
    if (m_tab) {
        m_stack->removeWidget(m_tab);
        m_tab->deleteLater();
        m_tab = nullptr;
    }

    m_header->setText(headerTextForCurrent());

    if (!m_layer) {
        m_stack->setCurrentWidget(m_placeholder);
        return;
    }

    auto *rl = m_layer->ruleList();
    if (!rl) {
        // Layer has no Rule Model (e.g. basemap / WMS / WMTS — those
        // skip Symbology in LayerStyleDialog too). Fall back to the
        // placeholder rather than mounting an empty editor.
        m_stack->setCurrentWidget(m_placeholder);
        return;
    }

    m_tab = new RuleSymbologyTab(rl, m_stack);
    m_stack->addWidget(m_tab);
    m_stack->setCurrentWidget(m_tab);
}

} // namespace openswmmvis::ui
