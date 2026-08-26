/*!
 * \file   initialqualityeditref.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/properties/initialqualityeditref.h"

#include <QCoreApplication>

#include <openswmm/engine/openswmm_initial_quality.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>

QString initialQualitySummaryFor(SWMM_Engine engine, int isLink,
                                 const QString &elementName)
{
    const auto none = QCoreApplication::translate("InitialQualityEditRef",
                                                  "(none)");
    if (!engine || elementName.isEmpty()) return none;
    const QByteArray nameUtf8 = elementName.toUtf8();
    const int elemIdx = isLink
        ? swmm_link_index(engine, nameUtf8.constData())
        : swmm_node_index(engine, nameUtf8.constData());
    if (elemIdx < 0) return none;

    int set = 0;
    const int count = swmm_init_quality_count(engine);
    for (int i = 0; i < count; ++i) {
        int rowIsLink = 0, rowElem = -1;
        char cons[128] = {0};
        double value = 0.0;
        if (swmm_init_quality_get(engine, i, &rowIsLink, &rowElem,
                                  cons, sizeof(cons), &value) != SWMM_OK)
            continue;
        if (rowIsLink == (isLink ? 1 : 0) && rowElem == elemIdx) ++set;
    }
    return set > 0
        ? QCoreApplication::translate("InitialQualityEditRef", "%1 set")
              .arg(set)
        : none;
}

void registerInitialQualityEditRefConverter()
{
    static bool s_registered = false;
    if (s_registered) return;
    QMetaType::registerConverter<InitialQualityEditRef, QString>(
        [](const InitialQualityEditRef &r) { return r.summary; });
    s_registered = true;
}
