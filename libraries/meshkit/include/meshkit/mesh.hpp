// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "geometry.hpp"
#include "events.hpp"
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace meshkit {
using Index=std::uint32_t;
constexpr Index missingIndex=~Index(0);
enum class PathKind:std::uint8_t { Outer,Hole,Line,Region };
struct Path {
    std::vector<Index> vertices;
    PathKind kind=PathKind::Line;
    std::int32_t marker=0;
    std::int64_t id=0;
    bool closed=false;
};
struct Cell {
    std::array<Index,4> vertices{};
    std::int32_t region=0;
    std::uint8_t count=3;
};
struct Edge { Index a=0,b=0;std::int32_t marker=0; };
struct Label {std::int32_t id=0;std::string name;};
struct VertexAlias {Index vertex=0;std::int64_t sourceId=0;std::int32_t marker=0;};
struct Metadata {
    double horizontalMetresPerUnit=1,verticalMetresPerUnit=1;
    bool unitsKnown=true;
    std::string crs,description;
};
// Geometry and sparse numeric attributes only; no engine/coupling ownership.
// Empty id/marker arrays mean sequential one-based ids / zero markers. Arrays
// are owned and contiguous. Connectivity is zero-based uint32 throughout.
struct Mesh {
    std::vector<Point3> vertices;
    std::vector<Cell> cells;
    std::vector<Edge> edges;
    std::vector<Path> paths;
    std::vector<std::int64_t> vertexIds,cellIds,edgeIds;
    std::vector<std::int32_t> vertexMarkers;
    std::vector<Label> labels;
    std::vector<VertexAlias> aliases;
    Metadata metadata;
};
struct Limits {
    std::uint64_t maxVertices=20000000,maxCells=20000000,maxEdges=40000000;
    std::uint64_t maxWorkspaceBytes=std::uint64_t(4)<<30;
    std::uint64_t maxWork=0; // Zero uses 20 billion orchestration/audit work units.
    std::uint64_t maxMetadataBytes=8*1024*1024,maxRecordBytes=1024*1024;
};
struct OperationReport {
    std::uint32_t schemaVersion=1;
    RunStatus status=RunStatus::Failed;
    std::string code,message;
    Counts counts;
    std::uint64_t work=0,record=0;
    double seconds=0;
    bool complete() const noexcept {return status==RunStatus::Complete || status==RunStatus::CompleteWithExceptions;}
};
// Local connectivity/finite/positive convex-cell validation. It does not prove
// arbitrary nonincident nonoverlap or CRS correctness. Caller owns metadata.
OperationReport validateMesh(const Mesh &,const Limits & = {},const std::function<bool()> &cancelled={});
}
