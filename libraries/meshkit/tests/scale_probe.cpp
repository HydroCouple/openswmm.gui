// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in capacity probe, not an end-to-end terrain/GUI performance claim.
#include "mesh/meshcdt.h"
#include "mesh/cellplanner.h"
#include "mesh/constraintaudit.h"
#include "mesh/meshresult.h"
#include "mesh/sizingaudit.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <iostream>
#include <limits>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool xOk = false, yOk = false;
    const int nx = argc > 1 ? QString::fromLocal8Bit(argv[1]).toInt(&xOk) : 0;
    const int ny = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt(&yOk) : 0;
    const qint64 points = qint64(nx) * ny, expected = 2ll * (qint64(nx) - 1) * (qint64(ny) - 1);
    if (!xOk || !yOk || nx < 2 || ny < 2 || points > 6'000'000 || expected > 12'000'000) {
        std::cerr << "Usage: meshkit_scale_probe columns rows (2.., at most 6000000 vertices)\n";
        return 2;
    }
    if(qEnvironmentVariableIntValue("MESHKIT_PLAN_QUADS")) {
        QElapsedTimer timer;timer.start();mesh::QuadPlanningGeometry geometry;
        geometry.domains={QPolygonF{{0,0},{double(nx),0},{double(nx),double(ny)},{0,double(ny)}}};
        QVector<mesh::QuadRegion> regions;
        const int sx=std::min(nx,300),sy=std::min(ny,200);
        for(int y=0;y<ny;y+=sy)for(int x=0;x<nx;x+=sx){
            mesh::QuadRegion region;const double x1=std::min(nx,x+sx),y1=std::min(ny,y+sy);
            region.ring={{double(x),double(y)},{x1,double(y)},{x1,y1},{double(x),y1}};
            region.spacing=1;region.requireMapped=true;regions.append(region);
        }
        mesh::QuadPlanningOptions options;options.limits.maximumSize=[](const QRectF &){return 1.;};
        const auto plan=mesh::planQuadRegions(regions,geometry,options);
        double peak=0;
#if defined(__unix__) || defined(__APPLE__)
        rusage usage{};if(getrusage(RUSAGE_SELF,&usage)==0){peak=double(usage.ru_maxrss);
#ifndef __APPLE__
        peak*=1024;
#endif
        }
#endif
        QJsonObject output{{"ok",plan.ok},{"cells",qint64(plan.plannedCells)},{"patches",plan.patches.size()},
            {"sharedSides",qint64(plan.sharedSides)},{"work",qint64(plan.workUsed)},{"elapsedMs",timer.elapsed()},{"peakBytes",peak},
            {"scope","Mapped planning and acceptance only; no CDT, DEM, file I/O or GUI."},{"error",plan.error}};
        std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).constData()<<'\n';
        return plan.ok && plan.plannedCells==quint64(nx)*ny?0:1;
    }
    QElapsedTimer timer;
    timer.start();
    QVector<QPointF> input;
    input.reserve(points);
    for (int y = 0; y < ny; ++y)
        for (int x = 0; x < nx; ++x)
            input.append({double(x), double(y)});
    mesh::ConstrainedDelaunay cdt;
    if (!cdt.build(input)) {
        std::cerr << qPrintable(cdt.errorMsg()) << '\n';
        return 1;
    }
    const auto buildMs = timer.elapsed();
    // Four authored sides, including every collinear boundary point.
    const int corners[] = {0, nx - 1, int(points - 1), int(points - nx)};
    for (int e = 0; e < 4; ++e)
        if (!cdt.insertConstraint(corners[e], corners[(e + 1) % 4])) {
            std::cerr << "constraint recovery failed\n";
            return 1;
        }
    cdt.removeExterior();
    const auto constraintMs = timer.elapsed() - buildMs;
    std::int64_t live = 0;
    double area = 0;
    bool valid = true;
    for (const auto &t : cdt.triangles()) {
        if (!t.alive)
            continue;
        ++live;
        const auto &a = cdt.vertices()[t.v[0]], &b = cdt.vertices()[t.v[1]],
                   &c = cdt.vertices()[t.v[2]];
        const double twice = (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
        valid = valid && twice > 0;
        area += .5 * twice;
    }
    valid = valid && live == expected && live == cdt.liveTriangleCount() &&
            area == double(nx - 1) * (ny - 1);
    const bool sizingRequested=qEnvironmentVariableIntValue("MESHKIT_AUDIT_SIZING")!=0;
    const bool auditRequested = sizingRequested || qEnvironmentVariableIntValue("MESHKIT_AUDIT_DELIVERED") != 0;
    qint64 auditMs = 0, adapterMs = 0, sizingMs = 0;
    mesh::SizingAuditReport sizing;
    mesh::ConstraintAuditReport audit;
    if (auditRequested) {
        const auto adapterStart = timer.elapsed();
        mesh::MeshResult delivered;
        delivered.ok = true;
        delivered.vertices.reserve(cdt.vertices().size());
        delivered.triangles.reserve(live);
        for (const auto point : cdt.vertices()) delivered.vertices.append({point, 0});
        for (const auto &cell : cdt.triangles()) {
            if (!cell.alive) continue;
            delivered.triangles.append({cell.v[0], cell.v[1], cell.v[2]});
            for (int k = 0; k < 3; ++k)
                if (cell.constrained[k])
                    delivered.boundaryEdges.append({cell.v[(k + 1) % 3], cell.v[(k + 2) % 3], 1});
        }
        mesh::SimplificationFeature boundary;
        boundary.id = "grid-domain"; boundary.closed = true; boundary.marker = 1;
        boundary.role = mesh::SimplificationFeature::Role::Domain;
        for (int corner : corners) boundary.xy.append(input[corner]);
        adapterMs = timer.elapsed() - adapterStart;
        const auto auditStart = timer.elapsed();
        audit = mesh::auditMeshConstraints({boundary}, delivered);
        auditMs = timer.elapsed() - auditStart;
        valid = valid && audit.ok && audit.cellsChecked == quint64(live);
        if (!audit.ok) std::cerr << qPrintable(audit.error) << '\n';
        if(sizingRequested) {
            mesh::SizingAuditOptions options;options.maximumSize=1.1;options.adjacentRatio=2;
            const auto start=timer.elapsed();sizing=mesh::auditMeshSizing(delivered,options);sizingMs=timer.elapsed()-start;
            valid=valid && sizing.limitsSatisfied() && sizing.cellsChecked==quint64(live);
            if(!sizing.limitsSatisfied()) std::cerr<<qPrintable(sizing.error)<<'\n';
        }
    }
    double peakBytes = 0;
#if defined(__unix__) || defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        peakBytes = double(usage.ru_maxrss);
#ifndef __APPLE__
        peakBytes *= 1024;
#endif
    }
#endif
    QJsonObject report{{"workload", "uniform_grid_cdt_with_four_constrained_sides"},
                       {"vertices", double(points)},
                       {"cells", double(live)},
                       {"triangle_slots", double(cdt.triangles().size())},
                       {"triangle_bytes", double(sizeof(mesh::ConstrainedDelaunay::Triangle))},
                       {"build_ms", double(buildMs)},
                       {"constraints_ms", double(constraintMs)},
                       {"total_ms", double(timer.elapsed())},
                       {"peak_rss_bytes", peakBytes},
                       {"area", area},
                       {"valid", valid}};
    if (auditRequested) {
        report["delivered_adapter_ms"] = double(adapterMs);
        report["delivered_audit_ms"] = double(auditMs);
        report["delivered_audit_passed"] = audit.ok;
        report["audited_cells"] = double(audit.cellsChecked);
        report["audit_work"] = double(audit.workUsed);
    }
    if(sizingRequested) {
        report["sizing_audit_passed"]=sizing.limitsSatisfied();report["sizing_audit_ms"]=double(sizingMs);
        report["shared_faces_checked"]=double(sizing.facesChecked);report["largest_adjacent_ratio"]=sizing.largestAdjacentRatio;
    }
    std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
    return valid ? 0 : 1;
}
