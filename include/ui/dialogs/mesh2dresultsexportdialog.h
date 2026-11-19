/**
 * @file      mesh2dresultsexportdialog.h
 * @brief     Collects the options for "Export 2D Results…" on the Analysis ribbon: output format
 *            and path, which variables, which time steps (with an every-Nth stride for a long
 *            run), whether to include the run maxima, and — for a raster — the pixel size, the
 *            interpolation method and the dry mask.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Collects the options for "Export 2D Results…" on the Analysis ribbon:
 * output format and path, which variables, which time steps (with an every-Nth
 * stride for a long run), whether to include the run maxima, and — for a raster
 * — the pixel size, the interpolation method and the dry mask.
 *
 * The dialog is purely a collector (CLAUDE.md §5.1): it holds no results and
 * writes no files. The caller reads \ref openswmmvis::ui::Mesh2DResultsExportDialog::options() and hands them to
 * openswmmvis::io::exportMesh2DResults with its own progress dialog, so the
 * same export is reachable headless and is tested without a widget.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_MESH2DRESULTSEXPORTDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_MESH2DRESULTSEXPORTDIALOG_H

#include "io/mesh2dresultsexport.h"

#include <QDateTime>
#include <QDialog>
#include <QList>
#include <QString>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace openswmmvis::ui
{

/*! \brief What the dialog needs to know about the run it is exporting.
 *
 *  Deliberately plain data rather than the results layer: the dialog then has
 *  no dependency on the layer or render stack, and its test can build a
 *  72-step run in three lines. */
struct Mesh2DExportDialogInputs {
    QList<QDateTime> times;                   ///< one per frame; invalid entries render as an index
    int currentIndex = -1;                    ///< the frame the animation is on ("Current")
    double suggestedCellSize = 0.0;           ///< model units; seeds the raster cell size
    double extentWidth = 0.0;                 ///< mesh bounding box, model units
    double extentHeight = 0.0;                ///< so the dialog can size the raster
    bool hasVelocity = false;                 ///< false disables the velocity variables
    bool live = false;                        ///< the run is still going: "so far" wording
    QString lengthUnit = QStringLiteral("m"); ///< spin-box suffix
    QString defaultDir;                       ///< where the file chooser opens
    QString defaultBaseName;                  ///< suggested file stem (the run's name)
};

class Mesh2DResultsExportDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] inputs Moved/copied plain run metadata; times/current frame/extents/velocity
     *   availability are frozen at construction, including live-run mode. No source object or
     *   engine retained.
     * @param[in] parent Optional QWidget owner; caller manages parentless modal dialog.
     * @brief Build export options and initialize current captured frame selection plus validation,
     *   without loading data/writing files.
     * @note Current frame invalid means no time selected; maxima defaults true so maxima-only can
     *   still be valid. Cell size is input model coordinate units with supplied suffix, no CRS
     *   lookup or conversion. GUI-thread controls and GDAL driver-registration query only, caller
     *   performs actual export.
     */
    explicit Mesh2DResultsExportDialog(Mesh2DExportDialogInputs inputs, QWidget *parent = nullptr);
    /**
     * @brief Destroy owned option widgets; no source closure, export, cancellation job or implicit
     *   settings save.
     */
    ~Mesh2DResultsExportDialog() override;

    /*! \brief The options the user chose. Valid once exec() returns Accepted;
     *  readable before that (the tests drive the widgets directly).
     * @return Owned options from current controls with sorted zero-based frame indices, variable
     *   bit mask, maxima and raster settings. No validity/acceptance check, results read or source
     *   ownership.
     * @note Base path becomes absolute directory plus QFileInfo completeBaseName, stripping final
     *   suffix even if user entered dotted stem rather than actual extension. Browse already strips
     *   one suffix, so options can strip another dotted component afterward. Empty input remains
     *   empty. Raster options/disabled checkbox states are still copied even for vector output;
     *   writer interprets selected format.
 */
    openswmmvis::io::Mesh2DExportOptions options() const;

private slots:
    /**
     * @brief Choose format-filtered save path, then store absolute directory and completeBaseName
     *   as extensionless stem.
     * @note Initial directory uses raw current text or defaultDir/home; cancellation no-op. Native
     *   chooser may confirm chosen file, but actual writer creates suffixed/variable outputs, not
     *   necessarily that path. No independent overwrite/protected-source/fingerprint check, source
     *   write or export.
     */
    void onBrowse();
    /**
     * @brief Enable raster grid group only for GeoTIFF and revalidate all choices.
     * @note Retains disabled raster values/path/step selections unchanged. Empty format list falls
     *   back GeoTIFF internally but validation still refuses unavailable drivers. No extension
     *   rewriting or data export.
     */
    void onFormatChanged();
    /**
     * @brief Select every captured timestep row when list exists; selectionChanged revalidates. No
     *   source animation/time mutation or live-frame refresh.
     */
    void onSelectAll();
    /**
     * @brief Clear timestep selection when list exists; maxima option remains unchanged and may
     *   permit maxima-only export. No source mutation.
     */
    void onSelectNone();
    /**
     * @brief Clear existing step selection, then select/scroll to input currentIndex only when in
     *   list range.
     * @note Uses frozen construction-time index, not current animation; invalid index leaves no
     *   selected frames. Does not disable maxima, change source time or write results.
     */
    void onSelectCurrent();
    /**
     * @brief Replace selected frames with indices 0, N, 2N, ... through captured list, using stride
     *   widget with minimum effective1.
     * @note Always starts at frame zero, not first selected/current row, and does not force last
     *   frame. Triggers normal per-selection refreshes plus final revalidate; no output data
     *   mutation.
     */
    void onApplyStride();
    /**
     * @brief Refresh selected-count/grid-cost feedback, IDW-neighbor enabled state, first
     *   validation text and OK enabled flag.
     * @note GeoTIFF estimate ceil(extentWidth/cellSize)*ceil(extentHeight/cellSize) uses model
     *   units, displays four-byte-band MiB estimate and refuses product >200 million pixels when
     *   dimensions/cell positive. Does not estimate all variables/times/maxima or enforce
     *   per-axis/int/finite limits separately. Invalid/nonpositive dimensions leave grid feedback
     *   empty rather than fail here.
     * @note Base validation error takes precedence over raster size error. Neighbors enabled only
     *   with raster+IDW. No actual grid allocation, file validation, source availability query or
     *   export; programmatic QDialog::accept can bypass disabled OK.
     */
    void revalidate();

private:
    /**
     * @brief Create owned available-format, output-stem, variable, timestep, maxima and raster
     *   option controls and wire live validation.
     * @note Ensure GDAL registered; list available drivers in GeoTIFF/Shapefile/GeoPackage order.
     *   Depth/head default checked, speed only when velocity available; other velocity components
     *   unchecked/disabled without field. Maxima defaults true. Captured time labels use input
     *   timestamps as supplied (invalid shows one-based index), no timezone conversion.
     * @note Pixel editor [1e-4,1e7]/four decimals seeds positive suggested size else1, clamped by
     *   Qt. IDW neighbors [1,64], default8; natural neighbor default and dry mask true. Path seeded
     *   only when both defaultDir/base supplied. No results object, data reads, preset persistence
     *   or write job.
     */
    void buildUi();
    /**
     * @return Current item-data enum cast, or GeoTiff if combo missing/empty. Does not verify
     *   capability availability or perform export.
     */
    openswmmvis::io::Mesh2DExportFormat currentFormat() const;
    /**
     * @return Owned sorted zero-based selected row indices; empty without list. Values refer to
     *   captured times, not newly arriving live frames; no source time changes.
     */
    std::vector<int> selectedSteps() const;
    /**
     * @return Bitwise depth/head/vx/vy/speed flags for checked controls, zero if none. Does not
     *   inspect enabled state or actual results availability, so programmatic disabled checks still
     *   count. No data conversion or source query.
     */
    unsigned selectedVariables() const;
    /*! \brief Empty when the choices are exportable, else why not.
     * @return First refusal for no listed driver, blank trimmed path, zero variable mask, neither
     *   selected frame nor maxima, or Shapefile fields >255 (selected frames + optional max +
     *   cell_id). Empty otherwise.
     * @note Does NOT include raster pixel-limit check (revalidate adds it), validate actual path
     *   permissions/extensions/stem, source data/velocity/CRS, per-variable maxima availability,
     *   timestep bounds after source changes or duplicate targets. Pure widget query, no file
     *   writes.
 */
    QString validationMessage() const;

    Mesh2DExportDialogInputs m_in;

    QComboBox *m_format = nullptr;
    QLineEdit *m_path = nullptr;
    QPushButton *m_browse = nullptr;
    QCheckBox *m_depth = nullptr;
    QCheckBox *m_head = nullptr;
    QCheckBox *m_vx = nullptr;
    QCheckBox *m_vy = nullptr;
    QCheckBox *m_vmag = nullptr;
    QCheckBox *m_includeMax = nullptr;
    QListWidget *m_steps = nullptr;
    QPushButton *m_all = nullptr;
    QPushButton *m_none = nullptr;
    QPushButton *m_current = nullptr;
    QSpinBox *m_stride = nullptr;
    QPushButton *m_applyStride = nullptr;
    QLabel *m_selection = nullptr;
    QGroupBox *m_rasterGroup = nullptr;
    QDoubleSpinBox *m_cellSize = nullptr;
    QLabel *m_gridInfo = nullptr;
    QComboBox *m_interp = nullptr;
    QSpinBox *m_neighbours = nullptr;
    QCheckBox *m_maskDry = nullptr;
    QLabel *m_validation = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESH2DRESULTSEXPORTDIALOG_H
