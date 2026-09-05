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
#include "ui/widgets/gwfexpressionedit.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <QAction>
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
    return m_hasAquifer && m_lateralOk && m_deepOk;
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
                           "bottom; H*: Hstar."));
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
    m_tw->setToolTip(tr("Threshold groundwater table elevation (Twgr): no "
                        "lateral flow while the water table is below it."));
    m_hstar->setToolTip(tr("Elevation of the receiving channel bottom "
                           "(Hstar); lateral flow ceases at this water table."));
    stdForm->addRow(tr("A1 (&GW coeff.)"),    m_a1);
    stdForm->addRow(tr("B1 (GW &expon.)"),    m_b1);
    stdForm->addRow(tr("A2 (Surf. c&oeff.)"), m_a2);
    stdForm->addRow(tr("B2 (Surf. e&xpon.)"), m_b2);
    stdForm->addRow(tr("A3 (interaction)"),   m_a3);
    stdForm->addRow(tr("Threshold Twgr"),     m_tw);
    stdForm->addRow(tr("Hstar"),              m_hstar);
    stdLay->addLayout(stdForm);
    formLay->addWidget(stdGrp);

    // ── Custom expressions ([GWF]) ──────────────────────────────────────────
    auto *gwfGrp = new QGroupBox(tr("Custom expressions ([GWF])"), m_form);
    auto *gwfLay = new QVBoxLayout(gwfGrp);

    auto addRow = [this, gwfGrp, gwfLay](const QString &title, const QString &note,
                                         const char *name,
                                         GwfExpressionEdit **editOut,
                                         QLabel **statusOut) {
        auto *cap = new QLabel(QStringLiteral("<b>%1</b> — %2").arg(title, note), gwfGrp);
        cap->setTextFormat(Qt::RichText);
        cap->setWordWrap(true);
        gwfLay->addWidget(cap);

        auto *row  = new QHBoxLayout;
        auto *edit = new GwfExpressionEdit(m_ref.engine, gwfGrp);
        edit->setObjectName(QString::fromLatin1(name));
        row->addWidget(edit, 1);

        auto *insert = new QToolButton(gwfGrp);
        insert->setText(tr("Insert variable ▾"));
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
        status->setWordWrap(true);
        gwfLay->addWidget(status);

        *editOut   = edit;
        *statusOut = status;
    };
    addRow(tr("LATERAL"), tr("added to the standard lateral flow"),
           "gwLateral", &m_lateral, &m_lateralStatus);
    addRow(tr("DEEP"), tr("replaces the standard deep percolation"),
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
    connect(edit, &GwfExpressionEdit::validationChanged, this,
            [this, status, okFlag](bool ok, const QString &msg, int col) {
                *okFlag = ok;
                if (ok) {
                    status->setText(QString());
                } else {
                    status->setText(col >= 0
                        ? tr("Column %1: %2").arg(col + 1).arg(msg)
                        : msg);
                }
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
                          .arg(m_ref.subName,
                               m_hasAquifer ? aqName : tr("(none)")));
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

    if (e && s >= 0) {
        int nd = -1;
        swmm_subcatch_get_gw_node(e, s, &nd);
        m_node->setCurrentIndex(nd >= 0 ? nd + 1 : 0);
        double surf=0,a1=0,b1=0,a2=0,b2=0,a3=0,tw=0,hstar=0;
        swmm_subcatch_get_gw_params(e, s, &surf, &a1, &b1, &a2, &b2, &a3, &tw, &hstar);
        m_surfEl->setValue(surf);
        m_a1->setValue(a1); m_b1->setValue(b1);
        m_a2->setValue(a2); m_b2->setValue(b2);
        m_a3->setValue(a3);
        m_tw->setValue(tw); m_hstar->setValue(hstar);

        char buf[512];
        buf[0] = '\0';
        swmm_subcatch_get_gwf_expression(e, s, SWMM_GWF_LATERAL, buf, sizeof(buf));
        m_lateral->setExpression(QString::fromUtf8(buf));
        buf[0] = '\0';
        swmm_subcatch_get_gwf_expression(e, s, SWMM_GWF_DEEP, buf, sizeof(buf));
        m_deep->setExpression(QString::fromUtf8(buf));
    }
    updateApplyState_();
}

void GroundwaterExchangeDialog::apply_()
{
    const int s = subIdx();
    if (s < 0 || !canApply()) return;
    SWMM_Engine e = m_ref.engine;

    // Combo index 0 is "(none)" → -1; otherwise the node index.
    const int nd = m_node->currentIndex() - 1;
    swmm_subcatch_set_gw_node(e, s, nd);
    const int rc = swmm_subcatch_set_gw_params(e, s,
        m_surfEl->value(), m_a1->value(), m_b1->value(),
        m_a2->value(), m_b2->value(), m_a3->value(),
        m_tw->value(), m_hstar->value());
    if (rc != SWMM_OK) {
        QMessageBox::warning(this, tr("Apply Groundwater"),
            tr("Engine rejected groundwater set (error %1).").arg(rc));
        return;
    }
    // Empty text clears the expression (engine treats "" as clear).
    const QByteArray lat  = m_lateral->expression().trimmed().toUtf8();
    const QByteArray deep = m_deep->expression().trimmed().toUtf8();
    swmm_subcatch_set_gwf_expression(e, s, SWMM_GWF_LATERAL, lat.constData());
    swmm_subcatch_set_gwf_expression(e, s, SWMM_GWF_DEEP,    deep.constData());

    // Dirty flag + per-object refresh so the property browser / attribute
    // table re-read the summary even after the opening cell editor is gone
    // (the dialog is modeless). Invoked by name so the dialog's unit test
    // links without SWMMModelLayer's moc symbols (same seam the adapter
    // tests stub around).
    if (m_ref.layer) {
        QMetaObject::invokeMethod(m_ref.layer, "markEdited", Qt::DirectConnection);
        QMetaObject::invokeMethod(m_ref.layer, "attributeChanged", Qt::DirectConnection,
                                  Q_ARG(QString, m_ref.subName));
    }
    m_ref.summary = updatedSummary();
    emit applied();
}
