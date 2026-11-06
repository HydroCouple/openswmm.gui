#ifndef USAGEEDITREF_H
#define USAGEEDITREF_H
#include "ui/properties/subcatchcompoundeditref.h"
#include "ui/properties/linkcompoundeditref.h"
#include <QVector>

struct UsageTarget {
    enum Kind { Subcatchment, Link, Node };
    Kind kind = Subcatchment;
    QString name;
    bool operator==(const UsageTarget &) const = default;
};
struct UsageEditRef {
    enum Family { Lid, Inlet };
    Family family = Lid;
    SWMM_Engine engine = nullptr;
    SWMMModelLayer *layer = nullptr;
    QPointer<QUndoStack> undoStack;
    QVector<UsageTarget> targets;
    QString summary;
    bool operator==(const UsageEditRef &) const = default;
};
Q_DECLARE_METATYPE(UsageEditRef)
UsageEditRef usageRef(const SubcatchCompoundEditRef &ref);
UsageEditRef usageRef(const LinkCompoundEditRef &ref);
void registerUsageEditRefConverter();
#endif
