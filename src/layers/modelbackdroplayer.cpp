// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      modelbackdroplayer.cpp
 * @brief     The model's [BACKDROP] image as a raster layer at its DIMENSIONS (review G-01).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/modelbackdroplayer.h"

#include "map/spatialreferencesystem.h"
#include "layers/gisrasterlayer.h"
#include "layers/swmmmodellayer.h"
#include "project/modelannotations.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QUuid>

#include <cpl_conv.h>
#include <cpl_string.h>
#include <cpl_vsi.h>
#include <gdal_priv.h>
#include <gdal_utils.h>

namespace openswmmvis::layers
{

namespace
{
QString tr(const char *text)
{
    return QCoreApplication::translate("ModelBackdropLayer", text);
}

/**
 * An in-memory VRT of @p image placed at the extent, in @p wkt when given; empty on failure.
 * The VRT reads an in-memory copy of the image file (its bytes, not decoded pixels), because GDAL
 * saves statistics computed through a VRT beside its source as `<image>.aux.xml`; with the copy
 * they, and any overviews, stay in @p folder.
 */
QString placedImage(const QString &image, const project::ModelBackdrop &backdrop,
                    const QByteArray &wkt, const QString &folder)
{
    GDALAllRegister();
    const QString copy = folder + QLatin1Char('/') + QFileInfo(image).fileName();
    if (CPLCopyFile(copy.toUtf8().constData(), image.toUtf8().constData()) != 0)
        return {};
    GDALDataset *source = static_cast<GDALDataset *>(
        GDALOpenEx(copy.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr,
                   nullptr, nullptr));
    if (!source)
        return {};
    const QString path = folder + QStringLiteral("/backdrop.vrt");
    const auto translate = [&](bool withSrs) {
        CPLStringList args;
        args.AddString("-of");
        args.AddString("VRT");
        args.AddString("-a_ullr"); // upper left, lower right
        for (double v : {backdrop.x1, backdrop.y2, backdrop.x2, backdrop.y1})
            args.AddString(QByteArray::number(v, 'g', 17).constData());
        if (withSrs) {
            args.AddString("-a_srs");
            args.AddString(wkt.constData());
        }
        GDALTranslateOptions *options = GDALTranslateOptionsNew(args.List(), nullptr);
        GDALDatasetH placed = GDALTranslate(path.toUtf8().constData(), source, options, nullptr);
        GDALTranslateOptionsFree(options);
        return placed && GDALClose(placed) == CE_None;
    };
    // A local (engineering) CRS may not be one GDAL can assign; the image then takes the
    // canvas's, as an unreferenced raster does.
    const bool ok = (!wkt.isEmpty() && translate(true)) || translate(false);
    GDALClose(source);
    return ok ? path : QString();
}
} // namespace

QString resolveBackdropPath(const QString &file, const QString &modelPath)
{
    if (QFileInfo(file).isAbsolute())
        return QDir::cleanPath(file);
    return QDir::cleanPath(QFileInfo(modelPath).absoluteDir().absoluteFilePath(file));
}

GISRasterLayer *createModelBackdropLayer(SWMMModelLayer &model, QStringList *warnings)
{
    const auto backdrop =
        project::parseBackdrop(project::readAnnotation(model.engine(), kModelBackdropSection));
    if (!backdrop)
        return nullptr;
    const auto warn = [warnings](const QString &text) {
        if (warnings)
            warnings->append(text);
    };
    const QString image = resolveBackdropPath(backdrop->file, model.modelFilePath());
    if (!QFileInfo::exists(image)) {
        warn(tr("The backdrop image %1 was not found; the model keeps its [BACKDROP] entry.")
                 .arg(QDir::toNativeSeparators(image)));
        return nullptr;
    }
    if (!backdrop->hasDimensions || backdrop->x2 == backdrop->x1 || backdrop->y2 == backdrop->y1) {
        warn(tr("The backdrop image %1 has no map extent ([BACKDROP] DIMENSIONS), so it is not "
                "shown.")
                 .arg(QDir::toNativeSeparators(image)));
        return nullptr;
    }
    const QByteArray wkt = model.srs() ? model.srs()->toWkt().toUtf8() : QByteArray();
    const QString folder = QStringLiteral("/vsimem/openswmm-backdrop-%1")
                               .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString placed = placedImage(image, *backdrop, wkt, folder);
    if (placed.isEmpty()) {
        VSIRmdirRecursive(folder.toUtf8().constData());
        warn(tr("The backdrop image %1 could not be read.").arg(QDir::toNativeSeparators(image)));
        return nullptr;
    }
    auto *layer = new GISRasterLayer(QString());
    layer->setName(tr("Backdrop"));
    layer->setProperty("modelSection", QString::fromLatin1(kModelBackdropSection));
    layer->setProperty("modelBackdropImage", image);
    QObject::connect(layer, &QObject::destroyed,
                     [folder] { VSIRmdirRecursive(folder.toUtf8().constData()); });
    // Opening names a raster after its file, here the in-memory copy.
    QObject::connect(
        layer, &GISRasterLayer::openFinished, layer, [layer] { layer->setName(tr("Backdrop")); },
        static_cast<Qt::ConnectionType>(Qt::SingleShotConnection));
    layer->openAsync(placed);
    return layer;
}

GISRasterLayer *findModelBackdropLayer(const QList<OpenSWMMVisLayer *> &layers)
{
    for (OpenSWMMVisLayer *base : layers)
        if (auto *layer = qobject_cast<GISRasterLayer *>(base))
            if (layer->property("modelSection").toString() ==
                QLatin1String(kModelBackdropSection))
                return layer;
    return nullptr;
}

} // namespace openswmmvis::layers
