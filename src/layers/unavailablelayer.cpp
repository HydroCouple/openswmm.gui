/**
 * @file      unavailablelayer.cpp
 * @brief     Stand-in for a saved GIS layer that could not be reopened.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/unavailablelayer.h"

#include <QFileInfo>

UnavailableLayer::UnavailableLayer(const QJsonObject &entry, const QString &sourcePath,
                                   const QString &reason, OpenSWMMVisWorkspace *parent)
    : OpenSWMMVisLayer(QString(), parent), m_entry(entry), m_sourcePath(sourcePath),
      m_reason(reason)
{
    // Keeps the base SWMMDefaultLayer type: not a raster or vector, so no code
    // path that switches on layerType() treats it as one.
    const QString name = entry.value(QStringLiteral("name")).toString();
    setName(name.isEmpty() ? QFileInfo(sourcePath).fileName() : name);
    setVisible(entry.value(QStringLiteral("visible")).toBool(true));
    setOpacity(entry.value(QStringLiteral("opacity")).toDouble(1.0));
}
