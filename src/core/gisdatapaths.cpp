/*!
 * \file   gisdatapaths.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
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
void publishDataPath(const char *key, const QString &path)
{
    const QByteArray native = QDir::toNativeSeparators(path).toLocal8Bit();
    qputenv(key, native);
    CPLSetConfigOption(key, native.constData());
}

bool dirHasAnyMarker(const QString &dir,
                     std::initializer_list<const char *> markers)
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
        if (dirHasAnyMarker(gdalDir, { "gml_registry.xml", "gdalvrt.xsd" })) {
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
