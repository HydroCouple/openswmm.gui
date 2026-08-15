/*!
 * \file   userflagsdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  UserFlagsDialog implementation — see userflagsdialog.h.
 */
#include "ui/dialogs/userflagsdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

using openswmmvis::ui::UserFlagsModel;
using FlagType = UserFlagsModel::FlagType;

namespace {

enum Column { ColName = 0, ColType = 1, ColDescription = 2, ColCount = 3 };

QComboBox *makeTypeCombo(FlagType current)
{
    auto *combo = new QComboBox;
    combo->addItem(UserFlagsModel::typeLabel(FlagType::Boolean),
                   static_cast<int>(FlagType::Boolean));
    combo->addItem(UserFlagsModel::typeLabel(FlagType::Integer),
                   static_cast<int>(FlagType::Integer));
    combo->addItem(UserFlagsModel::typeLabel(FlagType::Real),
                   static_cast<int>(FlagType::Real));
    combo->addItem(UserFlagsModel::typeLabel(FlagType::String),
                   static_cast<int>(FlagType::String));
    combo->setCurrentIndex(combo->findData(static_cast<int>(current)));
    return combo;
}

} // namespace

UserFlagsDialog::UserFlagsDialog(UserFlagsModel *model, QWidget *parent)
    : QDialog(parent)
    , m_model(model)
{
    setWindowTitle(tr("User Flags"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("UserFlagsDialog"));
    resize(560, 380);

    auto *intro = new QLabel(
        tr("User flags attach typed metadata to model objects (nodes, links, "
           "subcatchments, …). Define the available flags here; assign "
           "per-object values in the Attribute Table or the Attributes "
           "panel."));
    intro->setWordWrap(true);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels(
        {tr("Name"), tr("Type"), tr("Description")});
    // User-resizable columns; Description absorbs leftover width but can
    // still be dragged since only the last section stretches.
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(ColName, 160);
    m_table->setColumnWidth(ColType, 90);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);

    m_addButton    = new QPushButton(tr("Add"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    connect(m_addButton,    &QPushButton::clicked, this, &UserFlagsDialog::onAddClicked);
    connect(m_removeButton, &QPushButton::clicked, this, &UserFlagsDialog::onRemoveClicked);

    auto *sideButtons = new QVBoxLayout;
    sideButtons->addWidget(m_addButton);
    sideButtons->addWidget(m_removeButton);
    sideButtons->addStretch(1);

    auto *tableRow = new QHBoxLayout;
    tableRow->addWidget(m_table, 1);
    tableRow->addLayout(sideButtons);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok |
                                       QDialogButtonBox::Cancel |
                                       QDialogButtonBox::Apply, this);
    connect(m_buttonBox, &QDialogButtonBox::accepted,
            this, &UserFlagsDialog::onAccepted);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &UserFlagsDialog::onApplyClicked);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(tableRow, 1);
    layout->addWidget(m_buttonBox);

    reloadFromModel();
}

void UserFlagsDialog::reloadFromModel()
{
    m_table->setRowCount(0);
    if (!m_model) return;
    for (const auto &def : m_model->defs())
        addRow(def.name, def.type, def.description);
}

void UserFlagsDialog::addRow(const QString &name, FlagType type,
                             const QString &description)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, ColName, new QTableWidgetItem(name));
    m_table->setCellWidget(row, ColType, makeTypeCombo(type));
    m_table->setItem(row, ColDescription, new QTableWidgetItem(description));
}

UserFlagsModel::FlagType UserFlagsDialog::rowType(int row) const
{
    const auto *combo =
        qobject_cast<QComboBox *>(m_table->cellWidget(row, ColType));
    return combo ? static_cast<FlagType>(combo->currentData().toInt())
                 : FlagType::String;
}

void UserFlagsDialog::onAddClicked()
{
    addRow(QString(), FlagType::String, QString());
    const int row = m_table->rowCount() - 1;
    m_table->setCurrentCell(row, ColName);
    m_table->editItem(m_table->item(row, ColName));
}

void UserFlagsDialog::onRemoveClicked()
{
    const int row = m_table->currentRow();
    if (row < 0) return;

    const QString name =
        m_table->item(row, ColName)
            ? m_table->item(row, ColName)->text().trimmed().toUpper()
            : QString();
    // Removing a flag that exists in the engine also removes every value
    // assigned to objects — confirm before staging the removal.
    if (m_confirmationsEnabled && m_model && !name.isEmpty()
        && m_model->isDefined(name)) {
        const auto answer = QMessageBox::question(
            this, tr("Remove User Flag"),
            tr("Removing the flag \"%1\" also removes every value assigned "
               "to objects. Remove it?").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }
    m_table->removeRow(row);
}

bool UserFlagsDialog::applyChanges(QString *outError)
{
    if (!m_model) {
        if (outError) *outError = tr("No project is open.");
        return false;
    }

    // ---- Collect + validate the staged rows -------------------------------
    struct Row { QString name; FlagType type; QString description; };
    QVector<Row> staged;
    staged.reserve(m_table->rowCount());
    QSet<QString> seen;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        Row row;
        row.name = m_table->item(r, ColName)
                       ? m_table->item(r, ColName)->text().trimmed().toUpper()
                       : QString();
        row.type = rowType(r);
        row.description = m_table->item(r, ColDescription)
                              ? m_table->item(r, ColDescription)->text().trimmed()
                              : QString();
        if (row.name.isEmpty()) {
            if (outError)
                *outError = tr("Row %1 has an empty flag name.").arg(r + 1);
            return false;
        }
        if (seen.contains(row.name)) {
            if (outError)
                *outError = tr("The flag name \"%1\" appears more than once. "
                               "Flag names are case-insensitive and must be "
                               "unique.").arg(row.name);
            return false;
        }
        seen.insert(row.name);
        staged.append(row);
    }

    // ---- Diff against the engine's current definitions --------------------
    const QVector<UserFlagsModel::Def> before = m_model->defs();

    // Type changes drop the flag's values (undefine + redefine) because the
    // previously stored values no longer match the declared type. Confirm
    // the full list once.
    QStringList typeChanged;
    for (const auto &row : staged)
        for (const auto &def : before)
            if (def.name == row.name && def.type != row.type)
                typeChanged << row.name;
    if (!typeChanged.isEmpty() && m_confirmationsEnabled) {
        const auto answer = QMessageBox::question(
            this, tr("Change Flag Type"),
            tr("Changing the type of %1 clears every value currently "
               "assigned for the flag(s). Continue?")
                .arg(typeChanged.join(QStringLiteral(", "))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            if (outError) *outError = tr("Type change cancelled.");
            return false;
        }
    }

    bool wrote = false;

    // Removed definitions (cascade-deletes their values engine-side).
    for (const auto &def : before) {
        if (!seen.contains(def.name)) {
            if (m_model->undefine(def.name))
                wrote = true;
        }
    }

    // New / changed definitions. A redefine with the same type keeps
    // existing values (description-only edits are non-destructive); a type
    // change routes through undefine to clear now-mismatched values.
    for (const auto &row : staged) {
        const UserFlagsModel::Def *existing = nullptr;
        for (const auto &def : before)
            if (def.name == row.name) { existing = &def; break; }

        if (existing && existing->type == row.type
            && existing->description == row.description)
            continue;  // unchanged

        if (existing && existing->type != row.type)
            m_model->undefine(row.name);

        QString err;
        if (!m_model->define(row.name, row.type, row.description, &err)) {
            if (outError) *outError = err;
            return false;
        }
        wrote = true;
    }

    m_wroteAnyChanges = m_wroteAnyChanges || wrote;
    reloadFromModel();
    return true;
}

void UserFlagsDialog::onApplyClicked()
{
    QString err;
    if (!applyChanges(&err) && m_confirmationsEnabled)
        QMessageBox::warning(this, tr("User Flags"), err);
}

void UserFlagsDialog::onAccepted()
{
    QString err;
    if (!applyChanges(&err)) {
        if (m_confirmationsEnabled)
            QMessageBox::warning(this, tr("User Flags"), err);
        return;  // keep the dialog open so the user can fix the row
    }
    accept();
}
