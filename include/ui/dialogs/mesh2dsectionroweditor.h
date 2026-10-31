/**
 * @file      mesh2dsectionroweditor.h
 * @brief     View pieces shared by the scoped-row editors: a registry-choice delegate and a
 *            table page with Add / Remove for one Mesh2DSectionRowModel.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_MESH2DSECTIONROWEDITOR_H
#define OPENSWMMVIS_UI_DIALOGS_MESH2DSECTIONROWEDITOR_H

#include <QStyledItemDelegate>
#include <QWidget>

class QLabel;
class QPushButton;
class QTableView;

namespace openswmmvis::ui
{

class Mesh2DSectionRowModel;

/*! Combo editor listing the cell's ChoicesRole names (species, land uses, tags, series, scope
 *  labels); cells without choices get the default line editor. */
class Mesh2DChoiceDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
};

/*! A hint, the section's table and Add / Remove row buttons. Staging only: the owning dialog
 *  commits the model. */
class Mesh2DSectionRowEditor : public QWidget
{
    Q_OBJECT
public:
    /**
     * @param[in] model Borrowed model shown by the table; must outlive this widget.
     * @param[in] hint Rich-text explanation shown above the table.
     * @param[in] parent Optional owner.
     */
    Mesh2DSectionRowEditor(Mesh2DSectionRowModel *model, const QString &hint,
                           QWidget *parent = nullptr);
    [[nodiscard]] QTableView *view() const { return m_view; }
    /** Status line under the table (hidden while empty). */
    void setNote(const QString &text);
    /** Enable or disable editing, Add and Remove. */
    void setEditable(bool on);

private:
    Mesh2DSectionRowModel *m_model = nullptr;
    QTableView *m_view = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_remove = nullptr;
    QLabel *m_note = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESH2DSECTIONROWEDITOR_H
