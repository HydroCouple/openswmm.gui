/**
 * @file      gisdatapaths.cpp
 * @brief     Portable GDAL/PROJ data-directory resolution.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "core/gisdatapaths.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <initializer_list>

#include <cpl_conv.h>

namespace
{

// Publish a GIS data directory to both the process environment and GDAL's
// in-process CPL config store. Env covers child processes and code paths that
// read getenv() directly; CPLSetConfigOption covers the case where gdal's
// shared library was already loaded before the env var was set.
/**
 * @brief Publish one bundled data path to both environment and CPL configuration.
 * @param[in] key Non-null environment/configuration key such as GDAL_DATA.
 * @param[in] path Resolved directory converted to native separators and local bytes.
 * @details Mutates process-wide state; configure before starting GIS workers.
 */
void publishDataPath(const char *key, const QString &path)
{
    const QByteArray native = QDir::toNativeSeparators(path).toLocal8Bit();
    qputenv(key, native);
    CPLSetConfigOption(key, native.constData());
}

/**
 * @brief Identify a plausible GIS share directory using known marker files.
 * @param[in] dir Candidate directory.
 * @param[in] markers Non-null relative marker filenames.
 * @return True if any candidate marker exists below the directory.
 */
bool dirHasAnyMarker(const QString &dir, std::initializer_list<const char *> markers)
{
    for (const char *marker : markers) {
        if (QFileInfo::exists(QDir(dir).filePath(QString::fromLatin1(marker))))
            return true;
    }
    return false;
}

// Ordered list of directories to scan for `gdal/` and `proj/` subfolders.
//
// On every platform we look next to the executable first, which matches the
// POST_BUILD copy target on Windows/Linux and also handles the common dev
// case on macOS where the binary is not yet inside a .app bundle.
//
// On macOS we additionally check Contents/Resources/, which is where the
// POST_BUILD step places the data for a proper .app bundle layout.
/**
 * @brief Enumerate application-layout roots in precedence order.
 * @return Executable directory, followed on macOS by the normalized Resources directory.
 */
QStringList gisSearchRoots()
{
    const QString base = QCoreApplication::applicationDirPath();
    QStringList roots;
    roots << base;
#ifdef Q_OS_MACOS
    roots << QDir::cleanPath(QDir(base).filePath(QStringLiteral("../Resources")));
#endif
    return roots;
}

} // namespace

void setupBundledGisDataPaths()
{
    if (!QCoreApplication::instance())
        return;

    const QStringList roots = gisSearchRoots();

    for (const QString &root : roots) {
        const QString gdalDir = QDir(root).filePath(QStringLiteral("gdal"));
        if (dirHasAnyMarker(gdalDir, {"gml_registry.xml", "gdalvrt.xsd"})) {
            publishDataPath("GDAL_DATA", gdalDir);
            break;
        }
    }

    for (const QString &root : roots) {
        const QString projDir = QDir(root).filePath(QStringLiteral("proj"));
        if (QFileInfo::exists(QDir(projDir).filePath(QStringLiteral("proj.db")))) {
            // PROJ_DATA is the canonical var for PROJ 6+; PROJ_LIB is retained
            // for older PROJ builds that may still consult it.
            publishDataPath("PROJ_DATA", projDir);
            publishDataPath("PROJ_LIB", projDir);
            break;
        }
    }
}
