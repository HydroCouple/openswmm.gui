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
#include "render/sublayers/resultvectorsublayer.h"
#include <QJsonArray>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <random>

using openswmmvis::io::Mesh2DResultVariable;
using openswmmvis::io::Mesh2DScalarFrame;
using openswmmvis::io::Mesh2DValueStatus;
using OpenSWMM::Render::ResultScalarSublayer;
using OpenSWMM::Render::ResultVectorSublayer;
using openswmmvis::io::Mesh2DResultVector;

QVector<Mesh2DResultVariable> HDF5Mesh2DSource::faceVariables(QStringList *warnings) const
{
    auto variables = reader_->faceVariables(warnings);
    variables += openswmmvis::io::derivedResultVariables(variables, warnings);
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
    // Several rows read frames each rebuild (components, depth cutoff, bound
    // scalars); the byte cap still bounds large meshes.
    if (m_resultFrames.size() >= 16 || m_resultCacheBytes + bytes > maxBytes) {
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
                                         const std::vector<double> &levels, bool labels,
                                         const std::vector<double> &bandLevels,
                                         bool vertexValues, const QRectF &bandRect) const
{
    for (const auto &entry : m_scalarGeometryCache)
        if (entry.frame == frame && entry.levels == levels && entry.labels == labels &&
            entry.bandLevels == bandLevels && entry.vertexValues == vertexValues &&
            entry.bandRect == bandRect && entry.geometry == geomRevision())
            return entry.output;
    auto output = std::make_shared<OpenSWMM::Render::ResultScalarGeometry>();
    if (!frame)
        output->error = tr("The scalar frame is unavailable.");
    else
        *output = OpenSWMM::Render::buildResultScalarGeometry(
            m_sceneVerts, cellVertexIndices(), triVertexIndices(), triCellMap(), *frame, levels,
            labels, bandLevels, vertexValues, bandRect);
    size_t bytes = output->bytes();
    for (const auto &entry : m_scalarGeometryCache)
        bytes += entry.output->bytes();
    // Several rows can show at once (fills, bands, isolines, vector components).
    if (m_scalarGeometryCache.size() >= 8 || bytes > 96 * 1024 * 1024)
        m_scalarGeometryCache.clear();
    if (output->bytes() <= 64 * 1024 * 1024)
        m_scalarGeometryCache.append(
            {frame, levels, bandLevels, labels, vertexValues, bandRect, geomRevision(), output});
    return output;
}

QVector<Mesh2DResultVector> SWMM2DResultsLayer::resultVectors(QStringList *warnings) const
{
    return openswmmvis::io::pairResultVectors(resultVariables(warnings));
}

namespace
{
const QString kCatalogRowPrefix = QStringLiteral("results2d.variable.catalog.");
const QString kVectorRowPrefix = QStringLiteral("results2d.vector.catalog.");
const QString kOfferedKeys = QStringLiteral("resultCatalogKeys");
// Present in snapshots saved since the depth and velocity rows drive the
// rendering engines; absent snapshots are migrated (migrateEngineStateToRows_).
const QString kUnifiedMarker = QStringLiteral("unifiedResults");
const QString kDepthKey = QStringLiteral("surface:Mesh2_face_depth");
const QString kVelocityKey =
    QStringLiteral("vector:surface:Mesh2_face_vx|surface:Mesh2_face_vy");
const QString kTableKey = QStringLiteral("groundwater:Mesh2_face_gw_table_elev");
const QString kMoistureKey = QStringLiteral("derived:gw_theta_unsat_mean");
const QString kDarcyKey =
    QStringLiteral("vector:groundwater:Mesh2_face_gw_qx|groundwater:Mesh2_face_gw_qy");
// Snapshots before this version carry a hidden automatic row for every field.
constexpr int kDefaultRowsVersion = 2;

/** @return True for the fields that get a row of their own; others are added by hand. */
bool isDefaultResultKey(const QString &key)
{
    return key == kDepthKey || key == kVelocityKey || key == kMoistureKey || key == kTableKey ||
           key == kDarcyKey;
}

/** @return True for the internal passes the depth and velocity rows present. */
bool isEngineId(const QString &id)
{
    return id == QLatin1String("results2d.bands") || id == QLatin1String("results2d.isolines") ||
           id == QLatin1String("results2d.cellDepthFill") ||
           id == QLatin1String("results2d.smoothDepthFill") ||
           id == QLatin1String("results2d.velocity") ||
           id == QLatin1String("results2d.flowStreaks");
}

/** @return 1 for a groundwater key, else 0. A derived |v| key takes its vector's domain;
 *   other derived keys are groundwater. */
int keyDomain(const QString &key)
{
    if (key.startsWith(QLatin1String("vector:")))
        return key.mid(7).startsWith(QLatin1String("groundwater:")) ? 1 : 0;
    if (key.startsWith(QLatin1String("derived:"))) {
        const QString dataset = QUrl::fromPercentEncoding(key.mid(8).toLatin1());
        if (dataset.startsWith(QLatin1String("magnitude|")))
            return dataset.mid(10).startsWith(QLatin1String("groundwater:")) ? 1 : 0;
        return 1;
    }
    return key.startsWith(QLatin1String("groundwater:")) ? 1 : 0;
}

/**
 * @brief Paint index for a new row on top of its tree group. Rows paint below the mesh
 *   wireframe, except the surface vector rows (group 0), which paint above every pass.
 * @param[in] order Paint order (bottom-up).
 * @param[in] wireframe Mesh edge pass (null: after every builtin).
 * @param[in] group The new row's tree group.
 * @param[in] groupOf Tree group of a sublayer, -1 for builtins.
 * @return Below the first row of a group drawn above it in the same region, else just below
 *   the wireframe; the end for surface vectors. Existing rows are never moved.
 */
template <typename Group>
qsizetype rowInsertIndex(const QList<OpenSWMM::Render::ISublayer *> &order,
                         OpenSWMM::Render::ISublayer *wireframe, int group, const Group &groupOf)
{
    if (group == 0)
        return order.size();
    for (qsizetype i = 0; i < order.size(); ++i)
        if (const int g = groupOf(order[i]); g >= 1 && g < group)
            return i;
    const qsizetype at = wireframe ? order.indexOf(wireframe) : -1;
    return at >= 0 ? at : order.size();
}

/**
 * @brief Paint order of an older snapshot's rows around the builtin passes (it painted every
 *   row above them): rows below the mesh wireframe, surface vector rows (group 0) above every
 *   pass. Within each region the rows the snapshot saved stay above rows offered since.
 * @param[in,out] order Paint order (bottom-up).
 * @param[in] wireframe Mesh edge pass; rows go just below it (after every builtin if absent).
 * @param[in] group Tree group of a sublayer, -1 for builtins.
 * @param[in] saved Sublayer ids the snapshot saved.
 * @return True when the order changed.
 */
template <typename Group>
bool normalizeRowOrder(QList<OpenSWMM::Render::ISublayer *> &order,
                       OpenSWMM::Render::ISublayer *wireframe, const Group &group,
                       const QSet<QString> &saved)
{
    QList<OpenSWMM::Render::ISublayer *> builtins, below, top;
    for (auto *sub : order) {
        const int g = group(sub);
        (g < 0 ? builtins : g == 0 ? top : below).append(sub);
    }
    const auto savedOnTop = [&](QList<OpenSWMM::Render::ISublayer *> &rows) {
        std::stable_partition(rows.begin(), rows.end(), [&](OpenSWMM::Render::ISublayer *s) {
            return !saved.contains(s->id());
        });
    };
    savedOnTop(below);
    savedOnTop(top);
    qsizetype at = wireframe ? builtins.indexOf(wireframe) : -1;
    if (at < 0)
        at = builtins.size();
    const auto normalized = builtins.mid(0, at) + below + builtins.mid(at) + top;
    if (normalized == order)
        return false;
    order = normalized;
    return true;
}

/** @return Tree group of a domain and row kind (see SWMM2DResultsLayer::sublayerGroups()). */
int rowGroup(int domain, bool vector)
{
    return 2 * domain + (vector ? 0 : 1);
}

/** @return Visibility and display toggles packed for change detection. */
int scalarState(const ResultScalarSublayer *sub)
{
    const auto *st = sub->fillStyle();
    return int(sub->isVisible()) | int(st->cellFill()) << 1 | int(st->smoothFill()) << 2 |
           int(st->filledContours()) << 3 | int(st->isolines()) << 4;
}
int vectorState(const ResultVectorSublayer *sub)
{
    return int(sub->isVisible()) | int(sub->vectorStyle()->streaks()) << 1;
}

/// What a row's handler last saw: toggles, opacity and the variable or vector key.
struct RowState {
    int toggles = 0;
    qreal opacity = 1;
    QString key;
    int group = -1;
};

/** @brief Hide passes that no row presents any more. */
void hidePasses(std::initializer_list<OpenSWMM::Render::ISublayer *> passes)
{
    for (auto *pass : passes)
        if (pass)
            pass->setVisible(false);
}
} // namespace

bool SWMM2DResultsLayer::isDepthKey(const QString &key)
{
    return key == kDepthKey;
}

bool SWMM2DResultsLayer::isVelocityKey(const QString &key)
{
    return key == kVelocityKey;
}

ResultScalarSublayer *SWMM2DResultsLayer::depthResultSublayer() const
{
    ResultScalarSublayer *first = nullptr;
    for (auto *base : sublayers())
        if (auto *sub = qobject_cast<ResultScalarSublayer *>(base);
            sub && sub->variableKey() == kDepthKey) {
            if (sub->id() == kCatalogRowPrefix + kDepthKey)
                return sub;
            if (!first)
                first = sub;
        }
    return first;
}

ResultVectorSublayer *SWMM2DResultsLayer::velocityResultSublayer() const
{
    ResultVectorSublayer *first = nullptr;
    for (auto *base : sublayers())
        if (auto *sub = qobject_cast<ResultVectorSublayer *>(base);
            sub && sub->vectorKey() == kVelocityKey) {
            if (sub->id() == kVectorRowPrefix + kVelocityKey)
                return sub;
            if (!first)
                first = sub;
        }
    return first;
}

bool SWMM2DResultsLayer::staticWaterCacheable() const
{
    const OpenSWMM::Render::ISublayer *flow = velocityResultSublayer();
    if (!flow)
        flow = m_flowStreakSublayer;
    bool afterFlow = false;
    for (auto *sub : sublayers()) {
        if (sub == flow)
            afterFlow = true;
        else if (afterFlow && sub && sub->isVisible() && sub->opacity() > 0)
            return false;
    }
    return true;
}

bool SWMM2DResultsLayer::sublayerListed(const OpenSWMM::Render::ISublayer *sublayer) const
{
    if (!sublayer || !isEngineId(sublayer->id()))
        return true;
    // Without a velocity row (edge fluxes reported without cell velocities)
    // the animated streaks are a row of their own again.
    return sublayer == m_flowStreakSublayer && !velocityResultSublayer() && hasEdgeFluxData();
}

void SWMM2DResultsLayer::syncEngineVisibility_()
{
    if (auto *depth = depthResultSublayer()) {
        const auto *st = depth->fillStyle();
        const bool on = depth->isVisible();
        if (m_cellDepthFillSublayer)
            m_cellDepthFillSublayer->setVisible(on && st->cellFill() && !st->smoothFill());
        if (m_smoothDepthFillSublayer)
            m_smoothDepthFillSublayer->setVisible(on && st->cellFill() && st->smoothFill());
        if (m_contourBandSublayer)
            m_contourBandSublayer->setVisible(on && st->filledContours());
        if (m_isolineSublayer)
            m_isolineSublayer->setVisible(on && st->isolines());
    }
    if (auto *velocity = velocityResultSublayer()) {
        if (m_flowStreakSublayer)
            m_flowStreakSublayer->setVisible(velocity->isVisible() &&
                                             velocity->vectorStyle()->streaks());
        // Vector rows draw their own arrows.
        if (m_velocityVectorSublayer)
            m_velocityVectorSublayer->setVisible(false);
    }
}

bool SWMM2DResultsLayer::applyResultRowDefaults_(OpenSWMM::Render::ISublayer *base) const
{
    if (auto *sub = qobject_cast<ResultScalarSublayer *>(base)) {
        auto *st = sub->fillStyle();
        const QString key = sub->variableKey();
        if (key == kDepthKey) {
            st->setCellFill(false);
            st->setFilledContours(true);
            return true;
        }
        if (key == kMoistureKey) {
            st->setCellFill(true);
            st->setSmoothFill(true);
            return true;
        }
        if (key == kTableKey) {
            st->setCellFill(false);
            st->setIsolines(true);
            st->contourStyle()->setLabels(true);
            // Per-frame levels draw during live runs (no whole-run history) and
            // need no scan of every saved frame before the first paint.
            auto scheme = st->contourStyle()->scheme();
            scheme.setRangeMode(OpenSWMM::Render::RangeMode::PerFrameAutoStretch);
            st->contourStyle()->setScheme(scheme);
            return true;
        }
        return false;
    }
    if (auto *sub = qobject_cast<ResultVectorSublayer *>(base);
        sub && sub->vectorKey() == kVelocityKey) {
        sub->vectorStyle()->setArrows(false);
        sub->vectorStyle()->setStreaks(true);
        // Arrows keep the former velocity arrows' meaning: px per m/s.
        sub->vectorStyle()->setSizeMode(OpenSWMM::Render::ResultVectorStyle::SizeMode::FixedScale);
        return true;
    }
    // Darcy velocity: the vector style's own arrows (sized and coloured by magnitude).
    if (auto *sub = qobject_cast<ResultVectorSublayer *>(base); sub && sub->vectorKey() == kDarcyKey)
        return true;
    return false;
}

void SWMM2DResultsLayer::migrateEngineStateToRows_()
{
    const auto catalog = resultVariables();
    if (catalog.isEmpty()) {
        m_engineMigrationPending = true; // the catalog is not loaded yet
        return;
    }
    // A vector's components are shown by its vector row (and its magnitude
    // row); hidden automatic component rows of the older snapshot are retired.
    {
        const auto vectors = resultVectors();
        bool retired = false;
        for (auto *base : sublayers())
            if (auto *sub = qobject_cast<ResultScalarSublayer *>(base);
                sub && !sub->isVisible() && sub->id().startsWith(kCatalogRowPrefix) &&
                openswmmvis::io::isVectorComponentKey(vectors, sub->variableKey())) {
                m_sublayerOrder.removeOne(sub);
                delete sub;
                retired = true;
            }
        if (retired) {
            emit resultSublayersChanged();
            emit childrenChanged();
        }
    }
    // The passes are no longer rows of their own: a depth or velocity row the
    // older project removed while its passes stayed on returns to present them.
    const auto groupOf = [this](const OpenSWMM::Render::ISublayer *s) { return sublayerGroup(s); };
    const auto anyVisible = [](std::initializer_list<OpenSWMM::Render::ISublayer *> passes) {
        return std::any_of(passes.begin(), passes.end(),
                           [](auto *pass) { return pass && pass->isVisible(); });
    };
    bool restored = false;
    auto *depth = depthResultSublayer();
    if (!depth &&
        anyVisible({m_cellDepthFillSublayer, m_smoothDepthFillSublayer, m_contourBandSublayer,
                    m_isolineSublayer}) &&
        std::any_of(catalog.cbegin(), catalog.cend(),
                    [](const Mesh2DResultVariable &v) { return v.key() == kDepthKey; }))
        if ((depth = createResultSublayer_(kDepthKey, kCatalogRowPrefix + kDepthKey, false))) {
            m_sublayerOrder.insert(rowInsertIndex(m_sublayerOrder, m_meshEdgeSublayer,
                                                  sublayerGroup(depth), groupOf),
                                   depth);
            restored = true;
        }
    auto *velocity = velocityResultSublayer();
    const auto vectors = resultVectors();
    if (!velocity && anyVisible({m_flowStreakSublayer, m_velocityVectorSublayer}) &&
        std::any_of(vectors.cbegin(), vectors.cend(),
                    [](const Mesh2DResultVector &v) { return v.key() == kVelocityKey; }))
        if ((velocity = createResultVectorSublayer_(kVelocityKey, kVectorRowPrefix + kVelocityKey,
                                                    false))) {
            m_sublayerOrder.insert(rowInsertIndex(m_sublayerOrder, m_meshEdgeSublayer,
                                                  sublayerGroup(velocity), groupOf),
                                   velocity);
            restored = true;
        }
    if (restored) {
        emit resultSublayersChanged();
        emit childrenChanged();
    }
    // Still pending while the rows are written, so their handlers leave the
    // engines alone until every engine has been read.
    if (depth && m_cellDepthFillSublayer && m_smoothDepthFillSublayer && m_contourBandSublayer &&
        m_isolineSublayer) {
        const bool cell = m_cellDepthFillSublayer->isVisible(),
                   smooth = m_smoothDepthFillSublayer->isVisible(),
                   bands = m_contourBandSublayer->isVisible(),
                   iso = m_isolineSublayer->isVisible();
        auto *st = depth->fillStyle();
        // A depth row the user showed keeps its own displays on top of the
        // passes' state (its fills now draw through the passes).
        const bool shown = depth->isVisible();
        const bool ownFill = shown && st->cellFill(), ownSmooth = ownFill && st->smoothFill();
        st->setCellFill(cell || smooth || ownFill);
        st->setSmoothFill(cell ? false : smooth || ownSmooth);
        st->setFilledContours(bands || (shown && st->filledContours()));
        st->setIsolines(iso || (shown && st->isolines()));
        depth->setVisible(st->cellFill() || st->filledContours() || st->isolines() ||
                          (shown && st->cellLabels()));
    }
    if (velocity && m_velocityVectorSublayer && m_flowStreakSublayer) {
        using Style = OpenSWMM::Render::ResultVectorStyle;
        const bool arrows = m_velocityVectorSublayer->isVisible(),
                   streaks = m_flowStreakSublayer->isVisible();
        auto *vs = velocity->vectorStyle();
        // The former arrows' style, so they look the same: fixed px per m/s,
        // dry-depth cutoff and the speed range as the colour range.
        if (const auto *old = m_velocityVectorSublayer->vectorStyle()) {
            vs->setSpacingPx(old->glyphSpacingPx());
            vs->setMinLengthPx(old->glyphLengthMinPx());
            vs->setMaxLengthPx(old->glyphLengthMaxPx());
            vs->setHeadSizePx(old->headSizePx());
            vs->setShaftWidthPx(old->shaftWidthPx());
            vs->setSizeMode(Style::SizeMode::FixedScale);
            vs->setScalePxPerUnit(old->glyphLengthScalePxPerMps());
            vs->setLengthScaling(Style::LengthScaling(int(old->lengthScaling())));
            vs->setDryDepthCutoff(old->dryDepthCutoff());
            vs->setColor(old->color());
            vs->setColorMode(old->colorByMagnitude() ? Style::ColorMode::Magnitude
                                                     : Style::ColorMode::Constant);
            auto scheme = old->scheme();
            if (!scheme.useCustomRange()) {
                scheme.setUseCustomRange(true);
                scheme.setRangeMin(old->speedMinMps());
                scheme.setRangeMax(std::max(old->speedMaxMps(), old->speedMinMps() + 1e-9));
            }
            vs->setColorScheme(scheme);
        }
        vs->setArrows(arrows);
        vs->setStreaks(streaks);
        velocity->setVisible(arrows || streaks);
    }
    m_engineMigrationPending = false;
    syncEngineVisibility_();
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
    // The depth row drives the optimized depth passes: follow its visibility
    // and toggles, and pass a user opacity change on to them.
    auto state = std::make_shared<RowState>(
        RowState{scalarState(sub), sub->opacity(), key, sublayerGroup(sub)});
    connect(sub, &OpenSWMM::Render::ISublayer::invalidated, this, [this, sub, state] {
        // Re-keyed into another tree group: on top of that group's region.
        if (const int g = sublayerGroup(sub); g != state->group) {
            state->group = g;
            if (m_sublayerOrder.removeOne(sub)) {
                m_sublayerOrder.insert(
                    rowInsertIndex(m_sublayerOrder, m_meshEdgeSublayer, g,
                                   [this](const OpenSWMM::Render::ISublayer *s) {
                                       return sublayerGroup(s);
                                   }),
                    sub);
                emit childrenChanged();
            }
        }
        const bool presenting = sub == depthResultSublayer();
        const bool wasDepth = state->key == kDepthKey;
        // A loading snapshot's engines hold the state still to be restored or migrated.
        if (!m_engineMigrationPending && presenting) {
            if (const int now = scalarState(sub); now != state->toggles || !wasDepth) {
                state->toggles = now;
                syncEngineVisibility_();
            }
            if (sub->opacity() != state->opacity)
                for (OpenSWMM::Render::ISublayer *engine :
                     {static_cast<OpenSWMM::Render::ISublayer *>(m_cellDepthFillSublayer),
                      static_cast<OpenSWMM::Render::ISublayer *>(m_smoothDepthFillSublayer),
                      static_cast<OpenSWMM::Render::ISublayer *>(m_contourBandSublayer),
                      static_cast<OpenSWMM::Render::ISublayer *>(m_isolineSublayer)})
                    if (engine)
                        engine->setOpacity(sub->opacity());
        } else if (!m_engineMigrationPending && wasDepth && sub->variableKey() != kDepthKey) {
            // Re-keyed away from depth: the passes follow the next depth row, or hide.
            if (!depthResultSublayer())
                hidePasses({m_cellDepthFillSublayer, m_smoothDepthFillSublayer,
                            m_contourBandSublayer, m_isolineSublayer});
            syncEngineVisibility_();
        }
        state->toggles = scalarState(sub);
        state->opacity = sub->opacity();
        state->key = sub->variableKey();
        emit repaintRequested();
    });
    return sub;
}

ResultVectorSublayer *SWMM2DResultsLayer::createResultVectorSublayer_(const QString &key,
                                                                      const QString &id,
                                                                      bool visible)
{
    (void)sublayers();
    if (!id.startsWith(QLatin1String("results2d.vector.")) ||
        OpenSWMM::Render::ISublayerHost::findSublayer(*this, id))
        return nullptr;
    auto *sub = new ResultVectorSublayer(id, this);
    sub->setVisible(visible);
    sub->vectorStyle()->setVectorKey(key);
    const auto refreshDescriptor = [this, sub] {
        Mesh2DResultVector descriptor;
        for (const auto &v : resultVectors())
            if (v.key() == sub->vectorKey()) {
                descriptor = v;
                break;
            }
        sub->setDescriptor(descriptor);
    };
    refreshDescriptor();
    connect(sub->vectorStyle(), &OpenSWMM::Render::SublayerStyle::styleChanged, sub,
            [sub, refreshDescriptor] {
                if (sub->descriptor().key() != sub->vectorKey())
                    refreshDescriptor();
            });
    connect(this, &SWMM2DResultsLayer::timeRangeChanged, sub, refreshDescriptor);
    // The surface velocity row drives the animated flow-streak engine.
    auto state = std::make_shared<RowState>(
        RowState{vectorState(sub), sub->opacity(), key, sublayerGroup(sub)});
    connect(sub, &OpenSWMM::Render::ISublayer::invalidated, this, [this, sub, state] {
        // Re-keyed into another tree group: on top of that group's region.
        if (const int g = sublayerGroup(sub); g != state->group) {
            state->group = g;
            if (m_sublayerOrder.removeOne(sub)) {
                m_sublayerOrder.insert(
                    rowInsertIndex(m_sublayerOrder, m_meshEdgeSublayer, g,
                                   [this](const OpenSWMM::Render::ISublayer *s) {
                                       return sublayerGroup(s);
                                   }),
                    sub);
                emit childrenChanged();
            }
        }
        const bool presenting = sub == velocityResultSublayer();
        const bool wasVelocity = state->key == kVelocityKey;
        if (!m_engineMigrationPending && presenting) {
            if (const int now = vectorState(sub); now != state->toggles || !wasVelocity)
                syncEngineVisibility_();
            if (sub->opacity() != state->opacity && m_flowStreakSublayer)
                m_flowStreakSublayer->setOpacity(sub->opacity());
        } else if (!m_engineMigrationPending && wasVelocity && sub->vectorKey() != kVelocityKey) {
            // Re-keyed away from velocity: the streaks follow the next velocity row, or hide.
            if (!velocityResultSublayer())
                hidePasses({m_flowStreakSublayer, m_velocityVectorSublayer});
            syncEngineVisibility_();
        }
        state->toggles = vectorState(sub);
        state->opacity = sub->opacity();
        state->key = sub->vectorKey();
        emit repaintRequested();
    });
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
    // On top of its tree group: below the mesh wireframe (see rowInsertIndex()).
    m_sublayerOrder.insert(rowInsertIndex(m_sublayerOrder, m_meshEdgeSublayer, sublayerGroup(sub),
                                          [this](const OpenSWMM::Render::ISublayer *s) { return sublayerGroup(s); }),
                           sub);
    // A new depth row drives the depth passes (not while a snapshot loads).
    if (!m_engineMigrationPending && sub == depthResultSublayer())
        syncEngineVisibility_();
    emit resultSublayersChanged();
    emit childrenChanged();
    emit repaintRequested();
    return sub;
}

ResultVectorSublayer *SWMM2DResultsLayer::addResultVectorSublayer(const QString &key,
                                                                  const QString &requestedId)
{
    const QString id = requestedId.isEmpty()
                           ? QStringLiteral("results2d.vector.") +
                                 QUuid::createUuid().toString(QUuid::WithoutBraces)
                           : requestedId;
    auto *sub = createResultVectorSublayer_(key, id, true);
    if (!sub)
        return nullptr;
    // On top of its tree group: surface vectors above every pass (see rowInsertIndex()).
    m_sublayerOrder.insert(rowInsertIndex(m_sublayerOrder, m_meshEdgeSublayer, sublayerGroup(sub),
                                          [this](const OpenSWMM::Render::ISublayer *s) { return sublayerGroup(s); }),
                           sub);
    // A new velocity row drives the streaks (not while a snapshot loads).
    if (!m_engineMigrationPending && sub == velocityResultSublayer())
        syncEngineVisibility_();
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
    const auto vectors = openswmmvis::io::pairResultVectors(catalog);
    bool changed = false;
    const auto removeRow = [&](OpenSWMM::Render::ISublayer *sub) {
        m_sublayerOrder.removeOne(sub);
        delete sub;
        changed = true;
    };
    // A live catalog only grows. A saved one is complete: drop catalog rows
    // whose variable is gone, and let the key be offered again if it returns.
    if (!source_->isLive()) {
        QSet<QString> available;
        for (const auto &v : catalog)
            available.insert(v.key());
        for (const auto &v : vectors)
            available.insert(v.key());
        const auto existing = sublayers();
        for (auto *base : existing) {
            if (auto *sub = qobject_cast<ResultScalarSublayer *>(base);
                sub && sub->id().startsWith(kCatalogRowPrefix) &&
                !available.contains(sub->variableKey()))
                removeRow(sub);
            else if (auto *vec = qobject_cast<ResultVectorSublayer *>(base);
                     vec && vec->id().startsWith(kVectorRowPrefix) &&
                     !available.contains(vec->vectorKey()))
                removeRow(vec);
        }
        m_offeredResultKeys.removeIf([&](const QString &key) { return !available.contains(key); });
    }
    QSet<QString> shown;
    for (auto *base : sublayers()) {
        if (auto *sub = qobject_cast<ResultScalarSublayer *>(base))
            shown.insert(sub->variableKey());
        else if (auto *vec = qobject_cast<ResultVectorSublayer *>(base))
            shown.insert(vec->vectorKey());
    }
    const bool hadDepth = depthResultSublayer(), hadVelocity = velocityResultSublayer();
    // Insert a new row of tree group g below the group's existing rows (so the
    // tree reads in catalog order), else where its group goes: below the mesh
    // wireframe, or above every pass for surface vectors (rowInsertIndex()).
    const auto insertRow = [&](OpenSWMM::Render::ISublayer *sub, int group) {
        qsizetype at = -1;
        for (qsizetype i = 0; i < m_sublayerOrder.size() && at < 0; ++i)
            if (sublayerGroup(m_sublayerOrder[i]) == group)
                at = i;
        if (at < 0)
            at = rowInsertIndex(m_sublayerOrder, m_meshEdgeSublayer, group,
                                [this](const OpenSWMM::Render::ISublayer *s) { return sublayerGroup(s); });
        m_sublayerOrder.insert(at, sub);
        changed = true;
    };
    // Only the default fields get a row of their own (once: a removed one stays
    // removed); every other field is added by hand from the style panel.
    for (int group = 3; group >= 0; --group) {
        const bool vectorGroup = group % 2 == 0;
        const int domain = group / 2;
        if (vectorGroup) {
            for (const auto &v : vectors) {
                const QString key = v.key();
                if (int(v.domain()) != domain || !isDefaultResultKey(key) ||
                    m_offeredResultKeys.contains(key))
                    continue;
                m_offeredResultKeys.insert(key);
                if (shown.contains(key))
                    continue; // a row added by hand already shows it
                auto *sub = createResultVectorSublayer_(key, kVectorRowPrefix + key, false);
                if (!sub)
                    continue;
                // An older snapshot's migration, not the defaults, sets up the
                // velocity row from the passes it saved.
                sub->setVisible(m_engineMigrationPending && key == kVelocityKey
                                    ? false
                                    : applyResultRowDefaults_(sub));
                insertRow(sub, group);
                shown.insert(key);
            }
            continue;
        }
        for (const auto &v : catalog) {
            const QString key = v.key();
            if (int(v.domain) != domain || !isDefaultResultKey(key) ||
                m_offeredResultKeys.contains(key))
                continue;
            m_offeredResultKeys.insert(key);
            if (shown.contains(key) || openswmmvis::io::isVectorComponentKey(vectors, key))
                continue; // shown by hand, or by its vector row
            auto *sub = createResultSublayer_(key, kCatalogRowPrefix + key, false);
            if (!sub)
                continue;
            sub->setVisible(m_engineMigrationPending && key == kDepthKey
                                ? false
                                : applyResultRowDefaults_(sub));
            insertRow(sub, group);
            shown.insert(key);
        }
    }
    if (m_engineMigrationPending)
        migrateEngineStateToRows_();
    else if (bool(depthResultSublayer()) != hadDepth ||
             bool(velocityResultSublayer()) != hadVelocity)
        syncEngineVisibility_();
    if (changed) {
        emit resultSublayersChanged();
        emit childrenChanged();
        emit repaintRequested();
    }
}

QStringList SWMM2DResultsLayer::sublayerGroups() const
{
    // Index = 2 * domain + (scalar ? 1 : 0): vectors above scalars, surface
    // water above groundwater, as they paint.
    return {tr("Surface water — Vector"), tr("Surface water — Scalar"),
            tr("Groundwater — Vector"), tr("Groundwater — Scalar")};
}

int SWMM2DResultsLayer::sublayerGroup(const OpenSWMM::Render::ISublayer *sublayer) const
{
    if (const auto *sub = qobject_cast<const ResultScalarSublayer *>(sublayer)) {
        const QString key = sub->variableKey();
        const int domain = sub->descriptor().key() == key && !key.isEmpty()
                               ? int(sub->descriptor().domain)
                               : keyDomain(key);
        return rowGroup(domain, false);
    }
    if (const auto *sub = qobject_cast<const ResultVectorSublayer *>(sublayer)) {
        const QString key = sub->vectorKey();
        const int domain = sub->descriptor().key() == key && !key.isEmpty()
                               ? int(sub->descriptor().domain())
                               : keyDomain(key);
        return rowGroup(domain, true);
    }
    return -1;
}

void SWMM2DResultsLayer::saveSublayersJsonState(QJsonObject &j) const
{
    QStringList keys(m_offeredResultKeys.cbegin(), m_offeredResultKeys.cend());
    keys.sort();
    j.insert(kOfferedKeys, QJsonArray::fromStringList(keys));
    j.insert(kUnifiedMarker, kDefaultRowsVersion);
}

void SWMM2DResultsLayer::onSublayersJsonLoaded(const QJsonObject &j)
{
    const bool legacy = !j.contains(kUnifiedMarker);
    if (!legacy)
        m_engineMigrationPending = false; // rows and engines were saved in step
    // An older snapshot carries a hidden automatic row for every field: retire
    // those of the other fields. Visible rows, rows added by hand and the
    // default fields stay.
    if (j.value(kUnifiedMarker).toInt() < kDefaultRowsVersion) {
        bool retired = false;
        const auto rows = sublayers();
        for (auto *base : rows) {
            auto *scalar = qobject_cast<ResultScalarSublayer *>(base);
            auto *vector = qobject_cast<ResultVectorSublayer *>(base);
            const bool automatic = scalar ? base->id().startsWith(kCatalogRowPrefix)
                                          : vector && base->id().startsWith(kVectorRowPrefix);
            if (automatic && !base->isVisible() &&
                !isDefaultResultKey(scalar ? scalar->variableKey() : vector->vectorKey())) {
                m_sublayerOrder.removeOne(base);
                delete base;
                retired = true;
            }
        }
        if (retired) {
            emit resultSublayersChanged();
            emit childrenChanged();
            emit repaintRequested();
        }
    }
    syncResultSublayers_();
    if (m_engineMigrationPending)
        migrateEngineStateToRows_();
    else
        syncEngineVisibility_();
    // Older snapshots painted every row above the mesh wireframe; a current
    // one keeps the order the user arranged.
    if (legacy) {
        QSet<QString> saved;
        for (const auto &row : j.value(QStringLiteral("sublayers")).toArray())
            saved.insert(row.toObject().value(QStringLiteral("id")).toString());
        if (normalizeRowOrder(m_sublayerOrder, m_meshEdgeSublayer,
                              [this](const OpenSWMM::Render::ISublayer *s) { return sublayerGroup(s); }, saved)) {
            emit childrenChanged();
            emit repaintRequested();
        }
    }
}

bool SWMM2DResultsLayer::removeResultSublayer(const QString &id)
{
    auto *found = OpenSWMM::Render::ISublayerHost::findSublayer(*this, id);
    if (!qobject_cast<ResultScalarSublayer *>(found) && !qobject_cast<ResultVectorSublayer *>(found))
        return false;
    const bool depth = found == depthResultSublayer(), velocity = found == velocityResultSublayer();
    m_sublayerOrder.removeOne(found);
    delete found;
    // The passes a removed depth or velocity row drove follow the next such
    // row, or hide (not while a snapshot loads: it restores them).
    if (!m_engineMigrationPending && (depth || velocity)) {
        if (depth && !depthResultSublayer())
            hidePasses({m_cellDepthFillSublayer, m_smoothDepthFillSublayer, m_contourBandSublayer,
                        m_isolineSublayer});
        if (velocity && !velocityResultSublayer())
            hidePasses({m_flowStreakSublayer, m_velocityVectorSublayer});
        syncEngineVisibility_();
    }
    emit resultSublayersChanged();
    emit childrenChanged();
    emit repaintRequested();
    return true;
}

void SWMM2DResultsLayer::prepareSublayersJsonLoad(const QJsonObject &j)
{
    if (!j.value(QStringLiteral("sublayers")).isArray())
        return;
    // The row handlers leave the engines alone while the snapshot loads, so the
    // engines keep their restored visibility and opacity. A snapshot saved
    // before the rows drove the engines (no marker) then has the engine state
    // translated into the rows (onSublayersJsonLoaded).
    m_engineMigrationPending = true;
    const auto rows = j.value(QStringLiteral("sublayers")).toArray();
    QSet<QString> wanted, wantedKeys;
    for (const auto &entry : rows) {
        const auto row = entry.toObject();
        const auto id = row.value("id").toString();
        const bool scalar = id.startsWith(QLatin1String("results2d.variable."));
        const bool vector = id.startsWith(QLatin1String("results2d.vector."));
        if ((!scalar && !vector) || wanted.contains(id))
            continue;
        wanted.insert(id);
        const auto style = row.value("style").toObject();
        const QString key = (scalar ? style.value("attribute") : style.value("vectorKey")).toString();
        wantedKeys.insert(key);
        if (!OpenSWMM::Render::ISublayerHost::findSublayer(*this, id)) {
            if (scalar)
                addResultSublayer(key, id);
            else
                addResultVectorSublayer(key, id);
        }
    }
    // A snapshot saved before catalog rows existed has no record of them:
    // keep those rows unless a saved row already shows that variable.
    const auto offered = j.value(kOfferedKeys);
    const auto existing = sublayers();
    for (auto *sub : existing) {
        auto *scalar = qobject_cast<ResultScalarSublayer *>(sub);
        auto *vector = qobject_cast<ResultVectorSublayer *>(sub);
        if ((!scalar && !vector) || wanted.contains(sub->id()))
            continue;
        const bool automatic = scalar ? sub->id().startsWith(kCatalogRowPrefix)
                                      : sub->id().startsWith(kVectorRowPrefix);
        const QString key = scalar ? scalar->variableKey() : vector->vectorKey();
        if (!offered.isArray() && automatic && !wantedKeys.contains(key)) {
            // The snapshot's saved passes, not this row's defaults, set up the
            // depth and velocity rows of an older project (migration).
            if (!j.contains(kUnifiedMarker) && (key == kDepthKey || key == kVelocityKey))
                sub->setVisible(false);
            continue;
        }
        removeResultSublayer(sub->id());
    }
    if (offered.isArray()) {
        m_offeredResultKeys.clear();
        for (const auto &key : offered.toArray())
            m_offeredResultKeys.insert(key.toString());
    }
}

OpenSWMM::Render::ResultVectorGeometry
SWMM2DResultsLayer::resultVectorGeometry(const ResultVectorSublayer *sub, const QRectF &view,
                                         double pixelsPerScene, double *colorMin,
                                         double *colorMax, QString *colorUnits,
                                         int timeIndex) const
{
    using Style = OpenSWMM::Render::ResultVectorStyle;
    using S = Mesh2DValueStatus;
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    OpenSWMM::Render::ResultVectorGeometry out;
    if (colorMin)
        *colorMin = nan;
    if (colorMax)
        *colorMax = nan;
    if (colorUnits)
        colorUnits->clear();
    if (!sub || !sub->isVisible() || sub->opacity() <= 0 || !(pixelsPerScene > 0) ||
        !std::isfinite(pixelsPerScene))
        return out;
    const auto *style = sub->vectorStyle();
    const auto &vector = sub->descriptor();
    if (vector.key().isEmpty() || vector.key() != sub->vectorKey()) {
        out.error = tr("Vector result is unavailable: %1").arg(sub->vectorKey());
        return out;
    }
    // The presenting surface velocity row animates its streaks on the
    // flow-streak engine; every other row traces static streaks here.
    const bool animatedStreaks = sub == velocityResultSublayer();
    // Nothing drawn here: no arrows, and streaks off or animated by the
    // flow-streak engine. Skip the frame reads.
    if (!style->arrows() && (!style->streaks() || animatedStreaks))
        return out;
    const int time = timeIndex >= 0 ? timeIndex : currentTimeIndex();
    const auto fx = resultFrame(vector.x.key(), time), fy = resultFrame(vector.y.key(), time);
    if (!fx->error.isEmpty() || !fy->error.isEmpty() || fx->values.size() != fy->values.size() ||
        fx->status.size() != fx->values.size() || fy->status.size() != fy->values.size()) {
        out.error = !fx->error.isEmpty() ? fx->error : fy->error;
        return out;
    }
    const size_t cells = fx->values.size();
    // Surface vectors skip cells shallower than the dry-depth cutoff.
    std::shared_ptr<const Mesh2DScalarFrame> depth;
    const double cutoff = style->dryDepthCutoff();
    if (vector.domain() == Mesh2DResultVariable::Domain::Surface && cutoff > 0)
        if (auto d = resultFrame(kDepthKey, time);
            d->error.isEmpty() && d->values.size() == cells && d->status.size() == cells)
            depth = std::move(d);
    const auto cellVector = [&](int c, double &vx, double &vy) {
        if (c < 0 || size_t(c) >= cells || fx->status[size_t(c)] != S::Valid ||
            fy->status[size_t(c)] != S::Valid)
            return false;
        if (depth && (depth->status[size_t(c)] != S::Valid || !(depth->values[size_t(c)] >= cutoff)))
            return false;
        vx = fx->values[size_t(c)];
        vy = fy->values[size_t(c)];
        return std::isfinite(vx) && std::isfinite(vy);
    };
    // The |v| range over the cells the row draws (dry cells excluded), scanned
    // once per frame and remembered, so legend and status refreshes reuse it.
    double magMin = std::numeric_limits<double>::infinity(), magMax = 0;
    const double rangeCutoff = depth ? cutoff : -1;
    const auto cached =
        std::find_if(m_vectorRanges.cbegin(), m_vectorRanges.cend(), [&](const auto &entry) {
            return entry.x.lock() == fx && entry.y.lock() == fy &&
                   entry.depth.lock() == depth && entry.cutoff == rangeCutoff;
        });
    if (cached != m_vectorRanges.cend()) {
        magMin = cached->lo;
        magMax = cached->hi;
    } else {
        for (size_t c = 0; c < cells; ++c) {
            double vx, vy;
            if (!cellVector(int(c), vx, vy))
                continue;
            const double m = std::hypot(vx, vy);
            magMin = std::min(magMin, m);
            magMax = std::max(magMax, m);
        }
        m_vectorRanges[m_vectorRangeNext++ % m_vectorRanges.size()] = {fx, fy, depth,
                                                                       rangeCutoff, magMin, magMax};
    }
    if (!(magMax > 0))
        return out; // nothing moves
    const double tiny = magMax * 1e-6;

    // Bound scalars: a missing or failed frame hides the glyphs it would size or colour.
    std::shared_ptr<const Mesh2DScalarFrame> sizeFrame, colorFrame;
    if (style->sizeMode() == Style::SizeMode::Scalar)
        sizeFrame = resultFrame(style->sizeKey(), time);
    if (style->colorMode() == Style::ColorMode::Scalar)
        colorFrame = resultFrame(style->colorKey(), time);
    const auto bound = [&](const std::shared_ptr<const Mesh2DScalarFrame> &frame, int c,
                           double magnitude) {
        if (!frame)
            return magnitude;
        if (!frame->error.isEmpty() || c < 0 || size_t(c) >= frame->values.size() ||
            size_t(c) >= frame->status.size() || frame->status[size_t(c)] != S::Valid)
            return nan;
        return double(frame->values[size_t(c)]);
    };
    double sizeLo = 0, sizeHi = magMax;
    if (style->sizeUseCustomRange()) {
        sizeLo = style->sizeRangeMin();
        sizeHi = style->sizeRangeMax();
    } else if (sizeFrame) {
        sizeLo = sizeFrame->minimum;
        sizeHi = sizeFrame->maximum;
    }
    double colorLo = nan, colorHi = nan;
    QString units;
    if (style->colorMode() == Style::ColorMode::Magnitude) {
        colorLo = magMin;
        colorHi = magMax;
        units = vector.x.unitsKnown ? vector.x.units : QString();
    } else if (colorFrame && colorFrame->error.isEmpty()) {
        colorLo = colorFrame->minimum;
        colorHi = colorFrame->maximum;
        units = colorFrame->descriptor.unitsKnown ? colorFrame->descriptor.units : QString();
    }
    if (colorMin)
        *colorMin = colorLo;
    if (colorMax)
        *colorMax = colorHi;
    if (colorUnits)
        *colorUnits = units;
    const auto colorAt = [&](int c, double magnitude) {
        return sub->colorFor(bound(colorFrame, c, magnitude), colorLo, colorHi);
    };

    const QRectF area = view.intersected(m_sceneBBox);
    if (area.isEmpty())
        return out;
    // A world-aligned grid keeps glyphs still while panning.
    const auto forGrid = [&](double step, double offset, size_t cap, const auto &visit) {
        if (!(step > 0))
            return;
        // Coarsen the grid until it fits the cap, so glyphs cover the whole
        // area instead of stopping partway down it. Sized by the view (a fixed
        // number of screen pixels), not its overlap with the mesh, so the grid
        // stays put while panning.
        const double points =
            (std::ceil(view.width() / step) + 1) * (std::ceil(view.height() / step) + 1);
        if (points > double(cap))
            step *= 1.01 * std::sqrt(points / double(cap));
        const double x0 = std::floor(area.left() / step) * step + offset * step;
        const double y0 = std::floor(area.top() / step) * step + offset * step;
        size_t visited = 0;
        for (double y = y0; y <= area.bottom(); y += step)
            for (double x = x0; x <= area.right(); x += step) {
                if (x < area.left() || y < area.top())
                    continue;
                if (++visited > cap)
                    return;
                visit(QPointF(x, y));
            }
    };
    if (style->arrows())
        forGrid(style->spacingPx() / pixelsPerScene, 0.5, 60000, [&](const QPointF &p) {
            const int c = pickCellAt(p);
            double vx, vy;
            if (!cellVector(c, vx, vy))
                return;
            const double m = std::hypot(vx, vy);
            if (m <= tiny)
                return;
            const double sizeValue =
                style->sizeMode() == Style::SizeMode::Constant ? 0 : bound(sizeFrame, c, m);
            if (!std::isfinite(sizeValue) && style->sizeMode() != Style::SizeMode::Constant)
                return;
            const double length = style->lengthPxFor(sizeValue, sizeLo, sizeHi) / pixelsPerScene;
            const QColor color = colorAt(c, m);
            if (!(length > 0) || color.alpha() == 0)
                return;
            // Scene Y runs opposite to model Y.
            const QPointF half(vx / m * length * 0.5, -vy / m * length * 0.5);
            out.arrows.push_back({p - half, p + half, color});
        });

    // Static streaks traced through the vertex-interpolated field. The surface
    // velocity row animates its streaks on the flow-streak engine instead.
    if (!style->streaks() || animatedStreaks)
        return out;
    const auto gx = resultScalarGeometry(fx, {}, false, {}, true);
    const auto gy = resultScalarGeometry(fy, {}, false, {}, true);
    const bool nodal = gx->vertexValues.size() == size_t(m_sceneVerts.size()) &&
                       gy->vertexValues.size() == gx->vertexValues.size();
    const auto &triVerts = triVertexIndices();
    const auto fieldAt = [&](const QPointF &p, double &vx, double &vy, int &cell) {
        const int tri = pickDisplayTriAt_(p);
        if (tri < 0 || size_t(tri) >= triCell_.size() || tri >= m_sceneTris.size())
            return false;
        cell = triCell_[size_t(tri)];
        double cx, cy;
        if (!cellVector(cell, cx, cy) || std::hypot(cx, cy) <= tiny)
            return false; // stops in waterless/invalid or still cells
        vx = cx;
        vy = cy;
        std::array<double, 3> w;
        const auto &st = m_sceneTris[tri];
        if (nodal && size_t(tri) < triVerts.size() &&
            CellWaterGeometry::barycentric(p, st.a, st.b, st.c, w)) {
            double ix = 0, iy = 0;
            bool ok = true;
            for (int k = 0; k < 3 && ok; ++k) {
                const int v = triVerts[size_t(tri)][size_t(k)];
                ok = v >= 0 && size_t(v) < gx->vertexValues.size() &&
                     std::isfinite(gx->vertexValues[size_t(v)]) &&
                     std::isfinite(gy->vertexValues[size_t(v)]);
                if (ok) {
                    ix += w[size_t(k)] * gx->vertexValues[size_t(v)];
                    iy += w[size_t(k)] * gy->vertexValues[size_t(v)];
                }
            }
            if (ok && std::hypot(ix, iy) > tiny) {
                vx = ix;
                vy = iy;
            }
        }
        return true;
    };
    constexpr int kSteps = 12;
    const double stepLength = style->streakLengthPx() / pixelsPerScene / kSteps;
    forGrid(style->streakSpacingPx() / pixelsPerScene, 0.25, 30000, [&](const QPointF &seed) {
        double vx, vy;
        int cell = -1;
        if (!fieldAt(seed, vx, vy, cell))
            return;
        const QColor color = colorAt(cell, std::hypot(vx, vy));
        if (color.alpha() == 0)
            return;
        OpenSWMM::Render::ResultVectorStreak streak;
        streak.color = color;
        streak.points.reserve(kSteps + 1);
        streak.points.push_back(seed);
        QPointF q = seed;
        for (int i = 0; i < kSteps; ++i) {
            double ux, uy;
            int c;
            if (!fieldAt(q, ux, uy, c))
                break;
            // Midpoint step with unit speed: streaks show direction and
            // pattern; their colour carries the magnitude.
            double m = std::hypot(ux, uy);
            const QPointF mid = q + QPointF(ux / m, -uy / m) * (0.5 * stepLength);
            double mx, my;
            if (fieldAt(mid, mx, my, c)) {
                ux = mx;
                uy = my;
                m = std::hypot(ux, uy);
            }
            q += QPointF(ux / m, -uy / m) * stepLength; // scene Y runs opposite to model Y
            streak.points.push_back(q);
        }
        if (streak.points.size() >= 2)
            out.streaks.push_back(std::move(streak));
    });
    return out;
}
