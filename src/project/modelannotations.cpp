// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      modelannotations.cpp
 * @brief     The model's map labels, backdrop and saved profiles (review G-01).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "project/modelannotations.h"

#include <QHash>
#include <QLocale>

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_spatial.h>

#include <algorithm>

namespace openswmmvis::project
{

namespace
{
/** SWMM input tokens: whitespace separated, double quotes group, ';' starts a comment. */
QStringList tokens(const QString &line)
{
    QStringList out;
    QString current;
    bool quoted = false, inToken = false;
    for (const QChar c : line) {
        if (quoted) {
            if (c == QLatin1Char('"'))
                quoted = false;
            else
                current.append(c);
        } else if (c == QLatin1Char('"')) {
            quoted = inToken = true;
        } else if (c == QLatin1Char(';')) {
            break;
        } else if (c.isSpace()) {
            if (inToken)
                out.append(current);
            current.clear();
            inToken = false;
        } else {
            current.append(c);
            inToken = true;
        }
    }
    if (inToken)
        out.append(current);
    return out;
}

bool toNumber(const QString &text, double *value)
{
    bool ok = false;
    *value = QLocale::c().toDouble(text, &ok);
    return ok;
}

QString number(double value)
{
    return QLocale::c().toString(value, 'g', QLocale::FloatingPointShortest);
}

bool flag(const QString &text)
{
    return text == QLatin1String("1") || text.compare(QLatin1String("YES"), Qt::CaseInsensitive) == 0;
}

/** A quoted field; a double quote cannot be written inside one, so it becomes a single quote. */
QString quoted(const QString &text)
{
    return QLatin1Char('"') + QString(text).replace(QLatin1Char('"'), QLatin1Char('\'')) +
           QLatin1Char('"');
}
} // namespace

QList<ModelLabel> parseLabels(const QString &text, QStringList *warnings)
{
    QList<ModelLabel> labels;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QStringList t = tokens(line);
        if (t.isEmpty())
            continue;
        ModelLabel label;
        if (t.size() < 3 || !toNumber(t[0], &label.x) || !toNumber(t[1], &label.y)) {
            if (warnings)
                warnings->append(QStringLiteral("[LABELS] row not read: %1").arg(line.trimmed()));
            continue;
        }
        label.text = t[2];
        label.anchor = t.value(3);
        label.fontFamily = t.value(4);
        double size = 0.0;
        if (t.size() > 5 && toNumber(t[5], &size) && size > 0.0)
            label.fontSize = size;
        label.bold = t.size() > 6 && flag(t[6]);
        label.italic = t.size() > 7 && flag(t[7]);
        labels.append(label);
    }
    return labels;
}

QString labelsText(const QList<ModelLabel> &labels)
{
    if (labels.isEmpty())
        return {};
    QString out = QStringLiteral(";;X-Coord          Y-Coord            Label");
    for (const ModelLabel &label : labels) {
        out += QLatin1Char('\n') + number(label.x).leftJustified(18) + QLatin1Char(' ') +
               number(label.y).leftJustified(18) + QLatin1Char(' ') + quoted(label.text) +
               QLatin1Char(' ') + quoted(label.anchor) + QLatin1Char(' ') +
               quoted(label.fontFamily) + QLatin1Char(' ') + number(label.fontSize) +
               QLatin1Char(' ') + QLatin1Char(label.bold ? '1' : '0') + QLatin1Char(' ') +
               QLatin1Char(label.italic ? '1' : '0');
    }
    return out;
}

std::optional<ModelBackdrop> parseBackdrop(const QString &text)
{
    ModelBackdrop backdrop;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QStringList t = tokens(line);
        if (t.size() < 2)
            continue;
        const QString key = t[0].toUpper();
        if (key == QLatin1String("FILE")) {
            backdrop.file = t[1];
        } else if (key == QLatin1String("DIMENSIONS") && t.size() >= 5) {
            backdrop.hasDimensions = toNumber(t[1], &backdrop.x1) && toNumber(t[2], &backdrop.y1) &&
                                     toNumber(t[3], &backdrop.x2) && toNumber(t[4], &backdrop.y2);
        }
    }
    if (backdrop.file.isEmpty())
        return std::nullopt;
    return backdrop;
}

QList<ModelProfile> parseProfiles(const QString &text, QStringList *warnings)
{
    QList<ModelProfile> profiles;
    QHash<QString, int> byName;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QStringList t = tokens(line);
        if (t.isEmpty())
            continue;
        const QString name = t[0].trimmed();
        if (name.isEmpty() || t.size() < 2) {
            if (warnings)
                warnings->append(QStringLiteral("[PROFILES] row not read: %1").arg(line.trimmed()));
            continue;
        }
        auto it = byName.constFind(name);
        if (it == byName.cend()) {
            it = byName.insert(name, int(profiles.size()));
            profiles.append({name, {}});
        }
        profiles[*it].links.append(t.mid(1));
    }
    return profiles;
}

QString profilesText(const QList<ModelProfile> &profiles)
{
    if (profiles.isEmpty())
        return {};
    QString out = QStringLiteral(";;Name           Links");
    for (const ModelProfile &profile : profiles)
        for (qsizetype i = 0; i < profile.links.size(); i += 5)
            out += QLatin1Char('\n') + quoted(profile.name) + QLatin1Char(' ') +
                   profile.links.mid(i, 5).join(QLatin1Char(' '));
    return out;
}

QString readAnnotation(SWMM_Engine engine, const char *section)
{
    if (!engine)
        return {};
    int required = 0;
    if (swmm_spatial_get_annotation(engine, section, nullptr, 0, &required) != SWMM_OK ||
        required <= 1)
        return {};
    QByteArray buffer(required, '\0');
    if (swmm_spatial_get_annotation(engine, section, buffer.data(), required, nullptr) != SWMM_OK)
        return {};
    return QString::fromUtf8(buffer.constData());
}

bool writeAnnotation(SWMM_Engine engine, const char *section, const QString &text, QString *error)
{
    const QByteArray utf8 = text.toUtf8();
    const int rc = engine ? swmm_spatial_set_annotation(engine, section, utf8.constData())
                          : SWMM_ERR_BADHANDLE;
    if (rc != SWMM_OK && error)
        *error = QStringLiteral("The engine did not accept [%1] (code %2).")
                     .arg(QString::fromLatin1(section))
                     .arg(rc);
    return rc == SWMM_OK;
}

QList<ModelProfile> readProfiles(SWMM_Engine engine, QString *tag, QStringList *warnings)
{
    const QString plural = readAnnotation(engine, "PROFILES");
    const QString singular = readAnnotation(engine, "PROFILE");
    if (tag)
        *tag = plural.isEmpty() && !singular.isEmpty() ? QStringLiteral("PROFILE")
                                                       : QStringLiteral("PROFILES");
    return parseProfiles(plural + QLatin1Char('\n') + singular, warnings);
}

bool writeProfiles(SWMM_Engine engine, const QList<ModelProfile> &profiles, const QString &tag,
                   QString *error)
{
    const QByteArray primary = tag.toLatin1();
    const char *other = primary == "PROFILE" ? "PROFILES" : "PROFILE";
    if (!writeAnnotation(engine, primary.constData(), profilesText(profiles), error))
        return false;
    return readAnnotation(engine, other).isEmpty() || writeAnnotation(engine, other, {}, error);
}

bool saveProfile(SWMM_Engine engine, const ModelProfile &profile, QString *error)
{
    QString tag;
    QList<ModelProfile> profiles = readProfiles(engine, &tag);
    const auto it = std::find_if(profiles.begin(), profiles.end(), [&](const ModelProfile &p) {
        return p.name == profile.name;
    });
    if (it != profiles.end())
        *it = profile;
    else
        profiles.append(profile);
    return writeProfiles(engine, profiles, tag, error);
}

bool renameProfileLink(SWMM_Engine engine, const QString &from, const QString &to)
{
    QString tag;
    QList<ModelProfile> profiles = readProfiles(engine, &tag);
    bool changed = false;
    for (ModelProfile &profile : profiles)
        for (QString &link : profile.links)
            if (link == from) {
                link = to;
                changed = true;
            }
    return changed && writeProfiles(engine, profiles, tag);
}

bool profilePath(SWMM_Engine engine, const QStringList &links, QVector<int> *nodes,
                 QVector<int> *linkIndices, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (links.isEmpty())
        return fail(QStringLiteral("The profile has no links."));
    QVector<int> indices, from, to;
    for (const QString &name : links) {
        const int index = swmm_link_index(engine, name.toUtf8().constData());
        int a = -1, b = -1;
        if (index < 0 || swmm_link_get_from_node(engine, index, &a) != SWMM_OK ||
            swmm_link_get_to_node(engine, index, &b) != SWMM_OK)
            return fail(QStringLiteral("Link %1 is not in the model.").arg(name));
        indices.append(index);
        from.append(a);
        to.append(b);
    }
    // Start at the end of the first link that the second one does not share.
    int current = from[0];
    if (links.size() > 1 && (from[0] == from[1] || from[0] == to[1]))
        current = to[0];
    QVector<int> path{current};
    for (qsizetype i = 0; i < indices.size(); ++i) {
        if (from[i] == current)
            current = to[i];
        else if (to[i] == current)
            current = from[i];
        else
            return fail(QStringLiteral("Link %1 does not continue the profile from link %2.")
                            .arg(links[i], links[i - 1]));
        path.append(current);
    }
    if (nodes)
        *nodes = path;
    if (linkIndices)
        *linkIndices = indices;
    return true;
}

} // namespace openswmmvis::project
