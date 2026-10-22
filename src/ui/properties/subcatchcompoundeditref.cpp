/**
 * @file      subcatchcompoundeditref.cpp
 * @brief     property-cell value identifying one compound subcatchment attribute (land-use
 *            coverage / groundwater / LID usage).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/properties/subcatchcompoundeditref.h"

#include <QMetaType>

void registerSubcatchCompoundEditRefConverter()
{
    static bool s_registered = false;
    if (s_registered)
        return;
    QMetaType::registerConverter<SubcatchCompoundEditRef, QString>(
        [](const SubcatchCompoundEditRef &r) { return r.summary; });
    s_registered = true;
}
