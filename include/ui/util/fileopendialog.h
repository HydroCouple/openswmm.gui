/**
 * @file      fileopendialog.h
 * @brief     File Open Dialog interface and supporting types.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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

namespace openswmmvis::ui
{

// Shared by open/import pickers. Keys describe file categories, never translated
// captions, so one picker or locale cannot reset another type's remembered folder.
class FileOpenDialog
{
public:
    /**
     * @brief Choose the most applicable existing directory for an open/import category.
     * @param[in] type Stable untranslated category key; models first consults the preferred project-open directory.
     * @param[in] fallback Candidate directory or filename whose parent may supply a fallback.
     * @return Preferred project folder, remembered category folder, valid fallback/parent, or home directory. No settings are written.
     */
    static QString startDirectory(const QString &type, const QString &fallback = {})
    {
        if (type == QStringLiteral("models")) {
            const QString preferred =
                QSettings()
                    .value(QStringLiteral("SWMMVis/Preferences/General/ProjectOpenDirectory"))
                    .toString();
            if (!preferred.isEmpty() && QFileInfo(preferred).isDir())
                return preferred;
        }
        const QString saved = QSettings().value(settingKey(type)).toString();
        if (!saved.isEmpty() && QFileInfo(saved).isDir())
            return saved;
        if (!fallback.isEmpty()) {
            const QFileInfo candidate(fallback);
            if (candidate.isDir())
                return candidate.absoluteFilePath();
            if (QFileInfo(candidate.absolutePath()).isDir())
                return candidate.absolutePath();
        }
        return QDir::homePath();
    }

    /**
     * @brief Persist the parent of the last nonempty selected regular file.
     * @param[in] type Stable category key; empty uses other-files.
     * @param[in] paths Selection list traversed backward; missing/nonregular paths are skipped and cancellation preserves history.
     * @note Writes and synchronizes QSettings without separately reporting persistence errors.
     */
    static void rememberSelection(const QString &type, const QStringList &paths)
    {
        // A cancelled picker must not erase history. For multiple files, remember
        // the last selected file's folder (normally all selections share a folder).
        for (auto it = paths.crbegin(); it != paths.crend(); ++it) {
            if (it->isEmpty())
                continue;
            const QFileInfo file(*it);
            if (!file.isFile())
                continue;
            QSettings settings;
            settings.setValue(settingKey(type), file.absolutePath());
            settings.sync();
            return;
        }
    }

    /**
     * @brief Open the standard modal file picker with per-category folder memory.
     * @param[in] type Stable untranslated history category.
     * @param[in] parent Optional owner for the dialog; call on the GUI thread.
     * @param[in] caption Dialog title.
     * @param[in] directory Fallback initial directory/filename after preferred/remembered locations.
     * @param[in] filter Qt filename filters, optionally empty.
     * @param[in,out] selectedFilter Optional initial/returned active filter string.
     * @param[in] options QFileDialog options forwarded unchanged.
     * @return Selected filename or empty on cancellation.
     * @note Remembers the accepted regular file's folder through QSettings; does not open/read the selected file contents.
     */
    static QString getOpenFileName(const QString &type, QWidget *parent = nullptr,
                                   const QString &caption = {}, const QString &directory = {},
                                   const QString &filter = {}, QString *selectedFilter = nullptr,
                                   QFileDialog::Options options = {})
    {
        const QString path = QFileDialog::getOpenFileName(
            parent, caption, startDirectory(type, directory), filter, selectedFilter, options);
        rememberSelection(type, {path});
        return path;
    }

    /**
     * @brief Open the standard modal file picker with per-category folder memory.
     * @param[in] type Stable untranslated history category.
     * @param[in] parent Optional owner for the dialog; call on the GUI thread.
     * @param[in] caption Dialog title.
     * @param[in] directory Fallback initial directory/filename after preferred/remembered locations.
     * @param[in] filter Qt filename filters, optionally empty.
     * @param[in,out] selectedFilter Optional initial/returned active filter string.
     * @param[in] options QFileDialog options forwarded unchanged.
     * @return Selected filenames or empty on cancellation.
     * @note Remembers the accepted regular file's folder through QSettings; does not open/read the selected file contents.
     */
    static QStringList getOpenFileNames(const QString &type, QWidget *parent = nullptr,
                                        const QString &caption = {}, const QString &directory = {},
                                        const QString &filter = {},
                                        QString *selectedFilter = nullptr,
                                        QFileDialog::Options options = {})
    {
        const QStringList paths = QFileDialog::getOpenFileNames(
            parent, caption, startDirectory(type, directory), filter, selectedFilter, options);
        rememberSelection(type, paths);
        return paths;
    }

    // Generic engine/plugin path editors know their filters rather than a fixed
    // category. Known types share the corresponding main-window picker history.
    /**
     * @brief Map a filename filter to a shared category or deterministic extension-set key.
     * @param[in] filter Qt filter text whose *.extension patterns are extracted/lowercased/deduplicated/sorted.
     * @return First matching category by fixed precedence (mesh, results, models, styles, world/raster/vector/series), other-files for no patterns, or filter- plus a SHA-256 extension-set digest.
     */
    static QString typeForFilter(const QString &filter)
    {
        static const QRegularExpression pattern(QStringLiteral(R"(\*\.([\w.\-?*]+))"));
        QStringList extensions;
        auto matches = pattern.globalMatch(filter);
        while (matches.hasNext())
            extensions.append(matches.next().captured(1).toLower());
        extensions.removeDuplicates();
        extensions.sort();
        const auto hasAny = [&extensions](std::initializer_list<const char *> types) {
            for (const char *type : types)
                if (extensions.contains(QString::fromLatin1(type)))
                    return true;
            return false;
        };
        if (extensions.contains(QStringLiteral("2dm")))
            return QStringLiteral("mesh");
        if (extensions.contains(QStringLiteral("out")))
            return QStringLiteral("results-1d");
        if (hasAny({"h5", "hdf5"}))
            return QStringLiteral("results-2d");
        if (extensions.contains(QStringLiteral("inp")) ||
            extensions.contains(QStringLiteral("oswp")))
            return QStringLiteral("models");
        if (extensions.contains(QStringLiteral("qml")) ||
            extensions.contains(QStringLiteral("swmm-style.json")))
            return QStringLiteral("styles");
        if (hasAny({"tfw", "pgw", "jgw", "bpw", "wld"}))
            return QStringLiteral("world-files");
        if (hasAny({"tif", "tiff", "asc", "img", "vrt", "nc", "png", "jpg", "jpeg", "bmp"}))
            return QStringLiteral("raster");
        if (hasAny({"shp", "gpkg", "geojson", "gml", "kml", "fgb", "dxf"}))
            return QStringLiteral("vector");
        if (hasAny({"csv", "tsv", "txt", "dat", "tsf"}))
            return QStringLiteral("timeseries");
        if (extensions.isEmpty())
            return QStringLiteral("other-files");
        return QStringLiteral("filter-") +
               QString::fromLatin1(
                   QCryptographicHash::hash(extensions.join(QLatin1Char(';')).toUtf8(),
                                            QCryptographicHash::Sha256)
                       .toHex());
    }

private:
    /**
     * @brief Construct the per-category remembered-folder settings path.
     * @param[in] type Untranslated category key; empty becomes other-files.
     * @return SWMMVis/FileOpenDirectories/ followed by the category token.
     */
    static QString settingKey(const QString &type)
    {
        return QStringLiteral("SWMMVis/FileOpenDirectories/") +
               (type.isEmpty() ? QStringLiteral("other-files") : type);
    }
};

} // namespace openswmmvis::ui
#endif // FILEOPENDIALOG_H
