// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      meshcorridorrecipe.h
 * @brief     Project settings for mesh corridor generation recipes.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include "mesh/corridorsource.h"
#include "project/projectserializer.h"
#include <QSet>
#include <cmath>
#include <initializer_list>
#include <limits>

// The recipe is GUI state. It is stored in the same staged .oswp as the rest
// of the project, independently of whether its GIS layers have opened yet.
class MeshCorridorRecipe
{
public:
    /**
     * @brief Validate a version-1/2 explicit-feature corridor recipe and resolve its referenced paths.
     * @param[in] value Recipe object; unknown keys/versions/roles are rejected.
     * @param[in] sidecar Project sidecar filename whose parent anchors relative references.
     * @param[out] sources Required destination, replaced only after every source validates.
     * @param[out] error Optional diagnostic, cleared on entry and prefixed with recipe/source context on failure.
     * @return False leaves sources unchanged. True accepts positive finite width, nonnegative finite along spacing and positive integral across count; bank pairs require version 2, two distinct feature IDs and no width field.
     * @note Feature IDs are canonical nonnegative 64-bit decimal strings; geometry digests, when set, are 64 lowercase hexadecimal characters. This validates stored metadata without opening GIS data.
     */
    static bool decode(const QJsonValue &value, const QString &sidecar,
                       QVector<mesh::CorridorSource> *sources, QString *error)
    {
        const auto fail = [&](const QString &message) {
            if (error)
                *error = QStringLiteral("Invalid mesh corridor recipe: %1").arg(message);
            return false;
        };
        if (error)
            error->clear();
        if (!sources)
            return fail(QStringLiteral("no destination supplied."));
        if (!value.isObject())
            return fail(QStringLiteral("expected an object."));
        const QJsonObject object = value.toObject();
        if (!onlyKeys(object, {"version", "sources"}))
            return fail(QStringLiteral("unsupported recipe fields."));
        const double version = object.value("version").toDouble(-1);
        if (!object.value("version").isDouble() || (version != 1 && version != 2))
            return fail(QStringLiteral("unsupported or missing version (expected 1 or 2)."));
        if (!object.value("sources").isArray())
            return fail(QStringLiteral("sources must be an array."));
        QVector<mesh::CorridorSource> parsed;
        const auto rows = object.value("sources").toArray();
        for (int index = 0; index < rows.size(); ++index) {
            const auto bad = [&](const QString &message) {
                return fail(QStringLiteral("source %1: %2").arg(index + 1).arg(message));
            };
            if (!rows[index].isObject())
                return bad(QStringLiteral("expected an object."));
            const auto row = rows[index].toObject();
            if (!onlyKeys(row, {"role", "selection", "path", "layerName", "sourceCRSWkt",
                                "meshCRSWkt", "featureIds", "widthField", "width", "along",
                                "across", "tag", "geometryDigest", "sourceFiles"}))
                return bad(QStringLiteral("unsupported source fields."));
            const QString role = row.value("role").toString();
            if (role != QStringLiteral("centerline") &&
                !(version == 2 && role == QStringLiteral("bankPair")))
                return bad(
                    QStringLiteral("unsupported source role; bankPair requires recipe version 2."));
            if (row.value("selection").toString() != QStringLiteral("features"))
                return bad(QStringLiteral("an explicit feature selection is required."));
            for (const auto &key : {"path", "layerName", "sourceCRSWkt", "meshCRSWkt", "widthField",
                                    "tag", "geometryDigest"})
                if (!row.value(QLatin1String(key)).isString())
                    return bad(QStringLiteral("%1 must be a string.").arg(QLatin1String(key)));
            const QString path = row.value("path").toString();
            const QString layerName = row.value("layerName").toString();
            if (path.trimmed().isEmpty() || path.contains(QChar(0)) ||
                layerName.trimmed().isEmpty() || layerName.contains(QChar(0)))
                return bad(
                    QStringLiteral("a nonempty datasource path and sublayer name are required."));
            mesh::CorridorSource source;
            source.bankPair = role == QStringLiteral("bankPair");
            source.path = ProjectSerializer::resolveStoredPath(path, sidecar);
            source.layerName = layerName;
            source.sourceCRSWkt = row.value("sourceCRSWkt").toString();
            source.meshCRSWkt = row.value("meshCRSWkt").toString();
            source.widthField = row.value("widthField").toString();
            if (source.bankPair && !source.widthField.isEmpty())
                return bad(QStringLiteral(
                    "bank pairs derive width from geometry and cannot specify widthField."));
            source.tag = row.value("tag").toString();
            source.geometryDigest = row.value("geometryDigest").toString();
            if (!source.geometryDigest.isEmpty()) {
                if (source.geometryDigest.size() != 64)
                    return bad(QStringLiteral("geometryDigest must be a SHA-256 hex string."));
                for (QChar c : source.geometryDigest)
                    if (!(c >= QLatin1Char('0') && c <= QLatin1Char('9')) &&
                        !(c >= QLatin1Char('a') && c <= QLatin1Char('f')))
                        return bad(QStringLiteral("geometryDigest must be a SHA-256 hex string."));
            }
            if (!row.value("featureIds").isArray() || row.value("featureIds").toArray().isEmpty())
                return bad(QStringLiteral(
                    "select at least one feature; an empty selection never means all features."));
            QSet<qint64> seen;
            for (const auto &idValue : row.value("featureIds").toArray()) {
                if (!idValue.isString())
                    return bad(QStringLiteral("feature IDs must be decimal strings."));
                bool ok = false;
                const auto text = idValue.toString();
                const qint64 id = text.toLongLong(&ok);
                if (!ok || id < 0 || QString::number(id) != text || seen.contains(id))
                    return bad(QStringLiteral(
                        "feature IDs must be unique nonnegative 64-bit decimal integers."));
                source.featureIds.append(id);
                seen.insert(id);
            }
            if (source.bankPair && source.featureIds.size() != 2)
                return bad(QStringLiteral(
                    "bank pairs require exactly two distinct selected feature IDs."));
            for (const auto &key : {"width", "along", "across"})
                if (!row.value(QLatin1String(key)).isDouble() ||
                    !std::isfinite(row.value(QLatin1String(key)).toDouble()))
                    return bad(
                        QStringLiteral("%1 must be a finite number.").arg(QLatin1String(key)));
            source.width = row.value("width").toDouble();
            source.along = row.value("along").toDouble();
            const double across = row.value("across").toDouble();
            if (!(source.width > 0) || source.along < 0 || across < 1 ||
                across > std::numeric_limits<int>::max() || std::floor(across) != across)
                return bad(QStringLiteral(
                    "width must be positive, along-spacing nonnegative and across-count a positive integer."));
            source.across = int(across);
            if (!row.value("sourceFiles").isArray())
                return bad(QStringLiteral("sourceFiles must be an array."));
            for (const auto &fileValue : row.value("sourceFiles").toArray()) {
                if (!fileValue.isString() || fileValue.toString().trimmed().isEmpty() ||
                    fileValue.toString().contains(QChar(0)))
                    return bad(QStringLiteral("sourceFiles must contain nonempty paths."));
                source.sourceFiles.append(
                    ProjectSerializer::resolveStoredPath(fileValue.toString(), sidecar));
            }
            parsed.append(std::move(source));
        }
        *sources = std::move(parsed); // errors never discard the previous recipe
        return true;
    }

    /**
     * @brief Serialize sources relative to a sidecar and validate the resulting recipe before returning it.
     * @param[in] sources Explicit selections and generation settings; dimensional quantities retain the CorridorSource coordinate/spacing conventions.
     * @param[in] sidecar Destination sidecar filename for relative path conversion.
     * @param[out] object Optional result, replaced only after validation; null permits validation only.
     * @param[out] error Optional validation diagnostic, cleared by decode().
     * @return True emits version 2 when any bank pair is present, otherwise version 1; false leaves object unchanged.
     */
    static bool encode(const QVector<mesh::CorridorSource> &sources, const QString &sidecar,
                       QJsonObject *object, QString *error)
    {
        QJsonArray rows;
        bool hasBanks = false;
        for (const auto &source : sources) {
            hasBanks |= source.bankPair;
            QJsonArray ids, files;
            for (qint64 id : source.featureIds)
                ids.append(QString::number(id));
            for (const auto &file : source.sourceFiles)
                files.append(ProjectSerializer::toRelativePath(file, sidecar));
            rows.append(
                QJsonObject{{"role", source.bankPair ? "bankPair" : "centerline"},
                            {"selection", "features"},
                            {"path", ProjectSerializer::toRelativePath(source.path, sidecar)},
                            {"layerName", source.layerName},
                            {"sourceCRSWkt", source.sourceCRSWkt},
                            {"meshCRSWkt", source.meshCRSWkt},
                            {"featureIds", ids},
                            {"widthField", source.widthField},
                            {"width", source.width},
                            {"along", source.along},
                            {"across", source.across},
                            {"tag", source.tag},
                            {"geometryDigest", source.geometryDigest},
                            {"sourceFiles", files}});
        }
        const QJsonObject result{{"version", hasBanks ? 2 : 1}, {"sources", rows}};
        QVector<mesh::CorridorSource> checked;
        if (!decode(result, sidecar, &checked, error))
            return false;
        if (object)
            *object = result;
        return true;
    }

private:
    /**
     * @brief Reject object fields outside the recipe's explicit supported-key set.
     * @param[in] object Object to inspect.
     * @param[in] allowed Case-sensitive allowed key literals.
     * @return True when every present key is known; required-key checks are separate.
     */
    static bool onlyKeys(const QJsonObject &object, std::initializer_list<const char *> allowed)
    {
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            bool known = false;
            for (const char *key : allowed)
                known |= it.key() == QLatin1String(key);
            if (!known)
                return false;
        }
        return true;
    }
};
