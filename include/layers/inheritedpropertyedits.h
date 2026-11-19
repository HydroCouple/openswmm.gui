#pragma once
#include "selection/selectionmanager.h"
#include <QVariant>
class SWMMModelLayer;
class QUndoStack;
namespace inheritedproperties {
struct Edit { SWMMObjectRef target; QString key; QVariant value; int flagType = -1; };
bool field(const QString &key);
bool apply(SWMMModelLayer *layer, const QVector<Edit> &edits, QUndoStack *undo,
           QString *error = nullptr);
// Used by the table's applicability flags and owned field editors.
bool editable(SWMMModelLayer *layer, const SWMMObjectRef &target, const QString &key,
              QString *reason = nullptr);
}
