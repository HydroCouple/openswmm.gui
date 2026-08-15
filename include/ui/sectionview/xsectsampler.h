/*!
 * \file   xsectsampler.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  RAII wrapper over the engine's standalone cross-section API.
 *
 * Slice SP.1 (workplans/SECTION_PREVIEW_WORKPLAN.md).
 *
 * The engine ships a complete geometry API in openswmm_xsect.h that the GUI
 * had never consumed: an opaque SWMM_XSect handle plus width / area /
 * hydraulic-radius queries. Sampling `swmm_xsect_width_of_depth_array()` over
 * a depth ladder yields the exact outline the solver itself uses, for every
 * SWMM_XSectShape — which is what the section-preview panels draw instead of
 * the hand-drawn SVG thumbnails.
 *
 * Shape numbering: openswmm_xsect.h takes and returns SWMM_XSectShape codes
 * from openswmm_links.h — the SAME numbering space xsectshapegeom.h uses.
 * There is no second numbering space and no mapping table. Still spell shape
 * ids as SWMM_XSECT_* constants, never bare integers (see the drift warning
 * on XsectShapeRow in ui/properties/xsectshapegeom.h).
 */

#ifndef OPENSWMMVIS_SECTIONVIEW_XSECTSAMPLER_H
#define OPENSWMMVIS_SECTIONVIEW_XSECTSAMPLER_H

#include <QPolygonF>
#include <QVector>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_links.h>

namespace openswmmvis::sectionview {

/*! Bank-full properties, as reported by swmm_xsect_full_properties(). */
struct XsectFullProps
{
    double yFull = 0.0;   //!< Full depth.
    double aFull = 0.0;   //!< Area when full.
    double rFull = 0.0;   //!< Hydraulic radius when full.
    double wMax  = 0.0;   //!< Width at the widest point.
    double sFull = 0.0;   //!< Section factor when full.
    double aMax  = 0.0;   //!< Area of maximum flow.
    bool   open  = false; //!< True for an open channel.
};

/*!
 * \class XsectSampler
 * \brief Owning handle on one standalone SWMM cross-section.
 *
 * Move-only (the engine handle is not refcounted). A default-constructed or
 * failed sampler is `!isValid()` and every query returns a zeroed result, so
 * callers can build one unconditionally and let the empty state fall through
 * to the widget's "no geometry" message.
 */
class XsectSampler
{
public:
    XsectSampler() = default;
    ~XsectSampler();

    XsectSampler(XsectSampler &&other) noexcept;
    XsectSampler &operator=(XsectSampler &&other) noexcept;
    XsectSampler(const XsectSampler &) = delete;
    XsectSampler &operator=(const XsectSampler &) = delete;

    // ---- Construction ------------------------------------------------------

    /*! Self-contained shapes (everything except IRREGULAR / CUSTOM / STREET).
     *  \param shape  A SWMM_XSectShape code.
     *  \param si     true → metres/CMS, false → feet/CFS. */
    [[nodiscard]] static XsectSampler fromShape(int shape, double geom1,
                                                double geom2, double geom3,
                                                double geom4, bool si);

    /*! Deep-copies the geometry the engine actually built for a link,
     *  including transect / shape-curve / street tables — the only way to get
     *  IRREGULAR / CUSTOM / STREET geometry without re-supplying the tables.
     *
     *  \note The engine returns SWMM_ERR_LIFECYCLE while the model is still in
     *        the BUILDING state (geometry not resolved yet), so this yields an
     *        invalid sampler during programmatic model construction. Callers
     *        that have the tables to hand should prefer fromTransect() /
     *        fromStreet(), which work in every lifecycle state. */
    [[nodiscard]] static XsectSampler fromLink(SWMM_Engine engine, int linkIdx);

    /*! Irregular (natural channel) section from raw transect data.
     *  Roughness values are load-bearing: the hydraulic-radius table is
     *  conveyance-weighted across the overbank / channel subsections. */
    [[nodiscard]] static XsectSampler fromTransect(
        const QVector<double> &stations, const QVector<double> &elevations,
        double xLeftBank, double xRightBank,
        double nLeft, double nChannel, double nRight,
        double lengthFactor, bool si);

    /*! Street section from [STREETS] parameters (slopes in percent). */
    [[nodiscard]] static XsectSampler fromStreet(
        double width, double curbHeight, double slope, double roughness,
        double gutterDepression, double gutterWidth, int sides,
        double backWidth, double backSlope, double backRoughness, bool si);

    /*! Custom section from a normalized SHAPE curve (depths and widths are
     *  y/yFull and w/wMax in [0,1]). */
    [[nodiscard]] static XsectSampler fromCurve(
        double yFull, const QVector<double> &normDepths,
        const QVector<double> &normWidths, bool si);

    // ---- Queries -----------------------------------------------------------

    [[nodiscard]] bool isValid() const noexcept { return m_handle != nullptr; }

    /*! SWMM_XSectShape code, or -1 when invalid. */
    [[nodiscard]] int shape() const;

    /*! Bank-full properties; all zeros when invalid. */
    [[nodiscard]] XsectFullProps fullProps() const;

    /*! Width at each depth (one output per input). Empty on failure. */
    [[nodiscard]] QVector<double> widthsAtDepths(const QVector<double> &depths) const;

    /*!
     * \brief Closed outline polygon of the section, in section coordinates.
     *
     * The polygon is centred on x = 0 with y measured UP from the invert
     * (y = 0 at the invert, y = yFull at the crown / top of bank), i.e. the
     * natural coordinate system for a section drawing — the painter flips Y.
     *
     * Built by sampling half-width over `samples + 1` evenly spaced depths and
     * mirroring, so it reproduces the engine's own geometry for every shape,
     * including the tabulated ones.
     *
     * \param samples  Depth intervals; more samples smooth curved crowns.
     *                 Clamped to [8, 512].
     * \returns An empty polygon when the sampler is invalid or the section has
     *          no geometry (SWMM_XSECT_DUMMY, or a degenerate depth).
     */
    [[nodiscard]] QPolygonF outline(int samples = 96) const;

private:
    explicit XsectSampler(void *handle) noexcept : m_handle(handle) {}
    void reset() noexcept;

    void *m_handle = nullptr;   //!< SWMM_XSect (opaque void*).
};

} // namespace openswmmvis::sectionview

#endif // OPENSWMMVIS_SECTIONVIEW_XSECTSAMPLER_H
