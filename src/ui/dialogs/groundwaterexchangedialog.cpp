/*!
 * \file   groundwaterexchangedialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/dialogs/groundwaterexchangedialog.h"

#include "core/unitsystem.h"
#include "layers/swmmmodellayer.h"
#include "ui/properties/groundwatersummary.h"
#include "ui/uiscrollhelpers.h"
#include "ui/precisenumericvalue.h"
#include <cmath>
#include "ui/widgets/gwfexpressionedit.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

using openswmmvis::ui::GwfExpressionEdit;

namespace {
constexpr double kBig = 1e12;
// The engine's MISSING value. In [GROUNDWATER] it is Egwt `*`: use the
// receiving node's invert as the lateral-flow threshold.
constexpr double kMissing = -1.0e10;
}

GroundwaterExchangeDialog::GroundwaterExchangeDialog(SubcatchCompoundEditRef ref,
                                                     QWidget *parent)
    : QDialog(parent), m_ref(std::move(ref))
{
    // Naming wires the app-wide layout persistence (DialogLayoutPersistence).
    setObjectName(QStringLiteral("GroundwaterExchangeDialog"));
    setWindowTitle(tr("Groundwater Exchange — %1").arg(m_ref.subName));
    resize(600, 620);

    buildUi_();
    loadFromEngine_();
    if (m_ref.layer) {
        connect(m_ref.layer, &QObject::destroyed, this, &GroundwaterExchangeDialog::invalidateContext);
        connect(m_ref.layer, SIGNAL(engineAboutToClose()), this, SLOT(invalidateContext()));
    }
}

int GroundwaterExchangeDialog::subIdx() const
{
    if (!m_ref.engine || m_ref.subName.isEmpty()) return -1;
    return swmm_subcatch_index(m_ref.engine, m_ref.subName.toUtf8().constData());
}

QString GroundwaterExchangeDialog::updatedSummary() const
{
    return groundwaterSummary(m_ref.engine, subIdx());
}

bool GroundwaterExchangeDialog::canApply() const
{
    return m_loadedOk && m_hasAquifer && m_lateralOk && m_deepOk;
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
void GroundwaterExchangeDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);

    // Header: subcatchment + aquifer (read-only — assigned on the property row).
    m_header = new QLabel(this);
    m_header->setWordWrap(true);
    m_header->setTextFormat(Qt::RichText);
    outer->addWidget(m_header);

    m_hint = new QLabel(tr("Assign an aquifer in the property browser to "
                           "enable groundwater exchange."), this);
    m_hint->setWordWrap(true);
    outer->addWidget(m_hint);
    m_writeError = new QLabel(this);
    m_writeError->setObjectName(QStringLiteral("gwWriteError"));
    m_writeError->setTextFormat(Qt::PlainText);
    m_writeError->setWordWrap(true);
    m_writeError->setAccessibleName(tr("Groundwater edit error"));
    m_writeError->hide();
    outer->addWidget(m_writeError);

    m_form = new QWidget(this);
    auto *formLay = new QVBoxLayout(m_form);
    formLay->setContentsMargins(0, 0, 0, 0);

    const QString L = UnitSystem::instance()
                          ? UnitSystem::instance()->lengthLabel()
                          : QStringLiteral("ft");

    // ── Receiving node ──────────────────────────────────────────────────────
    auto *nodeGrp  = new QGroupBox(tr("Receiving node"), m_form);
    auto *nodeForm = new QFormLayout(nodeGrp);
    m_node = new QComboBox(nodeGrp);
    m_node->setObjectName(QStringLiteral("gwNode"));
    m_node->setToolTip(tr("Node that receives the subcatchment's lateral "
                          "groundwater flow ([GROUNDWATER] Node)."));
    m_surfEl = new QDoubleSpinBox(nodeGrp);
    m_surfEl->setObjectName(QStringLiteral("gwSurfEl"));
    m_surfEl->setRange(-kBig, kBig);
    m_surfEl->setDecimals(4);
    m_surfEl->setSuffix(QStringLiteral(" %1").arg(L));
    m_surfEl->setToolTip(tr("Elevation of the ground surface for the "
                            "subcatchment that lies above the aquifer."));
    nodeForm->addRow(tr("&Receiving Node"), m_node);
    nodeForm->addRow(tr("Surfa&ce Elevation"), m_surfEl);
    formLay->addWidget(nodeGrp);

    // ── Standard lateral flow ───────────────────────────────────────────────
    auto *stdGrp  = new QGroupBox(tr("Standard lateral flow"), m_form);
    auto *stdLay  = new QVBoxLayout(stdGrp);
    auto *formula = new QLabel(
        QStringLiteral("Q<sub>lat</sub> = A1·(HGW − H*)<sup>B1</sup> − "
                       "A2·(HSW − H*)<sup>B2</sup> + A3·HGW·HSW"), stdGrp);
    formula->setTextFormat(Qt::RichText);
    formula->setToolTip(tr("HGW: water table height above aquifer bottom; "
                           "HSW: receiving-node water depth above aquifer "
                           "bottom; H*: Egwt (or the receiving node's invert) "
                           "above aquifer bottom."));
    stdLay->addWidget(formula);
    auto *stdForm = new QFormLayout;
    auto mkSpin = [stdGrp](const char *name) {
        auto *s = new QDoubleSpinBox(stdGrp);
        s->setObjectName(QString::fromLatin1(name));
        s->setRange(-kBig, kBig);
        s->setDecimals(4);
        return s;
    };
    m_a1    = mkSpin("gwA1");
    m_b1    = mkSpin("gwB1");
    m_a2    = mkSpin("gwA2");
    m_b2    = mkSpin("gwB2");
    m_a3    = mkSpin("gwA3");
    m_tw    = mkSpin("gwTw");
    m_hstar = mkSpin("gwHstar");
    m_tw->setSuffix(QStringLiteral(" %1").arg(L));
    m_hstar->setSuffix(QStringLiteral(" %1").arg(L));
    m_a1->setToolTip(tr("Groundwater flow coefficient."));
    m_b1->setToolTip(tr("Groundwater flow exponent."));
    m_a2->setToolTip(tr("Surface-water flow coefficient."));
    m_b2->setToolTip(tr("Surface-water flow exponent."));
    m_a3->setToolTip(tr("Surface-water / groundwater interaction coefficient."));
    m_tw->setToolTip(tr("Fixed depth of surface water at the receiving node "
                        "(Dsw). 0 uses the node's computed water depth."));
    m_hstar->setToolTip(tr("Water-table elevation below which there is no "
                           "lateral flow (Egwt). Any value entered here, "
                           "including -99, is an elevation."));
    // `*` in the .inp: the threshold follows the receiving node's invert.
    m_hstar->setSpecialValueText(tr("(node invert)"));
    m_egwtUseInvert = new QCheckBox(tr("Use receiving node &invert"), stdGrp);
    m_egwtUseInvert->setObjectName(QStringLiteral("gwEgwtUseInvert"));
    m_egwtUseInvert->setToolTip(tr("Leave Egwt unset ('*' in the input file) so "
                                   "the receiving node's invert elevation is the "
                                   "threshold."));
    connect(m_egwtUseInvert, &QCheckBox::toggled, this, [this](bool useInvert) {
        m_hstar->setEnabled(!useInvert);
        if (useInvert)
            m_hstar->setValue(m_hstar->minimum());   // shows the special text
        else if (m_hstar->value() == m_hstar->minimum())
            m_hstar->setValue(0.0);
    });
    auto *egwtRow = new QHBoxLayout;
    egwtRow->addWidget(m_hstar, 1);
    egwtRow->addWidget(m_egwtUseInvert);
    stdForm->addRow(tr("A1 (&GW coeff.)"),    m_a1);
    stdForm->addRow(tr("B1 (GW &expon.)"),    m_b1);
    stdForm->addRow(tr("A2 (Surf. c&oeff.)"), m_a2);
    stdForm->addRow(tr("B2 (Surf. e&xpon.)"), m_b2);
    stdForm->addRow(tr("A3 (interaction)"),   m_a3);
    stdForm->addRow(tr("Fixed surface-water depth (Dsw)"), m_tw);
    stdForm->addRow(tr("Threshold water-table elev. (Egwt)"), egwtRow);
    if (auto *label = qobject_cast<QLabel *>(stdForm->labelForField(egwtRow)))
        label->setBuddy(m_hstar);
    stdLay->addLayout(stdForm);
    formLay->addWidget(stdGrp);

    // ── Custom expressions ([GWF]) ──────────────────────────────────────────
    auto *gwfGrp = new QGroupBox(tr("Custom expressions ([GWF])"), m_form);
    auto *gwfLay = new QVBoxLayout(gwfGrp);

    auto addRow = [this, gwfGrp, gwfLay](const QString &title, const QString &note,
                                         const char *name,
                                         GwfExpressionEdit **editOut,
                                         QLabel **statusOut) {
        auto *cap = new QLabel(tr("%1 — %2").arg(title, note), gwfGrp);
        cap->setTextFormat(Qt::PlainText);
        cap->setWordWrap(true);
        gwfLay->addWidget(cap);

        auto *row  = new QHBoxLayout;
        auto *edit = new GwfExpressionEdit(m_ref.engine, gwfGrp);
        edit->setObjectName(QString::fromLatin1(name));
        QString accessibleTitle = title;
        accessibleTitle.remove(QLatin1Char('&'));
        edit->setAccessibleName(accessibleTitle);
        edit->setAccessibleDescription(note);
        cap->setBuddy(edit);
        row->addWidget(edit, 1);

        auto *insert = new QToolButton(gwfGrp);
        insert->setObjectName(QString::fromLatin1(name) + QStringLiteral("Insert"));
        insert->setText(tr("Insert variable ▾"));
        insert->setAccessibleName(tr("Insert variable into %1").arg(accessibleTitle));
        insert->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(insert);
        const QStringList names = edit->variableNames();
        const QStringList descs = edit->variableDescriptions();
        for (int i = 0; i < names.size(); ++i) {
            const QString desc = i < descs.size() ? descs.at(i) : QString();
            const QString text = desc.isEmpty()
                ? names.at(i)
                : QStringLiteral("%1 — %2").arg(names.at(i), desc);
            QAction *act = menu->addAction(text);
            const QString var = names.at(i);
            connect(act, &QAction::triggered, edit,
                    [edit, var]() { edit->insertAtCursor(var); });
        }
        insert->setMenu(menu);
        row->addWidget(insert);
        gwfLay->addLayout(row);

        auto *status = new QLabel(gwfGrp);
        status->setTextFormat(Qt::PlainText);
        status->setAccessibleName(tr("%1 validation").arg(accessibleTitle));
        status->setWordWrap(true);
        gwfLay->addWidget(status);

        *editOut   = edit;
        *statusOut = status;
    };
    addRow(tr("&Lateral expression"), tr("Added to the standard lateral flow."),
           "gwLateral", &m_lateral, &m_lateralStatus);
    addRow(tr("&Deep expression"), tr("Replaces the standard deep percolation."),
           "gwDeep", &m_deep, &m_deepStatus);
    bindExpression_(m_lateral, m_lateralStatus, &m_lateralOk);
    bindExpression_(m_deep,    m_deepStatus,    &m_deepOk);
    formLay->addWidget(gwfGrp);
    formLay->addStretch(1);

    outer->addWidget(OpenSWMM::Ui::wrapInScrollArea(m_form, this), 1);

    // ── Buttons ─────────────────────────────────────────────────────────────
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close,
                                     this);
    m_applyBtn = m_buttons->button(QDialogButtonBox::Apply);
    m_applyBtn->setObjectName(QStringLiteral("gwApply"));
    connect(m_applyBtn, &QPushButton::clicked, this, &GroundwaterExchangeDialog::apply_);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(m_buttons);
}

void GroundwaterExchangeDialog::bindExpression_(GwfExpressionEdit *edit,
                                                QLabel *status, bool *okFlag)
{
    const QString description = edit->accessibleDescription();
    connect(edit, &GwfExpressionEdit::validationChanged, this,
            [this, edit, status, okFlag, description](bool ok, const QString &msg, int col) {
                *okFlag = ok;
                if (ok) {
                    status->setText(QString());
                } else {
                    status->setText(col >= 0
                        ? tr("Column %1: %2").arg(col + 1).arg(msg)
                        : msg);
                }
                edit->setAccessibleDescription(ok ? description
                    : tr("%1 %2").arg(description, status->text()));
                updateApplyState_();
            });
}

void GroundwaterExchangeDialog::updateApplyState_()
{
    if (!m_applyBtn) return;
    const bool ok = canApply();
    m_applyBtn->setEnabled(ok);
    if (ok)
        m_applyBtn->setToolTip(QString());
    else if (!m_hasAquifer)
        m_applyBtn->setToolTip(tr("Assign an aquifer to enable groundwater exchange."));
    else
        m_applyBtn->setToolTip(tr("Fix the invalid expression before applying."));
}

// ---------------------------------------------------------------------------
// Engine round-trip
// ---------------------------------------------------------------------------
void GroundwaterExchangeDialog::loadFromEngine_()
{
    SWMM_Engine e = m_ref.engine;
    const int s = subIdx();

    int aq = -1;
    QString aqName;
    if (e && s >= 0 && swmm_subcatch_get_aquifer(e, s, &aq) == SWMM_OK && aq >= 0)
        if (const char *id = swmm_aquifer_id(e, aq))
            aqName = QString::fromUtf8(id);
    m_hasAquifer = !aqName.isEmpty();

    m_header->setText(tr("<b>Subcatchment:</b> %1&nbsp;&nbsp;&nbsp;"
                         "<b>Aquifer:</b> %2")
                          .arg(m_ref.subName.toHtmlEscaped(),
                               m_hasAquifer ? aqName.toHtmlEscaped() : tr("(none)")));
    m_hint->setVisible(!m_hasAquifer);
    m_form->setEnabled(m_hasAquifer);

    // Node combo: "(none)" then every node, once.
    if (m_node->count() == 0) {
        m_node->addItem(tr("(none)"));
        const int nN = e ? swmm_node_count(e) : 0;
        for (int i = 0; i < nN; ++i)
            if (const char *id = swmm_node_id(e, i))
                m_node->addItem(QString::fromUtf8(id));
    }

    m_loadedOk = readSnapshot(m_loaded);
    if (m_loadedOk) {
        m_node->setCurrentIndex(m_loaded.node >= 0
            ? m_node->findText(m_loaded.nodeName) : 0);
        hydrateParams_(m_loaded.values);
        m_lateral->setExpression(m_loaded.lateral);
        m_deep->setExpression(m_loaded.deep);
    } else {
        reportWriteError(tr("Groundwater values could not be loaded completely. Close and reopen the editor."));
    }
    updateApplyState_();
}

void GroundwaterExchangeDialog::hydrateParams_(const std::array<double, 8> &values)
{
    const std::array<QDoubleSpinBox *, 8> spins{m_surfEl, m_a1, m_b1, m_a2, m_b2, m_a3, m_tw, m_hstar};
    for (size_t i = 0; i + 1 < spins.size(); ++i)
        OpenSWMM::Ui::setHydratedValue(spins[i], values[i]);
    const bool useInvert = values[7] == kMissing;
    if (!useInvert) OpenSWMM::Ui::setHydratedValue(m_hstar, values[7]);
    m_egwtUseInvert->setChecked(useInvert);
    m_hstar->setEnabled(!useInvert);
    if (useInvert) m_hstar->setValue(m_hstar->minimum());
}

bool GroundwaterExchangeDialog::readSnapshot(Snapshot &state) const
{
    const int index = subIdx();
    if (index < 0) return false;
    const auto e = m_ref.engine;
    if (swmm_subcatch_get_aquifer(e, index, &state.aquifer) != SWMM_OK
        || swmm_subcatch_get_gw_node(e, index, &state.node) != SWMM_OK
        || swmm_subcatch_get_gw_params(e, index, &state.values[0], &state.values[1],
            &state.values[2], &state.values[3], &state.values[4], &state.values[5],
            &state.values[6], &state.values[7]) != SWMM_OK)
        return false;
    for (double value : state.values)
        if (!std::isfinite(value)) return false;
    const char *aq = state.aquifer < 0 ? nullptr : swmm_aquifer_id(e, state.aquifer);
    const char *nd = state.node < 0 ? nullptr : swmm_node_id(e, state.node);
    if ((state.aquifer >= 0 && !aq) || (state.node >= 0 && !nd)) return false;
    state.aquiferName = aq ? QString::fromUtf8(aq) : QString();
    state.nodeName = nd ? QString::fromUtf8(nd) : QString();
    const auto readExpression = [&](int type, QString &value) {
        // The API reports success even when its buffer truncates. Grow until
        // there is spare space, and refuse an oversized value rather than
        // allowing a partial expression to be written back.
        for (int size = 512; size <= 1024 * 1024; size *= 2) {
            QByteArray bytes(size, '\0');
            if (swmm_subcatch_get_gwf_expression(e, index, type, bytes.data(), size) != SWMM_OK)
                return false;
            if (bytes.indexOf('\0') < size - 1) {
                value = QString::fromUtf8(bytes.constData());
                return true;
            }
        }
        return false;
    };
    return readExpression(SWMM_GWF_LATERAL, state.lateral)
        && readExpression(SWMM_GWF_DEEP, state.deep);
}

void GroundwaterExchangeDialog::reportWriteError(const QString &message, QWidget *field)
{
    m_writeError->setText(message);
    m_writeError->setVisible(!message.isEmpty());
    if (field) {
        if (auto *scroll = findChild<QScrollArea *>()) scroll->ensureWidgetVisible(field);
        field->setFocus(Qt::OtherFocusReason);
    }
}

void GroundwaterExchangeDialog::invalidateContext()
{
    m_ref.engine = nullptr;
    m_ref.layer = nullptr;
    m_loadedOk = false;
    m_lateral->invalidateEngine();
    m_deep->invalidateEngine();
    m_form->setEnabled(false);
    setEnabled(false);
    updateApplyState_();
    reject();
}

void GroundwaterExchangeDialog::notifyEdited()
{
    if (m_ref.layer) {
        QMetaObject::invokeMethod(m_ref.layer, "markEdited", Qt::DirectConnection);
        QMetaObject::invokeMethod(m_ref.layer, "attributeChanged", Qt::DirectConnection,
                                  Q_ARG(QString, m_ref.subName));
    }
    m_ref.summary = updatedSummary();
    emit applied();
}

void GroundwaterExchangeDialog::apply_()
{
    if (!m_loadedOk) return;
    reportWriteError(QString());
    Snapshot current;
    if (!readSnapshot(current) || current != m_loaded) {
        reportWriteError(tr("Groundwater data changed or is no longer available. Your draft is retained; close and reopen this editor before applying."));
        return;
    }
    const int selection = m_node->currentIndex();
    const int node = selection == 0 ? -1
        : swmm_node_index(m_ref.engine, m_node->currentText().toUtf8().constData());
    if (selection < 0 || (selection > 0 && node < 0)) {
        reportWriteError(tr("Select an available receiving node or (none)."), m_node);
        return;
    }
    m_lateral->validateNow();
    m_deep->validateNow();
    if (!canApply()) {
        auto *invalid = m_lateralOk ? m_deep : m_lateral;
        if (auto *scroll = findChild<QScrollArea *>()) scroll->ensureWidgetVisible(invalid);
        invalid->setFocus(Qt::OtherFocusReason);
        return;
    }
    const std::array<QDoubleSpinBox *, 8> spins{m_surfEl, m_a1, m_b1, m_a2, m_b2, m_a3, m_tw, m_hstar};
    auto values = m_loaded.values;
    for (size_t i = 0; i < spins.size(); ++i) {
        values[i] = (spins[i] == m_hstar && m_egwtUseInvert->isChecked())
            ? kMissing : OpenSWMM::Ui::preciseValue(spins[i]);
        if (!std::isfinite(values[i])) {
            reportWriteError(tr("Enter a finite groundwater value."), spins[i]);
            return;
        }
    }
    const QString lateral = m_lateral->expression() == m_loaded.lateral
        ? m_loaded.lateral : m_lateral->expression().trimmed();
    const QString deep = m_deep->expression() == m_loaded.deep
        ? m_loaded.deep : m_deep->expression().trimmed();
    const auto e = m_ref.engine;
    const int index = subIdx();
    bool wrote = false;
    const auto check = [&](int rc, const QString &field) {
        if (rc == SWMM_OK) { wrote = true; return true; }
        reportWriteError(tr("Could not apply %1 (engine error %2). %3")
            .arg(field).arg(rc).arg(wrote
                ? tr("Earlier changes were applied and marked unsaved. Your remaining draft is retained; close and reopen before retrying.")
                : tr("Your draft is retained. Retry when the model is editable.")));
        if (wrote) {
            // Keep the old snapshot: a retry must not silently overwrite the
            // partially changed model. Dirty notification is still required.
            notifyEdited();
        }
        return false;
    };
    if (node != m_loaded.node
        && !check(swmm_subcatch_set_gw_node(e, index, node), tr("receiving node"))) return;
    if (values != m_loaded.values
        && !check(swmm_subcatch_set_gw_params(e, index, values[0], values[1], values[2],
            values[3], values[4], values[5], values[6], values[7]), tr("groundwater parameters"))) return;
    if (lateral != m_loaded.lateral
        && !check(swmm_subcatch_set_gwf_expression(e, index, SWMM_GWF_LATERAL,
            lateral.toUtf8().constData()), tr("lateral expression"))) return;
    if (deep != m_loaded.deep
        && !check(swmm_subcatch_set_gwf_expression(e, index, SWMM_GWF_DEEP,
            deep.toUtf8().constData()), tr("deep expression"))) return;
    if (!wrote) return;
    m_loaded.node = node;
    m_loaded.nodeName = node < 0 ? QString() : m_node->currentText();
    m_loaded.values = values;
    m_loaded.lateral = lateral;
    m_loaded.deep = deep;
    hydrateParams_(values);
    notifyEdited();
}
