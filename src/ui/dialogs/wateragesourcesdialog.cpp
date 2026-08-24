/*!
 * \file wateragesourcesdialog.cpp
 * \brief Implementation of the water-age source-table editor (Y3).
 * \see include/ui/dialogs/wateragesourcesdialog.h
 */

#include "ui/dialogs/wateragesourcesdialog.h"

#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_water_age.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
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
        m_globalTable->setCellWidget(r, 1, spin);
    }
    vlay->addWidget(m_globalTable);

    // ── Per-node overrides ─────────────────────────────────────────────
    vlay->addWidget(new QLabel(
        tr("Per-node overrides (dry weather flow and external inflow only):"),
        this));
    m_overrideTable = new QTableWidget(0, 3, this);
    m_overrideTable->setObjectName(QStringLiteral("wa_overrideTable"));
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
    btnRow->addWidget(addBtn);
    btnRow->addWidget(remBtn);
    btnRow->addStretch();
    vlay->addLayout(btnRow);
    connect(addBtn, &QPushButton::clicked,
            this, &WaterAgeSourcesDialog::onAddOverride);
    connect(remBtn, &QPushButton::clicked,
            this, &WaterAgeSourcesDialog::onRemoveOverride);

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
}

void WaterAgeSourcesDialog::onRemoveOverride()
{
    const int r = m_overrideTable->currentRow();
    if (r >= 0) m_overrideTable->removeRow(r);
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
            spin->setValue(hours);
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
            s->setValue(hours);
    }
}

int WaterAgeSourcesDialog::writeToEngine()
{
    if (!m_engine) return 0;
    int writes = 0;

    // Globals: write only what changed, so an untouched OK is a no-op and
    // cannot dirty the project (the writeIfChanged discipline).
    for (int r = 0; r < kSourceCount; ++r) {
        auto *spin = qobject_cast<QDoubleSpinBox *>(
            m_globalTable->cellWidget(r, 1));
        if (!spin) continue;
        double current = 0.0;
        if (swmm_water_age_get_global_source(m_engine, kSources[r].code,
                                             &current) != SWMM_OK)
            continue;
        if (qFuzzyCompare(1.0 + current, 1.0 + spin->value())) continue;
        if (swmm_water_age_set_global_source(m_engine, kSources[r].code,
                                             spin->value()) == SWMM_OK)
            ++writes;
    }

    // Overrides: the engine keeps a keyed list, the table an ordered one.
    // Remove every engine row the table no longer carries, then set every
    // table row (set is add-or-update). Removing first keeps a row the
    // user deleted from surviving as a stale key.
    int count = 0;
    if (swmm_water_age_override_count(m_engine, &count) == SWMM_OK) {
        QVector<QPair<int, int>> engineKeys;     // (source, node)
        for (int i = 0; i < count; ++i) {
            int src = 0, node = 0;
            double h = 0.0;
            if (swmm_water_age_get_override(m_engine, i, &src, &node, &h)
                    == SWMM_OK)
                engineKeys.append({ src, node });
        }
        QVector<QPair<int, int>> tableKeys;
        for (int r = 0; r < m_overrideTable->rowCount(); ++r) {
            auto *sc = qobject_cast<QComboBox *>(
                m_overrideTable->cellWidget(r, 0));
            auto *nc = qobject_cast<QComboBox *>(
                m_overrideTable->cellWidget(r, 1));
            if (sc && nc)
                tableKeys.append({ sc->currentData().toInt(),
                                   nc->currentData().toInt() });
        }
        for (const auto &k : engineKeys)
            if (!tableKeys.contains(k)
                && swmm_water_age_remove_override(m_engine, k.first, k.second)
                       == SWMM_OK)
                ++writes;
    }

    for (int r = 0; r < m_overrideTable->rowCount(); ++r) {
        auto *sc = qobject_cast<QComboBox *>(
            m_overrideTable->cellWidget(r, 0));
        auto *nc = qobject_cast<QComboBox *>(
            m_overrideTable->cellWidget(r, 1));
        auto *hs = qobject_cast<QDoubleSpinBox *>(
            m_overrideTable->cellWidget(r, 2));
        if (!sc || !nc || !hs) continue;
        const int src  = sc->currentData().toInt();
        const int node = nc->currentData().toInt();
        // Skip a set that would change nothing — same no-op discipline.
        int existing = 0;
        double curHours = 0.0;
        bool found = false;
        if (swmm_water_age_override_count(m_engine, &existing) == SWMM_OK) {
            for (int i = 0; i < existing; ++i) {
                int s2 = 0, n2 = 0;
                double h2 = 0.0;
                if (swmm_water_age_get_override(m_engine, i, &s2, &n2, &h2)
                        == SWMM_OK && s2 == src && n2 == node) {
                    found = true;
                    curHours = h2;
                    break;
                }
            }
        }
        if (found && qFuzzyCompare(1.0 + curHours, 1.0 + hs->value()))
            continue;
        if (swmm_water_age_set_override(m_engine, src, node, hs->value())
                == SWMM_OK)
            ++writes;
    }

    return writes;
}

void WaterAgeSourcesDialog::onAccept()
{
    m_lastWriteCount  = writeToEngine();
    m_wroteAnyChanges = m_lastWriteCount > 0;
    accept();
}

} // namespace OpenSWMMVis
