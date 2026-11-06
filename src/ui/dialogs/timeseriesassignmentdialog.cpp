/**
 * @file timeseriesassignmentdialog.cpp
 * @brief Explicit observation-to-model preparation, preview and checked atomic commit.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @license GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */
#include "ui/dialogs/timeseriesassignmentdialog.h"
#include "swmmvisprojectwindow.h"
#include "layers/swmmmodellayer.h"
#include "plugins/extensionmodelaccess.h"
#include "sdk/IEditScope.hpp"
#include "core/unitsystem.h"
#include "ui/properties/dataobjectref.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimeZone>
#include <QVBoxLayout>
#include <openswmm/engine/openswmm_tables.h>
#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_nodes.h>

namespace openswmmvis::ui
{
using namespace OpenSWMM::TimeSeries;
bool applyPreparedTimeSeries(SWMMVisProjectWindow *project, const PreparedAssignment &p, QString *error)
{
    if (error) error->clear();
    if (!project || project->isClosing() || !project->modelLayer() || !p.ok()) {
        if (error) *error = QObject::tr("The prepared series or destination model is unavailable.");
        return false;
    }
    if (p.options.target == AssignmentTarget::NodeFlow) {
        const auto engine = project->modelLayer()->engine();
        const int target = swmm_node_index(engine,p.options.targetObject.toUtf8().constData());
        for (int i=0; i<swmm_ext_inflow_count(engine); ++i) {
            int node=-1; char constituent[1024]={},series[1024]={},type[64]={},pattern[1024]={};
            double multiplier=0,scale=0,baseline=0;
            if (swmm_ext_inflow_get(engine,i,&node,constituent,sizeof constituent,series,sizeof series,
                                   type,sizeof type,&multiplier,&scale,&baseline,pattern,sizeof pattern) != SWMM_OK) {
                if (error) *error=QObject::tr("Existing inflows could not be validated.");
                return false;
            }
            if (node==target && QString::fromUtf8(constituent).compare("FLOW",Qt::CaseInsensitive)==0) {
                if (error) *error=QObject::tr("This node already has a FLOW inflow. Create the time series only, then select it in the existing inflow editor.");
                return false;
            }
        }
    }
    auto scope = openswmmvis::beginTimeSeriesModelEdit(project, QObject::tr("Use observations in model"), error);
    if (!scope) return false;
    if (!scope->addTimeSeries(p.options.modelSeriesName, p.times, p.values, false)) {
        if (error) *error = QObject::tr("The model refused the new series. Its name may already exist.");
        return false;
    }
    bool bound = true;
    switch (p.options.target) {
    case AssignmentTarget::SeriesOnly: break;
    case AssignmentTarget::NodeFlow:
        bound = scope->addInflow(p.options.targetObject, "FLOW", p.options.modelSeriesName, "FLOW", 1., 0.);
        break;
    case AssignmentTarget::OutfallStage: {
        DataObjectRef ref; ref.kind = DataObjectRef::TimeSeries; ref.currentName = p.options.modelSeriesName;
        ref.engine = project->modelLayer()->engine(); ref.layer = project->modelLayer();
        bound = scope->setAttribute("node", p.options.targetObject, "outfallTimeseries", QVariant::fromValue(ref), error);
        break;
    }
    case AssignmentTarget::RainGage:
        bound = scope->setGageSeries(p.options.targetObject, p.options.modelSeriesName,
                                     p.options.rainfallIntervalSeconds, p.options.rainfallFormat);
        break;
    }
    if (!bound) {
        if (error && error->isEmpty()) *error = QObject::tr("The model refused the selected binding; no changes were retained.");
        return false;
    }
    auto provenance = project->property("timeSeriesAssignments").toJsonArray();
    provenance.append(p.provenance);
    project->setProperty("timeSeriesAssignments", provenance);
    return scope->commit(error);
}
/** @brief Detached preparation state; the completion flag publishes the owning result. */
struct TimeSeriesAssignmentDialog::Work {
    quint64 revision = 0; ///< Input revision copied before launch; stale completions cannot enable Apply.
    PreparedAssignment result; ///< Worker-owned output, read only after acquiring the completion flag.
    std::atomic_bool cancel{false}; ///< Cooperative cancellation checked between samples and pages.
    std::atomic_bool done{false}; ///< Release/acquire publication barrier for the completed preparation.
};
TimeSeriesAssignmentDialog::TimeSeriesAssignmentDialog(SWMMVisProjectWindow *project,
    std::shared_ptr<const TimeSeriesDataset> dataset, QString id, QWidget *parent)
    : QDialog(parent), m_project(project), m_dataset(std::move(dataset))
{
    setWindowTitle(tr("Use time series in model"));
    setObjectName("TimeSeriesAssignmentDialog");
    resize(660, 700);
    if (m_dataset) for (const auto &s : m_dataset->series) if (s.id == id) { m_series = s; break; }
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Create a model-owned copy of %1. The downloaded observations stay unchanged.").arg(m_series.title), this);
    intro->setWordWrap(true); layout->addWidget(intro);
    auto *form = new QFormLayout;
    m_name = new QLineEdit(this); m_name->setObjectName("modelSeriesName");
    QString proposed = QStringLiteral("OBS_%1_%2").arg(m_series.siteId, m_series.parameter);
    proposed.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
    if (proposed.size() > 28) proposed = proposed.left(28);
    if (project && project->modelLayer() && project->modelLayer()->engine()) {
        const auto stem=proposed;
        for (int suffix=2; swmm_table_index(project->modelLayer()->engine(),proposed.toUtf8().constData())>=0; ++suffix)
            proposed=stem.left(24)+QStringLiteral("_%1").arg(suffix);
    }
    m_name->setText(proposed);
    m_target = new QComboBox(this); m_target->setObjectName("assignmentTarget");
    m_target->addItems({tr("Create time series only"), tr("Node FLOW inflow"), tr("Outfall stage"), tr("Rain gage")});
    m_object = new QLineEdit(this); m_object->setObjectName("targetObject");
    m_unit = new QLineEdit(m_series.unit, this); m_unit->setObjectName("targetUnit");
    m_unitsConfirmed = new QCheckBox(tr("These are the intended model units"), this);
    m_unitsConfirmed->setObjectName("confirmUnits");
    m_clock = new QComboBox(this); m_clock->setObjectName("modelClock");
    m_clock->addItems({tr("Choose model clock…"), tr("Model wall clock uses UTC"), tr("Fixed offset from UTC")});
    if (m_series.timeBasis == TimeBasis::CalendarDate) {
        m_clock->clear(); m_clock->addItem(tr("Keep source calendar dates"));
    }
    m_offset = new QDoubleSpinBox(this); m_offset->setRange(-14, 14); m_offset->setDecimals(2); m_offset->setSuffix(tr(" hours"));
    m_from = new QDateTimeEdit(this); m_to = new QDateTimeEdit(this);
    for (auto *edit : {m_from, m_to}) { edit->setTimeZone(QTimeZone::UTC); edit->setCalendarPopup(true); edit->setDisplayFormat(m_series.timeBasis == TimeBasis::CalendarDate ? "yyyy-MM-dd" : "yyyy-MM-dd HH:mm:ss"); }
    const auto dt = [&](qint64 key) { return m_series.timeBasis == TimeBasis::CalendarDate ? QDateTime(QDate::fromJulianDay(key), QTime(0,0), QTimeZone::UTC) : QDateTime::fromMSecsSinceEpoch(key, QTimeZone::UTC); };
    m_from->setDateTime(dt(m_series.firstKey)); m_to->setDateTime(dt(m_series.lastKey));
    m_daily = new QCheckBox(tr("Hold each daily statistic across its calendar day (one-second transition between days)"), this);
    m_daily->setObjectName("confirmDailyHold");
    m_quality = new QCheckBox(tr("Include provisional and qualified observations"), this);
    m_interval = new QSpinBox(this); m_interval->setRange(0, 86400*30); m_interval->setSpecialValueText(tr("Check spacing from record")); m_interval->setSuffix(tr(" seconds"));
    m_irregular = new QCheckBox(tr("Allow linear interpolation of irregular finite samples"), this);
    m_datum = new QDoubleSpinBox(this); m_datum->setRange(-1e7,1e7); m_datum->setDecimals(5);
    m_datumConfirmed = new QCheckBox(tr("I reviewed the datum; this offset produces absolute model stage"), this);
    m_rainFormat = new QComboBox(this); m_rainFormat->addItems({tr("Intensity"),tr("Volume"),tr("Cumulative")});
    m_rainInterval = new QSpinBox(this); m_rainInterval->setRange(1,86400*30); m_rainInterval->setValue(900); m_rainInterval->setSuffix(tr(" seconds"));
    form->addRow(tr("New series name"), m_name); form->addRow(tr("Use"), m_target);
    form->addRow(tr("Existing object ID"), m_object); form->addRow(tr("Target units"),m_unit);
    form->addRow(m_unitsConfirmed); form->addRow(tr("From (source time)"),m_from); form->addRow(tr("Through (source time)"),m_to);
    form->addRow(tr("Model clock"),m_clock); form->addRow(tr("UTC offset"),m_offset);
    form->addRow(m_daily); form->addRow(tr("Expected interval"),m_interval); form->addRow(m_irregular); form->addRow(m_quality);
    form->addRow(tr("Stage datum offset"),m_datum); form->addRow(m_datumConfirmed);
    form->addRow(tr("Rainfall format"),m_rainFormat); form->addRow(tr("Rainfall interval"),m_rainInterval);
    layout->addLayout(form);
    m_preview = new QTableWidget(0,2,this); m_preview->setHorizontalHeaderLabels({tr("Model time"),tr("Value")});
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers); m_preview->horizontalHeader()->setStretchLastSection(true); m_preview->setMaximumHeight(150); layout->addWidget(m_preview);
    m_status = new QLabel(tr("Review the units and time interpretation, then Prepare preview."),this); m_status->setWordWrap(true); m_status->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_prepare = buttons->addButton(tr("Prepare preview"),QDialogButtonBox::ActionRole);
    m_apply = buttons->addButton(tr("Create and apply"),QDialogButtonBox::AcceptRole); m_apply->setEnabled(false);
    layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(m_prepare,&QPushButton::clicked,this,&TimeSeriesAssignmentDialog::prepare);
    connect(m_apply,&QPushButton::clicked,this,&TimeSeriesAssignmentDialog::apply);
    for (auto *edit : {m_name,m_object,m_unit}) connect(edit,&QLineEdit::textChanged,this,&TimeSeriesAssignmentDialog::invalidate);
    for (auto *box : {m_unitsConfirmed,m_daily,m_quality,m_irregular,m_datumConfirmed}) connect(box,&QCheckBox::toggled,this,&TimeSeriesAssignmentDialog::invalidate);
    for (auto *spin : {m_interval,m_rainInterval}) connect(spin,&QSpinBox::valueChanged,this,&TimeSeriesAssignmentDialog::invalidate);
    for (auto *spin : {m_offset,m_datum}) connect(spin,&QDoubleSpinBox::valueChanged,this,&TimeSeriesAssignmentDialog::invalidate);
    for (auto *edit : {m_from,m_to}) connect(edit,&QDateTimeEdit::dateTimeChanged,this,&TimeSeriesAssignmentDialog::invalidate);
    connect(m_target,&QComboBox::currentIndexChanged,this,&TimeSeriesAssignmentDialog::updateTarget);
    connect(m_rainFormat,&QComboBox::currentIndexChanged,this,&TimeSeriesAssignmentDialog::updateTarget);
    connect(m_clock,&QComboBox::currentIndexChanged,this,&TimeSeriesAssignmentDialog::updateTarget);
    if (project) {
        connect(project,&SWMMVisProjectWindow::aboutToClose,this,&QDialog::reject);
        if (project->modelLayer()) connect(project->modelLayer(),&SWMMModelLayer::modelEdited,this,&TimeSeriesAssignmentDialog::invalidate);
    }
    m_timer.setInterval(50); connect(&m_timer,&QTimer::timeout,this,&TimeSeriesAssignmentDialog::poll);
    updateTarget();
}
TimeSeriesAssignmentDialog::~TimeSeriesAssignmentDialog()
{
    if (m_work) m_work->cancel = true;
    if (m_thread.joinable()) m_thread.join();
}
void TimeSeriesAssignmentDialog::invalidate()
{
    ++m_revision; m_prepared = {}; m_apply->setEnabled(false);
    if (m_work) m_work->cancel = true;
}
void TimeSeriesAssignmentDialog::updateTarget()
{
    const auto target = AssignmentTarget(m_target->currentIndex());
    // Labels participate in the form's row visibility so hidden expert fields leave no dead rows.
    auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
    auto *fields = layout ? qobject_cast<QFormLayout *>(layout->itemAt(1)->layout()) : nullptr;
    auto show = [&](QWidget *w, bool on) { if (fields) fields->setRowVisible(w,on); else w->setVisible(on); };
    show(m_object,target != AssignmentTarget::SeriesOnly);
    show(m_offset,m_series.timeBasis == TimeBasis::Instant && m_clock->currentIndex() == 2);
    show(m_daily,m_series.timeBasis == TimeBasis::CalendarDate);
    show(m_interval,m_series.timeBasis == TimeBasis::Instant);
    show(m_irregular,m_series.timeBasis == TimeBasis::Instant);
    show(m_datum,target == AssignmentTarget::OutfallStage); show(m_datumConfirmed,target == AssignmentTarget::OutfallStage);
    show(m_rainFormat,target == AssignmentTarget::RainGage); show(m_rainInterval,target == AssignmentTarget::RainGage);
    const auto *units = m_project ? m_project->unitSystem() : nullptr;
    if (target == AssignmentTarget::SeriesOnly) m_unit->setText(m_series.unit);
    else if (units) {
        if (target == AssignmentTarget::NodeFlow) m_unit->setText(units->flowUnitLabel());
        else if (target == AssignmentTarget::OutfallStage) m_unit->setText(units->lengthLabel());
        else m_unit->setText((units->isSI() ? QStringLiteral("mm") : QStringLiteral("in")) + (m_rainFormat->currentIndex() == 0 ? "/hr" : ""));
    }
    m_unit->setReadOnly(target != AssignmentTarget::SeriesOnly);
    m_unitsConfirmed->setChecked(false); invalidate();
}
void TimeSeriesAssignmentDialog::prepare()
{
    if (m_work || !m_dataset || !m_project || m_project->isClosing() || !m_project->modelLayer()) return;
    AssignmentOptions o; o.seriesId = m_series.id; o.modelSeriesName = m_name->text().trimmed();
    o.target = AssignmentTarget(m_target->currentIndex()); o.targetObject = m_object->text().trimmed(); o.targetUnit = m_unit->text().trimmed();
    o.clockConfirmed = m_series.timeBasis == TimeBasis::CalendarDate || m_clock->currentIndex() > 0;
    o.utcOffsetSeconds = m_clock->currentIndex() == 2 ? qRound(m_offset->value()*3600) : 0;
    o.dailyConstantConfirmed = m_daily->isChecked(); o.allowQualified = m_quality->isChecked(); o.allowIrregularInterpolation = m_irregular->isChecked();
    o.expectedIntervalSeconds = m_interval->value(); o.unitInterpretationConfirmed = m_unitsConfirmed->isChecked();
    o.datumConfirmed = m_datumConfirmed->isChecked(); o.datumOffset = m_datum->value();
    o.rainfallFormat = m_rainFormat->currentIndex(); o.rainfallIntervalSeconds = m_rainInterval->value();
    o.fromKey = m_series.timeBasis == TimeBasis::CalendarDate ? m_from->date().toJulianDay() : m_from->dateTime().toMSecsSinceEpoch();
    o.toKey = m_series.timeBasis == TimeBasis::CalendarDate ? m_to->date().toJulianDay() : m_to->dateTime().toMSecsSinceEpoch();
    m_engine = m_project->modelLayer()->engine();
    m_work = std::make_shared<Work>(); m_work->revision = m_revision;
    const auto state = m_work; const auto dataset = m_dataset;
    m_thread = std::thread([state,dataset,o] { state->result = prepareAssignment(*dataset,o,&state->cancel); state->done.store(true,std::memory_order_release); });
    m_prepare->setEnabled(false); m_apply->setEnabled(false); m_status->setText(tr("Preparing full-resolution observations…")); m_timer.start();
}
void TimeSeriesAssignmentDialog::poll()
{
    if (!m_work || !m_work->done.load(std::memory_order_acquire)) return;
    m_thread.join(); auto state = std::move(m_work); m_work.reset(); m_timer.stop(); m_prepare->setEnabled(true);
    if (state->revision != m_revision || state->cancel) { m_status->setText(tr("The inputs changed. Prepare a new preview.")); return; }
    m_prepared = std::move(state->result); m_preview->setRowCount(0);
    if (!m_prepared.ok()) { m_status->setText(m_prepared.error); return; }
    const int total = m_prepared.times.size();
    const int shown = std::min(total, 12); m_preview->setRowCount(shown);
    for (int row=0; row<shown; ++row) {
        const int i = total > shown && row >= shown/2 ? total-shown+row : row;
        m_preview->setItem(row,0,new QTableWidgetItem(m_prepared.times[i].toString("yyyy-MM-dd HH:mm:ss")));
        m_preview->setItem(row,1,new QTableWidgetItem(QString::number(m_prepared.values[i],'g',12)));
    }
    m_status->setText(tr("Ready: %1 model points in %2. Preview shows %3 first/last points. Create and apply makes one undoable copy.").arg(total).arg(m_prepared.options.targetUnit).arg(shown));
    m_apply->setEnabled(true);
}
void TimeSeriesAssignmentDialog::apply()
{
    if (!m_prepared.ok() || !m_project || m_project->isClosing() || !m_project->modelLayer() || m_project->modelLayer()->engine() != m_engine) {
        invalidate(); m_status->setText(tr("The destination changed. Prepare a new preview.")); return;
    }
    const auto prepared = m_prepared; // modelEdited invalidates this dialog during the checked commit.
    QString error;
    if (!applyPreparedTimeSeries(m_project,prepared,&error)) { m_status->setText(error); return; }
    accept();
}
}
