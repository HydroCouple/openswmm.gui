/*!
 * \file   subcatchcompoundeditref.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/subcatchcompoundeditref.h"

#include <QMetaType>

void registerSubcatchCompoundEditRefConverter()
{
    static bool s_registered = false;
    if (s_registered) return;
    QMetaType::registerConverter<SubcatchCompoundEditRef, QString>(
        [](const SubcatchCompoundEditRef &r) { return r.summary; });
    s_registered = true;
}
