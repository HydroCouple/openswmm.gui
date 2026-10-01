#include "layers/swmm2dresultslayer.h"
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
{ return reader_->faceVariables(warnings); }
bool HDF5Mesh2DSource::readFaceVariableAt(const Mesh2DResultVariable &v, int t,
    std::vector<float> &values, std::vector<Mesh2DValueStatus> &status)
{ return reader_->readFaceVariableAt(v, t, values, status); }

QVector<Mesh2DResultVariable> SWMM2DResultsLayer::resultVariables(QStringList *warnings) const
{
    if (!source_) { if (warnings) warnings->clear(); return {}; }
    const int generation = source_->resultGeneration(), times = source_->timeCount();
    if (m_resultCatalogRevision != source_revision_ || m_resultCatalogGeneration != generation
        || m_resultCatalogTimes != times) {
        m_resultCatalog = source_->faceVariables(&m_resultCatalogWarnings);
        m_resultCatalogRevision = source_revision_; m_resultCatalogGeneration = generation;
        m_resultCatalogTimes = times;
    }
    if (warnings) *warnings = m_resultCatalogWarnings;
    return m_resultCatalog;
}

namespace {
constexpr qsizetype sampleLimit = 32768;
// Deterministic reservoir preserves distribution over the full run, without
// retaining every frame. Exact extrema are independent of sampling.
void accumulate(Mesh2DScalarFrame &range, const std::vector<float> &values,
                const std::vector<Mesh2DValueStatus> &status,
                quint64 &seen, std::mt19937_64 &rng)
{
    for (size_t i = 0; i < values.size(); ++i) {
        if (status[i] != Mesh2DValueStatus::Valid || !std::isfinite(values[i])) continue;
        const double v = values[i];
        if (!seen) range.minimum = range.maximum = v;
        else { range.minimum = std::min(range.minimum, v); range.maximum = std::max(range.maximum, v); }
        ++seen;
        if (range.samples.size() < sampleLimit) range.samples.push_back(v);
        else {
            const quint64 j = std::uniform_int_distribution<quint64>(0, seen - 1)(rng);
            if (j < quint64(sampleLimit)) range.samples[qsizetype(j)] = v;
        }
    }
}
}

std::shared_ptr<const Mesh2DScalarFrame> SWMM2DResultsLayer::resultFrame(
    const QString &key, int time, bool wholeRun) const
{
    auto frame = std::make_shared<Mesh2DScalarFrame>();
    frame->time = time;
    if (!source_) { frame->error = tr("No result source is available."); return frame; }
    const int generation = source_->resultGeneration(), times = source_->timeCount();
    if (m_resultCacheRevision != source_revision_ || m_resultCacheGeneration != generation
        || m_resultCacheTimes != times) {
        m_resultFrames.clear(); m_resultRanges.clear(); m_resultCacheBytes = 0;
        m_resultCacheRevision = source_revision_; m_resultCacheGeneration = generation;
        m_resultCacheTimes = times;
    }
    const QString cacheKey = key + QChar(0x1f) + QString::number(time) + (wholeRun ? ":run" : ":frame");
    if (auto it = m_resultFrames.constFind(cacheKey); it != m_resultFrames.cend()) return it.value();
    bool found = false;
    for (const auto &v : resultVariables()) if (v.key() == key) { frame->descriptor = v; found = true; break; }
    if (!found) { frame->error = tr("Saved result variable is unavailable: %1").arg(key); return frame; }
    auto read = [&](int t, std::vector<float> &v, std::vector<Mesh2DValueStatus> &s) {
        return source_->readFaceVariableAt(frame->descriptor, t, v, s)
            && v.size() == size_t(source_->triangleCount()) && s.size() == v.size();
    };
    if (!read(time, frame->values, frame->status)) {
        frame->values.clear(); frame->status.clear();
        frame->error = tr("The selected variable has no valid frame at this time."); return frame;
    }
    const bool scan = wholeRun && frame->descriptor.frameCount > 0;
    if (scan && !source_->supportsWholeRunRange()) {
        frame->error = tr("Whole-run range requires a complete output history. Live or retained snapshots support current frame or an explicit range.");
        return frame;
    }
    std::shared_ptr<const Mesh2DScalarFrame> range;
    if (scan) range = m_resultRanges.value(key);
    if (!range) {
        auto stats = std::make_shared<Mesh2DScalarFrame>();
        quint64 seen = 0; std::mt19937_64 rng(0);
        if (scan) {
            std::vector<float> values; std::vector<Mesh2DValueStatus> status;
            for (int t = 0; t < frame->descriptor.frameCount; ++t) {
                if (!read(t, values, status)) {
                    stats->error = tr("Whole-run range could not read frame %1. Choose current frame or an explicit range.").arg(t + 1);
                    break;
                }
                accumulate(*stats, values, status, seen, rng);
            }
        } else accumulate(*stats, frame->values, frame->status, seen, rng);
        range = stats;
        if (scan) {
            if (m_resultRanges.size() >= 16) m_resultRanges.clear();
            m_resultRanges.insert(key, stats);
        }
    }
    frame->minimum = range->minimum; frame->maximum = range->maximum;
    frame->samples = range->samples; frame->error = range->error;
    const size_t bytes = frame->values.size() * (sizeof(float) + sizeof(Mesh2DValueStatus))
        + size_t(frame->samples.size()) * sizeof(double);
    constexpr size_t maxBytes = 64 * 1024 * 1024;
    if (m_resultFrames.size() >= 8 || m_resultCacheBytes + bytes > maxBytes) {
        m_resultFrames.clear(); m_resultCacheBytes = 0;
    }
    if (bytes <= maxBytes) { m_resultFrames.insert(cacheKey, frame); m_resultCacheBytes += bytes; }
    return frame;
}

ResultScalarSublayer *SWMM2DResultsLayer::addResultSublayer(const QString &key, const QString &requestedId)
{
    (void)sublayers();
    const QString id = requestedId.isEmpty()
        ? QStringLiteral("results2d.variable.") + QUuid::createUuid().toString(QUuid::WithoutBraces) : requestedId;
    if (!id.startsWith(QLatin1String("results2d.variable."))
        || OpenSWMM::Render::ISublayerHost::findSublayer(*this, id)) return nullptr;
    auto *sub = new ResultScalarSublayer(id, this);
    sub->fillStyle()->setAttribute(key);
    for (const auto &v : resultVariables()) if (v.key() == key) { sub->setDescriptor(v); break; }
    const auto refreshDescriptor = [this, sub] {
        Mesh2DResultVariable descriptor;
        for (const auto &v : resultVariables())
            if (v.key() == sub->fillStyle()->attribute()) { descriptor = v; break; }
        sub->setDescriptor(descriptor);
    };
    connect(sub->fillStyle(), &OpenSWMM::Render::SublayerStyle::styleChanged, sub, refreshDescriptor);
    connect(this, &SWMM2DResultsLayer::timeRangeChanged, sub, refreshDescriptor);
    m_sublayerOrder.append(sub);
    connect(sub, &OpenSWMM::Render::ISublayer::invalidated, this, [this] { emit repaintRequested(); });
    emit resultSublayersChanged();
    emit childrenChanged();
    emit repaintRequested();
    return sub;
}

bool SWMM2DResultsLayer::removeResultSublayer(const QString &id)
{
    auto *sub = qobject_cast<ResultScalarSublayer *>(OpenSWMM::Render::ISublayerHost::findSublayer(*this, id));
    if (!sub) return false;
    m_sublayerOrder.removeOne(sub);
    delete sub;
    emit resultSublayersChanged(); emit childrenChanged(); emit repaintRequested();
    return true;
}

void SWMM2DResultsLayer::prepareSublayersJsonLoad(const QJsonObject &j)
{
    if (!j.value(QStringLiteral("sublayers")).isArray()) return;
    const auto rows = j.value(QStringLiteral("sublayers")).toArray();
    QSet<QString> wanted;
    for (const auto &entry : rows) {
        const auto row = entry.toObject(); const auto id = row.value("id").toString();
        if (!id.startsWith(QLatin1String("results2d.variable.")) || wanted.contains(id)) continue;
        wanted.insert(id);
        if (!OpenSWMM::Render::ISublayerHost::findSublayer(*this, id))
            addResultSublayer(row.value("style").toObject().value("attribute").toString(), id);
    }
    const auto existing = sublayers();
    for (auto *sub : existing) if (qobject_cast<ResultScalarSublayer *>(sub) && !wanted.contains(sub->id()))
        removeResultSublayer(sub->id());
}
