/**
 * @file      mesh2dsectionroweditor.cpp
 * @brief     View pieces shared by the scoped-row editors.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/mesh2dsectionroweditor.h"

#include "ui/dialogs/mesh2dsectionrowmodel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace openswmmvis::ui
{

QWidget *Mesh2DChoiceDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                            const QModelIndex &index) const
{
    const QStringList choices = index.data(Mesh2DSectionRowModel::ChoicesRole).toStringList();
    if (choices.isEmpty())
        return QStyledItemDelegate::createEditor(parent, option, index);
    auto *combo = new QComboBox(parent);
    combo->addItems(choices);
    return combo;
}

void Mesh2DChoiceDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        combo->setCurrentIndex(std::max(0, combo->findText(index.data(Qt::EditRole).toString())));
        return;
    }
    QStyledItemDelegate::setEditorData(editor, index);
}

void Mesh2DChoiceDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                        const QModelIndex &index) const
{
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        model->setData(index, combo->currentText(), Qt::EditRole);
        return;
    }
    QStyledItemDelegate::setModelData(editor, model, index);
}

Mesh2DSectionRowEditor::Mesh2DSectionRowEditor(Mesh2DSectionRowModel *model, const QString &hint,
                                               QWidget *parent)
    : QWidget(parent), m_model(model)
{
    auto *layout = new QVBoxLayout(this);
    auto *label = new QLabel(hint, this);
    label->setWordWrap(true);
    layout->addWidget(label);

    m_view = new QTableView(this);
    m_view->setObjectName(model->objectName() + QStringLiteral("Table"));
    m_view->setModel(model);
    m_view->setItemDelegate(new Mesh2DChoiceDelegate(m_view));
    m_view->setAccessibleName(model->sectionName());
    m_view->setAccessibleDescription(
        tr("Select a row with the arrow keys. Press F2 to edit the current cell."));
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(m_view, 1);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    m_note->hide();
    layout->addWidget(m_note);

    auto *buttons = new QHBoxLayout;
    m_add = new QPushButton(tr("Add row"), this);
    m_add->setObjectName(model->objectName() + QStringLiteral("Add"));
    m_remove = new QPushButton(tr("Remove row"), this);
    m_remove->setObjectName(model->objectName() + QStringLiteral("Remove"));
    for (auto *button : {m_add, m_remove}) {
        button->setAutoDefault(false);
        buttons->addWidget(button);
    }
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(m_add, &QPushButton::clicked, this, [this] {
        const int at = m_model->appendRow();
        m_view->selectRow(at);
        m_view->scrollTo(m_model->index(at, 0));
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        QList<int> rows;
        for (const QModelIndex &index : m_view->selectionModel()->selectedRows())
            rows.append(index.row());
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int row : rows)
            m_model->removeRow(row);
    });
}

void Mesh2DSectionRowEditor::setNote(const QString &text)
{
    m_note->setText(text);
    m_note->setVisible(!text.isEmpty());
}

void Mesh2DSectionRowEditor::setEditable(bool on)
{
    m_add->setEnabled(on);
    m_remove->setEnabled(on);
    m_view->setEditTriggers(on ? QAbstractItemView::AllEditTriggers
                               : QAbstractItemView::NoEditTriggers);
}

} // namespace openswmmvis::ui
