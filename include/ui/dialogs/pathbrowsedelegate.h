/*!
 * \file   pathbrowsedelegate.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Reusable QStyledItemDelegate that renders a {QLineEdit + "…" browse
 * button} composite editor for a path column. Used by the Simulation
 * Options dialog's hot-start-saves and [PLUGINS] tables, and any other
 * MVC list / table where a row maps to a file path.
 *
 * Each instance carries a fixed dialog title, file-filter string, and
 * open/save mode — pass these in via the constructor (or the matching
 * setters) so a single class can serve multiple columns with different
 * file pickers.
 */
#ifndef PATHBROWSEDELEGATE_H
#define PATHBROWSEDELEGATE_H

#include <QString>
#include <QStyledItemDelegate>

class PathBrowseDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    enum Mode { OpenFile, SaveFile };

    explicit PathBrowseDelegate(QObject *parent = nullptr);
    PathBrowseDelegate(Mode mode,
                       QString title,
                       QString filter,
                       QString placeholder = {},
                       QObject *parent = nullptr);

    void setMode(Mode m)                  { m_mode = m; }
    void setDialogTitle(const QString &t) { m_title = t; }
    void setFilter(const QString &f)      { m_filter = f; }
    void setPlaceholder(const QString &p) { m_placeholder = p; }

    // QStyledItemDelegate ------------------------------------------------------
    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &opt,
                          const QModelIndex &idx) const override;
    void setEditorData(QWidget *editor,
                       const QModelIndex &idx) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &idx) const override;

private:
    Mode    m_mode  = OpenFile;
    QString m_title;
    QString m_filter;
    QString m_placeholder;
};

#endif // PATHBROWSEDELEGATE_H
