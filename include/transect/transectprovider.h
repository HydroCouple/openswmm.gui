/*!
 * \file   transectprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.4 — MVC model for a single SWMM transect.
 *
 * One TransectProvider per `[TRANSECTS]` entry in the project. Owned by a
 * project-scoped TransectRegistry. Single source of truth: the editor's
 * list pane, property panel rows (QPropertyModel), station-elevation grid,
 * and the cross-section chart all bind to the same provider and refresh
 * through its Qt signals ([[feedback_mvc_synchronized_uis]]).
 *
 * Storage invariant: station-elevation points are **strictly ascending in
 * station** (X). Every public mutator validates the post-mutation state and
 * **refuses** any operation that would violate it — emitting
 * `mutationRejected(reason)` so the caller can surface the rejection. Bulk
 * operations are atomic: either all points are updated, or none.
 *
 * Mirrors `CurveProvider` (Phase 6.7.1) in shape; the additional surface
 * area covers Manning's roughness triple, bank stations, encroachment
 * stations (BQ-TR-02 Reading B), modifier triple (xFactor / yFactor /
 * lengthFactor), and comments.
 */
#ifndef OPENSWMMVIS_TRANSECT_TRANSECTPROVIDER_H
#define OPENSWMMVIS_TRANSECT_TRANSECTPROVIDER_H

#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

namespace openswmmvis::transect {

/*! \brief One (station, elevation) sample on a transect cross-section. */
struct TransectPoint {
    double station   = 0.0;
    double elevation = 0.0;
};

class TransectProvider : public QObject
{
    Q_OBJECT

public:
    explicit TransectProvider(QString name, QObject *parent = nullptr);
    ~TransectProvider() override;

    // ── Identity ────────────────────────────────────────────────────────────

    QString name()     const noexcept { return m_name; }
    QString comments() const noexcept { return m_comments; }

    // ── Roughness (Manning's n triple) ──────────────────────────────────────

    double nLeftBank()  const noexcept { return m_nLeft; }
    double nRightBank() const noexcept { return m_nRight; }
    double nChannel()   const noexcept { return m_nChannel; }

    // ── Bank stations (split overbank ↔ channel) ────────────────────────────

    double xLeftBank()  const noexcept { return m_xLeftBank; }
    double xRightBank() const noexcept { return m_xRightBank; }

    // ── Encroachment stations (BQ-TR-02 Reading B) ──────────────────────────

    double xLeftEncroachment()  const noexcept { return m_xLeftEncroach; }
    double xRightEncroachment() const noexcept { return m_xRightEncroach; }

    // ── Modifiers (X1 record cols 8-9 + lengthFactor) ───────────────────────

    double stationMultiplier() const noexcept { return m_xFactor; }
    double elevationOffset()   const noexcept { return m_yFactor; }
    double meanderFactor()     const noexcept { return m_lengthFactor; }

    // ── Station-elevation points ────────────────────────────────────────────

    int   pointCount() const noexcept { return m_points.size(); }
    const TransectPoint& pointAt(int i) const { return m_points.at(i); }
    const QVector<TransectPoint>& points() const noexcept { return m_points; }
    QVector<QPair<double,double>> allPoints() const;

    // ── Mutators (return false + emit mutationRejected on invariant violation) ──

    void setName(QString newName);
    void setComments(QString newComments);

    void setRoughness(double nLeft, double nRight, double nChannel);
    void setBankStations(double xLeft, double xRight);
    void setEncroachmentStations(double xLeft, double xRight);
    void setModifiers(double xFactor, double yFactor, double lengthFactor);

    /*! \brief Replace the entire station list. Validates strict-monotone
     *  stations before applying. Atomic. */
    bool setAllPoints(QVector<TransectPoint> newPoints, QString *reasonOut = nullptr);

    /*! \brief Edit elevation at index \p i; station stays put. */
    bool setElevationAt(int i, double newElev, QString *reasonOut = nullptr);

    /*! \brief Edit both station and elevation at index \p i. Validates that
     *  the new station still slots strictly between neighbours. */
    bool setPointAt(int i, double newStation, double newElev,
                     QString *reasonOut = nullptr);

    /*! \brief Live drag variant: skips strict monotonicity by clamping the
     *  new station to lie in (prev + eps, next - eps). When the value was
     *  clamped, \p clamped is set true. */
    bool setPointLive(int i, double newStation, double newElev,
                       bool *clamped = nullptr);

    /*! \brief Insert one point. Returns the new index on success, -1 on
     *  collision / out-of-order. */
    int insertPoint(double station, double elev, QString *reasonOut = nullptr);

    /*! \brief Remove points by index. Indices are deduplicated + sorted
     *  descending internally so the loop is safe. */
    void removePointsAt(QVector<int> indices);

signals:
    void nameChanged(QString prev, QString now);
    void commentsChanged();
    void roughnessChanged();
    void bankStationsChanged();
    void encroachmentStationsChanged();
    void modifiersChanged();

    void pointsChanged(int firstIndex, int count);
    void pointsInserted(int at, int count);
    void pointsRemoved(int at, int count);

    void mutationRejected(QString reason);

private:
    /*! \brief True iff \p pts is strictly ascending in station. */
    static bool isStrictlyMonotone_(const QVector<TransectPoint> &pts);

    QString m_name;
    QString m_comments;

    double m_nLeft       = 0.030;
    double m_nRight      = 0.030;
    double m_nChannel    = 0.030;

    double m_xLeftBank   = 0.0;
    double m_xRightBank  = 0.0;

    double m_xLeftEncroach  = 0.0;
    double m_xRightEncroach = 0.0;

    double m_xFactor      = 1.0;
    double m_yFactor      = 0.0;
    double m_lengthFactor = 1.0;

    QVector<TransectPoint> m_points;
};

} // namespace openswmmvis::transect

Q_DECLARE_METATYPE(openswmmvis::transect::TransectPoint)

#endif // OPENSWMMVIS_TRANSECT_TRANSECTPROVIDER_H
