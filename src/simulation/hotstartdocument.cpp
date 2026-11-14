// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      hotstartdocument.cpp
 * @brief     Hot-start inspector data (review G-09).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "simulation/hotstartdocument.h"

#include <QFile>
#include <QSaveFile>
#include <QScopeGuard>

#include <algorithm>
#include <cmath>

#include <openswmm/engine/openswmm_hotstart.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>

namespace openswmmvis::simulation
{

namespace
{
QString lastIoError()
{
    const char *text = swmm_hotstart_last_io_error();
    return text ? QString::fromUtf8(text).trimmed() : QString();
}

/** First bytes of a file: which kind of hot-start file it claims to be. */
enum class Stamp { Native, Legacy, Unknown, Unreadable };
Stamp stampOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Stamp::Unreadable;
    const QByteArray head = file.read(16);
    if (head.startsWith("OPENSWMM_HS_V1"))
        return Stamp::Native;
    if (head.startsWith("SWMM5-HOTSTART"))
        return Stamp::Legacy;
    return Stamp::Unknown;
}

/** Engine-owned hot-start handle, closed (and flushed when dirty) on scope exit. */
struct Handle {
    SWMM_HotStart hs = nullptr;
    ~Handle() { swmm_hotstart_close(hs); }
};
} // namespace

HotStartUnits HotStartUnits::forEngine(SWMM_Engine engine)
{
    HotStartUnits units;
    char buf[32] = {};
    if (!engine || swmm_options_get(engine, "FLOW_UNITS", buf, int(sizeof buf)) != SWMM_OK)
        return units;
    const QString flow = QString::fromLatin1(buf).trimmed().toUpper();
    static const QHash<QString, double> kFlow{{QStringLiteral("CFS"), 1.0},
                                              {QStringLiteral("GPM"), 448.831},
                                              {QStringLiteral("MGD"), 0.64631689},
                                              {QStringLiteral("CMS"), 0.02831685},
                                              {QStringLiteral("LPS"), 28.31685},
                                              {QStringLiteral("MLD"), 2.4465757}};
    if (!kFlow.contains(flow))
        return units;
    units.flow = kFlow.value(flow);
    units.flowLabel = flow;
    if (flow == QLatin1String("CMS") || flow == QLatin1String("LPS") ||
        flow == QLatin1String("MLD")) {
        units.length = 0.3048;
        units.volume = 0.02831685;
        units.lengthLabel = QStringLiteral("m");
        units.volumeLabel = QStringLiteral("m³");
    }
    return units;
}

bool HotStartDocument::isEditable() const
{
    return m_info.format == Format::Native &&
           !(m_info.contents & SWMM_HOTSTART_HAS_ENGINE_CHECKPOINT);
}

bool HotStartDocument::open(const QString &path, SWMM_Engine model, QString *error)
{
    const auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    Handle handle;
    const QByteArray file = path.toUtf8();
    Info info;
    switch (stampOf(path)) {
    case Stamp::Unreadable:
        return fail(QStringLiteral("The file cannot be opened."));
    case Stamp::Unknown:
        return fail(QStringLiteral("This is not a hot-start file."));
    case Stamp::Native:
        if (swmm_hotstart_open(file.constData(), &handle.hs) != SWMM_OK)
            return fail(QStringLiteral("The hot-start file cannot be read: %1").arg(lastIoError()));
        info.format = Format::Native;
        break;
    case Stamp::Legacy:
        if (!model)
            return fail(QStringLiteral(
                "A SWMM 5 hot-start file stores values in object order without names, so it can "
                "only be read against the model that wrote it. Open that model first."));
        if (swmm_hotstart_open_legacy(model, file.constData(), &handle.hs) != SWMM_OK)
            return fail(QStringLiteral("The SWMM 5 hot-start file cannot be read: %1")
                            .arg(lastIoError()));
        info.format = Format::Legacy;
        break;
    }
    SWMM_HotStart hs = handle.hs;
    swmm_hotstart_get_version(hs, &info.version);
    swmm_hotstart_get_contents(hs, &info.contents);
    swmm_hotstart_get_saved_at(hs, &info.savedAt);
    swmm_hotstart_get_start_date(hs, &info.startDate);
    swmm_hotstart_get_sim_time(hs, &info.elapsedSeconds);
    swmm_hotstart_get_end_date(hs, &info.endDate);
    char crs[2048] = {};
    if (swmm_hotstart_get_crs(hs, crs, int(sizeof crs)) == SWMM_OK)
        info.crs = QString::fromUtf8(crs);

    QStringList species;
    for (int s = 0; s < swmm_hotstart_species_count(hs); ++s) {
        char name[256] = {};
        swmm_hotstart_get_species(hs, s, name, int(sizeof name));
        species.append(QString::fromUtf8(name));
    }
    const auto speciesOf = [&](int (*get)(SWMM_HotStart, int, int, double *), int record) {
        QList<double> values;
        for (int s = 0; s < species.size(); ++s) {
            double value = std::nan("");
            get(hs, record, s, &value);
            values.append(value);
        }
        return values;
    };

    QList<NodeRecord> nodes;
    int matched[3] = {0, 0, 0};
    for (int i = 0; i < swmm_hotstart_node_count(hs); ++i) {
        NodeRecord rec;
        char id[512] = {};
        swmm_hotstart_get_node(hs, i, id, int(sizeof id), &rec.depth, &rec.head, &rec.volume,
                               &rec.age);
        rec.id = QString::fromUtf8(id);
        rec.species = speciesOf(swmm_hotstart_get_node_species, i);
        rec.inModel = model && swmm_node_index(model, id) >= 0;
        matched[0] += rec.inModel;
        nodes.append(rec);
    }
    QList<LinkRecord> links;
    for (int i = 0; i < swmm_hotstart_link_count(hs); ++i) {
        LinkRecord rec;
        char id[512] = {};
        swmm_hotstart_get_link(hs, i, id, int(sizeof id), &rec.flow, &rec.depth, &rec.volume,
                               &rec.age);
        rec.id = QString::fromUtf8(id);
        rec.species = speciesOf(swmm_hotstart_get_link_species, i);
        rec.inModel = model && swmm_link_index(model, id) >= 0;
        matched[1] += rec.inModel;
        links.append(rec);
    }
    QList<SubcatchRecord> subcatches;
    for (int i = 0; i < swmm_hotstart_subcatch_count(hs); ++i) {
        SubcatchRecord rec;
        char id[512] = {};
        swmm_hotstart_get_subcatch(hs, i, id, int(sizeof id), &rec.runoff, &rec.gwFlow,
                                   &rec.gwMoisture, &rec.gwLowerDepth);
        rec.id = QString::fromUtf8(id);
        rec.inModel = model && swmm_subcatch_index(model, id) >= 0;
        matched[2] += rec.inModel;
        subcatches.append(rec);
    }

    m_path = path;
    m_info = info;
    m_species = species;
    m_nodes = nodes;
    m_links = links;
    m_subcatches = subcatches;
    m_missing[0] = model ? std::max(0, swmm_node_count(model) - matched[0]) : 0;
    m_missing[1] = model ? std::max(0, swmm_link_count(model) - matched[1]) : 0;
    m_missing[2] = model ? std::max(0, swmm_subcatch_count(model) - matched[2]) : 0;
    return true;
}

bool HotStartDocument::save(const QHash<QString, double> &nodeDepth,
                            const QHash<QString, QPair<double, double>> &linkState,
                            SWMM_Engine model, QString *error)
{
    const auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (!isEditable())
        return fail(m_info.format == Format::Legacy
                        ? QStringLiteral("A SWMM 5 hot-start file is shown read-only.")
                        : QStringLiteral("A restart restores this file's engine checkpoint, so "
                                         "its per-object values cannot be edited."));
    for (auto it = nodeDepth.cbegin(); it != nodeDepth.cend(); ++it)
        if (!std::isfinite(it.value()) || it.value() < 0.0)
            return fail(QStringLiteral("Node %1: the depth must be a number that is not negative.")
                            .arg(it.key()));
    for (auto it = linkState.cbegin(); it != linkState.cend(); ++it)
        if (!std::isfinite(it->first) || !std::isfinite(it->second) || it->second < 0.0)
            return fail(QStringLiteral("Link %1: the flow must be a number and the depth a number "
                                       "that is not negative.")
                            .arg(it.key()));

    // Edit a copy beside the file, then replace the file in one step, so a refused edit or a
    // failed write leaves the original untouched.
    const QString staging = m_path + QStringLiteral(".inspector-edit");
    QFile::remove(staging);
    if (!QFile::copy(m_path, staging))
        return fail(QStringLiteral("The file cannot be copied for editing."));
    const auto cleanup = qScopeGuard([&] { QFile::remove(staging); });
    {
        Handle handle;
        if (swmm_hotstart_open(staging.toUtf8().constData(), &handle.hs) != SWMM_OK)
            return fail(QStringLiteral("The hot-start file cannot be read: %1").arg(lastIoError()));
        QString refused;
        for (auto it = nodeDepth.cbegin(); refused.isEmpty() && it != nodeDepth.cend(); ++it)
            if (swmm_hotstart_set_node_state(handle.hs, it.key().toUtf8().constData(),
                                             it.value()) != SWMM_OK)
                refused = QStringLiteral("Node %1 was refused.").arg(it.key());
        for (auto it = linkState.cbegin(); refused.isEmpty() && it != linkState.cend(); ++it)
            if (swmm_hotstart_set_link_state(handle.hs, it.key().toUtf8().constData(), it->first,
                                             it->second) != SWMM_OK)
                refused = QStringLiteral("Link %1 was refused.").arg(it.key());
        if (!refused.isEmpty())
            return fail(refused); // the staging copy is removed; the file is untouched
        const int closed = swmm_hotstart_close(handle.hs);
        handle.hs = nullptr;
        if (closed != SWMM_OK)
            return fail(QStringLiteral("The edits could not be written: %1").arg(lastIoError()));
    }
    QFile edited(staging);
    QSaveFile target(m_path);
    if (!edited.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly) ||
        target.write(edited.readAll()) < 0 || !target.commit())
        return fail(QStringLiteral("The file could not be replaced: %1").arg(target.errorString()));
    edited.close();
    QString reread;
    if (!open(m_path, model, &reread))
        return fail(QStringLiteral("The edits were written, but the file could not be read back: %1")
                        .arg(reread));
    return true;
}

} // namespace openswmmvis::simulation
