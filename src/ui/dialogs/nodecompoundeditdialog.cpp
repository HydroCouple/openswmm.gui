/*!
 * \file   nodecompoundeditdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/nodecompoundeditdialog.h"
#include "ui/theme/themehelpers.h"
#include "ui/panels/objectbrowserpanel.h"
#include "ui/dialogs/hydrographgroupeditor.h"
#include "ui/dialogs/patterneditordialog.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "ui/widgets/labeledcontrols.h"
#include "ui/widgets/treatmentexpressionedit.h"
#include "layers/swmmmodellayer.h"
#include "pattern/patternregistry.h"
#include "timeseries/timeseriesregistry.h"
#include "ui/uiscrollhelpers.h"

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
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_tables.h>

NodeCompoundEditDialog::NodeCompoundEditDialog(NodeCompoundEditRef ref,
                                               QWidget *parent)
    : QDialog(parent), m_ref(std::move(ref))
{
    const char *titles[] = {
        "External Inflows", "Dry Weather Flow", "RDII", "Pollutant Treatment",
    };
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("NodeCompoundEditDialog"));
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
// DB.4 — combo population + picker helpers
// ============================================================================

void NodeCompoundEditDialog::populateConstituentCombo(QComboBox *c)
{
    if (!c || !m_ref.engine) return;
    QSignalBlocker block(c);
    c->clear();
    c->addItem(QStringLiteral("FLOW"));
    const int n = swmm_pollutant_count(m_ref.engine);
    for (int i = 0; i < n; ++i) {
        const char *id = swmm_pollutant_id(m_ref.engine, i);
        if (id && *id) c->addItem(QString::fromUtf8(id));
    }
}

void NodeCompoundEditDialog::populateTimeSeriesCombo(LabeledPickerCombo *p)
{
    if (!p || !m_ref.engine) return;
    QStringList items;
    const int n = swmm_table_count(m_ref.engine);
    for (int i = 0; i < n; ++i) {
        int t = -1;
        if (swmm_table_get_type(m_ref.engine, i, &t) != SWMM_OK) continue;
        if (t != 0) continue;  // TIMESERIES == 0; CURVE_* are 1..11
        const char *id = swmm_table_id(m_ref.engine, i);
        if (id && *id) items << QString::fromUtf8(id);
    }
    const QString current = p->currentText();
    p->setItems(items, current);
}

void NodeCompoundEditDialog::populatePatternCombo(LabeledPickerCombo *p)
{
    if (!p || !m_ref.engine) return;
    QStringList items;
    const int n = swmm_pattern_count(m_ref.engine);
    for (int i = 0; i < n; ++i) {
        const char *id = swmm_pattern_id(m_ref.engine, i);
        if (id && *id) items << QString::fromUtf8(id);
    }
    const QString current = p->currentText();
    p->setItems(items, current);
}

void NodeCompoundEditDialog::populateUhGroupCombo(LabeledPickerCombo *p)
{
    if (!p || !m_ref.engine) return;
    QStringList items;
    const int n = swmm_hydrograph_group_count(m_ref.engine);
    char buf[128];
    for (int i = 0; i < n; ++i) {
        buf[0] = '\0';
        if (swmm_hydrograph_group_id(m_ref.engine, i, buf, sizeof(buf)) != SWMM_OK)
            continue;
        if (buf[0]) items << QString::fromUtf8(buf);
    }
    const QString current = p->currentText();
    p->setItems(items, current);
}

QString NodeCompoundEditDialog::launchObjectEditor(int dataCategory,
                                                    const QString &currentName)
{
    // Picker buttons require a bound layer for engine + registry access.
    if (!m_ref.layer) {
        QMessageBox::information(this, tr("Edit Data Object"),
            tr("This dialog wasn't bound to a project layer, so it can't "
               "open the editor. Use Data → Edit… in the main window instead."));
        return {};
    }
    const auto cat = static_cast<SWMMModelLayer::DataCategory>(dataCategory);

    switch (cat) {
    case SWMMModelLayer::DataTimeSeries: {
        auto *reg = qobject_cast<openswmmvis::timeseries::TimeseriesRegistry *>(
            m_ref.layer->ensureTimeseriesRegistry());
        if (!reg) return {};
        return openswmmvis::ui::TimeseriesEditorDialog::pickTimeseries(
            reg, /*undoStack=*/nullptr, currentName, this);
    }
    case SWMMModelLayer::DataPatterns: {
        auto *reg = qobject_cast<openswmmvis::pattern::PatternRegistry *>(
            m_ref.layer->ensurePatternRegistry());
        if (!reg) return {};
        const QString picked = openswmmvis::ui::PatternEditorDialog::pickPattern(
            reg, /*undoStack=*/nullptr, currentName, this);
        // PatternRegistry::create only mutates the registry; flush so the
        // combo's repopulate (which reads via swmm_pattern_count) sees a
        // brand-new pattern.
        if (m_ref.engine) reg->saveToEngine(m_ref.engine);
        return picked;
    }
    case SWMMModelLayer::DataHydrographs:
        return HydrographGroupEditor::pickGroup(m_ref.layer, currentName, this);

    default:
        // Categories without a complex editor — surface the gap tooltip.
        QMessageBox::information(this, tr("Edit"),
            ObjectBrowserPanel::gapTooltipFor(cat));
        return {};
    }
}

void NodeCompoundEditDialog::wirePicker(LabeledPickerCombo *picker,
                                          int dataCategory,
                                          void (NodeCompoundEditDialog::*repopulate)(LabeledPickerCombo*))
{
    if (!picker) return;
    connect(picker, &LabeledPickerCombo::pickerClicked, this,
            [this, picker, dataCategory, repopulate]() {
        const QString currentSel = picker->currentText().trimmed();
        const QString resultName = launchObjectEditor(dataCategory, currentSel);
        if (resultName.isEmpty()) return;
        (this->*repopulate)(picker);
        picker->setCurrentText(resultName);
    });
}

void NodeCompoundEditDialog::updateInflowsMassEnabled()
{
    if (!m_inflowsConstCombo || !m_inflowsTypeCombo) return;
    const bool isFlow = (m_inflowsConstCombo->currentText() == QLatin1String("FLOW"));
    // QComboBox doesn't expose per-item enable directly; reach into its
    // underlying QStandardItemModel.
    auto *model = qobject_cast<QStandardItemModel *>(m_inflowsTypeCombo->model());
    if (!model) return;
    QStandardItem *massItem = model->item(2);  // 0=FLOW, 1=CONCEN, 2=MASS
    if (!massItem) return;
    Qt::ItemFlags f = massItem->flags();
    if (isFlow) {
        f &= ~(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        // If MASS was selected, drop back to FLOW.
        if (m_inflowsTypeCombo->currentIndex() == 2)
            m_inflowsTypeCombo->setCurrentIndex(0);
    } else {
        f |= Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }
    massItem->setFlags(f);
}

// ============================================================================
// Inflows page (DB.4d) — full table editor with:
//   - constituent combo (FLOW + pollutants)
//   - type combo (FLOW/CONCEN/MASS; MASS disabled when FLOW selected)
//   - time series + pattern pickers (existing list + "..." button to create new)
//   - replace-on-duplicate per (node, constituent) per SWMM spec
//   - row selection populates the form below for edit
// ============================================================================

void NodeCompoundEditDialog::buildInflowsPage()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    m_inflowsSummary = new QLabel(page);
    m_inflowsSummary->setWordWrap(true);
    vlay->addWidget(m_inflowsSummary);

    m_inflowsTable = new QTableWidget(0, 7, page);
    m_inflowsTable->setHorizontalHeaderLabels({
        tr("Constituent"), tr("Type"), tr("Time Series"),
        tr("Baseline"), tr("M-Factor"), tr("S-Factor"), tr("Pattern")});
    m_inflowsTable->horizontalHeader()->setStretchLastSection(true);
    m_inflowsTable->verticalHeader()->setVisible(false);
    m_inflowsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_inflowsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_inflowsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vlay->addWidget(m_inflowsTable, 1);

    m_inflowsRemoveBtn = new QPushButton(tr("Remove Selected"), page);
    m_inflowsRemoveBtn->setEnabled(false);
    vlay->addWidget(m_inflowsRemoveBtn);
    connect(m_inflowsTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]() {
        m_inflowsRemoveBtn->setEnabled(!m_inflowsTable->selectedItems().isEmpty());
    });
    connect(m_inflowsRemoveBtn, &QPushButton::clicked, this, [this]() {
        const auto rows = m_inflowsTable->selectionModel()->selectedRows();
        if (rows.isEmpty()) return;
        QTableWidgetItem *first = m_inflowsTable->item(rows.first().row(), 0);
        if (!first) return;
        const int globalIdx = first->data(Qt::UserRole).toInt();
        const int rc = swmm_ext_inflow_remove(m_ref.engine, globalIdx);
        if (rc != SWMM_OK) {
            QMessageBox::warning(this, tr("Remove Inflow"),
                tr("Engine rejected inflow remove (error %1).").arg(rc));
            return;
        }
        refreshActivePage();
    });

    auto *grp = new QGroupBox(tr("Add / Update Inflow"), page);
    auto *form = new QFormLayout(grp);

    // Constituent: FLOW + every defined pollutant.
    m_inflowsConstCombo = new QComboBox(grp);
    populateConstituentCombo(m_inflowsConstCombo);

    // Type: FLOW/CONCEN/MASS. MASS is pollutant-only — gated by constituent.
    m_inflowsTypeCombo = new QComboBox(grp);
    m_inflowsTypeCombo->addItems({QStringLiteral("FLOW"),
                                  QStringLiteral("CONCEN"),
                                  QStringLiteral("MASS")});
    connect(m_inflowsConstCombo, &QComboBox::currentTextChanged, this,
            [this](const QString &){ updateInflowsMassEnabled(); });
    updateInflowsMassEnabled();

    // Time series + Pattern pickers.
    m_inflowsTsPicker  = new LabeledPickerCombo({}, grp);
    populateTimeSeriesCombo(m_inflowsTsPicker);
    wirePicker(m_inflowsTsPicker, SWMMModelLayer::DataTimeSeries,
                &NodeCompoundEditDialog::populateTimeSeriesCombo);

    m_inflowsPatPicker = new LabeledPickerCombo({}, grp);
    populatePatternCombo(m_inflowsPatPicker);
    wirePicker(m_inflowsPatPicker, SWMMModelLayer::DataPatterns,
                &NodeCompoundEditDialog::populatePatternCombo);

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

    form->addRow(tr("C&onstituent"),     m_inflowsConstCombo);
    form->addRow(tr("T&ype"),            m_inflowsTypeCombo);
    form->addRow(tr("T&ime Series"),     m_inflowsTsPicker);
    form->addRow(tr("Bas&eline"),        m_inflowsBaseSpin);
    form->addRow(tr("M&ultiplier (M)"),  m_inflowsMFactSpin);
    form->addRow(tr("Scale &Factor (S)"),m_inflowsSFactSpin);
    form->addRow(tr("&Pattern"),         m_inflowsPatPicker);

    auto *addBtn = new QPushButton(tr("Add / Update"), grp);
    form->addRow(QString(), addBtn);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        const int idx = nodeIdx();
        if (idx < 0) return;
        const QString constituent = m_inflowsConstCombo->currentText();
        const QByteArray cons = constituent.toUtf8();
        const QByteArray ts   = m_inflowsTsPicker->currentText().toUtf8();
        const QByteArray type = m_inflowsTypeCombo->currentText().toUtf8();
        const QByteArray pat  = m_inflowsPatPicker->currentText().toUtf8();

        // SWMM allows only one inflow per (node, constituent). Walk the
        // SoA backwards (so the index we're about to remove doesn't
        // shift) and detect a duplicate before adding.
        int dupIdx = -1;
        const int total = swmm_ext_inflow_count(m_ref.engine);
        char cBuf[64], tBuf[64], tyBuf[16], pBuf[64];
        for (int i = total - 1; i >= 0; --i) {
            int ni = -1;
            double mf = 0.0, sf = 0.0, base = 0.0;
            if (swmm_ext_inflow_get(m_ref.engine, i, &ni,
                                      cBuf, sizeof(cBuf), tBuf, sizeof(tBuf),
                                      tyBuf, sizeof(tyBuf), &mf, &sf, &base,
                                      pBuf, sizeof(pBuf)) != SWMM_OK)
                continue;
            if (ni == idx && constituent == QString::fromUtf8(cBuf)) {
                dupIdx = i;
                break;
            }
        }
        if (dupIdx >= 0) {
            const auto r = QMessageBox::question(this, tr("Replace Inflow"),
                tr("An inflow for <b>%1</b> already exists on this node. "
                   "Replace it with the new values?").arg(constituent),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (r != QMessageBox::Yes) return;
            swmm_ext_inflow_remove(m_ref.engine, dupIdx);
        }

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

    // Row selection → populate the form below so the user can review
    // and tweak values before clicking Add/Update to commit a replace.
    connect(m_inflowsTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        const auto sel = m_inflowsTable->selectionModel()->selectedRows();
        if (sel.isEmpty()) return;
        const int row = sel.first().row();
        auto cellText = [this, row](int col) -> QString {
            QTableWidgetItem *it = m_inflowsTable->item(row, col);
            return it ? it->text() : QString();
        };
        m_inflowsConstCombo->setCurrentText(cellText(0));
        m_inflowsTypeCombo->setCurrentText(cellText(1));
        m_inflowsTsPicker->setCurrentText(cellText(2));
        m_inflowsBaseSpin->setValue(cellText(3).toDouble());
        m_inflowsMFactSpin->setValue(cellText(4).toDouble());
        m_inflowsSFactSpin->setValue(cellText(5).toDouble());
        m_inflowsPatPicker->setCurrentText(cellText(6));
    });

    vlay->addWidget(grp);
    m_stack->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, m_stack));
}

// ============================================================================
// DWF page (DB.4e) — same shape as Inflows:
//   - constituent combo (FLOW + pollutants)
//   - 4 pattern pickers (Monthly/Daily/Hourly/Weekend) with "..." buttons
//   - replace-on-duplicate per (node, constituent)
//   - row selection populates form
// ============================================================================

void NodeCompoundEditDialog::buildDwfPage()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    m_dwfSummary = new QLabel(page);
    m_dwfSummary->setWordWrap(true);
    vlay->addWidget(m_dwfSummary);

    m_dwfTable = new QTableWidget(0, 6, page);
    m_dwfTable->setHorizontalHeaderLabels({
        tr("Constituent"), tr("Average"),
        tr("Monthly"), tr("Daily"), tr("Hourly"), tr("Weekend")});
    m_dwfTable->horizontalHeader()->setStretchLastSection(true);
    m_dwfTable->verticalHeader()->setVisible(false);
    m_dwfTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dwfTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_dwfTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vlay->addWidget(m_dwfTable, 1);

    m_dwfRemoveBtn = new QPushButton(tr("Remove Selected"), page);
    m_dwfRemoveBtn->setEnabled(false);
    vlay->addWidget(m_dwfRemoveBtn);
    connect(m_dwfTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]() {
        m_dwfRemoveBtn->setEnabled(!m_dwfTable->selectedItems().isEmpty());
    });
    connect(m_dwfRemoveBtn, &QPushButton::clicked, this, [this]() {
        const auto rows = m_dwfTable->selectionModel()->selectedRows();
        if (rows.isEmpty()) return;
        QTableWidgetItem *first = m_dwfTable->item(rows.first().row(), 0);
        if (!first) return;
        const int globalIdx = first->data(Qt::UserRole).toInt();
        const int rc = swmm_dwf_remove(m_ref.engine, globalIdx);
        if (rc != SWMM_OK) {
            QMessageBox::warning(this, tr("Remove DWF"),
                tr("Engine rejected DWF remove (error %1).").arg(rc));
            return;
        }
        refreshActivePage();
    });

    auto *grp = new QGroupBox(tr("Add / Update Dry Weather Flow"), page);
    auto *form = new QFormLayout(grp);

    m_dwfConstCombo = new QComboBox(grp);
    populateConstituentCombo(m_dwfConstCombo);

    m_dwfAvgSpin   = new QDoubleSpinBox(grp);
    m_dwfAvgSpin->setRange(0.0, 1e12);
    m_dwfAvgSpin->setDecimals(4);

    m_dwfPat1Picker = new LabeledPickerCombo({}, grp);
    m_dwfPat2Picker = new LabeledPickerCombo({}, grp);
    m_dwfPat3Picker = new LabeledPickerCombo({}, grp);
    m_dwfPat4Picker = new LabeledPickerCombo({}, grp);
    for (LabeledPickerCombo *p : {m_dwfPat1Picker, m_dwfPat2Picker,
                                     m_dwfPat3Picker, m_dwfPat4Picker}) {
        populatePatternCombo(p);
        wirePicker(p, SWMMModelLayer::DataPatterns,
                    &NodeCompoundEditDialog::populatePatternCombo);
    }

    form->addRow(tr("Constituent"),        m_dwfConstCombo);
    form->addRow(tr("A&verage Value"),      m_dwfAvgSpin);
    form->addRow(tr("Mont&hly Pattern"),    m_dwfPat1Picker);
    form->addRow(tr("&Daily Pattern"),      m_dwfPat2Picker);
    form->addRow(tr("Hou&rly Pattern"),     m_dwfPat3Picker);
    form->addRow(tr("&Weekend Pattern"),    m_dwfPat4Picker);

    auto *addBtn = new QPushButton(tr("Add / Update"), grp);
    form->addRow(QString(), addBtn);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        const int idx = nodeIdx();
        if (idx < 0) return;
        const QString constituent = m_dwfConstCombo->currentText();
        const QByteArray cons = constituent.toUtf8();
        const QByteArray p1   = m_dwfPat1Picker->currentText().toUtf8();
        const QByteArray p2   = m_dwfPat2Picker->currentText().toUtf8();
        const QByteArray p3   = m_dwfPat3Picker->currentText().toUtf8();
        const QByteArray p4   = m_dwfPat4Picker->currentText().toUtf8();

        int dupIdx = -1;
        const int total = swmm_dwf_count(m_ref.engine);
        char cBuf[64], p1Buf[64], p2Buf[64], p3Buf[64], p4Buf[64];
        for (int i = total - 1; i >= 0; --i) {
            int ni = -1;
            double avg = 0.0;
            if (swmm_dwf_get(m_ref.engine, i, &ni,
                              cBuf, sizeof(cBuf), &avg,
                              p1Buf, sizeof(p1Buf),
                              p2Buf, sizeof(p2Buf),
                              p3Buf, sizeof(p3Buf),
                              p4Buf, sizeof(p4Buf)) != SWMM_OK) continue;
            if (ni == idx && constituent == QString::fromUtf8(cBuf)) {
                dupIdx = i;
                break;
            }
        }
        if (dupIdx >= 0) {
            const auto r = QMessageBox::question(this, tr("Replace DWF"),
                tr("A DWF entry for <b>%1</b> already exists on this node. "
                   "Replace it with the new values?").arg(constituent),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (r != QMessageBox::Yes) return;
            swmm_dwf_remove(m_ref.engine, dupIdx);
        }

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

    connect(m_dwfTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        const auto sel = m_dwfTable->selectionModel()->selectedRows();
        if (sel.isEmpty()) return;
        const int row = sel.first().row();
        auto cellText = [this, row](int col) -> QString {
            QTableWidgetItem *it = m_dwfTable->item(row, col);
            return it ? it->text() : QString();
        };
        m_dwfConstCombo->setCurrentText(cellText(0));
        m_dwfAvgSpin->setValue(cellText(1).toDouble());
        m_dwfPat1Picker->setCurrentText(cellText(2));
        m_dwfPat2Picker->setCurrentText(cellText(3));
        m_dwfPat3Picker->setCurrentText(cellText(4));
        m_dwfPat4Picker->setCurrentText(cellText(5));
    });

    vlay->addWidget(grp);
    m_stack->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, m_stack));
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
    m_rdiiTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_rdiiTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vlay->addWidget(m_rdiiTable, 1);

    m_rdiiRemoveBtn = new QPushButton(tr("Remove Selected"), page);
    m_rdiiRemoveBtn->setEnabled(false);
    vlay->addWidget(m_rdiiRemoveBtn);
    connect(m_rdiiTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]() {
        m_rdiiRemoveBtn->setEnabled(!m_rdiiTable->selectedItems().isEmpty());
    });
    connect(m_rdiiRemoveBtn, &QPushButton::clicked, this, [this]() {
        const auto rows = m_rdiiTable->selectionModel()->selectedRows();
        if (rows.isEmpty()) return;
        QTableWidgetItem *first = m_rdiiTable->item(rows.first().row(), 0);
        if (!first) return;
        const int globalIdx = first->data(Qt::UserRole).toInt();
        const int rc = swmm_rdii_remove(m_ref.engine, globalIdx);
        if (rc != SWMM_OK) {
            QMessageBox::warning(this, tr("Remove RDII"),
                tr("Engine rejected RDII remove (error %1).").arg(rc));
            return;
        }
        refreshActivePage();
    });

    auto *grp = new QGroupBox(tr("Add / Update RDII Assignment"), page);
    auto *form = new QFormLayout(grp);

    // DB.4f — UH picker. The "..." button opens the HydrographGroupEditor
    // as a modal picker: the user can create / edit / browse UH groups
    // inside the editor, and whatever group is highlighted when they
    // close the editor is written back into this picker as the chosen
    // UH group name. Slice BS Phase 6.9.2 promotion of the old
    // NewDataObjectDialog-only flow.
    m_rdiiUhPicker = new LabeledPickerCombo({}, grp);
    populateUhGroupCombo(m_rdiiUhPicker);
    wirePicker(m_rdiiUhPicker, SWMMModelLayer::DataHydrographs,
                &NodeCompoundEditDialog::populateUhGroupCombo);

    m_rdiiAreaSpin = new QDoubleSpinBox(grp);
    m_rdiiAreaSpin->setRange(0.0, 1e9);
    m_rdiiAreaSpin->setDecimals(4);

    form->addRow(tr("UH &Group Name"), m_rdiiUhPicker);
    form->addRow(tr("Sewer Area"),    m_rdiiAreaSpin);

    auto *addBtn = new QPushButton(tr("Add"), grp);
    form->addRow(QString(), addBtn);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        const int idx = nodeIdx();
        if (idx < 0) return;
        const QString uhName = m_rdiiUhPicker->currentText();
        if (uhName.isEmpty()) {
            QMessageBox::warning(this, tr("Add RDII"),
                tr("Provide a UH group name."));
            return;
        }
        const QByteArray uh = uhName.toUtf8();
        const int rc = swmm_rdii_add(m_ref.engine, idx,
                                       uh.constData(), m_rdiiAreaSpin->value());
        if (rc != SWMM_OK) {
            QMessageBox::warning(this, tr("Add RDII"),
                tr("Engine rejected RDII add (error %1).").arg(rc));
            return;
        }
        refreshActivePage();
    });

    // Row selection → populate form (the user can tweak area then Add
    // a new assignment for a different UH).
    connect(m_rdiiTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        const auto sel = m_rdiiTable->selectionModel()->selectedRows();
        if (sel.isEmpty()) return;
        const int row = sel.first().row();
        auto cellText = [this, row](int col) -> QString {
            QTableWidgetItem *it = m_rdiiTable->item(row, col);
            return it ? it->text() : QString();
        };
        m_rdiiUhPicker->setCurrentText(cellText(0));
        m_rdiiAreaSpin->setValue(cellText(1).toDouble());
    });

    vlay->addWidget(grp);
    m_stack->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, m_stack));
}

// ============================================================================
// Treatment page — per-pollutant removal expression
//
// Mirrors the legacy `Dtreat.pas` two-column form: one row per pollutant
// defined on the model, with an editable Expression cell. An empty
// expression maps to `swmm_treatment_clear`; any non-empty string maps
// to `swmm_treatment_set`. The engine validates expression syntax at
// the set call (returns non-OK with a message in its error buffer);
// invalid input is rejected with a warning and the cell reverts on the
// next refresh.
// ============================================================================

void NodeCompoundEditDialog::buildTreatmentPage()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    m_treatmentSummary = new QLabel(page);
    m_treatmentSummary->setWordWrap(true);
    vlay->addWidget(m_treatmentSummary);

    auto *hint = new QLabel(
        tr("<i>Edit a removal expression per pollutant — e.g. "
           "<tt>R = 0.5 * exp(-0.1 * DT)</tt> or "
           "<tt>C = 0.3 * C</tt>. Clear a cell to remove the expression. "
           "Variables: <tt>R</tt> (removal fraction), <tt>C</tt> "
           "(concentration), <tt>DT</tt> (step seconds), "
           "<tt>HRT</tt> (hyd. residence time), <tt>Q</tt> (flow), "
           "<tt>V</tt> (volume), <tt>D</tt> (depth), <tt>AREA</tt> "
           "(surface area). Functions: <tt>exp log ln sqrt min max abs "
           "sgn step</tt>. Ctrl+Space completes.</i>"),
        page);
    hint->setWordWrap(true);
    hint->setStyleSheet(openswmmvis::ui::theme::hintStyle());
    vlay->addWidget(hint);

    m_treatmentTable = new QTableWidget(0, 2, page);
    m_treatmentTable->setHorizontalHeaderLabels(
        {tr("Pollutant"), tr("Expression")});
    m_treatmentTable->horizontalHeader()->setStretchLastSection(true);
    m_treatmentTable->verticalHeader()->setVisible(false);
    m_treatmentTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    vlay->addWidget(m_treatmentTable, 1);

    // Iteration 4 — expression cells edit through the highlighting /
    // completing / live-validating editor (the [TREATMENT] peer of the
    // control rules editor); the banner mirrors the validator verdict.
    auto *delegate = new openswmmvis::ui::TreatmentExpressionDelegate(
        m_ref.engine, m_treatmentTable);
    m_treatmentTable->setItemDelegateForColumn(1, delegate);

    m_treatmentBanner = new QLabel(page);
    m_treatmentBanner->setWordWrap(true);
    m_treatmentBanner->setVisible(false);
    vlay->addWidget(m_treatmentBanner);
    connect(delegate,
            &openswmmvis::ui::TreatmentExpressionDelegate::validationChanged,
            this, [this](bool ok, const QString &msg, int col) {
        using namespace openswmmvis::ui::theme;
        if (ok && msg.isEmpty()) {
            m_treatmentBanner->setText(tr("● Valid expression"));
            m_treatmentBanner->setStyleSheet(bannerStyle(Banner::Success));
        } else {
            m_treatmentBanner->setText(
                col >= 0 ? tr("⚠ Column %1: %2").arg(col + 1).arg(msg)
                         : tr("⚠ %1").arg(msg));
            m_treatmentBanner->setStyleSheet(bannerStyle(Banner::Error));
        }
        m_treatmentBanner->setVisible(true);
    });

    // Cell commit → engine. The first column (Pollutant) is non-editable
    // (flags cleared in refresh); only column 1 (Expression) reaches
    // this commit path.
    connect(m_treatmentTable, &QTableWidget::itemChanged,
            this, [this](QTableWidgetItem *item) {
        if (!item || m_treatmentSuppressCommit) return;
        if (item->column() != 1) return;
        const int idx = nodeIdx();
        if (idx < 0) return;
        const int polIdx = item->row();  // row == pollutant index
        const QString expr = item->text().trimmed();
        int rc = SWMM_OK;
        if (expr.isEmpty()) {
            rc = swmm_treatment_clear(m_ref.engine, idx, polIdx);
        } else {
            const QByteArray bytes = expr.toUtf8();
            rc = swmm_treatment_set(m_ref.engine, idx, polIdx, bytes.constData());
        }
        if (rc != SWMM_OK) {
            // Iteration 4 — the validator supplies the human diagnostic +
            // position the bare set call never had.
            char errbuf[512] = {};
            int col = -1;
            swmm_treatment_validate_expression(
                m_ref.engine, expr.toUtf8().constData(),
                errbuf, sizeof(errbuf), &col);
            const QString why = QString::fromUtf8(errbuf);
            QMessageBox::warning(this, tr("Treatment"),
                why.isEmpty()
                    ? tr("Engine rejected the expression (error %1). "
                         "The cell will revert.").arg(rc)
                    : (col >= 0
                           ? tr("Invalid expression at column %1: %2\n"
                                "The cell will revert.").arg(col + 1).arg(why)
                           : tr("Invalid expression: %1\n"
                                "The cell will revert.").arg(why)));
        }
        // Always re-read so a rejected edit reverts and an accepted
        // one normalises (engine may canonicalise whitespace).
        refreshActivePage();
    });

    m_stack->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, m_stack));
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
        m_inflowsTable->setRowCount(0);
        m_inflowsRemoveBtn->setEnabled(false);
        const int total = swmm_ext_inflow_count(m_ref.engine);
        int matched = 0;
        char consBuf[64], tsBuf[64], typeBuf[16], patBuf[64];
        for (int i = 0; i < total; ++i) {
            int ni = -1;
            double mf = 0.0, sf = 0.0, base = 0.0;
            if (swmm_ext_inflow_get(m_ref.engine, i, &ni,
                                      consBuf, sizeof(consBuf),
                                      tsBuf,   sizeof(tsBuf),
                                      typeBuf, sizeof(typeBuf),
                                      &mf, &sf, &base,
                                      patBuf,  sizeof(patBuf)) != SWMM_OK)
                continue;
            if (ni != idx) continue;
            const int row = m_inflowsTable->rowCount();
            m_inflowsTable->insertRow(row);
            auto *consItem = new QTableWidgetItem(QString::fromUtf8(consBuf));
            // Stash the global engine index on column 0 so Remove can find it.
            consItem->setData(Qt::UserRole, i);
            m_inflowsTable->setItem(row, 0, consItem);
            m_inflowsTable->setItem(row, 1,
                new QTableWidgetItem(QString::fromUtf8(typeBuf)));
            m_inflowsTable->setItem(row, 2,
                new QTableWidgetItem(QString::fromUtf8(tsBuf)));
            m_inflowsTable->setItem(row, 3,
                new QTableWidgetItem(QString::number(base, 'g', 6)));
            m_inflowsTable->setItem(row, 4,
                new QTableWidgetItem(QString::number(mf, 'g', 6)));
            m_inflowsTable->setItem(row, 5,
                new QTableWidgetItem(QString::number(sf, 'g', 6)));
            m_inflowsTable->setItem(row, 6,
                new QTableWidgetItem(QString::fromUtf8(patBuf)));
            ++matched;
        }
        m_inflowsSummary->setText(tr(
            "<b>%1</b> external inflow(s) on this node "
            "(model total: %2).").arg(matched).arg(total));
        m_ref.summary = (matched > 0)
            ? tr("(%1 entries)").arg(matched)
            : tr("(none)");
        break;
    }
    case NodeCompoundEditRef::Dwf: {
        m_dwfTable->setRowCount(0);
        m_dwfRemoveBtn->setEnabled(false);
        const int total = swmm_dwf_count(m_ref.engine);
        int matched = 0;
        char consBuf[64], p1Buf[64], p2Buf[64], p3Buf[64], p4Buf[64];
        for (int i = 0; i < total; ++i) {
            int ni = -1;
            double avg = 0.0;
            if (swmm_dwf_get(m_ref.engine, i, &ni,
                              consBuf, sizeof(consBuf),
                              &avg,
                              p1Buf, sizeof(p1Buf),
                              p2Buf, sizeof(p2Buf),
                              p3Buf, sizeof(p3Buf),
                              p4Buf, sizeof(p4Buf)) != SWMM_OK)
                continue;
            if (ni != idx) continue;
            const int row = m_dwfTable->rowCount();
            m_dwfTable->insertRow(row);
            auto *consItem = new QTableWidgetItem(QString::fromUtf8(consBuf));
            consItem->setData(Qt::UserRole, i);
            m_dwfTable->setItem(row, 0, consItem);
            m_dwfTable->setItem(row, 1,
                new QTableWidgetItem(QString::number(avg, 'g', 6)));
            m_dwfTable->setItem(row, 2,
                new QTableWidgetItem(QString::fromUtf8(p1Buf)));
            m_dwfTable->setItem(row, 3,
                new QTableWidgetItem(QString::fromUtf8(p2Buf)));
            m_dwfTable->setItem(row, 4,
                new QTableWidgetItem(QString::fromUtf8(p3Buf)));
            m_dwfTable->setItem(row, 5,
                new QTableWidgetItem(QString::fromUtf8(p4Buf)));
            ++matched;
        }
        m_dwfSummary->setText(tr(
            "<b>%1</b> DWF entr%2 on this node "
            "(model total: %3).")
                .arg(matched)
                .arg(matched == 1 ? "y" : "ies")
                .arg(total));
        m_ref.summary = (matched > 0)
            ? tr("(%1 entries)").arg(matched)
            : tr("(none)");
        break;
    }
    case NodeCompoundEditRef::Rdii: {
        // RDII has per-entry read: iterate, filter by node_idx.
        m_rdiiTable->setRowCount(0);
        m_rdiiRemoveBtn->setEnabled(false);
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
            auto *uhItem = new QTableWidgetItem(QString::fromUtf8(uhBuf));
            uhItem->setData(Qt::UserRole, i);
            m_rdiiTable->setItem(row, 0, uhItem);
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
    case NodeCompoundEditRef::Treatment: {
        // Per-pollutant grid. Engine indexes treatment by (node, pollut)
        // so we enumerate pollutants and read each cell. swmm_treatment_get
        // returns SWMM_OK with an empty buffer when no expression is set;
        // we treat empty as "no rule".
        m_treatmentSuppressCommit = true;
        m_treatmentTable->setRowCount(0);
        const int nPollut = swmm_pollutant_count(m_ref.engine);
        int active = 0;
        char exprBuf[256];
        for (int p = 0; p < nPollut; ++p) {
            const char *pName = swmm_pollutant_id(m_ref.engine, p);
            const int row = m_treatmentTable->rowCount();
            m_treatmentTable->insertRow(row);

            auto *nameItem = new QTableWidgetItem(
                pName ? QString::fromUtf8(pName) : tr("(pollutant %1)").arg(p));
            // Pollutant column is identification only — not editable.
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            m_treatmentTable->setItem(row, 0, nameItem);

            exprBuf[0] = '\0';
            swmm_treatment_get(m_ref.engine, idx, p, exprBuf, sizeof(exprBuf));
            auto *exprItem = new QTableWidgetItem(QString::fromUtf8(exprBuf));
            m_treatmentTable->setItem(row, 1, exprItem);
            if (exprBuf[0] != '\0') ++active;
        }
        m_treatmentSuppressCommit = false;

        if (nPollut == 0) {
            m_treatmentSummary->setText(tr(
                "<i>No pollutants defined in the model — add pollutants "
                "(Project → Data → Pollutants) before authoring treatment "
                "expressions.</i>"));
        } else {
            m_treatmentSummary->setText(tr(
                "<b>%1</b> of <b>%2</b> pollutant(s) have a treatment "
                "expression on this node.").arg(active).arg(nPollut));
        }
        m_ref.summary = (active > 0)
            ? tr("(%1 / %2 pollutants)").arg(active).arg(nPollut)
            : tr("(none)");
        break;
    }
    }
}
