/**
 * @file      exportlayerdialog.h
 * @brief     One export dialog for every source the layer tree exports
 *            (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §8.3, R8–R10).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * One export dialog for every source the layer tree exports
 * (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §8.3, R8–R10).
 *
 * A VIEW only: it collects the format, destination, CRS and options, asks
 * before overwriting, and hands them back. SWMMVis builds the tables, runs
 * io/vectorexport on a worker and logs the report.
 *
 *   - Format: the vector formats this build can write
 *     (gdalcaps::vectorWriteFormats(), GeoPackage first — Q8); a raster
 *     export is always GeoTIFF (Q10).
 *   - Destination: a file for a format that holds several layers, otherwise
 *     a folder receiving one file per table.
 *   - CRS: the source's by default, or another picked with the CRS dialog;
 *     GeoJSON and KML say they are written in WGS 84.
 *   - Options: selected only; the run's statistics (SWMM); add to the map.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef EXPORTLAYERDIALOG_H
#define EXPORTLAYERDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace openswmmvis::ui
{

/*! One output the dialog lists: an object type or a mesh table. */
struct ExportItem {
    int id = 0;    ///< SWMMModelLayer::Category, or a MeshExportPart bit.
    QString label; ///< "Conduits".
    QString stem;  ///< Output file / layer stem, "conduits".
    bool checked = true;
};

/*! What the dialog is exporting. */
struct ExportDialogSetup {
    enum class Mode { SwmmObjects, VectorLayer, Mesh, Raster };
    Mode mode = Mode::VectorLayer;
    QString title;
    QString sourceName; ///< One line naming the source.
    /*! The outputs; a checklist is shown for SwmmObjects and Mesh. */
    QVector<ExportItem> items;
    bool hasSelection = false; ///< Enables "Selected features only".
    bool canIncludeResults = false;
    bool includeResults = false;
    QString sourceSrsWkt;
    QString sourceCrsLabel; ///< e.g. "EPSG:25832"; empty = "none".
    QString defaultDir;
    QString defaultBaseName;
    /*! Files the export must never replace: every file open in the project
     *  (the model, its results, each layer's source) and the project's
     *  features GeoPackage. */
    QStringList protectedPaths;
};

class ExportLayerDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] setup Copied source context/items/protected paths and initial option capabilities;
     *   changes to caller setup afterward are not followed.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Collect format/destination/output/CRS/options without exporting, opening source or
     *   starting worker.
     * @note SWMM/Mesh show item checklist; other modes return all supplied item IDs regardless of
     *   initial checked flag. Raster forces GTiff and hides selection/add-to-map; results checkbox
     *   can appear whenever canIncludeResults is true. Source CRS LABEL is displayed, sourceSrsWkt
     *   is not parsed/used by this form. GUI-thread only, no live selection/source-file
     *   subscription.
     */
    explicit ExportLayerDialog(const ExportDialogSetup &setup, QWidget *parent = nullptr);

    /*! GDAL driver short name; "GTiff" in raster mode.
     * @return Current GDAL driver item-data string copy, GTiff for raster; empty if no writable
     *   vector formats. No capability refresh or acceptance check.
 */
    [[nodiscard]] QString driver() const;
    /**
     * @return Trimmed editor text with native separators normalized, without canonicalization,
     *   extension append, creation, validation or acceptance check. Relative paths remain relative.
     */
    [[nodiscard]] QString destination() const;
    /*! The CRS to write in; empty keeps the source CRS.
     * @return Stored picked CRS WKT override, initially empty meaning use source. Does NOT
     *   substitute forced WGS84 or clear override when selected driver requires it; downstream
     *   exporter interprets driver requirement. No owned SRS object/transform or acceptance check.
 */
    [[nodiscard]] QString targetSrsWkt() const { return m_targetWkt; }
    /*! The ids of the ticked outputs (all items when no checklist is shown).
     * @return Exactly checked checklist row IDs in setup order; with no checklist return ALL
     *   supplied IDs, ignoring ExportItem.checked. Duplicates retained, empty setup yields empty
     *   and fails validation. No source/query/acceptance check.
 */
    [[nodiscard]] QList<int> checkedItems() const;
    /**
     * @return Current selected-features checkbox state. False when absent in raster. Disabled
     *   checkbox can still be programmatically checked; getter does not inspect isEnabled or live
     *   map selection. Does not require accepted result.
     */
    [[nodiscard]] bool selectedOnly() const;
    /**
     * @return Current include-statistics checkbox state. False when absent; otherwise raw checked
     *   state, with no current output availability validation. Does not require accepted result.
     */
    [[nodiscard]] bool includeResults() const;
    /**
     * @return Current add-exported-layers checkbox state. False when absent for raster; otherwise
     *   raw checked state, no layer creation here. Does not require accepted result.
     */
    [[nodiscard]] bool addToMap() const;

    // ----- Test seams ----------------------------------------------------
    /**
     * @param[in] driver Exact item-data driver identifier; missing value is no-op, no insertion or
     *   case normalization.
     * @brief Select matching available driver, allowing connected format handler to adjust auto
     *   destination/CRS hints.
     * @note Programmatic selection ignores combo enabled state; raster has only GTiff. No data
     *   export or persistence.
     */
    void setFormat(const QString &driver);
    /**
     * @param[in] path Raw copied text, trim/separator normalization deferred to destination getter.
     * @brief Set editor and permanently mark destination explicitly edited so future format changes
     *   do not replace it.
     * @note Empty is allowed and still sets edited flag. No existence/extension validation,
     *   settings write or file creation.
     */
    void setDestination(const QString &path);
    /**
     * @param[in] id Raw ID matched against ALL checklist rows; duplicate IDs all updated.
     * @param[in] on True Checked, false Unchecked.
     * @brief Update matching checklist state; no-checklist/missing ID no-op. Does not change setup
     *   snapshot, validate target or export.
     */
    void setItemChecked(int id, bool on);
    /*! Why Export would be refused (no destination, nothing ticked, a folder
     *  where a file is written, a protected file in the way), or empty.
     * @return First translated refusal for zero driver choices, empty normalized destination, no
     *   checked IDs, existing directory where single container expected, or protected target
     *   collision; empty otherwise.
     * @note Existing .gdb directory allowed only for gdb extension. Per-table folder destination is
     *   not independently checked as directory; does not check parent writability/existence,
     *   extension, duplicate stems, CRS/geometry, source validity, companion files or races. Target
     *   versus protected checks canonical/equality/prefix paths with platform case rule; no files
     *   changed.
 */
    [[nodiscard]] QString validationError() const;
    /*! The files this export would create or replace.
     * @return One normalized destination for container/GTiff mode, otherwise each setup item
     *   stem.extension inside destination whose ID occurs in checked list. Setup order/duplicate
     *   IDs/stems retained; not a sidecar/companion-file manifest.
     * @note Does not validate empty/relative destination, sanitize stem, create folders or inspect
     *   driver output. Used for preliminary protection/overwrite prompt, actual exporter can
     *   produce other format files.
 */
    [[nodiscard]] QStringList targetPaths() const;

public slots:
    /**
     * @brief Validate current options, warn on first refusal, then confirm existing predicted paths
     *   before accepting.
     * @note Cancel/no leaves open. Checks QFileInfo::exists only, not broken-symlink companions; no
     *   fingerprint capture, after-prompt race recheck, export, deletion, source/model mutation or
     *   settings persistence. Downstream worker must validate actual targets/publication again.
     *   Accepted getters are values only, no success-of-export guarantee.
     */
    void accept() override;

private slots:
    /**
     * @brief Refresh driver-specific format note, auto default destination only while never
     *   manually edited, then CRS label/controls.
     * @note Explicit destination retains potentially old extension/path across format changes.
     *   GTiff note describes nearest-neighbor reprojection performed downstream; this handler does
     *   no raster work. Stored CRS override retained.
     */
    void onFormatChanged();
    /**
     * @brief Choose a save-file/container destination without native overwrite confirmation, or
     *   existing folder for per-table format.
     * @note For container file append expected extension only when chosen suffix absent;
     *   mismatching suffix retained. Cancellation leaves text/edited state unchanged. Successful
     *   choice marks edited and displays native separators; this is path collection only, no
     *   export/overwrite or preferences save.
     */
    void onBrowse();
    /**
     * @brief Run child CRS picker, allocate selected caller-owned SRS temporarily, store its
     *   WKT/authority label and release it.
     * @note Rejected/uncreatable selection leaves prior override. Does not preselect current/source
     *   CRS, validate transformability or reproject data. Normal UI disabled for forced WGS84,
     *   direct invocation itself has no driver gate.
     */
    void onChooseCrs();
    /**
     * @brief Clear picked WKT/label and refresh display, leaving source unchanged.
     * @note Does not copy setup source WKT, create SRS, transform data or check forced-WGS84
     *   driver; display helper handles its presentation.
     */
    void onUseSourceCrs();

private:
    /**
     * @return True for GTiff (single file) or selected vector driver metadata.multiLayer, false
     *   otherwise. Here means one container destination, not raster band count or actual output
     *   layer count. No datasource inspection.
     */
    [[nodiscard]] bool multiLayer() const;
    /**
     * @return tif for GTiff, otherwise driver metadata extension; no dot, suffix normalization or
     *   current path inspection. Unknown-driver fallback follows capability helper.
     */
    [[nodiscard]] QString extension() const;
    /**
     * @return Copied setup defaultDir (home when empty) joined with defaultBaseName (export when
     *   empty) plus .extension for container or _extension folder name for per-table format. No
     *   path sanitization/creation or side effects.
     */
    [[nodiscard]] QString defaultDestination() const;
    /**
     * @brief Display required WGS84 for forcing vector driver, otherwise picked override or source
     *   label, and enable appropriate CRS controls.
     * @note Forced WGS84 disables Change/Reset but does not change stored target WKT. Empty source
     *   label says none declared regardless of setup sourceSrsWkt. No source/model edits or
     *   transform creation.
     */
    void refreshCrsLabel();

    ExportDialogSetup m_setup;
    QString m_targetWkt;
    QString m_targetLabel;
    bool m_destinationEdited = false;

    QListWidget *m_items = nullptr;
    QComboBox *m_format = nullptr;
    QLabel *m_formatNote = nullptr;
    QLineEdit *m_dest = nullptr;
    QLabel *m_crsLabel = nullptr;
    QPushButton *m_crsButton = nullptr;
    QPushButton *m_crsReset = nullptr;
    QCheckBox *m_selected = nullptr;
    QCheckBox *m_results = nullptr;
    QCheckBox *m_addToMap = nullptr;
};

} // namespace openswmmvis::ui

#endif // EXPORTLAYERDIALOG_H
