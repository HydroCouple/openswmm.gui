/*!
 * \file   test_marchingtriangles.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice BJ.2-lite — unit tests for OpenSWMM::Contour::marchingTriangles.
 * Self-contained: header-only algorithm, no Qt widgets, no engine deps.
 */
#include "contour/marchingtriangles.h"

#include <QObject>
#include <QPointF>
#include <QTest>

#include <cmath>
#include <vector>

using OpenSWMM::Contour::IsoLineSegment;
using OpenSWMM::Contour::marchingTriangles;
using OpenSWMM::Contour::evenlySpacedLevels;
using OpenSWMM::Contour::evenlySpacedLevelsInclusive;
using OpenSWMM::Contour::IsoBandPolygon;
using OpenSWMM::Contour::marchingTrianglesIsobands;

namespace {

// Test handle = struct of {3 points, 3 values}; trivial extractor below.
struct Tri
{
    QPointF p0, p1, p2;
    double  v0, v1, v2;
};

const auto extract = [](const Tri &t,
                        QPointF &p0, QPointF &p1, QPointF &p2,
                        double  &v0, double  &v1, double  &v2)
{
    p0 = t.p0; p1 = t.p1; p2 = t.p2;
    v0 = t.v0; v1 = t.v1; v2 = t.v2;
};

bool nearlyEqual(double a, double b, double eps = 1e-9)
{
    return std::abs(a - b) <= eps;
}

bool nearlyEqual(const QPointF &a, const QPointF &b, double eps = 1e-9)
{
    return nearlyEqual(a.x(), b.x(), eps) && nearlyEqual(a.y(), b.y(), eps);
}

} // namespace

class TestMarchingTriangles : public QObject
{
    Q_OBJECT
private slots:

    // ---- Case enumeration ------------------------------------------------

    void singleCrossing_oneBelow()
    {
        // Right triangle, v0 < level < v1 = v2 (one vertex below)
        // Edges crossed: (0,1) and (0,2).
        std::vector<Tri> tris {
            { {0,0}, {1,0}, {0,1}, 0.0, 1.0, 1.0 }
        };
        auto segs = marchingTriangles(tris, std::vector<double>{0.5}, extract);
        QCOMPARE(segs.size(), size_t(1));
        // (0,1) crossing at t=0.5: (0.5, 0); (0,2) crossing at t=0.5: (0, 0.5)
        const auto &s = segs[0];
        const bool ok =
            (nearlyEqual(s.a, QPointF(0.5, 0.0)) && nearlyEqual(s.b, QPointF(0.0, 0.5))) ||
            (nearlyEqual(s.b, QPointF(0.5, 0.0)) && nearlyEqual(s.a, QPointF(0.0, 0.5)));
        QVERIFY2(ok, "endpoints should be (0.5,0) and (0,0.5) in either order");
        QCOMPARE(s.level, 0.5);
    }

    void singleCrossing_twoBelow()
    {
        // Mirror of above: v0 = v1 < level < v2  (one vertex above)
        std::vector<Tri> tris {
            { {0,0}, {1,0}, {0,1}, 0.0, 0.0, 1.0 }
        };
        auto segs = marchingTriangles(tris, std::vector<double>{0.5}, extract);
        QCOMPARE(segs.size(), size_t(1));
        // Edges crossed: (1,2) and (0,2). (1,2) at t=0.5: midpoint of (1,0)→(0,1) = (0.5,0.5)
        // (0,2) at t=0.5: (0, 0.5)
        const auto &s = segs[0];
        const bool ok =
            (nearlyEqual(s.a, QPointF(0.5, 0.5)) && nearlyEqual(s.b, QPointF(0.0, 0.5))) ||
            (nearlyEqual(s.b, QPointF(0.5, 0.5)) && nearlyEqual(s.a, QPointF(0.0, 0.5)));
        QVERIFY(ok);
    }

    void noCrossing_allBelow()
    {
        std::vector<Tri> tris {
            { {0,0}, {1,0}, {0,1}, 0.0, 0.0, 0.0 }
        };
        auto segs = marchingTriangles(tris, std::vector<double>{0.5}, extract);
        QCOMPARE(segs.size(), size_t(0));
    }

    void noCrossing_allAbove()
    {
        std::vector<Tri> tris {
            { {0,0}, {1,0}, {0,1}, 1.0, 1.0, 1.0 }
        };
        auto segs = marchingTriangles(tris, std::vector<double>{0.5}, extract);
        QCOMPARE(segs.size(), size_t(0));
    }

    void degenerateFlatTriangle_skipped()
    {
        // All three vertices share the same value — early-out path
        std::vector<Tri> tris {
            { {0,0}, {1,0}, {0,1}, 0.5, 0.5, 0.5 }
        };
        auto segs = marchingTriangles(tris, std::vector<double>{0.5}, extract);
        QCOMPARE(segs.size(), size_t(0));
    }

    // ---- Multi-level + multi-triangle ------------------------------------

    void multipleLevels_oneTriangle()
    {
        // Linear ramp z = x along the bottom edge; three contour levels
        // should each cross the triangle at a different chainage.
        std::vector<Tri> tris {
            { {0,0}, {1,0}, {0,1}, 0.0, 1.0, 0.0 }
        };
        std::vector<double> levels{0.25, 0.50, 0.75};
        auto segs = marchingTriangles(tris, levels, extract);
        QCOMPARE(segs.size(), size_t(3));

        // Each segment level should match one of the requested levels.
        std::vector<double> gotLevels;
        for (const auto &s : segs) gotLevels.push_back(s.level);
        std::sort(gotLevels.begin(), gotLevels.end());
        QCOMPARE(gotLevels, levels);
    }

    void levelOutsideRange_skipped()
    {
        // Levels far outside [vMin, vMax] should be skipped by the early-out.
        std::vector<Tri> tris {
            { {0,0}, {1,0}, {0,1}, 0.0, 1.0, 0.0 }
        };
        std::vector<double> levels{-1.0, 2.0};
        auto segs = marchingTriangles(tris, levels, extract);
        QCOMPARE(segs.size(), size_t(0));
    }

    void twoTriangles_independent()
    {
        // Two non-overlapping triangles each crossed by the same level.
        std::vector<Tri> tris {
            { {0,0}, {1,0}, {0,1}, 0.0, 1.0, 1.0 },
            { {10,10}, {11,10}, {10,11}, 0.0, 1.0, 1.0 }
        };
        auto segs = marchingTriangles(tris, std::vector<double>{0.5}, extract);
        QCOMPARE(segs.size(), size_t(2));
        // Second triangle's segment should be offset by (10,10).
        bool foundOrigin = false, foundOffset = false;
        for (const auto &s : segs) {
            if (s.a.x() < 5.0) foundOrigin = true;
            else               foundOffset = true;
        }
        QVERIFY(foundOrigin);
        QVERIFY(foundOffset);
    }

    // ---- evenlySpacedLevels helper ---------------------------------------

    void evenlySpacedLevels_basic()
    {
        auto lv = evenlySpacedLevels(0.0, 10.0, 4);
        QCOMPARE(lv.size(), size_t(4));
        // Step = 10 / (4+1) = 2, levels = 2,4,6,8
        QCOMPARE(lv[0], 2.0);
        QCOMPARE(lv[1], 4.0);
        QCOMPARE(lv[2], 6.0);
        QCOMPARE(lv[3], 8.0);
    }

    void evenlySpacedLevels_zeroCount_empty()
    {
        QCOMPARE(evenlySpacedLevels(0.0, 10.0, 0).size(), size_t(0));
        QCOMPARE(evenlySpacedLevels(0.0, 10.0, -3).size(), size_t(0));
    }

    void evenlySpacedLevels_degenerateRange_empty()
    {
        QCOMPARE(evenlySpacedLevels(5.0, 5.0, 4).size(), size_t(0));
        QCOMPARE(evenlySpacedLevels(10.0, 0.0, 4).size(), size_t(0));
    }

    // ---- evenlySpacedLevelsInclusive helper ------------------------------

    void evenlySpacedLevelsInclusive_basic()
    {
        // 5 levels across [0, 10] → step 2.5, endpoints kept.
        const auto lv = evenlySpacedLevelsInclusive(0.0, 10.0, 5);
        QCOMPARE(lv.size(), size_t(5));
        QCOMPARE(lv.front(), 0.0);
        QCOMPARE(lv.back(),  10.0);
        QCOMPARE(lv[2],      5.0);
    }

    void evenlySpacedLevelsInclusive_endpointGuard()
    {
        // The last entry is hard-set to vMax so floating-point drift across
        // many additions never lets it slip below by ε.
        const auto lv = evenlySpacedLevelsInclusive(0.1, 0.2, 10);
        QCOMPARE(lv.back(), 0.2);
    }

    void evenlySpacedLevelsInclusive_degenerate_empty()
    {
        QCOMPARE(evenlySpacedLevelsInclusive(0.0, 10.0, 1).size(), size_t(0));
        QCOMPARE(evenlySpacedLevelsInclusive(5.0, 5.0, 4).size(),  size_t(0));
    }

    // ---- marchingTrianglesIsobands ---------------------------------------

    void isobands_singleBand_coversFullTriangle()
    {
        // Triangle entirely inside band [-1, 2] → polygon = triangle.
        std::vector<Tri> tris {
            { {0,0}, {1,0}, {0,1}, 0.0, 1.0, 0.5 }
        };
        const auto bands = marchingTrianglesIsobands(
            tris, std::vector<double>{-1.0, 2.0}, extract);
        QCOMPARE(bands.size(), size_t(1));
        QCOMPARE(bands[0].verts.size(), size_t(3));
        QCOMPARE(bands[0].bandIndex, 0);
        QCOMPARE(bands[0].bandLo, -1.0);
        QCOMPARE(bands[0].bandHi,  2.0);
    }

    void isobands_singleBand_belowRange_empty()
    {
        // Triangle entirely above the band → no polygon emitted.
        std::vector<Tri> tris {
            { {0,0}, {1,0}, {0,1}, 5.0, 6.0, 7.0 }
        };
        const auto bands = marchingTrianglesIsobands(
            tris, std::vector<double>{0.0, 1.0}, extract);
        QCOMPARE(bands.size(), size_t(0));
    }

    void isobands_singleBand_partialCoverage()
    {
        // Triangle vertices at values 0, 1, 0.5. Band [0.25, 0.75] cuts
        // through. Expected polygon is a strip across the middle. Vertex
        // count should be 4 (two iso-crossings on each of two edges).
        std::vector<Tri> tris {
            { {0,0}, {1,0}, {0,1}, 0.0, 1.0, 0.5 }
        };
        const auto bands = marchingTrianglesIsobands(
            tris, std::vector<double>{0.25, 0.75}, extract);
        QCOMPARE(bands.size(), size_t(1));
        QVERIFY(bands[0].verts.size() >= 3);
        QVERIFY(bands[0].verts.size() <= 5);
    }

    void isobands_multipleBands_tile()
    {
        // 4 break levels = 3 bands. Triangle with values 0/1/0.5 should
        // contribute polygons to bands 0..2.
        std::vector<Tri> tris {
            { {0,0}, {1,0}, {0,1}, 0.0, 1.0, 0.5 }
        };
        const auto lv = evenlySpacedLevelsInclusive(0.0, 1.0, 4);
        QCOMPARE(lv.size(), size_t(4));
        const auto bands = marchingTrianglesIsobands(tris, lv, extract);
        QVERIFY(bands.size() >= 2);   // at least 2 bands cross this triangle
        // Each emitted band should have ≥ 3 vertices.
        for (const auto &bp : bands)
            QVERIFY(bp.verts.size() >= 3);
    }

    void isobands_degenerateTriangle_skipped()
    {
        std::vector<Tri> tris {
            { {0,0}, {1,0}, {0,1}, 0.5, 0.5, 0.5 }
        };
        const auto bands = marchingTrianglesIsobands(
            tris, std::vector<double>{0.0, 1.0}, extract);
        QCOMPARE(bands.size(), size_t(0));
    }

    void isobands_singleLevel_empty()
    {
        // <2 levels → no bands defined → empty
        std::vector<Tri> tris {
            { {0,0}, {1,0}, {0,1}, 0.0, 1.0, 0.5 }
        };
        const auto bands = marchingTrianglesIsobands(
            tris, std::vector<double>{0.5}, extract);
        QCOMPARE(bands.size(), size_t(0));
    }

    void isobands_bandIndexAssigned()
    {
        // Confirm the bandIndex matches the level-pair offset.
        std::vector<Tri> tris {
            { {0,0}, {10,0}, {0,10}, 0.0, 10.0, 5.0 }
        };
        std::vector<double> levels{0.0, 3.0, 6.0, 10.0};   // 3 bands
        const auto bands = marchingTrianglesIsobands(tris, levels, extract);
        // Each emitted band should report its index correctly.
        for (const auto &bp : bands) {
            QVERIFY(bp.bandIndex >= 0 && bp.bandIndex <= 2);
            QCOMPARE(bp.bandLo, levels[size_t(bp.bandIndex)]);
            QCOMPARE(bp.bandHi, levels[size_t(bp.bandIndex) + 1]);
        }
    }
};

QTEST_MAIN(TestMarchingTriangles)
#include "test_marchingtriangles.moc"
