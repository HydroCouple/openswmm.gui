/*!
 * \file   datespage.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
    explicit DatesPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    [[nodiscard]] QString title() const override;
    void read() override;
    int  write() override;
    bool validate(QString *warn) override;

    /*! \brief Tab order, so the dialog can name tabs without magic numbers. */
    enum Tab { TabWindow = 0, TabSteps, TabEvents };

    [[nodiscard]] QTabWidget *tabs() const { return m_tabs; }

    // ---- Schedule, published for the 2D page (PLAN §4.1) ------------------
    [[nodiscard]] qint64 wetStepSeconds() const;
    [[nodiscard]] qint64 reportStepSeconds() const;
    /*! \brief End − Start in seconds; 0 when the window is empty or inverted. */
    [[nodiscard]] qint64 durationSeconds() const;

signals:
    /*! \brief Any of the three accessors above changed. */
    void scheduleChanged();

private:
    void buildUi();
    void tagWidgets();
    void updateDurationLabel();
    void addEventRow();
    void removeSelectedEventRows();
    void readEventsFromEngine();
    int  writeEventsToEngine();

    QTabWidget *m_tabs = nullptr;

    QDateTimeEdit  *m_startEdit         = nullptr;
    QDateTimeEdit  *m_endEdit           = nullptr;
    QDateTimeEdit  *m_reportStartEdit   = nullptr;
    QLabel         *m_durationLabel     = nullptr;     // "1d 02:30:00"
    QCustomTimespanEdit *m_reportStepEdit = nullptr;   // (days, HH:mm:ss)
    QCustomTimespanEdit *m_dryStepEdit    = nullptr;   // (days, HH:mm:ss)
    QCustomTimespanEdit *m_wetStepEdit    = nullptr;   // (days, HH:mm:ss)
    QCustomTimespanEdit *m_ruleStepEdit   = nullptr;   // (days, HH:mm:ss)
    QLineEdit      *m_routingStepEdit   = nullptr;     // seconds (float text)
    QDoubleSpinBox *m_dryDaysSpin       = nullptr;     // days
    QDateEdit      *m_sweepStartEdit    = nullptr;     // MM/DD only
    QDateEdit      *m_sweepEndEdit      = nullptr;     // MM/DD only

    // [EVENTS] section editor (Slice CW). Each row is a {start, end} pair;
    // the engine stores decimal-day pairs round-tripped via swmm_events_*.
    QTableWidget   *m_eventsTable       = nullptr;
    QPushButton    *m_eventsAddBtn      = nullptr;
    QPushButton    *m_eventsRemoveBtn   = nullptr;
    /*! Rows as last read from the engine — change detection for write(). */
    QList<QPair<QDateTime, QDateTime>> m_eventsSnapshot;
};

} // namespace openswmmvis::ui

#endif // DATESPAGE_H
