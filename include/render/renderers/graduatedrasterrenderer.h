/*!
 * \file   graduatedrasterrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Raster renderer driven by the shared ClassificationScheme —
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
 *           - clipOutOfRange    — values outside the effective range render
 *                                 transparent instead of clamping to the end
 *                                 colours (QGIS "clip out of range values")
 *
 *         Hot path: colorForValue() is pure arithmetic over state baked by
 *         rebake() — a 256-entry LUT for Continuous mode (ScalarRampLut
 *         contract) and one QRgb per class for Classified mode. Every
 *         mutator rebakes eagerly, so the clone() handed to a tile worker is
 *         immutable: no lazy caches, no locking, and no ClassificationScheme
 *         call per pixel (the scheme's colour lookups build a RasterColorRamp
 *         on every call).
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
    ~GraduatedRasterRenderer() override = default;

    // ── Scheme ─────────────────────────────────────────────────────────
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    /*! Replaces the scheme and re-derives the edges over the data range
     *  WITHOUT samples — data-driven methods (Quantile / NaturalBreaks /
     *  StdDev) degrade to equal spacing until reclassify() is called with a
     *  value sample. Hosts that hold a sample call reclassify() right after. */
    void setScheme(const ClassificationScheme &scheme);

    // ── Data range ─────────────────────────────────────────────────────
    [[nodiscard]] double dataMin() const { return m_dataMin; }
    [[nodiscard]] double dataMax() const { return m_dataMax; }
    /*! Band statistics; re-derives the edges (no samples, see setScheme). */
    void setDataRange(double dataMin, double dataMax);

    /*! (lo, hi) actually classified / stretched over: the scheme's custom
     *  range when enabled and non-degenerate, otherwise (dataMin, dataMax). */
    [[nodiscard]] QPair<double, double> effectiveRange() const { return { m_lo, m_hi }; }

    // ── Out-of-range policy ────────────────────────────────────────────
    [[nodiscard]] bool clipOutOfRange() const { return m_clipOutOfRange; }
    void setClipOutOfRange(bool on);

    // ── Classification ─────────────────────────────────────────────────
    /*! Re-derive the class edges from the scheme over the effective range,
     *  feeding \p samples to the data-driven methods. */
    void reclassify(const QVector<double> &samples = {});

    /*! Class edges — N+1 ascending values inclusive of both range endpoints
     *  (for Manual: in-range breaks + 2). Empty when the range is degenerate. */
    [[nodiscard]] const QVector<double> &edges() const { return m_edges; }
    [[nodiscard]] int classCount() const { return std::max(0, int(m_edges.size()) - 1); }

    /*! Rows emitted for the Continuous-mode legend (sampled lo → hi). */
    static constexpr int kContinuousLegendRows = 6;

    // IRasterRenderer.
    [[nodiscard]] QString rendererId() const override
    {
        return QStringLiteral("graduatedraster");
    }
    [[nodiscard]] QColor colorForValue(double value,
                                       bool isNoData = false) const override;
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;
    [[nodiscard]] std::unique_ptr<IRasterRenderer> clone() const override;

private:
    /*! Bake the effective range, the Continuous LUT and the per-class
     *  colours from the current scheme / range / edges. */
    void rebake();

    ClassificationScheme m_scheme;
    double               m_dataMin = 0.0;
    double               m_dataMax = 1.0;
    QVector<double>      m_edges;
    bool                 m_clipOutOfRange = false;

    // Baked by rebake() — read-only on the paint path.
    double                                m_lo = 0.0;
    double                                m_hi = 1.0;
    std::array<QRgb, ScalarRampLut::kSize> m_lut{};
    QVector<QRgb>                         m_classColors;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_GRADUATEDRASTERRENDERER_H
