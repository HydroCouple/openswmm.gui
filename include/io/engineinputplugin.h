// SPDX-License-Identifier: Apache-2.0
/**
 * @file      engineinputplugin.h
 * @brief     Engine input plugin that opens a model file, chosen from its extension.
 */
#pragma once

#include <QFileInfo>
#include <QString>

namespace openswmmvis
{

/**
 * @brief Input plugin ID swmm_engine_open() needs for @p modelPath.
 *
 * The engine reads text input files itself and refuses a GeoPackage or HDF5 model unless its
 * plugin is named. Every place that opens a project file must pass the same choice.
 *
 * @param modelPath Model file path; only its extension is used.
 * @return Static plugin ID, or nullptr for the built-in text reader.
 */
inline const char *engineInputPluginFor(const QString &modelPath)
{
    const QString ext = QFileInfo(modelPath).suffix().toLower();
    if (ext == QLatin1String("gpkg"))
        return "org.hydrocouple.openswmm.plugins.geopackage";
    if (ext == QLatin1String("h5") || ext == QLatin1String("hdf5"))
        return "org.hydrocouple.openswmm.plugins.hdf5";
    return nullptr;
}

} // namespace openswmmvis
