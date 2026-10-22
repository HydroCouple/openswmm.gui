/**
 * @file      graduatedrasterrenderer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Raster renderer driven by the shared ClassificationScheme —
 *         continuous (stretched) or classified single-band colouring.
 *
 *         The raster analogue of GraduatedRenderer on the feature side and
 *         the model behind the raster Symbology tab's "Singleband
 *         pseudocolor" renderer. Everything the shared ClassificationEditor
 *         edits (Continuous/Classified mode, ramp + invert, method, class
 *         count, manual breaks, custom range, per-class colour / label
 *         overrides, label format) lives in the embedded ClassificationScheme;
 *         this class adds what a raster needs on top:
 *           - dataMin / dataMax — the band statistics the scheme classifies
 *                                 over (the scheme's custom range wins when
 *                                 enabled — see effectiveRange())
 *           - edges             — the class edges last derived from the
 *                                 scheme + a value sample. Persisted, so a
 *                                 Quantile / Jenks classification reloads
 *                                 from .oswp without re-sampling the raster
 *           - clipOutOfRange    — legacy serialized flag, retained for round trips.
 *                                 Numeric underflow is always transparent;
 *                                 overflow always uses the final color.
 *
 *         Hot path: colorForValue() is pure arithmetic over state baked by
 *         rebake() — a 256-entry LUT for Continuous mode (ScalarRampLut
 *         contract) and one QRgb per class for Classified mode. Every
 *         range/scheme/classification mutator rebakes eagerly, so the clone() handed to a tile worker is
 *         immutable: no lazy caches, no locking, and no ClassificationScheme
 *         call per pixel (the scheme's colour lookups build a RasterColorRamp
 *         on every call).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_GRADUATEDRASTERRENDERER_H
#define OPENSWMM_RENDER_GRADUATEDRASTERRENDERER_H

#include "render/classificationscheme.h"
#include "render/irasterrenderer.h"
#include "render/scalarramplut.h"

#include <QColor>
#include <QPair>
#include <QRgb>
#include <QVector>

#include <algorithm>
#include <array>

namespace OpenSWMM::Render
{

/*!
 * \class GraduatedRasterRenderer
 * \brief ClassificationScheme-driven single-band raster colouring.
 */
class GraduatedRasterRenderer final : public IRasterRenderer
{
public:
    /*! Default: Continuous grayscale over [0, 1] — visually the historic
     *  raster default once setDataRange() is fed the band statistics. */
    GraduatedRasterRenderer();
    /** @brief Destroy the stored configuration and baked lookup tables. */
    ~GraduatedRasterRenderer() override = default;

    // ── Scheme ─────────────────────────────────────────────────────────
    /** @brief Inspect the embedded classification configuration.
     * @return Borrowed scheme valid for this renderer's lifetime; replacement/loading changes it. */
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    /*! Replaces the scheme and re-derives the edges over the data range
     *  WITHOUT samples — data-driven methods classify the two range endpoints
     *  until reclassify() receives a sample. Hosts that hold a sample call it afterward.
     * @param scheme Configuration copied without an equality check. Cached edges
     * are recomputed and lookup tables rebuilt immediately; no notification is emitted. */
    void setScheme(const ClassificationScheme &scheme);

    // ── Data range ─────────────────────────────────────────────────────
    /** @brief Read the caller-supplied band-statistics minimum.
     * @return Stored scalar in raw band units, independent of custom range selection. */
    [[nodiscard]] double dataMin() const { return m_dataMin; }
    /** @brief Read the caller-supplied band-statistics maximum.
     * @return Stored scalar in the same band units as dataMin(). */
    [[nodiscard]] double dataMax() const { return m_dataMax; }
    /** @brief Store band statistics and reclassify without sample values.
     * @param dataMin Finite lower bound in raw band units.
     * @param dataMax Finite upper bound in the same units, expected at least dataMin.
     * Bounds are stored verbatim; scheme validation can produce no edges for
     * invalid bounds. Custom bounds take precedence without replacing these statistics. */
    void setDataRange(double dataMin, double dataMax);

    /** @brief Read the effective range cached by the last bake.
     * @return Custom bounds when enabled, otherwise band statistics, verbatim.
     * Invalid custom bounds do not fall back to automatic bounds. */
    [[nodiscard]] QPair<double, double> effectiveRange() const { return {m_lo, m_hi}; }

    // ── Legacy serialized policy ────────────────────────────────────────────
    /** @brief Read the persisted compatibility clipping flag.
     * @return Stored metadata; numeric underflow/overflow follow the fixed rendering policy. */
    [[nodiscard]] bool clipOutOfRange() const { return m_clipOutOfRange; }
    /** @brief Retain the legacy clipping flag without rebuilding tables.
     * @param on Metadata to store; sampling behavior is unchanged. */
    void setClipOutOfRange(bool on);

    // ── Classification ─────────────────────────────────────────────────
    /*! Re-derive the class edges from the scheme over the effective range,
     *  feeding \p samples to the data-driven methods.
     * @param samples Band-unit values, inspected without mutation. The scheme
     * filters finite values in range; an empty usable sample uses the endpoints.
     * Replaces cached edges and eagerly rebuilds continuous/classified colors.
     * No repaint notification or synchronization with worker reads is provided. */
    void reclassify(const QVector<double> &samples = {});

    /*! Class edges — N+1 ascending values inclusive of both range endpoints
     *  (for Manual: in-range breaks + 2). Empty when validation fails or the range is degenerate.
     * @return Borrowed finite nondecreasing edge vector, potentially containing
     * duplicate boundaries. Mutations/loading can replace it; saved edges may
     * differ from the scheme's configured count or bounds. */
    [[nodiscard]] const QVector<double> &edges() const { return m_edges; }
    /** @brief Count actual cached bands rather than the configured requested count.
     * @return max(0,edges().size()-1), including zero-width bands between duplicate edges. */
    [[nodiscard]] int classCount() const { return std::max(0, int(m_edges.size()) - 1); }

    /*! Rows emitted for the Continuous-mode legend (sampled lo → hi). */
    static constexpr int kContinuousLegendRows = 6;

    // IRasterRenderer.
    /** @brief Identify the persisted renderer type.
     * @return Exact token graduatedraster. */
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("graduatedraster"); }
    /** @brief Sample immutable baked colors for a raw raster-band value.
     * @param value Scalar in the finite effective bounds' units.
     * @param isNoData True to force transparent regardless of numeric value.
     * @return Transparent for no-data, nonfinite values or underflow. Continuous
     * mode, or an empty class-color table, samples the 256-entry clamped LUT;
     * otherwise cached edges select a class, with equality at an interior edge
     * assigned upward and overflow to the final class. The legacy clipping flag is ignored. */
    [[nodiscard]] QColor colorForValue(double value, bool isNoData = false) const override;
    /** @brief Build legend rows from the same baked tables used for pixels.
     * @return Six sampled reference rows in Continuous mode, otherwise one per
     * cached class with numeric bounds, index keys, and separate user-label
     * overrides. Classified mode with no edges returns no rows. */
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Serialize the scheme, band statistics, authoritative edges and clipping metadata.
     * @return Owned JSON snapshot; baked colors are recomputed rather than serialized. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Load persisted edges when usable, otherwise recompute from the scheme.
     * @param j Object whose id is ignored. Missing scheme/statistics retain current
     * values; missing clipping metadata becomes false. An edge list is accepted
     * when it has at least two finite nondecreasing converted numeric entries;
     * duplicate edges are legal and neither count nor endpoints are checked against
     * the scheme. Accepted edges are authoritative; other lists trigger reclassify().
     * Both paths eagerly rebuild color tables without emitting notifications. */
    void fromJson(const QJsonObject &j) override;
    /** @brief Copy configuration, cached edges and all baked colors for an independent consumer.
     * @return Sole ownership of a snapshot suitable for read-only worker use;
     * callers must still synchronize publication and later mutation themselves. */
    [[nodiscard]] std::unique_ptr<IRasterRenderer> clone() const override;

private:
    /*! Bake the effective range, the Continuous LUT and the per-class
     *  colours from the current scheme / range / edges.
     * Does not validate bounds, recompute edges, emit notifications or lock.
     * Color tables include scheme inversion and active sparse color overrides. */
    void rebake();

    ClassificationScheme m_scheme;
    double m_dataMin = 0.0;
    double m_dataMax = 1.0;
    QVector<double> m_edges;
    bool m_clipOutOfRange = false;

    // Baked by rebake() — read-only on the paint path.
    double m_lo = 0.0;
    double m_hi = 1.0;
    std::array<QRgb, ScalarRampLut::kSize> m_lut{};
    QVector<QRgb> m_classColors;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_GRADUATEDRASTERRENDERER_H
