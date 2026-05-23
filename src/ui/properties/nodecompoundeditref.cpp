/*!
 * \file   nodecompoundeditref.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/nodecompoundeditref.h"

void registerNodeCompoundEditRefConverter()
{
    static bool s_registered = false;
    if (s_registered) return;
    QMetaType::registerConverter<NodeCompoundEditRef, QString>(
        [](const NodeCompoundEditRef &r) { return r.summary; });
    s_registered = true;
}
