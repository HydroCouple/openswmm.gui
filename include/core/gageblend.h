/*!
 * \file   gageblend.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Volume-conserving weighted blend of rain-gage time series.
 *
 * \details Produces the single synthetic series that represents a weighted
 *          combination of several rain gages — the data half of an
 *          interpolated-gage assignment.
 *
 *          The blend must reproduce the ENGINE's reading of a rainfall series,
 *          not the intuitive one. Per `Gage.cpp::updateAllGages`, an entry at
 *          `t_k` is a BOXCAR, not a hold: it applies over `[t_k, t_k + I)`
 *          where `I` is the GAGE's recording interval, and rainfall is zero in
 *          the gaps between boxes and after the last entry. An entry that
 *          starts before the previous box ends preempts it.
 *
 *          Consequently the blend cannot simply union timestamps and combine
 *          values — that silently changes total depth whenever the sources'
 *          intervals differ. Instead each source is expanded to explicit boxes
 *          in the intensity domain, and those boxes are integrated exactly onto
 *          a common grid of pitch `gcd(intervals)`. Volume is then conserved
 *          cell-wise, and therefore globally, for any mix of intervals and
 *          alignments.
 *
 *          Output is always INTENSITY: it is the only rain type whose stored
 *          value does not depend on the interval field, so a later edit to the
 *          interval — or the `h:mm` truncation the INP writer applies — cannot
 *          silently rescale the total.
 *
 *          Leaf module: no Qt-GUI, engine, or GDAL dependency. Times are plain
 *          seconds since the epoch so the module stays testable without
 *          QDateTime.
 */

#ifndef OPENSWMMVIS_CORE_GAGEBLEND_H
#define OPENSWMMVIS_CORE_GAGEBLEND_H

#include <QString>
#include <QVector>

#include <QtGlobal>

namespace GageBlend
{

/*! \brief Rain-data format, matching SWMM_GageRainType. */
enum class RainType
{
    Intensity  = 0,  ///< Value is a rate (depth/hour).
    Volume     = 1,  ///< Value is depth accumulated over one interval.
    Cumulative = 2   ///< Value is a running total; deltas carry the depth.
};

/*! \brief Ceiling on grid cells, so a pathological gcd cannot melt the model. */
constexpr int kMaxGridCells = 200000;

/*! \brief Relative tolerance for the volume-conservation check. */
constexpr double kVolumeTolerance = 1e-9;

/*! \brief One (time, value) entry of a rainfall series. */
struct SeriesPoint
{
    qint64 t     = 0;    ///< Seconds since the epoch.
    double value = 0.0;  ///< In the series' own rain type and units.
};

/*! \brief A contributing gage: its series plus everything needed to read it. */
struct SourceGage
{
    QVector<SeriesPoint> points;                     ///< Strictly increasing in t.
    qint64               intervalSec = 0;            ///< Gage recording interval.
    RainType             rainType    = RainType::Intensity;
    double               scaleFactor = 1.0;          ///< Folded into the blend.
    QString              name;                       ///< For diagnostics only.
};

/*! \brief A constant-intensity interval, `[t0, t1)`, in depth/hour. */
struct Box
{
    qint64 t0 = 0;
    qint64 t1 = 0;
    double intensity = 0.0;
};

/*! \brief Outcome of one blend. */
struct BlendResult
{
    QVector<SeriesPoint> points;            ///< Emitted series; INTENSITY.
    qint64               intervalSec = 0;   ///< gcd of the source intervals.

    double blendedDepth   = 0.0;   ///< Total depth of \ref points.
    double referenceDepth = 0.0;   ///< Weighted sum of the sources' depths.
    double relativeError  = 0.0;   ///< |blended - reference| / max(reference, eps).

    double peakBlended   = 0.0;    ///< Max emitted intensity.
    double peakReference = 0.0;    ///< Weighted sum of the sources' peaks.

    QString error;                 ///< Non-empty means nothing was produced.

    /*! \brief True when the blend conserved volume within tolerance. */
    [[nodiscard]] bool volumeOk() const
    {
        return error.isEmpty() && relativeError <= kVolumeTolerance;
    }

    /*! \brief Fraction of the reference peak that survived, or 1 when flat.
     *         Reported, never enforced — attenuation is physically expected
     *         when misaligned sources are integrated onto a common grid. */
    [[nodiscard]] double peakRetention() const
    {
        return peakReference > 0.0 ? peakBlended / peakReference : 1.0;
    }
};

/*!
 * \brief Expand one source into explicit intensity boxes.
 * \details Folds in the gage's own interval, rain type, and scale factor. The
 *          box for entry `k` ends at `min(t_k + interval, t_{k+1})`; that `min`
 *          is load-bearing, because an entry starting before the previous box
 *          ends preempts it in the engine. Without it, a series whose entries
 *          are spaced closer than its declared interval reports more volume
 *          than the engine would apply.
 *
 *          CUMULATIVE deltas are taken by walking the entries in order, with a
 *          decrease read as a counter reset (the new value is then the whole
 *          interval's depth) — matching `convertRainfall`.
 * \returns Boxes in ascending time; empty when the source has no usable entry.
 */
[[nodiscard]] QVector<Box> toBoxes(const SourceGage &source);

/*! \brief Total depth carried by a box list. */
[[nodiscard]] double boxDepth(const QVector<Box> &boxes);

/*!
 * \brief Total depth of an emitted series under the ENGINE's boxcar rule.
 * \details Re-derives what the engine would actually apply, rather than what
 *          the blend intended, so it can be used to verify a series after it
 *          has round-tripped through storage. Any entry lost, reordered, or
 *          rounded on the way shows up here.
 */
[[nodiscard]] double engineDepth(const QVector<SeriesPoint> &points,
                                 qint64 intervalSec,
                                 RainType rainType   = RainType::Intensity,
                                 double   scaleFactor = 1.0);

/*!
 * \brief Blend \p sources under \p weights into one INTENSITY series.
 * \param sources  Contributing gages; must be non-empty and index-aligned
 *                 with \p weights.
 * \param weights  Blend weights; need not sum to exactly 1 (they are used as
 *                 given, so the caller's normalisation is what is honoured).
 * \returns The blended series plus its volume-conservation figures. On refusal
 *          \ref BlendResult::error is set and \ref BlendResult::points is empty.
 *
 * Refused: a source with no usable entry; a non-positive interval, one that is
 * not a whole number of minutes, or one over 24 h (the INP writer stores
 * `h:mm`, so anything else cannot round-trip); a grid that would exceed
 * \ref kMaxGridCells; and sources whose time spans are disjoint by more than a
 * year, which is the signature of a relative-hours series mixed with
 * absolute-dated ones.
 */
[[nodiscard]] BlendResult blend(const QVector<SourceGage> &sources,
                                const QVector<double> &weights);

} // namespace GageBlend

#endif // OPENSWMMVIS_CORE_GAGEBLEND_H
