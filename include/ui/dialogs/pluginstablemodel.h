/**
 * @file      pluginstablemodel.h
 * @brief     MVC model for the [PLUGINS] section table in the Simulation Options dialog.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * MVC model for the [PLUGINS] section table in the Simulation Options
 * dialog. Each row is one plugin entry: a path / id / id:version string
 * plus a free-form arguments string passed to the plugin's initialize()
 * call. Pairs with the reusable PathBrowseDelegate for column 0's
 * browse-button affordance.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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

    /**
     * @param[in] parent Optional QObject owner; caller manages parentless model, use on owning GUI
     *   thread.
     * @brief Initialize an empty local two-string staging table without engine/plugin registry
     *   access.
     */
    explicit PluginsTableModel(QObject *parent = nullptr);

    /**
     * @param[in] parent Valid parent indicates unsupported child table.
     * @return Zero for child parent, otherwise current staged row count. No engine enumeration.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @param[in] parent Valid parent indicates unsupported child table.
     * @return Two at root, zero for valid parent.
     */
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @param[in] index Requested staged cell; require valid in-range row, recognized path/args
     *   column.
     * @param[in] role Only DisplayRole/EditRole return text.
     * @return Raw stored string copy for supported cell/role, invalid QVariant otherwise. No
     *   path/plugin/version/argument validation or loading.
     */
    QVariant data(const QModelIndex &index, int role) const override;
    /**
     * @param[in] index Valid in-range staged row and path/args column required.
     * @param[in] value Converted with toString and stored verbatim, including blanks/whitespace.
     * @param[in] role Must be EditRole.
     * @return False for unsupported role/cell; true for supported equal text (no signal) or changed
     *   text (dataChanged for Display/Edit roles).
     * @note Local staging only; no engine/plugin mutation, path resolution, validation,
     *   persistence, undo or automatic dirty signal beyond model dataChanged.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @param[in] section Zero-based path/args column; other section invalid.
     * @param[in] orientation Only Horizontal supported.
     * @param[in] role Only DisplayRole supported.
     * @return Translated plugin identity/arguments heading or invalid QVariant; no vertical row
     *   numbering.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    /**
     * @param[in] index Invalid index returns NoItemFlags; any valid index gets
     *   editable/selectable/enabled without bounds/column checks.
     * @return Local edit permissions independent of engine lifecycle/plugin availability. Model
     *   mutations still validate cell.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @param[in] row Insertion offset in [0, current row count].
     * @param[in] count Positive number of empty path/args rows; row+count must fit int arithmetic.
     * @param[in] parent Must be invalid root parent.
     * @return False for unsupported parent/invalid range/count, otherwise true after
     *   begin/endInsertRows notification. No engine write; indexes at/after insertion shift.
     */
    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    /**
     * @param[in] row First staged row, nonnegative.
     * @param[in] count Positive count wholly within current rows; row+count must fit int.
     * @param[in] parent Must be invalid root parent.
     * @return False for unsupported parent/invalid range/count; true after begin/endRemoveRows and
     *   destruction of local strings. Does not uninstall plugins or alter engine.
     */
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    /**
     * @brief Clear nonempty staged list under model reset; already empty no-op/no signals. No
     *   plugin unload, engine mutation or preferences write.
     */
    void clearRows();
    /**
     * @param[in] path Raw plugin identity/path string copied unchanged; existence/identifier
     *   validity not checked.
     * @param[in] args Raw initialization arguments copied unchanged, no splitting/escaping here.
     * @return New zero-based row index after begin/endInsertRows; no automatic engine load or
     *   registry update.
     */
    int appendRow(const QString &path, const QString &args);

    /**
     * @param[in] row Zero-based staged row; negative/out-of-range permitted.
     * @return Raw stored plugin identity/path copy, or empty when row invalid. Empty valid row is
     *   indistinguishable; no plugin/source lookup.
     */
    QString pathAt(int row) const;
    /**
     * @param[in] row Zero-based staged row; negative/out-of-range permitted.
     * @return Raw stored initialization arguments copy, or empty when row invalid. Empty valid row
     *   is indistinguishable; no plugin/source lookup.
     */
    QString argsAt(int row) const;

private:
    struct Row {
        QString path;
        QString args;
    };
    QList<Row> m_rows;
};

#endif // PLUGINSTABLEMODEL_H
