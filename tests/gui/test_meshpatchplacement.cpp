// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh/meshgenerator.h"
#include "mesh/meshpatch.h"
#include "mesh/meshcellgeom.h"
#include <QTest>
#include <cmath>
#include <limits>

namespace {
QPolygonF squareDomain(double offset = 0.0) {
    QPolygonF ring;
    for (int i = 0; i < 10; ++i) ring << QPointF(offset + 10 * i, offset);
    for (int i = 0; i < 10; ++i) ring << QPointF(offset + 100, offset + 10 * i);
    for (int i = 0; i < 10; ++i) ring << QPointF(offset + 100 - 10 * i, offset + 100);
    for (int i = 0; i < 10; ++i) ring << QPointF(offset, offset + 100 - 10 * i);
    return ring;
}
mesh::PatchMesh rectangle(double x0, double y0, double x1, double y1, const QString &tag) {
    mesh::StructuredPatch patch;
    patch.corners = {{x0,y0}, {x1,y0}, {x1,y1}, {x0,y1}};
    patch.n = patch.m = 2;
    patch.tag = tag;
    return mesh::makeTransfinitePatch(patch);
}
void options(mesh::MeshGenerator &generator) {
    mesh::GenerationOptions opts;
    opts.maxArea = 200;
    opts.trianglesOnly = true;   // patches supply the only quads
    generator.setOptions(opts);
}
}

class TestMeshPatchPlacement : public QObject {
    Q_OBJECT
private slots:
    void invalidPlacement_data() {
        QTest::addColumn<QString>("scenario");
        QTest::newRow("duplicate-patches") << QString("duplicate");
        QTest::newRow("partially-overlapping-patches") << QString("overlap");
        QTest::newRow("crossing-thin-patches") << QString("cross");
        QTest::newRow("wholly-outside-domain") << QString("outside");
        QTest::newRow("crosses-domain-boundary") << QString("boundary");
        QTest::newRow("crosses-concave-notch-with-interior-corners") << QString("notch");
        QTest::newRow("contains-excluded-hole") << QString("hole");
        QTest::newRow("crosses-hole-boundary-with-seed-outside") << QString("hole-edge");
        QTest::newRow("inside-hole-with-seed-outside-patch") << QString("inside-hole");
        QTest::newRow("contains-user-hole-seed") << QString("seed");
        QTest::newRow("constraint-crosses-patch-interior") << QString("constraint");
        QTest::newRow("constraint-between-patch-boundaries") << QString("constraint-boundaries");
        QTest::newRow("empty-requested-patch") << QString("empty");
    }
    void invalidPlacement() {
        QFETCH(QString, scenario);
        mesh::MeshGenerator generator;
        generator.setDomain(squareDomain());
        options(generator);
        auto patch = rectangle(40, 40, 60, 60, "requested-corridor");
        QVERIFY(!patch.quads.isEmpty());
        if (scenario == "duplicate") generator.addPatch(patch);
        else if (scenario == "overlap") generator.addPatch(rectangle(50, 50, 70, 70, "overlap"));
        else if (scenario == "cross") {
            patch = rectangle(30, 48, 70, 52, "horizontal-corridor");
            generator.addPatch(rectangle(48, 30, 52, 70, "vertical-corridor"));
        } else if (scenario == "outside") patch = rectangle(110, 40, 130, 60, "outside-corridor");
        else if (scenario == "boundary") patch = rectangle(90, 40, 110, 60, "boundary-corridor");
        else if (scenario == "notch") {
            // All four corners AND the centroid lie in this domain, but
            // both horizontal patch edges cross the notch at x=70..80.
            generator.setDomain(QPolygonF(QVector<QPointF>{
                {0,0}, {100,0}, {100,100}, {80,100}, {80,40}, {70,40}, {70,100}, {0,100}}));
            patch = rectangle(10, 50, 90, 60, "notch-crossing-corridor");
        } else if (scenario == "hole" || scenario == "hole-edge") {
            generator.addConstraintSegment({{{45,45}, {55,45}, {55,55}, {45,55}, {45,45}}, 7, "hole"});
            generator.addHole({50, 50});
            if (scenario == "hole-edge") patch = rectangle(40, 46, 47, 54, "hole-crossing-corridor");
        } else if (scenario == "inside-hole") {
            generator.addConstraintSegment({{{20,20}, {80,20}, {80,80}, {20,80}, {20,20}}, 7, "hole"});
            generator.addHole({30, 30});
        } else if (scenario == "seed") generator.addHole({50, 50});
        else if (scenario == "constraint")
            generator.addConstraintSegment({{{20,50}, {80,50}}, 9, "required-road"});
        else if (scenario == "constraint-boundaries")
            generator.addConstraintSegment({{{40,50}, {60,50}}, 9, "required-road"});
        else if (scenario == "empty") patch = {};
        generator.addPatch(patch);
        const auto result = generator.generate();
        QVERIFY2(!result.ok, qPrintable(QString("Invalid placement was accepted: %1; cells=%2 quads=%3")
            .arg(scenario).arg(result.triangles.size()).arg(result.quadCount())));
        QVERIFY2(!result.errorMsg.isEmpty(), "A rejected requested corridor needs an actionable diagnostic");
    }

    void rejectsNonconformingWeld_data() {
        QTest::addColumn<bool>("snapCollapse");
        QTest::newRow("snap-collapses-patch-vertices") << true;
        QTest::newRow("abutting-different-subdivisions") << false;
    }
    void rejectsNonconformingWeld() {
        QFETCH(bool, snapCollapse);
        mesh::MeshGenerator generator;
        generator.setDomain(squareDomain());
        mesh::GenerationOptions opts;
        opts.maxArea = 200;
        opts.trianglesOnly = true;
        opts.patchSnapEps = snapCollapse ? 20 : 0;
        generator.setOptions(opts);
        generator.addPatch(rectangle(20, 40, 40, 60, "first"));
        if (!snapCollapse) {
            mesh::StructuredPatch second;
            second.corners = {{40,40}, {60,40}, {60,60}, {40,60}};
            second.n = 2;
            second.m = 3; // three seam intervals cannot meet the first patch's two
            second.tag = "nonconforming";
            const auto patch = mesh::makeTransfinitePatch(second);
            QVERIFY(!patch.quads.isEmpty());
            generator.addPatch(patch);
        }
        const auto result = generator.generate();
        QVERIFY2(!result.ok, "An invalid welded patch or unmatched shared interface was accepted");
        QVERIFY2(!result.errorMsg.isEmpty(), "Invalid topology must include a diagnostic");
        if (snapCollapse)
            QVERIFY2(result.errorMsg.contains("boundary"), qPrintable(result.errorMsg));
    }

    void invalidSnapTolerance_data() {
        QTest::addColumn<double>("tolerance");
        QTest::newRow("nan") << std::numeric_limits<double>::quiet_NaN();
        QTest::newRow("infinite") << std::numeric_limits<double>::infinity();
        QTest::newRow("finite-key-overflow") << 1e-18;
        QTest::newRow("division-overflow") << std::numeric_limits<double>::denorm_min();
    }
    void invalidSnapTolerance() {
        QFETCH(double, tolerance);
        mesh::MeshGenerator generator;
        generator.setDomain(squareDomain());
        mesh::GenerationOptions opts;
        opts.maxArea = 200;
        opts.patchSnapEps = tolerance;
        generator.setOptions(opts);
        generator.addPatch(rectangle(20, 40, 40, 60, "invalid-snap"));
        const auto result = generator.generate();
        QVERIFY(!result.ok);
        QVERIFY2(result.errorMsg.contains("snap tolerance"), qPrintable(result.errorMsg));
    }

    void validOutlinePlacement_data() {
        QTest::addColumn<QString>("scenario");
        QTest::newRow("along-domain-outline") << QString("domain");
        QTest::newRow("along-hole-outline") << QString("hole");
        QTest::newRow("nested-hole-preserves-protected-annulus") << QString("nested");
    }
    void validOutlinePlacement() {
        QFETCH(QString, scenario);
        mesh::MeshGenerator generator;
        generator.setDomain(squareDomain());
        options(generator);
        auto patch = rectangle(0, 40, 20, 60, "outline");
        double expectedArea = 10000;
        if (scenario != "domain") {
            generator.addConstraintSegment({{{45,45}, {55,45}, {55,55}, {45,55}, {45,45}}, 7, "hole"});
            generator.addHole({50, 50});
            expectedArea -= 100;
            patch = rectangle(25, 45, 45, 55, "hole-outline");
            if (scenario == "nested") {
                generator.addConstraintSegment({{{20,20}, {80,20}, {80,80}, {20,80}, {20,20}}, 9, "protected-ring"});
                patch = rectangle(25, 30, 35, 40, "protected-annulus");
            }
        }
        generator.addPatch(patch);
        const auto result = generator.generate();
        QVERIFY2(result.ok, qPrintable(result.errorMsg));
        QCOMPARE(result.quadCount(), patch.quads.size());
        double area = 0;
        for (const auto &cell : result.triangles) area += mesh::cellGeom(result.vertices, cell).area;
        QVERIFY2(std::abs(area - expectedArea) < 1e-5,
                 qPrintable(QString("Expected domain area %1, received %2").arg(expectedArea).arg(area, 0, 'g', 17)));
    }

    void validPlacement_data() {
        QTest::addColumn<bool>("abutting");
        QTest::addColumn<double>("offset");
        QTest::newRow("disjoint") << false << 0.0;
        QTest::newRow("exact-abutting") << true << 0.0;
        QTest::newRow("translated-disjoint") << false << 1000000.0;
        QTest::newRow("translated-exact-abutting") << true << 1000000.0;
    }
    void validPlacement() {
        QFETCH(bool, abutting);
        QFETCH(double, offset);
        mesh::MeshGenerator generator;
        generator.setDomain(squareDomain(offset));
        options(generator);
        const auto first = rectangle(offset + 20, offset + 40, offset + 40, offset + 60, "first");
        const auto second = rectangle(offset + (abutting ? 40 : 60), offset + 40,
                                      offset + (abutting ? 60 : 80), offset + 60, "second");
        QVERIFY(!first.quads.isEmpty());
        QVERIFY(!second.quads.isEmpty());
        generator.addPatch(first);
        generator.addPatch(second);
        const auto result = generator.generate();
        QVERIFY2(result.ok, qPrintable(result.errorMsg));
        QCOMPARE(result.quadCount(), first.quads.size() + second.quads.size());
        int firstCells = 0, secondCells = 0;
        double area = 0;
        for (const auto &cell : result.triangles) {
            QVERIFY(mesh::cellIsConvex(result.vertices, cell));
            const auto geometry = mesh::cellGeom(result.vertices, cell);
            QVERIFY(geometry.area > 0);
            area += geometry.area;
            if (cell.isQuad()) {
                if (cell.tag == "first") ++firstCells;
                if (cell.tag == "second") ++secondCells;
            }
        }
        QCOMPARE(firstCells, first.quads.size());
        QCOMPARE(secondCells, second.quads.size());
        QVERIFY2(std::abs(area - 10000.0) < 1e-5,
                 qPrintable(QString("Combined cell area %1 does not cover domain exactly once").arg(area, 0, 'g', 17)));
        if (abutting) {
            for (int row = 0; row <= 2; ++row) {
                const QPointF shared(offset + 40, offset + 40 + row * 10);
                int count = 0;
                for (const auto &vertex : result.vertices) count += vertex.xy == shared;
                QCOMPARE(count, 1); // exact conforming joins share topology
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestMeshPatchPlacement)
#include "test_meshpatchplacement.moc"
