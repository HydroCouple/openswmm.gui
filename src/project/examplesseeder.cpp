/*!
 * \file   examplesseeder.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * See examplesseeder.h for the seeding/discovery contract.
 */
#include "project/examplesseeder.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>

namespace {
Q_LOGGING_CATEGORY(lcExamples, "openswmm.examples")

QString prettifyBaseName(QString base)
{
    base.replace(QLatin1Char('_'), QLatin1Char(' '));
    base.replace(QLatin1Char('-'), QLatin1Char(' '));
    return base.simplified();
}

// First .oswp (preferred) else first .inp in \a dir, empty when neither.
QString openTargetIn(const QDir &dir)
{
    const QStringList oswp =
        dir.entryList({QStringLiteral("*.oswp")}, QDir::Files, QDir::Name);
    if (!oswp.isEmpty()) return dir.absoluteFilePath(oswp.first());
    const QStringList inp =
        dir.entryList({QStringLiteral("*.inp")}, QDir::Files, QDir::Name);
    if (!inp.isEmpty()) return dir.absoluteFilePath(inp.first());
    return {};
}

} // namespace

namespace openswmmvis::project::examples {

QString seedMarkerFileName()
{
    return QStringLiteral(".seeded_version");
}

QString installExamplesDir()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/../share/openswmmgui/examples"),
        appDir + QStringLiteral("/../Resources/examples"),
        appDir + QStringLiteral("/examples"),
    };
    for (const QString &c : candidates)
        if (QFileInfo(c).isDir())
            return QDir(c).absolutePath();
    return {};
}

QString appDataExamplesDir()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) return {};
    return base + QStringLiteral("/examples");
}

bool copyDirectoryRecursively(const QString &srcDir, const QString &dstDir,
                              QString *err)
{
    const QDir src(srcDir);
    if (!src.exists()) {
        if (err) *err = QStringLiteral("Source directory does not exist: %1")
                            .arg(srcDir);
        return false;
    }
    if (!QDir().mkpath(dstDir)) {
        if (err) *err = QStringLiteral("Could not create directory: %1")
                            .arg(dstDir);
        return false;
    }

    const QFileInfoList entries = src.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
        QDir::Name);
    for (const QFileInfo &fi : entries) {
        if (fi.fileName() == seedMarkerFileName()) continue;
        const QString dstPath = dstDir + QLatin1Char('/') + fi.fileName();
        if (fi.isDir()) {
            if (!copyDirectoryRecursively(fi.absoluteFilePath(), dstPath, err))
                return false;
            continue;
        }
        const QFileInfo dstFi(dstPath);
        if (dstFi.exists()) {
            // Cheap no-op when the destination already matches — keeps
            // re-seeding fast and leaves matching files' mtimes alone.
            if (dstFi.size() == fi.size()
                && dstFi.lastModified() == fi.lastModified())
                continue;
            if (!QFile::remove(dstPath)) {
                if (err) *err = QStringLiteral("Could not replace file: %1")
                                    .arg(dstPath);
                return false;
            }
        }
        if (!QFile::copy(fi.absoluteFilePath(), dstPath)) {
            if (err) *err = QStringLiteral("Could not copy %1 to %2")
                                .arg(fi.absoluteFilePath(), dstPath);
            return false;
        }
        // QFile::copy stamps the copy with "now" — carry the source mtime
        // over so the size+mtime no-op check above holds on the next sync.
        QFile copied(dstPath);
        if (copied.open(QIODevice::ReadWrite)) {
            copied.setFileTime(fi.lastModified(),
                               QFileDevice::FileModificationTime);
        }
    }
    return true;
}

bool syncFromInstall(const QString &srcDir, const QString &dstDir,
                     const QString &version, QString *err)
{
    const QString markerPath = dstDir + QLatin1Char('/') + seedMarkerFileName();
    {
        QFile marker(markerPath);
        if (marker.open(QIODevice::ReadOnly)
            && QString::fromUtf8(marker.readAll()).trimmed() == version.trimmed())
            return true;  // already seeded for this version
    }

    if (!copyDirectoryRecursively(srcDir, dstDir, err))
        return false;

    QFile marker(markerPath);
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QStringLiteral("Could not write seed marker: %1")
                            .arg(markerPath);
        return false;
    }
    marker.write(version.trimmed().toUtf8());
    return true;
}

QString preferredExamplesDir(const QString &version)
{
    const QString src = installExamplesDir();
    if (src.isEmpty()) return {};

    const QString dst = appDataExamplesDir();
    if (dst.isEmpty()) {
        qCWarning(lcExamples) << "No writable app-data location; scanning the"
                                 " install payload directly:" << src;
        return src;
    }
    QString err;
    if (!syncFromInstall(src, dst, version, &err)) {
        qCWarning(lcExamples) << "Example seeding failed (" << err
                              << ") — scanning the install payload directly:"
                              << src;
        return src;
    }
    return dst;
}

QVector<ExampleInfo> discoverExamples(const QString &dirPath)
{
    QVector<ExampleInfo> out;
    const QDir dir(dirPath);
    if (!dir.exists()) return out;

    // Directory-per-example — one entry per subdir with an openable target.
    const QFileInfoList subdirs =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &sub : subdirs) {
        const QDir subDir(sub.absoluteFilePath());
        const QString openPath = openTargetIn(subDir);
        if (openPath.isEmpty()) continue;

        ExampleInfo info;
        info.isDirectory = true;
        info.sourceRoot  = subDir.absolutePath();
        info.openPath    = openPath;
        info.displayName = prettifyBaseName(sub.fileName());

        QFile manifest(subDir.absoluteFilePath(QStringLiteral("example.json")));
        if (manifest.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(manifest.readAll()).object();
            const QString name = o.value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) info.displayName = name;
            info.description = o.value(QStringLiteral("description")).toString();
        }
        out.append(info);
    }

    // Legacy flat single-file examples at the top level.
    const QFileInfoList inps =
        dir.entryInfoList({QStringLiteral("*.inp")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : inps) {
        ExampleInfo info;
        info.isDirectory = false;
        info.sourceRoot  = fi.absoluteFilePath();
        info.openPath    = fi.absoluteFilePath();
        info.displayName = prettifyBaseName(fi.baseName());
        out.append(info);
    }
    return out;
}

} // namespace openswmmvis::project::examples
