// SPDX-License-Identifier: GPL-3.0-or-later
#include "output/traceanalysisstore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <gdal_priv.h>
#include <limits>
#include <ogrsf_frmts.h>
#include <stdexcept>

namespace openswmmvis::trace
{
namespace
{
using DS = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;
using Feature = std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)>;
const double nan = std::numeric_limits<double>::quiet_NaN();
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
void checkCancel(std::atomic_bool *c)
{
    if (c && c->load())
        throw std::runtime_error("Analysis cancelled");
}
DS open(const QString &p, bool write)
{
    GDALAllRegister();
    return {static_cast<GDALDataset *>(
                GDALOpenEx(p.toUtf8().constData(),
                           GDAL_OF_VECTOR | (write ? GDAL_OF_UPDATE : GDAL_OF_READONLY), nullptr,
                           nullptr, nullptr)),
            GDALClose};
}
QString json(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
QJsonObject object(const char *s) { return QJsonDocument::fromJson(QByteArray(s)).object(); }
void field(OGRLayer *l, const char *n, OGRFieldType t)
{
    OGRFieldDefn f(n, t);
    require(l->CreateField(&f) == OGRERR_NONE, "Cannot create analysis field");
}
void number(OGRFeature *f, const char *n, double v)
{
    int i = f->GetFieldIndex(n);
    if (std::isfinite(v))
        f->SetField(i, v);
    else
        f->SetFieldNull(i);
}
double number(const OGRFeature *f, const char *n)
{
    int i = f->GetFieldIndex(n);
    return i >= 0 && f->IsFieldSetAndNotNull(i) ? f->GetFieldAsDouble(i) : nan;
}
void textField(OGRFeature *f, const char *n, const QString &v)
{
    f->SetField(n, v.toUtf8().constData());
}
QString str(const OGRFeature *f, const char *n)
{
    return QString::fromUtf8(f->GetFieldAsString(n));
}
Feature feature(OGRLayer *l)
{
    return {OGRFeature::CreateFeature(l->GetLayerDefn()), OGRFeature::DestroyFeature};
}
void insert(OGRLayer *l, OGRFeature *f)
{
    require(l->CreateFeature(f) == OGRERR_NONE, "Cannot write analysis feature");
}
OGRLayer *table(GDALDataset *d, const char *n)
{
    auto *l = d->GetLayerByName(n);
    require(l, "Missing analysis table");
    return l;
}
OGRLayer *attributes(GDALDataset *d, const char *n)
{
    auto *l = d->CreateLayer(n, nullptr, wkbNone, nullptr);
    require(l, "Cannot create analysis table");
    return l;
}
QString tableName(const QString &id, bool nodes)
{
    QString token = id;
    token.remove('-');
    require(token.size() == 32, "Invalid analysis ID");
    for (QChar c : token)
        require(c.isDigit() || (c >= 'a' && c <= 'f'), "Invalid analysis ID");
    return QStringLiteral("trace_%1_%2")
        .arg(token, nodes ? QStringLiteral("nodes") : QStringLiteral("links"));
}
OGRLayer *features(GDALDataset *d, const QString &name, bool nodes, const QString &wkt, bool traced)
{
    OGRSpatialReference srs;
    srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    OGRSpatialReference *ref = nullptr;
    if (!wkt.isEmpty() && srs.SetFromUserInput(wkt.toUtf8().constData()) == OGRERR_NONE)
        ref = &srs;
    auto *l =
        d->CreateLayer(name.toUtf8().constData(), ref, nodes ? wkbPoint : wkbLineString, nullptr);
    require(l, "Cannot create spatial analysis layer");
    field(l, "object_index", OFTInteger);
    field(l, "object_id", OFTString);
    field(l, "object_type", OFTInteger);
    field(l, "input_flags", OFTInteger);
    field(l, "flags", OFTInteger);
    field(l, "status", OFTString);
    field(l, "first_volume_m3", OFTReal);
    field(l, "last_volume_m3", OFTReal);
    if (nodes)
    {
        for (auto n : {"mean_volume_m3", "mean_inflow_m3s", "lateral_in_m3s", "withdrawal_m3s",
                       "overflow_m3s", "mean_outgoing_m3s", "residence_s"})
            field(l, n, OFTReal);
    }
    else
    {
        field(l, "node1", OFTString);
        field(l, "node2", OFTString);
        field(l, "dominant_dir", OFTInteger);
        for (auto n : {"length_m", "q_net_mean_m3s", "q_net_abs_m3s", "q_abs_mean_m3s",
                       "v_abs_mean_mps", "forward_volume_m3", "reverse_volume_m3", "local_time_s"})
            field(l, n, OFTReal);
    }
    if (traced)
    {
        for (auto n : {"trace_ratio", "time_s", "time_coverage", "from_time_s", "to_time_s",
                       "terminal_fraction"})
            field(l, n, OFTReal);
        field(l, "terminal_kind", OFTInteger);
        field(l, "is_seed", OFTInteger);
    }
    return l;
}
void writeFeatures(GDALDataset *d, const Dataset &data, bool nodes, const QString &name,
                   const Result *r, std::atomic_bool *cancel)
{
    auto *l = features(d, name, nodes, data.snapshot.wkt, r);
    int count = nodes ? data.nodes.size() : data.links.size();
    for (int i = 0; i < count; ++i)
    {
        checkCancel(cancel);
        auto f = feature(l);
        f->SetField("object_index", i);
        int flags = 0;
        if (nodes)
        {
            const auto &n = data.snapshot.nodes[i];
            const auto &a = data.nodes[i];
            textField(f.get(), "object_id", n.id);
            f->SetField("object_type", n.type);
            f->SetField("input_flags", n.flags);
            if (n.hasGeometry)
            {
                OGRPoint p(n.point.x(), n.point.y());
                f->SetGeometry(&p);
            }
            number(f.get(), "mean_volume_m3", a.volume_m3);
            number(f.get(), "mean_inflow_m3s", a.inflow_m3s);
            number(f.get(), "lateral_in_m3s", a.lateral_in_m3s);
            number(f.get(), "withdrawal_m3s", a.withdrawal_m3s);
            number(f.get(), "overflow_m3s", a.overflow_m3s);
            number(f.get(), "mean_outgoing_m3s", a.outgoing_m3s);
            number(f.get(), "residence_s", a.residence_s);
            number(f.get(), "first_volume_m3", a.first_volume_m3);
            number(f.get(), "last_volume_m3", a.last_volume_m3);
            flags = a.flags;
        }
        else
        {
            const auto &v = data.snapshot.links[i];
            const auto &a = data.links[i];
            textField(f.get(), "object_id", v.id);
            textField(f.get(), "node1", data.snapshot.nodes[v.from].id);
            textField(f.get(), "node2", data.snapshot.nodes[v.to].id);
            f->SetField("object_type", v.type);
            f->SetField("dominant_dir", a.direction);
            if (v.points.size() > 1)
            {
                OGRLineString line;
                for (auto p : v.points)
                    line.addPoint(p.x(), p.y());
                f->SetGeometry(&line);
            }
            number(f.get(), "length_m", v.length);
            number(f.get(), "q_net_mean_m3s", a.net_flow_m3s);
            number(f.get(), "q_net_abs_m3s", std::abs(a.net_flow_m3s));
            number(f.get(), "q_abs_mean_m3s", a.absolute_flow_m3s);
            number(f.get(), "v_abs_mean_mps", a.absolute_velocity_mps);
            number(f.get(), "forward_volume_m3", a.forward_volume_m3);
            number(f.get(), "reverse_volume_m3", a.reverse_volume_m3);
            number(f.get(), "local_time_s", a.travel_s);
            number(f.get(), "first_volume_m3", a.first_volume_m3);
            number(f.get(), "last_volume_m3", a.last_volume_m3);
            flags = a.flags;
        }
        if (r)
        {
            const auto &v = nodes ? r->nodes[i] : r->links[i];
            number(f.get(), "trace_ratio", v.ratio);
            number(f.get(), "time_s", v.time_s);
            number(f.get(), "time_coverage", v.time_coverage);
            number(f.get(), "from_time_s", v.from_time_s);
            number(f.get(), "to_time_s", v.to_time_s);
            number(f.get(), "terminal_fraction", v.terminal_fraction);
            f->SetField("terminal_kind", v.terminal_kind);
            f->SetField("is_seed", nodes && i == r->seed ? 1 : 0);
            flags = v.flags;
        }
        f->SetField("flags", flags);
        textField(f.get(), "status", statusText(flags));
        insert(l, f.get());
    }
}
void removeTable(GDALDataset *d, const QString &name)
{
    for (int i = 0; i < d->GetLayerCount(); ++i)
        if (QString::fromUtf8(d->GetLayer(i)->GetName()) == name)
        {
            require(d->DeleteLayer(i) == OGRERR_NONE, "Cannot replace analysis table");
            return;
        }
}
QJsonObject record(const OGRFeature *f)
{
    auto j = object(f->GetFieldAsString("recipe"));
    j["id"] = str(f, "analysis_id");
    j["style"] = object(f->GetFieldAsString("style"));
    j["summary"] = object(f->GetFieldAsString("summary"));
    return j;
}
} // namespace
bool AnalysisStore::prepare(const QString &path, const Dataset &data, QString *error,
                            std::atomic_bool *cancel)
{
    QString staging = path + QStringLiteral(".part-") +
                      QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".gpkg");
    try
    {
        QString validation;
        require(data.snapshot.valid(&validation), validation.toUtf8().constData());
        require(data.nodes.size() == data.snapshot.nodes.size() &&
                    data.links.size() == data.snapshot.links.size(),
                "Average counts differ from topology");
        if (QFile::exists(path))
        {
            auto old = readDataset(path, error);
            return old && old->runId == data.runId && old->fingerprint == data.fingerprint &&
                   old->snapshot.modelFingerprint == data.snapshot.modelFingerprint;
        }
        require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot create analysis directory");
        GDALAllRegister();
        auto *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        require(driver, "GeoPackage driver is unavailable");
        DS d(driver->Create(staging.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr),
             GDALClose);
        require(bool(d), "Cannot create analysis GeoPackage");
        require(d->StartTransaction() == OGRERR_NONE, "Cannot start analysis transaction");
        auto *run = attributes(d.get(), "analysis_runs");
        for (auto n : {"run_id", "source_id", "label", "output_path", "fingerprint",
                       "model_fingerprint", "provenance", "metadata"})
            field(run, n, OFTString);
        auto f = feature(run);
        textField(f.get(), "run_id", data.runId);
        textField(f.get(), "source_id", data.sourceId);
        textField(f.get(), "label", data.label);
        textField(f.get(), "output_path", data.outputPath);
        textField(f.get(), "fingerprint", data.fingerprint);
        textField(f.get(), "model_fingerprint", data.snapshot.modelFingerprint);
        textField(f.get(), "provenance", data.provenance);
        textField(f.get(), "metadata",
                  json({{"schema", 2},
                        {"averaging", QStringLiteral("Trapezoidal over first to last saved report; "
                                                     "report convention may be unknown")},
                        {"units", QStringLiteral("SI: m, m3, m3/s, m/s, s")},
                        {"info", infoJson(data.info)},
                        {"options", optionsJson(data.options)},
                        {"snapshot", data.snapshot.toJson()}}));
        insert(run, f.get());
        writeFeatures(d.get(), data, true, QStringLiteral("average_nodes"), nullptr, cancel);
        writeFeatures(d.get(), data, false, QStringLiteral("average_links"), nullptr, cancel);
        auto *a = attributes(d.get(), "analyses");
        for (auto n : {"analysis_id", "run_id", "recipe", "summary", "style"})
            field(a, n, OFTString);
        auto *s = attributes(d.get(), "analysis_summary");
        for (auto n : {"analysis_id", "quantity", "unit"})
            field(s, n, OFTString);
        field(s, "value", OFTReal);
        auto *diag = attributes(d.get(), "analysis_diagnostics");
        for (auto n : {"analysis_id", "object_id", "kind", "message"})
            field(diag, n, OFTString);
        field(diag, "flags", OFTInteger);
        checkCancel(cancel);
        require(d->CommitTransaction() == OGRERR_NONE, "Cannot commit prepared analysis");
        d.reset();
        require(QFile::rename(staging, path), "Cannot publish prepared analysis");
        return true;
    }
    catch (const std::exception &e)
    {
        if (error)
            *error = QString::fromUtf8(e.what());
        QFile::remove(staging);
        return false;
    }
}
bool AnalysisStore::save(const QString &path, const Result &r, QString *error,
                         std::atomic_bool *cancel)
{
    DS d(nullptr, GDALClose);
    try
    {
        require(bool(r.dataset), "Missing analysis dataset");
        require(r.nodes.size() == r.dataset->nodes.size() &&
                    r.links.size() == r.dataset->links.size(),
                "Trace result dimensions do not match");
        d = open(path, true);
        require(bool(d), "Cannot open analysis package for writing");
        auto *run = table(d.get(), "analysis_runs");
        Feature rf(run->GetNextFeature(), OGRFeature::DestroyFeature);
        require(rf && str(rf.get(), "run_id") == r.dataset->runId &&
                    str(rf.get(), "fingerprint") == r.dataset->fingerprint,
                "Analysis package belongs to another run");
        require(d->StartTransaction() == OGRERR_NONE, "Cannot start analysis transaction");
        auto *a = table(d.get(), "analyses");
        for (auto name : {"analyses", "analysis_summary", "analysis_diagnostics"})
        {
            auto *l = table(d.get(), name);
            l->ResetReading();
            QVector<GIntBig> old;
            while (auto *raw = l->GetNextFeature())
            {
                Feature f(raw, OGRFeature::DestroyFeature);
                if (str(f.get(), "analysis_id") == r.id)
                    old.append(f->GetFID());
            }
            for (auto fid : old)
                require(l->DeleteFeature(fid) == OGRERR_NONE, "Cannot replace saved analysis");
        }
        for (bool nodes : {true, false})
        {
            QString name = tableName(r.id, nodes);
            removeTable(d.get(), name);
            writeFeatures(d.get(), *r.dataset, nodes, name, &r, cancel);
        }
        auto f = feature(a);
        textField(f.get(), "analysis_id", r.id);
        textField(f.get(), "run_id", r.dataset->runId);
        textField(f.get(), "recipe",
                  json({{"seed", r.seed},
                        {"seedId", r.dataset->snapshot.nodes[r.seed].id},
                        {"direction", r.direction},
                        {"title", r.title()},
                        {"nodeTable", tableName(r.id, true)},
                        {"linkTable", tableName(r.id, false)}}));
        textField(f.get(), "summary", json(summaryJson(r.summary)));
        textField(f.get(), "style", json(r.style));
        insert(a, f.get());
        auto *s = table(d.get(), "analysis_summary");
        const char *kinds[] = {"Outfall",  "Loss",       "Source",
                               "Retained", "Unresolved", "Trapped circulation"};
        for (int i = 0; i < 6; ++i)
        {
            auto sf = feature(s);
            textField(sf.get(), "analysis_id", r.id);
            sf->SetField("quantity", kinds[i]);
            sf->SetField("unit", "fraction");
            number(sf.get(), "value", r.summary.terminal[i]);
            insert(s, sf.get());
        }
        auto budget = summaryJson(r.summary);
        for (auto it = budget.begin(); it != budget.end(); ++it)
            if (it.value().isDouble() && it.key() != "cyclic" && it.key() != "nodes" &&
                it.key() != "links")
            {
                auto sf = feature(s);
                textField(sf.get(), "analysis_id", r.id);
                textField(sf.get(), "quantity", it.key());
                textField(sf.get(), "unit",
                          it.key() == "accountingError" || it.key() == "solverResidual"
                              ? QStringLiteral("dimensionless")
                              : QStringLiteral("m3"));
                number(sf.get(), "value", it.value().toDouble());
                insert(s, sf.get());
            }
        auto *diag = table(d.get(), "analysis_diagnostics");
        for (bool nodes : {true, false})
        {
            const auto &values = nodes ? r.nodes : r.links;
            for (int i = 0; i < values.size(); ++i)
                if (values[i].flags)
                {
                    auto df = feature(diag);
                    textField(df.get(), "analysis_id", r.id);
                    textField(df.get(), "object_id",
                              nodes ? r.dataset->snapshot.nodes[i].id
                                    : r.dataset->snapshot.links[i].id);
                    df->SetField("kind", nodes ? "node" : "link");
                    df->SetField("flags", values[i].flags);
                    textField(df.get(), "message", statusText(values[i].flags));
                    insert(diag, df.get());
                }
        }
        checkCancel(cancel);
        require(d->CommitTransaction() == OGRERR_NONE, "Cannot commit saved estimate");
        return true;
    }
    catch (const std::exception &e)
    {
        if (d)
            d->RollbackTransaction();
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}
std::shared_ptr<Dataset> AnalysisStore::readDataset(const QString &path, QString *error)
{
    try
    {
        auto d = open(path, false);
        require(bool(d), "Cannot open analysis GeoPackage");
        auto *l = table(d.get(), "analysis_runs");
        Feature f(l->GetNextFeature(), OGRFeature::DestroyFeature);
        require(bool(f), "Missing run metadata");
        auto j = object(f->GetFieldAsString("metadata"));
        require(j["schema"].toInt() == 2, "Unsupported analysis schema");
        auto data = std::make_shared<Dataset>();
        data->runId = str(f.get(), "run_id");
        data->sourceId = str(f.get(), "source_id");
        data->label = str(f.get(), "label");
        data->outputPath = str(f.get(), "output_path");
        data->fingerprint = str(f.get(), "fingerprint");
        data->provenance = str(f.get(), "provenance");
        data->packagePath = path;
        data->snapshot = Snapshot::fromJson(j["snapshot"].toObject());
        data->info = infoFromJson(j["info"].toObject());
        data->options = optionsFromJson(j["options"].toObject());
        require(data->snapshot.valid(), "Invalid saved topology");
        data->nodes.resize(data->snapshot.nodes.size());
        data->links.resize(data->snapshot.links.size());
        for (bool nodes : {true, false})
        {
            auto *layer = table(d.get(), nodes ? "average_nodes" : "average_links");
            QSet<int> seen;
            int count = nodes ? data->nodes.size() : data->links.size();
            while (auto *raw = layer->GetNextFeature())
            {
                Feature v(raw, OGRFeature::DestroyFeature);
                int i = v->GetFieldAsInteger("object_index");
                require(i >= 0 && i < count && !seen.contains(i), "Invalid average row index");
                seen.insert(i);
                require(str(v.get(), "object_id") ==
                            (nodes ? data->snapshot.nodes[i].id : data->snapshot.links[i].id),
                        "Average IDs differ from snapshot");
                if (nodes)
                {
                    auto &a = data->nodes[i];
                    a.volume_m3 = number(v.get(), "mean_volume_m3");
                    a.inflow_m3s = number(v.get(), "mean_inflow_m3s");
                    a.lateral_in_m3s = number(v.get(), "lateral_in_m3s");
                    a.withdrawal_m3s = number(v.get(), "withdrawal_m3s");
                    a.overflow_m3s = number(v.get(), "overflow_m3s");
                    a.outgoing_m3s = number(v.get(), "mean_outgoing_m3s");
                    a.residence_s = number(v.get(), "residence_s");
                    a.first_volume_m3 = number(v.get(), "first_volume_m3");
                    a.last_volume_m3 = number(v.get(), "last_volume_m3");
                    a.flags = v->GetFieldAsInteger("flags");
                }
                else
                {
                    auto &a = data->links[i];
                    a.net_flow_m3s = number(v.get(), "q_net_mean_m3s");
                    a.absolute_flow_m3s = number(v.get(), "q_abs_mean_m3s");
                    a.absolute_velocity_mps = number(v.get(), "v_abs_mean_mps");
                    a.forward_volume_m3 = number(v.get(), "forward_volume_m3");
                    a.reverse_volume_m3 = number(v.get(), "reverse_volume_m3");
                    a.travel_s = number(v.get(), "local_time_s");
                    a.first_volume_m3 = number(v.get(), "first_volume_m3");
                    a.last_volume_m3 = number(v.get(), "last_volume_m3");
                    a.direction = v->GetFieldAsInteger("dominant_dir");
                    a.flags = v->GetFieldAsInteger("flags");
                }
            }
            require(seen.size() == count, "Incomplete average layer");
        }
        return data;
    }
    catch (const std::exception &e)
    {
        if (error)
            *error = QString::fromUtf8(e.what());
        return {};
    }
}
QVector<QJsonObject> AnalysisStore::analyses(const QString &path, QString *error)
{
    QVector<QJsonObject> rows;
    try
    {
        auto d = open(path, false);
        require(bool(d), "Cannot open analysis package");
        auto *l = table(d.get(), "analyses");
        while (auto *raw = l->GetNextFeature())
        {
            Feature f(raw, OGRFeature::DestroyFeature);
            rows.append(record(f.get()));
        }
    }
    catch (const std::exception &e)
    {
        if (error)
            *error = QString::fromUtf8(e.what());
    }
    return rows;
}
std::shared_ptr<Result> AnalysisStore::read(const QString &path, const QString &id, QString *error)
{
    try
    {
        auto data = readDataset(path, error);
        require(bool(data), "Cannot read saved analysis averages");
        auto d = open(path, false);
        require(bool(d), "Cannot open analysis package");
        auto *a = table(d.get(), "analyses");
        QJsonObject row;
        while (auto *raw = a->GetNextFeature())
        {
            Feature f(raw, OGRFeature::DestroyFeature);
            if (str(f.get(), "analysis_id") == id)
            {
                row = record(f.get());
                break;
            }
        }
        require(!row.isEmpty(), "Saved analysis is missing");
        auto r = std::make_shared<Result>();
        r->id = id;
        r->dataset = data;
        r->saved = true;
        r->seed = row["seed"].toInt(-1);
        r->direction = row["direction"].toInt(-1);
        r->style = row["style"].toObject();
        r->summary = summaryFromJson(row["summary"].toObject());
        require(r->seed >= 0 && r->seed < data->nodes.size() && r->direction >= 0 &&
                    r->direction <= 1,
                "Invalid saved recipe");
        for (bool nodes : {true, false})
        {
            auto *l = table(d.get(), tableName(id, nodes).toUtf8().constData());
            auto &values = nodes ? r->nodes : r->links;
            values.resize(nodes ? data->nodes.size() : data->links.size());
            QSet<int> seen;
            while (auto *raw = l->GetNextFeature())
            {
                Feature f(raw, OGRFeature::DestroyFeature);
                int i = f->GetFieldAsInteger("object_index");
                require(i >= 0 && i < values.size() && !seen.contains(i),
                        "Invalid trace row index");
                seen.insert(i);
                require(str(f.get(), "object_id") ==
                            (nodes ? data->snapshot.nodes[i].id : data->snapshot.links[i].id),
                        "Trace ID mismatch");
                auto &v = values[i];
                v.ratio = number(f.get(), "trace_ratio");
                v.time_s = number(f.get(), "time_s");
                v.time_coverage = number(f.get(), "time_coverage");
                v.from_time_s = number(f.get(), "from_time_s");
                v.to_time_s = number(f.get(), "to_time_s");
                v.terminal_fraction = number(f.get(), "terminal_fraction");
                v.terminal_kind = f->GetFieldAsInteger("terminal_kind");
                v.flags = f->GetFieldAsInteger("flags");
            }
            require(seen.size() == values.size(), "Incomplete saved trace");
        }
        return r;
    }
    catch (const std::exception &e)
    {
        if (error)
            *error = QString::fromUtf8(e.what());
        return {};
    }
}
bool AnalysisStore::saveStyle(const QString &path, const QString &id, const QJsonObject &style,
                              QString *error)
{
    try
    {
        auto d = open(path, true);
        require(bool(d), "Cannot open saved analysis for style update");
        auto *l = table(d.get(), "analyses");
        while (auto *raw = l->GetNextFeature())
        {
            Feature f(raw, OGRFeature::DestroyFeature);
            if (str(f.get(), "analysis_id") == id)
            {
                textField(f.get(), "style", json(style));
                require(l->SetFeature(f.get()) == OGRERR_NONE,
                        "Cannot save default analysis style");
                return true;
            }
        }
        require(false, "Analysis missing");
    }
    catch (const std::exception &e)
    {
        if (error)
            *error = QString::fromUtf8(e.what());
    }
    return false;
}
} // namespace openswmmvis::trace
