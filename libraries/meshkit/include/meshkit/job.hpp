// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mesh.hpp"
#include "terrainpolicy.hpp"
#include <functional>

namespace meshkit {
enum class CellPlan:std::uint8_t { Triangles,AutomaticMixed };
struct RegionSeed {Point2 point;std::int32_t region=0;};
struct JobOptions {
    CellPlan cells=CellPlan::Triangles;
    double minimumTarget=0,maximumSize=0,minimumEdge=0,minimumAngleDegrees=30;
    double adjacentRatio=0,flatElevation=0;
    int smoothingPasses=0,coarsenPasses=0,sizingRepairPasses=2;
    std::uint32_t repairInsertions=200000;
    CoarseningOrder coarseningOrder=CoarseningOrder::LegacyFlatness;
    double coarseningReferenceSize=10,terrainTolerance=0;
    TerrainTolerancePolicy terrainPolicy;
    bool strictSizing=true,requireTerrainCertification=false,retainInputMapping=true;
    Limits limits;
};
struct JobRequest {
    std::vector<Point2> points;
    std::vector<Path> paths;
    std::vector<std::int64_t> pointIds;
    std::vector<std::int32_t> pointMarkers;
    std::vector<Label> labels;
    std::vector<RegionSeed> regions;
    Metadata metadata;
    JobOptions options;
};
struct Providers {
    // Immutable, synchronous native callbacks on the job thread. They must not
    // reenter the job or mutate request arrays. Exceptions fail the whole job.
    // Footprint bounds include every prescribed source, including narrow strips.
    std::function<double(const Box2 &)> maximumSize;
    std::function<double(double,double)> elevation;
    std::function<double(const Point2 *,const double *,Point2 *)> terrainWitness;
    std::function<double(const Point2 *,const double *)> terrainBound;
    std::function<bool()> cancelled;
    std::string identity; ///< Caller supplied dataset/provider version or fingerprint.
};
struct JobException {std::string code;Index cell=missingIndex;double measured=0,allowed=0;};
struct JobReport:OperationReport {
    std::string algorithmVersion="cdt-1/job-1";
    JobOptions resolvedOptions;
    std::string providerIdentity;
    std::uint64_t inserted=0,removed=0,removalAttempts=0,queuePeak=0;
    std::uint64_t belowMinimum=0,edgeConflicts=0,sizeConflicts=0,transitionConflicts=0;
    std::uint64_t terrainConflicts=0,terrainUnknown=0;
    double largestAdjacentRatio=1,largestTerrainRatio=0;
    std::vector<JobException> examples; // At most 64, never one object per cell.
};
struct JobResult {Mesh mesh;std::vector<Index> inputVertexToOutput;JobReport report;};
struct Capabilities {
    bool qtFree=true,triangles=true,automaticMixed=false;
    bool terrainProviders=true,coarsening=true,streamingIO=true;
};
constexpr Capabilities nativeCapabilities(){return {};}
// Borrowed immutable inputs for this synchronous call; owned result buffers.
// No mesh is returned on infeasible/cancelled/resource/failed jobs. The native
// backend currently supports triangles; unsupported shape requests fail clearly.
// CompleteWithExceptions is distinct from a certified result. No CRS conversion,
// DEM access, Python callbacks, or file publication occurs inside runJob.
JobResult runJob(const JobRequest &,const Providers & = {},EventChannel *events=nullptr);
}
