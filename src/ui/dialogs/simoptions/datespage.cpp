/*!
 * \file   datespage.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simoptions/datespage.h"

#include <QAbstractItemView>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <qcustomeditors.h>

#include "core/preferencesmanager.h"
#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/theme/themehelpers.h"

namespace openswmmvis::ui
{

namespace {

// Clicking a cell WIDGET never reaches the table's mousePressEvent, so the
// row under the click is not selected and row-based actions (Remove) see no
// selection.  Mirror the editor's focus into a row selection instead: on
// FocusIn, find the row owning this editor and select it.
class EventEditorFocusFilter : public QObject
{
public:
    EventEditorFocusFilter(QTableWidget *table, QObject *parent)
        : QObject(parent), m_table(table) {}

protected:
    bool eventFilter(QObject *watched, QEvent *ev) override
    {
        if (ev->type() == QEvent::FocusIn && m_table) {
            auto *w = qobject_cast<QWidget *>(watched);
            for (int r = 0; w && r < m_table->rowCount(); ++r) {
                for (int c = 0; c < m_table->columnCount(); ++c) {
                    if (m_table->cellWidget(r, c) == w) {
                        m_table->selectRow(r);
                        return false;
                    }
                }
            }
        }
        return false;
    }

private:
    QTableWidget *m_table;
};

// Wrap a QDateTimeEdit inside a QTableWidget cell.  Centralised so every
// row uses the same display format / calendar policy.  HH:MM precision
// (legacy SWMM 5 parity, decided 2026-05-21).
QDateTimeEdit *makeEventCellEditor(const QDateTime &dt, QWidget *parent)
{
    auto *edit = new QDateTimeEdit(dt, parent);
    edit->setCalendarPopup(true);
    edit->setDisplayFormat(QStringLiteral("MM/dd/yyyy HH:mm"));
    edit->setFrame(false);
    if (auto *table = qobject_cast<QTableWidget *>(parent))
        edit->installEventFilter(new EventEditorFocusFilter(table, edit));
    return edit;
}

} // namespace

DatesPage::DatesPage(SimOptionsContext &ctx, QWidget *parent)
    : SimOptionsPage(ctx, parent)
{
    buildUi();
    tagWidgets();
}

QString DatesPage::title() const
{
    return tr("Dates & Times");
}

qint64 DatesPage::wetStepSeconds() const
{
    return m_wetStepEdit ? m_wetStepEdit->totalSeconds() : 0;
}

qint64 DatesPage::reportStepSeconds() const
{
    return m_reportStepEdit ? m_reportStepEdit->totalSeconds() : 0;
}

qint64 DatesPage::durationSeconds() const
{
    if (!m_startEdit || !m_endEdit) return 0;
    return std::max<qint64>(0, m_startEdit->dateTime().secsTo(m_endEdit->dateTime()));
}

void DatesPage::buildUi()
{
    auto *root = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("datesTabs"));
    root->addWidget(m_tabs, 1);

    auto *winTab  = new QWidget(m_tabs); auto *winTabLay  = new QVBoxLayout(winTab);
    auto *stepTab = new QWidget(m_tabs); auto *stepTabLay = new QVBoxLayout(stepTab);
    auto *evTab   = new QWidget(m_tabs); auto *evTabLay   = new QVBoxLayout(evTab);

    // ── Simulation window ──────────────────────────────────────────────
    auto *winGroup = new QGroupBox(tr("Simulation window"), winTab);
    auto *winForm  = new QFormLayout(winGroup);

    m_startEdit = new QDateTimeEdit(winGroup);
    m_startEdit->setCalendarPopup(true);
    m_startEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    winForm->addRow(tr("Start:"), m_startEdit);

    m_endEdit = new QDateTimeEdit(winGroup);
    m_endEdit->setCalendarPopup(true);
    m_endEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    winForm->addRow(tr("End:"), m_endEdit);

    m_durationLabel = new QLabel(QStringLiteral("—"), winGroup);
    m_durationLabel->setToolTip(tr("Simulation timespan (End − Start)."));
    winForm->addRow(tr("Duration:"), m_durationLabel);

    m_reportStartEdit = new QDateTimeEdit(winGroup);
    m_reportStartEdit->setCalendarPopup(true);
    m_reportStartEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    winForm->addRow(tr("Report start:"), m_reportStartEdit);

    winTabLay->addWidget(winGroup);

    // Live sync: clamp report-start ≥ start, refresh duration label.
    connect(m_startEdit, &QDateTimeEdit::dateTimeChanged,
            this, [this](const QDateTime &s) {
                m_reportStartEdit->setMinimumDateTime(s);
                if (m_reportStartEdit->dateTime() < s)
                    m_reportStartEdit->setDateTime(s);
                updateDurationLabel();
                emit scheduleChanged();
            });
    connect(m_endEdit, &QDateTimeEdit::dateTimeChanged,
            this, [this](const QDateTime &) {
                updateDurationLabel();
                emit scheduleChanged();
            });

    // ── Time steps ─────────────────────────────────────────────────────
    auto *stepGroup = new QGroupBox(tr("Time steps"), stepTab);
    auto *stepForm  = new QFormLayout(stepGroup);

    m_reportStepEdit = new QCustomTimespanEdit(stepGroup);
    m_reportStepEdit->setToolTip(tr("Reporting step (REPORT_STEP)."));
    stepForm->addRow(tr("Reporting step:"), m_reportStepEdit);

    m_dryStepEdit = new QCustomTimespanEdit(stepGroup);
    m_dryStepEdit->setToolTip(tr("Runoff dry-weather step (DRY_STEP)."));
    stepForm->addRow(tr("Dr&y-weather step:"), m_dryStepEdit);

    m_wetStepEdit = new QCustomTimespanEdit(stepGroup);
    m_wetStepEdit->setToolTip(tr("Runoff wet-weather step (WET_STEP)."));
    stepForm->addRow(tr("Wet-weat&her step:"), m_wetStepEdit);

    m_ruleStepEdit = new QCustomTimespanEdit(stepGroup);
    m_ruleStepEdit->setToolTip(tr("Control-rule evaluation step (RULE_STEP). "
                                   "0 means rules are evaluated every routing step."));
    stepForm->addRow(tr("Control rule step:"), m_ruleStepEdit);

    // Routing step — plain floating-point text box (no spin buttons),
    // displayed in seconds. Engine accepts decimal seconds via ROUTING_STEP.
    m_routingStepEdit = new QLineEdit(stepGroup);
    auto *routingValidator = new QDoubleValidator(0.001, 3600.0, 6, m_routingStepEdit);
    routingValidator->setNotation(QDoubleValidator::StandardNotation);
    m_routingStepEdit->setValidator(routingValidator);
    m_routingStepEdit->setPlaceholderText(QStringLiteral("seconds"));
    m_routingStepEdit->setToolTip(tr("Routing step in seconds (ROUTING_STEP)."));
    stepForm->addRow(tr("Routing step:"), m_routingStepEdit);

    stepTabLay->addWidget(stepGroup);

    // The 2D page mirrors REPORT_STEP and WET_STEP; one signal covers both.
    connect(m_reportStepEdit, &QCustomTimespanEdit::totalSecondsChanged, this,
            [this](qint64) { emit scheduleChanged(); });
    connect(m_wetStepEdit, &QCustomTimespanEdit::totalSecondsChanged, this,
            [this](qint64) { emit scheduleChanged(); });

    // ── Sweep / antecedent ─────────────────────────────────────────────
    auto *sweepGroup = new QGroupBox(tr("Sweep / antecedent"), winTab);
    auto *sweepForm  = new QFormLayout(sweepGroup);

    // SWEEP_START / SWEEP_END are MM/DD only — use a fixed year (2000, a
    // leap year so 02/29 stays selectable) internally and strip it on
    // write. Matches the legacy SWMM-GUI Delphi convention.
    m_sweepStartEdit = new QDateEdit(QDate(2000, 1, 1), sweepGroup);
    m_sweepStartEdit->setDisplayFormat(QStringLiteral("MM/dd"));
    m_sweepStartEdit->setCalendarPopup(true);
    m_sweepStartEdit->setToolTip(tr("Street-sweeping season start (SWEEP_START, MM/DD)."));
    sweepForm->addRow(tr("Start sweeping on:"), m_sweepStartEdit);

    m_sweepEndEdit = new QDateEdit(QDate(2000, 12, 31), sweepGroup);
    m_sweepEndEdit->setDisplayFormat(QStringLiteral("MM/dd"));
    m_sweepEndEdit->setCalendarPopup(true);
    m_sweepEndEdit->setToolTip(tr("Street-sweeping season end (SWEEP_END, MM/DD)."));
    sweepForm->addRow(tr("End sweeping on:"), m_sweepEndEdit);

    m_dryDaysSpin = new QDoubleSpinBox(sweepGroup);
    m_dryDaysSpin->setRange(0.0, 3650.0);
    m_dryDaysSpin->setDecimals(2);
    m_dryDaysSpin->setSuffix(QStringLiteral(" d"));
    sweepForm->addRow(tr("Antecedent dry days:"), m_dryDaysSpin);

    winTabLay->addWidget(sweepGroup);

    // ── Events ([EVENTS] section editor, Slice CW) ─────────────────────
    // Mirrors SWMM 5.2's [EVENTS] block: a list of {start, end} windows the
    // engine treats as routing-active periods when SKIP_STEADY_STATE is YES
    // (see swmm_is_between_events in openswmm_engine.h:301).  Two columns:
    // Start and End.  Cells use a QDateTimeEdit inline editor.  HH:MM
    // precision (legacy SWMM 5 parity, decided 2026-05-21).
    auto *evGroup = new QGroupBox(tr("Events ([EVENTS])"), evTab);
    auto *evLay   = new QVBoxLayout(evGroup);
    evGroup->setToolTip(tr(
        "Optional list of routing-active time windows. When Skip Steady State "
        "is on the engine routes full dynamic-wave hydraulics only inside "
        "these windows."));

    m_eventsTable = new QTableWidget(0, 2, evGroup);
    m_eventsTable->setHorizontalHeaderLabels(
        {tr("Start (MM/DD/YYYY HH:MM)"), tr("End (MM/DD/YYYY HH:MM)")});
    m_eventsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_eventsTable->verticalHeader()->setVisible(false);
    m_eventsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_eventsTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_eventsTable->setEditTriggers(QAbstractItemView::AllEditTriggers);
    evLay->addWidget(m_eventsTable);

    auto *evBtnRow = new QHBoxLayout();
    m_eventsAddBtn    = new QPushButton(tr("Add row"),         evGroup);
    m_eventsRemoveBtn = new QPushButton(tr("Remove selected"), evGroup);
    m_eventsRemoveBtn->setEnabled(false);
    evBtnRow->addWidget(m_eventsAddBtn);
    evBtnRow->addWidget(m_eventsRemoveBtn);
    evBtnRow->addStretch();
    evLay->addLayout(evBtnRow);

    connect(m_eventsAddBtn,    &QPushButton::clicked,
            this, &DatesPage::addEventRow);
    connect(m_eventsRemoveBtn, &QPushButton::clicked,
            this, &DatesPage::removeSelectedEventRows);
    // Gate on the selection MODEL, not selectedItems(): the cells hold only
    // setCellWidget() editors (no QTableWidgetItems), so selectedItems() is
    // always empty and an item-based gate leaves Remove permanently disabled.
    connect(m_eventsTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_eventsRemoveBtn->setEnabled(
            !SimulationOptionsDialog::selectedRowsDescending(m_eventsTable).isEmpty());
    });

    evTabLay->addWidget(evGroup, 1);

    winTabLay->addStretch();
    stepTabLay->addStretch();

    m_tabs->addTab(winTab,  tr("Simulation Window"));
    m_tabs->addTab(stepTab, tr("Time Steps"));
    m_tabs->addTab(evTab,   tr("Events"));
}

void DatesPage::tagWidgets()
{
    tagOption(m_startEdit, "START_DATE");
    tagOption(m_startEdit, "START_TIME");
    tagOption(m_endEdit, "END_DATE");
    tagOption(m_endEdit, "END_TIME");
    tagOption(m_reportStartEdit, "REPORT_START_DATE");
    tagOption(m_reportStartEdit, "REPORT_START_TIME");
    tagOption(m_reportStepEdit, "REPORT_STEP");
    tagOption(m_dryStepEdit, "DRY_STEP");
    tagOption(m_wetStepEdit, "WET_STEP");
    tagOption(m_ruleStepEdit, "RULE_STEP");
    tagOption(m_routingStepEdit, "ROUTING_STEP");
    tagOption(m_dryDaysSpin, "DRY_DAYS");
    tagOption(m_sweepStartEdit, "SWEEP_START");
    tagOption(m_sweepEndEdit, "SWEEP_END");
    tagOption(m_eventsTable, "[EVENTS]");
}

void DatesPage::updateDurationLabel()
{
    if (!m_durationLabel || !m_startEdit || !m_endEdit) return;
    const qint64 secs = m_startEdit->dateTime().secsTo(m_endEdit->dateTime());
    if (secs <= 0) {
        m_durationLabel->setText(QStringLiteral("—"));
        return;
    }
    const qint64 days  = secs / 86400;
    const qint64 hours = (secs % 86400) / 3600;
    const qint64 mins  = (secs % 3600) / 60;
    const qint64 ss    = secs % 60;
    m_durationLabel->setText(
        QString::asprintf("%lldd %02lld:%02lld:%02lld",
                          static_cast<long long>(days),
                          static_cast<long long>(hours),
                          static_cast<long long>(mins),
                          static_cast<long long>(ss)));
}

void DatesPage::read()
{
    // Source every fallback from PreferencesManager so the dialog shows the
    // user-preferred default whenever the engine has no value for a key —
    // keeps the new-project synthesis path and the missing-key path in
    // lockstep and avoids hardcoded magic-number drift.
    const auto sim = PreferencesManager::instance()->simulationDefaults();
    using SOD = SimulationOptionsDialog;

    // Block signals on Start/Report-start during seeding so the clamp
    // connection doesn't bump report-start prematurely between the two
    // reads. Seed the minimum + duration label explicitly at the end.
    {
        QSignalBlocker bs(m_startEdit);
        QSignalBlocker br(m_reportStartEdit);

        QDateTime start = SOD::parseEngineDateTime(
            ctx_.option("START_DATE"), ctx_.option("START_TIME", "00:00:00"));
        if (start.isValid()) m_startEdit->setDateTime(start);

        QDateTime end = SOD::parseEngineDateTime(
            ctx_.option("END_DATE"),   ctx_.option("END_TIME",   "00:00:00"));
        if (end.isValid()) m_endEdit->setDateTime(end);

        QDateTime rpt = SOD::parseEngineDateTime(
            ctx_.option("REPORT_START_DATE"),
            ctx_.option("REPORT_START_TIME", "00:00:00"));
        if (rpt.isValid()) m_reportStartEdit->setDateTime(rpt);
    }
    m_reportStartEdit->setMinimumDateTime(m_startEdit->dateTime());
    updateDurationLabel();

    // Engine round-trip for step values is loose: a step may come back as
    // plain seconds ("900"), decimal seconds ("900.000000") or as HH:MM:SS
    // ("00:15:00", "48:00:00"). The static parseStepSeconds() helper
    // (simulationoptionshelpers.cpp, unit-tested) accepts all three.
    m_reportStepEdit->setTotalSeconds(
        SOD::parseStepSeconds(ctx_.option("REPORT_STEP", QString::number(sim.reportStepSec)),
                              sim.reportStepSec));
    m_dryStepEdit->setTotalSeconds(
        SOD::parseStepSeconds(ctx_.option("DRY_STEP", QString::number(sim.dryStepSec)),
                              sim.dryStepSec));
    m_wetStepEdit->setTotalSeconds(
        SOD::parseStepSeconds(ctx_.option("WET_STEP", QString::number(sim.wetStepSec)),
                              sim.wetStepSec));
    m_ruleStepEdit->setTotalSeconds(
        SOD::parseStepSeconds(ctx_.option("RULE_STEP", QString::number(sim.ruleStepSec)),
                              sim.ruleStepSec));

    bool ok = false;
    const double routeStep = ctx_.option("ROUTING_STEP",
                                         QString::number(sim.routingStepSec, 'g', 6))
                                 .toDouble(&ok);
    m_routingStepEdit->setText(
        QString::number(ok ? routeStep : sim.routingStepSec, 'g', 6));

    const double dryDays = ctx_.option("DRY_DAYS", QString::number(sim.dryDays, 'g', 6))
                               .toDouble(&ok);
    m_dryDaysSpin->setValue(ok ? dryDays : sim.dryDays);

    // Sweep window — engine stores "MM/DD"; map into a fixed-year QDate
    // (2000 is a leap year so 02/29 stays selectable).
    auto parseSweep = [](const QString &s, QDate fallback) {
        const QStringList parts = s.split(QLatin1Char('/'));
        if (parts.size() != 2) return fallback;
        bool okM = false, okD = false;
        const int m = parts[0].toInt(&okM);
        const int d = parts[1].toInt(&okD);
        if (!okM || !okD) return fallback;
        const QDate q(2000, m, d);
        return q.isValid() ? q : fallback;
    };
    const QDate sweepStartPref = parseSweep(sim.sweepStart, QDate(2000, 1, 1));
    const QDate sweepEndPref   = parseSweep(sim.sweepEnd,   QDate(2000, 12, 31));
    m_sweepStartEdit->setDate(parseSweep(ctx_.option("SWEEP_START", sim.sweepStart),
                                         sweepStartPref));
    m_sweepEndEdit->setDate(parseSweep(ctx_.option("SWEEP_END", sim.sweepEnd),
                                       sweepEndPref));

    readEventsFromEngine();
    emit scheduleChanged();
}

int DatesPage::write()
{
    int n = 0;
    using SOD = SimulationOptionsDialog;

    QString d, t;
    SOD::formatEngineDateTime(m_startEdit->dateTime(), d, t);
    n += ctx_.writeIfChanged("START_DATE", d);
    n += ctx_.writeIfChanged("START_TIME", t);

    SOD::formatEngineDateTime(m_endEdit->dateTime(), d, t);
    n += ctx_.writeIfChanged("END_DATE", d);
    n += ctx_.writeIfChanged("END_TIME", t);

    SOD::formatEngineDateTime(m_reportStartEdit->dateTime(), d, t);
    n += ctx_.writeIfChanged("REPORT_START_DATE", d);
    n += ctx_.writeIfChanged("REPORT_START_TIME", t);

    n += ctx_.writeIfChanged("REPORT_STEP",
                             QString::number(m_reportStepEdit->totalSeconds()));
    n += ctx_.writeIfChanged("DRY_STEP",
                             QString::number(m_dryStepEdit->totalSeconds()));
    n += ctx_.writeIfChanged("WET_STEP",
                             QString::number(m_wetStepEdit->totalSeconds()));
    n += ctx_.writeIfChanged("RULE_STEP",
                             QString::number(m_ruleStepEdit->totalSeconds()));
    {
        // Routing step is a plain text box; preserve the user's typed
        // decimal precision but normalise to a canonical %g rendering.
        bool ok = false;
        const double v = m_routingStepEdit->text().trimmed().toDouble(&ok);
        if (ok)
            n += ctx_.writeIfChanged("ROUTING_STEP", QString::number(v, 'g', 6));
    }
    n += ctx_.writeIfChanged("DRY_DAYS",
                             QString::number(m_dryDaysSpin->value(), 'f', 2));
    n += ctx_.writeIfChanged("SWEEP_START",
                             m_sweepStartEdit->date().toString(QStringLiteral("MM/dd")));
    n += ctx_.writeIfChanged("SWEEP_END",
                             m_sweepEndEdit->date().toString(QStringLiteral("MM/dd")));

    // [EVENTS] (Slice CW). writeEventsToEngine() returns the number of rows
    // it actually pushed; folded into n so wroteChanges flips.
    n += writeEventsToEngine();
    return n;
}

// ---------------------------------------------------------------------------
// [EVENTS] section helpers (Slice CW — 2026-05-21)
// ---------------------------------------------------------------------------
// oaDateFromQDateTime / qDateTimeFromOaDate are static methods on
// SimulationOptionsDialog defined in simulationoptionshelpers.cpp so the
// leaf QtTest can link them without dragging the spatial-tab OGR cascade.

void DatesPage::addEventRow()
{
    if (!m_eventsTable) return;
    const int row = m_eventsTable->rowCount();
    m_eventsTable->insertRow(row);
    // Default both columns to (project start, project end) so the user only
    // edits the deltas.  Fall back to "now" when the window edits haven't
    // been populated yet (shouldn't happen — buildUi seeds them).
    const QDateTime defStart = m_startEdit ? m_startEdit->dateTime()
                                           : QDateTime::currentDateTime();
    const QDateTime defEnd   = m_endEdit   ? m_endEdit->dateTime()
                                           : defStart.addDays(1);
    m_eventsTable->setCellWidget(row, 0, makeEventCellEditor(defStart, m_eventsTable));
    m_eventsTable->setCellWidget(row, 1, makeEventCellEditor(defEnd,   m_eventsTable));
}

void DatesPage::removeSelectedEventRows()
{
    if (!m_eventsTable) return;
    // Distinct rows, descending, so removeRow() doesn't shift the indices
    // we still need to delete. Shares the query with the Remove-button
    // enable gate so the two can't disagree about what counts as selected.
    const QList<int> rows =
        SimulationOptionsDialog::selectedRowsDescending(m_eventsTable);
    for (int r : rows)
        m_eventsTable->removeRow(r);
}

void DatesPage::readEventsFromEngine()
{
    if (!m_eventsTable) return;

    // Wipe before refilling — read() is also called after a write pass to
    // surface engine-normalised values.
    m_eventsTable->setRowCount(0);
    m_eventsSnapshot.clear();

    SWMM_Engine e = ctx_.engine();
    if (!e) return;

    int count = 0;
    if (swmm_events_count(e, &count) != 0) return;

    for (int i = 0; i < count; ++i) {
        double start = 0.0, end = 0.0;
        if (swmm_events_get(e, i, &start, &end) != 0) continue;
        const QDateTime qs = SimulationOptionsDialog::qDateTimeFromOaDate(start);
        const QDateTime qe = SimulationOptionsDialog::qDateTimeFromOaDate(end);

        const int row = m_eventsTable->rowCount();
        m_eventsTable->insertRow(row);
        m_eventsTable->setCellWidget(row, 0, makeEventCellEditor(qs, m_eventsTable));
        m_eventsTable->setCellWidget(row, 1, makeEventCellEditor(qe, m_eventsTable));
        m_eventsSnapshot.append(qMakePair(qs, qe));
    }
}

bool DatesPage::validate(QString *warn)
{
    if (!m_eventsTable) return true;

    bool anyInvalid = false;
    QList<QPair<QDateTime, QDateTime>> rows;
    const int n = m_eventsTable->rowCount();
    rows.reserve(n);
    // Tokenized error fill (D5) — the old hardcoded #ffc8c8 was
    // illegible on the dark theme.
    const QString badStyle = QStringLiteral("QDateTimeEdit { %1 }")
                                 .arg(openswmmvis::ui::theme::errorFillStyle());
    for (int r = 0; r < n; ++r) {
        auto *startEdit = qobject_cast<QDateTimeEdit *>(
            m_eventsTable->cellWidget(r, 0));
        auto *endEdit   = qobject_cast<QDateTimeEdit *>(
            m_eventsTable->cellWidget(r, 1));
        if (!startEdit || !endEdit) { anyInvalid = true; continue; }
        const QDateTime s = startEdit->dateTime();
        const QDateTime e = endEdit->dateTime();
        rows.append(qMakePair(s, e));

        const bool bad = !(s < e);
        startEdit->setStyleSheet(bad ? badStyle : QString());
        endEdit  ->setStyleSheet(bad ? badStyle : QString());
        const QString tip = bad ? tr("Start must be earlier than End.")
                                : QString();
        startEdit->setToolTip(tip);
        endEdit  ->setToolTip(tip);
        if (bad) anyInvalid = true;
    }

    if (warn) {
        // Out-of-range check against the simulation window.
        const QDateTime simStart = m_startEdit ? m_startEdit->dateTime() : QDateTime();
        const QDateTime simEnd   = m_endEdit   ? m_endEdit->dateTime()   : QDateTime();
        for (int r = 0; r < rows.size(); ++r) {
            const auto &p = rows[r];
            if (simStart.isValid() && simEnd.isValid()
                && (p.second <= simStart || p.first >= simEnd))
            {
                *warn += tr("Row %1 lies entirely outside the simulation window.\n")
                            .arg(r + 1);
            }
        }
        // Overlap detection: O(n^2) — n is small (typically << 20).
        for (int i = 0; i < rows.size(); ++i)
            for (int j = i + 1; j < rows.size(); ++j)
                if (rows[i].first < rows[j].second &&
                    rows[j].first < rows[i].second)
                {
                    *warn += tr("Rows %1 and %2 overlap.\n")
                                .arg(i + 1).arg(j + 1);
                }
    }

    return !anyInvalid;
}

int DatesPage::writeEventsToEngine()
{
    SWMM_Engine e = ctx_.engine();
    if (!m_eventsTable || !e) return 0;

    // Snapshot the table into a flat list for diffing against m_eventsSnapshot.
    QList<QPair<QDateTime, QDateTime>> current;
    const int n = m_eventsTable->rowCount();
    current.reserve(n);
    for (int r = 0; r < n; ++r) {
        auto *startEdit = qobject_cast<QDateTimeEdit *>(
            m_eventsTable->cellWidget(r, 0));
        auto *endEdit   = qobject_cast<QDateTimeEdit *>(
            m_eventsTable->cellWidget(r, 1));
        if (!startEdit || !endEdit) continue;
        current.append(qMakePair(startEdit->dateTime(), endEdit->dateTime()));
    }

    if (current == m_eventsSnapshot)
        return 0;   // no change → no write, no dirty flag

    if (swmm_events_clear(e) != 0)
        return 0;

    int written = 0;
    for (const auto &p : current) {
        const double start = SimulationOptionsDialog::oaDateFromQDateTime(p.first);
        const double end   = SimulationOptionsDialog::oaDateFromQDateTime(p.second);
        if (!(start < end)) continue;   // skip invalid rows defensively
        if (swmm_events_add(e, start, end, nullptr) == 0)
            ++written;
    }

    m_eventsSnapshot = current;
    return written;
}

} // namespace openswmmvis::ui
