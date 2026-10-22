/**
 * @file      profilesectionio.cpp
 * @brief     Serialization of saved profile section definitions.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "plot/profilesection.h"
#include "plot/profilesectionseries.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>
#include <QSet>
#include <cmath>
#include <filesystem>
#include <limits>

namespace ProfileSection
{
namespace
{
/**
 * @brief Store a section persistence/export diagnostic.
 * @param[out] e Optional destination.
 * @param[in] m Diagnostic text.
 * @return Always false.
 */
bool fail(QString *e, const QString &m)
{
    if (e)
        *e = m;
    return false;
}
/**
 * @brief Extract a strictly typed JSON string.
 * @param[in] o Object to inspect.
 * @param[in] k Non-null Latin-1 key.
 * @param[out] v String assigned only when the field is a string.
 * @return True on a string field, including an empty string; false otherwise.
 */
bool stringField(const QJsonObject &o, const char *k, QString &v)
{
    const auto a = o.value(QLatin1String(k));
    if (!a.isString())
        return false;
    v = a.toString();
    return true;
}
/**
 * @brief Extract a finite JSON number without string coercion.
 * @param[in] o Object to inspect.
 * @param[in] k Non-null Latin-1 key.
 * @param[out] v Number assigned only on success.
 * @return False for missing, nonnumeric, or nonfinite values.
 */
bool numberField(const QJsonObject &o, const char *k, double &v)
{
    const auto a = o.value(QLatin1String(k));
    if (!a.isDouble() || !std::isfinite(a.toDouble()))
        return false;
    v = a.toDouble();
    return true;
}
/**
 * @brief Extract a strictly typed JSON boolean.
 * @param[in] o Object to inspect.
 * @param[in] k Non-null Latin-1 key.
 * @param[out] v Boolean assigned only on success.
 * @return True only for a present boolean field.
 */
bool boolField(const QJsonObject &o, const char *k, bool &v)
{
    const auto a = o.value(QLatin1String(k));
    if (!a.isBool())
        return false;
    v = a.toBool();
    return true;
}
/**
 * @brief Reject fields outside the supported saved-section schema.
 * @param[in] o Object whose keys are checked.
 * @param[in] allowed Complete allowed-key set for this level.
 * @return True when every present key is allowed; required fields are checked separately.
 */
bool keys(const QJsonObject &o, const QStringList &allowed)
{
    for (auto i = o.constBegin(); i != o.constEnd(); ++i)
        if (!allowed.contains(i.key()))
            return false;
    return true;
}
/**
 * @brief Escape one section CSV text field.
 * @param[in] s Text copied for double-quote escaping.
 * @return Double-quoted field with internal quotes doubled.
 */
QString csv(QString s)
{
    s.replace('"', "\"\"");
    return '"' + s + '"';
}
/**
 * @brief Format numeric section provenance without losing double precision.
 * @param[in] value Number in the field-specific units.
 * @return Round-trip decimal text, or empty for nonfinite values.
 */
QString num(double value)
{
    return std::isfinite(value)
               ? QString::number(value, 'g', std::numeric_limits<double>::max_digits10)
               : QString();
}
/**
 * @brief Test whether a proposed CSV destination resolves to a result source.
 * @param[in] a Source path; empty returns false.
 * @param[in] b Proposed output path.
 * @return True for filesystem equivalence or equal weakly canonical paths; false when resolution fails.
 */
bool alias(const QString &a, const QString &b)
{
    if (a.isEmpty())
        return false;
    auto native = [](const QString &p) {
#ifdef Q_OS_WIN
        return std::filesystem::path(p.toStdWString());
#else
        return std::filesystem::path(p.toUtf8().constData());
#endif
    };
    std::error_code e;
    if (std::filesystem::equivalent(native(a), native(b), e))
        return true;
    const auto aa = std::filesystem::weakly_canonical(native(a), e);
    if (e)
        return false;
    const auto bb = std::filesystem::weakly_canonical(native(b), e);
    return !e && aa == bb;
}
}
/**
 * @brief Serialize a saved section using version-one scene-coordinate metadata.
 * @param[in] d Definition with scene XY polyline and source/series display metadata.
 * @param[in] base Directory against which nonempty source paths are serialized relatively.
 * @return Owning JSON object; source paths and pen/style properties are copied, with no source IO or validation.
 */
QJsonObject definitionToJson(const Definition &d, const QString &base)
{
    QJsonObject o{{"version", 1},
                  {"id", d.id},
                  {"title", d.title},
                  {"sceneCRS", d.sceneCRS},
                  {"coordinateConvention", "scene_xy_y_negated"},
                  {"horizontalUnits", d.horizontalUnits},
                  {"elevationUnits", d.elevationUnits},
                  {"verticalDatum", d.verticalDatum},
                  {"primarySourceId", d.primarySourceId},
                  {"displayOptions", d.displayOptions}};
    QJsonArray path;
    for (const auto &p : d.scenePolyline)
        path.append(QJsonArray{p.x(), p.y()});
    o["path"] = path;
    QJsonArray sources;
    for (const auto &s : d.sources)
        sources.append(QJsonObject{
            {"id", s.id},
            {"path", s.path.isEmpty()
                         ? QString()
                         : QDir(base).relativeFilePath(QFileInfo(s.path).absoluteFilePath())},
            {"verticalDatum", s.verticalDatum}});
    o["sources"] = sources;
    QJsonArray series;
    for (const auto &s : d.series) {
        QJsonArray dash;
        for (qreal value : s.pen.dashPattern())
            dash.append(value);
        series.append(
            QJsonObject{{"id", s.id},
                        {"sourceId", s.sourceId},
                        {"variableKey", s.variableKey},
                        {"label", s.label},
                        {"role", s.role == SeriesRole::Elevation ? "elevation" : "scalar"},
                        {"timePolicy", s.timePolicy == TimePolicy::Exact ? "exact" : "hold"},
                        {"visible", s.visible},
                        {"color", s.pen.color().name(QColor::HexArgb)},
                        {"width", s.pen.widthF()},
                        {"lineStyle", int(s.pen.style())},
                        {"dashPattern", dash},
                        {"dashOffset", s.pen.dashOffset()},
                        {"capStyle", int(s.pen.capStyle())},
                        {"joinStyle", int(s.pen.joinStyle())},
                        {"opacity", s.opacity},
                        {"customRange", s.customRange},
                        {"minimum", s.minimum},
                        {"maximum", s.maximum}});
    }
    o["series"] = series;
    return o;
}
/**
 * @brief Decode and validate a strict saved-section schema without partially replacing the caller definition.
 * @param[in] o Version-one JSON object using scene_xy_y_negated coordinates.
 * @param[in] base Directory used to resolve nonempty relative source paths; no source existence check occurs here.
 * @param[out] out Validated definition replaced only on success.
 * @param[out] error Optional diagnostic cleared on entry and set on schema/definition failure.
 * @return False for unknown keys, invalid typed metadata, or definition validation failure. Limits are 100000 path points, 64 sources, 128 series, and 64 dash values.
 */
bool definitionFromJson(const QJsonObject &o, const QString &base, Definition &out, QString *error)
{
    if (error)
        error->clear();
    auto bad = [&] {
        return fail(
            error, QStringLiteral("The saved section is malformed or uses an unsupported schema."));
    };
    if (!keys(o, {"version", "id", "title", "sceneCRS", "coordinateConvention", "horizontalUnits",
                  "elevationUnits", "verticalDatum", "primarySourceId", "path", "sources", "series",
                  "displayOptions"}) ||
        !o.value("version").isDouble() || o.value("version").toDouble() != 1 ||
        o.value("coordinateConvention").toString() != "scene_xy_y_negated")
        return bad();
    Definition d;
    if (!o.value("displayOptions").isObject())
        return bad();
    d.displayOptions = o.value("displayOptions").toObject();
    if (!stringField(o, "id", d.id) || !stringField(o, "title", d.title) ||
        !stringField(o, "sceneCRS", d.sceneCRS) ||
        !stringField(o, "horizontalUnits", d.horizontalUnits) ||
        !stringField(o, "elevationUnits", d.elevationUnits) ||
        !stringField(o, "verticalDatum", d.verticalDatum) ||
        !stringField(o, "primarySourceId", d.primarySourceId) || !o.value("path").isArray() ||
        !o.value("sources").isArray() || !o.value("series").isArray())
        return bad();
    const auto path = o.value("path").toArray(), sources = o.value("sources").toArray(),
               series = o.value("series").toArray();
    if (path.size() > 100000 || sources.size() > 64 || series.size() > 128)
        return bad();
    for (const auto &p : path) {
        if (!p.isArray())
            return bad();
        const auto a = p.toArray();
        if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble() ||
            !std::isfinite(a[0].toDouble()) || !std::isfinite(a[1].toDouble()))
            return bad();
        d.scenePolyline.push_back({a[0].toDouble(), a[1].toDouble()});
    }
    for (const auto &v : sources) {
        if (!v.isObject())
            return bad();
        const auto s = v.toObject();
        SourceReference r;
        if (!keys(s, {"id", "path", "verticalDatum"}) || !stringField(s, "id", r.id) ||
            !stringField(s, "path", r.path) || !stringField(s, "verticalDatum", r.verticalDatum))
            return bad();
        if (!r.path.isEmpty())
            r.path = QDir::cleanPath(QDir(base).absoluteFilePath(r.path));
        d.sources.push_back(r);
    }
    for (const auto &v : series) {
        if (!v.isObject())
            return bad();
        const auto j = v.toObject();
        SeriesDefinition s;
        QString role, policy, color;
        double width, line, offset, cap, join;
        if (!keys(j, {"id", "sourceId", "variableKey", "label", "role", "timePolicy", "visible",
                      "color", "width", "lineStyle", "dashPattern", "dashOffset", "capStyle",
                      "joinStyle", "opacity", "customRange", "minimum", "maximum"}) ||
            !stringField(j, "id", s.id) || !stringField(j, "sourceId", s.sourceId) ||
            !stringField(j, "variableKey", s.variableKey) || !stringField(j, "label", s.label) ||
            !stringField(j, "role", role) || !stringField(j, "timePolicy", policy) ||
            !boolField(j, "visible", s.visible) || !stringField(j, "color", color) ||
            !numberField(j, "width", width) || !numberField(j, "lineStyle", line) ||
            !numberField(j, "dashOffset", offset) || !numberField(j, "capStyle", cap) ||
            !numberField(j, "joinStyle", join) || !numberField(j, "opacity", s.opacity) ||
            !boolField(j, "customRange", s.customRange) || !numberField(j, "minimum", s.minimum) ||
            !numberField(j, "maximum", s.maximum) || !j.value("dashPattern").isArray())
            return bad();
        if ((role != "elevation" && role != "scalar") || (policy != "exact" && policy != "hold") ||
            !QColor(color).isValid() || line != std::floor(line) || line < 1 ||
            line > int(Qt::CustomDashLine) ||
            (cap != Qt::FlatCap && cap != Qt::SquareCap && cap != Qt::RoundCap) ||
            (join != Qt::MiterJoin && join != Qt::BevelJoin && join != Qt::RoundJoin &&
             join != Qt::SvgMiterJoin))
            return bad();
        s.role = role == "elevation" ? SeriesRole::Elevation : SeriesRole::Scalar;
        s.timePolicy = policy == "exact" ? TimePolicy::Exact : TimePolicy::Hold;
        s.pen = QPen(QColor(color), width, Qt::PenStyle(int(line)), Qt::PenCapStyle(int(cap)),
                     Qt::PenJoinStyle(int(join)));
        QVector<qreal> dash;
        for (const auto &item : j.value("dashPattern").toArray()) {
            if (!item.isDouble() || !std::isfinite(item.toDouble()) || item.toDouble() <= 0 ||
                dash.size() >= 64)
                return bad();
            dash.push_back(item.toDouble());
        }
        if (line == int(Qt::CustomDashLine)) {
            if (dash.isEmpty() || dash.size() % 2)
                return bad();
            s.pen.setDashPattern(dash);
        }
        s.pen.setDashOffset(offset);
        d.series.push_back(s);
    }
    if (!validateDefinition(d, error))
        return false;
    out = std::move(d);
    return true;
}
/**
 * @brief Atomically export visible sampled and built-in section series with scientific provenance.
 * @param[in] section Sampled section with declared units and availability for all visible series.
 * @param[in] d Validated definition that must match sampled source/variable identities.
 * @param[in] path CSV destination distinct from every referenced result source, including filesystem aliases.
 * @param[out] error Optional diagnostic cleared on entry and set on validation or IO failure.
 * @return True after atomic commit; false preserves the prior destination.
 * @details Writes chainage in declared horizontal units, native series values with explicit status, and map Y as negative scene Y. Missing numeric values are blank. This synchronous operation does not access live engines.
 */
bool exportSectionCsv(const Section &section, const Definition &d, const QString &path,
                      QString *error)
{
    if (error)
        error->clear();
    if (!validateDefinition(d, error))
        return false;
    if (path.trimmed().isEmpty())
        return fail(error, QStringLiteral("Choose a section CSV destination."));
    auto protectedPath = [&] {
        for (const auto &s : d.sources)
            if (alias(s.path, path))
                return true;
        for (const auto &s : section.series)
            if (alias(s.sourcePath, path))
                return true;
        return false;
    };
    if (protectedPath())
        return fail(error, QStringLiteral("The section CSV destination aliases a result source."));
    QSet<QString> ids;
    for (const auto &s : section.series)
        if (s.definition.visible) {
            if (ids.contains(s.definition.id) || !s.error.isEmpty() || !s.unitsKnown ||
                s.units.trimmed().isEmpty())
                return fail(
                    error,
                    QStringLiteral(
                        "Every visible series must be available with declared units before scientific export."));
            ids.insert(s.definition.id);
            bool found = false;
            for (const auto &def : d.series)
                if (def.id == s.definition.id && def.sourceId == s.definition.sourceId &&
                    def.variableKey == s.definition.variableKey)
                    found = true;
            if (!found)
                return fail(
                    error,
                    QStringLiteral("The sampled section no longer matches its saved definition."));
            for (const auto &p : s.points)
                if (!std::isfinite(p.chainage) || !std::isfinite(p.scenePt.x()) ||
                    !std::isfinite(p.scenePt.y()) ||
                    (p.status != openswmmvis::io::Mesh2DValueStatus::Missing &&
                     !std::isfinite(p.value)))
                    return fail(error, QStringLiteral("A section station is invalid."));
        }
    for (const auto &def : d.series)
        if (def.visible && !ids.contains(def.id))
            return fail(error, QStringLiteral("A visible series has not been sampled."));
    auto allSeries = builtInSeries(section, d);
    allSeries += section.series;
    QSaveFile f(path);
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly))
        return fail(error, f.errorString());
    auto write = [&](const QStringList &fields) {
        const auto bytes = (fields.join(',') + '\n').toUtf8();
        return f.write(bytes) == bytes.size();
    };
    if (!write({"section_id",     "series_id",
                "source_id",      "source_path",
                "variable_key",   "quantity",
                "domain",         "zone",
                "species",        "role",
                "chainage",       "horizontal_units",
                "map_x",          "map_y",
                "scene_crs",      "vertical_datum",
                "requested_time", "effective_time",
                "temporal",       "time_policy",
                "frame",          "cell_index_0based",
                "break_before",   "value",
                "status",         "units"}))
        return fail(error, f.errorString());
    using S = openswmmvis::io::Mesh2DValueStatus;
    using V = openswmmvis::io::Mesh2DResultVariable;
    for (const auto &s : allSeries)
        if (s.definition.visible) {
            const auto &v = s.descriptor;
            const QString domain = v.domain == V::Domain::Groundwater ? "groundwater" : "surface";
            const QString zone = v.zone == V::Zone::Saturated     ? "saturated"
                                 : v.zone == V::Zone::Unsaturated ? "unsaturated"
                                 : v.zone == V::Zone::Sigma       ? "sigma"
                                                                  : "none";
            const QString temporal = v.temporal == V::Temporal::Static     ? "static"
                                     : v.temporal == V::Temporal::Envelope ? "envelope"
                                     : v.temporal == V::Temporal::Held     ? "held"
                                                                           : "reported";
            for (const auto &p : s.points) {
                const QString status = p.status == S::Valid       ? "valid"
                                       : p.status == S::Missing   ? "missing"
                                       : p.status == S::Waterless ? "waterless"
                                                                  : "not_applicable";
                if (!write({csv(d.id),
                            csv(s.definition.id),
                            csv(s.definition.sourceId),
                            csv(s.sourcePath),
                            csv(s.definition.variableKey),
                            csv(v.dataset),
                            domain,
                            zone,
                            csv(v.species),
                            s.definition.role == SeriesRole::Elevation ? "elevation" : "scalar",
                            num(p.chainage),
                            csv(d.horizontalUnits),
                            num(p.scenePt.x()),
                            num(-p.scenePt.y()),
                            csv(d.sceneCRS),
                            csv(d.verticalDatum),
                            csv(s.requestedTime.toString(Qt::ISODateWithMs)),
                            csv(s.effectiveTime.toString(Qt::ISODateWithMs)),
                            temporal,
                            s.definition.timePolicy == TimePolicy::Exact ? "exact" : "hold",
                            s.frame < 0 ? QString() : QString::number(s.frame),
                            p.cellId < 0 ? QString() : QString::number(p.cellId),
                            p.breakBefore ? "true" : "false",
                            p.status == S::Missing ? QString() : num(p.value),
                            status,
                            csv(s.units)}))
                    return fail(error, f.errorString());
            }
        }
    if (protectedPath())
        return fail(error, QStringLiteral("The CSV destination changed to alias a result source."));
    if (!f.commit())
        return fail(error, f.errorString());
    return true;
}
} // namespace ProfileSection
