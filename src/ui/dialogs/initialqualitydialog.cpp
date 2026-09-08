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
#include <openswmm/engine/openswmm_reactions.h>   // U2: MSX species constituents

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QFile>
#include <QRegularExpression>
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

    // U2 — the `[INITIAL_QUALITY] FILE <csv>` sidecar. Rows loaded from it
    // are read-only here (the file is their source); the reference itself
    // is editable and the Import button copies a CSV's rows in as ordinary
    // inline rows instead, for a one-off bulk edit.
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(new QLabel(tr("CSV file:"), this));
    m_fileEdit = new QLineEdit(this);
    m_fileEdit->setObjectName(QStringLiteral("iq_fileEdit"));
    m_fileEdit->setPlaceholderText(tr("none — rows are stored in the model file"));
    m_fileEdit->setToolTip(
        tr("[INITIAL_QUALITY] FILE — a CSV of scope,element,constituent,value "
           "read at every open (relative to the model file). Its rows are "
           "shown greyed below and are edited in the file itself."));
    fileRow->addWidget(m_fileEdit, 1);
    auto *browseBtn = new QPushButton(tr("&Browse…"), this);
    browseBtn->setObjectName(QStringLiteral("iq_fileBrowseBtn"));
    fileRow->addWidget(browseBtn);
    auto *importBtn = new QPushButton(tr("&Import rows…"), this);
    importBtn->setObjectName(QStringLiteral("iq_importBtn"));
    importBtn->setToolTip(
        tr("Read a CSV's rows into the table as ordinary rows stored in the "
           "model file (no lasting reference to the CSV)."));
    fileRow->addWidget(importBtn);
    vlay->addLayout(fileRow);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(
            this, tr("Initial quality CSV"), m_fileEdit->text(),
            tr("CSV files (*.csv *.txt);;All files (*)"));
        if (!f.isEmpty()) m_fileEdit->setText(f);
    });
    connect(importBtn, &QPushButton::clicked,
            this, &InitialQualityDialog::onImportCsv);

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
    // U2 (2026-09-07): reactions-component species are constituents here on
    // the same footing as pollutants — [INITIAL_QUALITY] is the canonical
    // home for a per-element initial value of ANY species, and the engine
    // mirrors the row into the reactions seed table every engine reads.
    // WALL species are offered too: their initial value is a legal wall
    // concentration even though inflow carries none.
    const int nsp = swmm_reaction_species_count(m_engine);
    for (int m = 0; m < nsp; ++m) {
        char name[128] = {0}, units[32] = {0};
        int isWall = 0;
        double atol = 0.0, rtol = 0.0;
        if (swmm_reaction_species_get(m_engine, m, name, sizeof(name), &isWall,
                                      units, sizeof(units), &atol, &rtol) != SWMM_OK)
            continue;
        if (!name[0]) continue;
        const QString nm = QString::fromUtf8(name);
        if (consCombo->findData(nm) >= 0) continue;
        consCombo->addItem(units[0] ? tr("%1 (%2)").arg(nm, QString::fromUtf8(units))
                                    : nm,
                           nm);
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
    m_fileRows.clear();
    {
        char buf[1024] = {0};
        if (swmm_init_quality_file_get(m_engine, buf, sizeof(buf)) == SWMM_OK)
            m_fileEdit->setText(QString::fromUtf8(buf));
    }
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
        // U2: a row that came from the FILE sidecar is displayed but not
        // editable here — the CSV is its source, and writeToEngine skips it.
        if (swmm_init_quality_is_file(m_engine, i)) {
            for (int c = 0; c < m_table->columnCount(); ++c)
                if (QWidget *w = m_table->cellWidget(r, c)) {
                    w->setEnabled(false);
                    w->setToolTip(tr("From the CSV file — edit it there."));
                }
            m_fileRows.insert(r);
        }
    }
}

void InitialQualityDialog::onImportCsv()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import initial quality rows"), QString(),
        tr("CSV files (*.csv *.txt);;All files (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_hintLabel->setText(tr("Could not read %1.").arg(QFileInfo(path).fileName()));
        return;
    }
    int added = 0, skipped = 0, lineno = 0;
    while (!f.atEnd()) {
        ++lineno;
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';'))) continue;
        const QStringList tok =
            line.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
        if (tok.size() < 4) { ++skipped; continue; }
        bool okNum = false;
        const double v = tok[3].toDouble(&okNum);
        if (!okNum) { if (lineno > 1) ++skipped; continue; }   // header
        const QString scope = tok[0].toUpper();
        if (scope != QLatin1String("NODE") && scope != QLatin1String("LINK")) {
            ++skipped;
            continue;
        }
        const bool link = scope == QLatin1String("LINK");
        onAddRow();
        const int r = m_table->rowCount() - 1;
        if (auto *c = qobject_cast<QComboBox *>(m_table->cellWidget(r, kColScope))) {
            const int i = c->findData(link ? 1 : 0);
            if (i >= 0) c->setCurrentIndex(i);
        }
        populateElementCombo(r);
        bool resolved = false;
        if (auto *c = qobject_cast<QComboBox *>(m_table->cellWidget(r, kColElement))) {
            const int i = c->findText(tok[1], Qt::MatchFixedString);
            if (i >= 0) { c->setCurrentIndex(i); resolved = true; }
        }
        if (auto *c = qobject_cast<QComboBox *>(m_table->cellWidget(r, kColConstituent))) {
            const int i = c->findData(tok[2]);
            if (i >= 0) c->setCurrentIndex(i);
            else { c->addItem(tok[2], tok[2]); c->setCurrentIndex(c->count() - 1); }
        }
        if (auto *s = qobject_cast<QDoubleSpinBox *>(m_table->cellWidget(r, kColValue)))
            s->setValue(v);
        if (!resolved) { m_table->removeRow(r); ++skipped; continue; }
        ++added;
    }
    m_hintLabel->setText(
        skipped > 0
            ? tr("Imported %1 row(s) from %2; %3 line(s) skipped (unknown "
                 "element or malformed).")
                  .arg(added).arg(QFileInfo(path).fileName()).arg(skipped)
            : tr("Imported %1 row(s) from %2.")
                  .arg(added).arg(QFileInfo(path).fileName()));
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
        if (swmm_init_quality_is_file(m_engine, i)) continue;   // U2: the file's row
        engineRows.append(
            { { is_link, elem, QString::fromUtf8(cons) }, value, i });
    }

    // U2: the FILE reference itself.
    {
        char buf[1024] = {0};
        swmm_init_quality_file_get(m_engine, buf, sizeof(buf));
        const QString cur = QString::fromUtf8(buf);
        const QString nv  = m_fileEdit->text().trimmed();
        if (cur != nv &&
            swmm_init_quality_file_set(m_engine, nv.toUtf8().constData()) == SWMM_OK)
            ++writes;
    }

    // Collect the table rows. Rows loaded from the FILE sidecar are the
    // file's, not the dialog's: they are neither re-written nor removed.
    QVector<EngineRow> tableRows;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (m_fileRows.contains(r)) continue;
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
