/*!
 * \file   nodecompoundeditdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/nodecompoundeditdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_nodes.h>

NodeCompoundEditDialog::NodeCompoundEditDialog(NodeCompoundEditRef ref,
                                               QWidget *parent)
    : QDialog(parent), m_ref(std::move(ref))
{
    const char *titles[] = {
        "External Inflows", "Dry Weather Flow", "RDII", "Pollutant Treatment",
    };
    setWindowTitle(tr("%1 — %2")
                       .arg(QString::fromLatin1(titles[m_ref.kind]),
                            m_ref.nodeName));
    resize(560, 380);

    m_stack = new QStackedWidget(this);
    buildInflowsPage();    // index 0
    buildDwfPage();        // index 1
    buildRdiiPage();       // index 2
    buildTreatmentPage();  // index 3
    m_stack->setCurrentIndex(int(m_ref.kind));

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_stack, 1);
    lay->addWidget(m_buttons);

    refreshActivePage();
}

int NodeCompoundEditDialog::nodeIdx() const
{
    if (!m_ref.engine || m_ref.nodeName.isEmpty()) return -1;
    return swmm_node_index(m_ref.engine, m_ref.nodeName.toUtf8().constData());
}

// ============================================================================
// Inflows page — Add-only until AG.0 ships per-entry getter
// ============================================================================

void NodeCompoundEditDialog::buildInflowsPage()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    m_inflowsSummary = new QLabel(page);
    m_inflowsSummary->setWordWrap(true);
    vlay->addWidget(m_inflowsSummary);

    auto *info = new QLabel(
        tr("<i>Note: the engine API to list existing rows lands with AG.0 "
           "(<tt>swmm_inflow_get</tt>). Today this dialog only appends new "
           "entries — once that ABI ships, a full table editor (Edit + "
           "Remove) replaces this form.</i>"),
        page);
    info->setWordWrap(true);
    info->setStyleSheet(QStringLiteral("color: gray;"));
    vlay->addWidget(info);

    auto *grp = new QGroupBox(tr("Add Inflow"), page);
    auto *form = new QFormLayout(grp);

    m_inflowsConstEdit = new QLineEdit(QStringLiteral("FLOW"), grp);
    m_inflowsTypeCombo = new QComboBox(grp);
    m_inflowsTypeCombo->addItems({QStringLiteral("FLOW"),
                                  QStringLiteral("CONCEN"),
                                  QStringLiteral("MASS")});
    m_inflowsTsEdit    = new QLineEdit(grp);
    m_inflowsBaseSpin  = new QDoubleSpinBox(grp);
    m_inflowsBaseSpin->setRange(0.0, 1e12);
    m_inflowsBaseSpin->setDecimals(4);
    m_inflowsMFactSpin = new QDoubleSpinBox(grp);
    m_inflowsMFactSpin->setRange(-1e6, 1e6);
    m_inflowsMFactSpin->setDecimals(4);
    m_inflowsMFactSpin->setValue(1.0);
    m_inflowsSFactSpin = new QDoubleSpinBox(grp);
    m_inflowsSFactSpin->setRange(-1e6, 1e6);
    m_inflowsSFactSpin->setDecimals(4);
    m_inflowsSFactSpin->setValue(1.0);
    m_inflowsPatEdit   = new QLineEdit(grp);

    form->addRow(tr("Constituent"),     m_inflowsConstEdit);
    form->addRow(tr("Type"),            m_inflowsTypeCombo);
    form->addRow(tr("Time Series"),     m_inflowsTsEdit);
    form->addRow(tr("Baseline"),        m_inflowsBaseSpin);
    form->addRow(tr("Multiplier (M)"),  m_inflowsMFactSpin);
    form->addRow(tr("Scale Factor (S)"),m_inflowsSFactSpin);
    form->addRow(tr("Pattern"),         m_inflowsPatEdit);

    auto *addBtn = new QPushButton(tr("Add Inflow"), grp);
    form->addRow(QString(), addBtn);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        const int idx = nodeIdx();
        if (idx < 0) return;
        const QByteArray cons = m_inflowsConstEdit->text().toUtf8();
        const QByteArray ts   = m_inflowsTsEdit->text().toUtf8();
        const QByteArray type = m_inflowsTypeCombo->currentText().toUtf8();
        const QByteArray pat  = m_inflowsPatEdit->text().toUtf8();
        const int rc = swmm_ext_inflow_add(
            m_ref.engine, idx, cons.constData(), ts.constData(),
            type.constData(), m_inflowsMFactSpin->value(),
            m_inflowsSFactSpin->value(), m_inflowsBaseSpin->value(),
            pat.constData());
        if (rc != SWMM_OK) {
            QMessageBox::warning(this, tr("Add Inflow"),
                tr("Engine rejected inflow add (error %1).").arg(rc));
            return;
        }
        refreshActivePage();
    });

    vlay->addWidget(grp);
    vlay->addStretch(1);
    m_stack->addWidget(page);
}

// ============================================================================
// DWF page — Add-only until AG.0 ships per-entry getter
// ============================================================================

void NodeCompoundEditDialog::buildDwfPage()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    m_dwfSummary = new QLabel(page);
    m_dwfSummary->setWordWrap(true);
    vlay->addWidget(m_dwfSummary);

    auto *info = new QLabel(
        tr("<i>Note: per-entry read lands with AG.0 (<tt>swmm_dwf_get</tt>). "
           "Today this dialog only appends new DWF rows.</i>"),
        page);
    info->setWordWrap(true);
    info->setStyleSheet(QStringLiteral("color: gray;"));
    vlay->addWidget(info);

    auto *grp = new QGroupBox(tr("Add Dry Weather Flow"), page);
    auto *form = new QFormLayout(grp);

    m_dwfConstEdit = new QLineEdit(QStringLiteral("FLOW"), grp);
    m_dwfAvgSpin   = new QDoubleSpinBox(grp);
    m_dwfAvgSpin->setRange(0.0, 1e12);
    m_dwfAvgSpin->setDecimals(4);
    m_dwfPat1Edit = new QLineEdit(grp);
    m_dwfPat2Edit = new QLineEdit(grp);
    m_dwfPat3Edit = new QLineEdit(grp);
    m_dwfPat4Edit = new QLineEdit(grp);

    form->addRow(tr("Constituent"),        m_dwfConstEdit);
    form->addRow(tr("Average Value"),      m_dwfAvgSpin);
    form->addRow(tr("Monthly Pattern"),    m_dwfPat1Edit);
    form->addRow(tr("Daily Pattern"),      m_dwfPat2Edit);
    form->addRow(tr("Hourly Pattern"),     m_dwfPat3Edit);
    form->addRow(tr("Weekend Pattern"),    m_dwfPat4Edit);

    auto *addBtn = new QPushButton(tr("Add DWF"), grp);
    form->addRow(QString(), addBtn);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        const int idx = nodeIdx();
        if (idx < 0) return;
        const QByteArray cons = m_dwfConstEdit->text().toUtf8();
        const QByteArray p1   = m_dwfPat1Edit->text().toUtf8();
        const QByteArray p2   = m_dwfPat2Edit->text().toUtf8();
        const QByteArray p3   = m_dwfPat3Edit->text().toUtf8();
        const QByteArray p4   = m_dwfPat4Edit->text().toUtf8();
        const int rc = swmm_dwf_add(m_ref.engine, idx, cons.constData(),
            m_dwfAvgSpin->value(), p1.constData(), p2.constData(),
            p3.constData(), p4.constData());
        if (rc != SWMM_OK) {
            QMessageBox::warning(this, tr("Add DWF"),
                tr("Engine rejected DWF add (error %1).").arg(rc));
            return;
        }
        refreshActivePage();
    });

    vlay->addWidget(grp);
    vlay->addStretch(1);
    m_stack->addWidget(page);
}

// ============================================================================
// RDII page — fully functional via `swmm_rdii_*`
// ============================================================================

void NodeCompoundEditDialog::buildRdiiPage()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    m_rdiiSummary = new QLabel(page);
    m_rdiiSummary->setWordWrap(true);
    vlay->addWidget(m_rdiiSummary);

    m_rdiiTable = new QTableWidget(0, 2, page);
    m_rdiiTable->setHorizontalHeaderLabels({tr("UH Group"), tr("Sewer Area")});
    m_rdiiTable->horizontalHeader()->setStretchLastSection(true);
    m_rdiiTable->verticalHeader()->setVisible(false);
    m_rdiiTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rdiiTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vlay->addWidget(m_rdiiTable, 1);

    auto *grp = new QGroupBox(tr("Add RDII Assignment"), page);
    auto *form = new QFormLayout(grp);

    m_rdiiUhCombo = new QComboBox(grp);
    m_rdiiUhCombo->setEditable(true);  // engine accepts any UH name
    m_rdiiAreaSpin = new QDoubleSpinBox(grp);
    m_rdiiAreaSpin->setRange(0.0, 1e9);
    m_rdiiAreaSpin->setDecimals(4);

    form->addRow(tr("UH Group Name"), m_rdiiUhCombo);
    form->addRow(tr("Sewer Area"),    m_rdiiAreaSpin);

    auto *addBtn = new QPushButton(tr("Add"), grp);
    form->addRow(QString(), addBtn);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        const int idx = nodeIdx();
        if (idx < 0) return;
        const QByteArray uh = m_rdiiUhCombo->currentText().toUtf8();
        if (uh.isEmpty()) {
            QMessageBox::warning(this, tr("Add RDII"),
                tr("Provide a UH group name."));
            return;
        }
        const int rc = swmm_rdii_add(m_ref.engine, idx,
                                       uh.constData(), m_rdiiAreaSpin->value());
        if (rc != SWMM_OK) {
            QMessageBox::warning(this, tr("Add RDII"),
                tr("Engine rejected RDII add (error %1).").arg(rc));
            return;
        }
        refreshActivePage();
    });

    vlay->addWidget(grp);
    m_stack->addWidget(page);
}

// ============================================================================
// Treatment page — placeholder (no engine API today)
// ============================================================================

void NodeCompoundEditDialog::buildTreatmentPage()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    m_treatmentNotice = new QLabel(
        tr("<b>Pollutant Treatment is not yet wired.</b><br><br>"
           "The engine ships no <tt>swmm_treatment_*</tt> API today. "
           "The legacy <tt>Dtreat.pas</tt> form maps to a per-node × "
           "per-pollutant grid of removal expressions. Implementation "
           "is tracked under engine request <tt>DB-ENG-04</tt> + the "
           "BP.6.6.3 dual-surface design (per-pollutant expression vs "
           "per-node grid — gap G7 in EDITOR_PARITY_GAP_ANALYSIS).<br><br>"
           "Once those ship, this page will host the treatment-expression "
           "editor inline."),
        page);
    m_treatmentNotice->setWordWrap(true);
    m_treatmentNotice->setStyleSheet(QStringLiteral(
        "padding: 12px; background-color: palette(alternate-base);"));
    vlay->addWidget(m_treatmentNotice);
    vlay->addStretch(1);

    m_stack->addWidget(page);
}

// ============================================================================
// Refresh helpers
// ============================================================================

void NodeCompoundEditDialog::refreshActivePage()
{
    const int idx = nodeIdx();
    if (idx < 0) {
        m_ref.summary = tr("(node removed)");
        return;
    }

    switch (m_ref.kind) {
    case NodeCompoundEditRef::Inflows: {
        // Without `swmm_inflow_get` we can only report the global total.
        // Filtering by node would require iterating entries with their
        // node_idx exposed; that's the AG.0 contract.
        const int total = swmm_ext_inflow_count(m_ref.engine);
        m_inflowsSummary->setText(tr(
            "Total <b>[INFLOWS]</b> rows in model: %1 "
            "(per-node filter pending engine API).").arg(total));
        m_ref.summary = (total > 0)
            ? tr("(model total %1)").arg(total)
            : tr("(none)");
        break;
    }
    case NodeCompoundEditRef::Dwf: {
        const int total = swmm_dwf_count(m_ref.engine);
        m_dwfSummary->setText(tr(
            "Total <b>[DWF]</b> rows in model: %1 "
            "(per-node filter pending engine API).").arg(total));
        m_ref.summary = (total > 0)
            ? tr("(model total %1)").arg(total)
            : tr("(none)");
        break;
    }
    case NodeCompoundEditRef::Rdii: {
        // RDII has per-entry read: iterate, filter by node_idx.
        m_rdiiTable->setRowCount(0);
        const int total = swmm_rdii_count(m_ref.engine);
        int matched = 0;
        char uhBuf[128];
        for (int i = 0; i < total; ++i) {
            int ni = -1; double area = 0.0;
            if (swmm_rdii_get(m_ref.engine, i, &ni, uhBuf,
                               sizeof(uhBuf), &area) != SWMM_OK) continue;
            if (ni != idx) continue;
            const int row = m_rdiiTable->rowCount();
            m_rdiiTable->insertRow(row);
            m_rdiiTable->setItem(row, 0,
                new QTableWidgetItem(QString::fromUtf8(uhBuf)));
            m_rdiiTable->setItem(row, 1,
                new QTableWidgetItem(QString::number(area, 'g', 6)));
            ++matched;
        }
        m_rdiiSummary->setText(tr(
            "<b>%1</b> RDII assignment(s) on this node "
            "(model total: %2).").arg(matched).arg(total));
        m_ref.summary = (matched > 0)
            ? tr("(%1 entries)").arg(matched)
            : tr("(none)");
        break;
    }
    case NodeCompoundEditRef::Treatment:
        m_ref.summary = tr("(engine API pending)");
        break;
    }
}
