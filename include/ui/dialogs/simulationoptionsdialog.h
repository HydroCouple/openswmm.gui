/**
 * @file      simulationoptionsdialog.h
 * @brief     Simulation settings host and shared option conversion helpers.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Simulation settings host and shared option conversion helpers.
 * Coordinates page drafts, capability gates, nested configuration sessions and
 * specialized file/process/output editors for the active model.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
namespace OpenSWMMVis
{
class ConfigurationSession;
}
class QUndoStack;
class SWMMVisProjectWindow;
class HotstartSavesModel;
class HotstartSavesDateTimeDelegate;
class PathBrowseDelegate;
class PluginsTableModel;
class ProcessComponentsModel;
class ProcessComponentIdDelegate;

namespace openswmmvis::ui
{
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

namespace openswmmvis::ui
{
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

/**
 * @class SimulationOptionsDialog
 * @brief Coordinate simulation-setting drafts and nested configuration editors.
 * Construction reads registered pages. Apply/OK validates drafts, attempts page
 * writers independently and commits an optional undo segment. Counted writes and
 * selected direct-edit signals latch change notifications; this is not an atomic
 * success guarantee. Cancel discards widget drafts and attempts to undo recorded
 * edits in an open session segment, subject to identity/lifecycle checks. Immediate
 * disk actions and edits outside that segment can remain applied. See individual
 * page contracts for persistence, partial-failure and ownership behavior.
 */
class SimulationOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Construct and hydrate the settings host, page registry and cross-page draft hooks.
     * @param engine Borrowed engine handle; null permits construction but prevents Apply.
     * Caller serializes access on the GUI thread and invalidates the context before closure
     * when no layer/project lifecycle signal is available.
     * @param layer Optional borrowed layer for notifications, spatial/mesh actions and session
     * undo; engineAboutToClose/destruction invalidates the dialog.
     * @param engineVersion Copied version text used in the once-computed capability snapshot.
     * @param projectWindow Optional borrowed project for notes, canvas history and mesh actions;
     * aboutToClose invalidates the dialog. Dependencies must survive nested event loops.
     * @param parent Optional QWidget owner; null leaves lifetime to the caller.
     * Creates all pages, reads their state and applies cached restrictions. With a project
     * canvas, installs its undo stack. Rejection cancels a recorded open nested segment,
     * resetting the change latch when cancel reports recorded children; cancellation
     * success is subject to history identity checks. Immediate page/file actions outside
     * that segment are not universally reversible by outer Cancel.
     */
    explicit SimulationOptionsDialog(SWMM_Engine engine, SWMMModelLayer *layer = nullptr,
                                     const QString &engineVersion = QStringLiteral("6.0.0"),
                                     SWMMVisProjectWindow *projectWindow = nullptr,
                                     QWidget *parent = nullptr);
    // Inline so leaf tests that only compile simulationoptionshelpers.cpp can
    // still link the moc-generated vtable. Defining it out-of-line in the
    // main .cpp would force the test to drag in the entire dialog (and its
    // OGR/GDAL deps via the spatial-tab code).
    /**
     * @brief Release dialog-owned state and widgets; an owned configuration session attempts to
     * cancel an open segment.
     * Does not own the engine, model layer, project window or undo stack. Session rollback
     * can be refused by lifecycle/configuration identity checks; destruction is not a file
     * restore or a guarantee that every immediate page action is reversed.
     */
    ~SimulationOptionsDialog() override = default;

    /**
     * @brief Inspect the dialog's change-notification latch.
     * @return Stored flag, set by counted writes or selected direct-editor signals and
     * possibly reset to the last Apply latch by session cancellation. It is not a fresh
     * engine diff, a complete inventory of file/partial changes or proof of save success.
     */
    [[nodiscard]] bool wroteAnyChanges() const { return m_wroteChanges; }

    /**
     * @brief Copy the shared context's option-consideration log from the last started write pass.
     * @return Owned ordered keys, including duplicates, no-ops/refusals and explicit records
     * from specialized writers; empty without context. Page actions outside that mechanism
     * are not necessarily represented. Does not indicate which keys successfully changed.
     */
    [[nodiscard]] QStringList lastWriteKeys() const;

    /**
     * @brief Replace the optional configuration-session history destination.
     * @param stack Borrowed undo stack, guarded by the new session; null disables sessions.
     * First attempts cancellation of the old session and releases it. Creates a new session
     * only with both layer and stack, using the input directory as its path anchor.
     * Does not inspect cancellation success, reset the change latch, save files or itself
     * open a segment; direct edits without recorded commands can remain applied.
     */
    void setUndoStack(QUndoStack *stack);

    // ---- Pure helpers (testable without an engine) ------------------------

    /**
     * @brief Interpret a common engine boolean token after trimming and uppercasing.
     * @param s Borrowed text; recognizes YES/TRUE/1 and NO/FALSE/0 exactly after normalization.
     * @return Qt::Checked, Qt::Unchecked or Qt::PartiallyChecked for every other value,
     * including empty text. Does not parse arbitrary numeric expressions or change data.
     */
    [[nodiscard]] static int parseEngineBool(const QString &s);

    /**
     * @brief Render the canonical common engine boolean token.
     * @param on True selects YES; false selects NO.
     * @return Owned uppercase string; no engine access or option write.
     */
    [[nodiscard]] static QString engineBoolString(bool on);

    /**
     * @brief Return the current worker/minimum-step recipe without applying it.
     * @param out_threads Required output reference overwritten with fastPresetThreads().
     * @param out_min_step_sec Required output reference overwritten with 1.0 seconds.
     * Queries hardware thread information but requires no engine handle. No benchmark
     * speed/accuracy guarantee or project-specific stability analysis is performed.
     */
    static void fastPresetValues(int &out_threads, double &out_min_step_sec);

    /**
     * @brief Query the preferred hardware worker count for the fast preset.
     * @return Positive performance-core count when reported, else positive logical-CPU
     * count on a successful thread query, else eight. Does not enforce OpenMP/environment
     * limits, set THREADS or establish the effective runtime team size.
     */
    static int fastPresetThreads();

    /**
     * @brief Format an informational snapshot of machine and runtime thread limits.
     * @param ti Borrowed previously populated SWMM_ThreadInfo; values are displayed without
     * re-query or validation. Missing/zero fields can suppress optional lines.
     * @return Owned localized newline-separated summary of logical/performance cores,
     * OpenMP availability/limits and an already-running Kokkos team where reported.
     * Does not alter runtime configuration or compute the solver's effective thread count.
     */
    static QString threadLimitsSummary(const SWMM_ThreadInfo &ti);

    /**
     * @brief Format calendar/clock fields for common engine date and time options.
     * @param dt Borrowed date/time; reads its current calendar fields without zone conversion.
     * @param out_date Output overwritten with MM/dd/yyyy, empty for an invalid date.
     * @param out_time Output overwritten with HH:mm:ss, empty for an invalid time.
     * Subseconds and timezone labels are omitted. Outputs should be distinct objects;
     * no input validity error, locale-dependent text or engine write is produced.
     */
    static void formatEngineDateTime(const QDateTime &dt, QString &out_date, QString &out_time);

    /**
     * @brief Parse trimmed engine date/time fields with explicit numeric formats.
     * @param date Borrowed MM/dd/yyyy text.
     * @param time Borrowed HH:mm:ss text, including seconds.
     * @return QDateTime constructed with the default local-time specification, or invalid
     * when either calendar/clock parse fails. No UTC conversion, alternate format or
     * fallback date is applied; local timezone/DST validity follows Qt construction.
     */
    [[nodiscard]] static QDateTime parseEngineDateTime(const QString &date, const QString &time);

    /**
     * @brief Encode wall-clock fields as SWMM/OA decimal days since 1899-12-30.
     * @param dt Borrowed date/time; valid calendar years must be within engine encoder range
     * one through 9999. Timezone label does not convert the fields to another zone.
     * @return Zero for invalid input, otherwise canonical engine encoding with subseconds
     * rounded to whole seconds and end-of-day rounding clamped to 23:59:59. Zero is also
     * a valid epoch encoding, so it does not uniquely identify failure.
     */
    [[nodiscard]] static double oaDateFromQDateTime(const QDateTime &dt);

    /**
     * @brief Decode SWMM/OA decimal days into local-labeled wall-clock editor fields.
     * @param oa Absolute fractional days since 1899-12-30, not elapsed seconds; expected
     * within the engine decoder's supported calendar range.
     * @return Invalid for nonfinite/invalid canonical decode, otherwise a QDateTime built
     * from decoded date/time using Qt's default local specification. Canonical rounding
     * is to whole seconds with end-of-day clamping. Relabels fields without converting
     * a UTC instant to local time; DST validity follows Qt's local constructor.
     */
    [[nodiscard]] static QDateTime qDateTimeFromOaDate(double oa);

    /**
     * @brief Parse a nonnegative numeric or colon-delimited duration into whole seconds.
     * @param s Borrowed trimmed text: bare decimal seconds or up to three base-60 numeric
     * components, conventionally HH:MM:SS. Components may be decimal and exceed 59.
     * @param fallback Returned for empty text, failed component conversion, negative values
     * or more than three components; may itself be any caller-supplied qint64.
     * @return qRound64 of the accumulated seconds for accepted input, otherwise fallback.
     * Caller must supply finite values whose accumulated seconds fit qint64: no explicit
     * nonfinite/overflow guard precedes rounding. Does not parse units or enforce step ranges.
     */
    [[nodiscard]] static qint64 parseStepSeconds(const QString &s, qint64 fallback);

    /**
     * @brief Compare option renderings by trimmed equality or tolerant numeric value.
     * @param a Borrowed first rendering, normally the engine getter result.
     * @param b Borrowed second rendering, normally a proposed widget rendering.
     * @return True for identical trimmed text; otherwise requires both double conversions
     * and absolute difference at most 1e-9 times max(1, abs(a), abs(b)). Case is significant
     * for nonnumeric tokens. Intended for finite numbers; nonfinite values are not rejected
     * and infinity arithmetic can yield surprising equality. Does not normalize stored text.
     */
    [[nodiscard]] static bool optionValueEquals(const QString &a, const QString &b);

    /**
     * @brief Collect distinct selected table row indices for safe descending deletion.
     * @param table Optional borrowed table valid during selection queries; null returns empty.
     * @return Owned descending rows from selectionModel()->selectedRows(), or, only if that
     * is empty, unique rows of selectedItems(). Handles row selections of editor-only
     * cells. Does not combine both queries, alter selection, delete rows or own the table.
     */
    [[nodiscard]] static QList<int> selectedRowsDescending(const QTableWidget *table);

signals:
    /**
     * @brief Notify receivers after a positive counted write pass or selected nested editor
     * changes.
     * Connected layer/project receivers mark edits. The signal carries no key list and
     * is not emitted for every possible immediate or zero-count partial change.
     */
    void changesApplied();

public slots:
    /**
     * @brief Abandon the current session, clear borrowed context handles, disable and reject the
     * dialog.
     * Closes any open undo macro while preserving recorded applied changes instead of
     * undoing a closing model. Then resets the session and engine/layer/project pointers
     * and invalidates the shared context. Cached capabilities and existing draft widgets
     * remain; does not close/free the engine or guarantee safety of raw pointers already
     * captured inside a nested operation before this call.
     */
    void invalidateContext();

private slots:
    /**
     * @brief Run a validated Apply pass and reread pages only when the pass proceeds.
     * Refused validation/absent engine leaves drafts visible. Successful progression
     * rehydrates whatever setters accepted, including after partial/refused writes;
     * this slot does not interpret the count as atomic success or close the dialog.
     */
    void onApply();
    /**
     * @brief Run a validated Apply pass and accept the dialog only when the pass proceeds.
     * Does not reread drafts before acceptance and does not require every setter to
     * succeed. Validation refusal or absent/invalidated engine keeps the dialog open.
     */
    void onAccept();

private:
    /**
     * @brief Validate pages in registration order, then ask whether to continue with advisory
     * text.
     * @return False at the first blocking page refusal or rejected advisory prompt, true
     * otherwise. Uses one shared diagnostic string across pages; page implementations
     * may append or overwrite it. Shows modal messages but performs no write itself.
     * Nested prompts can invalidate the engine; the caller must recheck it afterward.
     */
    bool confirmBeforeWrite();

    /**
     * @brief Reapply each registered page's restrictions from the existing capability snapshot.
     * Does not re-probe engine/version or refresh all host gates. Called after hydration;
     * individual capability methods can change unsupported selections and emit signals.
     */
    void applyEngineConstraints();

    /**
     * @brief Create owned navigation/pages, nested-editor actions and cross-page hooks once.
     * Synchronizes list/stack indices, wraps pages in scroll areas and builds host gates.
     * Retained performance hook receives seconds; mesh hook receives module-enabled bool;
     * transport hooks receive class index and checked bool, while schedule hooks return
     * wet/report seconds and duration. All capture this and use the dialog receiver life.
     * Snow/evaporation open nested climatology; heat captures snapshots, marks direct edits
     * and optionally records guarded undo, raster/radiation/groundwater navigation. Its
     * undo getter returns the live guarded layer engine, notifier marks options changed,
     * and borrowed error text opens a warning. Modal mesh snow/groundwater pickers borrow
     * selected layer/canvas dependencies that must survive nested loops; no general raw
     * pointer lifetime guard is added by these connections. Groundwater applied callbacks
     * refresh gates/matrix and latch edits. Spatial/direct-2D signals also latch edits.
     * Creates OK/Cancel/Apply wiring; construction itself does not commit common drafts.
     */
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
        Target target = Target::Widget;
        int row = -1;
        QTabWidget *tabs = nullptr;
        int tabIndex = -1;
        QWidget *widget = nullptr;
        std::function<bool()> enabled;
        QString reasonWhenOff;
    };

    /**
     * @brief Refresh every page's widget gates, routing mirror and transport preview, then host
     * rules.
     * Each retained zero-argument predicate determines its row/tab/widget state; empty
     * predicates are ignored. Disabled items stay visible with reason tooltips, and active
     * disabled rows/tabs redirect using helper policies. Does not re-probe capabilities
     * or commit drafts; predicate exceptions are not caught.
     */
    void refreshGates(); ///< Evaluate + apply every gate, then redirect.
    /**
     * @brief Replace host row/tab rules after all page targets exist.
     * Local row builder takes a row index, moved bool predicate and copied reason, skipping
     * negative rows; tab builder takes borrowed tabs, index, moved predicate and reason,
     * skipping null tabs. Predicates capture dialog/page accessors for 2D module, routing,
     * quality solver and cached support. Quality remains reachable; dynamic/FV/UF and
     * ARD/LARD tabs follow their solver gates. No predicates execute until refreshGates().
     */
    void buildGateTable(); ///< Build m_gates once, after every page exists.

    /**
     * @brief Append a sidebar title and adopt its page through an owned scroll-area wrapper.
     * @param title Borrowed caption copied into the new list item.
     * @param page Non-null widget transferred into the stack's scroll-area ownership.
     * Requires built navigation widgets and matching insertion order; does not register
     * SimOptionsPage behavior, hydrate values or evaluate gates.
     */
    void addCategory(const QString &title, QWidget *page);
    /**
     * @brief Register an option page in write/read order, adopt it into navigation and apply
     * capabilities.
     * @param page Optional page; null is a no-op. Non-null ownership transfers into the
     * scroll wrapper/stack, and the registry retains a borrowed pointer for dialog life.
     * Uses title() for its row. Does not call read(), write() or rebuild host rules.
     */
    void addPage(openswmmvis::ui::SimOptionsPage *page);

    /**
     * @brief Hydrate all registered pages in order, then refresh host and intra-page gates.
     * Individual pages define fallback/signal behavior and can read files/settings as
     * well as engine values. Does not reset the change latch or last write-key log.
     */
    void readFromEngine();
    /**
     * @brief Start a fresh context key log and invoke every registered page writer in order.
     * @return Sum of page write counts; absent engine returns zero without clearing the
     * previous log. Positive count latches wroteAnyChanges and emits changesApplied.
     * No rollback or validation occurs here; page counts are not uniform transactions
     * and zero can still accompany a disk/metadata/clear side effect.
     */
    int writeToEngine(); ///< returns count of keys written

    SWMM_Engine m_engine = nullptr;
    SWMMModelLayer *m_layer = nullptr;
    SWMMVisProjectWindow *m_projectWindow = nullptr; ///< owner of .oswp-persisted notes
    QString m_engineVersion;                         ///< e.g. "6.0.0" or "5.2.4"
    bool m_wroteChanges = false;
    /// Each Apply is one Undo entry covering nested editors accepted since the
    /// previous Apply; Cancel reverts those (closure audit C2). Shared so the
    /// inline destructor needs no complete type.
    std::shared_ptr<OpenSWMMVis::ConfigurationSession> m_session;
    bool m_wroteAtCommit = false; ///< m_wroteChanges at the last Apply
    /**
     * @brief Open the optional configuration session's undo macro before a nested editor.
     * No session is a no-op; an already-open macro is reused. Does not itself capture
     * engine state, record an edit or make unrelated immediate actions reversible.
     */
    void openNestedSegment();
    /// Validated write pass plus its Undo entry; false when validation refused.
    /**
     * @brief Validate, attempt page writes and commit the optional session's Apply segment.
     * @return False for absent engine, refused validation or invalidation during prompts;
     * true once writes/optional commit were attempted, including no-op/partial failure.
     * Captures a before snapshot only with a session, passes considered keys to session
     * commit and updates the last-commit change latch. Does not check snapshot/undo push
     * success, save files, guarantee atomic writes or reread/close the dialog.
     */
    bool applyPass();
    /// Runs Climatology on \a tab as a nested editor with its own Undo entry.
    /**
     * @brief Run a modal climatology editor inside the current optional undo segment.
     * @param tab Child-tab index passed directly to setCurrentTab(); its handling belongs
     * to ClimatologyDialog. Captures the model directory as path anchor when available.
     * @return True when an engine remains and the child reports wroteAnyChanges, otherwise
     * false. Captures snapshot/identity before exec, connects layer closure invalidation,
     * marks the layer and attempts guarded undo after changes; undo failure does not make
     * the return false. Does not itself emit changesApplied or hydrate other drafts.
     */
    bool runNestedClimatology(int tab);

    // ---- sidebar rows the gate table names ------------------------------
    int m_2DRow = -1;                      ///< 2D page row (-1 if not built).
    int m_meshRow = -1;                    ///< Mesh-configurations row.
    int m_qualityRow = -1;                 ///< Quality & Transport row.
    int m_hydraulicsRow = -1;              ///< Routing & Hydraulics row.
    QVector<PageGate> m_gates;             ///< Built once by buildGateTable().
    QListWidget *m_categoryList = nullptr; ///< Left sidebar (page selector).
    QStackedWidget *m_pages = nullptr;     ///< Right page stack.

    // ---- page registry ---------------------------------------------------
    openswmmvis::ui::EngineCapabilities m_caps; ///< Probed once in the ctor.
    std::unique_ptr<openswmmvis::ui::SimOptionsContext> m_ctx;
    QVector<openswmmvis::ui::SimOptionsPage *> m_pageOrder; ///< Sidebar order.
    // Pages the dialog talks to by name — every one of these is either a gate
    // input, a gate target, or one half of a cross-page hook (PLAN §4.1).
    openswmmvis::ui::DatesPage *m_datesPage = nullptr;
    openswmmvis::ui::FilesPage *m_filesPage = nullptr;
    openswmmvis::ui::HydraulicsPage *m_hydraulicsPage = nullptr;
    openswmmvis::ui::ModelsPage *m_modelsPage = nullptr;
    openswmmvis::ui::QualityPage *m_qualityPage = nullptr;
    openswmmvis::ui::SpatialPage *m_spatialPage = nullptr;
#ifdef OPENSWMM_HAS_2D
    openswmmvis::ui::TwoDPage *m_twoDPage = nullptr;
#endif
};

#endif // SIMULATIONOPTIONSDIALOG_H
