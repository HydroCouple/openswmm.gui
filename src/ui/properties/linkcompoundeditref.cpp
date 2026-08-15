/*!
 * \file   linkcompoundeditref.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/linkcompoundeditref.h"

#include <QCoreApplication>

void registerLinkCompoundEditRefConverter()
{
    static bool s_registered = false;
    if (s_registered) return;
    QMetaType::registerConverter<LinkCompoundEditRef, QString>(
        [](const LinkCompoundEditRef &r) {
            return r.summary.isEmpty()
                       ? QCoreApplication::translate("LinkCompoundEditRef", "Edit…")
                       : r.summary;
        });
    s_registered = true;
}
