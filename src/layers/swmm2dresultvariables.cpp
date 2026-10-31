/**
 * @file      swmm2dresultvariables.cpp
 * @brief     Discovery and selection of available 2D result variables.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmm2dresultslayer.h"
#include "io/mesh2dderivedvariables.h"
#include "render/sublayers/resultscalarsublayer.h"
#include <QJsonArray>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <random>

using openswmmvis::io::Mesh2DResultVariable;
using openswmmvis::io::Mesh2DScalarFrame;
using openswmmvis::io::Mesh2DValueStatus;
using OpenSWMM::Render::ResultScalarSublayer;

QVector<Mesh2DResultVariable> HDF5Mesh2DSource::faceVariables(QStringList *warnings) const
{
    auto variables = reader_->faceVariables(warnings);
    variables += openswmmvis::io::derivedGroundwaterVariables(variables, warnings);
    return variables;
}
bool HDF5Mesh2DSource::readFaceVariableAt(const Mesh2DResultVariable &v, int t,
                                          std::vector<float> &values,
                                          std::vector<Mesh2DValueStatus> &status)
{
    if (v.derived)
        return openswmmvis::io::readDerivedVariableAt(
            v, t,
            [this](const Mesh2DResultVariable &input, int time, std::vector<float> &out,
                   std::vector<Mesh2DValueStatus> &flags) {
                return reader_->readFaceVariableAt(input, time, out, flags);
            },
            values, status);
    return reader_->readFaceVariableAt(v, t, values, status);
}

QVector<Mesh2DResultVariable> SWMM2DResultsLayer::resultVariables(QStringList *warnings) const
{
    if (!source_) {
        if (warnings)
            warnings->clear();
        return {};
    }
    const int generation = source_->resultGeneration(), times = source_->timeCount();
    if (m_resultCatalogRevision != source_revision_ || m_resultCatalogGeneration != generation ||
        m_resultCatalogTimes != times) {
        m_resultCatalog = source_->faceVariables(&m_resultCatalogWarnings);
        m_resultCatalogRevision = source_revision_;
        m_resultCatalogGeneration = generation;
        m_resultCatalogTimes = times;
    }
    if (warnings)
        *warnings = m_resultCatalogWarnings;
    return m_resultCatalog;
}

namespace
{
constexpr qsizetype sampleLimit = 32768;
// Deterministic reservoir preserves distribution over the full run, without
// retaining every frame. Exact extrema are independent of sampling.
/**
 * @param[in] range Mutable statistics with reservoir retained between frames.
 * @param[in] values Cell values in descriptor units.
 * @param[in] status Parallel same-length validity statuses; only finite Valid values accepted.
 * @param[in] seen Mutable cumulative accepted count; initializes extrema when zero.
 * @param[in] rng Mutable deterministic generator state shared across scan.
 * @brief Extend exact scalar extrema and uniform reservoir of at most 32768 valid finite samples;
 *   consumes RNG only after reservoir fills.
 */
void accumulate(Mesh2DScalarFrame &range, const std::vector<float> &values,
                const std::vector<Mesh2DValueStatus> &status, quint64 &seen, std::mt19937_64 &rng)
{
    for (size_t i = 0; i < values.size(); ++i) {
        if (status[i] != Mesh2DValueStatus::Valid || !std::isfinite(values[i]))
            continue;
        const double v = values[i];
        if (!seen)
            range.minimum = range.maximum = v;
        else {
            range.minimum = std::min(range.minimum, v);
            range.maximum = std::max(range.maximum, v);
        }
        ++seen;
        if (range.samples.size() < sampleLimit)
            range.samples.push_back(v);
        else {
            const quint64 j = std::uniform_int_distribution<quint64>(0, seen - 1)(rng);
            if (j < quint64(sampleLimit))
                range.samples[qsizetype(j)] = v;
        }
    }
}
}

std::shared_ptr<const Mesh2DScalarFrame>
SWMM2DResultsLayer::resultFrame(const QString &key, int time, bool wholeRun) const
{
    auto frame = std::make_shared<Mesh2DScalarFrame>();
    frame->time = time;
    if (!source_) {
        frame->error = tr("No result source is available.");
        return frame;
    }
    const int generation = source_->resultGeneration(), times = source_->timeCount();
    if (m_resultCacheRevision != source_revision_ || m_resultCacheGeneration != generation ||
        m_resultCacheTimes != times) {
        m_resultFrames.clear();
        m_resultRanges.clear();
        m_resultCacheBytes = 0;
        m_resultCacheRevision = source_revision_;
        m_resultCacheGeneration = generation;
        m_resultCacheTimes = times;
    }
    const QString cacheKey =
        key + QChar(0x1f) + QString::number(time) + (wholeRun ? ":run" : ":frame");
    if (auto it = m_resultFrames.constFind(cacheKey); it != m_resultFrames.cend())
        return it.value();
    bool found = false;
    for (const auto &v : resultVariables())
        if (v.key() == key) {
            frame->descriptor = v;
            found = true;
            break;
        }
    if (!found) {
        frame->error = tr("Saved result variable is unavailable: %1").arg(key);
        return frame;
    }
    auto read = [&](int t, std::vector<float> &v, std::vector<Mesh2DValueStatus> &s) {
        return source_->readFaceVariableAt(frame->descriptor, t, v, s) &&
               v.size() == size_t(source_->triangleCount()) && s.size() == v.size();
    };
    if (!read(time, frame->values, frame->status)) {
        frame->values.clear();
        frame->status.clear();
        frame->error = tr("The selected variable has no valid frame at this time.");
        return frame;
    }
    const bool scan = wholeRun && frame->descriptor.frameCount > 0;
    if (scan && !source_->supportsWholeRunRange()) {
        frame->error = tr(
            "Whole-run range requires a complete output history. Live or retained snapshots support current frame or an explicit range.");
        return frame;
    }
    std::shared_ptr<const Mesh2DScalarFrame> range;
    if (scan)
        range = m_resultRanges.value(key);
    if (!range) {
        auto stats = std::make_shared<Mesh2DScalarFrame>();
        quint64 seen = 0;
        std::mt19937_64 rng(0);
        if (scan) {
            std::vector<float> values;
            std::vector<Mesh2DValueStatus> status;
            for (int t = 0; t < frame->descriptor.frameCount; ++t) {
                if (!read(t, values, status)) {
                    stats->error =
                        tr("Whole-run range could not read frame %1. Choose current frame or an explicit range.")
                            .arg(t + 1);
                    break;
                }
                accumulate(*stats, values, status, seen, rng);
            }
        } else
            accumulate(*stats, frame->values, frame->status, seen, rng);
        range = stats;
        if (scan) {
            if (m_resultRanges.size() >= 16)
                m_resultRanges.clear();
            m_resultRanges.insert(key, stats);
        }
    }
    frame->minimum = range->minimum;
    frame->maximum = range->maximum;
    frame->samples = range->samples;
    frame->error = range->error;
    const size_t bytes = frame->values.size() * (sizeof(float) + sizeof(Mesh2DValueStatus)) +
                         size_t(frame->samples.size()) * sizeof(double);
    constexpr size_t maxBytes = 64 * 1024 * 1024;
    if (m_resultFrames.size() >= 8 || m_resultCacheBytes + bytes > maxBytes) {
        m_resultFrames.clear();
        m_resultCacheBytes = 0;
    }
    if (bytes <= maxBytes) {
        m_resultFrames.insert(cacheKey, frame);
        m_resultCacheBytes += bytes;
    }
    return frame;
}

std::shared_ptr<const OpenSWMM::Render::ResultScalarGeometry>
SWMM2DResultsLayer::resultScalarGeometry(const std::shared_ptr<const Mesh2DScalarFrame> &frame,
                                         const std::vector<double> &levels, bool labels) const
{
    for (const auto &entry : m_scalarGeometryCache)
        if (entry.frame == frame && entry.levels == levels && entry.labels == labels &&
            entry.geometry == geomRevision())
            return entry.output;
    auto output = std::make_shared<OpenSWMM::Render::ResultScalarGeometry>();
    if (!frame)
        output->error = tr("The scalar frame is unavailable.");
    else
        *output = OpenSWMM::Render::buildResultScalarGeometry(m_sceneVerts, cellVertexIndices(),
                                                              triVertexIndices(), triCellMap(),
                                                              *frame, levels, labels);
    size_t bytes = output->bytes();
    for (const auto &entry : m_scalarGeometryCache)
        bytes += entry.output->bytes();
    if (m_scalarGeometryCache.size() >= 4 || bytes > 64 * 1024 * 1024)
        m_scalarGeometryCache.clear();
    if (output->bytes() <= 64 * 1024 * 1024)
        m_scalarGeometryCache.append({frame, levels, labels, geomRevision(), output});
    return output;
}

namespace
{
const QString kCatalogRowPrefix = QStringLiteral("results2d.variable.catalog.");
const QString kOfferedKeys = QStringLiteral("resultCatalogKeys");
/** @brief Layer-tree group of a variable key: 1 groundwater (incl. derived), 0 surface. */
int resultKeyGroup(const QString &key)
{
    return key.startsWith(QLatin1String("groundwater:")) ||
                   key.startsWith(QLatin1String("derived:"))
               ? 1
               : 0;
}
}

ResultScalarSublayer *SWMM2DResultsLayer::createResultSublayer_(const QString &key,
                                                                const QString &id, bool visible)
{
    (void)sublayers();
    if (!id.startsWith(QLatin1String("results2d.variable.")) ||
        OpenSWMM::Render::ISublayerHost::findSublayer(*this, id))
        return nullptr;
    auto *sub = new ResultScalarSublayer(id, this);
    sub->setVisible(visible);
    sub->fillStyle()->setAttribute(key);
    for (const auto &v : resultVariables())
        if (v.key() == key) {
            sub->setDescriptor(v);
            break;
        }
    const auto refreshDescriptor = [this, sub] {
        Mesh2DResultVariable descriptor;
        for (const auto &v : resultVariables())
            if (v.key() == sub->fillStyle()->attribute()) {
                descriptor = v;
                break;
            }
        sub->setDescriptor(descriptor);
    };
    connect(sub->fillStyle(), &OpenSWMM::Render::SublayerStyle::styleChanged, sub,
            refreshDescriptor);
    connect(this, &SWMM2DResultsLayer::timeRangeChanged, sub, refreshDescriptor);
    connect(sub, &OpenSWMM::Render::ISublayer::invalidated, this,
            [this] { emit repaintRequested(); });
    return sub;
}

ResultScalarSublayer *SWMM2DResultsLayer::addResultSublayer(const QString &key,
                                                            const QString &requestedId)
{
    const QString id = requestedId.isEmpty()
                           ? QStringLiteral("results2d.variable.") +
                                 QUuid::createUuid().toString(QUuid::WithoutBraces)
                           : requestedId;
    auto *sub = createResultSublayer_(key, id, true);
    if (!sub)
        return nullptr;
    m_sublayerOrder.append(sub);
    emit resultSublayersChanged();
    emit childrenChanged();
    emit repaintRequested();
    return sub;
}

void SWMM2DResultsLayer::syncResultSublayers_()
{
    const auto catalog = resultVariables();
    if (catalog.isEmpty())
        return; // no source, or a live run that has not reported variables yet
    bool changed = false;
    // A live catalog only grows. A saved one is complete: drop catalog rows
    // whose variable is gone, and let the key be offered again if it returns.
    if (!source_->isLive()) {
        QSet<QString> available;
        for (const auto &v : catalog)
            available.insert(v.key());
        const auto existing = sublayers();
        for (auto *base : existing) {
            auto *sub = qobject_cast<ResultScalarSublayer *>(base);
            if (sub && sub->id().startsWith(kCatalogRowPrefix) &&
                !available.contains(sub->variableKey())) {
                m_sublayerOrder.removeOne(sub);
                delete sub;
                changed = true;
            }
        }
        m_offeredResultKeys.removeIf([&](const QString &key) { return !available.contains(key); });
    }
    QSet<QString> shown;
    for (auto *base : sublayers())
        if (auto *sub = qobject_cast<ResultScalarSublayer *>(base))
            shown.insert(sub->variableKey());
    for (const int group : {0, 1})
        for (const auto &v : catalog) {
            const QString key = v.key();
            if (resultKeyGroup(key) != group || m_offeredResultKeys.contains(key))
                continue;
            m_offeredResultKeys.insert(key);
            if (shown.contains(key))
                continue; // a row added by hand already shows it
            auto *sub = createResultSublayer_(key, kCatalogRowPrefix + key, false);
            if (!sub)
                continue;
            // Below this group's rows so the tree reads in catalog order; a
            // first groundwater row goes below the surface rows. Variable rows
            // paint above the builtin hydraulics, as Add result always did.
            int at = m_sublayerOrder.size(), firstSurface = -1, firstSame = -1;
            for (int i = 0; i < m_sublayerOrder.size(); ++i) {
                const int g = sublayerGroup(m_sublayerOrder[i]);
                if (g == group && firstSame < 0)
                    firstSame = i;
                if (g == 0 && firstSurface < 0)
                    firstSurface = i;
            }
            if (firstSame >= 0)
                at = firstSame;
            else if (group == 1 && firstSurface >= 0)
                at = firstSurface;
            m_sublayerOrder.insert(at, sub);
            shown.insert(key);
            changed = true;
        }
    if (changed) {
        emit resultSublayersChanged();
        emit childrenChanged();
        emit repaintRequested();
    }
}

QStringList SWMM2DResultsLayer::sublayerGroups() const
{
    return {tr("Surface results"), tr("Groundwater results")};
}

int SWMM2DResultsLayer::sublayerGroup(const OpenSWMM::Render::ISublayer *sublayer) const
{
    const auto *sub = qobject_cast<const ResultScalarSublayer *>(sublayer);
    return sub ? resultKeyGroup(sub->variableKey()) : -1;
}

void SWMM2DResultsLayer::saveSublayersJsonState(QJsonObject &j) const
{
    QStringList keys(m_offeredResultKeys.cbegin(), m_offeredResultKeys.cend());
    keys.sort();
    j.insert(kOfferedKeys, QJsonArray::fromStringList(keys));
}

void SWMM2DResultsLayer::onSublayersJsonLoaded(const QJsonObject &j)
{
    Q_UNUSED(j);
    syncResultSublayers_();
}

bool SWMM2DResultsLayer::removeResultSublayer(const QString &id)
{
    auto *sub = qobject_cast<ResultScalarSublayer *>(
        OpenSWMM::Render::ISublayerHost::findSublayer(*this, id));
    if (!sub)
        return false;
    m_sublayerOrder.removeOne(sub);
    delete sub;
    emit resultSublayersChanged();
    emit childrenChanged();
    emit repaintRequested();
    return true;
}

void SWMM2DResultsLayer::prepareSublayersJsonLoad(const QJsonObject &j)
{
    if (!j.value(QStringLiteral("sublayers")).isArray())
        return;
    const auto rows = j.value(QStringLiteral("sublayers")).toArray();
    QSet<QString> wanted, wantedKeys;
    for (const auto &entry : rows) {
        const auto row = entry.toObject();
        const auto id = row.value("id").toString();
        if (!id.startsWith(QLatin1String("results2d.variable.")) || wanted.contains(id))
            continue;
        wanted.insert(id);
        const QString key = row.value("style").toObject().value("attribute").toString();
        wantedKeys.insert(key);
        if (!OpenSWMM::Render::ISublayerHost::findSublayer(*this, id))
            addResultSublayer(key, id);
    }
    // A snapshot saved before catalog rows existed has no record of them:
    // keep those rows unless a saved row already shows that variable.
    const auto offered = j.value(kOfferedKeys);
    const auto existing = sublayers();
    for (auto *sub : existing) {
        auto *scalar = qobject_cast<ResultScalarSublayer *>(sub);
        if (!scalar || wanted.contains(sub->id()))
            continue;
        if (!offered.isArray() && sub->id().startsWith(kCatalogRowPrefix) &&
            !wantedKeys.contains(scalar->variableKey()))
            continue;
        removeResultSublayer(sub->id());
    }
    if (offered.isArray()) {
        m_offeredResultKeys.clear();
        for (const auto &key : offered.toArray())
            m_offeredResultKeys.insert(key.toString());
    }
}
