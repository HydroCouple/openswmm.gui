/*!
 * \file   rulelistio.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  RuleList file I/O implementation (Slice Z.17-files).
 */

#include "render/rulelistio.h"

#include "render/rule.h"
#include "render/rulelist.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>

namespace OpenSWMM::Render::RuleListIO {

namespace {

constexpr const char *kKeyFormat       = "format";
constexpr const char *kKeyVersion      = "version";
constexpr const char *kKeyRules        = "rules";
constexpr const char *kKeySymbolLevels = "symbolLevels";

/*! Walk the top-level envelope keys we know about; anything else
 *  produces a warning. Keeps forward-compat loose. */
void warnOnUnknownKeys(const QJsonObject &env, QStringList &warnings)
{
    static const QStringList known = {
        QString::fromLatin1(kKeyFormat),
        QString::fromLatin1(kKeyVersion),
        QString::fromLatin1(kKeyRules),
        QString::fromLatin1(kKeySymbolLevels),
    };
    for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
        if (!known.contains(it.key()))
            warnings.append(QStringLiteral("Unknown envelope key: %1").arg(it.key()));
    }
}

} // namespace

RuleListIoResult save(const RuleList *list, const QString &path)
{
    RuleListIoResult res;

    if (path.isEmpty()) {
        res.error = QStringLiteral("Save path is empty");
        return res;
    }

    QJsonObject env;
    env[QString::fromLatin1(kKeyFormat)]  = QString::fromLatin1(kFormat);
    env[QString::fromLatin1(kKeyVersion)] = kVersion;
    env[QString::fromLatin1(kKeyRules)]   = list ? list->toJson() : QJsonArray{};

    // Symbol-levels envelope hook is a per-Rule flag in the data model
    // (Z.11); a future Z.11a may surface a layer-scope "enabled" flag
    // here. For now write an empty object so the loader doesn't warn.
    env[QString::fromLatin1(kKeySymbolLevels)] = QJsonObject{};

    QJsonDocument doc(env);
    const QByteArray bytes = doc.toJson(QJsonDocument::Indented);

    // QSaveFile gives atomic replace — partial writes don't leave a
    // half-written file when the disk fills mid-save.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        res.error = QStringLiteral("Cannot open '%1' for write: %2")
                        .arg(path, file.errorString());
        return res;
    }
    const qint64 written = file.write(bytes);
    if (written != bytes.size()) {
        res.error = QStringLiteral("Short write to '%1' (%2 of %3 bytes)")
                        .arg(path).arg(written).arg(bytes.size());
        file.cancelWriting();
        return res;
    }
    if (!file.commit()) {
        res.error = QStringLiteral("Failed to commit save to '%1': %2")
                        .arg(path, file.errorString());
        return res;
    }
    res.ok = true;
    return res;
}

RuleListIoResult load(const QString &path, RuleList *list)
{
    RuleListIoResult res;

    if (!list) {
        res.error = QStringLiteral("Destination RuleList is null");
        return res;
    }
    QFileInfo fi(path);
    if (!fi.exists()) {
        res.error = QStringLiteral("File not found: %1").arg(path);
        return res;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        res.error = QStringLiteral("Cannot open '%1' for read: %2")
                        .arg(path, file.errorString());
        return res;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseErr);
    if (doc.isNull()) {
        res.error = QStringLiteral("Malformed JSON in '%1': %2 (offset %3)")
                        .arg(path,
                             parseErr.errorString())
                        .arg(parseErr.offset);
        return res;
    }
    if (!doc.isObject()) {
        res.error = QStringLiteral("Top-level value in '%1' is not a JSON object").arg(path);
        return res;
    }
    const QJsonObject env = doc.object();

    // Format / version checks are warnings, not failures — keeps the
    // loader permissive for hand-edited and legacy files.
    if (env.value(QString::fromLatin1(kKeyFormat)).toString()
        != QString::fromLatin1(kFormat)) {
        res.warnings.append(QStringLiteral(
            "Format key missing or unexpected (expected '%1')")
                .arg(QString::fromLatin1(kFormat)));
    }
    const int version = env.value(QString::fromLatin1(kKeyVersion)).toInt(0);
    if (version != kVersion) {
        res.warnings.append(QStringLiteral(
            "Version mismatch (expected %1, got %2) — proceeding optimistically")
                .arg(kVersion).arg(version));
    }
    warnOnUnknownKeys(env, res.warnings);

    if (!env.contains(QString::fromLatin1(kKeyRules))) {
        res.error = QStringLiteral("Envelope missing required 'rules' array");
        return res;
    }
    const QJsonValue rulesVal = env.value(QString::fromLatin1(kKeyRules));
    if (!rulesVal.isArray()) {
        res.error = QStringLiteral("'rules' field is not a JSON array");
        return res;
    }

    // Atomic semantics: load into a scratch list first; only commit
    // (clear destination + repopulate) on success. We can't easily
    // pre-build a sibling RuleList without exposing more API; instead
    // we walk the array and validate every entry's renderer id before
    // mutating the destination.
    const QJsonArray rules = rulesVal.toArray();

    QJsonArray accepted;
    for (const QJsonValue &v : rules) {
        if (!v.isObject()) {
            res.warnings.append(QStringLiteral("Skipping non-object entry in 'rules'"));
            ++res.rulesSkipped;
            continue;
        }
        // Probe by attempting a fromJson — Rule::fromJson returns null
        // for malformed entries and unknown renderer ids. Cheap on
        // failure (no allocations stick around) thanks to unique_ptr.
        auto probe = Rule::fromJson(v.toObject());
        if (!probe) {
            const QJsonObject inner = v.toObject().value(QStringLiteral("renderer")).toObject();
            const QString id = inner.value(QStringLiteral("id")).toString();
            res.warnings.append(QStringLiteral(
                "Skipping rule with unknown renderer id '%1'")
                    .arg(id.isEmpty() ? QStringLiteral("(missing)") : id));
            ++res.rulesSkipped;
            continue;
        }
        accepted.append(v);
        ++res.rulesLoaded;
    }

    // Commit: clear destination, then re-load the accepted array.
    list->clear();
    list->fromJson(accepted);

    res.ok = true;
    return res;
}

} // namespace OpenSWMM::Render::RuleListIO
