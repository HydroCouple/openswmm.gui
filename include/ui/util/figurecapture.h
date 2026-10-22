/**
 * @file      figurecapture.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Manifest-driven screenshot capture for the user manual's figures.
 *
 * The manual (docs/manual/) declares every screenshot as a \c \\figtodo
 * placeholder and turns it into \c \\fig once the PNG exists in
 * docs/manual/images/. This class produces those PNGs without a human at the
 * keyboard: SWMMVIS_CAPTURE_MANIFEST=<abs .json> names a list of figures, each
 * describing the UI state to reach and the widget to grab.
 *
 * Generalises the three one-off SWMMVIS_SNAPSHOT_* dev hooks in swmmvis.cpp
 * (which stay, being documented in appendices/a04_performance.md) into one
 * table so a whole chapter is captured per launch — 326 one-shot launches would
 * be ~45 min of pure startup, grouped runs are ~2 min.
 *
 * Two run modes, one manifest. QT_QPA_PLATFORM=offscreen handles widget-only
 * figures (dialogs, docks, panels) with no display; anything containing
 * scene-graph content (2D mesh, 2D results) reads back BLANK offscreen and must
 * run on the live cocoa platform. Rows are not classified by hand: every grab is
 * scored and a blank one is reported so it can be re-shot on the live lane.
 *
 * Deliberately public-API only (ActionRegistry, ThemeManager, QWidget) so it
 * needs no access to SWMMVis internals. Models are supplied by the launcher
 * through SWMMVIS_OPEN_ON_STARTUP, one launch per model.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_UTIL_FIGURECAPTURE_H
#define OPENSWMMVIS_UI_UTIL_FIGURECAPTURE_H

#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>

class QMainWindow;
class QTimer;
class QWidget;

namespace openswmmvis::ui
{

/*! \brief Which platform a figure can be captured on. */
enum class FigureLane {
    Offscreen, //!< widget-only; no display needed (the default)
    Live,      //!< needs real scene-graph pixels — cocoa on a real display
    Human      //!< native menus / file pickers / Finder — engine skips it
};

/*! \brief One row of the manifest: a figure and how to reach it. */
struct FigureSpec {
    QString name;             //!< "18_routing_hydraulics.png"
    QString action;           //!< ActionCatalog id to trigger (opens a dialog)
    QString widget;           //!< objectName to grab, resolved under the host
    bool wholeWindow = false; //!< grab the main window itself
    bool floatDock = false;   //!< detach the target's dock so size is not constrained by neighbors
    QString page;             //!< tab / list-row text to select in the target
    QString tab;              //!< sub-tab to select after \a page (sidebar + tab)
    QString tabIn;      //!< scope tab selection to this objectName (e.g. compactToolbarTabBar)
    QString dialogPage; //!< page in the child dialog opened by the last click
    QStringList
        prepareActions; //!< non-dialog actions applied before rendering (e.g. zoom to extent)
    QStringList hostSelectMore; //!< additional selected rows (same panel as hostSelect)
    QStringList hide;           //!< hide named widgets for this capture layout
    QStringList show;           //!< show and raise named widgets for this capture layout
    QString type;               //!< text to type into the target's first editable field
    QString typeInto;           //!< which field: objectName, or part of its placeholder
    QString afterTypeClick;     //!< inline button to press after typing (does not open a dialog)
    QString select;             //!< item to select in the target's list ("first", or its text)
    QString activate;           //!< select an item and emit its view's clicked signal
    QStringList check;          //!< check named rows before pressing a dialog button
    QStringList uncheck;        //!< uncheck named layer rows before capture
    QString sortColumn;         //!< sort a table by this header, ascending
    QString hostSelect;         //!< item to select in the MAIN WINDOW before \a action fires
    QString hostSelectIn;       //!< scope \a hostSelect to this panel (objectName or class)
    QStringList
        clicks;       //!< buttons to press in the target; the LAST one's dialog becomes the target
    QString column;   //!< scroll the target's table to this column header
    QString editCell; //!< open the row-0 cell editor in this column (compound "Edit…")
    QString grab;     //!< narrow the grab to this descendant (objectName or class)
    QSize size;       //!< resize the target before grabbing
    QSize hostSize;   //!< resize the MAIN WINDOW first (layout-dependent widgets)
    FigureLane lane = FigureLane::Offscreen;
    int settleMs = 0;       //!< extra settle before the grab (0 = default)
    int maxWidth = 0;       //!< clamp THIS figure's width (0 = the manifest default)
    double cursorNorm = -1; //!< seek the visible animation scrubber to [0,1]; -1 leaves it alone
};

/*! \brief Result of attempting one row; serialised into run.json. */
struct FigureResult {
    QString name;
    QString status; //!< "ok" | "skipped" | "failed"
    QString detail; //!< failure reason, or the skip rationale
    int width = 0;
    int height = 0;
    qint64 bytes = 0;
    bool blank = false; //!< grab carried (almost) no information
    int elapsedMs = 0;
};

/*!
 * \brief Drives a manifest of figures to PNG, then quits the application.
 *
 * Lifetime: created by createIfRequested() from the main window's constructor
 * and parented to it; a no-op (nullptr) when the env var is unset, so normal
 * runs pay nothing.
 */
class FigureCapture : public QObject
{
    Q_OBJECT

public:
    /*! \brief Build a capture run when SWMMVIS_CAPTURE_MANIFEST names a
     *         existing manifest path and a host is supplied, else nullptr.
     *         Parsing/readability are checked later by start().
     *
     *  \param host   the main window; widget lookups resolve under it.
     *  \param parent owner (normally \a host).
     * @return Parent-owned capture runner when the environment variable names an existing path, otherwise null (missing paths log a warning). Does not start the queue.
     * @note A null parent falls back to host. Caller keeps the borrowed raw host pointer alive; all operations require the Qt GUI thread.
     */
    static FigureCapture *createIfRequested(QMainWindow *host, QObject *parent);

    /**
     * @brief Destroy the runner and its owned watchdog/timers; generated image/report files remain on disk.
     */
    ~FigureCapture() override;

    /*! \brief Begin the queue. Safe to call once; later calls are ignored.
     *
     *  Waits out \c defaults.startupMs first so a model handed in through
     *  SWMMVIS_OPEN_ON_STARTUP has finished loading.
     * @note Marks the runner started before parsing; parse failure logs and schedules application quit with no retry. Successful startup may change theme/window size and schedules the queue asynchronously.
     */
    void start();

private:
    /**
     * @brief Construct the queue driver and a watchdog that dismisses dialogs/records a failed current figure.
     * @param[in] host Required borrowed main window, kept alive by caller for the run.
     * @param[in] manifestPath Manifest filename moved into this runner.
     * @param[in] parent QObject owner, normally host. No manifest is loaded until start().
     */
    FigureCapture(QMainWindow *host, QString manifestPath, QObject *parent);

    /**
     * @brief Read manifest defaults and enqueue selected named figure rows.
     * @param[out] error Required diagnostic destination on read/JSON/no-selected-rows/invalid-cursor failure; unchanged on success.
     * @return True with at least one queued figure; false can leave partially populated defaults/queue and a created output directory.
     * @note SWMMVIS_CAPTURE_ONLY filters exact comma-separated names. Missing outDir defaults beside the manifest; directory creation success is not checked here. Unsupported row fields are ignored and unknown lane strings default Offscreen.
     */
    bool loadManifest(QString *error);
    /**
     * @brief Load settle/startup/watchdog/image-size/render-scale defaults and optional theme/window dimensions.
     * @note Positive SWMMVIS_CAPTURE_STARTUP_MS overrides manifest startup delay. Values are not comprehensively validated. Theme and host geometry are changed live and are not restored at run end.
     */
    void applyDefaults();
    /**
     * @brief Pop the next figure, reset completion timing and run it under the watchdog, or finish the application run.
     * @note Human rows become skipped without capture. Live lane does not switch the platform; actual rendering still depends on the launched Qt platform.
     */
    void processNext();
    /**
     * @brief Record one figure result, stop its watchdog and schedule the next queue item.
     * @param[in] result Complete result copied into the run log.
     * @note A current-row guard suppresses duplicate completion from watchdog/capture callbacks; scheduling continues asynchronously.
     */
    void finishSpec(const FigureResult &result);
    /**
     * @brief Write aggregate run.json and schedule application exit code one if any figure failed, otherwise zero.
     * @note Output is truncated/replaced directly; report open/write failures are not separately reported. Blank-but-saved images count as ok plus blank, and skipped Human rows do not cause failure exit.
     */
    void finishRun();

    /*! \brief Reach the state \a spec describes and grab it. */
    /*! \brief Apply \a spec's hostSelect; false (and fails the row) if it misses.
     * @param[in] spec Figure row describing optional main-window selection scope, primary row and appended rows.
     * @return True if no selection requested or all rows resolve; false completes the current figure as failed. Earlier successful selection changes remain.
     */
    bool applyHostSelect(const FigureSpec &spec);
    /**
     * @brief Apply named widget visibility/preconditions, select the host row and schedule the target/dialog capture.
     * @param[in] spec Current figure configuration copied into asynchronous callbacks.
     * @note Triggers live registered actions and changes UI state. Known native-picker actions are refused on the action-only path; preparation actions are separately checked later. Missing targets/actions finish the row as failed.
     */
    void captureSpec(const FigureSpec &spec);
    /**
     * @brief Drive page/tab/selection/editor/click/typing/sort/scroll preparation and render a target to a PNG.
     * @param[in,out] target Borrowed target widget; null completes the figure as failed. Must remain alive while nested event processing/callbacks use it.
     * @param[in] spec Figure row and continuation settings; child-dialog clicks schedule a new asynchronous grab rather than completing immediately.
     * @note Can resize/float widgets, trigger actions, set model check states and edit live controls; rejecting dialogs afterward does not guarantee rollback of those side effects. Pumps Qt events during settle waits. Renders at configured scale then caps width, overwrites the named output PNG, scores blankness and records completion.
     */
    void grabInto(QWidget *target, const FigureSpec &spec);

    /*! \brief Top-most visible dialog that is not the host, or nullptr.
     * @return Borrowed active modal widget when present; otherwise the deepest-parented visible top-level QDialog other than host. Equal-depth ordering follows unspecified topLevelWidgets order.
     */
    QWidget *activeDialog() const;
    /*! \brief Select \a page inside \a target (tab bar, list + stack, tree).
     * @param[in,out] target Required widget scope searched for MDI windows, tabs, lists, top-level trees and visible combos.
     * @param[in] page Page label matched case-insensitively after mnemonic removal; MDI modified marks and combo count suffixes are tolerated.
     * @return True after selecting the first supported match, false leaves no successful selection. Host signal handlers may change other state synchronously.
     */
    bool selectPage(QWidget *target, const QString &page) const;
    /*! \brief Select an item in the target's first populated item view.
     * @param[in,out] target Required scope searched for visible populated descendant item views.
     * @param[in] which Case-insensitive display text, first sentinel, or hierarchical slash/ > path; full row text is tried before path splitting.
     * @param[in] append Add to selection instead of clearing it when true.
     * @param[in] operation Empty for selection, check/uncheck for CheckStateRole, or activate for direct clicked() invocation.
     * @return True after successful selection/operation and event processing; false for no match or rejected check/invocation. Selection may already have changed before a failed operation.
     */
    bool selectItem(QWidget *target, const QString &which, bool append = false,
                    const QString &operation = {}) const;
    /*! \brief Reject up to eight active dialogs after a figure; prior live changes are not rolled back by this helper.
     * @note Rejects QDialogs (or closes a non-dialog active modal widget) and pumps non-user events. Does not restore prior theme/layout/selection or undo live model writes.
     */
    void dismissDialogs();
    /*! \brief dismissDialogs(), but only for a row that opens something.
     * @param[in] spec Current figure; nonempty action triggers dismissDialogs(), while widget/window-only rows leave existing dialogs alone.
     */
    void dismissOpenedBy(const FigureSpec &spec);

    QMainWindow *mHost = nullptr;
    QString mManifestPath;
    QString mOutDir;
    QJsonObject mDefaults;
    QList<FigureSpec> mQueue;
    QList<FigureResult> mResults;

    FigureSpec mCurrent;
    bool mCurrentDone = false; //!< guards double-advance
    bool mStarted = false;
    QElapsedTimer mSpecTimer;
    QTimer *mWatchdog = nullptr;

    int mDefaultSettleMs = 600;
    int mStartupMs = 2500;
    int mWatchdogMs = 20000;
    int mMaxWidth = 1400;
    qreal mRenderScale = 2.0; //!< offscreen DPR is 1; render at 2x for the manual
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_UTIL_FIGURECAPTURE_H
