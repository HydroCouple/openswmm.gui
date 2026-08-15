/*!
 * \file   hotstartsavesmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/hotstartsavesmodel.h"
#include "ui/dialogs/simulationoptionsdialog.h"

#include <QDateTime>
#include <QDateTimeEdit>
#include <QSignalBlocker>

// ---------------------------------------------------------------------------
// HotstartSavesModel
// ---------------------------------------------------------------------------

HotstartSavesModel::HotstartSavesModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int HotstartSavesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int HotstartSavesModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant HotstartSavesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case ColPath:     return r.path;
        case ColDateTime: return r.oaDate;
        }
    }
    return {};
}

bool HotstartSavesModel::setData(const QModelIndex &index,
                                 const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return false;
    if (role != Qt::EditRole) return false;
    Row &r = m_rows[index.row()];

    switch (index.column()) {
    case ColPath: {
        const QString s = value.toString();
        if (r.path == s) return true;
        r.path = s;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }
    case ColDateTime: {
        const double d = value.toDouble();
        if (qFuzzyCompare(r.oaDate, d)) return true;
        r.oaDate = d;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }
    }
    return false;
}

QVariant HotstartSavesModel::headerData(int section,
                                        Qt::Orientation orientation,
                                        int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal) {
        switch (section) {
        case ColPath:     return tr("Path (relative to .inp directory)");
        case ColDateTime: return tr("Datetime");
        }
    }
    return {};
}

Qt::ItemFlags HotstartSavesModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool HotstartSavesModel::insertRows(int row, int count,
                                    const QModelIndex &parent)
{
    if (parent.isValid() || count <= 0 || row < 0 || row > m_rows.size())
        return false;
    beginInsertRows(QModelIndex(), row, row + count - 1);
    for (int i = 0; i < count; ++i) m_rows.insert(row, Row{});
    endInsertRows();
    return true;
}

bool HotstartSavesModel::removeRows(int row, int count,
                                    const QModelIndex &parent)
{
    if (parent.isValid() || count <= 0 || row < 0
        || row + count > m_rows.size()) return false;
    beginRemoveRows(QModelIndex(), row, row + count - 1);
    for (int i = 0; i < count; ++i) m_rows.removeAt(row);
    endRemoveRows();
    return true;
}

void HotstartSavesModel::clearRows()
{
    if (m_rows.isEmpty()) return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

int HotstartSavesModel::appendRow(const QString &path, double oaDate)
{
    const int row = m_rows.size();
    beginInsertRows(QModelIndex(), row, row);
    m_rows.append(Row{path, oaDate});
    endInsertRows();
    return row;
}

bool HotstartSavesModel::swapRows(int a, int b)
{
    const int n = m_rows.size();
    if (a < 0 || b < 0 || a >= n || b >= n || a == b) return false;
    m_rows.swapItemsAt(a, b);
    const int lo = std::min(a, b);
    const int hi = std::max(a, b);
    emit dataChanged(index(lo, 0), index(hi, ColCount - 1),
                     {Qt::DisplayRole, Qt::EditRole});
    return true;
}

QString HotstartSavesModel::pathAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row).path : QString();
}

double HotstartSavesModel::oaDateAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row).oaDate : 0.0;
}

// ---------------------------------------------------------------------------
// HotstartSavesDateTimeDelegate
// ---------------------------------------------------------------------------

HotstartSavesDateTimeDelegate::HotstartSavesDateTimeDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QWidget *HotstartSavesDateTimeDelegate::createEditor(
    QWidget *parent, const QStyleOptionViewItem & /*opt*/,
    const QModelIndex & /*idx*/) const
{
    auto *w = new QDateTimeEdit(parent);
    w->setDisplayFormat(QStringLiteral("MM/dd/yyyy HH:mm:ss"));
    // OA date 0.0 (1899-12-30 00:00) renders as the "(end of run)" special
    // value — same sentinel the legacy cell-widget table used.
    w->setMinimumDateTime(SimulationOptionsDialog::qDateTimeFromOaDate(0.0));
    w->setSpecialValueText(tr("(end of run)"));
    w->setCalendarPopup(true);
    w->setFrame(false);

    // Push edits back to the model immediately so persistent-editor changes
    // don't get lost if the user clicks away without committing manually.
    QObject::connect(w, &QDateTimeEdit::dateTimeChanged, w,
        [this, w] {
            auto *self = const_cast<HotstartSavesDateTimeDelegate *>(this);
            emit self->commitData(w);
        });
    return w;
}

void HotstartSavesDateTimeDelegate::setEditorData(QWidget *editor,
                                                  const QModelIndex &idx) const
{
    auto *w = qobject_cast<QDateTimeEdit *>(editor);
    if (!w) return;
    const double oa = idx.data(Qt::EditRole).toDouble();
    QSignalBlocker block(w);   // avoid round-tripping our own write
    if (oa > 0.0) w->setDateTime(SimulationOptionsDialog::qDateTimeFromOaDate(oa));
    else          w->setDateTime(w->minimumDateTime());
}

void HotstartSavesDateTimeDelegate::setModelData(QWidget *editor,
                                                 QAbstractItemModel *model,
                                                 const QModelIndex &idx) const
{
    auto *w = qobject_cast<QDateTimeEdit *>(editor);
    if (!w || !model) return;
    double oa = 0.0;
    if (w->dateTime() != w->minimumDateTime())
        oa = SimulationOptionsDialog::oaDateFromQDateTime(w->dateTime());
    model->setData(idx, oa, Qt::EditRole);
}
