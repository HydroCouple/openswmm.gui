// SPDX-License-Identifier: Apache-2.0
#include "mesh/sizingaudit.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace mesh
{
namespace
{
struct Stop {
};
struct Face {
    quint64 key;
    int cell;
    bool forward;
};
quint64 key(int a, int b)
{
    return quint64(quint32(std::min(a, b))) << 32 | quint32(std::max(a, b));
}
bool finite(QPointF p)
{
    return std::isfinite(p.x()) && std::isfinite(p.y());
}
}
SizingAuditReport auditMeshSizing(const MeshResult &mesh, const SizingAuditOptions &options)
{
    SizingAuditReport out;
    quint64 budget =
        options.workBudget ? options.workBudget : quint64(mesh.triangles.size()) * 1024 + 100000;
    auto work = [&] {
        if (!budget--) {
            out.resourceLimited = true;
            throw Stop{};
        }
        if ((++out.workUsed & 4095) == 0 && options.cancelled && options.cancelled()) {
            out.cancelled = true;
            throw Stop{};
        }
    };
    auto invalid = [&](QString error) {
        out.error = std::move(error);
        throw Stop{};
    };
    auto example = [&](int cell, int neighbour, quint8 flag, double measured, double requested,
                       const QString &message) {
        out.cellFlags[cell] |= flag;
        if (out.examples.size() >= std::clamp(options.maximumExamples, 0, 1000))
            return;
        const auto &c = mesh.triangles[cell];
        QPointF location;
        for (int k = 0; k < c.vertexCount(); ++k)
            location += mesh.vertices[c.vertex(k)].xy / c.vertexCount();
        out.examples.append({cell, neighbour, flag, location, measured, requested, message});
    };
    try {
        if (options.cancelled && options.cancelled()) {
            out.cancelled = true;
            throw Stop{};
        }
        for (double value : {options.minimumTarget, options.maximumSize, options.minimumEdge,
                             options.adjacentRatio})
            if (!std::isfinite(value) || value < 0)
                invalid("Invalid sizing audit limits.");
        if (options.adjacentRatio > 0 && options.adjacentRatio < 1)
            invalid("Adjacent size ratio must be at least one.");
        if (!mesh.ok || mesh.triangles.isEmpty())
            invalid("Sizing audit requires a nonempty successful mesh.");
        quint64 count = 0;
        for (const auto &c : mesh.triangles) {
            work();
            count += c.vertexCount();
        }
        const quint64 bytes = count * sizeof(Face) +
                              quint64(mesh.triangles.size()) * (sizeof(double) + 1) +
                              quint64(mesh.boundaryEdges.size()) * sizeof(quint64);
        if (bytes > options.maximumWorkspaceBytes) {
            out.resourceLimited = true;
            throw Stop{};
        }
        std::vector<Face> faces;
        faces.reserve(count);
        std::vector<quint64> constraints;
        constraints.reserve(mesh.boundaryEdges.size());
        QVector<double> sizes(mesh.triangles.size());
        out.cellFlags.fill(0, mesh.triangles.size());
        for (const auto &edge : mesh.boundaryEdges) {
            work();
            if (edge.v0 < 0 || edge.v1 < 0 || edge.v0 >= mesh.vertices.size() ||
                edge.v1 >= mesh.vertices.size() || edge.v0 == edge.v1)
                invalid("Invalid constraint-edge connectivity during sizing audit.");
            constraints.push_back(key(edge.v0, edge.v1));
        }
        std::sort(constraints.begin(), constraints.end(), [&](quint64 a, quint64 b) {
            work();
            return a < b;
        });
        out.smallestNominal = std::numeric_limits<double>::infinity();
        constexpr double tolerance = 1e-9;
        for (int i = 0; i < mesh.triangles.size(); ++i) {
            work();
            const auto &cell = mesh.triangles[i];
            const int n = cell.vertexCount();
            QPointF p[4];
            for (int k = 0; k < n; ++k) {
                const int v = cell.vertex(k);
                if (v < 0 || v >= mesh.vertices.size() || !finite(mesh.vertices[v].xy))
                    invalid("Invalid cell coordinate/connectivity during sizing audit.");
                for (int j = 0; j < k; ++j)
                    if (v == cell.vertex(j))
                        invalid("A cell repeats a vertex.");
                p[k] = mesh.vertices[v].xy;
            }
            long double twiceArea = 0;
            double shortest = std::numeric_limits<double>::infinity();
            double xmin = p[0].x(), xmax = xmin, ymin = p[0].y(), ymax = ymin;
            for (int k = 0; k < n; ++k) {
                const auto a = p[k] - p[0], b = p[(k + 1) % n] - p[0];
                twiceArea += static_cast<long double>(a.x()) * b.y() -
                             static_cast<long double>(a.y()) * b.x();
                const auto u = p[(k + 1) % n] - p[k], v = p[(k + 2) % n] - p[(k + 1) % n];
                if (!(static_cast<long double>(u.x()) * v.y() -
                          static_cast<long double>(u.y()) * v.x() >
                      0))
                    invalid("A delivered cell is inverted, degenerate or nonconvex.");
                shortest = std::min(shortest, std::hypot(u.x(), u.y()));
                xmin = std::min(xmin, p[k].x());
                xmax = std::max(xmax, p[k].x());
                ymin = std::min(ymin, p[k].y());
                ymax = std::max(ymax, p[k].y());
                const int aId = cell.vertex(k), bId = cell.vertex((k + 1) % n);
                const auto edge = key(aId, bId);
                faces.push_back({edge, i, aId < bId});
                if (std::binary_search(constraints.begin(), constraints.end(), edge))
                    out.cellFlags[i] |= TouchesConstraint;
            }
            const double size = std::sqrt(double(twiceArea / 2) / (n == 3 ? std::sqrt(3.) / 4 : 1));
            if (!(size > 0) || !std::isfinite(size))
                invalid("Unrepresentable delivered nominal size.");
            sizes[i] = size;
            ++out.cellsChecked;
            out.smallestNominal = std::min(out.smallestNominal, size);
            out.largestNominal = std::max(out.largestNominal, size);
            if (options.minimumTarget > 0 && size < options.minimumTarget * (1 - tolerance)) {
                ++out.belowTarget;
                out.cellFlags[i] |= BelowTarget;
            }
            if (options.minimumEdge > 0 && shortest < options.minimumEdge * (1 - tolerance)) {
                ++out.belowEdgeFloor;
                example(i, -1, BelowEdgeFloor, shortest, options.minimumEdge,
                        "Edge shorter than the separately requested geometric floor.");
            }
            if (options.maximumSize > 0 && size > options.maximumSize * (1 + tolerance)) {
                ++out.aboveMaximum;
                example(i, -1, AboveMaximum, size, options.maximumSize,
                        "Nominal size exceeds the global maximum.");
            }
            const double local =
                options.footprintMaximum
                    ? options.footprintMaximum(QRectF(QPointF(xmin, ymin), QPointF(xmax, ymax)))
                    : 0;
            if (!std::isfinite(local) || local < 0)
                invalid("Requested footprint-size bound is invalid or unavailable.");
            if (local > 0 && size > local * (1 + tolerance)) {
                ++out.aboveLocalMaximum;
                example(i, -1, AboveLocalMaximum, size, local,
                        "Nominal size exceeds the combined target over its footprint.");
            }
        }
        std::sort(faces.begin(), faces.end(), [&](const Face &a, const Face &b) {
            work();
            return a.key != b.key ? a.key < b.key : a.cell < b.cell;
        });
        for (size_t first = 0; first < faces.size();) {
            work();
            size_t last = first + 1;
            while (last < faces.size() && faces[last].key == faces[first].key) {
                work();
                ++last;
            }
            if (last - first > 2 ||
                (last - first == 2 && (faces[first].cell == faces[first + 1].cell ||
                                       faces[first].forward == faces[first + 1].forward)))
                invalid("Nonmanifold or inconsistent shared-face connectivity.");
            if (last - first == 2) {
                ++out.facesChecked;
                const int a = faces[first].cell, b = faces[first + 1].cell;
                const double ratio = std::max(sizes[a], sizes[b]) / std::min(sizes[a], sizes[b]);
                out.largestAdjacentRatio = std::max(out.largestAdjacentRatio, ratio);
                if (options.adjacentRatio > 0 && ratio > options.adjacentRatio * (1 + tolerance)) {
                    ++out.transitionFaces;
                    const int large = sizes[a] > sizes[b] ? a : b, small = large == a ? b : a;
                    example(large, small, TransitionExceeded, ratio, options.adjacentRatio,
                            "Adjacent nominal-size ratio exceeds the requested transition.");
                    out.cellFlags[small] |= TransitionExceeded;
                }
            }
            first = last;
        }
        out.valid = true;
    } catch (const Stop &) {
    }
    out.complete = !out.cancelled && !out.resourceLimited;
    if (out.cancelled)
        out.error = "Sizing audit cancelled.";
    if (out.resourceLimited)
        out.error = "Sizing audit exceeded its work or memory budget.";
    return out;
}
}
