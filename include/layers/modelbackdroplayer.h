// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      modelbackdroplayer.h
 * @brief     The model's [BACKDROP] image as a raster layer at its DIMENSIONS (review G-01).
 * @details   SWMM 5 places a backdrop image by the map extent in [BACKDROP] DIMENSIONS, not by
 *            georeferencing in the image. The image is wrapped in an in-memory GDAL VRT
 *            (/vsimem) that carries that extent and the model's CRS, so the raster layer places
 *            it without any file being written beside the image (the VRT reads an in-memory copy
 *            of the image file, which costs memory equal to the file's size). The section itself
 *            is never rewritten: the model saves it as it was read.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class GISRasterLayer;
class OpenSWMMVisLayer;
class SWMMModelLayer;

namespace openswmmvis::layers
{

/// Value of the "modelSection" QObject property on the model backdrop layer.
inline constexpr char kModelBackdropSection[] = "BACKDROP";

/** @return @p file as an absolute path, resolved against the folder of @p modelPath. */
QString resolveBackdropPath(const QString &file, const QString &modelPath);

/**
 * @brief The model's backdrop as a raster layer, opening asynchronously.
 * @param[out] warnings Why a backdrop the model names is not shown: the image is missing or
 *   unreadable, or [BACKDROP] has no DIMENSIONS.
 * @return A new unparented layer named "Backdrop", or nullptr.
 */
GISRasterLayer *createModelBackdropLayer(SWMMModelLayer &model, QStringList *warnings);

/** @return The model backdrop layer among @p layers, or nullptr. */
GISRasterLayer *findModelBackdropLayer(const QList<OpenSWMMVisLayer *> &layers);

} // namespace openswmmvis::layers
