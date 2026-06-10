/*!
 * \file   userflagvaluesdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  UserFlagValuesDialog implementation — see userflagvaluesdialog.h.
 */
#include "ui/dialogs/userflagvaluesdialog.h"

#include "ui/models/userflagsmodel.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QTableWidget>
#include <QVBoxLayout>

using openswmmvis::ui::UserFlagsModel;
using FlagType = UserFlagsModel::FlagType;

namespace {

enum Column { ColFlag = 0, ColType = 1, ColValue = 2, ColCount = 3 };

QComboBox *makeBoolCombo(bool found, bool isYes)
{
    auto *combo = new QComboBox;
    combo->addItem(QObject::tr("(unset)"), -1);
    combo->addItem(QStringLiteral("YES"), 1);
    combo->addItem(QStringLiteral("NO"), 0);
    combo->setCurrentIndex(!found ? 0 : (isYes ? 1 : 2));
    return combo;
}

} // namespace

UserFlagValuesDialog::UserFlagValuesDialog(UserFlagsEditRef ref, QWidget *parent)
    : QDialog(parent)
    , m_ref(std::move(ref))
{
    setWindowTitle(tr("User Flags — %1").arg(m_ref.objectName));
    resize(480, 320);

    auto *intro = new QLabel(
        tr("Flag values for %1 \"%2\". Leave a value blank (or pick "
           "\"(unset)\") to remove the assignment.")
            .arg(m_ref.objectType.toLower(), m_ref.objectName));
    intro->setWordWrap(true);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({tr("Flag"), tr("Type"), tr("Value")});
    m_table->horizontalHeader()->setSectionResizeMode(ColFlag,
                                                      QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColType,
                                                      QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColValue,
                                                      QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &UserFlagValuesDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(m_table, 1);
    layout->addWidget(buttons);

    reloadFromModel();
}

void UserFlagValuesDialog::reloadFromModel()
{
    m_table->setRowCount(0);
    if (!m_ref.model) return;

    for (const auto &def : m_ref.model->defs()) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        auto *flagItem = new QTableWidgetItem(def.name);
        flagItem->setFlags(flagItem->flags() & ~Qt::ItemIsEditable);
        if (!def.description.isEmpty())
            flagItem->setToolTip(def.description);
        m_table->setItem(row, ColFlag, flagItem);

        auto *typeItem =
            new QTableWidgetItem(UserFlagsModel::typeLabel(def.type));
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, ColType, typeItem);

        bool found = false;
        const QString v = m_ref.model->value(m_ref.objectType,
                                             m_ref.objectName,
                                             def.name, &found);
        if (def.type == FlagType::Boolean) {
            m_table->setCellWidget(row, ColValue,
                                   makeBoolCombo(found,
                                                 v == QStringLiteral("YES")));
        } else {
            m_table->setItem(row, ColValue,
                             new QTableWidgetItem(found ? v : QString()));
        }
    }
}

bool UserFlagValuesDialog::applyChanges(QString *outError)
{
    if (!m_ref.model) {
        if (outError) *outError = tr("No project is open.");
        return false;
    }

    const auto defs = m_ref.model->defs();
    for (int row = 0; row < m_table->rowCount() && row < defs.size(); ++row) {
        const auto &def = defs[row];

        bool currentFound = false;
        const QString current = m_ref.model->value(m_ref.objectType,
                                                   m_ref.objectName,
                                                   def.name, &currentFound);

        // Staged value: combo for booleans, text item otherwise.
        bool stagedSet = false;
        QString staged;
        if (const auto *combo =
                qobject_cast<QComboBox *>(m_table->cellWidget(row, ColValue))) {
            const int data = combo->currentData().toInt();
            stagedSet = (data >= 0);
            staged = (data == 1) ? QStringLiteral("YES") : QStringLiteral("NO");
        } else if (const auto *item = m_table->item(row, ColValue)) {
            staged = item->text().trimmed();
            stagedSet = !staged.isEmpty();
        }

        if (!stagedSet) {
            if (currentFound) {
                m_ref.model->clearValue(m_ref.objectType, m_ref.objectName,
                                        def.name);
                m_wroteAnyChanges = true;
            }
            continue;
        }
        if (currentFound && staged == current)
            continue;  // unchanged

        QString err;
        if (!m_ref.model->setValue(m_ref.objectType, m_ref.objectName,
                                   def.name, staged, &err)) {
            // Earlier rows may already have committed — m_wroteAnyChanges
            // reflects that so the caller still marks the project dirty.
            if (outError) *outError = err;
            return false;
        }
        m_wroteAnyChanges = true;
    }

    return true;
}

QString UserFlagValuesDialog::updatedSummary() const
{
    return userFlagsSummaryFor(m_ref.model, m_ref.objectType,
                               m_ref.objectName);
}

void UserFlagValuesDialog::onAccepted()
{
    QString err;
    if (!applyChanges(&err)) {
        if (m_confirmationsEnabled)
            QMessageBox::warning(this, tr("User Flags"), err);
        return;  // keep the dialog open so the user can fix the value
    }
    accept();
}
