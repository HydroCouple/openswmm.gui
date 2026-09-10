/*!
 * \file   simulationoptionsdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase 3.9 (slice G-1) — SimulationOptionsDialog Tabs 1–2.
 * Round-trips OPTIONS keys via the engine's `swmm_options_get/set` C API.
 * Subsequent slices add Tabs 3–7 (Routing, Performance, CRS, 2D, Files).
 */
#ifndef SIMULATIONOPTIONSDIALOG_H
#define SIMULATIONOPTIONSDIALOG_H

#include <QDateTime>
#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QDateEdit;
class QDateTimeEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableView;
class QTableWidget;
class QTabWidget;
class QListWidget;
class QStackedWidget;
class QTextEdit;
class QToolButton;
class QAction;
class QButtonGroup;
class QRadioButton;
class QCustomTimespanEdit;

class SWMMModelLayer;
class SWMMVisProjectWindow;
class HotstartSavesModel;
class HotstartSavesDateTimeDelegate;
class PathBrowseDelegate;
class PluginsTableModel;
class ProcessComponentsModel;
class ProcessComponentIdDelegate;

namespace openswmmvis::ui {
    class RelativePathPicker;
}

#include <openswmm/engine/openswmm_engine.h>

#include <memory>

#include <QVector>

#include <functional>

#include "ui/dialogs/simoptions/enginecapabilities.h"
// Full definition, not a forward declaration: the dialog holds a
// unique_ptr<SimOptionsContext> and its destructor is inline (moc vtable).
#include "ui/dialogs/simoptions/simoptionscontext.h"

namespace openswmmvis::ui {
class SimOptionsPage;
class DatesPage;
class FilesPage;
class HydraulicsPage;
class MeshPage;
class ModelsPage;
class PerformancePage;
class QualityPage;
class SpatialPage;
class TitleNotesPage;
class TwoDPage;
}

/*!
 * \class SimulationOptionsDialog
 * \brief Edit OPTIONS for the active SWMM project.
 *
 * Round-trip layout:
 *   - On construction: read every option from the engine via
 *     `swmm_options_get` and populate the controls.
 *   - On **Apply** / **OK**: diff against the last-read snapshot and write
 *     only changed keys via `swmm_options_set`. Marks the project dirty if
 *     any key was written.
 *   - On **Cancel**: discard pending edits.
 */
class SimulationOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \param engine        Open SWMM engine handle (required).
     * \param layer         Optional model layer — gives Tab 5 access to the layer
     *                      CRS + extent for the *Detect from coordinates* helper
     *                      and the read-only extent summary.
     * \param engineVersion Version string of the engine that will run the
     *                      simulation (e.g. "6.0.0" or "5.2.4").  Controls
     *                      which tabs and controls are enabled — options that
     *                      the selected engine does not support are disabled
     *                      with an explanatory tooltip.
     * \param projectWindow Optional MDI window — provides .oswp-persisted rich
     *                      HTML notes for the new "Title/Notes" tab. When
     *                      omitted, the tab still works but only round-trips
     *                      plain text through the engine.
     * \param parent        Qt parent.
     */
    explicit SimulationOptionsDialog(SWMM_Engine engine,
                                     SWMMModelLayer *layer = nullptr,
                                     const QString &engineVersion = QStringLiteral("6.0.0"),
                                     SWMMVisProjectWindow *projectWindow = nullptr,
                                     QWidget *parent = nullptr);
    // Inline so leaf tests that only compile simulationoptionshelpers.cpp can
    // still link the moc-generated vtable. Defining it out-of-line in the
    // main .cpp would force the test to drag in the entire dialog (and its
    // OGR/GDAL deps via the spatial-tab code).
    ~SimulationOptionsDialog() override = default;

    /*! \brief True after a successful Apply / OK that wrote at least one key. */
    [[nodiscard]] bool wroteAnyChanges() const { return m_wroteChanges; }

    /*! \brief Every option key offered to writeIfChanged during the last write
     *         pass — whether or not the value actually changed.
     *
     *  The reachability seam for the dialog restructure
     *  (OPTIONS_DIALOG_TABBED_RESTRUCTURE_PLAN_2026-09-07.md §6 test 6): a key
     *  recorded here with no widget carrying a matching `optionKey` property
     *  means some editor was orphaned by a page move. Every page writes
     *  through the shared context, so the record lives in one place. */
    [[nodiscard]] QStringList lastWriteKeys() const;

    // ---- Pure helpers (testable without an engine) ------------------------

    /*! \brief Map an engine boolean string ("YES"/"NO"/"TRUE"/"FALSE"/"1"/"0")
     *         to a Qt::CheckState. Unknown values → Qt::PartiallyChecked. */
    [[nodiscard]] static int parseEngineBool(const QString &s);

    /*! \brief Render a checkbox state as the canonical engine string. */
    [[nodiscard]] static QString engineBoolString(bool on);

    /*! \brief Canonical "fast preset" recipe for 1D/2D-coupled runs: all worker
     *         threads on, and the adaptive routing step floored so the 2D
     *         coupling can't collapse it. Benchmarked ~4x faster with a ~4–5%
     *         peak-depth trade on the Bellinge model (see FAST_RUN_RECIPE.md).
     *         Kept as a static so the recipe values are locked by a unit test. */
    static void fastPresetValues(int &out_threads, double &out_min_step_sec);

    /*! \brief THREADS the fast preset uses: the machine's performance-core
     *         count (macOS), else its logical-CPU count, else 8. */
    static int fastPresetThreads();

    /*! \brief Human-readable summary of the machine / OpenMP thread limits
     *         (from swmm_get_thread_info) for tooltips. */
    static QString threadLimitsSummary(const SWMM_ThreadInfo &ti);

    /*! \brief Format a Qt date+time as the engine's expected MM/DD/YYYY +
     *         HH:MM:SS pair (returned as `out_date` and `out_time`). */
    static void formatEngineDateTime(const QDateTime &dt,
                                     QString &out_date,
                                     QString &out_time);

    /*! \brief Inverse of formatEngineDateTime. Returns an invalid QDateTime if
     *         either string is malformed. */
    [[nodiscard]] static QDateTime parseEngineDateTime(const QString &date,
                                                       const QString &time);

    /*! \brief Convert a QDateTime to SWMM's OLE Automation Date (decimal days
     *         since 1899-12-30 00:00).  Used by the [EVENTS] section editor
     *         to round-trip through the swmm_events_* C API which speaks
     *         OADate directly.  Returns 0.0 for invalid input. */
    [[nodiscard]] static double    oaDateFromQDateTime(const QDateTime &dt);

    /*! \brief Inverse of oaDateFromQDateTime. */
    [[nodiscard]] static QDateTime qDateTimeFromOaDate(double oa);

    /*! \brief Parse a step value as returned by swmm_options_get() into whole
     *         seconds. The engine round-trip is loose: a step comes back as
     *         plain seconds ("900"), decimal seconds ("900.000000" — the
     *         `std::to_string(double)` form used for REPORT_STEP /
     *         ROUTING_STEP) or as HH:MM:SS ("00:15:00", "48:00:00").
     *         Returns \a fallback when \a s matches none of those. */
    [[nodiscard]] static qint64 parseStepSeconds(const QString &s,
                                                 qint64 fallback);

    /*! \brief Compare an option value from swmm_options_get() against a
     *         freshly formatted one, tolerating formatting differences.
     *         The engine renders numerics as `std::to_string(double)`
     *         ("0.000000") while the dialog formats with 'f'/'g' variants
     *         ("0.00"), so a plain string compare treats every unchanged
     *         numeric as an edit. Exact string equality → true; else if both
     *         sides parse as doubles they compare with a relative tolerance;
     *         else false. */
    [[nodiscard]] static bool optionValueEquals(const QString &a,
                                                const QString &b);

    /*! \brief Distinct selected row indices of \a table, sorted descending
     *         (safe order for removeRow()).  Reads the selection MODEL first —
     *         the [EVENTS] table populates cells exclusively with
     *         setCellWidget() editors, so item-based queries like
     *         selectedItems() see an always-empty selection — then falls back
     *         to selectedItems() for plain cell selections. */
    [[nodiscard]] static QList<int> selectedRowsDescending(
        const QTableWidget *table);

private slots:
    void onApply();
    void onAccept();

private:
    /*! \brief Run every page's validate(); pop the blocking / warning boxes.
     *         False means the caller must not write. */
    bool confirmBeforeWrite();

    /*! Disable controls that the currently-selected engine does not support.
     *  Called once after buildUi() + readFromEngine() and re-callable when
     *  the engine version changes. */
    void applyEngineConstraints();

    void buildUi();

    /*!
     * \brief One enable rule for a sidebar row, an inner tab, or a widget.
     *
     * Replaces six ad-hoc gating paths. Disabled rows and tabs stay VISIBLE
     * but greyed with reasonWhenOff as the tooltip, so a gated option is
     * discoverable rather than missing (PLAN §4.3).
     */
    struct PageGate {
        enum class Target { SidebarRow, Tab, Widget };
        Target                target   = Target::Widget;
        int                   row      = -1;
        QTabWidget           *tabs     = nullptr;
        int                   tabIndex = -1;
        QWidget              *widget   = nullptr;
        std::function<bool()> enabled;
        QString               reasonWhenOff;
    };

    void refreshGates();     ///< Evaluate + apply every gate, then redirect.
    void buildGateTable();   ///< Build m_gates once, after every page exists.

    void addCategory(const QString &title, QWidget *page);
    /*! \brief Register a page class: appends to m_pageOrder and adds its row. */
    void addPage(openswmmvis::ui::SimOptionsPage *page);

    void readFromEngine();
    int  writeToEngine();   ///< returns count of keys written

    SWMM_Engine     m_engine = nullptr;
    SWMMModelLayer *m_layer  = nullptr;
    SWMMVisProjectWindow *m_projectWindow = nullptr;  ///< owner of .oswp-persisted notes
    QString         m_engineVersion;        ///< e.g. "6.0.0" or "5.2.4"
    bool            m_wroteChanges = false;

    // ---- sidebar rows the gate table names ------------------------------
    int             m_2DRow          = -1;   ///< 2D page row (-1 if not built).
    int             m_meshRow        = -1;   ///< Mesh-configurations row.
    int             m_qualityRow     = -1;   ///< Quality & Transport row.
    int             m_hydraulicsRow  = -1;   ///< Routing & Hydraulics row.
    QVector<PageGate> m_gates;               ///< Built once by buildGateTable().
    QListWidget    *m_categoryList   = nullptr;   ///< Left sidebar (page selector).
    QStackedWidget *m_pages          = nullptr;   ///< Right page stack.

    // ---- page registry ---------------------------------------------------
    openswmmvis::ui::EngineCapabilities        m_caps;       ///< Probed once in the ctor.
    std::unique_ptr<openswmmvis::ui::SimOptionsContext> m_ctx;
    QVector<openswmmvis::ui::SimOptionsPage *> m_pageOrder;  ///< Sidebar order.
    // Pages the dialog talks to by name — every one of these is either a gate
    // input, a gate target, or one half of a cross-page hook (PLAN §4.1).
    openswmmvis::ui::DatesPage                *m_datesPage      = nullptr;
    openswmmvis::ui::FilesPage                *m_filesPage      = nullptr;
    openswmmvis::ui::HydraulicsPage           *m_hydraulicsPage = nullptr;
    openswmmvis::ui::ModelsPage               *m_modelsPage     = nullptr;
    openswmmvis::ui::QualityPage              *m_qualityPage    = nullptr;
    openswmmvis::ui::SpatialPage              *m_spatialPage    = nullptr;
#ifdef OPENSWMM_HAS_2D
    openswmmvis::ui::TwoDPage                 *m_twoDPage       = nullptr;
#endif
};

#endif // SIMULATIONOPTIONSDIALOG_H
