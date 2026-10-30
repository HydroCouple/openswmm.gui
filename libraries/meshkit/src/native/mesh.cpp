// SPDX-License-Identifier: Apache-2.0
#include "meshkit/mesh.hpp"
#include "meshkit/cdt.hpp"
#include "operation.hpp"
#include <unordered_set>

namespace meshkit
{
OperationReport validateMesh(const Mesh &mesh, const Limits &limits,
                             const std::function<bool()> &cancelled)
{
    using namespace native_detail;
    OperationReport report;
    const auto start = std::chrono::steady_clock::now();
    try {
        Work work(limits, cancelled);
        if (mesh.vertices.size() > limits.maxVertices || mesh.cells.size() > limits.maxCells ||
            mesh.edges.size() > limits.maxEdges || mesh.vertices.size() >= missingIndex ||
            mesh.paths.size() > limits.maxEdges || mesh.aliases.size() > limits.maxVertices ||
            mesh.labels.size() > limits.maxMetadataBytes / 4)
            fail("count_limit", "Mesh counts exceed configured/index limits.",
                 RunStatus::ResourceLimit);
        if (!(mesh.metadata.horizontalMetresPerUnit > 0) ||
            !(mesh.metadata.verticalMetresPerUnit > 0) ||
            !std::isfinite(mesh.metadata.horizontalMetresPerUnit) ||
            !std::isfinite(mesh.metadata.verticalMetresPerUnit))
            fail("units", "Unit scales must be positive and finite.");
        std::uint64_t text = mesh.metadata.crs.size() + mesh.metadata.description.size();
        if (!utf8(mesh.metadata.crs) || !utf8(mesh.metadata.description))
            fail("utf8", "Metadata must be valid UTF-8.");
        std::unordered_set<std::int32_t> labels;
        for (const auto &label : mesh.labels) {
            work.tick();
            text += label.name.size();
            if (text > limits.maxMetadataBytes)
                fail("metadata_limit", "Metadata limit exceeded.", RunStatus::ResourceLimit);
            if (!labels.insert(label.id).second || !utf8(label.name))
                fail("label", "Invalid or duplicate label.");
        }
        if (text > limits.maxMetadataBytes)
            fail("metadata_limit", "Metadata limit exceeded.", RunStatus::ResourceLimit);
        auto ids = [&](const auto &values, std::size_t count) {
            if (values.empty())
                return;
            if (values.size() != count)
                fail("id_count", "External id count does not match geometry.");
            bool dense = true;
            for (std::size_t i = 0; i < count; ++i) {
                work.tick();
                dense = dense && values[i] == std::int64_t(i + 1);
            }
            if (dense)
                return;
            work.memory(count * 48);
            std::unordered_set<std::int64_t> seen;
            seen.reserve(count);
            for (auto id : values) {
                work.tick();
                if (!seen.insert(id).second)
                    fail("duplicate_id", "Duplicate external identifier.");
            }
        };
        ids(mesh.vertexIds, mesh.vertices.size());
        ids(mesh.cellIds, mesh.cells.size());
        ids(mesh.edgeIds, mesh.edges.size());
        if (!mesh.vertexMarkers.empty() && mesh.vertexMarkers.size() != mesh.vertices.size())
            fail("marker_count", "Vertex marker count mismatch.");
        for (const auto &p : mesh.vertices) {
            work.tick();
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                fail("nonfinite_vertex", "Mesh vertex is not finite.");
        }
        auto validIndex = [&](Index id) {
            if (id >= mesh.vertices.size())
                fail("connectivity", "Connectivity index is out of range.");
        };
        for (const auto &cell : mesh.cells) {
            work.tick();
            if (cell.count != 3 && cell.count != 4)
                fail("cell_type", "Only triangles and quadrilaterals are supported.");
            Point2 p[4];
            for (int k = 0; k < cell.count; ++k) {
                validIndex(cell.vertices[k]);
                const auto &v = mesh.vertices[cell.vertices[k]];
                p[k] = {v.x, v.y};
            }
            for (int k = 0; k < cell.count; ++k) {
                const double turn = ConstrainedDelaunay::orientExact(p[k], p[(k + 1) % cell.count],
                                                                     p[(k + 2) % cell.count]);
                if (!(turn > 0) || !std::isfinite(turn))
                    fail(
                        "cell_geometry",
                        "Cell is clockwise, nonconvex, degenerate or numerically unrepresentable.");
            }
        }
        for (auto edge : mesh.edges) {
            work.tick();
            validIndex(edge.a);
            validIndex(edge.b);
            if (edge.a == edge.b)
                fail("edge_geometry", "Zero-length edge identity.");
        }
        std::uint64_t pathRecords = 0;
        for (const auto &path : mesh.paths) {
            work.tick();
            if (int(path.kind) > int(PathKind::Region))
                fail("path_kind", "Unknown path kind.");
            if (path.vertices.size() < 2)
                fail("path_geometry", "Path needs at least two vertices.");
            for (auto v : path.vertices) {
                work.tick();
                validIndex(v);
            }
            if (pathRecords > limits.maxEdges ||
                path.vertices.size() > limits.maxEdges - pathRecords)
                fail("path_limit", "Path connectivity exceeds the limit.",
                     RunStatus::ResourceLimit);
            pathRecords += path.vertices.size();
            if (pathRecords > limits.maxEdges)
                fail("path_limit", "Path connectivity exceeds the limit.",
                     RunStatus::ResourceLimit);
        }
        for (const auto &alias : mesh.aliases) {
            work.tick();
            validIndex(alias.vertex);
        }
        report.work = work.used;
        report.counts.vertices = mesh.vertices.size();
        report.counts.triangles = 0;
        report.counts.quads = 0;
        for (const auto &cell : mesh.cells)
            (cell.count == 4 ? report.counts.quads : report.counts.triangles)++;
        report.status = RunStatus::Complete;
        report.code = "valid";
        report.message = "Local mesh geometry and connectivity validated.";
    } catch (const Stop &e) {
        report.status = e.status;
        report.code = e.code;
        report.message = e.message;
    } catch (const std::bad_alloc &) {
        report.status = RunStatus::ResourceLimit;
        report.code = "allocation";
        report.message = "Insufficient memory.";
    } catch (const std::exception &e) {
        report.status = RunStatus::Failed;
        report.code = "exception";
        report.message = e.what();
    }
    report.seconds = seconds(start);
    return report;
}
}
