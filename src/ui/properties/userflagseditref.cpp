/**
 * @file      userflagseditref.cpp
 * @brief     Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — compound-attribute cell value for
 *            the per-object "User Flags" row in the Property Browser.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/properties/userflagseditref.h"

#include "ui/models/userflagsmodel.h"

#include <QObject>

QString userFlagsSummaryFor(openswmmvis::ui::UserFlagsModel *model, const QString &objectType,
                            const QString &objectName)
{
    if (!model || model->defs().isEmpty())
        return QObject::tr("(no flags defined)");
    int set = 0;
    const auto &defs = model->defs();
    for (const auto &def : defs) {
        bool found = false;
        model->value(objectType, objectName, def.name, &found);
        if (found)
            ++set;
    }
    if (set == 0)
        return QObject::tr("(none set)");
    return QObject::tr("%1 of %2 set").arg(set).arg(defs.size());
}

void registerUserFlagsEditRefConverter()
{
    static bool s_registered = false;
    if (s_registered)
        return;
    QMetaType::registerConverter<UserFlagsEditRef, QString>(
        [](const UserFlagsEditRef &r) { return r.summary; });
    s_registered = true;
}
