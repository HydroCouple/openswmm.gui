/*!
 * \file   pluginstablemodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * MVC model for the [PLUGINS] section table in the Simulation Options
 * dialog. Each row is one plugin entry: a path / id / id:version string
 * plus a free-form arguments string passed to the plugin's initialize()
 * call. Pairs with the reusable PathBrowseDelegate for column 0's
 * browse-button affordance.
 */
#ifndef PLUGINSTABLEMODEL_H
#define PLUGINSTABLEMODEL_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>

class PluginsTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColPath = 0, ColArgs = 1, ColCount };

    explicit PluginsTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool insertRows(int row, int count,
                    const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count,
                    const QModelIndex &parent = QModelIndex()) override;

    void clearRows();
    int  appendRow(const QString &path, const QString &args);

    QString pathAt(int row) const;
    QString argsAt(int row) const;

private:
    struct Row {
        QString path;
        QString args;
    };
    QList<Row> m_rows;
};

#endif // PLUGINSTABLEMODEL_H
