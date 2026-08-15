/*!
 * \file   dataobjectref.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/dataobjectref.h"

#include <QCoreApplication>

void registerDataObjectRefConverter()
{
    static bool s_registered = false;
    if (s_registered) return;
    QMetaType::registerConverter<DataObjectRef, QString>(
        [](const DataObjectRef &r) {
            return r.currentName.isEmpty()
                       ? QCoreApplication::translate("DataObjectRef", "(unassigned)")
                       : r.currentName;
        });
    s_registered = true;
}
