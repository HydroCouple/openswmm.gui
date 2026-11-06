#ifndef NODESTRUCTUREEDITS_H
#define NODESTRUCTUREEDITS_H
#include "layers/nodecoupling2d.h"
#include <QVariant>
class SWMMModelLayer;
class QUndoStack;
namespace nodestructure
{
// Shared complete-record transactions for Properties, tables and existing layer facades.
bool field(const QString &setter);
bool applyFields(SWMMModelLayer *layer, const QStringList &nodes,
                 const QMap<QString, QVariant> &fields, QUndoStack *undo, QString *error = nullptr);
bool applyCoupling(SWMMModelLayer *layer, QVector<nodecoupling2d::State> states, QUndoStack *undo,
                   const QString &text, QString *error = nullptr);
}
#endif
