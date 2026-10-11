/*!
 * \file   labelstab.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/labelstab.h"
#include "layers/swmmmodellayer.h"
#include "render/iattributeprovider.h"
#include "ui/uiscrollhelpers.h"
#include "ui/widgets/labelconfigeditor.h"
#include <QSet>
#include <QVBoxLayout>

namespace openswmmvis::ui {

LabelsTab::LabelsTab(OpenSWMMVisLayer *layer, QWidget *parent)
    : QWidget(parent), m_layer(layer)
{
    setMinimumWidth(OpenSWMM::Ui::kComboMinWidthPx + 240);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    m_editor = new LabelConfigEditor(this);
    layout->addWidget(m_editor);
    layout->addStretch();
    connect(m_editor, &LabelConfigEditor::configChanged, this, &LabelsTab::pushToModel);
    if (layer) {
        connect(layer, &OpenSWMMVisLayer::labelConfigChanged, this, &LabelsTab::refreshFromModel);
        connect(layer, &QObject::destroyed, this, [this] { setEnabled(false); });
    }
    refreshFromModel();
}

void LabelsTab::refreshFromModel()
{
    if (!m_layer) return;
    QVector<OpenSWMM::Render::AttributeField> fields;
    QSet<QString> seen;
    if (auto *provider = dynamic_cast<OpenSWMM::Render::IAttributeProvider *>(m_layer.data())) {
        for (int category = 0; category < SWMMModelLayer::NumCategories; ++category) {
            for (const auto &field : provider->availableAttributes(
                     static_cast<OpenSWMMVis::SwmmCategory>(category))) {
                if (seen.contains(field.name)) continue;
                seen.insert(field.name);
                fields.append(field);
            }
        }
    }
    m_editor->setAvailableFields(fields);
    m_editor->setConfig(m_layer->labelConfig());
}

void LabelsTab::pushToModel()
{
    if (m_layer) m_layer->setLabelConfig(m_editor->config());
}

} // namespace openswmmvis::ui
