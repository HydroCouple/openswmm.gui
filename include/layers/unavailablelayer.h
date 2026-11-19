/**
 * @file      unavailablelayer.h
 * @brief     Stand-in for a saved GIS layer that could not be reopened.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * A project's raster, vector or feature layer whose file is missing or fails
 * to open is kept in the layer tree as this placeholder, so the next save
 * writes its entry back unchanged instead of dropping it. The layer tree
 * shows it with a warning icon and the reason as its tooltip.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef UNAVAILABLELAYER_H
#define UNAVAILABLELAYER_H

#include "layers/openswmmvislayer.h"

#include <QJsonObject>
#include <QString>

class UnavailableLayer : public OpenSWMMVisLayer
{
    Q_OBJECT
public:
    /**
     * @brief Keep a saved layer entry that could not be reopened.
     * @param[in] entry Project-file entry as read; written back by projectserializer on save.
     * @param[in] sourcePath Absolute path the entry resolved to (it may not exist).
     * @param[in] reason User-facing explanation, shown as the layer-tree tooltip.
     * @param[in] parent Optional workspace QObject owner.
     * @details Name, visibility and opacity are taken from the entry; the layer draws nothing.
     */
    UnavailableLayer(const QJsonObject &entry, const QString &sourcePath, const QString &reason,
                     OpenSWMMVisWorkspace *parent = nullptr);

    /** @return The project-file entry exactly as read. */
    [[nodiscard]] QJsonObject entry() const { return m_entry; }
    /** @return Absolute source path the entry resolved to. */
    [[nodiscard]] QString sourcePath() const { return m_sourcePath; }
    /** @return Why the layer is not loaded. */
    [[nodiscard]] QString reason() const { return m_reason; }

    /** @return The unresolved source path. */
    [[nodiscard]] QString sourceDescription() const override { return m_sourcePath; }

    /** @brief Draw nothing; the source is not loaded. */
    void render(QPainter *, const MapExtent &, const QSize &,
                const SpatialReferenceSystem *) override
    {
    }

    /** @brief Add nothing to the scene; the source is not loaded. */
    void populateScene(QGraphicsScene *, const MapExtent &, const SpatialReferenceSystem *) override
    {
    }

private:
    QJsonObject m_entry;
    QString m_sourcePath;
    QString m_reason;
};

#endif // UNAVAILABLELAYER_H
