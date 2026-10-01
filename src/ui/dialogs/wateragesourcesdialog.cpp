/*!
 * \file wateragesourcesdialog.cpp
 * \brief Implementation of the water-age source-table editor (Y3).
 * \see include/ui/dialogs/wateragesourcesdialog.h
 */

#include "ui/dialogs/wateragesourcesdialog.h"
#include "ui/precisenumericvalue.h"

#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_water_age.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace OpenSWMMVis
{

namespace {

/*! The seven pathways, in the engine's storage order — the same order
 *  `SWMM_WaterAgeSource` declares, so a row index IS the source code. */
struct SourceRow { int code; const char *label; };

const SourceRow kSources[] = {
    { SWMM_AGE_SRC_RAINFALL,        QT_TRANSLATE_NOOP("WaterAge", "Rainfall / runoff") },
    { SWMM_AGE_SRC_DWF,             QT_TRANSLATE_NOOP("WaterAge", "Dry weather flow") },
    { SWMM_AGE_SRC_GW,              QT_TRANSLATE_NOOP("WaterAge", "Groundwater") },
    { SWMM_AGE_SRC_RDII,            QT_TRANSLATE_NOOP("WaterAge", "RDII") },
    { SWMM_AGE_SRC_EXTERNAL_INFLOW, QT_TRANSLATE_NOOP("WaterAge", "External inflow") },
    { SWMM_AGE_SRC_IFACE,           QT_TRANSLATE_NOOP("WaterAge", "Routing interface file") },
    { SWMM_AGE_SRC_INITIAL_STATE,   QT_TRANSLATE_NOOP("WaterAge", "Initial network state") },
};
constexpr int kSourceCount = int(sizeof(kSources) / sizeof(kSources[0]));

/*! Only these two pathways accept per-node overrides — the engine parser's
 *  A1a scope rule, which `swmm_water_age_set_override` refuses to break. */
bool nodeScoped(int code)
{
    return code == SWMM_AGE_SRC_DWF || code == SWMM_AGE_SRC_EXTERNAL_INFLOW;
}

/*! Hours spin: wide range, and NEGATIVE values are legal (D-NS1 —
 *  extraction). Three decimals so a seconds-scale age survives a
 *  round-trip through hours. */
QDoubleSpinBox *makeHoursSpin(QWidget *parent)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(-1.0e6, 1.0e6);
    s->setDecimals(3);
    s->setSuffix(QStringLiteral(" h"));
    return s;
}

} // namespace

WaterAgeSourcesDialog::WaterAgeSourcesDialog(SWMM_Engine engine,
                                             QWidget *parent)
    : QDialog(parent), m_engine(engine)
{
    setWindowTitle(tr("Water Age Sources"));
    setObjectName(QStringLiteral("waterAgeSourcesDialog"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    buildUi();
    readFromEngine();
}

void WaterAgeSourcesDialog::invalidateEngine()
{
    m_engine = nullptr;
    setEnabled(false);
    reject();
}

void WaterAgeSourcesDialog::buildUi()
{
    auto *vlay = new QVBoxLayout(this);

    m_hintLabel = new QLabel(
        tr("Initial age of the water entering the model by each pathway, in "
           "hours. A <b>negative</b> age extracts age-volume — the water "
           "reads younger — and is clamped so age never falls below zero."),
        this);
    m_hintLabel->setObjectName(QStringLiteral("wa_hint"));
    m_hintLabel->setWordWrap(true);
    vlay->addWidget(m_hintLabel);

    // ── Global ages: one fixed row per pathway ─────────────────────────
    vlay->addWidget(new QLabel(tr("Global source ages:"), this));
    m_globalTable = new QTableWidget(kSourceCount, 2, this);
    m_globalTable->setObjectName(QStringLiteral("wa_globalTable"));
    m_globalTable->setAccessibleName(tr("Global water ages"));
    m_globalTable->setAccessibleDescription(tr("Initial age in hours for each source pathway."));
    m_globalTable->setHorizontalHeaderLabels(
        { tr("Source"), tr("Age (hours)") });
    m_globalTable->verticalHeader()->setVisible(false);
    m_globalTable->horizontalHeader()->setStretchLastSection(true);
    m_globalTable->setSelectionMode(QAbstractItemView::NoSelection);
    for (int r = 0; r < kSourceCount; ++r) {
        auto *nameItem = new QTableWidgetItem(
            QCoreApplication::translate("WaterAge", kSources[r].label));
        nameItem->setFlags(Qt::ItemIsEnabled);          // label column
        nameItem->setData(Qt::UserRole, kSources[r].code);
        m_globalTable->setItem(r, 0, nameItem);

        auto *spin = makeHoursSpin(m_globalTable);
        spin->setObjectName(QStringLiteral("wa_globalSpin_%1")
                                .arg(kSources[r].code));
        spin->setAccessibleName(tr("%1 age (hours)").arg(nameItem->text()));
        spin->setAccessibleDescription(tr("Negative ages extract age-volume."));
        m_globalTable->setCellWidget(r, 1, spin);
    }
    vlay->addWidget(m_globalTable);

    // ── Per-node overrides ─────────────────────────────────────────────
    vlay->addWidget(new QLabel(
        tr("Per-node overrides (dry weather flow and external inflow only):"),
        this));
    m_overrideTable = new QTableWidget(0, 3, this);
    m_overrideTable->setObjectName(QStringLiteral("wa_overrideTable"));
    m_overrideTable->setAccessibleName(tr("Per-node water age overrides"));
    m_overrideTable->setAccessibleDescription(
        tr("Each source and node pair must be unique. Ages are in hours; negative ages extract age-volume."));
    m_overrideTable->setHorizontalHeaderLabels(
        { tr("Source"), tr("Node"), tr("Age (hours)") });
    m_overrideTable->verticalHeader()->setVisible(false);
    m_overrideTable->horizontalHeader()->setStretchLastSection(true);
    vlay->addWidget(m_overrideTable);

    auto *btnRow = new QHBoxLayout;
    auto *addBtn = new QPushButton(tr("&Add"), this);
    addBtn->setObjectName(QStringLiteral("wa_addBtn"));
    auto *remBtn = new QPushButton(tr("&Remove"), this);
    remBtn->setObjectName(QStringLiteral("wa_removeBtn"));
    addBtn->setAutoDefault(false);
    remBtn->setAutoDefault(false);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(remBtn);
    btnRow->addStretch();
    vlay->addLayout(btnRow);
    connect(addBtn, &QPushButton::clicked,
            this, &WaterAgeSourcesDialog::onAddOverride);
    connect(remBtn, &QPushButton::clicked,
            this, &WaterAgeSourcesDialog::onRemoveOverride);

    m_validationError = new QLabel(this);
    m_validationError->setObjectName(QStringLiteral("wa_validationError"));
    m_validationError->setAccessibleName(tr("Water age validation"));
    m_validationError->setTextFormat(Qt::PlainText);
    m_validationError->setWordWrap(true);
    m_validationError->hide();
    vlay->addWidget(m_validationError);

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    vlay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted,
            this, &WaterAgeSourcesDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void WaterAgeSourcesDialog::onAddOverride()
{
    if (!m_engine) return;
    const int r = m_overrideTable->rowCount();
    m_overrideTable->insertRow(r);

    auto *srcCombo = new QComboBox(m_overrideTable);
    for (const auto &s : kSources)
        if (nodeScoped(s.code))
            srcCombo->addItem(
                QCoreApplication::translate("WaterAge", s.label), s.code);
    m_overrideTable->setCellWidget(r, 0, srcCombo);

    auto *nodeCombo = new QComboBox(m_overrideTable);
    const int n = swmm_node_count(m_engine);
    for (int i = 0; i < n; ++i) {
        const char *id = swmm_node_id(m_engine, i);
        nodeCombo->addItem(id ? QString::fromUtf8(id)
                              : QStringLiteral("#%1").arg(i), i);
    }
    m_overrideTable->setCellWidget(r, 1, nodeCombo);

    m_overrideTable->setCellWidget(r, 2, makeHoursSpin(m_overrideTable));
    refreshOverrideAccessibility();
}

void WaterAgeSourcesDialog::onRemoveOverride()
{
    const int r = m_overrideTable->currentRow();
    if (r >= 0) m_overrideTable->removeRow(r);
    refreshOverrideAccessibility();
}

void WaterAgeSourcesDialog::refreshOverrideAccessibility()
{
    for (int row = 0; row < m_overrideTable->rowCount(); ++row) {
        for (int column = 0; column < m_overrideTable->columnCount(); ++column) {
            if (auto *editor = m_overrideTable->cellWidget(row, column)) {
                editor->setAccessibleName(tr("Override row %1, %2")
                    .arg(row + 1).arg(m_overrideTable->horizontalHeaderItem(column)->text()));
                // Native combo accessibility can expose the selected value
                // as Name; retain row/field context in Description as well.
                editor->setAccessibleDescription(tr("%1. %2").arg(editor->accessibleName(), column == 2
                    ? tr("Age in hours. Negative ages extract age-volume.")
                    : tr("Each source and node pair must be unique.")));
            }
        }
    }
}

bool WaterAgeSourcesDialog::validateOverrides()
{
    m_validationError->clear();
    m_validationError->hide();
    refreshOverrideAccessibility();
    if (!m_engine) {
        m_validationError->setText(tr("Open a model before editing water age sources."));
        m_validationError->show();
        return false;
    }

    auto refuse = [this](int row, int column, const QString &message) {
        m_validationError->setText(message);
        m_validationError->show();
        m_overrideTable->setCurrentCell(row, column);
        m_overrideTable->scrollTo(m_overrideTable->model()->index(row, column));
        if (auto *editor = m_overrideTable->cellWidget(row, column)) {
            editor->setAccessibleDescription(tr("%1. %2").arg(editor->accessibleName(), message));
            editor->setFocus(Qt::OtherFocusReason);
        }
        return false;
    };
    QHash<QPair<int, int>, int> rowsByKey;
    const int nodeCount = swmm_node_count(m_engine);
    for (int row = 0; row < m_overrideTable->rowCount(); ++row) {
        auto *source = qobject_cast<QComboBox *>(m_overrideTable->cellWidget(row, 0));
        auto *node = qobject_cast<QComboBox *>(m_overrideTable->cellWidget(row, 1));
        bool sourceOk = false, nodeOk = false;
        const int sourceCode = source ? source->currentData().toInt(&sourceOk) : -1;
        const int nodeIndex = node ? node->currentData().toInt(&nodeOk) : -1;
        if (!sourceOk || !nodeScoped(sourceCode))
            return refuse(row, 0, tr("Row %1: select dry weather flow or external inflow.").arg(row + 1));
        const char *nodeId = nodeOk && nodeIndex >= 0 && nodeIndex < nodeCount
            ? swmm_node_id(m_engine, nodeIndex) : nullptr;
        if (!nodeId || QString::fromUtf8(nodeId) != node->currentText())
            return refuse(row, 1, tr("Row %1: select an available node. If the model's nodes changed, reopen this dialog.").arg(row + 1));
        const QPair<int, int> key{sourceCode, nodeIndex};
        const auto previous = rowsByKey.constFind(key);
        if (previous != rowsByKey.cend())
            return refuse(row, 0, tr("Row %1 duplicates row %2. Choose a different source or node.")
                .arg(row + 1).arg(previous.value() + 1));
        rowsByKey.insert(key, row);
    }
    return true;
}

void WaterAgeSourcesDialog::readFromEngine()
{
    if (!m_engine) return;

    for (int r = 0; r < kSourceCount; ++r) {
        double hours = 0.0;
        if (swmm_water_age_get_global_source(m_engine, kSources[r].code,
                                             &hours) != SWMM_OK)
            continue;
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(
                m_globalTable->cellWidget(r, 1)))
            OpenSWMM::Ui::setHydratedValue(spin, hours);
    }

    m_overrideTable->setRowCount(0);
    int count = 0;
    if (swmm_water_age_override_count(m_engine, &count) != SWMM_OK) return;
    for (int i = 0; i < count; ++i) {
        int src = 0, node = 0;
        double hours = 0.0;
        if (swmm_water_age_get_override(m_engine, i, &src, &node, &hours)
                != SWMM_OK)
            continue;
        onAddOverride();                        // builds the row's widgets
        const int r = m_overrideTable->rowCount() - 1;
        if (auto *c = qobject_cast<QComboBox *>(
                m_overrideTable->cellWidget(r, 0))) {
            const int idx = c->findData(src);
            if (idx >= 0) c->setCurrentIndex(idx);
        }
        if (auto *c = qobject_cast<QComboBox *>(
                m_overrideTable->cellWidget(r, 1))) {
            const int idx = c->findData(node);
            if (idx >= 0) c->setCurrentIndex(idx);
        }
        if (auto *s = qobject_cast<QDoubleSpinBox *>(
                m_overrideTable->cellWidget(r, 2)))
            OpenSWMM::Ui::setHydratedValue(s, hours);
    }
}

int WaterAgeSourcesDialog::writeToEngine()
{
    if (!m_engine) return 0;
    int writes = 0;
    auto checked = [this](int status, QWidget *field, const QString &action) {
        if (status == SWMM_OK) return true;
        m_writeFailed = true;
        const QString message = tr("%1 failed (engine error %2). Earlier successful changes remain applied; correct the draft and try again.")
            .arg(action).arg(status);
        m_validationError->setText(message);
        m_validationError->show();
        if (field) {
            field->setAccessibleDescription(tr("%1. %2").arg(field->accessibleName(), message));
            field->setFocus(Qt::OtherFocusReason);
        }
        return false;
    };

    for (int r = 0; r < kSourceCount; ++r) {
        auto *spin = qobject_cast<QDoubleSpinBox *>(m_globalTable->cellWidget(r, 1));
        if (!spin) continue;
        const QString label = m_globalTable->item(r, 0)->text();
        double current = 0.0;
        if (!checked(swmm_water_age_get_global_source(m_engine, kSources[r].code, &current),
                     spin, tr("Reading %1").arg(label))) return writes;
        const double desired = OpenSWMM::Ui::preciseValue(spin);
        if (qFuzzyCompare(1.0 + current, 1.0 + desired)) continue;
        if (!checked(swmm_water_age_set_global_source(m_engine, kSources[r].code, desired),
                     spin, tr("Saving %1").arg(label))) return writes;
        ++writes;
    }

    int count = 0;
    if (!checked(swmm_water_age_override_count(m_engine, &count), m_overrideTable,
                 tr("Reading overrides"))) return writes;
    QVector<QPair<int, int>> engineKeys, tableKeys;
    for (int i = 0; i < count; ++i) {
        int source = 0, node = 0;
        double value = 0.0;
        if (!checked(swmm_water_age_get_override(m_engine, i, &source, &node, &value),
                     m_overrideTable, tr("Reading override %1").arg(i + 1))) return writes;
        engineKeys.append({source, node});
    }
    for (int row = 0; row < m_overrideTable->rowCount(); ++row) {
        auto *source = qobject_cast<QComboBox *>(m_overrideTable->cellWidget(row, 0));
        auto *node = qobject_cast<QComboBox *>(m_overrideTable->cellWidget(row, 1));
        tableKeys.append({source->currentData().toInt(), node->currentData().toInt()});
    }
    for (const auto &key : engineKeys) {
        if (tableKeys.contains(key)) continue;
        if (!checked(swmm_water_age_remove_override(m_engine, key.first, key.second),
                     m_overrideTable, tr("Removing an override"))) return writes;
        ++writes;
    }
    for (int row = 0; row < m_overrideTable->rowCount(); ++row) {
        auto *spin = qobject_cast<QDoubleSpinBox *>(m_overrideTable->cellWidget(row, 2));
        const auto key = tableKeys.at(row);
        int existing = 0;
        if (!checked(swmm_water_age_override_count(m_engine, &existing), spin,
                     tr("Reading overrides"))) return writes;
        bool found = false;
        double current = 0.0;
        for (int i = 0; i < existing; ++i) {
            int source = 0, node = 0;
            double value = 0.0;
            if (!checked(swmm_water_age_get_override(m_engine, i, &source, &node, &value), spin,
                         tr("Reading override %1").arg(row + 1))) return writes;
            if (source == key.first && node == key.second) { found = true; current = value; break; }
        }
        const double desired = OpenSWMM::Ui::preciseValue(spin);
        if (found && qFuzzyCompare(1.0 + current, 1.0 + desired)) continue;
        if (!checked(swmm_water_age_set_override(m_engine, key.first, key.second, desired), spin,
                     tr("Saving override %1").arg(row + 1))) return writes;
        ++writes;
    }
    return writes;
}

void WaterAgeSourcesDialog::onAccept()
{
    // Complete draft validation must precede globals and removals as well as
    // override writes; the engine otherwise silently replaces duplicate keys.
    m_lastWriteCount = 0;
    if (!validateOverrides()) return;
    m_writeFailed = false;
    m_lastWriteCount = writeToEngine();
    if (m_lastWriteCount > 0) {
        m_wroteAnyChanges = true;
        emit changesApplied();
    }
    if (!m_writeFailed) accept();
}

} // namespace OpenSWMMVis
