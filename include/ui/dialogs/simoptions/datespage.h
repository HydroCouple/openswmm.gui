/**
 * @file      datespage.h
 * @brief     Simulation Options → Dates & Times.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Simulation Options → Dates & Times.
 *
 * Tabs: Simulation Window · Time Steps · Events.
 *
 * "Skip steady state" is NOT here — it moved to Routing & Hydraulics › Routing
 * in T4, because SKIP_STEADY_STATE is honoured under every routing method
 * (PLAN §7 Q1, resolved by measurement).
 *
 * The 2D page mirrors two of these steps and multiplies by the run length for
 * its output-size estimate, so the schedule is published as three read
 * accessors plus one change signal rather than by handing out widgets
 * (PLAN §4.1).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef DATESPAGE_H
#define DATESPAGE_H

#include <QDateTime>
#include <QList>
#include <QPair>

#include "ui/dialogs/simoptions/simoptionspage.h"

class QDateEdit;
class QDateTimeEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QCustomTimespanEdit;

namespace openswmmvis::ui
{

class DatesPage : public SimOptionsPage
{
    Q_OBJECT

public:
    /**
     * @brief Construct the owned simulation-window, step and event draft controls.
     * @param ctx Borrowed GUI-thread context that must outlive this page and provide
     * valid engine handles while accessed.
     * @param parent Optional widget owner; null leaves lifetime to the caller.
     * Builds and tags controls; caller invokes read() to hydrate them.
     */
    explicit DatesPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    /**
     * @brief Return the localized sidebar caption.
     * @return Owned Dates & Times string.
     */
    [[nodiscard]] QString title() const override;
    /**
     * @brief Load common date/step options and replace the event table/baseline from the engine.
     * Window editors are signal-blocked during hydration; invalid parsed endpoints
     * leave their previous values. Report start is clamped to the new valid window.
     * Step/sweep/dry-day fallbacks come from simulation preferences and widget ranges
     * may clamp values. Sweep dates use leap year 2000 while persisting only month/day.
     * Event getter failures are skipped. Emits scheduleChanged after loading; step
     * signals can also emit it during hydration. No engine setter or file save runs.
     */
    void read() override;
    /**
     * @brief Apply all common date/step drafts, then replace changed event rows.
     * @return Sum of successful common-option setters and successful event additions.
     * Each write is independent: partial failures have no rollback. Routing text that
     * cannot convert to double is skipped; accepted text is rendered to six significant
     * digits. This does not invoke validate(), create undo or save files. Clearing an
     * event list can change engine state while contributing zero to the return count.
     */
    int write() override;
    /**
     * @brief Mark invalid event intervals and append advisory event diagnostics.
     * @param warn Optional borrowed append-only warning output; existing text is retained.
     * Reports disjoint simulation windows and overlaps here without rejecting those rows.
     * @return False for missing cell editors or start not earlier than end; true for an
     * absent event table or otherwise valid intervals. Highlights invalid intervals and
     * clears styling on valid ones. Does not validate the overall simulation window,
     * step text or event ordering, change engine state or reject overlaps/outside rows.
     */
    bool validate(QString *warn) override;

    /*! \brief Tab order, so the dialog can name tabs without magic numbers. */
    enum Tab { TabWindow = 0, TabSteps, TabEvents };

    /**
     * @brief Expose the owned tab container for host navigation and tab gating.
     * @return Borrowed container; do not delete or retain beyond this page's lifetime.
     */
    [[nodiscard]] QTabWidget *tabs() const { return m_tabs; }

    // ---- Schedule, published for the 2D page (PLAN §4.1) ------------------
    /**
     * @brief Read the current wet-weather draft step.
     * @return Total editor seconds, or zero if its editor is absent; no engine query.
     */
    [[nodiscard]] qint64 wetStepSeconds() const;
    /**
     * @brief Read the current reporting draft step.
     * @return Total editor seconds, or zero if its editor is absent; no engine query.
     */
    [[nodiscard]] qint64 reportStepSeconds() const;
    /**
     * @brief Measure the current simulation-window draft using QDateTime::secsTo.
     * @return Nonnegative elapsed seconds, clamped to zero for reversed/equal endpoints
     * or absent controls. Uses the editors' date/time semantics without engine access.
     */
    [[nodiscard]] qint64 durationSeconds() const;

signals:
    /**
     * @brief Notify listeners after start/end, wet/report step edits or a completed read().
     * Listeners re-query the schedule accessors; no values are carried and repeated
     * notifications or a notification with unchanged values are possible.
     */
    void scheduleChanged();

private:
    /**
     * @brief Create owned window, duration, step, sweep and event controls once.
     * Start/end callbacks ignore their borrowed date argument, constrain report start,
     * refresh duration and emit scheduleChanged. Wet/report callbacks ignore the supplied
     * seconds and publish the same signal. Event selection gates Remove using selected
     * rows, including editor-only cells. Add/Remove affect only the draft table.
     * Routing text has a decimal validator; event displays have minute precision while
     * window displays retain seconds. These connections do not commit engine options.
     */
    void buildUi();
    /**
     * @brief Associate common option keys and [EVENTS] with the draft widgets.
     * Multiple date/time keys accumulate on one editor; tags support reachability
     * inspection and do not read, validate or persist the widget values.
     */
    void tagWidgets();
    /**
     * @brief Constrain the report-start editor to ordered start/end drafts.
     * For an inverted window clears both limits so either endpoint can be edited first.
     * May clamp the report-start draft; assumes all three owned editors exist.
     */
    void updateReportStartRange();
    /**
     * @brief Render positive draft elapsed seconds as days and HH:MM:SS.
     * Uses an em dash for equal/reversed endpoints and returns if required controls are
     * absent. Does not alter endpoints, report bounds or engine options.
     */
    void updateDurationLabel();
    /**
     * @brief Append an editor-only event row seeded from the simulation-window draft.
     * Missing start editor falls back to current local date/time, missing end to one day
     * later. Missing table is a no-op. Table adopts both editors; no engine write or
     * baseline update occurs until a later apply/read.
     */
    void addEventRow();
    /**
     * @brief Delete distinct selected draft event rows in descending index order.
     * Uses the shared selection-model query so editor-only rows are recognized and
     * index shifts cannot remove subsequent wrong rows. Missing table is a no-op;
     * engine rows and the read baseline remain unchanged here.
     */
    void removeSelectedEventRows();
    /**
     * @brief Clear the table/baseline and copy readable engine event pairs into owned editors.
     * Absent engine or failed count leaves them empty; failed per-row getters are skipped.
     * OA decimal-day values are converted to local-labeled wall-clock editor fields.
     * Snapshot stores those converted values for later exact QList comparison. No setters
     * run and failed reads are not separately reported to the user.
     */
    void readEventsFromEngine();
    /**
     * @brief Replace the engine event list when current editor pairs differ from the baseline.
     * @return Successful add count; zero for absent dependencies, unchanged draft, failed
     * clear or no successful additions. A successful clear itself contributes zero.
     * Skips missing cell editors and non-increasing converted intervals. Clear then adds
     * is non-atomic, so failures can leave a partial/empty engine list. After successful
     * clear the baseline advances to the entire draft even when adds fail or are skipped;
     * no rollback, common-key record, external-change merge or file save is performed.
     */
    int writeEventsToEngine();

    QTabWidget *m_tabs = nullptr;

    QDateTimeEdit *m_startEdit = nullptr;
    QDateTimeEdit *m_endEdit = nullptr;
    QDateTimeEdit *m_reportStartEdit = nullptr;
    QLabel *m_durationLabel = nullptr;               // "1d 02:30:00"
    QCustomTimespanEdit *m_reportStepEdit = nullptr; // (days, HH:mm:ss)
    QCustomTimespanEdit *m_dryStepEdit = nullptr;    // (days, HH:mm:ss)
    QCustomTimespanEdit *m_wetStepEdit = nullptr;    // (days, HH:mm:ss)
    QCustomTimespanEdit *m_ruleStepEdit = nullptr;   // (days, HH:mm:ss)
    QLineEdit *m_routingStepEdit = nullptr;          // seconds (float text)
    QDoubleSpinBox *m_dryDaysSpin = nullptr;         // days
    QDateEdit *m_sweepStartEdit = nullptr;           // MM/DD only
    QDateEdit *m_sweepEndEdit = nullptr;             // MM/DD only

    // [EVENTS] section editor (Slice CW). Each row is a {start, end} pair;
    // the engine stores decimal-day pairs round-tripped via swmm_events_*.
    QTableWidget *m_eventsTable = nullptr;
    QPushButton *m_eventsAddBtn = nullptr;
    QPushButton *m_eventsRemoveBtn = nullptr;
    /*! Rows as last read from the engine — change detection for write(). */
    QList<QPair<QDateTime, QDateTime>> m_eventsSnapshot;
};

} // namespace openswmmvis::ui

#endif // DATESPAGE_H
