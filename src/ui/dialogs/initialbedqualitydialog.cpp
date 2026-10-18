/* SPDX-License-Identifier: Apache-2.0 */
#include "ui/dialogs/initialbedqualitydialog.h"

#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_heat.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_reactions.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QByteArray>
#include <QDoubleSpinBox>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace OpenSWMMVis {
namespace {

constexpr int ScopeColumn = 0;
constexpr int TargetColumn = 1;
constexpr int SpeciesColumn = 2;
constexpr int ValueColumn = 3;

struct Row {
    int scope = SWMM_2D_QUAL_SCOPE_GLOBAL;
    QString target;
    QString species;
    double concentration = 0.0;
};

bool editable(SWMM_Engine engine) {
    int state = SWMM_STATE_NONE;
    return engine && swmm_engine_get_state(engine, &state) == SWMM_OK &&
        (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}

bool optionOn(SWMM_Engine engine, const char* key) {
    char value[32]{};
    return engine && swmm_options_get(engine, key, value, sizeof(value)) == SWMM_OK &&
        std::strcmp(value, "YES") == 0;
}

QString keyFor(const Row& row) {
    return QStringLiteral("%1|%2|%3")
        .arg(row.scope).arg(row.scope == SWMM_2D_QUAL_SCOPE_GLOBAL ? QString() : row.target,
                            row.species.toCaseFolded());
}

QString comboText(QTableWidget* table, int row, int column) {
    auto* combo = qobject_cast<QComboBox*>(table->cellWidget(row, column));
    return combo ? (combo->currentData().isValid()
        ? combo->currentData().toString() : combo->currentText()) : QString();
}

int comboScope(QTableWidget* table, int row) {
    auto* combo = qobject_cast<QComboBox*>(table->cellWidget(row, ScopeColumn));
    return combo ? combo->currentData().toInt() : -1;
}

Row readRow(QTableWidget* table, int row) {
    Row result;
    result.scope = comboScope(table, row);
    result.target = comboText(table, row, TargetColumn);
    result.species = comboText(table, row, SpeciesColumn);
    if (auto* value = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, ValueColumn)))
        result.concentration = value->value();
    return result;
}

} // namespace

InitialBedQualityDialog::InitialBedQualityDialog(SWMM_Engine engine, QWidget* parent)
    : QDialog(parent), engine_(engine) {
    setWindowTitle(tr("Initial Mesh-Bed Quality"));
    setObjectName(QStringLiteral("initialBedQualityDialog"));
    buildUi();
    readFromEngine();
    setEditable(editable(engine_));
}

void InitialBedQualityDialog::invalidateEngine() {
    engine_ = nullptr;
    setEditable(false);
    if (status_) status_->setText(tr("The model closed. Reopen it to edit bed initial quality."));
}

void InitialBedQualityDialog::buildUi() {
    auto* layout = new QVBoxLayout(this);
    auto* help = new QLabel(tr(
        "Seed pollutant or water-age concentration in the finite local mesh bed. "
        "Values use each species’ units; water age is entered in hours. "
        "Rows apply to all cells, a mesh-region tag, or one cell. More specific "
        "rows override broader rows. These values need coupled heat and "
        "BED_HEAT_MODEL LOCAL_STORAGE; groundwater-owned porewater uses the "
        "groundwater initial-quality editor."), this);
    help->setObjectName(QStringLiteral("ibq_help"));
    help->setWordWrap(true);
    layout->addWidget(help);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("ibq_status"));
    status_->setWordWrap(true);
    layout->addWidget(status_);

    table_ = new QTableWidget(0, 4, this);
    table_->setObjectName(QStringLiteral("ibq_table"));
    table_->setAccessibleName(tr("Initial concentration in mesh-bed porewater"));
    table_->setHorizontalHeaderLabels({tr("Scope"), tr("Mesh target"),
                                       tr("Species"), tr("Initial concentration")});
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_);

    auto* rowButtons = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add row"), this);
    add->setObjectName(QStringLiteral("ibq_add"));
    add->setAutoDefault(false);
    remove_ = new QPushButton(tr("Remove selected"), this);
    remove_->setObjectName(QStringLiteral("ibq_remove"));
    remove_->setEnabled(false);
    rowButtons->addWidget(add);
    rowButtons->addWidget(remove_);
    rowButtons->addStretch(1);
    layout->addLayout(rowButtons);
    connect(add, &QPushButton::clicked, this, &InitialBedQualityDialog::addRow);
    connect(remove_, &QPushButton::clicked, this, &InitialBedQualityDialog::removeRow);
    connect(table_, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { remove_->setEnabled(engine_ && row >= 0); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("ibq_ok"));
    connect(buttons, &QDialogButtonBox::accepted, this, &InitialBedQualityDialog::acceptDraft);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(760, 430);
}

void InitialBedQualityDialog::readFromEngine() {
    if (!engine_) return;
    table_->setRowCount(0);
    int count = 0;
    if (swmm_2d_initial_bed_quality_count(engine_, &count) != SWMM_OK) return;
    for (int i = 0; i < count; ++i) {
        int scope = SWMM_2D_QUAL_SCOPE_GLOBAL, cell = -1;
        char target[256]{}, species[128]{};
        double value = 0.0;
        if (swmm_2d_get_initial_bed_quality(engine_, i, &scope, target, sizeof target,
                                            &cell, species, sizeof species, &value) != SWMM_OK)
            continue;
        Row row;
        row.scope = scope;
        row.target = scope == SWMM_2D_QUAL_SCOPE_CELL
            ? QString::number(cell) : QString::fromUtf8(target);
        row.species = QString::fromUtf8(species);
        row.concentration = value;
        appendRow(row.scope, row.target, row.species, row.concentration);
    }
    char heat[64]{}, bed[64]{};
    const bool configured = optionOn(engine_, "HEAT_TRANSPORT") &&
        swmm_options_get_ext(engine_, "HEAT_MODEL", heat, sizeof heat) == SWMM_OK &&
        swmm_options_get_ext(engine_, "BED_HEAT_MODEL", bed, sizeof bed) == SWMM_OK &&
        std::strcmp(heat, "COUPLED") == 0 && std::strcmp(bed, "LOCAL_STORAGE") == 0;
    status_->setText(configured
        ? tr("Local bed heat is configured. Initial concentrations will seed bed porewater at model start.")
        : tr("These rows are saved but inactive until temperature transport is enabled with HEAT_MODEL COUPLED and BED_HEAT_MODEL LOCAL_STORAGE."));
}

void InitialBedQualityDialog::addRow() {
    appendRow(SWMM_2D_QUAL_SCOPE_GLOBAL, {}, {}, 0.0);
}

void InitialBedQualityDialog::appendRow(int scopeValue, const QString& initialTarget,
                                        const QString& initialSpecies, double concentration) {
    Row row{scopeValue, initialTarget, initialSpecies, concentration};
    const int r = table_->rowCount();
    table_->insertRow(r);

    auto* scope = new QComboBox(table_);
    scope->setObjectName(QStringLiteral("ibq_scope_%1").arg(r));
    scope->addItem(tr("All cells"), SWMM_2D_QUAL_SCOPE_GLOBAL);
    scope->addItem(tr("Mesh tag"), SWMM_2D_QUAL_SCOPE_TAG);
    scope->addItem(tr("Cell"), SWMM_2D_QUAL_SCOPE_CELL);
    int scopeIndex = scope->findData(row.scope);
    if (scopeIndex >= 0) scope->setCurrentIndex(scopeIndex);
    table_->setCellWidget(r, ScopeColumn, scope);

    auto* target = new QComboBox(table_);
    target->setObjectName(QStringLiteral("ibq_target_%1").arg(r));
    target->setProperty("ibqScope", row.scope);
    auto populateTarget = [this, target, row](int selectedScope) {
        const int currentScope = target->property("ibqScope").toInt();
        const QString previous = selectedScope == currentScope
            ? (target->currentData().isValid() ? target->currentData().toString() : target->currentText())
            : (selectedScope == row.scope ? row.target : QString());
        target->clear();
        if (selectedScope == SWMM_2D_QUAL_SCOPE_CELL) {
            int cells = 0;
            if (swmm_2d_cell_count(engine_, &cells) == SWMM_OK)
                for (int c = 0; c < cells; ++c)
                    target->addItem(tr("Cell %1").arg(c + 1), QString::number(c));
            target->setEditable(false);
            bool ok = false;
            const int cell = previous.toInt(&ok);
            if (ok) {
                const int idx = target->findData(QString::number(cell));
                if (idx >= 0) target->setCurrentIndex(idx);
            }
        } else if (selectedScope == SWMM_2D_QUAL_SCOPE_TAG) {
            int triangles = 0;
            QSet<QString> tags;
            if (swmm_2d_triangle_count(engine_, &triangles) == SWMM_OK) {
                for (int c = 0; c < triangles; ++c) {
                    char tag[256]{};
                    if (swmm_2d_get_triangle_tag(engine_, c, tag, sizeof tag) == SWMM_OK && tag[0])
                        tags.insert(QString::fromUtf8(tag));
                }
            }
            QStringList ordered = tags.values();
            std::sort(ordered.begin(), ordered.end(), [](const QString& a, const QString& b) {
                return a.compare(b, Qt::CaseInsensitive) < 0;
            });
            target->addItems(ordered);
            target->setEditable(true);
            target->setInsertPolicy(QComboBox::NoInsert);
            target->setCurrentText(previous);
            target->setToolTip(tr("Choose a tag from the mesh or type an exact mesh-region tag."));
        } else {
            target->addItem(tr("Every mesh cell"), QString());
            target->setEditable(false);
            target->setCurrentIndex(0);
        }
        target->setProperty("ibqScope", selectedScope);
    };
    // Retain existing targets when rebuilding after scope changes.
    target->setProperty("initialTarget", row.target);
    populateTarget(row.scope);
    if (!row.target.isEmpty()) {
        if (row.scope == SWMM_2D_QUAL_SCOPE_CELL) {
            bool ok = false;
            const int cell = row.target.toInt(&ok);
            const int idx = ok ? target->findData(QString::number(cell)) : -1;
            if (idx >= 0) target->setCurrentIndex(idx);
        } else {
            target->setCurrentText(row.target);
        }
    }
    table_->setCellWidget(r, TargetColumn, target);
    connect(scope, &QComboBox::currentIndexChanged, this,
            [scope, populateTarget](int index) { Q_UNUSED(index); populateTarget(scope->currentData().toInt()); });

    auto* species = new QComboBox(table_);
    species->setObjectName(QStringLiteral("ibq_species_%1").arg(r));
    const int pollutants = swmm_pollutant_count(engine_);
    for (int p = 0; p < pollutants; ++p) {
        const char* id = swmm_pollutant_id(engine_, p);
        if (id) species->addItem(QString::fromUtf8(id), QString::fromUtf8(id));
    }
    const int reactionSpecies = swmm_reaction_species_count(engine_);
    for (int i = 0; i < reactionSpecies; ++i) {
        char name[128]{}, units[32]{};
        int wall = 0;
        double atol = 0.0, rtol = 0.0;
        if (swmm_reaction_species_get(engine_, i, name, sizeof name, &wall,
                                      units, sizeof units, &atol, &rtol) != SWMM_OK || wall)
            continue;
        const QString value = QString::fromUtf8(name);
        if (!value.isEmpty() && species->findData(value) < 0)
            species->addItem(units[0] ? tr("%1 (%2)").arg(value, QString::fromUtf8(units)) : value,
                             value);
    }
    if (optionOn(engine_, "WATER_AGE"))
        species->addItem(tr("Water age (hours)"), QStringLiteral("__WATER_AGE__"));
    if (!row.species.isEmpty() && species->findData(row.species) < 0)
        species->addItem(row.species, row.species);
    if (!row.species.isEmpty()) {
        const int idx = species->findData(row.species);
        if (idx >= 0) species->setCurrentIndex(idx);
    }
    table_->setCellWidget(r, SpeciesColumn, species);

    auto* value = new QDoubleSpinBox(table_);
    value->setObjectName(QStringLiteral("ibq_value_%1").arg(r));
    value->setRange(0.0, 1.0e12);
    value->setDecimals(10);
    value->setValue(row.concentration);
    value->setAccessibleName(tr("Initial bed concentration row %1").arg(r + 1));
    table_->setCellWidget(r, ValueColumn, value);
    connect(table_, &QTableWidget::currentCellChanged, this,
            [this](int current, int, int, int) { remove_->setEnabled(engine_ && current >= 0); });
}

void InitialBedQualityDialog::removeRow() {
    const int row = table_->currentRow();
    if (row >= 0) table_->removeRow(row);
}

void InitialBedQualityDialog::setEditable(bool canEdit) {
    if (table_) table_->setEnabled(canEdit);
    if (remove_) remove_->setEnabled(canEdit && table_ && table_->currentRow() >= 0);
    if (auto* add = findChild<QPushButton*>(QStringLiteral("ibq_add"))) add->setEnabled(canEdit);
    if (auto* buttons = findChild<QDialogButtonBox*>()) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(canEdit);
        buttons->button(QDialogButtonBox::Cancel)->setText(canEdit ? tr("Cancel") : tr("Close"));
    }
}

void InitialBedQualityDialog::acceptDraft() {
    if (!editable(engine_)) {
        QMessageBox::warning(this, tr("Initial Mesh-Bed Quality"),
                             tr("The model is no longer editable. Reset the simulation before changing initial conditions."));
        return;
    }
    QVector<Row> desired;
    QSet<QString> keys;
    for (int r = 0; r < table_->rowCount(); ++r) {
        const Row row = readRow(table_, r);
        if (row.species.isEmpty() || !std::isfinite(row.concentration) || row.concentration < 0.0 ||
            (row.scope == SWMM_2D_QUAL_SCOPE_TAG && row.target.trimmed().isEmpty()) ||
            (row.scope == SWMM_2D_QUAL_SCOPE_CELL && row.target.toInt() < 0)) {
            QMessageBox::warning(this, tr("Initial Mesh-Bed Quality"),
                                 tr("Complete every species, target and non-negative concentration before applying."));
            return;
        }
        const QString key = keyFor(row);
        if (keys.contains(key)) {
            QMessageBox::warning(this, tr("Initial Mesh-Bed Quality"),
                                 tr("Two rows address the same scope, target and species."));
            return;
        }
        keys.insert(key);
        desired.push_back(row);
    }

    QVector<Row> original;
    int count = 0;
    if (swmm_2d_initial_bed_quality_count(engine_, &count) != SWMM_OK) return;
    for (int i = 0; i < count; ++i) {
        int scope = SWMM_2D_QUAL_SCOPE_GLOBAL, cell = -1;
        char target[256]{}, species[128]{};
        double value = 0.0;
        if (swmm_2d_get_initial_bed_quality(engine_, i, &scope, target, sizeof target,
                                            &cell, species, sizeof species, &value) != SWMM_OK)
            return;
        original.push_back({scope, scope == SWMM_2D_QUAL_SCOPE_CELL
            ? QString::number(cell) : QString::fromUtf8(target), QString::fromUtf8(species), value});
    }

    QHash<QString, double> oldValues;
    for (const Row& row : original) oldValues.insert(keyFor(row), row.concentration);
    bool same = oldValues.size() == desired.size();
    if (same)
        for (const Row& row : desired)
            if (!oldValues.contains(keyFor(row)) || oldValues.value(keyFor(row)) != row.concentration) {
                same = false;
                break;
            }
    last_write_count_ = 0;
    if (!same) {
        SWMM_HeatSnapshot snapshot = swmm_heat_snapshot_create(engine_);
        if (!snapshot) {
            QMessageBox::warning(this, tr("Initial Mesh-Bed Quality"), tr("Could not protect the current model settings."));
            return;
        }
        auto restore = [this, snapshot]() { swmm_heat_snapshot_restore(engine_, snapshot); };
        bool failed = false;
        // Remove existing rows first, in descending index order, then write
        // the complete validated draft so key changes remain atomic.
        for (int i = original.size() - 1; i >= 0; --i) {
            if (keys.contains(keyFor(original[i]))) continue;
            if (swmm_2d_remove_initial_bed_quality(engine_, i) != SWMM_OK) { failed = true; break; }
            ++last_write_count_;
        }
        if (!failed) {
            for (const Row& row : desired) {
                const int cell = row.scope == SWMM_2D_QUAL_SCOPE_CELL
                    ? row.target.toInt() : -1;
                const QByteArray species = row.species.toUtf8();
                const QByteArray tagBytes = row.target.toUtf8();
                const char* safeTag = row.scope == SWMM_2D_QUAL_SCOPE_TAG ? tagBytes.constData() : nullptr;
                if (swmm_2d_set_initial_bed_quality(engine_, row.scope, safeTag, cell,
                        species.constData(), row.concentration) != SWMM_OK) { failed = true; break; }
                ++last_write_count_;
            }
        }
        if (failed) {
            restore();
            swmm_heat_snapshot_destroy(snapshot);
            status_->setText(tr("The edit could not be applied. The previous settings were restored; review the rows and try again."));
            return;
        }
        swmm_heat_snapshot_destroy(snapshot);
        wrote_any_changes_ = last_write_count_ > 0;
        if (wrote_any_changes_) emit changesApplied();
    }
    accept();
}

} // namespace OpenSWMMVis
