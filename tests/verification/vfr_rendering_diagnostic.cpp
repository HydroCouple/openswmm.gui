// Planning diagnostic for workplans/2D_VFR_RENDERING_PLAN_2026-09-30.md.
// Uses production helpers; characterizes existing behavior, not a fix.
// Reproduction from the repository root on the inspected macOS Qt toolchain:
// c++ -std=c++17 -O2 -include arm_acle.h -Iinclude \
//   -F/Users/calebbuahin/Qt/6.9.3/macos/lib \
//   -I/Users/calebbuahin/Qt/6.9.3/macos/lib/QtCore.framework/Headers \
//   -I/Users/calebbuahin/Qt/6.9.3/macos/lib/QtGui.framework/Headers \
//   -framework QtCore -framework QtGui \
//   -Wl,-rpath,/Users/calebbuahin/Qt/6.9.3/macos/lib \
//   tests/verification/vfr_rendering_diagnostic.cpp \
//   -o tests/verification/vfr_rendering_artifacts/diagnostic
// tests/verification/vfr_rendering_artifacts/diagnostic \
//   > tests/verification/vfr_rendering_artifacts/diagnostic.txt
#include "contour/marchingtriangles.h"
#include "layers/cellsurfaceinterp.h"
#include "layers/vertexdepthreconstruct.h"
#include "plot/meshprofileinterp.h"

#include <iomanip>
#include <iostream>

int main()
{
    std::cout << std::setprecision(12);
    const QPointF a(0, 0), b(1, 0), c(0, 1);
    float d0 = 1, d1 = 2, d2 = 0;
    VertexDepthReconstruct::extrapolateDryCorners(0, 0, 4, d0, d1, d2);
    std::cout << "Case 1: two wet corners at different stages; adverse bed z=4y\n"
              << "Map corner signed depths: " << d0 << ", " << d1 << ", " << d2 << "\n"
              << "y, ground, profile_depth, map_depth, map_wse_when_wet\n";
    bool reproduced = true;
    for (const double y : {0.0, 0.2, 0.3, 0.375, 0.4}) {
        const double x = (1-y)/2;
        const double profile = CellSurfaceInterp::depthAt(
            QPointF(x, y), a, b, c, 0, 0, 4, d0, d1, d2);
        const double map = std::max(0.0, x*d0 + x*d1 + y*d2);
        std::cout << y << ", " << 4*y << ", " << profile << ", " << map
                  << ", " << (map > 0 ? 4*y + map : NAN) << "\n";
        if (y == 0.4) reproduced &= profile == 0 && map > 0.09;
    }
    const auto contours = OpenSWMM::Contour::marchingTriangles(
        std::vector<int>{0}, std::vector<double>{0.0},
        [&](int, QPointF& p0, QPointF& p1, QPointF& p2,
            double& v0, double& v1, double& v2) {
            p0 = a; p1 = b; p2 = c; v0 = d0; v1 = d1; v2 = d2;
        });
    for (const auto& s : contours)
        std::cout << "Production zero-contour endpoints: (" << s.a.x() << ", "
                  << s.a.y() << ") -> (" << s.b.x() << ", " << s.b.y() << ")\n";
    std::cout << "Along x=(1-y)/2: profile shoreline y=0.375; map shoreline y="
              << 3.0/7.0 << "\n\n";

    QVector<MeshProfileSampler::Sample> samples(3);
    for (int i = 0; i < 3; ++i) {
        samples[i].chainage = i;
        samples[i].ground = 0;
        samples[i].depthNow = i == 1 ? 0 : 1;
        samples[i].cellHasSurface = i != 1;
    }
    const auto tops = MeshProfileInterp::bridgedTops(samples, [](const auto& s) {
        return s.ground + s.depthNow;
    });
    std::cout << "Case 2: wet / no-surface / wet over flat bed\n"
              << "Middle input depth=" << samples[1].depthNow
              << "; painted depth=" << tops[1] - samples[1].ground << "\n\n";
    reproduced &= tops[1] == 1;

    std::cout << "Case 3: fallback datum sensitivity; constant 0.001 m depth\n"
              << "datum_m, reconstructed_depth_m\n";
    for (const double datum : {0.0, 1000.0, 100000.0}) {
        std::vector<float> sum, weights, depths;
        VertexDepthReconstruct::reconstructVertexSignedDepths(
            std::vector<std::array<int, 3>>{{0, 1, 2}},
            std::vector<float>{0.001f}, std::vector<float>{float(datum)},
            std::vector<double>{datum, datum, datum}, 0.0001f,
            sum, weights, depths);
        std::cout << datum << ", " << depths[0] << "\n";
    }
    std::cout << "\nDiagnostic behavior reproduced: " << (reproduced ? "YES" : "NO")
              << "\nThese synthetic cases do not identify the user's specific run.\n";
    return reproduced ? 0 : 1;
}
