/*!
 * \file   mapextent.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 * \pre
 * \bug
 * \warning
 * \todo
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
     */
    MapExtent() = default;

    /*!
     * \brief Constructs an extent from explicit coordinates.
     * \param xMin  Left / west boundary.
     * \param yMin  Bottom / south boundary.
     * \param xMax  Right / east boundary.
     * \param yMax  Top / north boundary.
     */
    MapExtent(double xMin, double yMin, double xMax, double yMax);

    /*!
     * \brief Constructs an extent from a QRectF (x-left, y-bottom, width, height).
     */
    explicit MapExtent(const QRectF &rect);

    // ----- Accessors -------------------------------------------------------

    [[nodiscard]] double xMin() const { return m_xMin; }
    [[nodiscard]] double yMin() const { return m_yMin; }
    [[nodiscard]] double xMax() const { return m_xMax; }
    [[nodiscard]] double yMax() const { return m_yMax; }

    [[nodiscard]] double width()  const { return m_xMax - m_xMin; }
    [[nodiscard]] double height() const { return m_yMax - m_yMin; }
    [[nodiscard]] double centerX() const { return (m_xMin + m_xMax) * 0.5; }
    [[nodiscard]] double centerY() const { return (m_yMin + m_yMax) * 0.5; }

    /*!
     * \brief Returns true when all coordinates are finite and xMin < xMax and yMin < yMax.
     */
    [[nodiscard]] bool isValid() const;

    /*!
     * \brief Returns true when this extent is equal to a default-constructed one.
     */
    [[nodiscard]] bool isNull() const;

    // ----- Conversion ------------------------------------------------------

    /*!
     * \brief Returns a QRectF(xMin, yMin, width, height) representation.
     */
    [[nodiscard]] QRectF toRectF() const;

    /*!
     * \brief Returns a human-readable "[xMin, yMin] — [xMax, yMax]" string.
     */
    [[nodiscard]] QString toString() const;

    // ----- Mutators --------------------------------------------------------

    void setXMin(double v) { m_xMin = v; }
    void setYMin(double v) { m_yMin = v; }
    void setXMax(double v) { m_xMax = v; }
    void setYMax(double v) { m_yMax = v; }

    /*!
     * \brief Expands this extent to include the point \p (x, y).
     */
    void expandToInclude(double x, double y);

    /*!
     * \brief Expands this extent to include all of \p other.
     */
    void expandToInclude(const MapExtent &other);

    /*!
     * \brief Grows the extent by \p factor on all sides (relative to centre).
     * \param factor  e.g. 0.1 for 10 % padding on each side.
     */
    void growByFactor(double factor);

    /*!
     * \brief Returns a new extent scaled by \p factor around the centre point.
     */
    [[nodiscard]] MapExtent scaled(double factor) const;

    /*!
     * \brief Returns a new extent panned by the given delta values.
     */
    [[nodiscard]] MapExtent panned(double dx, double dy) const;

    /*!
     * \brief Returns the intersection of this extent and \p other.
     *        Returns a null extent if they do not overlap.
     */
    [[nodiscard]] MapExtent intersected(const MapExtent &other) const;

    /*!
     * \brief Returns an extent that exactly contains both this and \p other.
     */
    [[nodiscard]] MapExtent united(const MapExtent &other) const;

    /*!
     * \brief Returns true when this extent contains the given point.
     */
    [[nodiscard]] bool contains(double x, double y) const;

    /*!
     * \brief Returns true when this extent fully contains \p other.
     */
    [[nodiscard]] bool contains(const MapExtent &other) const;

    /*!
     * \brief Returns true when this extent intersects (overlaps) \p other.
     */
    [[nodiscard]] bool intersects(const MapExtent &other) const;

    // ----- Operators -------------------------------------------------------

    bool operator==(const MapExtent &other) const;
    bool operator!=(const MapExtent &other) const;

private:
    double m_xMin = 0.0;
    double m_yMin = 0.0;
    double m_xMax = 0.0;
    double m_yMax = 0.0;
};

#endif // MAPEXTENT_H
