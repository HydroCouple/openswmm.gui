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
        QVERIFY(dialog->editedSeries()->observationBinding().isEmpty()); // cancelled: nothing chosen
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
        for (const char *family : {"pollutant", "aquifer", "street", "inlet", "transect", "timeseries"})
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

    void everyFieldUndoesCompletely_data()
    {
        QTest::addColumn<QString>("family");
        QTest::addColumn<QString>("section");
        QTest::newRow("pollutant") << QStringLiteral("pollutant") << QStringLiteral("[POLLUTANTS]");
        QTest::newRow("aquifer") << QStringLiteral("aquifer") << QStringLiteral("[AQUIFERS]");
        QTest::newRow("street") << QStringLiteral("street") << QStringLiteral("[STREETS]");
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
        const int changed = changeEveryField(dialog);
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
