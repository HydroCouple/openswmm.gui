/*!
 * \file fileopendialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \license GPL-3.0-or-later
 */
#ifndef FILEOPENDIALOG_H
#define FILEOPENDIALOG_H

#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <initializer_list>

namespace openswmmvis::ui {

// Shared by open/import pickers. Keys describe file categories, never translated
// captions, so one picker or locale cannot reset another type's remembered folder.
class FileOpenDialog
{
public:
    static QString startDirectory(const QString &type, const QString &fallback = {})
    {
        const QString saved = QSettings().value(settingKey(type)).toString();
        if (!saved.isEmpty() && QFileInfo(saved).isDir())
            return saved;
        if (!fallback.isEmpty()) {
            const QFileInfo candidate(fallback);
            if (candidate.isDir()) return candidate.absoluteFilePath();
            if (QFileInfo(candidate.absolutePath()).isDir()) return candidate.absolutePath();
        }
        return QDir::homePath();
    }

    static void rememberSelection(const QString &type, const QStringList &paths)
    {
        // A cancelled picker must not erase history. For multiple files, remember
        // the last selected file's folder (normally all selections share a folder).
        for (auto it = paths.crbegin(); it != paths.crend(); ++it) {
            if (it->isEmpty()) continue;
            const QFileInfo file(*it);
            if (!file.isFile()) continue;
            QSettings settings;
            settings.setValue(settingKey(type), file.absolutePath());
            settings.sync();
            return;
        }
    }

    static QString getOpenFileName(const QString &type, QWidget *parent = nullptr,
        const QString &caption = {}, const QString &directory = {},
        const QString &filter = {}, QString *selectedFilter = nullptr,
        QFileDialog::Options options = {})
    {
        const QString path = QFileDialog::getOpenFileName(parent, caption,
            startDirectory(type, directory), filter, selectedFilter, options);
        rememberSelection(type, {path});
        return path;
    }

    static QStringList getOpenFileNames(const QString &type, QWidget *parent = nullptr,
        const QString &caption = {}, const QString &directory = {},
        const QString &filter = {}, QString *selectedFilter = nullptr,
        QFileDialog::Options options = {})
    {
        const QStringList paths = QFileDialog::getOpenFileNames(parent, caption,
            startDirectory(type, directory), filter, selectedFilter, options);
        rememberSelection(type, paths);
        return paths;
    }

    // Generic engine/plugin path editors know their filters rather than a fixed
    // category. Known types share the corresponding main-window picker history.
    static QString typeForFilter(const QString &filter)
    {
        static const QRegularExpression pattern(QStringLiteral(R"(\*\.([\w.\-?*]+))"));
        QStringList extensions;
        auto matches = pattern.globalMatch(filter);
        while (matches.hasNext()) extensions.append(matches.next().captured(1).toLower());
        extensions.removeDuplicates();
        extensions.sort();
        const auto hasAny = [&extensions](std::initializer_list<const char *> types) {
            for (const char *type : types)
                if (extensions.contains(QString::fromLatin1(type))) return true;
            return false;
        };
        if (extensions.contains(QStringLiteral("2dm"))) return QStringLiteral("mesh");
        if (extensions.contains(QStringLiteral("out"))) return QStringLiteral("results-1d");
        if (hasAny({"h5", "hdf5"})) return QStringLiteral("results-2d");
        if (extensions.contains(QStringLiteral("inp")) || extensions.contains(QStringLiteral("oswp")))
            return QStringLiteral("models");
        if (extensions.contains(QStringLiteral("qml")) || extensions.contains(QStringLiteral("swmm-style.json")))
            return QStringLiteral("styles");
        if (hasAny({"tfw", "pgw", "jgw", "bpw", "wld"})) return QStringLiteral("world-files");
        if (hasAny({"tif", "tiff", "asc", "img", "vrt", "nc", "png", "jpg", "jpeg", "bmp"}))
            return QStringLiteral("raster");
        if (hasAny({"shp", "gpkg", "geojson", "gml", "kml", "fgb", "dxf"}))
            return QStringLiteral("vector");
        if (hasAny({"csv", "tsv", "txt", "dat", "tsf"})) return QStringLiteral("timeseries");
        if (extensions.isEmpty()) return QStringLiteral("other-files");
        return QStringLiteral("filter-") + QString::fromLatin1(QCryptographicHash::hash(
            extensions.join(QLatin1Char(';')).toUtf8(), QCryptographicHash::Sha256).toHex());
    }

private:
    static QString settingKey(const QString &type)
    {
        return QStringLiteral("SWMMVis/FileOpenDirectories/")
            + (type.isEmpty() ? QStringLiteral("other-files") : type);
    }
};

} // namespace openswmmvis::ui
#endif // FILEOPENDIALOG_H
