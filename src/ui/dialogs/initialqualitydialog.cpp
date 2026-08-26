/*!
 * \file initialqualitydialog.cpp
 * \brief Implementation of the per-element initial-quality editor (G-A1).
 * \see include/ui/dialogs/initialqualitydialog.h
 */

#include "ui/dialogs/initialqualitydialog.h"

#include <openswmm/engine/openswmm_initial_quality.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_pollutants.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cstring>

namespace OpenSWMMVis
{

namespace {

constexpr int kColScope       = 0;
constexpr int kColElement     = 1;
constexpr int kColConstituent = 2;
constexpr int kColValue       = 3;

const char kWaterAgeName[]    = "__WATER_AGE__";
const char kTemperatureName[] = "__TEMPERATURE__";

/*! YES/NO [OPTIONS] probe — gates the reserved constituent entries. */
bool optionOn(SWMM_Engine engine, const char *key)
{
    char buf[16] = {0};
    if (!engine ||
        swmm_options_get(engine, key, buf, sizeof(buf)) != SWMM_OK)
        return false;
    return std::strcmp(buf, "YES") == 0;
}

/*! Wide-range value spin. Constituent-dependent limits/suffix are applied
 *  by the constituent-combo handler: pollutants floor at 0; the reserved
 *  species are signed (age extraction, D-NS1; degC temperatures). */
QDoubleSpinBox *makeValueSpin(QWidget *parent)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(-1.0e6, 1.0e6);
    s->setDecimals(4);
    return s;
}

} // namespace

InitialQualityDialog::InitialQualityDialog(SWMM_Engine engine,
                                           QWidget *parent)
    : QDialog(parent), m_engine(engine)
{
    setWindowTitle(tr("Initial Quality"));
    setObjectName(QStringLiteral("initialQualityDialog"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    buildUi();
    readFromEngine();
}

void InitialQualityDialog::buildUi()
{
    auto *vlay = new QVBoxLayout(this);

    m_hintLabel = new QLabel(
        tr("Initial concentrations for individual nodes and links, applied "
           "at the start of the run over the global initial concentration "
           "from the pollutant editor. Water age is in <b>hours</b> "
           "(negative extracts age), temperature in <b>°C</b>. A value on "
           "a dry element takes effect when the element wets."),
        this);
    m_hintLabel->setObjectName(QStringLiteral("iq_hint"));
    m_hintLabel->setWordWrap(true);
    vlay->addWidget(m_hintLabel);

    m_table = new QTableWidget(0, 4, this);
    m_table->setObjectName(QStringLiteral("iq_table"));
    m_table->setHorizontalHeaderLabels(
        { tr("Scope"), tr("Element"), tr("Constituent"), tr("Value") });
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    vlay->addWidget(m_table);

    auto *btnRow = new QHBoxLayout;
    auto *addBtn = new QPushButton(tr("&Add"), this);
    addBtn->setObjectName(QStringLiteral("iq_addBtn"));
    auto *remBtn = new QPushButton(tr("&Remove"), this);
    remBtn->setObjectName(QStringLiteral("iq_removeBtn"));
    btnRow->addWidget(addBtn);
    btnRow->addWidget(remBtn);
    btnRow->addStretch();
    vlay->addLayout(btnRow);
    connect(addBtn, &QPushButton::clicked,
            this, &InitialQualityDialog::onAddRow);
    connect(remBtn, &QPushButton::clicked,
            this, &InitialQualityDialog::onRemoveRow);

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    vlay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted,
            this, &InitialQualityDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void InitialQualityDialog::setElementScope(int isLink, const QString &elementName)
{
    if (!m_engine || elementName.isEmpty()) return;
    const QByteArray nameUtf8 = elementName.toUtf8();
    const int idx = isLink ? swmm_link_index(m_engine, nameUtf8.constData())
                           : swmm_node_index(m_engine, nameUtf8.constData());
    if (idx < 0) return;

    m_scopeIsLink  = isLink ? 1 : 0;
    m_scopeElemIdx = idx;

    setWindowTitle(tr("Initial Quality — %1 %2")
                       .arg(isLink ? tr("Link") : tr("Node"), elementName));
    m_hintLabel->setText(
        tr("Initial concentrations for %1 <b>%2</b>, applied at the start "
           "of the run over the global initial concentration from the "
           "pollutant editor. Water age is in <b>hours</b> (negative "
           "extracts age), temperature in <b>°C</b>. A value on a dry "
           "element takes effect when the element wets.")
            .arg(isLink ? tr("link") : tr("node"), elementName));
    m_table->setColumnHidden(kColScope, true);
    m_table->setColumnHidden(kColElement, true);

    readFromEngine();
}

void InitialQualityDialog::populateElementCombo(int row)
{
    auto *scopeCombo = qobject_cast<QComboBox *>(
        m_table->cellWidget(row, kColScope));
    auto *elemCombo = qobject_cast<QComboBox *>(
        m_table->cellWidget(row, kColElement));
    if (!scopeCombo || !elemCombo || !m_engine) return;

    const bool link = scopeCombo->currentData().toInt() == 1;
    elemCombo->clear();
    const int n = link ? swmm_link_count(m_engine)
                       : swmm_node_count(m_engine);
    for (int i = 0; i < n; ++i) {
        const char *id = link ? swmm_link_id(m_engine, i)
                              : swmm_node_id(m_engine, i);
        elemCombo->addItem(id ? QString::fromUtf8(id)
                              : QStringLiteral("#%1").arg(i), i);
    }
}

void InitialQualityDialog::onAddRow()
{
    if (!m_engine) return;
    const int r = m_table->rowCount();
    m_table->insertRow(r);

    auto *scopeCombo = new QComboBox(m_table);
    scopeCombo->addItem(tr("Node"), 0);
    scopeCombo->addItem(tr("Link"), 1);
    m_table->setCellWidget(r, kColScope, scopeCombo);

    auto *elemCombo = new QComboBox(m_table);
    m_table->setCellWidget(r, kColElement, elemCombo);

    auto *consCombo = new QComboBox(m_table);
    const int np = swmm_pollutant_count(m_engine);
    for (int p = 0; p < np; ++p) {
        const char *id = swmm_pollutant_id(m_engine, p);
        if (id) consCombo->addItem(QString::fromUtf8(id),
                                   QString::fromUtf8(id));
    }
    // Reserved species, offered only while their option is on — a row for
    // an off species would be stored-but-inert (the engine warns), so the
    // editor does not invite it.
    if (optionOn(m_engine, "WATER_AGE"))
        consCombo->addItem(tr("Water age (hours)"),
                           QString::fromUtf8(kWaterAgeName));
    if (optionOn(m_engine, "HEAT_TRANSPORT"))
        consCombo->addItem(tr("Temperature (°C)"),
                           QString::fromUtf8(kTemperatureName));
    m_table->setCellWidget(r, kColConstituent, consCombo);

    auto *spin = makeValueSpin(m_table);
    m_table->setCellWidget(r, kColValue, spin);

    // Scope drives the element list; constituent drives the value floor
    // (pollutant concentrations cannot be negative; the reserved species
    // are signed).
    connect(scopeCombo, &QComboBox::currentIndexChanged, this,
            [this, r]() { populateElementCombo(r); });
    auto applyFloor = [consCombo, spin]() {
        const QString name = consCombo->currentData().toString();
        const bool reserved = name == QLatin1String(kWaterAgeName) ||
                              name == QLatin1String(kTemperatureName);
        spin->setMinimum(reserved ? -1.0e6 : 0.0);
    };
    connect(consCombo, &QComboBox::currentIndexChanged, this,
            [applyFloor]() { applyFloor(); });
    populateElementCombo(r);
    applyFloor();

    // Element-scoped mode: pin the (hidden) Scope / Element combos to the
    // scoped element so every row the user adds belongs to it.
    if (m_scopeIsLink >= 0) {
        const int si = scopeCombo->findData(m_scopeIsLink);
        if (si >= 0) scopeCombo->setCurrentIndex(si);
        populateElementCombo(r);
        const int ei = elemCombo->findData(m_scopeElemIdx);
        if (ei >= 0) elemCombo->setCurrentIndex(ei);
        scopeCombo->setEnabled(false);
        elemCombo->setEnabled(false);
    }
}

void InitialQualityDialog::onRemoveRow()
{
    const int r = m_table->currentRow();
    if (r >= 0) m_table->removeRow(r);
}

void InitialQualityDialog::readFromEngine()
{
    if (!m_engine) return;
    m_table->setRowCount(0);
    const int count = swmm_init_quality_count(m_engine);
    for (int i = 0; i < count; ++i) {
        int is_link = 0, elem = -1;
        char cons[128] = {0};
        double value = 0.0;
        if (swmm_init_quality_get(m_engine, i, &is_link, &elem,
                                  cons, sizeof(cons), &value) != SWMM_OK)
            continue;
        if (m_scopeIsLink >= 0 &&
            (is_link != m_scopeIsLink || elem != m_scopeElemIdx))
            continue;                        // element-scoped: other elements stay put
        onAddRow();                          // builds the row's widgets
        const int r = m_table->rowCount() - 1;
        if (auto *c = qobject_cast<QComboBox *>(
                m_table->cellWidget(r, kColScope))) {
            const int idx = c->findData(is_link ? 1 : 0);
            if (idx >= 0) c->setCurrentIndex(idx);
        }
        populateElementCombo(r);
        if (auto *c = qobject_cast<QComboBox *>(
                m_table->cellWidget(r, kColElement))) {
            const int idx = c->findData(elem);
            if (idx >= 0) c->setCurrentIndex(idx);
        }
        if (auto *c = qobject_cast<QComboBox *>(
                m_table->cellWidget(r, kColConstituent))) {
            const int idx = c->findData(QString::fromUtf8(cons));
            if (idx >= 0) {
                c->setCurrentIndex(idx);
            } else {
                // A saved row whose species the combo does not offer (its
                // option is off, or the pollutant is gone): keep it VISIBLE
                // and editable rather than silently dropping it on OK.
                c->addItem(QString::fromUtf8(cons), QString::fromUtf8(cons));
                c->setCurrentIndex(c->count() - 1);
            }
        }
        if (auto *s = qobject_cast<QDoubleSpinBox *>(
                m_table->cellWidget(r, kColValue)))
            s->setValue(value);
    }
}

int InitialQualityDialog::writeToEngine()
{
    if (!m_engine) return 0;
    int writes = 0;

    struct Key {
        int is_link; int elem; QString cons;
        bool operator==(const Key &o) const {
            return is_link == o.is_link && elem == o.elem && cons == o.cons;
        }
    };

    // Snapshot the engine rows (key + value + entry index). In element-
    // scoped mode only the scoped element's rows enter the diff, so rows
    // belonging to other elements can never be removed by this dialog.
    struct EngineRow { Key key; double value; int entry; };
    QVector<EngineRow> engineRows;
    const int count = swmm_init_quality_count(m_engine);
    for (int i = 0; i < count; ++i) {
        int is_link = 0, elem = -1;
        char cons[128] = {0};
        double value = 0.0;
        if (swmm_init_quality_get(m_engine, i, &is_link, &elem,
                                  cons, sizeof(cons), &value) != SWMM_OK)
            continue;
        if (m_scopeIsLink >= 0 &&
            (is_link != m_scopeIsLink || elem != m_scopeElemIdx))
            continue;
        engineRows.append(
            { { is_link, elem, QString::fromUtf8(cons) }, value, i });
    }

    // Collect the table rows.
    QVector<EngineRow> tableRows;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto *sc = qobject_cast<QComboBox *>(
            m_table->cellWidget(r, kColScope));
        auto *ec = qobject_cast<QComboBox *>(
            m_table->cellWidget(r, kColElement));
        auto *cc = qobject_cast<QComboBox *>(
            m_table->cellWidget(r, kColConstituent));
        auto *vs = qobject_cast<QDoubleSpinBox *>(
            m_table->cellWidget(r, kColValue));
        if (!sc || !ec || !cc || !vs || ec->currentIndex() < 0) continue;
        tableRows.append({ { sc->currentData().toInt(),
                             ec->currentData().toInt(),
                             cc->currentData().toString() },
                           vs->value() });
    }

    // Remove engine rows the table no longer carries — by DESCENDING entry
    // index, since remove shifts subsequent entries down.
    for (int i = engineRows.size() - 1; i >= 0; --i) {
        bool kept = false;
        for (const auto &t : tableRows)
            if (t.key == engineRows[i].key) { kept = true; break; }
        if (!kept &&
            swmm_init_quality_remove(m_engine, engineRows[i].entry) == SWMM_OK)
            ++writes;
    }

    // Upsert every table row whose value differs (or is new) — set is
    // keyed, so one call per row suffices; untouched rows write nothing
    // (the writeIfChanged discipline).
    for (const auto &t : tableRows) {
        bool same = false;
        for (const auto &e : engineRows)
            if (e.key == t.key &&
                qFuzzyCompare(1.0 + e.value, 1.0 + t.value)) {
                same = true;
                break;
            }
        if (same) continue;
        if (swmm_init_quality_set(m_engine, t.key.is_link, t.key.elem,
                                  t.key.cons.toUtf8().constData(),
                                  t.value) == SWMM_OK)
            ++writes;
    }

    return writes;
}

void InitialQualityDialog::onAccept()
{
    m_lastWriteCount  = writeToEngine();
    m_wroteAnyChanges = m_lastWriteCount > 0;
    accept();
}

} // namespace OpenSWMMVis
