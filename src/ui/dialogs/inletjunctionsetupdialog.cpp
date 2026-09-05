/*!
 * \file   inletjunctionsetupdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/inletjunctionsetupdialog.h"

#include "inlet/inletregistry.h"
#include "layers/swmmmodellayer.h"
#include "ui/dialogs/inleteditordialog.h"
#include "ui/properties/xsectshapegeom.h"   // kXsectStreetId
#include "ui/widgets/labeledcontrols.h"

#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace openswmmvis::ui {

InletJunctionSetupDialog::InletJunctionSetupDialog(SWMMModelLayer *layer,
                                                   QVector<int> excludeNodes,
                                                   QWidget *parent)
    : QDialog(parent), m_layer(layer), m_exclude(std::move(excludeNodes))
{
    setObjectName(QStringLiteral("InletJunctionSetupDialog"));
    setWindowTitle(tr("New Inlet Junction"));
    setModal(true);

    auto *root = new QVBoxLayout(this);
    auto *intro = new QLabel(
        tr("An inlet junction captures street flow into a separate drainage "
           "node. Choose the inlet design and the node it discharges to."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto *form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_designPicker = new LabeledPickerCombo(QString(), this);
    form->addRow(tr("Inlet Design"), m_designPicker);

    m_captureCombo = new QComboBox(this);
    form->addRow(tr("Capture Node"), m_captureCombo);

    m_placement = new QComboBox(this);
    m_placement->addItem(tr("Automatic"), int(SWMM_INLET_AUTOMATIC));
    m_placement->addItem(tr("On Grade"),  int(SWMM_INLET_ON_GRADE));
    m_placement->addItem(tr("On Sag"),    int(SWMM_INLET_ON_SAG));
    form->addRow(tr("Placement"), m_placement);

    root->addLayout(form);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                     this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(m_buttons);

    refreshDesignItems();
    refreshCaptureItems();

    connect(m_designPicker, &LabeledPickerCombo::currentTextChanged,
            this, [this](const QString &) { updateOkEnabled(); });
    connect(m_designPicker, &LabeledPickerCombo::pickerClicked,
            this, &InletJunctionSetupDialog::onDesignPickerClicked);
    connect(m_captureCombo, &QComboBox::currentTextChanged,
            this, [this](const QString &) { updateOkEnabled(); });

    updateOkEnabled();
}

void InletJunctionSetupDialog::refreshDesignItems(const QString &selected)
{
    if (!m_designPicker || !m_layer || !m_layer->engine()) return;
    QStringList items;
    const int n = swmm_inlet_count(m_layer->engine());
    for (int i = 0; i < n; ++i)
        if (const char *id = swmm_inlet_id(m_layer->engine(), i))
            if (*id) items << QString::fromUtf8(id);
    m_designPicker->setItems(items, selected);
}

void InletJunctionSetupDialog::refreshCaptureItems()
{
    if (!m_captureCombo || !m_layer || !m_layer->engine()) return;
    SWMM_Engine eng = m_layer->engine();

    QStringList items;
    const int n = swmm_node_count(eng);
    for (int i = 0; i < n; ++i) {
        if (m_exclude.contains(i)) continue;
        // Engine rule 627: a zero-storage node cannot receive the capture.
        int isVirtual = 0;
        swmm_node_is_virtual(eng, i, &isVirtual);
        if (isVirtual) continue;
        if (const char *id = swmm_node_id(eng, i))
            if (*id) items << QString::fromUtf8(id);
    }
    items.sort(Qt::CaseInsensitive);
    // No blank entry: OK stays disabled until a real node is chosen, and a
    // placeholder would only make the disabled state look like a bug.
    m_captureCombo->clear();
    m_captureCombo->addItems(items);
    if (!items.isEmpty()) m_captureCombo->setCurrentIndex(0);
}

void InletJunctionSetupDialog::updateOkEnabled()
{
    if (!m_buttons) return;
    const bool ready = !inletDesign().isEmpty() && !captureNode().isEmpty();
    if (auto *ok = m_buttons->button(QDialogButtonBox::Ok)) {
        ok->setEnabled(ready);
        ok->setToolTip(ready ? QString()
                             : tr("An inlet junction needs both an inlet "
                                  "design and a capture node."));
    }
}

void InletJunctionSetupDialog::onDesignPickerClicked()
{
    if (!m_layer) return;
    using openswmmvis::inlet::InletRegistry;
    auto *reg = qobject_cast<InletRegistry *>(m_layer->ensureInletRegistry());
    if (!reg) return;
    // An inlet junction always sits between two STREET conduits, so the
    // editor is filtered to the designs legal on a gutter.
    const QString chosen = InletEditorDialog::pickInlet(
        reg, m_layer, /*undoStack=*/nullptr, this, openswmmvis::kXsectStreetId);
    if (chosen.isEmpty()) return;
    refreshDesignItems(chosen);
    updateOkEnabled();
}

QString InletJunctionSetupDialog::inletDesign() const
{
    return m_designPicker ? m_designPicker->currentText() : QString();
}

QString InletJunctionSetupDialog::captureNode() const
{
    return m_captureCombo ? m_captureCombo->currentText() : QString();
}

int InletJunctionSetupDialog::placement() const
{
    return m_placement ? m_placement->currentData().toInt()
                       : int(SWMM_INLET_AUTOMATIC);
}

} // namespace openswmmvis::ui
