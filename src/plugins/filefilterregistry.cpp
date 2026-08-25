/**
 * @file filefilterregistry.cpp
 * @brief FileFilterRegistry implementation.
 */

#include "plugins/filefilterregistry.h"

#include <openswmm/plugin_sdk/PluginDiscovery.hpp>

#include <QSet>

namespace openswmmvis {

namespace {

QString joinPatterns(const QStringList &patterns)
{
    return patterns.join(QLatin1Char(' '));
}

} // namespace

FileFilterRegistry *FileFilterRegistry::instance()
{
    static FileFilterRegistry s_instance;
    return &s_instance;
}

FileFilterRegistry::FileFilterRegistry(QObject *parent)
    : QObject(parent)
{
    rescan();
}

void FileFilterRegistry::rescan()
{
    entries_.clear();
    registerBuiltinFilters();
    registerEngineFilters();
    emit entriesChanged();
}

void FileFilterRegistry::registerBuiltinFilters()
{
    // Project (.oswp) — read + write are always available.
    entries_.append({tr("SWMMVis Project File"), {QStringLiteral("*.oswp")},
                     FilterKind::ProjectRead, {}, true, false, true});
    entries_.append({tr("SWMMVis Project File"), {QStringLiteral("*.oswp")},
                     FilterKind::ProjectWrite, {}, false, true, true});

    // Results read — engine writes via OUTPUT_WRITE, GUI reads via this kind.
    // The engine's swmm_output_open consumes plain *.out; if a plugin advertises
    // OUTPUT_WRITE for an alternate format that plugin's also expected to
    // register matching ResultsRead support via the GUI plugin SDK (Slice AA-2).
    entries_.append({tr("SWMM Binary Output"), {QStringLiteral("*.out")},
                     FilterKind::ResultsRead, {}, true, false, true});

    // Vector layers — one row per format so Open's filter list is browseable.
    entries_.append({tr("ESRI Shapefile"), {QStringLiteral("*.shp")},
                     FilterKind::VectorRead, {}, true, false, true});
    entries_.append({tr("GeoJSON"), {QStringLiteral("*.geojson"), QStringLiteral("*.json")},
                     FilterKind::VectorRead, {}, true, false, true});
    entries_.append({tr("GeoPackage"), {QStringLiteral("*.gpkg")},
                     FilterKind::VectorRead, {}, true, false, true});
    entries_.append({tr("KML"), {QStringLiteral("*.kml")},
                     FilterKind::VectorRead, {}, true, false, true});
    entries_.append({tr("GML"), {QStringLiteral("*.gml")},
                     FilterKind::VectorRead, {}, true, false, true});

    // Raster layers.
    entries_.append({tr("GeoTIFF"), {QStringLiteral("*.tif"), QStringLiteral("*.tiff")},
                     FilterKind::RasterRead, {}, true, false, true});
    entries_.append({tr("ERDAS Imagine"), {QStringLiteral("*.img")},
                     FilterKind::RasterRead, {}, true, false, true});
    entries_.append({tr("ESRI ASCII Grid"), {QStringLiteral("*.asc")},
                     FilterKind::RasterRead, {}, true, false, true});
    entries_.append({tr("NetCDF"), {QStringLiteral("*.nc")},
                     FilterKind::RasterRead, {}, true, false, true});
    entries_.append({tr("HDF"), {QStringLiteral("*.hdf"), QStringLiteral("*.h5")},
                     FilterKind::RasterRead, {}, true, false, true});

    // Tabular / observed data.
    entries_.append({tr("Comma-Separated Values"), {QStringLiteral("*.csv")},
                     FilterKind::TabularRead, {}, true, false, true});
    entries_.append({tr("Tab-Separated Values"), {QStringLiteral("*.tsv")},
                     FilterKind::TabularRead, {}, true, false, true});
    // .xlsx is conditionally available based on QXlsx; registered by the
    // tabular layer module at startup if linked, not here.

    // Map export.
    entries_.append({tr("Portable Network Graphics"), {QStringLiteral("*.png")},
                     FilterKind::MapExportWrite, {}, false, true, true});
    entries_.append({tr("Scalable Vector Graphics"), {QStringLiteral("*.svg")},
                     FilterKind::MapExportWrite, {}, false, true, true});
    entries_.append({tr("AutoCAD DXF"), {QStringLiteral("*.dxf")},
                     FilterKind::MapExportWrite, {}, false, true, true});
    entries_.append({tr("Enhanced Metafile"), {QStringLiteral("*.emf")},
                     FilterKind::MapExportWrite, {}, false, true, true});
    entries_.append({tr("EPA SWMM Map"), {QStringLiteral("*.map")},
                     FilterKind::MapExportWrite, {}, false, true, true});

    // Process-component config sidecars (G-D1; GUI plan §4.1).
    entries_.append({tr("Reaction System Config"), {QStringLiteral("*.rxn")},
                     FilterKind::ComponentConfigRead, {}, true, false, true});
    entries_.append({tr("ARD Transport Config"), {QStringLiteral("*.ard")},
                     FilterKind::ComponentConfigRead, {}, true, false, true});
    entries_.append({tr("Lagrangian Transport Config"),
                     {QStringLiteral("*.lard")},
                     FilterKind::ComponentConfigRead, {}, true, false, true});
    entries_.append({tr("Heat Transport Config"), {QStringLiteral("*.heat")},
                     FilterKind::ComponentConfigRead, {}, true, false, true});
    entries_.append({tr("Water Age Config"), {QStringLiteral("*.age")},
                     FilterKind::ComponentConfigRead, {}, true, false, true});
    entries_.append({tr("Integrated 2D Config"), {QStringLiteral("*.i2d")},
                     FilterKind::ComponentConfigRead, {}, true, false, true});
}

void FileFilterRegistry::registerEngineFilters()
{
    const auto discovered = openswmm::discover_all_filters();
    for (const auto &d : discovered)
    {
        Entry e;
        e.description = QString::fromStdString(d.filter.description);
        e.kind        = kindForRole(d.filter.role);
        e.pluginId    = QString::fromStdString(d.plugin_id);
        for (const auto &p : d.filter.patterns)
            e.patterns << QString::fromStdString(p);
        if (e.description.isEmpty() || e.patterns.isEmpty())
            continue;

        switch (d.filter.role)
        {
            case openswmm::PluginRole::INPUT_READ:
            case openswmm::PluginRole::STATE_READ:
                e.canRead = true;
                break;
            case openswmm::PluginRole::OUTPUT_WRITE:
            case openswmm::PluginRole::REPORT_WRITE:
            case openswmm::PluginRole::STATE_WRITE:
                e.canWrite = true;
                break;
        }
        entries_.append(e);
    }
}

FilterKind FileFilterRegistry::kindForRole(openswmm::PluginRole role) noexcept
{
    switch (role)
    {
        case openswmm::PluginRole::INPUT_READ:    return FilterKind::InputRead;
        case openswmm::PluginRole::OUTPUT_WRITE:  return FilterKind::ResultsWrite;
        case openswmm::PluginRole::REPORT_WRITE:  return FilterKind::ReportWrite;
        case openswmm::PluginRole::STATE_READ:    return FilterKind::StateRead;
        case openswmm::PluginRole::STATE_WRITE:   return FilterKind::StateWrite;
    }
    return FilterKind::InputRead;
}

QString FileFilterRegistry::filterFor(FilterKind kind) const
{
    QStringList parts;
    QStringList allPatterns;
    QSet<QString> seen;

    for (const auto &e : entries_)
    {
        if (e.kind != kind || !e.enabled) continue;
        parts << QStringLiteral("%1 (%2)").arg(e.description, joinPatterns(e.patterns));
        for (const auto &p : e.patterns)
        {
            if (!seen.contains(p))
            {
                seen.insert(p);
                allPatterns << p;
            }
        }
    }

    if (parts.isEmpty())
        return tr("All Files (*)");

    QStringList result;
    if (allPatterns.size() > 1)
    {
        result << QStringLiteral("%1 (%2)")
                      .arg(tr("All Supported"), joinPatterns(allPatterns));
    }
    result.append(parts);
    result << tr("All Files (*)");
    return result.join(QStringLiteral(";;"));
}

QString FileFilterRegistry::saveAsFilter() const
{
    // Collect entries from InputRead (writable) + ProjectWrite.
    QStringList parts;
    QStringList allPatterns;
    QSet<QString> seen;

    auto addEntries = [&](FilterKind kind) {
        for (const auto &e : entries_) {
            if (e.kind != kind || !e.enabled) continue;
            if (kind == FilterKind::InputRead && !e.canWrite) continue;
            parts << QStringLiteral("%1 (%2)").arg(e.description, joinPatterns(e.patterns));
            for (const auto &p : e.patterns) {
                if (!seen.contains(p)) {
                    seen.insert(p);
                    allPatterns << p;
                }
            }
        }
    };
    addEntries(FilterKind::ProjectWrite);
    addEntries(FilterKind::InputRead);

    if (parts.isEmpty())
        return tr("All Files (*)");

    QStringList result;
    if (allPatterns.size() > 1)
        result << QStringLiteral("%1 (%2)").arg(tr("All Supported"), joinPatterns(allPatterns));
    result.append(parts);
    result << tr("All Files (*)");
    return result.join(QStringLiteral(";;"));
}

QStringList FileFilterRegistry::patternsFor(FilterKind kind) const
{
    QStringList out;
    QSet<QString> seen;
    for (const auto &e : entries_)
    {
        if (e.kind != kind || !e.enabled) continue;
        for (const auto &p : e.patterns)
        {
            if (!seen.contains(p))
            {
                seen.insert(p);
                out << p;
            }
        }
    }
    return out;
}

QString FileFilterRegistry::filterFor(openswmm::PluginRole role) const
{
    return filterFor(kindForRole(role));
}

QStringList FileFilterRegistry::patternsFor(openswmm::PluginRole role) const
{
    return patternsFor(kindForRole(role));
}

QList<FileFilterRegistry::Entry>
FileFilterRegistry::entriesFor(FilterKind kind) const
{
    QList<Entry> out;
    for (const auto &e : entries_)
        if (e.kind == kind) out.append(e);
    return out;
}

const char *FileFilterRegistry::kindLabel(FilterKind kind) noexcept
{
    switch (kind)
    {
        case FilterKind::InputRead:      return "Input (read)";
        case FilterKind::ResultsRead:    return "Results (read)";
        case FilterKind::ResultsWrite:   return "Results (write)";
        case FilterKind::ReportWrite:    return "Report (write)";
        case FilterKind::StateRead:      return "Hot-Start (read)";
        case FilterKind::StateWrite:     return "Hot-Start (write)";
        case FilterKind::ProjectRead:    return "Project (read)";
        case FilterKind::ProjectWrite:   return "Project (write)";
        case FilterKind::VectorRead:     return "Vector (read)";
        case FilterKind::RasterRead:     return "Raster (read)";
        case FilterKind::TabularRead:    return "Tabular (read)";
        case FilterKind::MapExportWrite: return "Map Export (write)";
        case FilterKind::ComponentConfigRead:
            return "Component Config (read)";
    }
    return "Unknown";
}

bool FileFilterRegistry::isWriteKind(FilterKind kind) noexcept
{
    switch (kind)
    {
        case FilterKind::ResultsWrite:
        case FilterKind::ReportWrite:
        case FilterKind::StateWrite:
        case FilterKind::ProjectWrite:
        case FilterKind::MapExportWrite:
            return true;
        default:
            return false;
    }
}

} // namespace openswmmvis
