/**
 * @file filefilterregistry.h
 * @brief Builds Qt QFileDialog filter strings from engine plugins + GUI built-ins.
 *
 * @details The engine SDK's plugin discovery (`openswmm::discover_all_filters()`)
 *          covers I/O that the engine itself performs — reading model input,
 *          writing time-series output, summary reports, hot-start state. The
 *          GUI also needs filters for things the engine never touches:
 *          project files (`.oswp`), GIS vector / raster layers, tabular
 *          observed-data files, and map exports (PNG / SVG / DXF / …).
 *
 *          This registry layers a GUI-side @ref FilterKind enum over
 *          @ref openswmm::PluginRole — engine-backed kinds proxy the engine
 *          discovery, GUI-only kinds come from a built-in static table.
 *          A back-compat overload accepts the engine `PluginRole` directly so
 *          older call sites keep compiling.
 *
 *          Singleton with an `entriesChanged()` signal so future
 *          enable / disable / add-plugin features (Slice AA-2) can mutate
 *          the table without reconstructing dialogs.
 *
 * @see <openswmm/plugin_sdk/PluginDiscovery.hpp>
 */

#ifndef OPENSWMM_GUI_FILE_FILTER_REGISTRY_H
#define OPENSWMM_GUI_FILE_FILTER_REGISTRY_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <openswmm/plugin_sdk/IPluginComponentInfo.hpp>

namespace openswmmvis {

/**
 * @brief GUI-side filter category. Superset of @ref openswmm::PluginRole.
 */
enum class FilterKind {
    InputRead,        ///< SWMM model input (`*.inp`, …). Engine-backed.
    ResultsRead,      ///< SWMM binary output (`*.out`). GUI-only — engine writes, GUI reads.
    ResultsWrite,     ///< SWMM binary output (`*.out`, …). Engine-backed (`OUTPUT_WRITE`).
    ReportWrite,      ///< SWMM summary report (`*.rpt`). Engine-backed (`REPORT_WRITE`).
    StateRead,        ///< Hot-start input. Engine-backed (`STATE_READ`).
    StateWrite,       ///< Hot-start output. Engine-backed (`STATE_WRITE`).
    ProjectRead,      ///< OpenSWMM project sidecar (`*.oswp`). GUI-only.
    ProjectWrite,     ///< OpenSWMM project sidecar (`*.oswp`). GUI-only.
    VectorRead,       ///< GIS vector layer (Shapefile, GeoJSON, GeoPackage, …). GUI-only.
    RasterRead,       ///< GIS raster layer (GeoTIFF, ASCII grid, NetCDF, …). GUI-only.
    TabularRead,      ///< Observed / tabular data (`*.csv`, `*.tsv`, …). GUI-only.
    MapExportWrite    ///< Map export (`*.png`, `*.svg`, `*.dxf`, `*.emf`, …). GUI-only.
};

/**
 * @brief Process-wide registry of file filters, populated from engine plugins + built-ins.
 */
class FileFilterRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief One row in the registry.
     */
    struct Entry
    {
        QString     description;     ///< Human-readable name, e.g. "SWMM Input File".
        QStringList patterns;        ///< Glob patterns, e.g. {"*.inp"}.
        FilterKind  kind = FilterKind::InputRead;
        QString     pluginId;        ///< Empty for built-in entries.
        bool        canRead  = false;
        bool        canWrite = false;
        bool        enabled  = true;
    };

    /**
     * @brief Process-wide singleton.
     */
    static FileFilterRegistry *instance();

    /**
     * @brief Qt filter string for a kind. "All Supported" first, "All Files (*)" last.
     */
    QString filterFor(FilterKind kind) const;

    /**
     * @brief Combined filter string for the Save As dialog.
     *
     * @details Merges all writable SWMM input formats (InputRead entries with
     *          canWrite == true) and all project formats (ProjectWrite entries)
     *          into one Qt filter string, with a combined "All Supported" entry
     *          first and "All Files (*)" last.  Use this for the single
     *          File → Save As dialog so the user can pick any project-level
     *          output format from one dropdown.
     */
    QString saveAsFilter() const;

    /**
     * @brief Glob patterns for a kind, deduplicated.
     */
    QStringList patternsFor(FilterKind kind) const;

    /**
     * @brief Engine-`PluginRole` overload. Maps role → FilterKind then dispatches.
     *
     * @note Kept so existing call sites
     *       (e.g. `kFilters.filterFor(openswmm::PluginRole::INPUT_READ)`)
     *       compile unchanged after this slice. New code should use the
     *       FilterKind overload.
     */
    QString filterFor(openswmm::PluginRole role) const;
    QStringList patternsFor(openswmm::PluginRole role) const;

    /**
     * @brief Snapshot of all entries — for the Plugins dialog.
     */
    QList<Entry> allEntries() const { return entries_; }

    /**
     * @brief Snapshot filtered by kind.
     */
    QList<Entry> entriesFor(FilterKind kind) const;

    /**
     * @brief Rebuild from engine discovery + built-in tables. Idempotent.
     *
     * @details Future Slice AA-2 calls this after enable / disable / install.
     *          Emits @ref entriesChanged().
     */
    void rescan();

    /**
     * @brief Stable display label for a kind (for menus, dialogs, logs).
     */
    static const char *kindLabel(FilterKind kind) noexcept;

    /**
     * @brief Project sentinel kind for filters that participate in a "Save As"
     *        decision (`Project` / `Input` / `Map Export`). For UI grouping.
     */
    static bool isWriteKind(FilterKind kind) noexcept;

signals:
    void entriesChanged();

private:
    explicit FileFilterRegistry(QObject *parent = nullptr);

    void registerBuiltinFilters();
    void registerEngineFilters();

    static FilterKind kindForRole(openswmm::PluginRole role) noexcept;

    QList<Entry> entries_;
};

} // namespace openswmmvis

#endif // OPENSWMM_GUI_FILE_FILTER_REGISTRY_H
