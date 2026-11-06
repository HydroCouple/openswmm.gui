/**
 * @file swmmvistimeseries.cpp
 * @brief Native collection import entry point with explicit CSV mapping and guarded publication.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "layers/timeserieslayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "plugins/extensionstateregistry.h"
#include "timeseries/timeseriesstore.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSaveFile>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>

namespace
{
/** @brief Worker-owned outcome distinguishing created assets from a refused destination. */
struct LocalImportResult {
    QString error; ///< Source/validation failure, empty on successful preparation.
    bool created = false; ///< This worker atomically created the requested native package.
};
/** @brief Decode a bounded single-line CSV header, retaining quoted commas and escaped quotes.
 * @param[in] bytes UTF-8 first record; multiline headers are intentionally refused.
 * @return Unique nonempty column names, or empty for invalid/unclosed quoting. */
QStringList csvHeaders(const QByteArray &bytes)
{
    QString line = QString::fromUtf8(bytes);
    if (line.startsWith(QChar(0xfeff)))
        line.remove(0, 1);
    line = line.trimmed();
    QStringList headers;
    QString field;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const auto c = line[i];
        if (c == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (c == ',' && !quoted) {
            headers.append(field.trimmed());
            field.clear();
        } else {
            field += c;
        }
    }
    headers.append(field.trimmed());
    if (quoted || headers.contains(QString{}) || QSet<QString>(headers.begin(), headers.end()).size() != headers.size())
        return {};
    return headers;
}
}
void SWMMVis::onAddTimeSeriesLayer()
{
    const QPointer<SWMMVis> self(this);
    const QPointer<SWMMVisProjectWindow> project(activeProjectWindow());
    const QPointer<MapCanvas> canvas(project ? project->canvas() : nullptr);
    if (!canvas) {
        QMessageBox::information(this, tr("Time-series layer"), tr("Open a project to add a layer."));
        return;
    }
    const QPointer<openswmmvis::ExtensionStateRegistry> registry(
        openswmmvis::ExtensionStateRegistry::instance());
    const auto session = registry ? registry->sessionForOwner(project) : QUuid{};
    const auto generation = registry ? registry->session(session).loadGeneration : 0;
    const auto originalTarget = [project, canvas, registry, session, generation] {
        return project && canvas && !project->isClosing() &&
               (!registry || (registry->sessionForOwner(project) == session &&
                              registry->session(session).loadGeneration == generation &&
                              registry->session(session).status == OpenSWMM::Sdk::SessionStatus::Ready));
    };
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Add time-series layer"), {},
        tr("Time-series sources (*.gpkg *.json *.csv);;All files (*)"));
    if (!self || !originalTarget() || path.isEmpty())
        return;
    QString descriptor = path;
    QString package;
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "csv") {
        QFile file(path);
        const auto headers = file.open(QIODevice::ReadOnly) ? csvHeaders(file.readLine(262145))
                                                           : QStringList{};
        if (headers.size() < 2) {
            QMessageBox::warning(this, tr("Import time series"),
                                  tr("Choose a CSV with unique column headers, time and value columns."));
            return;
        }
        QDialog mapping;
        mapping.setWindowModality(Qt::ApplicationModal);
        connect(project, &QObject::destroyed, &mapping, &QDialog::reject);
        connect(this, &QObject::destroyed, &mapping, &QDialog::reject);
        mapping.setWindowTitle(tr("Import time series"));
        auto *layout = new QVBoxLayout(&mapping);
        auto *form = new QFormLayout;
        auto *time = new QComboBox(&mapping);
        auto *value = new QComboBox(&mapping);
        auto *series = new QComboBox(&mapping);
        time->addItems(headers);
        value->addItems(headers);
        value->setCurrentIndex(1);
        series->addItem(tr("One series"), QString{});
        for (const auto &header : headers)
            series->addItem(header, header);
        auto *unit = new QLineEdit(&mapping);
        unit->setPlaceholderText(tr("Source unit; leave blank if unknown"));
        auto *basis = new QComboBox(&mapping);
        basis->addItem(tr("Timestamps with UTC offsets"), QStringLiteral("instant"));
        basis->addItem(tr("Calendar dates / daily values"), QStringLiteral("calendar-date"));
        form->addRow(tr("Time column"), time);
        form->addRow(tr("Value column"), value);
        form->addRow(tr("Series identifier"), series);
        form->addRow(tr("Units"), unit);
        form->addRow(tr("Time meaning"), basis);
        layout->addLayout(form);
        auto *hint = new QLabel(tr("Blank values remain missing. Original values and timestamps are retained. "
                                   "Creating a layer does not assign model inputs."), &mapping);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &mapping);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &mapping, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &mapping, &QDialog::reject);
        if (mapping.exec() != QDialog::Accepted || !self || !originalTarget())
            return;
        if (time->currentText() == value->currentText()) {
            QMessageBox::warning(this, tr("Import time series"), tr("Choose different time and value columns."));
            return;
        }
        package = QFileDialog::getSaveFileName(this, tr("Save native time-series data"),
            QFileInfo(path).absolutePath() + '/' + QFileInfo(path).completeBaseName() + "-series.gpkg",
            tr("GeoPackage (*.gpkg)"));
        if (!self || !originalTarget() || package.isEmpty())
            return;
        if (!package.endsWith(".gpkg", Qt::CaseInsensitive))
            package += ".gpkg";
        descriptor = package + ".import.json";
        if (QFileInfo::exists(package) || QFileInfo::exists(descriptor)) {
            QMessageBox::warning(this, tr("Import time series"), tr("Choose a new destination; existing assets are retained."));
            return;
        }
        QJsonObject columns{{"time", time->currentText()}, {"value", value->currentText()}};
        if (!series->currentData().toString().isEmpty())
            columns["sourceSeriesId"] = series->currentData().toString();
        QJsonObject defaults{{"provider", "local-file"}, {"collection", "observations"},
                             {"sourceSeriesId", "series"}, {"unit", unit->text().trimmed()},
                             {"title", QFileInfo(path).completeBaseName()},
                             {"timeBasis", basis->currentData().toString()}};
        const QJsonObject spec{{"schema", "openswmm.timeseries-import/1"},
            {"datasetId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"title", QFileInfo(path).completeBaseName()},
            {"sources", QJsonArray{QJsonObject{{"path", QFileInfo(path).absoluteFilePath()},
                                               {"columns", columns}, {"defaults", defaults}}}}};
        QSaveFile output(descriptor);
        const auto bytes = QJsonDocument(spec).toJson();
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
            QMessageBox::warning(this, tr("Import time series"), tr("The import description could not be saved."));
            return;
        }
    } else if (suffix != "gpkg") {
        package = QFileInfo(path).absolutePath() + "/series-" +
                  QUuid::createUuid().toString(QUuid::WithoutBraces) + ".gpkg";
    }
    const auto publish = [self, project, canvas, session, generation](const QString &source) {
        if (!self || !canvas || !project || project->isClosing())
            return;
        auto *registry = openswmmvis::ExtensionStateRegistry::instance();
        if (registry && (registry->sessionForOwner(project) != session ||
                         registry->session(session).loadGeneration != generation)) {
            self->statusBar()->showMessage(tr("Saved, not added: the project was reloaded. %1").arg(source), 15000);
            return;
        }
        auto *layer = new TimeSeriesLayer(project->modelLayer() ? project->modelLayer()->workspace() : nullptr);
        QString error;
        if (!layer->openTimeSeries(source, {}, &error)) {
            delete layer;
            QMessageBox::warning(self, tr("Time-series layer"), error);
            return;
        }
        self->wireTimeSeriesLayer(project, layer);
        canvas->addLayer(layer);
        if (project)
            project->setHasChanges(true);
    };
    if (suffix == "gpkg") {
        publish(path);
        return;
    }
    auto cancel = std::make_shared<std::atomic_bool>(false);
    auto *progress = new QProgressDialog(tr("Preparing time-series layer…"), tr("Cancel"), 0, 0, this);
    progress->setWindowModality(Qt::NonModal);
    progress->show();
    connect(progress, &QProgressDialog::canceled, progress, [cancel] { cancel->store(true); });
    connect(project, &QObject::destroyed, progress, [cancel] { cancel->store(true); });
    connect(this, &QObject::destroyed, progress, [cancel] { cancel->store(true); });
    auto *watcher = new QFutureWatcher<LocalImportResult>(this);
    connect(watcher, &QFutureWatcher<LocalImportResult>::finished, this,
            [self, watcher, progress, cancel, package, publish] {
        const auto result = watcher->result();
        watcher->deleteLater();
        progress->deleteLater();
        if (!self || cancel->load()) {
            if (result.created)
                QFile::remove(package);
            return;
        }
        if (!result.error.isEmpty())
            QMessageBox::warning(self, tr("Import time series"), result.error);
        else
            publish(package);
    });
    watcher->setFuture(QtConcurrent::run([descriptor, package, cancel] {
        LocalImportResult result;
        result.created = OpenSWMM::TimeSeries::TimeSeriesStore::importDescriptor(
            descriptor, package, &result.error, cancel.get());
        if (cancel->load() && result.created) {
            QFile::remove(package);
            result.created = false;
        }
        return result;
    }));
}
