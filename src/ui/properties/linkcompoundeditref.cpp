/**
 * @file      linkcompoundeditref.cpp
 * @brief     Compound-attribute cell value for the link Property Browser.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/properties/linkcompoundeditref.h"

#include <QCoreApplication>

void registerLinkCompoundEditRefConverter()
{
    static bool s_registered = false;
    if (s_registered)
        return;
    QMetaType::registerConverter<LinkCompoundEditRef, QString>([](const LinkCompoundEditRef &r) {
        return r.summary.isEmpty() ? QCoreApplication::translate("LinkCompoundEditRef", "Edit…")
                                   : r.summary;
    });
    s_registered = true;
}
