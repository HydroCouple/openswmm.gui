/*!
 * \file   inletjunctionsetupdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/inletjunctionsetupdialog.h"

#include "inlet/inletregistry.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/nodepicksession.h"
#include "ui/dialogs/dialoglayoutpersistence.h"   // floatingPanelFlags
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
#include <QToolButton>
#include <QVBoxLayout>

namespace openswmmvis::ui {

InletJunctionSetupDialog::InletJunctionSetupDialog(SWMMModelLayer *layer,
                                                   QVector<int> excludeNodes,
                                                   QWidget *parent)
    : QDialog(parent), m_layer(layer), m_exclude(std::move(excludeNodes))
{
    setObjectName(QStringLiteral("InletJunctionSetupDialog"));
    setWindowTitle(tr("New Inlet Junction"));
    // Non-modal floating panel: the map must stay clickable for the capture
    // node pick (see the header). floatingPanelFlags() keeps it above the
    // main window without stealing keyboard focus.
    setModal(false);
    setWindowFlags(floatingPanelFlags() | Qt::CustomizeWindowHint
                   | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

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
    m_designPicker->setObjectName(QStringLiteral("designPicker"));
    form->addRow(tr("Inlet Design"), m_designPicker);

    m_capturePicker = new LabeledPickerCombo(QString(), this);
    m_capturePicker->setObjectName(QStringLiteral("capturePicker"));
    m_capturePicker->button()->setToolTip(tr("Pick the capture node on the map"));
    form->addRow(tr("Capture Node"), m_capturePicker);

    m_placement = new QComboBox(this);
    m_placement->addItem(tr("Automatic"), int(SWMM_INLET_AUTOMATIC));
    m_placement->addItem(tr("On Grade"),  int(SWMM_INLET_ON_GRADE));
    m_placement->addItem(tr("On Sag"),    int(SWMM_INLET_ON_SAG));
    form->addRow(tr("Placement"), m_placement);

    root->addLayout(form);

    m_pickHint = new QLabel(this);
    m_pickHint->setWordWrap(true);
    m_pickHint->setVisible(false);
    root->addWidget(m_pickHint);

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
    connect(m_capturePicker, &LabeledPickerCombo::currentTextChanged,
            this, [this](const QString &) { updateOkEnabled(); });
    connect(m_capturePicker, &LabeledPickerCombo::pickerClicked,
            this, &InletJunctionSetupDialog::startCaptureNodePick);

    // The map pick needs a canvas to pick on.
    m_capturePicker->button()->setEnabled(m_layer && m_layer->editCanvas());

    updateOkEnabled();
}

InletJunctionSetupDialog::~InletJunctionSetupDialog()
{
    endCaptureNodePick();
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
    if (!m_capturePicker || !m_layer || !m_layer->engine()) return;
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
    // Preselect the first eligible node (the "(none)" placeholder keeps OK
    // disabled otherwise).
    m_capturePicker->setItems(items, items.isEmpty() ? QString() : items.first());
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

// ── Capture node: pick on the map ───────────────────────────────────────

void InletJunctionSetupDialog::setPickHint(const QString &text)
{
    if (!m_pickHint) return;
    m_pickHint->setText(text);
    m_pickHint->setVisible(!text.isEmpty());
}

void InletJunctionSetupDialog::startCaptureNodePick()
{
    MapCanvas *canvas = m_layer ? m_layer->editCanvas() : nullptr;
    if (!canvas) return;
    if (m_pick && m_pick->isActive()) return;
    endCaptureNodePick();

    m_pick = new NodePickSession(canvas, this);
    connect(m_pick, &NodePickSession::nodePicked,
            this, &InletJunctionSetupDialog::onCaptureNodePicked);
    connect(m_pick, &NodePickSession::cancelled, this, [this]() {
        setPickHint(QString());
        if (m_pick) m_pick->deleteLater();
    });
    setPickHint(tr("Click a node on the map to make it the capture node. "
                   "Esc cancels."));
}

void InletJunctionSetupDialog::endCaptureNodePick()
{
    if (m_pick) {
        m_pick->finish();
        m_pick->deleteLater();
        m_pick.clear();
    }
    setPickHint(QString());
}

bool InletJunctionSetupDialog::isPickingCaptureNode() const
{
    return m_pick && m_pick->isActive();
}

void InletJunctionSetupDialog::onCaptureNodePicked(SWMMModelLayer *layer,
                                                    const QString &name, int nodeIdx)
{
    if (!m_layer || layer != m_layer) {
        setPickHint(tr("Pick a node of this model."));
        return;
    }
    if (m_exclude.contains(nodeIdx)) {
        setPickHint(tr("\"%1\" is an end of the host conduit — pick another node.")
                        .arg(name));
        return;
    }
    int isVirtual = 0;
    swmm_node_is_virtual(m_layer->engine(), nodeIdx, &isVirtual);
    if (isVirtual) {
        setPickHint(tr("\"%1\" is a virtual or inlet junction and cannot receive "
                       "the capture — pick another node.").arg(name));
        return;
    }
    m_capturePicker->setCurrentText(name);
    updateOkEnabled();
    endCaptureNodePick();
    raise();
    activateWindow();
}

QString InletJunctionSetupDialog::inletDesign() const
{
    return m_designPicker ? m_designPicker->currentText() : QString();
}

QString InletJunctionSetupDialog::captureNode() const
{
    return m_capturePicker ? m_capturePicker->currentText() : QString();
}

int InletJunctionSetupDialog::placement() const
{
    return m_placement ? m_placement->currentData().toInt()
                       : int(SWMM_INLET_AUTOMATIC);
}

} // namespace openswmmvis::ui
