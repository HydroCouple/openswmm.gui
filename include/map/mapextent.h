/**
 * @file      mapextent.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Axis-aligned bounding box in a map coordinate system, used by
 *         MapCanvas and all layer types to define spatial extents.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MAPEXTENT_H
#define MAPEXTENT_H

#include <QRectF>
#include <QString>

class SpatialReferenceSystem;

/*!
 * \class MapExtent
 * \brief An axis-aligned bounding box in the coordinate space of a given CRS.
 * \details Stores the geographic or projected envelope used by the MapCanvas to
 *          define the visible area and by layers to report their spatial extent.
 *          All coordinate values are in the units of the associated CRS.
 */
class MapExtent
{
public:
    /*!
     * \brief Constructs a default (null) extent.
     * @note Initializes all four coordinates exactly zero. No CRS object or unit metadata is
     *   retained; the caller keeps every compared/combined extent in the same map frame. Default is
     *   also a real origin point, so null sentinel cannot distinguish it.
     */
    MapExtent() = default;

    /*!
     * \brief Constructs an extent from explicit coordinates.
     * \param xMin  Left / west boundary.
     * \param yMin  Bottom / south boundary.
     * \param xMax  Right / east boundary.
     * \param yMax  Top / north boundary.
     * @note Copies coordinates verbatim without ordering/finiteness validation.
     *   Degenerate/inverted/NaN extents can be represented even though isValid() rejects them. No
     *   scene Y inversion or CRS conversion.
     */
    MapExtent(double xMin, double yMin, double xMax, double yMax);

    /*!
     * \brief Constructs an extent from a QRectF (x-left, y-bottom, width, height).
     * @param[in] rect Rectangle whose left/top/right/bottom values are copied directly; top
     *   supplies numeric yMin, bottom supplies yMax.
     * @note Does not normalize negative dimensions or invert Qt scene Y. Caller must distinguish
     *   map axes from downward scene axes.
     */
    explicit MapExtent(const QRectF &rect);

    // ----- Accessors -------------------------------------------------------

    /**
     * @return Raw stored minimum X, not automatically normalized.
     */
    [[nodiscard]] double xMin() const { return m_xMin; }
    /**
     * @return Raw stored minimum Y, not Qt scene top conversion.
     */
    [[nodiscard]] double yMin() const { return m_yMin; }
    /**
     * @return Raw stored maximum X.
     */
    [[nodiscard]] double xMax() const { return m_xMax; }
    /**
     * @return Raw stored maximum Y.
     */
    [[nodiscard]] double yMax() const { return m_yMax; }

    /**
     * @return Raw xMax-xMin in caller map units; can be zero/negative/nonfinite.
     */
    [[nodiscard]] double width() const { return m_xMax - m_xMin; }
    /**
     * @return Raw yMax-yMin in caller map units; can be zero/negative/nonfinite.
     */
    [[nodiscard]] double height() const { return m_yMax - m_yMin; }
    /**
     * @return Arithmetic half-sum of X bounds; no validity/overflow guard.
     */
    [[nodiscard]] double centerX() const { return (m_xMin + m_xMax) * 0.5; }
    /**
     * @return Arithmetic half-sum of Y bounds; no validity/overflow guard.
     */
    [[nodiscard]] double centerY() const { return (m_yMin + m_yMax) * 0.5; }

    /*!
     * \brief Returns true when all coordinates are finite and xMin < xMax and yMin < yMax.
     * @return Whether all bounds finite and BOTH dimensions strictly positive; finite point/line
     *   extents are invalid.
     */
    [[nodiscard]] bool isValid() const;

    /*!
     * \brief Returns true when this extent is equal to a default-constructed one.
     * @return Whether all four bounds compare exactly zero, including negative zero;
     *   NaN/inverted/nonorigin-degenerate is not null.
     */
    [[nodiscard]] bool isNull() const;

    // ----- Conversion ------------------------------------------------------

    /*!
     * \brief Returns a QRectF(xMin, yMin, width, height) representation.
     * @return QRectF(xMin, yMin, width, height) value with raw dimensions; no Y
     *   inversion/normalization.
     */
    [[nodiscard]] QRectF toRectF() const;

    /*!
     * \brief Returns a human-readable "[xMin, yMin] — [xMax, yMax]" string.
     * @return Owned display text formatted by QString::arg default numeric conventions, preserving
     *   raw bounds including nonfinite values.
     */
    [[nodiscard]] QString toString() const;

    // ----- Mutators --------------------------------------------------------

    /**
     * @param[in] v Raw new bound in same caller map units, copied without reordering/finite checks
     *   or notification.
     */
    void setXMin(double v) { m_xMin = v; }
    /**
     * @param[in] v Raw new bound in same caller map units, copied without reordering/finite checks
     *   or notification.
     */
    void setYMin(double v) { m_yMin = v; }
    /**
     * @param[in] v Raw new bound in same caller map units, copied without reordering/finite checks
     *   or notification.
     */
    void setXMax(double v) { m_xMax = v; }
    /**
     * @param[in] v Raw new bound in same caller map units, copied without reordering/finite checks
     *   or notification.
     */
    void setYMax(double v) { m_yMax = v; }

    /*!
     * \brief Expands this extent to include the point \p (x, y).
     * @param[in] x, y Point in same map CRS/units; finite values expected.
     * @note All-zero/null extent is REPLACED by point, otherwise component min/max comparisons
     *   expand it. Therefore expanding origin point then another point can lose origin due null
     *   ambiguity. NaN inputs compare false except null initialization; no general validity repair.
     */
    void expandToInclude(double x, double y);

    /*!
     * \brief Expands this extent to include all of \p other.
     * @param[in] other Extent in same CRS/units, required finite and strictly positive both
     *   dimensions.
     * @note Invalid/point/line other ignored. Valid other contributes opposite corners through
     *   point overload; null-sentinel semantics still apply. No CRS metadata validation.
     */
    void expandToInclude(const MapExtent &other);

    /*!
     * \brief Grows the extent by \p factor on all sides (relative to centre).
     * \param factor  e.g. 0.1 for 10 % padding on each side.
     * @note Adds factor*current width/height to EACH side, so total dimensions
     *   multiply(1+2*factor). No finite/range clamp; negative shrinks, below minus 0.5 inverts.
     *   Degenerate axes remain degenerate; no validity requirement.
     */
    void growByFactor(double factor);

    /*!
     * \brief Returns a new extent scaled by \p factor around the centre point.
     * @param[in] factor Requested whole width/height multiplier about current center; no
     *   finite/nonnegative check.
     * @return New extent with dimensions multiplied by factor; zero collapses, negative reverses
     *   bounds. Original unchanged, no CRS conversion.
     */
    [[nodiscard]] MapExtent scaled(double factor) const;

    /*!
     * \brief Returns a new extent panned by the given delta values.
     * @param[in] dx Added X displacement in caller map units.
     * @param[in] dy Added Y displacement in caller map units.
     * @return New bounds translated verbatim, preserving size/invalidity aside from arithmetic;
     *   original unchanged.
     */
    [[nodiscard]] MapExtent panned(double dx, double dy) const;

    /*!
     * \brief Returns the intersection of this extent and \p other.
     *        Returns a null extent if they do not overlap.
     * @param[in] other Other raw bounds in identical caller coordinate frame/units.
     * @return Component-wise overlap only if both resulting dimensions strictly positive;
     *   edge/point contact returns default zero/null. Inputs are not validated, NaN behavior
     *   follows std::min/max/comparisons.
     */
    [[nodiscard]] MapExtent intersected(const MapExtent &other) const;

    /*!
     * \brief Returns an extent that exactly contains both this and \p other.
     * @param[in] other Other raw bounds in identical caller coordinate frame/units.
     * @return Raw min/max envelope of both inputs, WITHOUT null/invalid special cases; union with
     *   default includes origin. Inputs must share CRS/units.
     */
    [[nodiscard]] MapExtent united(const MapExtent &other) const;

    /*!
     * \brief Returns true when this extent contains the given point.
     * @param[in] x, y Point in same map frame/units.
     * @return Inclusive min<=point<=max test, without isValid prerequisite; NaN point/bound
     *   normally rejects via comparisons.
     */
    [[nodiscard]] bool contains(double x, double y) const;

    /*!
     * \brief Returns true when this extent fully contains \p other.
     * @param[in] other Other raw bounds in identical caller coordinate frame/units.
     * @return Inclusive bounds comparison of all four other coordinates, without validity/ordering
     *   checks; degenerate extents can be contained.
     */
    [[nodiscard]] bool contains(const MapExtent &other) const;

    /*!
     * \brief Returns true when this extent intersects (overlaps) \p other.
     * @param[in] other Other raw bounds in identical caller coordinate frame/units.
     * @return Inclusive separating-axis result; edge/point contact counts. Invalid bounds not
     *   rejected and NaN can produce true because comparisons fail.
     */
    [[nodiscard]] bool intersects(const MapExtent &other) const;

    // ----- Operators -------------------------------------------------------

    /**
     * @param[in] other Other raw bounds in identical caller coordinate frame/units.
     * @return Exact component equality, no tolerance/CRS check; any NaN coordinate makes equality
     *   false even to itself.
     */
    bool operator==(const MapExtent &other) const;
    /**
     * @param[in] other Other raw bounds in identical caller coordinate frame/units.
     * @return Negation of exact component equality, including NaN inequality.
     */
    bool operator!=(const MapExtent &other) const;

private:
    double m_xMin = 0.0;
    double m_yMin = 0.0;
    double m_xMax = 0.0;
    double m_yMax = 0.0;
};

#endif // MAPEXTENT_H
