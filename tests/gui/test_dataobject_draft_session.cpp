/**
 * @file      test_dataobject_draft_session.cpp
 * @brief     Explicit Apply / Revert in a data object editor against a real
 *            model (workplans/DATA_OBJECT_APPLY_REVERT_PLAN_2026-11-11.md,
 *            Phase 2 — Pollutant pilot).
 *
 * Guards:
 *   1. A draft is invisible to the engine and the Attribute Table until Apply.
 *   2. Apply writes the engine, marks the project edited, and is one undo
 *      step that still reaches the engine after the editor has closed.
 *   3. An edit to the same object from another view neither wipes the draft
 *      nor is overwritten without asking.
 *   4. A refused Apply leaves the engine as it was and keeps the draft.
 *   5. Save writes the APPLIED state: applied edits are in the file (.inp and
 *      GeoPackage), an unapplied draft is not.
 *   Time series (Phase 3): guards 1, 2 and 5 for the series editor, plus the
 *   cost of a draft, an edit, Apply and its Undo at 100k and 1M points.
 *
 * Saved files are kept in workplans/data-object-apply/phase2/review (pollutant)
 * and phase3/review (time series, timing.md).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmmmodellayer.h"
#include "pollutant/pollutantprovider.h"
#include "pollutant/pollutantregistry.h"
#include "project/projectserializer.h"
#include "timeseries/timeseriesassignment.h"
#include "timeseries/timeseriesstore.h"
#include "layers/timeserieslayer.h"
#include "map/mapcanvas.h"
#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesregistry.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/pollutanteditordialog.h"
#include "aquifer/aquiferprovider.h"
#include "inlet/inletprovider.h"
#include "ui/dialogs/aquifereditordialog.h"
#include "ui/dialogs/inleteditordialog.h"
#include "ui/dialogs/streeteditordialog.h"
#include "ui/dialogs/transecteditordialog.h"
#include "ui/dialogs/snowpackeditordialog.h"
#include "ui/dialogs/landuseeditordialog.h"
#include "ui/dialogs/ruleseditordialog.h"
#include "ui/dialogs/hydrographgroupeditor.h"
#include "ui/dialogs/lidcontroleditordialog.h"
#include "lid/lidcontrolregistry.h"
#include "layers/hydrographmodels.h"
#include "ui/models/qualityfunctiontablemodels.h"
#include "snowpack/snowpackprovider.h"
#include "snowpack/snowpackregistry.h"
#include "transect/transectprovider.h"
#include "ui/dialogs/timeseriesassignmentdialog.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "ui/editors/comprehensiveeditorregistry.h"
#include "ui/editors/dataobjectdraftsession.h"
#include "ui/panels/dataobjectattributetablemodel.h"
#include "ui/panels/timeseriestablemodel.h"

#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_tables.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_snow.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_controls.h>
#include <openswmm/engine/openswmm_inflows.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QListWidget>
#include <QTextEdit>
#include <QPointer>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QScopeGuard>
#include <QTextStream>
#include <QTimer>
#include <QTimeZone>
#include <QUndoStack>

#include <cmath>
#include <functional>
#include <limits>

using openswmmvis::pollutant::PollutantRegistry;
using openswmmvis::timeseries::TimeseriesPoint;
using openswmmvis::timeseries::TimeseriesRegistry;
using openswmmvis::ui::PollutantEditorDialog;
using openswmmvis::ui::TimeseriesEditorDialog;
using openswmmvis::ui::DataObjectDraftSession;
using State = openswmmvis::ui::DataObjectDraftSession::State;

namespace
{

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/** @brief Reviewable output folder: &lt;repo&gt;/workplans/data-object-apply/&lt;phase&gt;/review. */
QString outDir(const QString &phase = QStringLiteral("phase2"))
{
    QDir d(dataDir());
    d.cdUp();
    d.cdUp();
    d.cdUp();
    const QString out =
        d.filePath(QStringLiteral("workplans/data-object-apply/%1/review").arg(phase));
    QDir().mkpath(out);
    return out;
}

SWMMVisProjectWindow *openWindow(const QString &path, QString *why)
{
    auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    auto *window = new SWMMVisProjectWindow(workspace, path, nullptr);
    QList<QString> warnings, errors;
    if (!window->loadModel(warnings, errors)) {
        *why = errors.join(QLatin1String("; "));
        delete window;
        return nullptr;
    }
    return window;
}

/** A fresh copy of a fixture, opened in a project window. */
SWMMVisProjectWindow *openFixtureCopy(const QString &tag, QString *why,
                                      const QString &fixture = QStringLiteral("data_object_table_fixture.inp"),
                                      const QString &phase = QStringLiteral("phase2"))
{
    const QString src = outDir(phase) + QStringLiteral("/%1_src.inp").arg(tag);
    QFile::remove(src);
    if (!QFile::copy(QDir(dataDir()).filePath(fixture), src)) {
        *why = QStringLiteral("cannot copy fixture");
        return nullptr;
    }
    return openWindow(src, why);
}

const QString kSeries = QStringLiteral("TSERIES1");

/** Engine value of row \p row of series \p name, or NaN. */
double engineSeriesValue(SWMMModelLayer *layer, const QString &name, int row)
{
    const int idx = swmm_table_index(layer->engine(), name.toUtf8().constData());
    double x = 0.0, y = std::numeric_limits<double>::quiet_NaN();
    if (idx < 0 || swmm_table_get_point(layer->engine(), idx, row, &x, &y) != SWMM_OK)
        return std::numeric_limits<double>::quiet_NaN();
    return y;
}

TimeseriesEditorDialog *openSeriesEditor(SWMMModelLayer *layer, QUndoStack *undo, QWidget *host,
                                         const QString &name)
{
    ComprehensiveEditorRegistry::instance().openForObject(SWMMModelLayer::DataTimeSeries, layer,
                                                          undo, name, host);
    for (auto *dialog : host->findChildren<TimeseriesEditorDialog *>())
        if (dialog->isVisible() && dialog->currentName() == name)
            return dialog;
    return nullptr;
}

/** A native observation dataset (one 15-minute flow series), kept for review in \p root. */
std::shared_ptr<const OpenSWMM::TimeSeries::TimeSeriesDataset> makeObservationDataset(const QString &root,
                                                                                      QString *error)
{
    using namespace OpenSWMM::TimeSeries;
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    {
        QFile csv(root + QStringLiteral("/samples.csv"));
        if (!csv.open(QIODevice::WriteOnly | QIODevice::Text))
            return {};
        csv.write("time,value\n1990-01-18T00:00:00Z,1.5\n1990-01-18T00:15:00Z,2.5\n"
                  "1990-01-18T00:30:00Z,3.5\n1990-01-18T00:45:00Z,4.5\n");
    }
    const QJsonObject defaults{{"provider", "fixture"},   {"collection", "observations"},
                               {"siteId", "00123"},       {"sourceSeriesId", "flow-01"},
                               {"parameter", "00060"},    {"statistic", "instantaneous"},
                               {"timeBasis", "instant"},  {"unit", "ft^3/s"}};
    const QJsonObject descriptor{
        {"schema", "openswmm.timeseries-import/1"},
        {"datasetId", "7f9b2f2e-0d0a-4d3c-9a77-3d5c1f0b0e01"},
        {"title", "Gauge 00123"},
        {"sources", QJsonArray{QJsonObject{{"path", "samples.csv"}, {"defaults", defaults},
                                           {"columns", QJsonObject{{"time", "time"},
                                                                   {"value", "value"}}}}}}};
    {
        QFile json(root + QStringLiteral("/import.json"));
        if (!json.open(QIODevice::WriteOnly))
            return {};
        json.write(QJsonDocument(descriptor).toJson());
    }
    if (!TimeSeriesStore::importDescriptor(root + "/import.json", root + "/series.gpkg", error))
        return {};
    return TimeSeriesStore::open(root + "/series.gpkg", error);
}

QStringList g_timing;

double engineDecay(SWMMModelLayer *layer, const char *name = "TSS")
{
    double k = -1.0;
    swmm_pollutant_get_kdecay(layer->engine(), swmm_pollutant_index(layer->engine(), name), &k);
    return k;
}

PollutantEditorDialog *openEditor(SWMMModelLayer *layer, QUndoStack *undo, QWidget *host)
{
    ComprehensiveEditorRegistry::instance().openForObject(SWMMModelLayer::DataPollutants, layer,
                                                          undo, QStringLiteral("TSS"), host);
    for (auto *dialog : host->findChildren<PollutantEditorDialog *>())
        if (dialog->isVisible())
            return dialog;
    return nullptr;
}

QDoubleSpinBox *spinFor(QWidget *dialog, const QString &labelText)
{
    for (auto *label : dialog->findChildren<QLabel *>())
        if (label->text().remove(QLatin1Char('&')) == labelText)
            return qobject_cast<QDoubleSpinBox *>(label->buddy());
    return nullptr;
}

const QString kDecayLabel = QStringLiteral("Decay Coefficient (1/days)");

void answerNextPrompt(const QString &buttonText, bool *seen)
{
    QTimer::singleShot(0, [buttonText, seen] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        for (auto *button : box->buttons())
            if (button->text().remove(QLatin1Char('&')) == buttonText) {
                *seen = true;
                button->click();
                return;
            }
        box->reject();
    });
}

/** Like answerNextPrompt, and keeps the question's text in \p text. */
void answerNextPromptReading(const QString &buttonText, QString *text)
{
    QTimer::singleShot(0, [buttonText, text] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        *text = box->text();
        for (auto *button : box->buttons())
            if (button->text().remove(QLatin1Char('&')) == buttonText) {
                button->click();
                return;
            }
        box->reject();
    });
}

/** Section \p header ("[POLLUTANTS]") of the .inp file at \p path, data lines only. */
QStringList inpSection(const QString &path, const QString &header)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {QStringLiteral("<unreadable>")};
    QStringList lines;
    bool in = false;
    for (const QString &raw : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('[')))
            in = line.compare(header, Qt::CaseInsensitive) == 0;
        else if (in && !line.isEmpty() && !line.startsWith(QLatin1Char(';')))
            lines << line.simplified();
    }
    return lines;
}

/** Changes every value field the editor's form shows to another valid value,
 *  the way a user would (spin boxes, combo boxes, check boxes, radio buttons
 *  outside list and table views). @return the number of fields changed. */
int changeEveryField(QWidget *dialog)
{
    int changed = 0;
    for (QWidget *w : dialog->findChildren<QWidget *>()) {
        if (!w->isEnabled() || !w->isVisibleTo(dialog))
            continue;
        bool inView = false;
        for (QWidget *up = w->parentWidget(); up && up != dialog; up = up->parentWidget())
            inView = inView || qobject_cast<QAbstractItemView *>(up);
        if (inView)
            continue;
        if (auto *d = qobject_cast<QDoubleSpinBox *>(w)) {
            const double v = d->value();
            const double step = std::max(d->singleStep(), std::pow(10.0, -d->decimals()));
            d->setValue(v + step <= d->maximum() ? v + step : v - step);
            emit d->editingFinished();
            changed += d->value() != v;
        } else if (auto *i = qobject_cast<QSpinBox *>(w)) {
            const int v = i->value();
            i->setValue(v < i->maximum() ? v + 1 : v - 1);
            emit i->editingFinished();
            changed += i->value() != v;
        } else if (auto *c = qobject_cast<QComboBox *>(w)) {
            if (c->count() > 1 && !c->isEditable()) {
                c->setCurrentIndex((c->currentIndex() + 1) % c->count());
                ++changed;
            }
        } else if (auto *r = qobject_cast<QRadioButton *>(w)) {
            if (!r->isChecked()) {
                r->click();
                ++changed;
            }
        } else if (auto *b = qobject_cast<QCheckBox *>(w)) {
            b->click();
            ++changed;
        }
    }
    return changed;
}

QAbstractButton *buttonWithText(QWidget *root, const QString &text)
{
    for (auto *b : root->findChildren<QAbstractButton *>())
        if (b->isEnabled() && b->text().remove(QLatin1Char('&')) == text)
            return b;
    return nullptr;
}

/** One draft-based family as the structure tests drive it (plans D5 / D8):
 *  where its object lives, how to edit one field in the open editor, how to
 *  delete it there, and how to read it back from the engine. */
struct FamilyProbe {
    QString fixture;
    SWMMModelLayer::DataCategory category = SWMMModelLayer::DataPollutants;
    QString object;
    double target = 0.0;
    std::function<int(SWMM_Engine, const QString &)> index; ///< -1 when absent
    std::function<int(SWMM_Engine)> count;
    std::function<double(SWMM_Engine, const QString &)> value; ///< the probed field
    std::function<DataObjectDraftSession *(QWidget *host, double v)> edit;
    std::function<void(QWidget *host, QString *question)> remove;
    std::function<QDialog *(QWidget *host)> dialog;
};

int seriesCount(SWMM_Engine eng)
{
    int n = 0;
    for (int i = 0; i < swmm_table_count(eng); ++i) {
        int type = -1;
        if (swmm_table_get_type(eng, i, &type) == SWMM_OK && type == 0)
            ++n;
    }
    return n;
}

FamilyProbe familyProbe(const QString &family)
{
    using namespace openswmmvis;
    const QString tableFixture = QStringLiteral("data_object_table_fixture.inp");
    const QString phase6Fixture = QStringLiteral("data_object_phase6_fixture.inp");
    const auto pressDelete = [](QWidget *host, QString *question) {
        answerNextPromptReading(QStringLiteral("Yes"), question);
        if (auto *b = buttonWithText(host, QStringLiteral("Delete")))
            b->click();
    };
    FamilyProbe f;
    if (family == QStringLiteral("pollutant")) {
        f = {tableFixture, SWMMModelLayer::DataPollutants, QStringLiteral("TSS"), 0.25,
             [](SWMM_Engine e, const QString &n) { return swmm_pollutant_index(e, n.toUtf8().constData()); },
             [](SWMM_Engine e) { return swmm_pollutant_count(e); },
             [](SWMM_Engine e, const QString &n) {
                 double k = -1;
                 swmm_pollutant_get_kdecay(e, swmm_pollutant_index(e, n.toUtf8().constData()), &k);
                 return k;
             },
             [](QWidget *host, double v) -> DataObjectDraftSession * {
                 auto *d = host->findChild<PollutantEditorDialog *>();
                 if (!d || !spinFor(d, kDecayLabel))
                     return nullptr;
                 spinFor(d, kDecayLabel)->setValue(v);
                 return d->draftSession();
             },
             pressDelete,
             [](QWidget *host) -> QDialog * { return host->findChild<PollutantEditorDialog *>(); }};
    } else if (family == QStringLiteral("aquifer")) {
        f = {tableFixture, SWMMModelLayer::DataAquifers, QStringLiteral("AQ1"), 0.75,
             [](SWMM_Engine e, const QString &n) { return swmm_aquifer_index(e, n.toUtf8().constData()); },
             [](SWMM_Engine e) { return swmm_aquifer_count(e); },
             [](SWMM_Engine e, const QString &n) {
                 double v = -1;
                 swmm_aquifer_get_param(e, swmm_aquifer_index(e, n.toUtf8().constData()),
                                        aquifer::AquiferProvider::Conductivity, &v);
                 return v;
             },
             [](QWidget *host, double v) -> DataObjectDraftSession * {
                 auto *d = host->findChild<ui::AquiferEditorDialog *>();
                 if (!d)
                     return nullptr;
                 d->spinBox(aquifer::AquiferProvider::Conductivity)->setValue(v);
                 return d->draftSession();
             },
             pressDelete,
             [](QWidget *host) -> QDialog * { return host->findChild<ui::AquiferEditorDialog *>(); }};
    } else if (family == QStringLiteral("street")) {
        f = {tableFixture, SWMMModelLayer::DataStreets, QStringLiteral("ST1"), 0.02,
             [](SWMM_Engine e, const QString &n) { return swmm_street_index(e, n.toUtf8().constData()); },
             [](SWMM_Engine e) { return swmm_street_count(e); },
             [](SWMM_Engine e, const QString &n) {
                 double r = -1;
                 swmm_street_get_params(e, swmm_street_index(e, n.toUtf8().constData()), nullptr,
                                        nullptr, nullptr, &r, nullptr, nullptr, nullptr, nullptr,
                                        nullptr, nullptr);
                 return r;
             },
             [](QWidget *host, double v) -> DataObjectDraftSession * {
                 auto *d = host->findChild<ui::StreetEditorDialog *>();
                 if (!d || !spinFor(d, QStringLiteral("Road Roughness (n)")))
                     return nullptr;
                 spinFor(d, QStringLiteral("Road Roughness (n)"))->setValue(v);
                 return d->draftSession();
             },
             pressDelete,
             [](QWidget *host) -> QDialog * { return host->findChild<ui::StreetEditorDialog *>(); }};
    } else if (family == QStringLiteral("inlet")) {
        f = {tableFixture, SWMMModelLayer::DataInlets, QStringLiteral("GRATE1"), 2.75,
             [](SWMM_Engine e, const QString &n) { return swmm_inlet_index(e, n.toUtf8().constData()); },
             [](SWMM_Engine e) { return swmm_inlet_count(e); },
             [](SWMM_Engine e, const QString &n) {
                 SWMM_InletDesign design{};
                 swmm_inlet_get_design(e, swmm_inlet_index(e, n.toUtf8().constData()), &design);
                 return design.grate_length;
             },
             [](QWidget *host, double v) -> DataObjectDraftSession * {
                 auto *d = host->findChild<ui::InletEditorDialog *>();
                 if (!d || !d->editedInlet())
                     return nullptr;
                 d->editedInlet()->setGrateLength(v); // what the property tree writes
                 return d->draftSession();
             },
             pressDelete,
             [](QWidget *host) -> QDialog * { return host->findChild<ui::InletEditorDialog *>(); }};
    } else if (family == QStringLiteral("transect")) {
        f = {QStringLiteral("data_editor_routing.inp"), SWMMModelLayer::DataTransects,
             QStringLiteral("Profile"), 3.25,
             [](SWMM_Engine e, const QString &n) { return swmm_transect_index(e, n.toUtf8().constData()); },
             [](SWMM_Engine e) { return swmm_transect_count(e); },
             [](SWMM_Engine e, const QString &n) {
                 double station = -1, elevation = -1;
                 swmm_transect_get_station(e, swmm_transect_index(e, n.toUtf8().constData()), 1,
                                           &station, &elevation);
                 return elevation;
             },
             [](QWidget *host, double v) -> DataObjectDraftSession * {
                 auto *d = host->findChild<ui::TransectEditorDialog *>();
                 if (!d || !d->editedTransect() || !d->editedTransect()->setElevationAt(1, v))
                     return nullptr;
                 return d->draftSession();
             },
             pressDelete,
             [](QWidget *host) -> QDialog * { return host->findChild<ui::TransectEditorDialog *>(); }};
    } else if (family == QStringLiteral("landuse")) {
        f = {phase6Fixture, SWMMModelLayer::DataLandUses, QStringLiteral("Commercial"), 3.5,
             [](SWMM_Engine e, const QString &n) { return swmm_landuse_index(e, n.toUtf8().constData()); },
             [](SWMM_Engine e) { return swmm_landuse_count(e); },
             [](SWMM_Engine e, const QString &n) {
                 double v = -1;
                 swmm_landuse_get_sweep_interval(e, swmm_landuse_index(e, n.toUtf8().constData()), &v);
                 return v;
             },
             [](QWidget *host, double v) -> DataObjectDraftSession * {
                 auto *d = host->findChild<ui::LandUseEditorDialog *>();
                 if (!d || !spinFor(d, QStringLiteral("Sweep Interval (days)")))
                     return nullptr;
                 spinFor(d, QStringLiteral("Sweep Interval (days)"))->setValue(v);
                 return d->draftSession();
             },
             pressDelete,
             [](QWidget *host) -> QDialog * { return host->findChild<ui::LandUseEditorDialog *>(); }};
    } else if (family == QStringLiteral("snowpack")) {
        f = {phase6Fixture, SWMMModelLayer::DataSnowpacks, QStringLiteral("Snow"), 2.5,
             [](SWMM_Engine e, const QString &n) { return swmm_snowpack_index(e, n.toUtf8().constData()); },
             [](SWMM_Engine e) { return swmm_snowpack_count(e); },
             [](SWMM_Engine e, const QString &n) { // removal threshold depth
                 double r[6] = {-1, 0, 0, 0, 0, 0};
                 swmm_snowpack_get_removal(e, swmm_snowpack_index(e, n.toUtf8().constData()), &r[0],
                                           &r[1], &r[2], &r[3], &r[4], &r[5]);
                 return r[0];
             },
             [](QWidget *host, double v) -> DataObjectDraftSession * {
                 auto *d = host->findChild<ui::SnowpackEditorDialog *>();
                 auto *spin = d ? d->findChild<QDoubleSpinBox *>(QStringLiteral("snowParameter21")) : nullptr;
                 if (!spin)
                     return nullptr;
                 spin->setValue(v);
                 return d->draftSession();
             },
             pressDelete,
             [](QWidget *host) -> QDialog * { return host->findChild<ui::SnowpackEditorDialog *>(); }};
    } else if (family == QStringLiteral("lid")) {
        f = {QStringLiteral("data_object_phase7_fixture.inp"), SWMMModelLayer::DataLIDControls,
             QStringLiteral("BioCell"), 7.5,
             [](SWMM_Engine e, const QString &n) { return swmm_lid_index(e, n.toUtf8().constData()); },
             [](SWMM_Engine e) { return swmm_lid_count(e); },
             [](SWMM_Engine e, const QString &n) { // surface storage depth
                 double storage = -1, roughness = -1, slope = -1;
                 swmm_lid_get_surface(e, swmm_lid_index(e, n.toUtf8().constData()), &storage,
                                      &roughness, &slope);
                 return storage;
             },
             [](QWidget *host, double v) -> DataObjectDraftSession * {
                 auto *d = host->findChild<ui::LidControlEditorDialog *>();
                 auto *spin = d ? d->findChild<QDoubleSpinBox *>(QStringLiteral("lidField:surfStorage")) : nullptr;
                 if (!spin)
                     return nullptr;
                 spin->setValue(v);
                 return d->draftSession();
             },
             pressDelete,
             [](QWidget *host) -> QDialog * { return host->findChild<ui::LidControlEditorDialog *>(); }};
    } else {
        f = {QStringLiteral("single_rain_series.inp"), SWMMModelLayer::DataTimeSeries, kSeries, 5.5,
             [](SWMM_Engine e, const QString &n) {
                 return TimeseriesRegistry::engineTableIndex(e, n);
             },
             seriesCount,
             [](SWMM_Engine e, const QString &n) {
                 double x = 0, y = std::numeric_limits<double>::quiet_NaN();
                 swmm_table_get_point(e, TimeseriesRegistry::engineTableIndex(e, n), 0, &x, &y);
                 return y;
             },
             [](QWidget *host, double v) -> DataObjectDraftSession * {
                 auto *d = host->findChild<TimeseriesEditorDialog *>();
                 if (!d || !d->tableModel())
                     return nullptr;
                 d->tableModel()->setData(d->tableModel()->index(0, 1), v);
                 return d->draftSession();
             },
             pressDelete,
             [](QWidget *host) -> QDialog * { return host->findChild<TimeseriesEditorDialog *>(); }};
    }
    return f;
}

} // namespace

class TestDataObjectDraftSession : public QObject
{
    Q_OBJECT
private slots:

    void draftReachesTheModelOnlyAtApplyAndUndoesAfterClose()
    {
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("apply_undo"), &why);
        QVERIFY2(w, qPrintable(why));
        SWMMModelLayer *layer = w->modelLayer();
        QWidget host;
        QUndoStack undo;
        auto *dialog = openEditor(layer, &undo, &host);
        QVERIFY(dialog);
        DataObjectAttributeTableModel table;
        table.setSource(layer, SWMMModelLayer::DataPollutants);
        int decayColumn = -1;
        for (int i = 0; i < table.columnSpecs().size(); ++i)
            if (table.columnSpecs()[i].key == QStringLiteral("kDecay"))
                decayColumn = i;
        QVERIFY(decayColumn >= 0);
        const double before = engineDecay(layer);
        QSignalSpy edited(layer, &SWMMModelLayer::modelEdited);

        // 0.3 per day is not exactly representable once stored per second.
        spinFor(dialog, kDecayLabel)->setValue(0.3);
        QCOMPARE(dialog->draftSession()->state(), State::Unapplied);
        QCOMPARE(engineDecay(layer), before);
        QCOMPARE(table.index(0, decayColumn).data(Qt::EditRole).toDouble(), before);
        QCOMPARE(edited.count(), 0);
        QCOMPARE(undo.count(), 0);

        dialog->findChild<QPushButton *>(QStringLiteral("draftApply"))->click();
        QCOMPARE(dialog->draftSession()->state(), State::Applied);
        QVERIFY(qAbs(engineDecay(layer) - 0.3) < 1e-9);
        QVERIFY(qAbs(table.index(0, decayColumn).data(Qt::EditRole).toDouble() - 0.3) < 1e-9);
        QVERIFY(edited.count() >= 1);
        QCOMPARE(undo.count(), 1);

        dialog->close();
        QVERIFY(!dialog->isVisible());
        undo.undo(); // the editor is gone; the step must still reach the engine
        QVERIFY(qAbs(engineDecay(layer) - before) < 1e-9);
        undo.redo();
        QVERIFY(qAbs(engineDecay(layer) - 0.3) < 1e-9);
        delete w;
    }

    void editElsewhereKeepsTheDraftAndAsksAtApply()
    {
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("conflict"), &why);
        QVERIFY2(w, qPrintable(why));
        SWMMModelLayer *layer = w->modelLayer();
        QWidget host;
        QUndoStack undo;
        auto *dialog = openEditor(layer, &undo, &host);
        QVERIFY(dialog);

        spinFor(dialog, kDecayLabel)->setValue(0.25);
        // The Attribute Table / Properties write path, on the same pollutant.
        QVERIFY(DataObjectAttributeTableModel::applyValue(layer, SWMMModelLayer::DataPollutants,
                                                          QStringLiteral("TSS"),
                                                          QStringLiteral("mwt"), 12.5));
        QCOMPARE(dialog->draftSession()->state(), State::Conflict);
        QCOMPARE(spinFor(dialog, kDecayLabel)->value(), 0.25); // draft not wiped
        auto *provider =
            qobject_cast<PollutantRegistry *>(layer->ensurePollutantRegistry())->findByName("TSS");
        QCOMPARE(provider->mwt(), 12.5); // the other view's edit not wiped either

        bool seen = false;
        answerNextPrompt(QStringLiteral("Cancel"), &seen);
        QVERIFY(!dialog->draftSession()->apply());
        QVERIFY(seen);
        QVERIFY(dialog->draftSession()->hasDraft());

        seen = false;
        answerNextPrompt(QStringLiteral("Apply My Changes"), &seen);
        QVERIFY(dialog->draftSession()->apply());
        QVERIFY(seen);
        QVERIFY(qAbs(engineDecay(layer) - 0.25) < 1e-9);
        dialog->close();
        delete w;
    }

    void refusedApplyChangesNothingAndKeepsTheDraft()
    {
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("rejected"), &why);
        QVERIFY2(w, qPrintable(why));
        SWMMModelLayer *layer = w->modelLayer();
        QWidget host;
        QUndoStack undo;
        auto *dialog = openEditor(layer, &undo, &host);
        QVERIFY(dialog);
        const double before = engineDecay(layer);

        // The engine refuses a molecular weight of 0, so this Apply is refused
        // as a whole — the decay edit with it. The form stops at a positive
        // weight; its minimum is lowered here to reach the engine's refusal.
        spinFor(dialog, kDecayLabel)->setValue(0.4);
        QDoubleSpinBox *mwt = spinFor(dialog, QStringLiteral("Molecular Weight"));
        QVERIFY(mwt && mwt->value() > 0.0 && mwt->minimum() > 0.0);
        mwt->setMinimum(0.0);
        mwt->setValue(0.0);

        QVERIFY(!dialog->draftSession()->apply());
        QCOMPARE(dialog->draftSession()->state(), State::Rejected);
        QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("draftStatus"))->text().contains("Rejected"));
        QVERIFY(qAbs(engineDecay(layer) - before) < 1e-9);
        QCOMPARE(undo.count(), 0);
        QVERIFY(dialog->draftSession()->hasDraft());
        QCOMPARE(spinFor(dialog, kDecayLabel)->value(), 0.4);

        dialog->draftSession()->revert();
        dialog->close();
        delete w;
    }

    /** A pollutant made with New starts with the model's molecular weight
     *  (1.0), so an edit to it applies (it started at 0, which the model
     *  refuses, and every Apply of a new pollutant was rejected). */
    void newPollutantTakesAnEditAtApply()
    {
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("new-pollutant"), &why);
        QVERIFY2(w, qPrintable(why));
        SWMMModelLayer *layer = w->modelLayer();
        QWidget host;
        QUndoStack undo;
        auto *dialog = openEditor(layer, &undo, &host);
        QVERIFY(dialog);
        QPushButton *add = nullptr;
        for (auto *b : dialog->findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("New"))
                add = b;
        QVERIFY(add);
        add->click();
        QVERIFY(dialog->currentProvider());
        const QByteArray name = dialog->currentProvider()->name().toUtf8();
        QVERIFY(name != "TSS");

        spinFor(dialog, kDecayLabel)->setValue(0.25);
        QVERIFY2(dialog->draftSession()->apply(), qPrintable(dialog->draftSession()->message()));
        QVERIFY(qAbs(engineDecay(layer, name.constData()) - 0.25) < 1e-9);
        QCOMPARE(spinFor(dialog, QStringLiteral("Molecular Weight"))->value(), 1.0);
        double mwt = 0.0;
        swmm_pollutant_get_mwt(layer->engine(),
                               swmm_pollutant_index(layer->engine(), name.constData()), &mwt);
        QCOMPARE(mwt, 1.0);

        dialog->close();
        delete w;
    }

    void saveWritesTheAppliedState_data()
    {
        QTest::addColumn<QString>("ext");
        QTest::newRow("inp") << QStringLiteral("inp");
        QTest::newRow("gpkg") << QStringLiteral("gpkg");
    }

    void saveWritesTheAppliedState()
    {
        QFETCH(QString, ext);
        const QString saved = outDir() + QStringLiteral("/applied_then_draft_saved.") + ext;
        for (const QString &f : {saved, saved + "-wal", saved + "-shm"})
            QFile::remove(f);
        QString why, err;
        {
            SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("save_") + ext, &why);
            QVERIFY2(w, qPrintable(why));
            QWidget host;
            QUndoStack undo;
            auto *dialog = openEditor(w->modelLayer(), &undo, &host);
            QVERIFY(dialog);
            spinFor(dialog, kDecayLabel)->setValue(0.3);
            QVERIFY(dialog->draftSession()->apply());
            spinFor(dialog, kDecayLabel)->setValue(0.9); // left unapplied
            QVERIFY2(w->saveAs(saved, &err), qPrintable(err));
            dialog->draftSession()->revert();
            dialog->close();
            delete w;
        }
        SWMMVisProjectWindow *r = openWindow(saved, &why);
        QVERIFY2(r, qPrintable(why));
        const double got = engineDecay(r->modelLayer());
        delete r;
        QVERIFY2(qAbs(got - 0.3) < 1e-9, qPrintable(QString::number(got)));
    }

    // ── Time series (Phase 3) ────────────────────────────────────────────────

    void seriesDraftReachesTheEngineOnlyAtApplyAndUndoesAfterClose()
    {
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("ts_apply_undo"), &why,
                                                  QStringLiteral("single_rain_series.inp"),
                                                  QStringLiteral("phase3"));
        QVERIFY2(w, qPrintable(why));
        SWMMModelLayer *layer = w->modelLayer();
        QWidget host;
        QUndoStack undo;
        QPointer<TimeseriesEditorDialog> dialog = openSeriesEditor(layer, &undo, &host, kSeries);
        QVERIFY(dialog);
        const double before = engineSeriesValue(layer, kSeries, 0);
        QVERIFY(!std::isnan(before));
        QSignalSpy edited(layer, &SWMMModelLayer::modelEdited);

        QVERIFY(dialog->tableModel()->setData(dialog->tableModel()->index(0, 1), 5.5));
        QTRY_COMPARE(dialog->draftSession()->state(), State::Unapplied);
        QCOMPARE(engineSeriesValue(layer, kSeries, 0), before);
        QCOMPARE(edited.count(), 0);
        QCOMPARE(undo.count(), 0);

        dialog->findChild<QPushButton *>(QStringLiteral("draftApply"))->click();
        QCOMPARE(dialog->draftSession()->state(), State::Applied);
        QCOMPARE(engineSeriesValue(layer, kSeries, 0), 5.5);
        QVERIFY(edited.count() >= 1);
        QCOMPARE(undo.count(), 1);

        dialog->close();
        QTRY_VERIFY(!dialog); // WA_DeleteOnClose
        undo.undo(); // the editor is gone; the step must still reach the engine
        QCOMPARE(engineSeriesValue(layer, kSeries, 0), before);
        undo.redo();
        QCOMPARE(engineSeriesValue(layer, kSeries, 0), 5.5);
        delete w;
    }

    void seriesSaveWritesTheAppliedState_data()
    {
        QTest::addColumn<QString>("ext");
        QTest::newRow("inp") << QStringLiteral("inp");
        QTest::newRow("gpkg") << QStringLiteral("gpkg");
    }

    void seriesSaveWritesTheAppliedState()
    {
        QFETCH(QString, ext);
        const QString saved = outDir(QStringLiteral("phase3")) +
                              QStringLiteral("/series_applied_then_draft_saved.") + ext;
        for (const QString &f : {saved, saved + "-wal", saved + "-shm"})
            QFile::remove(f);
        QString why, err;
        {
            SWMMVisProjectWindow *w =
                openFixtureCopy(QStringLiteral("ts_save_") + ext, &why,
                                QStringLiteral("single_rain_series.inp"), QStringLiteral("phase3"));
            QVERIFY2(w, qPrintable(why));
            QWidget host;
            QUndoStack undo;
            auto *dialog = openSeriesEditor(w->modelLayer(), &undo, &host, kSeries);
            QVERIFY(dialog);
            QVERIFY(dialog->tableModel()->setData(dialog->tableModel()->index(0, 1), 5.5));
            QVERIFY(dialog->draftSession()->apply());
            QVERIFY(dialog->tableModel()->setData(dialog->tableModel()->index(0, 1), 9.5)); // unapplied
            QVERIFY2(w->saveAs(saved, &err), qPrintable(err));
            dialog->draftSession()->revert();
            dialog->close();
            delete w;
        }
        SWMMVisProjectWindow *r = openWindow(saved, &why);
        QVERIFY2(r, qPrintable(why));
        const double got = engineSeriesValue(r->modelLayer(), kSeries, 0);
        delete r;
        QCOMPARE(got, 5.5);
    }

    /*! Save / Run / Close ask when editors hold unapplied changes (D6); the
     *  prompt covers every editor of the model at once. */
    void saveRunCloseAskAboutUnappliedChanges()
    {
        using openswmmvis::ui::resolveUnappliedChanges;
        using openswmmvis::ui::UnappliedAction;
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("ts_prompt"), &why,
                                                  QStringLiteral("data_object_table_fixture.inp"),
                                                  QStringLiteral("phase3"));
        QVERIFY2(w, qPrintable(why));
        SWMMModelLayer *layer = w->modelLayer();
        // The fixture has a pollutant (TSS) but no series: add one.
        auto *reg = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
        QVERIFY(reg);
        auto *made = reg->create(kSeries);
        QVERIFY(made);
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        QVERIFY(made->setAllPoints({{t0, 0.0}, {t0.addSecs(900), 0.1}}));
        QVERIFY(reg->flushPendingEdits());
        QWidget host;
        QUndoStack undo;
        auto *series = openSeriesEditor(layer, &undo, &host, kSeries);
        QVERIFY(series);
        const double before = engineSeriesValue(layer, kSeries, 0);

        bool seen = false;
        QVERIFY(resolveUnappliedChanges(layer, UnappliedAction::Save, nullptr)); // nothing to ask
        QVERIFY(!seen);

        QVERIFY(series->tableModel()->setData(series->tableModel()->index(0, 1), 5.5));
        auto *pollutants = openEditor(layer, &undo, &host); // TSS
        QVERIFY(pollutants);
        spinFor(pollutants, kDecayLabel)->setValue(0.3);
        QCOMPARE(openswmmvis::ui::DataObjectDraftSession::withUnappliedChanges(layer).size(), 2);

        answerNextPrompt(QStringLiteral("Cancel"), &seen);
        QVERIFY(!resolveUnappliedChanges(layer, UnappliedAction::Save, nullptr));
        QVERIFY(seen);
        QVERIFY(series->draftSession()->hasDraft());
        QCOMPARE(engineSeriesValue(layer, kSeries, 0), before);

        seen = false;
        answerNextPrompt(QStringLiteral("Run Without Them"), &seen);
        QVERIFY(resolveUnappliedChanges(layer, UnappliedAction::Run, nullptr));
        QVERIFY(seen);
        QVERIFY(series->draftSession()->hasDraft()); // kept for later

        seen = false;
        answerNextPrompt(QStringLiteral("Apply and Save"), &seen);
        QVERIFY(resolveUnappliedChanges(layer, UnappliedAction::Save, nullptr));
        QVERIFY(seen);
        QVERIFY(!series->draftSession()->hasDraft());
        QCOMPARE(engineSeriesValue(layer, kSeries, 0), 5.5);
        QVERIFY(!pollutants->draftSession()->hasDraft());
        QVERIFY(qAbs(engineDecay(layer) - 0.3) < 1e-9);

        QVERIFY(series->tableModel()->setData(series->tableModel()->index(0, 1), 9.5));
        seen = false;
        answerNextPrompt(QStringLiteral("Discard"), &seen);
        QVERIFY(resolveUnappliedChanges(layer, UnappliedAction::Close, nullptr));
        QVERIFY(seen);
        QVERIFY(!series->draftSession()->hasDraft()); // Close discards
        QCOMPARE(engineSeriesValue(layer, kSeries, 0), 5.5);
        delete w;
    }

    /*! Geopackage source (D3): an observation copy is written as ordinary rows,
     *  its binding goes to the .oswp, comes back on reopen, and Refresh works
     *  from the saved binding. */
    void observationCopySavesRowsAndItsBinding()
    {
        using namespace OpenSWMM::TimeSeries;
        QString error;
        const QString root = outDir(QStringLiteral("phase3")) + QStringLiteral("/observation_dataset");
        const auto dataset = makeObservationDataset(root, &error);
        QVERIFY2(dataset && dataset->series.size() == 1, qPrintable(error));

        const QString saved = outDir(QStringLiteral("phase3")) + QStringLiteral("/observation_copy_saved.inp");
        const QString sidecar = ProjectSerializer::sidecarPathFor(saved);
        for (const QString &f : {saved, sidecar})
            QFile::remove(f);
        QString why;
        {
            SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("ts_observations"), &why,
                                                      QStringLiteral("single_rain_series.inp"),
                                                      QStringLiteral("phase3"));
            QVERIFY2(w, qPrintable(why));
            QWidget host;
            QUndoStack undo;
            auto *dialog = openSeriesEditor(w->modelLayer(), &undo, &host, kSeries);
            QVERIFY(dialog);

            // What the reviewed preparation dialog produces for "Use in Editor".
            AssignmentOptions o;
            o.seriesId = dataset->series.first().id;
            o.modelSeriesName = kSeries;
            o.targetUnit = dataset->series.first().unit;
            o.clockConfirmed = true;
            o.unitInterpretationConfirmed = true;
            const PreparedAssignment prepared = prepareAssignment(*dataset, o);
            QVERIFY2(prepared.ok(), qPrintable(prepared.error));
            openswmmvis::ui::ObservationCopy copy;
            openswmmvis::ui::observationCopyFrom(prepared, *dataset, &copy);
            QVERIFY(dialog->useObservationCopy(copy));
            QVERIFY(dialog->draftSession()->apply());
            QCOMPARE(engineSeriesValue(w->modelLayer(), kSeries, 0), 1.5);
            QCOMPARE(engineSeriesValue(w->modelLayer(), kSeries, 3), 4.5);

            QString err;
            QVERIFY2(w->saveAs(saved, &err), qPrintable(err));
            dialog->close();
            delete w;
        }
        // The .oswp holds the binding, keyed by the series name.
        {
            QFile f(sidecar);
            QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(sidecar));
            QVERIFY(f.readAll().contains("\"modelSeries\": \"TSERIES1\""));
        }
        SWMMVisProjectWindow *r = openWindow(saved, &why);
        QVERIFY2(r, qPrintable(why));
        QCOMPARE(engineSeriesValue(r->modelLayer(), kSeries, 0), 1.5); // ordinary rows in the .inp
        QString err;
        QStringList warnings;
        QVERIFY2(ProjectSerializer::applyFromFile(sidecar, r, &err, &warnings), qPrintable(err));
        // Saving again without touching any series keeps the binding, whether it is
        // already on its series or still waiting for the registry to be created.
        const bool registryExisted = r->modelLayer()->findChild<TimeseriesRegistry *>() != nullptr;
        QCOMPARE(r->modelLayer()->timeseriesObservationBindings().size(), 1);
        const QString resaved = outDir(QStringLiteral("phase3")) + QStringLiteral("/observation_copy_resaved.inp");
        QFile::remove(resaved);
        QFile::remove(ProjectSerializer::sidecarPathFor(resaved));
        QVERIFY2(r->saveAs(resaved, &err), qPrintable(err));
        {
            QFile f(ProjectSerializer::sidecarPathFor(resaved));
            QVERIFY(f.open(QIODevice::ReadOnly));
            QVERIFY2(f.readAll().contains("\"modelSeries\": \"TSERIES1\""),
                     registryExisted ? "registry existed" : "binding was pending");
        }
        qInfo() << "binding restore path:" << (registryExisted ? "registry existed" : "pending until created");
        auto *reg = qobject_cast<TimeseriesRegistry *>(r->modelLayer()->ensureTimeseriesRegistry());
        auto *series = reg->findByName(kSeries);
        QVERIFY(series);
        QCOMPARE(series->sourceMode(),
                 openswmmvis::timeseries::TimeseriesProvider::SourceMode::GeopackageObserved);
        QCOMPARE(series->observationBinding().value("seriesId").toString(), dataset->series.first().id);
        QCOMPARE(QFileInfo(series->observationBinding().value("sourcePath").toString()).canonicalFilePath(),
                 QFileInfo(root + "/series.gpkg").canonicalFilePath());
        QVERIFY2(!r->hasChanges(), "restoring a binding is not an edit");

        openswmmvis::ui::ObservationCopy again;
        QVERIFY2(openswmmvis::ui::refreshObservationCopy(series->observationBinding(), kSeries, &again, &err),
                 qPrintable(err));
        QCOMPARE(again.points.size(), 4);
        QCOMPARE(again.points.last().value, 4.5);
        delete r;
    }

    /*! Choose Series… lists the series of the observation datasets loaded on
     *  the project's map (Browse… adds one from a file). */
    void chooseSeriesListsTheLoadedObservationDatasets()
    {
        QString error;
        const QString root = outDir(QStringLiteral("phase3")) + QStringLiteral("/observation_dataset_loaded");
        const auto dataset = makeObservationDataset(root, &error);
        QVERIFY2(dataset, qPrintable(error));
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("ts_choose"), &why,
                                                  QStringLiteral("single_rain_series.inp"),
                                                  QStringLiteral("phase3"));
        QVERIFY2(w, qPrintable(why));
        QCOMPARE(openswmmvis::ui::loadedObservationSeries(w->modelLayer()).size(), 0);
        auto *observations = new TimeSeriesLayer;
        observations->setParent(w);
        QVERIFY2(observations->openTimeSeries(root + "/series.gpkg", {}, &error), qPrintable(error));
        w->canvas()->addLayer(observations, false);
        const auto choices = openswmmvis::ui::loadedObservationSeries(w->modelLayer());
        QCOMPARE(choices.size(), 1);
        QVERIFY(choices.first().label.contains(QStringLiteral("Gauge 00123")));
        QCOMPARE(choices.first().seriesId, dataset->series.first().id);

        // The installed Choose Series… offers them.
        openswmmvis::ui::installTimeseriesObservationSource();
        const auto reset = qScopeGuard([] { TimeseriesEditorDialog::setObservationSource({}, {}); });
        QWidget host;
        QUndoStack undo;
        auto *dialog = openSeriesEditor(w->modelLayer(), &undo, &host, kSeries);
        QVERIFY(dialog);
        for (auto *radio : dialog->findChildren<QRadioButton *>())
            if (radio->text() == QStringLiteral("Geopackage"))
                radio->setChecked(true);
        int listed = -1;
        QString first;
        QTimer::singleShot(0, [&] {
            auto *chooser = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (chooser && chooser->objectName() == QStringLiteral("observationSeriesChooser")) {
                auto *list = chooser->findChild<QListWidget *>(QStringLiteral("observationSeriesList"));
                listed = list->count();
                first = list->count() ? list->item(0)->text() : QString();
            }
            if (chooser)
                chooser->reject();
        });
        dialog->findChild<QPushButton *>(QStringLiteral("gpkgChooseBtn"))->click();
        QCOMPARE(listed, 1);
        QVERIFY(first.contains(QStringLiteral("Gauge 00123")));
        QVERIFY(dialog->editedSeries()->observationBinding().isEmpty()); // canceled: nothing chosen
        dialog->draftSession()->revert();
        dialog->close();
        delete w;
    }

    // ── Phase 4: aquifer, street, inlet ──────────────────────────────────────

    void phase4FamiliesApplyAndUndoAfterClose_data()
    {
        QTest::addColumn<QString>("family");
        QTest::newRow("aquifer") << QStringLiteral("aquifer");
        QTest::newRow("street") << QStringLiteral("street");
        QTest::newRow("inlet") << QStringLiteral("inlet");
        QTest::newRow("transect") << QStringLiteral("transect");
    }

    /*! Edits wait in the editor's draft; Apply writes the engine and marks the
     *  project edited as one step that still undoes after the editor closed. */
    void phase4FamiliesApplyAndUndoAfterClose()
    {
        QFETCH(QString, family);
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(
            QStringLiteral("p4_") + family, &why,
            family == QStringLiteral("transect") ? QStringLiteral("data_editor_routing.inp")
                                                 : QStringLiteral("data_object_table_fixture.inp"),
            QStringLiteral("phase4"));
        QVERIFY2(w, qPrintable(why));
        SWMMModelLayer *layer = w->modelLayer();
        SWMM_Engine eng = layer->engine();
        QWidget host;
        QUndoStack undo;
        QSignalSpy edited(layer, &SWMMModelLayer::modelEdited);
        QPointer<QDialog> dialog;
        DataObjectDraftSession *session = nullptr;
        std::function<double()> engineValue;
        double target = 0.0;

        auto &editors = ComprehensiveEditorRegistry::instance();
        if (family == QStringLiteral("aquifer")) {
            editors.openForObject(SWMMModelLayer::DataAquifers, layer, &undo, "AQ1", &host);
            auto *d = host.findChild<openswmmvis::ui::AquiferEditorDialog *>();
            QVERIFY(d);
            dialog = d;
            session = d->draftSession();
            engineValue = [eng] {
                double v = -1;
                swmm_aquifer_get_param(eng, swmm_aquifer_index(eng, "AQ1"),
                                       openswmmvis::aquifer::AquiferProvider::Conductivity, &v);
                return v;
            };
            target = 0.75;
            d->spinBox(openswmmvis::aquifer::AquiferProvider::Conductivity)->setValue(target);
        } else if (family == QStringLiteral("street")) {
            editors.openForObject(SWMMModelLayer::DataStreets, layer, &undo, "ST1", &host);
            auto *d = host.findChild<openswmmvis::ui::StreetEditorDialog *>();
            QVERIFY(d);
            dialog = d;
            session = d->draftSession();
            engineValue = [eng] {
                double n = -1;
                swmm_street_get_params(eng, swmm_street_index(eng, "ST1"), nullptr, nullptr,
                                       nullptr, &n, nullptr, nullptr, nullptr, nullptr, nullptr,
                                       nullptr);
                return n;
            };
            target = 0.02;
            spinFor(d, QStringLiteral("Road Roughness (n)"))->setValue(target);
        } else if (family == QStringLiteral("transect")) {
            editors.openForObject(SWMMModelLayer::DataTransects, layer, &undo, "Profile", &host);
            auto *d = host.findChild<openswmmvis::ui::TransectEditorDialog *>();
            QVERIFY(d && d->editedTransect());
            dialog = d;
            session = d->draftSession();
            engineValue = [eng] {
                double station = -1, elevation = -1;
                swmm_transect_get_station(eng, swmm_transect_index(eng, "Profile"), 1, &station,
                                          &elevation);
                return elevation;
            };
            target = 3.25;
            QVERIFY(d->editedTransect()->setElevationAt(1, target)); // what the table writes
        } else {
            editors.openForObject(SWMMModelLayer::DataInlets, layer, &undo, "GRATE1", &host);
            auto *d = host.findChild<openswmmvis::ui::InletEditorDialog *>();
            QVERIFY(d && d->editedInlet());
            dialog = d;
            session = d->draftSession();
            engineValue = [eng] {
                SWMM_InletDesign design{};
                swmm_inlet_get_design(eng, swmm_inlet_index(eng, "GRATE1"), &design);
                return design.grate_length;
            };
            target = 2.75;
            d->editedInlet()->setGrateLength(target); // what the property tree writes
            d->findChild<QTextEdit *>()->setPlainText(QStringLiteral("draft note"));
        }
        QVERIFY(session);
        const double before = engineValue();
        QVERIFY(before != target);
        QTRY_VERIFY(session->hasDraft());
        QCOMPARE(engineValue(), before);
        QCOMPARE(edited.count(), 0);
        QCOMPARE(undo.count(), 0);

        QVERIFY2(session->apply(), qPrintable(session->message()));
        QCOMPARE(engineValue(), target);
        QVERIFY(edited.count() >= 1);
        QCOMPARE(undo.count(), 1);
        if (family == QStringLiteral("inlet")) {
            char note[256] = {};
            swmm_inlet_get_comment(eng, swmm_inlet_index(eng, "GRATE1"), note, sizeof note);
            QCOMPARE(QString::fromUtf8(note), QStringLiteral("draft note"));
        }

        dialog->close();
        QTRY_VERIFY(!dialog || !dialog->isVisible());
        undo.undo(); // the editor is gone; the step must still reach the engine
        QCOMPARE(engineValue(), before);
        undo.redo();
        QCOMPARE(engineValue(), target);
        delete w;
    }

    /*! Inlet field edits undo step by step in the editor (they used to undo on
     *  the project stack; Apply is now the project step), and undoing back to
     *  the model's values leaves nothing to apply. */
    void inletDraftEditsUndoInTheEditor()
    {
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("p4_inlet_undo"), &why,
                                                  QStringLiteral("data_object_table_fixture.inp"),
                                                  QStringLiteral("phase4"));
        QVERIFY2(w, qPrintable(why));
        QWidget host;
        QUndoStack project;
        ComprehensiveEditorRegistry::instance().openForObject(SWMMModelLayer::DataInlets,
                                                              w->modelLayer(), &project, "GRATE1",
                                                              &host);
        auto *d = host.findChild<openswmmvis::ui::InletEditorDialog *>();
        QVERIFY(d && d->editedInlet());
        QComboBox *type = nullptr;
        for (auto *combo : d->findChildren<QComboBox *>())
            if (combo->findText(QStringLiteral("CURB OPENING")) >= 0)
                type = combo;
        QVERIFY(type);
        QAction *undoAction = nullptr;
        for (auto *a : d->findChildren<QAction *>())
            if (a->text().startsWith(QStringLiteral("Undo")))
                undoAction = a;
        QVERIFY(undoAction);

        type->setCurrentIndex(type->findText(QStringLiteral("CURB OPENING")));
        QCOMPARE(d->editedInlet()->type(), openswmmvis::inlet::InletType::Curb);
        QVERIFY(d->draftSession()->hasDraft());
        QVERIFY(undoAction->isEnabled());
        undoAction->trigger();
        QCOMPARE(d->editedInlet()->type(), openswmmvis::inlet::InletType::Grate);
        QVERIFY(!d->draftSession()->hasDraft());
        QCOMPARE(project.count(), 0); // nothing reached the project stack
        d->close();
        delete w;
    }

    void deleteUndoBringsBackTheObjectAndItsSettings_data()
    {
        QTest::addColumn<QString>("family");
        for (const char *family :
             {"pollutant", "aquifer", "street", "inlet", "transect", "timeseries", "snowpack",
              "landuse", "lid"})
            QTest::newRow(family) << QString::fromLatin1(family);
    }

    /*! Plans D5 / D8: Apply an edit, delete the object in its editor and close
     *  the editor. Undo brings the object back with the applied settings, in
     *  the engine; the next Undo takes the Apply back on the object Undo
     *  brought back; Redo replays both. Undo runs before the deleted object's
     *  deferred deletion, so a step must not write to it. */
    void deleteUndoBringsBackTheObjectAndItsSettings()
    {
        QFETCH(QString, family);
        const FamilyProbe probe = familyProbe(family);
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("delete_undo_") + family, &why,
                                                  probe.fixture, QStringLiteral("fixes"));
        QVERIFY2(w, qPrintable(why));
        SWMM_Engine eng = w->modelLayer()->engine();
        QWidget host;
        QUndoStack undo;
        ComprehensiveEditorRegistry::instance().openForObject(probe.category, w->modelLayer(), &undo,
                                                              probe.object, &host);
        const QString obj = probe.object;
        const double before = probe.value(eng, obj);
        QVERIFY(before != probe.target);
        DataObjectDraftSession *session = probe.edit(&host, probe.target);
        QVERIFY(session);
        QTRY_VERIFY(session->hasDraft());
        QVERIFY2(session->apply(), qPrintable(session->message()));
        QCOMPARE(probe.value(eng, obj), probe.target);
        QCOMPARE(undo.count(), 1);

        QString question;
        probe.remove(&host, &question);
        QTRY_COMPARE(probe.index(eng, obj), -1);
        QCOMPARE(undo.count(), 2);
        if (family == QStringLiteral("pollutant")) // Lead names TSS as its co-pollutant
            QVERIFY2(question.contains(QStringLiteral("1 pollutant(s)")) &&
                         question.contains(QStringLiteral("Undo brings back")),
                     qPrintable(question));
        if (QDialog *d = probe.dialog(&host))
            d->close();

        undo.undo(); // the delete
        QVERIFY(probe.index(eng, obj) >= 0);
        QCOMPARE(probe.value(eng, obj), probe.target);
        undo.undo(); // the Apply, on the object Undo brought back
        QCOMPARE(probe.value(eng, obj), before);
        undo.redo();
        QCOMPARE(probe.value(eng, obj), probe.target);
        undo.redo();
        QCOMPARE(probe.index(eng, obj), -1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        undo.undo();
        undo.undo();
        QVERIFY(probe.index(eng, obj) >= 0);
        QCOMPARE(probe.value(eng, obj), before);
        delete w;
    }

    void createRenameAndDraftMarks_data() { deleteUndoBringsBackTheObjectAndItsSettings_data(); }

    void enterInAFieldPressesNoButton_data() { deleteUndoBringsBackTheObjectAndItsSettings_data(); }

    /*! Plan §4: Enter in a field commits that field only. A spin box passes Enter on to
     *  the dialog, which presses its default button: with none set, the first
     *  auto-default push button (New, in most of these editors). */
    void enterInAFieldPressesNoButton()
    {
        QFETCH(QString, family);
        const FamilyProbe probe = familyProbe(family);
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("enter_") + family, &why,
                                                  probe.fixture, QStringLiteral("phase7"));
        QVERIFY2(w, qPrintable(why));
        SWMM_Engine eng = w->modelLayer()->engine();
        QWidget host;
        QUndoStack undo;
        ComprehensiveEditorRegistry::instance().openForObject(probe.category, w->modelLayer(), &undo,
                                                              probe.object, &host);
        QDialog *dialog = probe.dialog(&host);
        QVERIFY(dialog);
        dialog->show();
        const auto visibleField = [dialog]() -> QAbstractSpinBox * {
            for (auto *spin : dialog->findChildren<QAbstractSpinBox *>()) {
                bool inView = false;
                for (QWidget *up = spin->parentWidget(); up && up != dialog; up = up->parentWidget())
                    inView = inView || qobject_cast<QAbstractItemView *>(up);
                if (!inView && spin->isEnabled() && spin->isVisibleTo(dialog))
                    return spin;
            }
            return nullptr;
        };
        QAbstractSpinBox *field = visibleField();
        for (auto *tabs : dialog->findChildren<QTabWidget *>()) // a field on another tab
            for (int i = 0; !field && i < tabs->count(); ++i) {
                tabs->setCurrentIndex(i);
                field = visibleField();
            }
        if (!field)
            QSKIP("this editor has no spin box field outside a table");
        const int n = probe.count(eng);
        field->setFocus();
        QTest::keyClick(field, Qt::Key_Return);
        QCoreApplication::processEvents();
        QCOMPARE(probe.count(eng), n);
        QCOMPARE(undo.count(), 0);
        QVERIFY(dialog->isVisible());
        dialog->close();
        delete w;
    }

    void everyFieldUndoesCompletely_data()
    {
        QTest::addColumn<QString>("family");
        QTest::addColumn<QString>("section");
        QTest::newRow("pollutant") << QStringLiteral("pollutant") << QStringLiteral("[POLLUTANTS]");
        QTest::newRow("aquifer") << QStringLiteral("aquifer") << QStringLiteral("[AQUIFERS]");
        QTest::newRow("street") << QStringLiteral("street") << QStringLiteral("[STREETS]");
        QTest::newRow("snowpack") << QStringLiteral("snowpack") << QStringLiteral("[SNOWPACKS]");
        QTest::newRow("lid") << QStringLiteral("lid") << QStringLiteral("[LID_CONTROLS]");
    }

    /*! Plan §9 record-completeness gate: change every field the editor shows,
     *  Apply, and the saved section changes; Undo, and it is the original text
     *  again; Redo, and it is the applied text again. A field the editor wrote
     *  outside its draft value would leave the Undo partial. */
    void everyFieldUndoesCompletely()
    {
        QFETCH(QString, family);
        QFETCH(QString, section);
        const FamilyProbe probe = familyProbe(family);
        QString why, err;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("complete_") + family, &why,
                                                  probe.fixture, QStringLiteral("fixes"));
        QVERIFY2(w, qPrintable(why));
        const QString base = outDir(QStringLiteral("fixes")) + QStringLiteral("/complete_") + family;
        const auto saved = [&](const QString &tag) {
            const QString path = base + QLatin1Char('_') + tag + QStringLiteral(".inp");
            QFile::remove(path);
            return w->saveAs(path, &err) ? inpSection(path, section) : QStringList{err};
        };
        const QStringList original = saved(QStringLiteral("A_original"));
        QWidget host;
        QUndoStack undo;
        ComprehensiveEditorRegistry::instance().openForObject(probe.category, w->modelLayer(), &undo,
                                                              probe.object, &host);
        QDialog *dialog = probe.dialog(&host);
        QVERIFY(dialog);
        DataObjectDraftSession *session = probe.edit(&host, probe.target); // the probed field
        QVERIFY(session);
        int changed = 0;
        if (auto *tabs = dialog->findChild<QTabWidget *>(QStringLiteral("lidDraftFields"))) {
            for (int i = 0; i < tabs->count(); ++i) { // the LID form: every tab
                tabs->setCurrentIndex(i);
                changed += changeEveryField(dialog);
                QCoreApplication::processEvents();
            }
        } else {
            changed = changeEveryField(dialog);
        }
        QVERIFY2(changed >= 5, qPrintable(QStringLiteral("only %1 fields changed").arg(changed)));
        QTRY_VERIFY(session->hasDraft());
        QVERIFY2(session->apply(), qPrintable(session->message()));
        const QStringList applied = saved(QStringLiteral("B_applied"));
        QVERIFY2(applied != original, qPrintable(applied.join(QLatin1Char('\n'))));
        dialog->close();
        undo.undo();
        const QStringList undone = saved(QStringLiteral("C_undone"));
        QCOMPARE(undone, original);
        undo.redo();
        QCOMPARE(saved(QStringLiteral("D_redone")), applied);
        delete w;
    }

    /*! Plan D5: New and rename in the editor are one undoable step each (the
     *  engine follows), and the list marks an object holding a draft until
     *  Revert. */
    void createRenameAndDraftMarks()
    {
        QFETCH(QString, family);
        const FamilyProbe probe = familyProbe(family);
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("create_rename_") + family, &why,
                                                  probe.fixture, QStringLiteral("fixes"));
        QVERIFY2(w, qPrintable(why));
        SWMM_Engine eng = w->modelLayer()->engine();
        QWidget host;
        QUndoStack undo;
        ComprehensiveEditorRegistry::instance().openForObject(probe.category, w->modelLayer(), &undo,
                                                              probe.object, &host);
        QDialog *dialog = probe.dialog(&host);
        QVERIFY(dialog);

        // Draft mark on the edited object's row, gone after Revert.
        QListView *list = nullptr;
        QModelIndex row;
        for (auto *view : dialog->findChildren<QListView *>())
            for (int r = 0; view->model() && r < view->model()->rowCount(); ++r)
                if (view->model()->index(r, 0).data().toString() == probe.object) {
                    list = view;
                    row = view->model()->index(r, 0);
                }
        QVERIFY(list && row.isValid());
        QVERIFY(!row.data(Qt::FontRole).value<QFont>().italic());
        DataObjectDraftSession *session = probe.edit(&host, probe.target);
        QVERIFY(session);
        QTRY_VERIFY(session->hasDraft());
        QTRY_VERIFY(row.data(Qt::FontRole).value<QFont>().italic());
        QVERIFY(!row.data(Qt::AccessibleDescriptionRole).toString().isEmpty());
        session->revert();
        QTRY_VERIFY(!row.data(Qt::FontRole).value<QFont>().italic());

        // New: one step; Undo removes it from the engine, Redo brings it back.
        const int n = probe.count(eng);
        QAbstractButton *newButton = buttonWithText(dialog, QStringLiteral("New"));
        if (!newButton)
            newButton = buttonWithText(dialog, QStringLiteral("Add"));
        if (!newButton)
            newButton = buttonWithText(dialog, QStringLiteral("New standard LID"));
        if (newButton)
            newButton->click();
        else
            QFAIL("no New in the editor");
        QTRY_COMPARE(probe.count(eng), n + 1);
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(probe.count(eng), n);
        undo.redo();
        QCOMPARE(probe.count(eng), n + 1);

        // Rename the probed object: one step; Undo restores the engine name.
        const QString renamed = QStringLiteral("Renamed_") + family;
        if (family == QStringLiteral("timeseries")) {
            for (int r = 0; r < list->model()->rowCount(); ++r)
                if (list->model()->index(r, 0).data().toString() == probe.object)
                    QVERIFY(list->model()->setData(list->model()->index(r, 0), renamed));
        } else {
            for (int r = 0; r < list->model()->rowCount(); ++r) // back to the probed object
                if (list->model()->index(r, 0).data().toString() == probe.object)
                    list->setCurrentIndex(list->model()->index(r, 0));
            QLineEdit *name = nullptr;
            for (auto *edit : dialog->findChildren<QLineEdit *>())
                if (edit->isVisible() && edit->text() == probe.object)
                    name = edit;
            QVERIFY(name);
            name->setText(renamed);
            emit name->editingFinished();
        }
        QTRY_VERIFY(probe.index(eng, renamed) >= 0);
        QCOMPARE(probe.index(eng, probe.object), -1);
        QCOMPARE(undo.count(), 2);
        dialog->close();
        undo.undo();
        QVERIFY(probe.index(eng, probe.object) >= 0);
        QCOMPARE(probe.index(eng, renamed), -1);
        undo.redo();
        QVERIFY(probe.index(eng, renamed) >= 0);
        delete w;
    }

    /*! Phase 6, Unit Hydrographs: the R-T-K and decay tables edit a draft per
     *  group (the engine is untouched until Apply, one undo step), and Undo of
     *  a group delete brings back its gage and parameter rows (plan D8). */
    void hydrographDraftUntilApplyAndUndoableGroups()
    {
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("hydrograph_draft"), &why,
                                                  QStringLiteral("data_object_phase6_fixture.inp"),
                                                  QStringLiteral("phase6"));
        QVERIFY2(w, qPrintable(why));
        const auto guard = qScopeGuard([w] { delete w; });
        SWMMModelLayer *layer = w->modelLayer();
        SWMM_Engine eng = layer->engine();
        const auto shortR = [eng](const char *group) { // ALL months, short response
            char name[64];
            int month = 0, response = 0;
            double v[6] = {};
            for (int i = 0; i < swmm_hydrograph_count(eng); ++i)
                if (swmm_hydrograph_get(eng, i, name, int(sizeof name), &month, &response, &v[0],
                                        &v[1], &v[2], &v[3], &v[4], &v[5]) == SWMM_OK &&
                    qstrcmp(name, group) == 0 && month == -1 && response == 0)
                    return v[0];
            return -1.0;
        };
        QCOMPARE(shortR("GroupB"), 0.1);
        QCOMPARE(swmm_rdii_decay_count(eng), 0);

        QWidget host;
        QUndoStack undo;
        ComprehensiveEditorRegistry::instance().openForObject(
            SWMMModelLayer::DataHydrographs, layer, &undo, QStringLiteral("GroupB"), &host);
        HydrographGroupEditor *editor = nullptr;
        for (auto *e : host.findChildren<HydrographGroupEditor *>())
            if (e->isVisible())
                editor = e;
        QVERIFY(editor && editor->rtkModel() && editor->decayModel());
        DataObjectDraftSession *session = editor->draftSession();
        auto *rtk = editor->rtkModel();
        auto *decay = editor->decayModel();
        QCOMPARE(rtk->index(0, HydrographRtkTableModel::ColR).data(Qt::EditRole).toDouble(), 0.1);
        QVERIFY(rtk->setData(rtk->index(0, HydrographRtkTableModel::ColR), 0.25, Qt::EditRole));
        QVERIFY(decay->setData(decay->index(0, HydrographDecayTableModel::ColActive), Qt::Checked,
                               Qt::CheckStateRole));
        QVERIFY(session->hasDraft());
        QCOMPARE(rtk->index(0, HydrographRtkTableModel::ColR).data(Qt::EditRole).toDouble(), 0.25);
        QCOMPARE(shortR("GroupB"), 0.1); // the engine has not seen it
        QCOMPARE(swmm_rdii_decay_count(eng), 0);

        QVERIFY2(session->apply(), qPrintable(session->message()));
        QCOMPARE(shortR("GroupB"), 0.25);
        QCOMPARE(swmm_rdii_decay_count(eng), 1);
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(shortR("GroupB"), 0.1);
        QCOMPARE(swmm_rdii_decay_count(eng), 0);
        undo.redo();
        QCOMPARE(shortR("GroupB"), 0.25);

        // Delete the group; Undo brings it back with its rows.
        bool seen = false;
        answerNextPrompt(QStringLiteral("Yes"), &seen);
        QVERIFY(QMetaObject::invokeMethod(editor, "onDeleteGroup"));
        QVERIFY(seen);
        QCOMPARE(shortR("GroupB"), -1.0);
        QCOMPARE(undo.count(), 2);
        undo.undo();
        QCOMPARE(shortR("GroupB"), 0.25);
        QCOMPARE(swmm_rdii_decay_count(eng), 1);
        editor->close();
    }

    /*! Phase 6, Control Rules: each rule's text is a draft until Apply (one
     *  undo step), kept while another rule is shown, and kept when the engine
     *  refuses it. New, Delete and Rename are one undo step each; Undo of a
     *  delete puts the rule back in its place in the rule order. */
    void rulesDraftUntilApplyAndUndoableStructure()
    {
        using openswmmvis::ui::RulesEditorDialog;
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("rules_draft"), &why,
                                                  QStringLiteral("data_object_phase6_fixture.inp"),
                                                  QStringLiteral("phase6"));
        QVERIFY2(w, qPrintable(why));
        const auto guard = qScopeGuard([w] { delete w; });
        SWMMModelLayer *layer = w->modelLayer();
        SWMM_Engine eng = layer->engine();
        const auto ruleText = [eng](int i) {
            QByteArray bytes(8192, 0);
            swmm_control_get_rule(eng, i, bytes.data(), int(bytes.size()));
            return QString::fromUtf8(bytes.constData());
        };
        const auto ruleId = [eng](int i) {
            char id[256] = {};
            swmm_control_get_id(eng, i, id, int(sizeof id));
            return QString::fromUtf8(id);
        };
        QCOMPARE(swmm_control_count(eng), 2);
        const QString originalB = ruleText(1);
        QVERIFY(originalB.contains(QStringLiteral("DEPTH > 2")));

        QWidget host;
        QUndoStack undo;
        ComprehensiveEditorRegistry::instance().openForObject(SWMMModelLayer::DataControls, layer,
                                                              &undo, QStringLiteral("RuleB"), &host);
        RulesEditorDialog *dialog = nullptr;
        for (auto *d : host.findChildren<RulesEditorDialog *>())
            if (d->isVisible())
                dialog = d;
        QVERIFY(dialog && dialog->currentProvider());
        DataObjectDraftSession *session = dialog->draftSession();
        auto *code = dialog->findChild<QTextEdit *>();
        QVERIFY(code);
        code->setPlainText(code->toPlainText().replace(QStringLiteral("DEPTH > 2"),
                                                       QStringLiteral("DEPTH > 3")));
        QVERIFY(session->hasDraft());
        QCOMPARE(ruleText(1), originalB); // the engine has not seen it

        // Another rule shows its own text; coming back shows the draft.
        dialog->openForRule(QStringLiteral("RuleA"));
        QVERIFY(code->toPlainText().contains(QStringLiteral("DEPTH > 1")));
        dialog->openForRule(QStringLiteral("RuleB"));
        QVERIFY(code->toPlainText().contains(QStringLiteral("DEPTH > 3")));

        QVERIFY2(session->apply(), qPrintable(session->message()));
        QVERIFY(ruleText(1).contains(QStringLiteral("DEPTH > 3")));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(ruleText(1), originalB);
        undo.redo();
        QVERIFY(ruleText(1).contains(QStringLiteral("DEPTH > 3")));

        // Text the engine refuses: nothing changes and the draft stays.
        code->setPlainText(QStringLiteral("RULE RuleB\nIF NODE NoSuchNode DEPTH > 1\n"
                                          "THEN CONDUIT C1 STATUS = CLOSED"));
        QVERIFY(!session->apply());
        QCOMPARE(session->state(), State::Rejected);
        QVERIFY(ruleText(1).contains(QStringLiteral("DEPTH > 3")));
        QVERIFY(session->hasDraft());
        QCOMPARE(undo.count(), 1);
        session->revert();

        // Delete the first rule; Undo puts it back first.
        dialog->openForRule(QStringLiteral("RuleA"));
        QString question;
        answerNextPromptReading(QStringLiteral("Yes"), &question);
        dialog->invokeDelete();
        QCOMPARE(swmm_control_count(eng), 1);
        QCOMPARE(undo.count(), 2);
        undo.undo();
        QCOMPARE(swmm_control_count(eng), 2);
        QCOMPARE(ruleId(0), QStringLiteral("RuleA"));
        QCOMPARE(ruleId(1), QStringLiteral("RuleB"));

        // Rename: one step; Undo gives the old name back.
        dialog->openForRule(QStringLiteral("RuleB"));
        QVERIFY(dialog->renameCurrent(QStringLiteral("RuleC")));
        QCOMPARE(ruleId(1), QStringLiteral("RuleC"));
        undo.undo();
        QCOMPARE(ruleId(1), QStringLiteral("RuleB"));
        dialog->close();
    }

    /*! Phase 6, Land Use: sweeping and the Buildup / Washoff grids edit one
     *  draft record. The engine and the shared record keep the applied values
     *  until Apply; Apply is one undo step that changes [LANDUSES], [BUILDUP]
     *  and [WASHOFF], and Undo gives back the original text. The grids keep
     *  the shared writer's rules (no coefficients on a NONE row), a pollutant
     *  added during a draft keeps the edits, and Undo of a delete brings the
     *  buildup back (plan D8). */
    void landUseDraftCoversSweepBuildupAndWashoff()
    {
        using openswmmvis::ui::BuildupTableModel;
        using openswmmvis::ui::LandUseEditorDialog;
        using openswmmvis::ui::WashoffTableModel;
        QString why, err;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("landuse_record"), &why,
                                                  QStringLiteral("data_object_phase6_fixture.inp"),
                                                  QStringLiteral("phase6"));
        QVERIFY2(w, qPrintable(why));
        const auto guard = qScopeGuard([w] { delete w; });
        SWMMModelLayer *layer = w->modelLayer();
        SWMM_Engine eng = layer->engine();
        const QString base = outDir(QStringLiteral("phase6")) + QStringLiteral("/landuse_record");
        const auto saved = [&](const QString &tag) {
            const QString path = base + QLatin1Char('_') + tag + QStringLiteral(".inp");
            QFile::remove(path);
            if (!w->saveAs(path, &err))
                return QStringList{err};
            return inpSection(path, QStringLiteral("[LANDUSES]")) +
                   inpSection(path, QStringLiteral("[BUILDUP]")) +
                   inpSection(path, QStringLiteral("[WASHOFF]"));
        };
        const auto buildupC1 = [eng](const char *landUse, int pollutant) {
            int ft = 0, norm = 0;
            double c1 = -1, c2 = 0, c3 = 0;
            swmm_buildup_get(eng, swmm_landuse_index(eng, landUse), pollutant, &ft, &c1, &c2, &c3,
                             &norm);
            return c1;
        };
        const QStringList original = saved(QStringLiteral("A_original"));
        QWidget host;
        QUndoStack undo;
        ComprehensiveEditorRegistry::instance().openForObject(
            SWMMModelLayer::DataLandUses, layer, &undo, QStringLiteral("Commercial"), &host);
        auto *dialog = host.findChild<LandUseEditorDialog *>();
        QVERIFY(dialog);
        DataObjectDraftSession *session = dialog->draftSession();
        BuildupTableModel *buildup = dialog->buildupModel();
        WashoffTableModel *washoff = dialog->washoffModel();
        QCOMPARE(buildup->rowCount(), 2); // TSS, Lead
        QCOMPARE(buildup->index(0, BuildupTableModel::ColC1).data(Qt::EditRole).toDouble(), 80.0);

        // A NONE row takes no coefficients (the shared writer's rule).
        QVERIFY(!buildup->setData(buildup->index(1, BuildupTableModel::ColC1), 5.0, Qt::EditRole));
        QVERIFY(!session->hasDraft());

        spinFor(dialog, QStringLiteral("Sweep Interval (days)"))->setValue(3.5);
        QVERIFY(buildup->setData(buildup->index(0, BuildupTableModel::ColC1), 90.0, Qt::EditRole));
        QVERIFY(buildup->setData(buildup->index(1, BuildupTableModel::ColFunction),
                                 QStringLiteral("POW"), Qt::EditRole));
        QVERIFY(buildup->setData(buildup->index(1, BuildupTableModel::ColC1), 42.0, Qt::EditRole));
        QVERIFY(washoff->setData(washoff->index(0, WashoffTableModel::ColCoeff), 2.5, Qt::EditRole));
        QVERIFY(washoff->setData(washoff->index(1, WashoffTableModel::ColSweepEffic), 55.0,
                                 Qt::EditRole));
        QVERIFY(session->hasDraft());
        // The draft shows; the engine and the shared record keep the applied values.
        QCOMPARE(buildup->index(0, BuildupTableModel::ColC1).data(Qt::EditRole).toDouble(), 90.0);
        QCOMPARE(buildupC1("Commercial", 0), 80.0);
        const QVariantMap shared = DataObjectAttributeTableModel::dataRecord(
            layer, SWMMModelLayer::DataLandUses, QStringLiteral("Commercial"));
        QCOMPARE(shared.value(QStringLiteral("sweepInterval")).toDouble(), 7.0);
        QCOMPARE(undo.count(), 0);

        // A pollutant added meanwhile (the editor opened from an object does not
        // follow the pollutant set): the edits stay and Apply still writes them.
        auto *polls = qobject_cast<PollutantRegistry *>(layer->ensurePollutantRegistry());
        QVERIFY(polls && polls->create(QStringLiteral("Zinc")));
        QVERIFY(swmm_pollutant_index(eng, "Zinc") >= 0);
        QVERIFY2(session->apply(), qPrintable(session->message()));
        dialog->refreshPollutants();
        QCOMPARE(buildup->rowCount(), 3);
        QCOMPARE(buildup->index(0, BuildupTableModel::ColC1).data(Qt::EditRole).toDouble(), 90.0);
        QCOMPARE(undo.count(), 1);
        QCOMPARE(buildupC1("Commercial", 0), 90.0);
        QCOMPARE(buildupC1("Commercial", 1), 42.0);
        const QStringList applied = saved(QStringLiteral("B_applied"));
        QVERIFY(applied != original);
        QVERIFY(applied.join(QLatin1Char('\n')).contains(QStringLiteral("Commercial")));

        // Undo of a delete brings back the buildup, not just the land use.
        QString question;
        answerNextPromptReading(QStringLiteral("Yes"), &question);
        buttonWithText(dialog, QStringLiteral("Delete"))->click();
        QTRY_COMPARE(swmm_landuse_index(eng, "Commercial"), -1);
        QVERIFY2(question.contains(QStringLiteral("Undo brings back")), qPrintable(question));
        dialog->close();
        undo.undo();
        QVERIFY(swmm_landuse_index(eng, "Commercial") >= 0);
        QCOMPARE(buildupC1("Commercial", 0), 90.0);
        QCOMPARE(buildupC1("Commercial", 1), 42.0);

        undo.undo(); // the Apply
        QCOMPARE(buildupC1("Commercial", 0), 80.0);
        QVERIFY(polls->findByName(QStringLiteral("Zinc")));
        polls->remove(polls->findByName(QStringLiteral("Zinc")));
        QCOMPARE(saved(QStringLiteral("C_undone")), original);
    }

    /*! Phase 6, Snow Pack: the editor's own rules act on the draft, and the
     *  model sees nothing until Apply. A UEB site value on a pack inheriting a
     *  UEB project makes it an explicit UEB pack; "Project default" resets the
     *  site values; a value still being typed is applied by Ctrl+Return; the
     *  engine's reason for refusing a pack reaches the status line; and the
     *  picker's "Use Snow Pack" reads "Apply and Use" while there is a draft. */
    void snowpackDraftRulesAndPicker()
    {
        using openswmmvis::snowpack::SnowpackProvider;
        using openswmmvis::snowpack::SnowpackRegistry;
        using openswmmvis::ui::SnowpackEditorDialog;
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("snowpack_rules"), &why,
                                                  QStringLiteral("data_object_phase6_fixture.inp"),
                                                  QStringLiteral("phase6"));
        QVERIFY2(w, qPrintable(why));
        const auto guard = qScopeGuard([w] { delete w; });
        SWMMModelLayer *layer = w->modelLayer();
        SWMM_Engine eng = layer->engine();
        const int pack = swmm_snowpack_index(eng, "Snow");
        QVERIFY(pack >= 0);
        QCOMPARE(swmm_snow_set_model(eng, 1), SWMM_OK); // the project uses UEB
        auto *reg = qobject_cast<SnowpackRegistry *>(layer->ensureSnowpackRegistry());
        QVERIFY(reg);
        QUndoStack undo;
        SnowpackEditorDialog dialog(reg, layer);
        dialog.setUndoStack(&undo);
        dialog.openForSnowpack(QStringLiteral("Snow"));
        DataObjectDraftSession *session = dialog.draftSession();
        auto *kind = dialog.findChild<QComboBox *>(QStringLiteral("snowModelKind"));
        const auto spin = [&dialog](int k) {
            return dialog.findChild<QDoubleSpinBox *>(QStringLiteral("snowParameter%1").arg(k));
        };
        QVERIFY(kind && spin(SnowpackProvider::UebForest));
        QCOMPARE(kind->currentData().toInt(), -1);
        QVERIFY(spin(SnowpackProvider::UebForest)->isEnabled()); // inherited UEB

        // A site value makes the draft explicit UEB; the model is untouched.
        spin(SnowpackProvider::UebForest)->setValue(0.4);
        QCOMPARE(kind->currentData().toInt(), 1);
        SWMM_UebSnowpack ueb{};
        QCOMPARE(swmm_snowpack_get_ueb(eng, pack, &ueb), SWMM_OK);
        QCOMPARE(ueb.model, -1);
        QCOMPARE(ueb.forest_fraction, 0.0);
        QVERIFY(session->hasDraft());

        // Project default resets the site values in the draft; back to the
        // model's values, so no draft is left.
        kind->setCurrentIndex(kind->findData(-1));
        QCOMPARE(spin(SnowpackProvider::UebForest)->value(), 0.0);
        QVERIFY(!session->hasDraft());

        // A value still being typed (the spin boxes commit on Enter) is applied.
        QDoubleSpinBox *depth = spin(SnowpackProvider::RemovalDsnow);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.activateWindow();
        depth->setFocus();
        QTRY_VERIFY(depth->hasFocus());
        depth->selectAll();
        QTest::keyClicks(depth, QStringLiteral("2.5"));
        QVERIFY(!session->hasDraft());
        QTest::keyClick(depth, Qt::Key_Return, Qt::ControlModifier); // Apply's shortcut
        double r[6] = {};
        QCOMPARE(swmm_snowpack_get_removal(eng, pack, &r[0], &r[1], &r[2], &r[3], &r[4], &r[5]),
                 SWMM_OK);
        QCOMPARE(r[0], 2.5);
        QCOMPARE(undo.count(), 1);

        // Removal fractions over one: the engine refuses, says why, nothing changes.
        spin(SnowpackProvider::RemovalFSubcatch)->setValue(0.9);
        QVERIFY(!session->apply());
        QCOMPARE(session->state(), State::Rejected);
        QVERIFY2(session->message().contains(QStringLiteral("Snow (")), qPrintable(session->message()));
        QCOMPARE(swmm_snowpack_get_removal(eng, pack, &r[0], &r[1], &r[2], &r[3], &r[4], &r[5]),
                 SWMM_OK);
        QCOMPARE(r[5], 0.1);
        QVERIFY(session->hasDraft());
        QCOMPARE(undo.count(), 1);
        session->revert();
        dialog.hide();

        // The picker: "Use Snow Pack", "Apply and Use" with a draft (applied first).
        bool sawUse = false, sawApplyAndUse = false;
        QTimer::singleShot(0, [&] {
            auto *picker = qobject_cast<SnowpackEditorDialog *>(QApplication::activeModalWidget());
            if (!picker)
                return;
            auto *use = picker->findChild<QPushButton *>(QStringLiteral("draftClose"));
            sawUse = use && use->text() == QStringLiteral("Use Snow Pack");
            picker->findChild<QDoubleSpinBox *>(QStringLiteral("snowParameter21"))->setValue(3.5);
            sawApplyAndUse = use && use->text() == QStringLiteral("Apply and Use");
            if (use)
                use->click();
            else
                picker->reject();
        });
        QCOMPARE(SnowpackEditorDialog::pickSnowpack(reg, layer, QStringLiteral("Snow")),
                 QStringLiteral("Snow"));
        QVERIFY(sawUse);
        QVERIFY(sawApplyAndUse);
        QCOMPARE(swmm_snowpack_get_removal(eng, pack, &r[0], &r[1], &r[2], &r[3], &r[4], &r[5]),
                 SWMM_OK);
        QCOMPARE(r[0], 3.5);
    }

    /*! Phase 7, LID controls (plan D7): the form holds the shared writer's fields on a
     *  draft; their rules follow the draft (a type change shows that type's layers) and
     *  the model's checks refuse a bad value at once. Nothing reaches the model before
     *  Apply (one undo step, still undoable after close); a change made elsewhere is
     *  kept when Apply puts the draft on it; the picker applies first. */
    void lidStandardDraftRulesConflictAndPicker()
    {
        using openswmmvis::ui::LidControlEditorDialog;
        using Model = DataObjectAttributeTableModel;
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("lid_standard"), &why,
                                                  QStringLiteral("data_object_phase7_fixture.inp"),
                                                  QStringLiteral("phase7"));
        QVERIFY2(w, qPrintable(why));
        SWMMModelLayer *layer = w->modelLayer();
        SWMM_Engine eng = layer->engine();
        auto *reg = qobject_cast<openswmmvis::lid::LidControlRegistry *>(layer->ensureLidControlRegistry());
        QVERIFY(reg);
        QUndoStack undo;
        LidControlEditorDialog dialog(reg, layer);
        dialog.setUndoStack(&undo);
        dialog.openForLidControl(QStringLiteral("BioCell"));
        DataObjectDraftSession *session = dialog.draftSession();
        QVERIFY(session);
        const auto field = [&dialog](const QString &key) {
            return dialog.findChild<QWidget *>(QStringLiteral("lidField:") + key);
        };
        const auto surface = [eng](const char *name) {
            double a = -1, b = -1, c = -1;
            swmm_lid_get_surface(eng, swmm_lid_index(eng, name), &a, &b, &c);
            return a;
        };
        const auto soilKsat = [eng] {
            double v[6] = {-1, -1, -1, -1, -1, -1};
            swmm_lid_get_soil(eng, swmm_lid_index(eng, "BioCell"), &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
            return v[4];
        };
        const auto type = [eng](const char *name) {
            int code = -1;
            swmm_lid_get_type(eng, swmm_lid_index(eng, name), &code);
            return code;
        };
        // Every field the Properties panel edits is in the form (the record-based writer's list).
        for (const char *key : {"surfStorage", "surfVegetation", "surfSideSlope", "soilSuction", "storClogging",
                                "drainDelay", "drainOpen", "drainClose", "drainCoeff"})
            QVERIFY2(field(QString::fromLatin1(key)), key);
        auto *storage = qobject_cast<QDoubleSpinBox *>(field(QStringLiteral("surfStorage")));
        QVERIFY(storage);
        QCOMPARE(storage->value(), 6.0);
        storage->setValue(7.5);
        QTRY_VERIFY(session->hasDraft());
        QCOMPARE(surface("BioCell"), 6.0); // nothing written before Apply
        QCOMPARE(Model::lidProfile(layer, QStringLiteral("BioCell"))["surfStorage"].toDouble(), 6.0);

        // The model's checks act on the draft at once: porosity must exceed field capacity.
        qobject_cast<QDoubleSpinBox *>(field(QStringLiteral("soilPorosity")))->setValue(0.15);
        QTRY_VERIFY(!dialog.findChild<QLabel *>(QStringLiteral("lidDraftStatus"))->text().isEmpty());
        QTRY_COMPARE(qobject_cast<QDoubleSpinBox *>(field(QStringLiteral("soilPorosity")))->value(), 0.5);
        // A standard control does not become a layered one (New layered LID makes those).
        auto *kind = qobject_cast<QComboBox *>(field(QStringLiteral("type")));
        kind->setCurrentIndex(kind->findData(8));
        QTRY_COMPARE(qobject_cast<QComboBox *>(field(QStringLiteral("type")))->currentData().toInt(), 0);

        // The form follows the draft's type: Pave as a bio-retention cell has no Pavement layer.
        dialog.openForLidControl(QStringLiteral("Pave"));
        QVERIFY(field(QStringLiteral("paveThick")) && field(QStringLiteral("paveImpervious")));
        kind = qobject_cast<QComboBox *>(field(QStringLiteral("type")));
        kind->setCurrentIndex(kind->findData(0));
        QTRY_VERIFY(field(QStringLiteral("soilThick")) && !field(QStringLiteral("paveThick")));
        QCOMPARE(type("Pave"), 4);

        // Changed elsewhere (the Properties panel's writer) while BioCell holds a draft.
        QVERIFY(Model::applyValues(layer, SWMMModelLayer::DataLIDControls, {QStringLiteral("BioCell")},
                                   QStringLiteral("soilKsat"), 0.75, nullptr));
        QTRY_COMPARE(session->state(), State::Conflict);
        bool asked = false;
        answerNextPrompt(QStringLiteral("Apply My Changes"), &asked);
        QVERIFY2(session->apply(), qPrintable(session->message()));
        QVERIFY(asked);
        QCOMPARE(surface("BioCell"), 7.5);
        QCOMPARE(soilKsat(), 0.75); // kept: Apply puts the draft on what the model holds
        QCOMPARE(type("Pave"), 0);
        QCOMPARE(undo.count(), 1);
        dialog.close();
        undo.undo();
        QCOMPARE(surface("BioCell"), 6.0);
        QCOMPARE(type("Pave"), 4);
        QCOMPARE(soilKsat(), 0.75);
        undo.redo();
        QCOMPARE(surface("BioCell"), 7.5);
        QCOMPARE(type("Pave"), 0);

        // The picker: "Use LID Control", "Apply and Use" with a draft (applied first).
        bool sawUse = false, sawApplyAndUse = false;
        QTimer::singleShot(0, [&] {
            auto *picker = qobject_cast<LidControlEditorDialog *>(QApplication::activeModalWidget());
            if (!picker)
                return;
            auto *use = picker->findChild<QPushButton *>(QStringLiteral("draftClose"));
            sawUse = use && use->text() == QStringLiteral("Use LID Control");
            if (auto *s = picker->findChild<QDoubleSpinBox *>(QStringLiteral("lidField:surfStorage")))
                s->setValue(8.25);
            sawApplyAndUse = use && use->text() == QStringLiteral("Apply and Use");
            if (use)
                use->click();
            else
                picker->reject();
        });
        QCOMPARE(LidControlEditorDialog::pickLidControl(reg, layer, QStringLiteral("BioCell"), nullptr),
                 QStringLiteral("BioCell"));
        QVERIFY(sawUse);
        QVERIFY(sawApplyAndUse);
        QCOMPARE(surface("BioCell"), 8.25);

        // Save writes the model, not a registry copy: a stale value in the LID registry
        // (what the Save-time flush used to write over the model) does not reach the file.
        reg->findByName(QStringLiteral("Pave"))->setSurfStorage(99.0);
        QString err;
        const QString path = outDir(QStringLiteral("phase7")) + QStringLiteral("/lid_standard_saved.inp");
        QFile::remove(path);
        QVERIFY2(w->saveAs(path, &err), qPrintable(err));
        const QString lids = inpSection(path, QStringLiteral("[LID_CONTROLS]")).join(QLatin1Char('\n'));
        QVERIFY2(lids.contains(QStringLiteral("BioCell")) &&
                     !lids.contains(QRegularExpression(QStringLiteral("Pave\\s+SURFACE\\s+99"))),
                 qPrintable(lids));
        delete w;
    }

    /*! Phase 7, layered (NODE) LID: Insert / Up / Initialize materials, a layer's fields,
     *  treatment and the Richards flow model all change the draft only; Apply writes the
     *  whole profile as one undo step (still undoable after close); Revert drops it. */
    void lidNodeDraftLayersRichardsAndTreatment()
    {
        using openswmmvis::ui::LidControlEditorDialog;
        using Model = DataObjectAttributeTableModel;
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("lid_node"), &why,
                                                  QStringLiteral("data_object_phase7_fixture.inp"),
                                                  QStringLiteral("phase7"));
        QVERIFY2(w, qPrintable(why));
        SWMMModelLayer *layer = w->modelLayer();
        SWMM_Engine eng = layer->engine();
        auto *reg = qobject_cast<openswmmvis::lid::LidControlRegistry *>(layer->ensureLidControlRegistry());
        QVERIFY(reg);
        QUndoStack undo;
        LidControlEditorDialog dialog(reg, layer);
        dialog.setUndoStack(&undo);
        dialog.openForLidControl(QStringLiteral("Stack"));
        DataObjectDraftSession *session = dialog.draftSession();
        const int control = swmm_lid_index(eng, "Stack");
        const auto flowModel = [eng, control] {
            SWMM_LidRichardsOptions options{};
            swmm_lid_richards_options_get(eng, control, &options);
            return options.model;
        };
        const auto treatmentRemoval = [](const QVariantMap &record) { // SURFACE 1, its one treatment (TSS)
            for (auto it = record.cbegin(); it != record.cend(); ++it)
                if (it.key().startsWith(QStringLiteral("node/0/1/t/")) && it.key().endsWith(QStringLiteral("/removal")) &&
                    record.value(it.key().chopped(7) + QStringLiteral("present")).toInt() == 1)
                    return it.value().toDouble();
            return -1.0;
        };
        auto *layers = dialog.findChild<QListWidget *>(QStringLiteral("lidLiveLayerOrder"));
        QVERIFY(layers);
        QCOMPARE(layers->count(), 3);

        // Insert a MEDIA layer before AGGREGATE 1: the draft only.
        layers->setCurrentRow(2);
        dialog.findChild<QComboBox *>(QStringLiteral("lidInsertKind"))->setCurrentIndex(1);
        dialog.findChild<QPushButton *>(QStringLiteral("lidLayer_insert"))->click();
        QCOMPARE(layers->count(), 4);
        QCOMPARE(layers->item(2)->text(), QStringLiteral("MEDIA 2"));
        QVERIFY(session->hasDraft());
        QCOMPARE(swmm_lid_node_layer_count(eng, control), 3);
        // Its fields are in the form; edit its thickness and SURFACE 1's TSS removal.
        auto *thick = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("lidField:node/1/2/p/0"));
        QVERIFY(thick);
        thick->setValue(9);
        QDoubleSpinBox *removal = nullptr;
        for (auto *s : dialog.findChildren<QDoubleSpinBox *>())
            if (s->objectName().startsWith(QStringLiteral("lidField:node/0/1/t/")) &&
                s->objectName().endsWith(QStringLiteral("/removal")))
                removal = s;
        QVERIFY(removal);
        QCOMPARE(removal->value(), 25.0);
        removal->setValue(40);
        // Richards needs complete materials: Initialize materials, then the flow model.
        dialog.findChild<QPushButton *>(QStringLiteral("lidInitializeMaterials"))->click();
        QTRY_VERIFY(dialog.findChild<QComboBox *>(QStringLiteral("lidField:flowModel")));
        auto *flow = dialog.findChild<QComboBox *>(QStringLiteral("lidField:flowModel"));
        flow->setCurrentIndex(flow->findData(1));
        QTRY_VERIFY(dialog.findChild<QSpinBox *>(QStringLiteral("lidField:flowCells")));
        QTRY_VERIFY(dialog.findChild<QWidget *>(QStringLiteral("lidRetentionCurves"))->isVisibleTo(&dialog));
        QCOMPARE(flowModel(), 0);
        QCOMPARE(swmm_lid_node_layer_count(eng, control), 3);
        QVERIFY(dialog.grab().save(outDir(QStringLiteral("phase7")) + QStringLiteral("/lid_node_draft.png")));

        const QVariant revision = reg->findByName(QStringLiteral("Stack"))->property("lidStructureRevision");
        QVERIFY2(session->apply(), qPrintable(session->message()));
        QCOMPARE(swmm_lid_node_layer_count(eng, control), 4);
        // A layer change is a whole-profile step: older per-field steps and held Properties
        // selections on Stack no longer address the right layers, so they stop applying.
        QVERIFY(reg->findByName(QStringLiteral("Stack"))->property("lidStructureRevision") != revision);
        const QVariantMap applied = Model::lidProfile(layer, QStringLiteral("Stack"));
        QCOMPARE(applied["node/1/2/p/0"].toDouble(), 9.0);
        QCOMPARE(treatmentRemoval(applied), 40.0);
        QCOMPARE(flowModel(), 1);
        QCOMPARE(undo.count(), 1);
        dialog.close();
        undo.undo();
        QCOMPARE(swmm_lid_node_layer_count(eng, control), 3);
        QCOMPARE(flowModel(), 0);
        QCOMPARE(treatmentRemoval(Model::lidProfile(layer, QStringLiteral("Stack"))), 25.0);
        undo.redo();
        QCOMPARE(swmm_lid_node_layer_count(eng, control), 4);

        // Up on the draft moves MEDIA 2's values above MEDIA 1; Revert drops it.
        dialog.openForLidControl(QStringLiteral("Stack"));
        QTRY_COMPARE(layers->count(), 4);
        layers->setCurrentRow(2);
        dialog.findChild<QPushButton *>(QStringLiteral("lidLayer_up"))->click();
        QTRY_COMPARE(dialog.findChild<QDoubleSpinBox *>(QStringLiteral("lidField:node/1/1/p/0"))->value(), 9.0);
        QCOMPARE(layers->currentRow(), 1);
        QVERIFY(session->hasDraft());
        QCOMPARE(Model::lidProfile(layer, QStringLiteral("Stack"))["node/1/1/p/0"].toDouble(), 12.0);
        session->revert();
        QVERIFY(!session->hasDraft());
        QTRY_COMPARE(dialog.findChild<QDoubleSpinBox *>(QStringLiteral("lidField:node/1/1/p/0"))->value(), 12.0);
        dialog.close();
        delete w;
    }

    /*! Cost of the draft at scale: opening the editor (the draft shares the
     *  model's points), the first edit (copies them) with its indicator check,
     *  Apply (engine write) and the Undo of that Apply. Recorded, not gated. */
    void seriesDraftTiming_data()
    {
        QTest::addColumn<int>("points");
        QTest::newRow("100k") << 100000;
        QTest::newRow("1M") << 1000000;
    }

    void seriesDraftTiming()
    {
        QFETCH(int, points);
        QString why;
        SWMMVisProjectWindow *w = openFixtureCopy(QStringLiteral("ts_timing"), &why,
                                                  QStringLiteral("single_rain_series.inp"),
                                                  QStringLiteral("phase3"));
        QVERIFY2(w, qPrintable(why));
        SWMMModelLayer *layer = w->modelLayer();
        auto *reg = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
        QVERIFY(reg);
        const QString name = QStringLiteral("BIG");
        auto *big = reg->create(name);
        QVERIFY(big);
        QVector<TimeseriesPoint> pts;
        pts.reserve(points);
        // UTC: stepping a local-time start across a daylight-saving change
        // would give wall-clock times that go backwards, which the engine refuses.
        const QDateTime start = reg->simulationStart();
        const QDateTime t0 = start.isValid() ? QDateTime(start.date(), start.time(), QTimeZone::UTC)
                                             : QDateTime(QDate(2000, 1, 1), QTime(0, 0), QTimeZone::UTC);
        for (int i = 0; i < points; ++i)
            pts.push_back({t0.addSecs(60LL * i), double(i % 97)});
        QVERIFY(big->setAllPoints(pts));
        QVERIFY(reg->flushPendingEdits());

        QWidget host;
        QUndoStack undo;
        QElapsedTimer t;
        t.start();
        auto *dialog = openSeriesEditor(layer, &undo, &host, name);
        const qint64 openMs = t.elapsed();
        QVERIFY(dialog);

        t.restart();
        QVERIFY(dialog->tableModel()->setData(dialog->tableModel()->index(10, 1), 1234.5));
        QCoreApplication::processEvents(); // the coalesced indicator check
        const qint64 editMs = t.elapsed();
        QCOMPARE(dialog->draftSession()->state(), State::Unapplied);

        t.restart();
        const bool dirty = dialog->draftSession()->hasDraft();
        const qint64 checkMs = t.elapsed();
        QVERIFY(dirty);

        t.restart();
        QVERIFY(dialog->draftSession()->apply());
        const qint64 applyMs = t.elapsed();
        QCOMPARE(engineSeriesValue(layer, name, 10), 1234.5);

        t.restart();
        undo.undo();
        const qint64 undoMs = t.elapsed();
        QCOMPARE(engineSeriesValue(layer, name, 10), double(10 % 97));

        t.restart();
        QVERIFY(dialog->tableModel()->setData(dialog->tableModel()->index(20, 1), 99.0));
        dialog->draftSession()->revert();
        const qint64 revertMs = t.elapsed();

        // Worst case for the dirty check: the only difference is the last row.
        QVERIFY(dialog->tableModel()->setData(dialog->tableModel()->index(points - 1, 1), -1.0));
        t.restart();
        QVERIFY(dialog->draftSession()->hasDraft());
        const qint64 lastRowCheckMs = t.elapsed();
        dialog->draftSession()->revert();

        g_timing << QStringLiteral("| %1 | %2 | %3 | %4 | %5 | %6 | %7 | %8 |")
                        .arg(points)
                        .arg(openMs)
                        .arg(editMs)
                        .arg(checkMs)
                        .arg(lastRowCheckMs)
                        .arg(applyMs)
                        .arg(undoMs)
                        .arg(revertMs);
        dialog->close();
        delete w;
    }

    void cleanupTestCase()
    {
        if (g_timing.isEmpty())
            return;
        QFile f(outDir(QStringLiteral("phase3")) + QStringLiteral("/timing.md"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        QTextStream ts(&f);
        ts << "Time series draft cost, milliseconds (offscreen, "
           << QDateTime::currentDateTime().toString(Qt::ISODate) << ").\n\n"
           << "| Points | Open editor | First edit + indicator | Dirty check (row 10 edited) | "
              "Dirty check (last row edited) | Apply | Undo of Apply | Edit + Revert |\n"
              "|---|---|---|---|---|---|---|---|\n";
        for (const QString &row : std::as_const(g_timing))
            ts << row << "\n";
    }
};

QTEST_MAIN(TestDataObjectDraftSession)
#include "test_dataobject_draft_session.moc"
