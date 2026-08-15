/*!
 * \file   xsectsampler.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/sectionview/xsectsampler.h"

#include <openswmm/engine/openswmm_xsect.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace openswmmvis::sectionview {

namespace {

//! Engine handles are `void*`; keep the cast in one place.
inline SWMM_XSect h(void *p) { return static_cast<SWMM_XSect>(p); }

inline int unitCode(bool si) { return si ? SWMM_UNITS_SI : SWMM_UNITS_US; }

} // namespace

XsectSampler::~XsectSampler()
{
    reset();
}

XsectSampler::XsectSampler(XsectSampler &&other) noexcept
    : m_handle(std::exchange(other.m_handle, nullptr))
{
}

XsectSampler &XsectSampler::operator=(XsectSampler &&other) noexcept
{
    if (this != &other) {
        reset();
        m_handle = std::exchange(other.m_handle, nullptr);
    }
    return *this;
}

void XsectSampler::reset() noexcept
{
    if (m_handle) {
        swmm_xsect_free(h(m_handle));
        m_handle = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

XsectSampler XsectSampler::fromShape(int shape, double geom1, double geom2,
                                     double geom3, double geom4, bool si)
{
    SWMM_XSect x = nullptr;
    if (swmm_xsect_create(shape, geom1, geom2, geom3, geom4,
                          unitCode(si), &x) != SWMM_OK)
        return {};
    return XsectSampler(x);
}

XsectSampler XsectSampler::fromLink(SWMM_Engine engine, int linkIdx)
{
    if (!engine || linkIdx < 0) return {};
    SWMM_XSect x = nullptr;
    // Returns SWMM_ERR_LIFECYCLE while the model is still BUILDING, and
    // SWMM_ERR_BADPARAM for links with no cross-section (pumps). Both are
    // ordinary "nothing to draw" outcomes here, not errors worth logging.
    if (swmm_link_create_xsect(engine, linkIdx, &x) != SWMM_OK)
        return {};
    return XsectSampler(x);
}

XsectSampler XsectSampler::fromTransect(const QVector<double> &stations,
                                        const QVector<double> &elevations,
                                        double xLeftBank, double xRightBank,
                                        double nLeft, double nChannel,
                                        double nRight, double lengthFactor,
                                        bool si)
{
    const int n = static_cast<int>(std::min(stations.size(), elevations.size()));
    if (n < 2 || nChannel <= 0.0) return {};

    SWMM_XSect x = nullptr;
    if (swmm_xsect_create_irregular(stations.constData(), elevations.constData(),
                                    n, xLeftBank, xRightBank,
                                    nLeft, nChannel, nRight, lengthFactor,
                                    unitCode(si), &x) != SWMM_OK)
        return {};
    return XsectSampler(x);
}

XsectSampler XsectSampler::fromStreet(double width, double curbHeight,
                                      double slope, double roughness,
                                      double gutterDepression,
                                      double gutterWidth, int sides,
                                      double backWidth, double backSlope,
                                      double backRoughness, bool si)
{
    SWMM_XSect x = nullptr;
    if (swmm_xsect_create_street(width, curbHeight, slope, roughness,
                                 gutterDepression, gutterWidth, sides,
                                 backWidth, backSlope, backRoughness,
                                 unitCode(si), &x) != SWMM_OK)
        return {};
    return XsectSampler(x);
}

XsectSampler XsectSampler::fromCurve(double yFull,
                                     const QVector<double> &normDepths,
                                     const QVector<double> &normWidths, bool si)
{
    const int n = static_cast<int>(std::min(normDepths.size(), normWidths.size()));
    if (n < 2 || yFull <= 0.0) return {};

    SWMM_XSect x = nullptr;
    if (swmm_xsect_create_custom(yFull, normDepths.constData(),
                                 normWidths.constData(), n,
                                 unitCode(si), &x) != SWMM_OK)
        return {};
    return XsectSampler(x);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

int XsectSampler::shape() const
{
    if (!m_handle) return -1;
    int s = -1;
    if (swmm_xsect_get_shape(h(m_handle), &s) != SWMM_OK) return -1;
    return s;
}

XsectFullProps XsectSampler::fullProps() const
{
    XsectFullProps p;
    if (!m_handle) return p;

    if (swmm_xsect_full_properties(h(m_handle), &p.yFull, &p.aFull, &p.rFull,
                                   &p.wMax, &p.sFull, &p.aMax) != SWMM_OK)
        return XsectFullProps{};

    int isOpen = 0;
    if (swmm_xsect_is_open(h(m_handle), &isOpen) == SWMM_OK)
        p.open = (isOpen != 0);
    return p;
}

QVector<double> XsectSampler::widthsAtDepths(const QVector<double> &depths) const
{
    if (!m_handle || depths.isEmpty()) return {};

    QVector<double> widths(depths.size(), 0.0);
    // A single out-of-domain input fails the whole call, so the caller's
    // depth ladder must already be finite and non-negative — outline() below
    // builds it from yFull, which fullProps() guarantees.
    if (swmm_xsect_width_of_depth_array(h(m_handle), depths.constData(),
                                        static_cast<int>(depths.size()),
                                        widths.data()) != SWMM_OK)
        return {};
    return widths;
}

QPolygonF XsectSampler::outline(int samples) const
{
    if (!m_handle) return {};

    const XsectFullProps p = fullProps();
    if (!(p.yFull > 0.0) || !std::isfinite(p.yFull))
        return {};   // DUMMY, or a degenerate section — nothing to draw.

    const int n = std::clamp(samples, 8, 512);

    QVector<double> depths(n + 1);
    for (int i = 0; i <= n; ++i)
        depths[i] = p.yFull * static_cast<double>(i) / static_cast<double>(n);

    const QVector<double> widths = widthsAtDepths(depths);
    if (widths.size() != depths.size()) return {};

    // Right half bottom→top, then the mirrored left half top→bottom, so the
    // result is a single closed ring in CCW order with y measured up from the
    // invert.
    QPolygonF poly;
    poly.reserve(2 * (n + 1));
    for (int i = 0; i <= n; ++i)
        poly << QPointF(0.5 * widths[i], depths[i]);
    for (int i = n; i >= 0; --i)
        poly << QPointF(-0.5 * widths[i], depths[i]);

    return poly;
}

} // namespace openswmmvis::sectionview
