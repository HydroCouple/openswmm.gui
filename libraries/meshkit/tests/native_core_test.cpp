// SPDX-License-Identifier: Apache-2.0
#include "meshkit/cdt.hpp"
#include "meshkit/job.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" at line "+std::to_string(__LINE__));}while(false)
using namespace meshkit;
static JobRequest rectangle() {
    JobRequest r;r.points={{0,0},{8,0},{8,8},{0,8}};
    r.paths={{{0,1,2,3},PathKind::Outer,10,1,true}};
    r.options.maximumSize=2;r.options.minimumTarget=.25;r.options.minimumAngleDegrees=20;
    return r;
}
int main(){try {
    ConstrainedDelaunay cdt;CHECK(cdt.build({{0,0},{10,0},{10,10},{0,10},{5,5}}));
    for(int i=0;i<4;++i)CHECK(cdt.insertConstraint(i,(i+1)%4));cdt.removeExterior();CHECK(cdt.liveTriangleCount()==4);
    ConstrainedDelaunay::QualityOptions q;q.hAt=[](double,double){return 3.;};q.minAngleDeg=20;q.coarsenPasses=3;
    auto stats=cdt.refineQuality(q);CHECK(!stats.invalidPolicy && !stats.capped && !stats.cancelled);
    auto r=rectangle();auto result=runJob(r);if(!result.report.complete())std::cerr<<result.report.code<<": "<<result.report.message<<'\n';CHECK(result.report.complete());CHECK(result.mesh.cells.size()>2);CHECK(validateMesh(result.mesh).complete());
    r.points.insert(r.points.end(),{{0,0},{8,0},{4,4}});
    r.pointMarkers={1,2,0,0,11,12,0};r.paths.push_back({{4,6,5},PathKind::Line,20,2,false});
    r.paths.push_back({{5,4},PathKind::Line,30,3,false});
    result=runJob(r);if(!result.report.complete())std::cerr<<result.report.code<<": "<<result.report.message<<'\n';CHECK(result.report.complete());CHECK(result.inputVertexToOutput[0]==result.inputVertexToOutput[4]);CHECK(result.inputVertexToOutput[1]==result.inputVertexToOutput[5]);CHECK(result.mesh.aliases.size()==2);
    bool marker20=false,marker30=false;for(auto edge:result.mesh.edges){marker20|=edge.marker==20;marker30|=edge.marker==30;}CHECK(marker20 && marker30);
    r=rectangle();r.points.insert(r.points.end(),{{3,3},{5,3},{5,5},{3,5}});r.paths.push_back({{4,5,6,7},PathKind::Hole,40,4,true});
    result=runJob(r);if(!result.report.complete())std::cerr<<result.report.code<<": "<<result.report.message<<'\n';CHECK(result.report.complete());double area=0;for(const auto &cell:result.mesh.cells){auto a=result.mesh.vertices[cell.vertices[0]],b=result.mesh.vertices[cell.vertices[1]],c=result.mesh.vertices[cell.vertices[2]];area+=((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))*.5;}CHECK(std::abs(area-60)<1e-8);
    r.options.cells=CellPlan::AutomaticMixed;result=runJob(r);CHECK(result.report.complete());
    r=rectangle();r.options.limits.maxWorkspaceBytes=1;CHECK(runJob(r).report.status==RunStatus::ResourceLimit);
    r=rectangle();r.options.limits.maxWork=1;CHECK(runJob(r).report.status==RunStatus::ResourceLimit);
    Providers p;p.cancelled=[] {return true;};EventChannel events;result=runJob(rectangle(),p,&events);CHECK(result.report.status==RunStatus::Cancelled && result.mesh.vertices.empty());CHECK(events.drain().events.back().status==RunStatus::Cancelled);
    p={};p.maximumSize=[](const Box2 &){return std::numeric_limits<double>::quiet_NaN();};CHECK(!runJob(rectangle(),p).report.complete());
    p={};p.elevation=[](double,double)->double{throw std::runtime_error("provider failure");};result=runJob(rectangle(),p);CHECK(result.report.status==RunStatus::Failed && result.mesh.cells.empty());
    r=rectangle();r.options.minimumEdge=100;result=runJob(r);CHECK(result.report.status==RunStatus::Infeasible && result.mesh.cells.empty());
    std::vector<Point2> many;for(int y=0;y<100;++y)for(int x=0;x<200;++x)many.emplace_back(x,y);
    ConstrainedDelaunay cancelledBuild;bool cancelBuild=false;CHECK(!cancelledBuild.build(many,nullptr,[&]{return cancelBuild;},[&](std::uint64_t done){if(done>=4096)cancelBuild=true;}));CHECK(cancelBuild);
    r=rectangle();r.options.coarsenPasses=2;r.options.coarseningOrder=CoarseningOrder::ResolutionAware;
    r.options.terrainTolerance=.01;r.options.requireTerrainCertification=true;
    p={};p.identity="plane-v1";p.elevation=[](double x,double y){return .01*x+.02*y;};p.terrainWitness=[](const Point2 *,const double *,Point2 *){return 0.;};p.terrainBound=[](const Point2 *,const double *){return 0.;};
    result=runJob(r,p);CHECK(result.report.complete());CHECK(!result.report.terrainUnknown && !result.report.terrainConflicts);CHECK(result.report.providerIdentity=="plane-v1");
    p.terrainBound=[](const Point2 *,const double *){return std::numeric_limits<double>::quiet_NaN();};result=runJob(r,p);CHECK(!result.report.complete());CHECK(result.mesh.cells.empty());
    std::cout<<"Native kernel/job tests passed.\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
