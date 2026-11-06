/**
 * @file      dataobjectref.cpp
 * @brief     Property-cell value carrying a reference to one named data object (curve / time
 *            series / pattern / unit hydrograph).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/properties/dataobjectref.h"

#include <QCoreApplication>

void registerDataObjectRefConverter()
{
    static bool s_registered = false;
    if (s_registered)
        return;
    QMetaType::registerConverter<DataObjectRef, QString>([](const DataObjectRef &r) {
        return r.currentName.isEmpty()
                   ? QCoreApplication::translate("DataObjectRef", "(unassigned)")
                   : r.currentName;
    });
    s_registered = true;
}
