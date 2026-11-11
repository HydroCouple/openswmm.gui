// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Caleb Buahin. All rights reserved.
// Included inside dataobjectattributetablemodel.cpp's private namespace.
int nativeIndex(SWMM_Engine, DC::DataCategory, const QString &);
QString numberKey(double v)
{
    return QString::number(v, 'g', 17);
}
bool remainingCategory(DC::DataCategory c)
{
    return c == DC::DataCurves || c == DC::DataTimeSeries || c == DC::DataControls ||
           c == DC::DataHydrographs || c == DC::DataLandUses;
}
class HydrographIdentities final : public QObject
{
public:
    QHash<QString, QPointer<QObject>> objects;
    explicit HydrographIdentities(SWMMModelLayer *layer) : QObject(layer)
    {
        setObjectName("bulkHydrographIdentities");
        connect(layer, &DC::hydrographChanged, this, [this](const QString &name) {
            if (!name.isEmpty())
                return;
            for (auto p : objects)
                if (p)
                    p->deleteLater();
            objects.clear(); // Structural changes invalidate old scopes before deferred deletion.
        });
    }
    QList<QObject *> providers(SWMMModelLayer *layer)
    {
        QList<QObject *> out;
        for (int i = 0; i < swmm_hydrograph_group_count(layer->engine()); ++i) {
            char buf[4096]{};
            if (swmm_hydrograph_group_id(layer->engine(), i, buf, sizeof(buf)) != SWMM_OK)
                continue;
            const auto name = QString::fromUtf8(buf), key = name.toLower();
            if (!objects.value(key)) {
                auto *p = new QObject(this);
                p->setProperty("name", name);
                objects[key] = p;
            }
            out << objects[key];
        }
        return out;
    }
};
HydrographIdentities *hydrographIdentities(SWMMModelLayer *layer)
{
    for (auto *child : layer->QObject::children())
        if (auto *r = dynamic_cast<HydrographIdentities *>(child))
            return r;
    return new HydrographIdentities(layer);
}
QString controlBlock(SWMM_Engine e, int index)
{
    QByteArray bytes(8192, 0);
    for (;;) {
        if (swmm_control_get_rule(e, index, bytes.data(), bytes.size()) != SWMM_OK)
            return {};
        if (qstrlen(bytes.constData()) < bytes.size() - 1)
            return QString::fromUtf8(bytes.constData());
        bytes.resize(bytes.size() * 2);
    }
}
QStringList controlProgram(SWMM_Engine e)
{
    QStringList out;
    for (int i = 0; i < swmm_control_count(e); ++i)
        out << controlBlock(e, i);
    return out;
}
bool writeProgram(SWMM_Engine e, const QStringList &program, bool publish, QString *error = nullptr)
{
    QVector<QByteArray> bytes;
    QVector<const char *> ptrs;
    for (const auto &s : program)
        bytes << s.toUtf8();
    for (const auto &s : bytes)
        ptrs << s.constData();
    char message[8192]{};
    const bool ok = swmm_control_replace_program(e, ptrs.constData(), ptrs.size(), publish ? 1 : 0,
                                                 message, sizeof(message)) == SWMM_OK;
    if (!ok && error)
        *error = QString::fromUtf8(message);
    return ok;
}
struct SurfaceConstituent {
    QString key, name, units;
    int index;
    bool wall;
    QObject *provider;
};
QList<SurfaceConstituent> surfaceConstituents(SWMMModelLayer *layer)
{
    QList<SurfaceConstituent> out;
    auto e = layer->engine();
    for (auto *p : lidPollutants(layer)) {
        int units = 0;
        const int i = swmm_pollutant_index(e, p->name().toUtf8());
        swmm_pollutant_get_units(e, i, &units);
        out << SurfaceConstituent{
            "p/" + lidPollToken(p), p->name(), QString::number(units), i, false, p};
    }
    const int np = swmm_pollutant_count(e);
    for (int i = 0; i < swmm_reaction_species_count(e); ++i) {
        char n[4096]{}, u[4096]{};
        int wall = 0;
        double a = 0, b = 0;
        if (swmm_reaction_species_get(e, i, n, sizeof(n), &wall, u, sizeof(u), &a, &b) != SWMM_OK)
            continue;
        out << SurfaceConstituent{"s/" + QString::fromLatin1(QByteArray(n).toPercentEncoding()),
                                  QString::fromUtf8(n),
                                  QString::fromUtf8(u),
                                  np + i,
                                  wall != 0,
                                  nullptr};
    }
    return out;
}
QVariantMap remainingRecord(SWMMModelLayer *layer, DC::DataCategory cat, const QString &name)
{
    if (cat == DC::DataTimeSeries) {
        auto *series = qobject_cast<openswmmvis::timeseries::TimeseriesRegistry *>(
            layer->ensureTimeseriesRegistry());
        if (series && !series->flushPendingEdit(series->findByName(name)))
            return {};
    }
    const auto e = layer->engine();
    const int idx = nativeIndex(e, cat, name);
    if (idx < 0)
        return {};
    QVariantMap r;
    if (cat == DC::DataCurves || cat == DC::DataTimeSeries) {
        int type = 0, n = 0;
        swmm_table_get_type(e, idx, &type);
        swmm_table_get_point_count(e, idx, &n);
        r["type"] = type;
        for (int i = 0; i < n; ++i) {
            double x = 0, y = 0;
            if (swmm_table_get_point(e, idx, i, &x, &y) != SWMM_OK)
                return {};
            r["point/" + numberKey(x)] = y;
        }
        if (cat == DC::DataTimeSeries) {
            int nr = 0;
            double anchor = 0;
            swmm_timeseries_get_relative_info(e, idx, &nr, &anchor);
            r["relativeCount"] = nr;
            r["anchor"] = anchor;
            char abs[32768]{}, token[32768]{};
            swmm_file_path_get(e, SWMM_FILE_TIMESERIES_DATA, name.toUtf8(), abs, sizeof(abs), token,
                               sizeof(token));
            r["fileToken"] = QString::fromUtf8(token);
            if (token[0]) {
                std::string path, col;
                openswmmvis::ui::extcol::splitPathColumn(abs[0] ? abs : token, path, col);
                auto *reg = qobject_cast<openswmmvis::timeseries::TimeseriesRegistry *>(
                    layer->ensureTimeseriesRegistry());
                QFileInfo info(
                    QDir(reg->projectAnchor()).absoluteFilePath(QString::fromStdString(path)));
                r["_fileStamp"] = QVariantList{info.canonicalFilePath(), info.size(),
                                               info.lastModified().toMSecsSinceEpoch()};
            } else
                r["_fileStamp"] = QVariantList{};
        }
    } else if (cat == DC::DataControls)
        r["body"] = controlBlock(e, idx);
    else if (cat == DC::DataHydrographs) {
        r["gage"] = QString();
        const auto id = name.toUtf8();
        for (int i = 0; i < swmm_hydrograph_gage_count(e); ++i) {
            char n[4096]{}, g[4096]{};
            if (swmm_hydrograph_get_gage(e, i, n, sizeof(n), g, sizeof(g)) == SWMM_OK &&
                name.compare(QString::fromUtf8(n), Qt::CaseInsensitive) == 0)
                r["gage"] = QString::fromUtf8(g);
        }
        for (int i = 0; i < swmm_hydrograph_count(e); ++i) {
            char n[4096]{};
            int m = 0, response = 0;
            double v[6]{};
            if (swmm_hydrograph_get(e, i, n, sizeof(n), &m, &response, &v[0], &v[1], &v[2], &v[3],
                                    &v[4], &v[5]) != SWMM_OK ||
                name.compare(QString::fromUtf8(n), Qt::CaseInsensitive) != 0)
                continue;
            const auto prefix = QString("rtk/%1/%2/").arg(m).arg(response);
            if (r.contains(prefix + "present"))
                return {};
            r[prefix + "present"] = 1;
            for (int j = 0; j < 6; ++j)
                r[prefix + QString::number(j)] = v[j];
        }
        for (int i = 0; i < swmm_rdii_decay_count(e); ++i) {
            char n[4096]{};
            int response = 0, on = 0;
            double v[8]{};
            if (swmm_rdii_decay_get(e, i, n, sizeof(n), &response, &v[0], &v[1], &v[2], &v[3],
                                    &v[4], &v[5], &on, &v[6], &v[7]) != SWMM_OK ||
                name.compare(QString::fromUtf8(n), Qt::CaseInsensitive) != 0)
                continue;
            const auto prefix = QString("decay/%1/").arg(response);
            if (r.contains(prefix + "present"))
                return {};
            r[prefix + "present"] = 1;
            for (int j = 0; j < 8; ++j)
                r[prefix + QString::number(j)] = v[j];
            r[prefix + "snow"] = on;
        }
    } else if (cat == DC::DataLandUses) {
        double interval = 0, removal = 0;
        swmm_landuse_get_sweep_interval(e, idx, &interval);
        swmm_landuse_get_sweep_removal(e, idx, &removal);
        r["sweepInterval"] = interval;
        r["sweepRemoval"] = removal;
        QVariantList catalog;
        for (const auto &c : surfaceConstituents(layer)) {
            catalog << QVariant(
                QVariantList{c.key, c.units, c.wall, QVariant::fromValue<QObject *>(c.provider)});
            int type = 0, norm = 0;
            double v[4]{};
            swmm_buildup_get(e, idx, c.index, &type, &v[0], &v[1], &v[2], &norm);
            auto prefix = "buildup/" + c.key + "/";
            r[prefix + "type"] = type;
            r[prefix + "normalizer"] = norm;
            r[prefix + "series"] = type == 4 && v[2] >= 0 && v[2] < swmm_table_count(e)
                                       ? QString::fromUtf8(swmm_table_id(e, int(v[2])))
                                       : QString();
            for (int j = 0; j < 3; ++j)
                r[prefix + QString::number(j)] = type == 4 && j == 2 ? 0. : v[j];
            type = 0;
            std::fill(std::begin(v), std::end(v), 0.);
            swmm_washoff_get(e, idx, c.index, &type, &v[0], &v[1], &v[2], &v[3]);
            prefix = "washoff/" + c.key + "/";
            r[prefix + "type"] = type;
            for (int j = 0; j < 4; ++j)
                r[prefix + QString::number(j)] = v[j];
        }
        r["_catalog"] = catalog;
    }
    return r;
}
QVector<double> tableXs(const QVariantMap &r)
{
    QVector<double> xs;
    for (auto it = r.cbegin(); it != r.cend(); ++it)
        if (it.key().startsWith("point/"))
            xs << it.key().mid(6).toDouble();
    std::sort(xs.begin(), xs.end());
    return xs;
}
bool tablePayload(SWMMModelLayer *layer, DC::DataCategory cat, const QString &name,
                  const QVariantMap &r, bool publish)
{
    const auto xs = tableXs(r);
    QVector<double> ys;
    for (double x : xs)
        ys << r["point/" + numberKey(x)].toDouble();
    const int idx = nativeIndex(layer->engine(), cat, name),
              nr = cat == DC::DataTimeSeries ? r["relativeCount"].toInt() : 0;
    const double anchor = cat == DC::DataTimeSeries ? r["anchor"].toDouble() : 0.;
    if (!publish)
        return swmm_table_validate_points(layer->engine(), idx, xs.constData(), ys.constData(),
                                          xs.size(), nr, anchor) == SWMM_OK;
    const auto token = r["fileToken"].toString().toUtf8();
    return swmm_table_replace_points(
               layer->engine(), idx, xs.constData(), ys.constData(), xs.size(), nr, anchor,
               cat == DC::DataTimeSeries ? token.constData() : nullptr) == SWMM_OK;
}
bool remainingValid(SWMMModelLayer *layer, DC::DataCategory cat, const QString &name,
                    const QVariantMap &r)
{
    if (cat == DC::DataCurves || cat == DC::DataTimeSeries)
        return tablePayload(layer, cat, name, r, false);
    if (cat == DC::DataControls) {
        auto program = controlProgram(layer->engine());
        const int idx = nativeIndex(layer->engine(), cat, name);
        if (idx < 0 || idx >= program.size())
            return false;
        program[idx] = r["body"].toString();
        return writeProgram(layer->engine(), program, false);
    }
    for (auto it = r.cbegin(); it != r.cend(); ++it) {
        if (it.key().startsWith("_") || it.key() == "gage" || it.key().endsWith("/series"))
            continue;
        bool ok = false;
        const double v = it.value().toDouble(&ok);
        if (!ok || !std::isfinite(v))
            return false;
        if (cat == DC::DataHydrographs) {
            const auto k = it.key();
            const int member = k.section('/', -1).toInt();
            if (k.startsWith("rtk/") && !k.endsWith("present") && (v < 0 || (member == 0 && v > 1)))
                return false;
            if (k.startsWith("rtk/") && k.endsWith("/5") &&
                v > r[k.left(k.lastIndexOf('/') + 1) + "3"].toDouble())
                return false;
            if (k.startsWith("decay/") && !k.endsWith("present") && !k.endsWith("snow") &&
                (member < 3 || member == 7) && v < 0)
                return false;
        } else if (cat == DC::DataLandUses) {
            if (v < 0 || (it.key() == "sweepRemoval" && v > 1))
                return false;
            if (it.key().startsWith("washoff/") &&
                (it.key().endsWith("/2") || it.key().endsWith("/3")) && v > 100)
                return false;
            if (it.key().endsWith("/series") && !it.value().toString().isEmpty() &&
                nativeIndex(layer->engine(), DC::DataTimeSeries, it.value().toString()) < 0)
                return false;
        }
    }
    if (cat == DC::DataHydrographs) {
        bool present = !r["gage"].toString().isEmpty();
        for (const auto &key : r.keys())
            if (key.endsWith("/present") && r[key].toInt())
                present = true;
        if (!present)
            return false; // Removal of the last row belongs to explicit group deletion.
    }
    if (cat == DC::DataHydrographs && !r["gage"].toString().isEmpty() &&
        swmm_gage_index(layer->engine(), r["gage"].toString().toUtf8()) < 0)
        return false;
    if (cat == DC::DataLandUses)
        for (const auto &c : surfaceConstituents(layer)) {
            const auto prefix = "buildup/" + c.key + "/";
            if (r[prefix + "type"].toInt() == 4 && nativeIndex(layer->engine(), DC::DataTimeSeries,
                                                               r[prefix + "series"].toString()) < 0)
                return false;
        }
    return true;
}
bool writeRemaining(SWMMModelLayer *layer, DC::DataCategory cat, const QString &name,
                    const QVariantMap &r)
{
    const auto before = remainingRecord(layer, cat, name);
    auto e = layer->engine();
    const auto id = name.toUtf8();
    const int idx = nativeIndex(e, cat, name);
    if (cat == DC::DataCurves || cat == DC::DataTimeSeries)
        return tablePayload(layer, cat, name, r, true);
    if (cat == DC::DataControls) {
        auto program = controlProgram(e);
        if (idx < 0 || idx >= program.size())
            return false;
        program[idx] = r["body"].toString();
        return writeProgram(e, program, true);
    }
    if (cat == DC::DataHydrographs) {
        if (r["gage"] != before["gage"] &&
            swmm_hydrograph_set_gage(e, id, r["gage"].toString().toUtf8()) != SWMM_OK)
            return false;
        for (int month = -1; month < 12; ++month)
            for (int response = 0; response < 3; ++response) {
                const auto p = QString("rtk/%1/%2/").arg(month).arg(response);
                bool changed = false;
                for (const auto &key : r.keys() + before.keys())
                    if (key.startsWith(p) && r.value(key) != before.value(key))
                        changed = true;
                if (!changed)
                    continue;
                if (!r[p + "present"].toInt()) {
                    if (swmm_hydrograph_remove_entry(e, id, month, response) != SWMM_OK)
                        return false;
                    continue;
                }
                if (swmm_hydrograph_set_rtk(e, id, month, response, r[p + "0"].toDouble(),
                                            r[p + "1"].toDouble(),
                                            r[p + "2"].toDouble()) != SWMM_OK ||
                    swmm_hydrograph_set_ia(e, id, month, response, r[p + "3"].toDouble(),
                                           r[p + "4"].toDouble(), r[p + "5"].toDouble()) != SWMM_OK)
                    return false;
            }
        for (int response = 0; response < 3; ++response) {
            const auto p = QString("decay/%1/").arg(response);
            bool changed = false;
            for (const auto &key : r.keys() + before.keys())
                if (key.startsWith(p) && r.value(key) != before.value(key))
                    changed = true;
            if (!changed)
                continue;
            if (!r[p + "present"].toInt()) {
                if (swmm_rdii_decay_remove(e, id, response) != SWMM_OK)
                    return false;
                continue;
            }
            if (swmm_rdii_decay_set(e, id, response, r[p + "0"].toDouble(), r[p + "1"].toDouble(),
                                    r[p + "2"].toDouble(), r[p + "3"].toDouble(),
                                    r[p + "4"].toDouble(), r[p + "5"].toDouble(),
                                    r[p + "snow"].toInt(), r[p + "6"].toDouble(),
                                    r[p + "7"].toDouble()) != SWMM_OK)
                return false;
        }
        return true;
    }
    if (cat == DC::DataLandUses) {
        if (swmm_landuse_set_sweep_interval(e, idx, r["sweepInterval"].toDouble()) != SWMM_OK ||
            swmm_landuse_set_sweep_removal(e, idx, r["sweepRemoval"].toDouble()) != SWMM_OK)
            return false;
        for (const auto &c : surfaceConstituents(layer))
            for (const auto &group : {QString("buildup/"), QString("washoff/")}) {
                const auto p = group + c.key + "/";
                bool changed = false;
                for (const auto &key : r.keys())
                    if (key.startsWith(p) && r[key] != before[key])
                        changed = true;
                if (!changed)
                    continue;
                int rc;
                if (group == "buildup/")
                    rc = swmm_buildup_set(
                        e, idx, c.index, r[p + "type"].toInt(), r[p + "0"].toDouble(),
                        r[p + "1"].toDouble(),
                        r[p + "type"].toInt() == 4
                            ? nativeIndex(e, DC::DataTimeSeries, r[p + "series"].toString())
                            : r[p + "2"].toDouble(),
                        r[p + "normalizer"].toInt());
                else
                    rc = swmm_washoff_set(e, idx, c.index, r[p + "type"].toInt(),
                                          r[p + "0"].toDouble(), r[p + "1"].toDouble(),
                                          r[p + "2"].toDouble(), r[p + "3"].toDouble());
                if (rc != SWMM_OK)
                    return false;
            }
        return true;
    }
    return false;
}
void refreshRemaining(SWMMModelLayer *layer, DC::DataCategory cat, QObject *p, const QString &name)
{
    const auto r = remainingRecord(layer, cat, name);
    if (r.isEmpty() || !p)
        return;
    if (cat == DC::DataCurves) {
        auto *v = static_cast<openswmmvis::curve::CurveProvider *>(p);
        QVector<openswmmvis::curve::CurvePoint> points;
        for (double x : tableXs(r))
            points << openswmmvis::curve::CurvePoint{x, r["point/" + numberKey(x)].toDouble()};
        v->setAllPoints(points);
    }
    if (cat == DC::DataTimeSeries) {
        auto *v = static_cast<openswmmvis::timeseries::TimeseriesProvider *>(p);
        QVector<openswmmvis::timeseries::TimeseriesPoint> points;
        for (double x : tableXs(r))
            points << openswmmvis::timeseries::TimeseriesPoint{
                openswmmvis::core::swmmDateTimeToQDateTime(x),
                r["point/" + numberKey(x)].toDouble()};
        v->setAllPoints(points);
        v->setRelativeInfo(r["relativeCount"].toInt(),
                           openswmmvis::core::swmmDateTimeToQDateTime(r["anchor"].toDouble()));
        char absolute[32768]{}, original[32768]{};
        swmm_file_path_get(layer->engine(), SWMM_FILE_TIMESERIES_DATA, name.toUtf8(), absolute,
                           sizeof(absolute), original, sizeof(original));
        std::string path, column;
        openswmmvis::ui::extcol::splitPathColumn(
            absolute[0] ? std::string(absolute) : r["fileToken"].toString().toStdString(), path,
            column);
        v->setSourceMode(
            path.empty() ? openswmmvis::timeseries::TimeseriesProvider::SourceMode::Inline
                         : openswmmvis::timeseries::TimeseriesProvider::SourceMode::ExternalFile);
        v->setFileSource(QString::fromStdString(path), QString::fromStdString(column), {});
    }
    if (cat == DC::DataControls)
        static_cast<openswmmvis::controls::ControlRuleProvider *>(p)->setBody(r["body"].toString());
    if (cat == DC::DataLandUses) {
        auto *v = static_cast<LandUseProvider *>(p);
        v->setSweepInterval(r["sweepInterval"].toDouble());
        v->setSweepRemoval(r["sweepRemoval"].toDouble());
    }
    p->setProperty("bulkAccepted", r);
}
QObject *hydroRegistry(SWMMModelLayer *layer)
{
    return hydrographIdentities(layer);
}
QList<QObject *> hydroProviders(QObject *reg)
{
    auto *r = dynamic_cast<HydrographIdentities *>(reg);
    auto *layer = r ? qobject_cast<SWMMModelLayer *>(r->parent()) : nullptr;
    return layer ? r->providers(layer) : QList<QObject *>{};
}
Field remainingField(const QString &key, const QString &label,
                     EditorKind editor = EditorKind::Numeric, double minimum = 0,
                     double maximum = kInf)
{
    Field f;
    f.spec.key = key;
    f.spec.label = label;
    f.spec.editor = editor;
    f.spec.setter = key;
    f.spec.minValue = minimum;
    f.spec.maxValue = maximum;
    f.spec.decimals = 9;
    f.get = [key](QObject *p) { return p->property("bulkAccepted").toMap().value(key); };
    f.set = [](QObject *, const QVariant &) {};
    return f;
}
QList<Field> remainingFields(SWMMModelLayer *layer, DC::DataCategory cat)
{
    QList<Field> f;
    if (cat == DC::DataCurves) {
        auto type = remainingField("type", tr("Curve type"), EditorKind::Enum);
        QStringList labels;
        QVariantList values;
        for (int i = 1; i <= 11; ++i) {
            labels << openswmmvis::curve::CurveProvider::typeLabel(
                static_cast<openswmmvis::curve::CurveType>(i));
            values << i;
        }
        type.spec.enumValues = pairs(labels, values);
        type.set = {};
        type.spec.tooltip = tr(
            "Changing curve axes reinterprets assigned data; keep the individual curve type owner.");
        f << type;
    }
    if (cat == DC::DataTimeSeries) {
        auto file = remainingField("fileToken", tr("External file and column"), EditorKind::Text);
        file.spec.tooltip = tr(
            "path:column binding; validates and loads the complete selected file column. Empty detaches the accepted samples to inline.");
        f << file;
        auto time = remainingField("timeMode", tr("Time mode"), EditorKind::Enum);
        time.spec.enumValues =
            pairs({tr("Absolute"), tr("Relative"), tr("Mixed (retained)")}, {0, 1, 2});
        time.get = [](QObject *p) -> QVariant {
            auto r = p->property("bulkAccepted").toMap();
            auto n = tableXs(r).size();
            int nr = r["relativeCount"].toInt();
            return nr == 0 ? 0 : nr == n ? 1 : 2;
        };
        f << time;
    }
    if (cat == DC::DataHydrographs) {
        QStringList names{tr("(none)")};
        QVariantList values{QString()};
        if (layer)
            for (int i = 0; i < layer->categoryCount(DC::CatRainGages); ++i) {
                auto n = layer->objectNameAt(DC::CatRainGages, i);
                names << n;
                values << n;
            }
        auto g = remainingField("gage", tr("Rain gage"), EditorKind::Enum);
        g.spec.enumValues = pairs(names, values);
        f << g;
    }
    if (cat == DC::DataCurves || cat == DC::DataTimeSeries) {
        auto n = remainingField("pointCount", tr("Sample count"), EditorKind::ReadOnly);
        n.set = {};
        n.get = [](QObject *p) { return tableXs(p->property("bulkAccepted").toMap()).size(); };
        f << n;
    }
    auto rows = remainingField("records", tr("Detailed properties"), EditorKind::Compound);
    rows.get = [layer, cat](QObject *p) -> QVariant {
        DataRecordsRef ref;
        ref.layer = layer;
        ref.engine = layer ? layer->engine() : nullptr;
        ref.category = int(cat);
        ref.providers << p;
        ref.names << nameOf(p, cat);
        return QVariant::fromValue(ref);
    };
    rows.spec.tooltip = tr(
        "Edit explicit keys across the captured selection. Values commit immediately; other rows are preserved. Close dismisses and Undo reverses a committed field.");
    f << rows;
    return f;
}
QString recordPrefix(const QString &key)
{
    return key.left(key.lastIndexOf('/') + 1);
}
QStringList remainingOwnedKeys(DC::DataCategory cat, const QString &key, const QVariantMap &before,
                               const QVariantMap &after)
{
    QStringList out;
    if ((cat == DC::DataTimeSeries && (key == "fileToken" || key == "timeMode")) ||
        cat == DC::DataControls) {
        out = before.keys();
        for (const auto &k : after.keys())
            if (!out.contains(k))
                out << k;
        return out;
    }
    if (key == "_pointAdd" || key == "_pointRemove") {
        for (const auto &k : before.keys() + after.keys())
            if (before.value(k) != after.value(k) && !out.contains(k))
                out << k;
        return out;
    }
    if (key.endsWith("/series") || key.endsWith("/present") ||
        ((key.startsWith("buildup/") || key.startsWith("washoff/")) && key.endsWith("/type"))) {
        const auto prefix = recordPrefix(key);
        for (const auto &k : before.keys() + after.keys())
            if (k.startsWith(prefix) && !out.contains(k))
                out << k;
        return out;
    }
    return {key};
}
int readBoundSeries(const QString &path, const QString &column,
                    QVector<openswmmvis::ui::ExternalSeriesPoint> &points, QString *error)
{
    int count = openswmmvis::ui::readColumn(path, column, points, nullptr, error);
    if (count > 0)
        return count;
    if (!column.isEmpty())
        return -1;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1;
    QTextStream stream(&file);
    QDate date;
    points.clear();
    while (!stream.atEnd()) {
        auto line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(';') || line.startsWith('#'))
            continue;
        auto words = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (words.size() == 3) {
            date = QDate::fromString(words.takeFirst(), "MM/dd/yyyy");
            if (!date.isValid())
                date = QDate::fromString(line.section(' ', 0, 0), "M/d/yyyy");
        }
        if (words.size() != 2 || !date.isValid()) {
            if (error)
                *error = QObject::tr("A headerless source must contain dated time/value rows.");
            return -1;
        }
        auto time = QTime::fromString(words[0], "H:mm:ss");
        if (!time.isValid())
            time = QTime::fromString(words[0], "H:mm");
        bool ok = false;
        double value = words[1].toDouble(&ok);
        if (!time.isValid() || !ok || !std::isfinite(value))
            return -1;
        points << openswmmvis::ui::ExternalSeriesPoint{
            openswmmvis::core::qDateTimeToSwmmDateTime(QDateTime(date, time, QTimeZone::UTC)),
            value};
    }
    if (error)
        error->clear();
    return points.size();
}
bool proposeRemaining(SWMMModelLayer *layer, DC::DataCategory cat, const QString &name,
                      const QString &key, const QVariant &value, const QVariantMap &before,
                      QVariantMap &after, QString *error)
{
    auto fail = [&](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };
    after = before;
    if (key == "records" || key == "type" || key == "pointCount")
        return fail(tr("This field has a dedicated owner."));
    if (cat == DC::DataCurves || cat == DC::DataTimeSeries) {
        if (key == "fileToken") {
            const auto token = value.toString();
            if (token.contains('\n') || token.contains('\r') || token.contains(QChar::Null) ||
                token.contains('"'))
                return fail(tr("Invalid file binding."));
            if (token == before[key].toString())
                return true; // Leaving the binding unchanged does not reload its source.
            after[key] = token;
            if (token.isEmpty()) {
                after["_fileStamp"] = QVariantList{};
                if (!before["fileToken"].toString().isEmpty() && tableXs(before).isEmpty()) {
                    std::string path, column;
                    openswmmvis::ui::extcol::splitPathColumn(
                        before["fileToken"].toString().toStdString(), path, column);
                    auto *reg = qobject_cast<openswmmvis::timeseries::TimeseriesRegistry *>(
                        layer->ensureTimeseriesRegistry());
                    QVector<openswmmvis::ui::ExternalSeriesPoint> points;
                    QString reason;
                    if (readBoundSeries(QDir(reg->projectAnchor())
                                            .absoluteFilePath(QString::fromStdString(path)),
                                        QString::fromStdString(column), points, &reason) < 1)
                        return fail(reason);
                    for (const auto &p : points)
                        after["point/" + numberKey(p.timeJulian)] = p.value;
                }
            }
            if (!token.isEmpty()) {
                std::string path, column;
                openswmmvis::ui::extcol::splitPathColumn(token.toStdString(), path, column);
                auto *reg = qobject_cast<openswmmvis::timeseries::TimeseriesRegistry *>(
                    layer->ensureTimeseriesRegistry());
                const auto absolute =
                    QDir(reg->projectAnchor()).absoluteFilePath(QString::fromStdString(path));
                QFileInfo info(absolute);
                after["_fileStamp"] = QVariantList{info.canonicalFilePath(), info.size(),
                                                   info.lastModified().toMSecsSinceEpoch()};
                QVector<openswmmvis::ui::ExternalSeriesPoint> points;
                QString reason;
                if (readBoundSeries(absolute, QString::fromStdString(column), points, &reason) < 1)
                    return fail(reason.isEmpty()
                                    ? tr("No usable samples in the selected file column.")
                                    : reason);
                for (const auto &k : tableXs(before))
                    after.remove("point/" + numberKey(k));
                for (const auto &p : points) {
                    const auto k = "point/" + numberKey(p.timeJulian);
                    if (after.contains(k))
                        return fail(tr("Duplicate timestamps cannot be authored by this editor."));
                    after[k] = p.value;
                }
                after["relativeCount"] = 0;
                after["anchor"] = 0.;
            }
        } else if (key == "timeMode") {
            bool validMode = false;
            const int mode = value.toInt(&validMode);
            const int count = tableXs(before).size(), relative = before["relativeCount"].toInt();
            const int currentMode = relative == 0 ? 0 : relative == count ? 1 : 2;
            if (!validMode || mode < 0 || mode > 2)
                return fail(
                    tr("Choose Absolute or Relative; Mixed is retained from authored data."));
            if (mode == currentMode)
                return true;
            if (!before["fileToken"].toString().isEmpty())
                return fail(tr("Detach the series to inline before changing time mode."));
            if (mode == 2)
                return fail(
                    tr("Choose Absolute or Relative; Mixed is retained from authored data."));
            after["relativeCount"] = mode ? tableXs(before).size() : 0;
            if (!mode)
                after["anchor"] = 0.;
            if (mode && before["anchor"].toDouble() == 0) {
                char date[64]{};
                swmm_options_get(layer->engine(), "START_DATE", date, sizeof(date));
                const auto d = QDate::fromString(QString::fromUtf8(date), "MM/dd/yyyy");
                if (!d.isValid())
                    return fail(tr("A valid simulation start date is required."));
                char start[64]{};
                swmm_options_get(layer->engine(), "START_TIME", start, sizeof(start));
                auto t = QTime::fromString(QString::fromUtf8(start), "H:mm:ss");
                if (!t.isValid())
                    t = QTime(0, 0);
                after["anchor"] =
                    openswmmvis::core::qDateTimeToSwmmDateTime(QDateTime(d, t, QTimeZone::UTC));
            }
            if (mode)
                for (double x : tableXs(before))
                    if (x < after["anchor"].toDouble())
                        return fail(tr("Relative samples cannot precede the simulation start."));
        } else {
            if (cat == DC::DataTimeSeries && !before["fileToken"].toString().isEmpty())
                return fail(tr("File samples are source-owned. Detach to inline to edit them."));
            if (key == "_pointAdd" || key == "_pointRemove") {
                const auto point = value.toList();
                bool ok = false;
                const double x = point.value(0).toDouble(&ok);
                if (!ok || !std::isfinite(x))
                    return fail(tr("Choose a finite X/timestamp."));
                const auto k = "point/" + numberKey(x);
                const int n = tableXs(before).size(), nr = before["relativeCount"].toInt();
                if (cat == DC::DataTimeSeries && nr > 0 && nr < n)
                    return fail(tr(
                        "Choose a complete time mode before inserting/removing mixed relative/absolute rows."));
                if (key == "_pointAdd") {
                    bool yes = false;
                    const double y = point.value(1).toDouble(&yes);
                    if (!yes || !std::isfinite(y) || before.contains(k))
                        return fail(tr("The point already exists or its value is invalid."));
                    after[k] = y;
                } else {
                    if (!before.contains(k))
                        return fail(tr("The keyed point is missing from a selected object."));
                    if (n <= 1)
                        return fail(tr(
                            "Keep at least one sample. Delete the definition through its individual owner instead."));
                    after.remove(k);
                }
                if (cat == DC::DataTimeSeries && nr == n && nr > 0)
                    after["relativeCount"] = tableXs(after).size();
            } else if (key.startsWith("point/") && before.contains(key)) {
                bool ok = false;
                const double y = value.toDouble(&ok);
                if (!ok || !std::isfinite(y))
                    return fail(tr("Enter a finite sample value."));
                after[key] = y;
            } else
                return fail(tr(
                    "The selected X/timestamp is missing. Use the explicit Add action to create it."));
        }
    } else if (cat == DC::DataControls) {
        if (key != "body")
            return false;
        QString text = value.toString();
        if (text.contains(QChar::Null))
            return fail(tr("Null characters are not permitted."));
        const QRegularExpression header("(?im)^\\s*RULE\\s+\\S+[^\\n]*(?:\\n|$)");
        auto m = header.match(text);
        if (m.hasMatch())
            text = text.mid(m.capturedEnd());
        if (header.match(text).hasMatch())
            return fail(tr("A per-rule edit cannot introduce additional RULE identities."));
        const auto old = before["body"].toString();
        const auto oldHeader = header.match(old);
        if (value.toString() == old ||
            (oldHeader.hasMatch() && text == old.mid(oldHeader.capturedEnd())))
            return true;
        const auto preamble =
            oldHeader.hasMatch() ? old.left(oldHeader.capturedStart()) : QString();
        after["body"] = preamble + "RULE " + name + "\n" + text;
    } else if (cat == DC::DataHydrographs || cat == DC::DataLandUses) {
        if (key == "gage")
            after[key] = value.toString();
        else if (key.startsWith("rtk/") || key.startsWith("decay/") || key.startsWith("buildup/") ||
                 key.startsWith("washoff/")) {
            const auto p = recordPrefix(key);
            if (cat == DC::DataLandUses) {
                bool found = false;
                for (const auto &c : surfaceConstituents(layer))
                    if (p == "buildup/" + c.key + "/" || p == "washoff/" + c.key + "/") {
                        if (c.wall || (c.provider == nullptr &&
                                       swmm_pollutant_index(layer->engine(), c.name.toUtf8()) >= 0))
                            return fail(
                                tr("This constituent has no unambiguous writable surface row."));
                        found = true;
                    }
                if (!found || !before.contains(key))
                    return fail(tr("The constituent definition changed."));
                if (key.endsWith("/type")) {
                    const int type = value.toInt(), maximum = key.startsWith("buildup/") ? 4 : 3;
                    if (value.toDouble() != type || type < 0 || type > maximum)
                        return fail(tr("Choose a valid function type."));
                    if (type != before[key].toInt()) {
                        for (const auto &k : after.keys())
                            if (k.startsWith(p) && !(type && k.endsWith("normalizer")))
                                after[k] =
                                    k.endsWith("series") ? QVariant(QString()) : QVariant(0.);
                    }
                    after[key] = type;
                    if (type == 4 && after[p + "series"].toString().isEmpty())
                        return fail(tr(
                            "Assign an external series first; selecting it enters External buildup."));
                } else if (key.endsWith("/series")) {
                    const int index =
                        nativeIndex(layer->engine(), DC::DataTimeSeries, value.toString());
                    if (index < 0)
                        return fail(tr("Choose an existing model series."));
                    after[p + "type"] = 4;
                    after[key] = QString::fromUtf8(swmm_table_id(layer->engine(), index));
                    after[p + "2"] = 0.;
                } else {
                    if (before[p + "type"].toInt() == 0)
                        return fail(
                            tr("Choose an active function before editing its coefficients."));
                    after[key] = value;
                }
            } else {
                auto pieces = key.split('/');
                bool ok = false;
                int response = pieces.value(key.startsWith("rtk/") ? 2 : 1).toInt(&ok);
                if (!ok || response < 0 || response > 2)
                    return false;
                if (key.startsWith("rtk/")) {
                    int month = pieces.value(1).toInt(&ok);
                    if (!ok || month < -1 || month > 11)
                        return false;
                }
                const bool present = before[p + "present"].toInt() != 0;
                if (key.endsWith("/present")) {
                    const int on = value.toInt();
                    if (value.toDouble() != on || on < 0 || on > 1)
                        return false;
                    if (!on) {
                        for (const auto &k : after.keys())
                            if (k.startsWith(p))
                                after.remove(k);
                    } else if (!present) {
                        after[p + "present"] = 1;
                        const int n = key.startsWith("rtk/") ? 6 : 8;
                        for (int i = 0; i < n; ++i)
                            after[p + QString::number(i)] =
                                key.startsWith("rtk/") && i == 2 ? 1. : 0.;
                        if (key.startsWith("decay/")) {
                            after[p + "3"] = 10.;
                            after[p + "snow"] = 0;
                        }
                    }
                } else {
                    if (!present || !before.contains(key))
                        return fail(tr(
                            "Enable this keyed row first; missing rows are not silently created."));
                    after[key] = value;
                }
            }
        } else if (cat == DC::DataLandUses && (key == "sweepInterval" || key == "sweepRemoval"))
            after[key] = value;
        else
            return false;
        for (auto it = after.begin(); it != after.end(); ++it)
            if (it.key() == key && !key.endsWith("/series") && key != "gage") {
                bool ok = false;
                double n = value.toDouble(&ok);
                if (!ok || !std::isfinite(n))
                    return fail(tr("Enter a finite value."));
                if (key.endsWith("/normalizer") && (n != std::floor(n) || n < 0 || n > 1))
                    return false;
                it.value() = before.value(key).metaType().id() == QMetaType::Int
                                 ? QVariant(value.toInt())
                                 : QVariant(n);
            }
    } else
        return false;
    return remainingValid(layer, cat, name, after) ||
           fail(tr(
               "The complete proposed record is invalid. Check ranges, source bindings and required references."));
}
