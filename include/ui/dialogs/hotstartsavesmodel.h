/*!
 * \file   hotstartsavesmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * MVC backing for the scheduled-hot-start-saves table in the Simulation
 * Options dialog ([FILES] SAVE HOTSTART). Pairs with the reusable
 * PathBrowseDelegate (for the path column) and ships its own
 * HotstartSavesDateTimeDelegate for the datetime picker.
 *
 * The delegates are intended for use with QTableView::openPersistentEditor()
 * so each row's path field, browse button, and date-time picker are visible
 * without first clicking into the cell.
 */
#ifndef HOTSTARTSAVESMODEL_H
#define HOTSTARTSAVESMODEL_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>
#include <QStyledItemDelegate>

/*!
 * \brief Row schema: {path relative to the .inp dir, OLE-Automation datetime
 *        sentinel}. An oaDate of 0.0 means "save at end of run" and renders
 *        as the QDateTimeEdit special-value text "(end of run)".
 */
class HotstartSavesModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColPath = 0, ColDateTime = 1, ColCount };

    explicit HotstartSavesModel(QObject *parent = nullptr);

    // QAbstractTableModel -----------------------------------------------------
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

    // Convenience -------------------------------------------------------------
    void clearRows();
    int  appendRow(const QString &path, double oaDate);
    bool swapRows(int a, int b);

    QString pathAt(int row) const;
    double  oaDateAt(int row) const;

private:
    struct Row {
        QString path;
        double  oaDate {0.0};
    };
    QList<Row> m_rows;
};

/*!
 * \brief Delegate for the Datetime column. Editor is a QDateTimeEdit whose
 *        minimum date-time renders as the special value "(end of run)" so
 *        the user can pick a wall-clock time or leave it as the sentinel.
 */
class HotstartSavesDateTimeDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit HotstartSavesDateTimeDelegate(QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &opt,
                          const QModelIndex &idx) const override;
    void setEditorData(QWidget *editor,
                       const QModelIndex &idx) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &idx) const override;
};

#endif // HOTSTARTSAVESMODEL_H
