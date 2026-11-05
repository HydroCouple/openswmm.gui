// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/panels/dataobjectattributetablemodel.h"
#include <QMap>

class DataObjectAggregateSelection : public QObject
{
    Q_OBJECT
public:
    struct Field {
        bool mixed = false;
        QVariant display, edit;
        int editable = 0;
        QString reason;
    };
    DataObjectAggregateSelection(SWMMModelLayer *layer, SWMMModelLayer::DataCategory category,
                                 const QStringList &names, QUndoStack *undo,
                                 QObject *parent = nullptr);
    DataObjectAttributeTableModel *source() const { return m_source; }
    QList<int> rows() const;
    int unresolvedCount() const;
    QStringList names() const;
    Field field(int column) const;
    bool setValue(int column, const QVariant &value);
    bool isStaged(int column) const { return m_staged.contains(column); }
    QVariant stagedValue(int column) const { return m_staged.value(column); }
    QList<int> stagedColumns() const { return m_staged.keys(); }
    bool stage(int column, const QVariant &value);
    void discard();
    void invalidate() { m_valid = false; }
    bool active() const { return schemaValid() && unresolvedCount() == 0; }
    bool apply(QString *error);
    static bool supports(const openswmmvis::ColumnSpec &) { return true; }
signals:
    void editFinished(const QString &error);
    void stagedChanged();

private:
    DataObjectAttributeTableModel *m_source;
    QList<QPointer<QObject>> m_targets;
    QStringList m_originalNames;
    QMap<int, QVariant> m_staged;
    QString m_stagedKey;
    QMap<int, QList<QVariant>> m_before;
    QPointer<QUndoStack> m_undo;
    QList<QVariant> m_schemas;
    bool schemaValid() const;
    bool m_valid = true;
};
