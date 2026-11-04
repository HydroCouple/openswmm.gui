/**
 * @file      swmmaggregateselection.h
 * @brief     Shared field edits for selected SWMM network objects.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef SWMMAGGREGATESELECTION_H
#define SWMMAGGREGATESELECTION_H

#include "ui/panels/swmmattributetablemodel.h"
#include <QMap>

class QUndoStack;

/*! A frozen set of network objects, using the table's schema and checked scalar writes. */
class SWMMAggregateSelection : public QObject
{
    Q_OBJECT
public:
    struct Field {
        bool mixed = false;
        QVariant display;
        QVariant edit;
        int editable = 0;
    };
    SWMMAggregateSelection(SWMMModelLayer *layer, SWMMModelLayer::Category category,
                          const QStringList &names, QUndoStack *undo, QObject *parent);
    SWMMAttributeTableModel *source() const { return m_source; }
    QList<int> rows() const;
    int unresolvedCount() const { return m_names.size() - rows().size(); }
    Field field(int column) const;
    bool setValue(int column, const QVariant &value);
    bool isStaged(int column) const { return m_staged.contains(column); }
    QVariant stagedValue(int column) const { return m_staged.value(column); }
    QList<int> stagedColumns() const { return m_staged.keys(); }
    bool stage(int column, const QVariant &value);
    void discard();
    bool apply(QString *error);
    static bool supports(const openswmmvis::ColumnSpec &spec);
signals:
    void editFinished(const QString &error);
    void stagedChanged();
private:
    SWMMAttributeTableModel *m_source;
    QStringList m_names;
    QPointer<QUndoStack> m_undo;
    QMap<int, QVariant> m_staged;
    QMap<int, QList<QVariant>> m_original;
};

#endif
