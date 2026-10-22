/**
 * @file      filefilterregistry.h
 * @brief     Builds Qt QFileDialog filter strings from engine plugins + GUI built-ins.
 *
 * @details The engine SDK's plugin discovery (`openswmm::discover_all_filters()`)
 *          covers I/O that the engine itself performs — reading model input,
 *          writing time-series output, summary reports, hot-start state. The
 *          GUI also needs filters for things the engine never touches:
 *          project files (`.oswp`), GIS vector / raster layers, tabular
 *          observed-data files, and map exports (PNG / SVG / DXF / …).
 *
 *          This registry layers a GUI-side @ref openswmmvis::FilterKind enum over
 *          `openswmm::PluginRole` — engine-backed kinds proxy the engine
 *          discovery, GUI-only kinds come from a built-in static table.
 *          A back-compat overload accepts the engine `PluginRole` directly so
 *          older call sites keep compiling.
 *
 *          Singleton with an `entriesChanged()` signal so future
 *          enable / disable / add-plugin features (Slice AA-2) can mutate
 *          the table without reconstructing dialogs.
 *
 * @see <openswmm/plugin_sdk/PluginDiscovery.hpp>
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_GUI_FILE_FILTER_REGISTRY_H
#define OPENSWMM_GUI_FILE_FILTER_REGISTRY_H

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <openswmm/plugin_sdk/IPluginComponentInfo.hpp>

namespace openswmmvis
{

/**
 * @brief GUI-side filter category. Superset of `openswmm::PluginRole`.
 */
enum class FilterKind {
    InputRead,          ///< SWMM model input (`*.inp`, …). Engine-backed.
    ResultsRead,        ///< SWMM binary output (`*.out`). GUI-only — engine writes, GUI reads.
    ResultsWrite,       ///< SWMM binary output (`*.out`, …). Engine-backed (`OUTPUT_WRITE`).
    ReportWrite,        ///< SWMM summary report (`*.rpt`). Engine-backed (`REPORT_WRITE`).
    StateRead,          ///< Hot-start input. Engine-backed (`STATE_READ`).
    StateWrite,         ///< Hot-start output. Engine-backed (`STATE_WRITE`).
    ProjectRead,        ///< SWMMVis project sidecar (`*.oswp`). GUI-only.
    ProjectWrite,       ///< SWMMVis project sidecar (`*.oswp`). GUI-only.
    VectorRead,         ///< GIS vector layer (Shapefile, GeoJSON, GeoPackage, …). GUI-only.
    RasterRead,         ///< GIS raster layer (GeoTIFF, ASCII grid, NetCDF, …). GUI-only.
    TabularRead,        ///< Observed / tabular data (`*.csv`, `*.tsv`, …). GUI-only.
    MapExportWrite,     ///< Map export (`*.png`, `*.svg`, `*.dxf`, `*.emf`, …). GUI-only.
    ComponentConfigRead ///< Process-component config sidecars (`*.rxn`, `*.ard`, `*.lard`, `*.heat`, `*.age`, `*.i2d`). GUI-only (G-D1).
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
    struct Entry {
        QString description;  ///< Human-readable name, e.g. "SWMM Input File".
        QStringList patterns; ///< Glob patterns, e.g. {"*.inp"}.
        FilterKind kind = FilterKind::InputRead;
        QString pluginId; ///< Empty for built-in entries.
        bool canRead = false;
        bool canWrite = false;
        bool enabled = true;
    };

    /**
     * @brief Process-wide singleton.
     * @return Borrowed process-lifetime singleton, populated on first access; initialize/use on the GUI thread because entries/QObject state are unsynchronized.
     */
    static FileFilterRegistry *instance();

    /**
     * @brief Qt filter string for a kind. "All Supported" first, "All Files (*)" last.
     * @param[in] kind Requested GUI filter category.
     * @return Enabled per-entry Qt filters plus All Files; combined All Supported appears only when more than one unique glob exists.
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
     * @return Enabled project-write entries followed by writable input entries, combined only when multiple patterns exist, ending in All Files. Performs no file writes.
     */
    QString saveAsFilter() const;

    /**
     * @brief Lower-case extensions (no dot) the Save As dialog can produce:
     *        every writable InputRead pattern plus every ProjectWrite pattern.
     *
     * @details This is the set `normalizeSaveAsPath()` collapses duplicate
     *          suffixes over, so it must be built from the same entries as
     *          @ref saveAsFilter — a format offered in the dropdown but missing
     *          here is re-appended by the normalizer (`model.inp.inp`).
     * @return Owning lowercase pattern suffix set for enabled Save As entries, stripping an initial *. when present.
     */
    QSet<QString> saveAsExtensions() const;

    /**
     * @brief True for the engine's own built-in plugins
     *        (`org.hydrocouple.openswmm.builtin.*`).
     *
     * @details Built-in input entries are writable Save As targets, but the
     *          GUI writes them with the staged built-in writer
     *          (`swmm_model_write_staged`, empty plugin id), never through
     *          `swmm_model_write_with_plugin`.
     * @param[in] pluginId Plugin identifier matched against the case-sensitive built-in namespace prefix.
     * @return True for an identifier beginning org.hydrocouple.openswmm.builtin.; does not query plugin discovery.
     */
    static bool isBuiltinPlugin(const QString &pluginId) noexcept;

    /**
     * @brief Glob patterns for a kind, deduplicated.
     * @param[in] kind GUI category to include.
     * @return Enabled glob patterns with exact duplicates removed in first-encounter order; performs no driver/file probing.
     */
    QStringList patternsFor(FilterKind kind) const;

    /**
     * @brief Engine-`PluginRole` overload. Maps role → FilterKind then dispatches.
     *
     * @note Kept so existing call sites
     *       (e.g. `kFilters.filterFor(openswmm::PluginRole::INPUT_READ)`)
     *       compile unchanged after this slice. New code should use the
     *       FilterKind overload.
     * @param[in] role Engine plugin role mapped to a GUI category; unsupported values fall back to InputRead.
     * @return Same enabled Qt filter as filterFor(FilterKind).
     */
    QString filterFor(openswmm::PluginRole role) const;
    /**
     * @brief Map an engine role and copy its enabled glob patterns.
     * @param[in] role Plugin role; unsupported values fall back to InputRead.
     * @return Exact-deduplicated patterns in discovery/registration order.
     */
    QStringList patternsFor(openswmm::PluginRole role) const;

    /**
     * @brief Snapshot of all entries — for the Plugins dialog.
     * @return Owning copy including disabled entries and current plugin metadata.
     */
    QList<Entry> allEntries() const { return entries_; }

    /**
     * @brief Snapshot filtered by kind.
     * @param[in] kind GUI category to filter by.
     * @return Copy of all matching entries, including disabled ones, in registration order.
     */
    QList<Entry> entriesFor(FilterKind kind) const;

    /**
     * @brief Rebuild from engine discovery + built-in tables. Idempotent.
     *
     * @details Future Slice AA-2 calls this after enable / disable / install.
     *          Emits @ref entriesChanged().
     * @details Clears the list, appends GUI built-ins, appends discovered engine filters, and unconditionally emits entriesChanged(). This does not check whether each advertised GIS driver is installed or open files.
     */
    void rescan();

    /**
     * @brief Stable display label for a kind (for menus, dialogs, logs).
     * @param[in] kind GUI filter category.
     * @return Borrowed static English display label, or Unknown for an unsupported value; caller translates if needed.
     */
    static const char *kindLabel(FilterKind kind) noexcept;

    /**
     * @brief Classify write-role categories for UI grouping.
     * @param[in] kind GUI filter category.
     * @return True only for ResultsWrite, ReportWrite, StateWrite, ProjectWrite, or MapExportWrite; writable InputRead entries remain categorized as read kinds.
     */
    static bool isWriteKind(FilterKind kind) noexcept;

signals:
    /**
     * @brief Notify observers after a complete registry rebuild, including identical rescans.
     */
    void entriesChanged();

private:
    /**
     * @brief Construct and immediately populate the singleton filter list.
     * @param[in] parent Optional QObject owner; singleton construction supplies none.
     */
    explicit FileFilterRegistry(QObject *parent = nullptr);

    /**
     * @brief Append static GUI project/results/GIS/tabular/map/config filter entries.
     * @details Does not clear prior entries, check GDAL capabilities, or emit notifications independently.
     */
    void registerBuiltinFilters();
    /**
     * @brief Append discovered engine plugin filters with nonempty description/patterns.
     * @details Input plugins advertise read and write, state readers read, other known roles write. Does not clear previous entries or emit independently.
     */
    void registerEngineFilters();

    /**
     * @brief Map engine plugin roles to GUI filter categories.
     * @param[in] role Engine role.
     * @return Corresponding category, falling back to InputRead for unsupported values.
     */
    static FilterKind kindForRole(openswmm::PluginRole role) noexcept;

    QList<Entry> entries_;
};

} // namespace openswmmvis

#endif // OPENSWMM_GUI_FILE_FILTER_REGISTRY_H
