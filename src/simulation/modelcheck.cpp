// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      modelcheck.cpp
 * @brief     Check Model (review G-09): strict open + initialize, diagnostics and object links.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "simulation/modelcheck.h"

#include "simulation/twodbackendpolicy.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_tables.h>

namespace openswmmvis::simulation
{

namespace
{
using Severity = ModelCheckIssue::Severity;

/** Append the engine's accumulated errors (and its last error) and warnings, without repeats. */
void collect(SWMM_Engine engine, int failureCode, ModelCheckResult &result)
{
    QStringList seen;
    const auto add = [&](const QString &raw, Severity severity) {
        const QString text = raw.trimmed();
        if (text.isEmpty() || seen.contains(text))
            return;
        seen.append(text);
        result.issues.append(parseModelCheckMessage(text, severity));
    };
    for (int i = 0; i < swmm_get_error_count(engine); ++i)
        add(QString::fromUtf8(swmm_get_error_at(engine, i)), Severity::Error);
    if (failureCode != SWMM_OK) {
        add(QString::fromUtf8(swmm_get_last_error_msg(engine)), Severity::Error);
        const bool anyError = std::any_of(result.issues.cbegin(), result.issues.cend(),
                                          [](const ModelCheckIssue &issue) {
                                              return issue.severity == Severity::Error;
                                          });
        if (!anyError)
            add(QString::fromUtf8(swmm_error_message(failureCode)), Severity::Error);
    }
    for (int i = 0; i < swmm_get_warning_count(engine); ++i)
        add(QString::fromUtf8(swmm_get_warning_at(engine, i)), Severity::Warning);
    std::stable_sort(result.issues.begin(), result.issues.end(),
                     [](const ModelCheckIssue &a, const ModelCheckIssue &b) {
                         return a.severity == Severity::Error && b.severity != Severity::Error;
                     });
}

/** Type words, longest first so "Virtual Junction" wins over "Junction". */
struct TypeWord {
    const char *pattern;
    SWMMObjectRef::ObjectType type;
};
constexpr TypeWord kTypeWords[] = {
    {"virtual junction", SWMMObjectRef::Node},
    {"inlet junction", SWMMObjectRef::Node},
    {"storage node", SWMMObjectRef::Node},
    {"storage unit", SWMMObjectRef::Node},
    {"regulator link", SWMMObjectRef::Link},
    {"unit hydrograph", SWMMObjectRef::Unknown}, // read so its name is not taken as another type
    {"time series", SWMMObjectRef::TimeSeries},
    {"timeseries", SWMMObjectRef::TimeSeries},
    {"time pattern", SWMMObjectRef::TimePattern},
    {"subcatchment", SWMMObjectRef::Subcatchment},
    {"lid control", SWMMObjectRef::LIDControl},
    {"rain gage", SWMMObjectRef::RainGage},
    {"raingage", SWMMObjectRef::RainGage},
    {"snow pack", SWMMObjectRef::Snowpack},
    {"snowpack", SWMMObjectRef::Snowpack},
    {"land use", SWMMObjectRef::LandUse},
    {"landuse", SWMMObjectRef::LandUse},
    {"junction", SWMMObjectRef::Node},
    {"outfall", SWMMObjectRef::Node},
    {"divider", SWMMObjectRef::Node},
    {"storage", SWMMObjectRef::Node},
    {"node", SWMMObjectRef::Node},
    {"regulator", SWMMObjectRef::Link},
    {"conduit", SWMMObjectRef::Link},
    {"orifice", SWMMObjectRef::Link},
    {"outlet", SWMMObjectRef::Link},
    {"pump", SWMMObjectRef::Link},
    {"weir", SWMMObjectRef::Link},
    {"link", SWMMObjectRef::Link},
    {"gage", SWMMObjectRef::RainGage},
    {"aquifer", SWMMObjectRef::Aquifer},
    {"lid", SWMMObjectRef::LIDControl},
    {"transect", SWMMObjectRef::Transect},
    {"pollutant", SWMMObjectRef::Pollutant},
    {"pattern", SWMMObjectRef::TimePattern},
    {"curve", SWMMObjectRef::Curve},
    {"street", SWMMObjectRef::Street},
    {"inlet", SWMMObjectRef::Inlet},
};

const QRegularExpression &objectPattern()
{
    static const QRegularExpression pattern = [] {
        QStringList words;
        for (const auto &word : kTypeWords)
            words.append(QString::fromLatin1(word.pattern).replace(' ', QStringLiteral("\\s+")));
        return QRegularExpression(
            QStringLiteral("\\b(%1)\\s+(?:'([^']+)'|\"([^\"]+)\"|(\\S+))").arg(words.join('|')),
            QRegularExpression::CaseInsensitiveOption);
    }();
    return pattern;
}

SWMMObjectRef::ObjectType typeFor(const QString &word)
{
    const QString normal = word.simplified().toLower();
    for (const auto &entry : kTypeWords)
        if (normal == QLatin1String(entry.pattern))
            return entry.type;
    return SWMMObjectRef::Unknown;
}

/** Table index of a time series (@p series) or a curve with this exact name, or -1. */
int tableIndex(SWMM_Engine engine, const QByteArray &name, bool series)
{
    const int count = swmm_table_count(engine);
    for (int i = 0; i < count; ++i) {
        const char *id = swmm_table_id(engine, i);
        int type = -1;
        if (id && name == id && swmm_table_get_type(engine, i, &type) == SWMM_OK &&
            (type == 0) == series)
            return i;
    }
    return -1;
}

bool exists(SWMM_Engine engine, SWMMObjectRef::ObjectType type, const QString &name)
{
    const QByteArray id = name.toUtf8();
    switch (type) {
    case SWMMObjectRef::Node:
        return swmm_node_index(engine, id.constData()) >= 0;
    case SWMMObjectRef::Link:
        return swmm_link_index(engine, id.constData()) >= 0;
    case SWMMObjectRef::Subcatchment:
        return swmm_subcatch_index(engine, id.constData()) >= 0;
    case SWMMObjectRef::RainGage:
        return swmm_gage_index(engine, id.constData()) >= 0;
    case SWMMObjectRef::Aquifer:
        return swmm_aquifer_index(engine, id.constData()) >= 0;
    case SWMMObjectRef::Snowpack:
        return swmm_snowpack_index(engine, id.constData()) >= 0;
    case SWMMObjectRef::LIDControl:
        return swmm_lid_index(engine, id.constData()) >= 0;
    case SWMMObjectRef::Transect:
        return swmm_transect_index(engine, id.constData()) >= 0;
    case SWMMObjectRef::Pollutant:
        return swmm_pollutant_index(engine, id.constData()) >= 0;
    case SWMMObjectRef::LandUse:
        return swmm_landuse_index(engine, id.constData()) >= 0;
    case SWMMObjectRef::Street:
        return swmm_street_index(engine, id.constData()) >= 0;
    case SWMMObjectRef::Inlet:
        return swmm_inlet_index(engine, id.constData()) >= 0;
    case SWMMObjectRef::TimePattern:
        return swmm_pattern_index(engine, id.constData()) >= 0;
    case SWMMObjectRef::TimeSeries:
        return tableIndex(engine, id, true) >= 0;
    case SWMMObjectRef::Curve:
        return tableIndex(engine, id, false) >= 0;
    default:
        return false;
    }
}
} // namespace

ModelCheckResult checkModelFile(const QString &inpPath)
{
    ModelCheckResult result;
    struct EngineGuard {
        SWMM_Engine engine = swmm_engine_create();
        ~EngineGuard()
        {
            if (!engine)
                return;
            swmm_engine_close(engine);
            swmm_engine_destroy(engine);
        }
    } guard;
    if (!guard.engine) {
        result.issues.append({Severity::Error, 0,
                              QStringLiteral("The engine could not be created."),
                              SWMMObjectRef::Unknown, {}});
        return result;
    }
    const QFileInfo info(inpPath);
    const QString stem = info.dir().filePath(info.completeBaseName());
    const QByteArray inp = inpPath.toUtf8();
    const QByteArray rpt = (stem + QStringLiteral(".check.rpt")).toUtf8();
    const QByteArray out = (stem + QStringLiteral(".check.out")).toUtf8();
    int rc = swmm_engine_open(guard.engine, inp.constData(), rpt.constData(), out.constData(),
                              nullptr);
    if (rc != SWMM_OK) {
        collect(guard.engine, rc, result);
        return result;
    }
    result.opened = true;
    QString notice;
    rc = applyTwoDBackendPolicy(guard.engine, notice);
    if (rc != SWMM_OK) {
        result.issues.append({Severity::Error, 0, notice, SWMMObjectRef::Unknown, {}});
        collect(guard.engine, SWMM_OK, result);
        return result;
    }
    rc = swmm_engine_initialize(guard.engine);
    result.initialized = rc == SWMM_OK;
    collect(guard.engine, rc, result);
    return result;
}

ModelCheckIssue parseModelCheckMessage(const QString &message, ModelCheckIssue::Severity fallback)
{
    static const QRegularExpression prefix(QStringLiteral("^\\s*(ERROR|WARNING)\\s+(\\d+)\\s*:"));
    ModelCheckIssue issue;
    issue.message = message.trimmed();
    issue.severity = fallback;
    const auto match = prefix.match(issue.message);
    if (match.hasMatch()) {
        issue.severity =
            match.captured(1) == QLatin1String("ERROR") ? Severity::Error : Severity::Warning;
        issue.code = match.captured(2).toInt();
    }
    return issue;
}

QList<QPair<SWMMObjectRef::ObjectType, QString>> objectsNamedIn(const QString &message)
{
    QList<QPair<SWMMObjectRef::ObjectType, QString>> found;
    auto matches = objectPattern().globalMatch(message);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const auto type = typeFor(match.captured(1));
        QString name = match.captured(2);
        if (name.isEmpty())
            name = match.captured(3);
        if (name.isEmpty())
            name = match.captured(4);
        if (type != SWMMObjectRef::Unknown && !name.isEmpty())
            found.append({type, name});
    }
    return found;
}

void resolveModelCheckObjects(SWMM_Engine engine, QList<ModelCheckIssue> &issues)
{
    if (!engine)
        return;
    static const QString trailing = QStringLiteral(".,;:)");
    for (auto &issue : issues) {
        for (const auto &[type, name] : objectsNamedIn(issue.message)) {
            QString candidate = name;
            bool matched = exists(engine, type, candidate);
            while (!matched && candidate.size() > 1 && trailing.contains(candidate.back())) {
                candidate.chop(1);
                matched = exists(engine, type, candidate);
            }
            if (matched) {
                issue.objectType = type;
                issue.objectName = candidate;
                break;
            }
        }
    }
}

} // namespace openswmmvis::simulation
