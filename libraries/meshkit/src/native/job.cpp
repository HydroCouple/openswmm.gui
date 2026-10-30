// SPDX-License-Identifier: GPL-3.0-or-later
#include "meshkit/job.hpp"
#include "meshkit/cdt.hpp"
#include "operation.hpp"
#include <numeric>
#include <unordered_set>

namespace meshkit {
namespace {
using namespace native_detail;
struct Segment {Point2 a,b;std::uint32_t owner;};
struct Node {Box2 box;int begin=0,end=0,left=-1,right=-1;};
class DomainIndex {
    std::vector<Segment> segments;std::vector<Node> nodes;
    const std::vector<Path> &paths;Work &work;
    int build(int begin,int end){int at=int(nodes.size());Box2 bounds=box(&segments[begin].a,1); // a/b are not treated as an array.
        bounds=Box2({std::min(segments[begin].a.x(),segments[begin].b.x()),std::min(segments[begin].a.y(),segments[begin].b.y())},{std::max(segments[begin].a.x(),segments[begin].b.x()),std::max(segments[begin].a.y(),segments[begin].b.y())});
        for(int i=begin+1;i<end;++i){work.tick();const Point2 p[2]={segments[i].a,segments[i].b};bounds=bounds.united(box(p,2));}
        nodes.push_back({bounds,begin,end});if(end-begin>8){int middle=begin+(end-begin)/2;const bool x=bounds.width()>=bounds.height();
            std::nth_element(segments.begin()+begin,segments.begin()+middle,segments.begin()+end,[&](const Segment &a,const Segment &b){work.tick();const double ca=x?a.a.x()+a.b.x():a.a.y()+a.b.y(),cb=x?b.a.x()+b.b.x():b.a.y()+b.b.y();return ca!=cb?ca<cb:a.owner<b.owner;});
            const int left=build(begin,middle),right=build(middle,end);nodes[at].left=left;nodes[at].right=right;}
        return at;
    }
public:
    DomainIndex(const std::vector<Point2> &points,const std::vector<Path> &p,Work &w):paths(p),work(w){
        for(std::uint32_t owner=0;owner<paths.size();++owner){const auto &path=paths[owner];if(path.kind!=PathKind::Outer && path.kind!=PathKind::Hole)continue;
            for(std::size_t i=0;i<path.vertices.size();++i){work.tick();segments.push_back({points[path.vertices[i]],points[path.vertices[(i+1)%path.vertices.size()]],owner});}}
        if(!segments.empty())build(0,int(segments.size()));
    }
    bool contains(Point2 p){
        if(nodes.empty())return false;
        std::uint64_t bits=0;std::unordered_set<std::uint32_t> odd;
        const Box2 ray({p.x(),p.y()},{nodes.front().box.right(),p.y()});
        auto visit=[&](auto &&self,int at)->void {work.tick();const auto &node=nodes[at];if(!meets(ray,node.box))return;
            if(node.left>=0){self(self,node.left);self(self,node.right);return;}
            for(int i=node.begin;i<node.end;++i){const auto &s=segments[i];if((s.a.y()>p.y())==(s.b.y()>p.y()))continue;
                const double turn=ConstrainedDelaunay::orientExact(s.a,s.b,p);
                if((s.b.y()>s.a.y() && turn>0)||(s.b.y()<s.a.y() && turn<0)){
                    if(paths.size()<=64)bits^=std::uint64_t(1)<<s.owner;else if(!odd.erase(s.owner))odd.insert(s.owner);
                }}
        };visit(visit,0);bool outer=false,hole=false;
        auto mark=[&](std::uint32_t i){outer|=paths[i].kind==PathKind::Outer;hole|=paths[i].kind==PathKind::Hole;};
        if(paths.size()<=64){for(std::uint32_t i=0;i<paths.size();++i)if(bits&(std::uint64_t(1)<<i))mark(i);}else for(auto i:odd)mark(i);
        return outer && !hole;
    }
};
void requireFinite(double v,const char *name,bool zero=true){if(!std::isfinite(v) || (zero?v<0:v<=0))fail("option",std::string("Invalid ")+name+".");}
}

JobResult runJob(const JobRequest &request,const Providers &providers,EventChannel *events) {
    using namespace native_detail;
    JobResult result;auto &report=result.report;const auto start=std::chrono::steady_clock::now();
    report.resolvedOptions=request.options;
    auto publish=[&](EventKind kind,const char *stage,int percent,const std::string &message={}){
        if(events){Event event;event.kind=kind;event.stage=stage;event.percent=percent;event.message=message;event.counts=report.counts;events->publish(std::move(event));}
    };
    try {
        Work work(request.options.limits,providers.cancelled);auto record=detail::scopeExit([&]{report.work=work.used;});
        const auto &o=request.options;const auto &limits=o.limits;
        if(!request.metadata.unitsKnown)fail("units_required","Declare physical unit scales before running a mesh job.");
        publish(EventKind::Stage,"preflight",0);
        if(o.cells!=CellPlan::Triangles)fail("unsupported_cell_plan","The Qt-free backend currently supports triangles. Automatic mixed planning is available through the transitional Qt API.");
        if(request.points.size()<3)fail("domain","At least three source points and an outer path are required.");
        if(request.points.size()>limits.maxVertices || request.points.size()>std::size_t(std::numeric_limits<int>::max()-3))fail("vertex_limit","Source vertex limit exceeded.",RunStatus::ResourceLimit);
        if(!request.pointIds.empty() && request.pointIds.size()!=request.points.size())fail("id_count","Source id count mismatch.");
        if(!request.pointMarkers.empty() && request.pointMarkers.size()!=request.points.size())fail("marker_count","Source marker count mismatch.");
        requireFinite(o.minimumTarget,"minimum target");requireFinite(o.maximumSize,"maximum size");requireFinite(o.minimumEdge,"minimum edge");requireFinite(o.minimumAngleDegrees,"minimum angle");
        requireFinite(o.adjacentRatio,"adjacent ratio");requireFinite(o.terrainTolerance,"terrain tolerance");requireFinite(o.coarseningReferenceSize,"ordering reference size",false);
        if(!std::isfinite(o.flatElevation) || o.minimumAngleDegrees>34 || (o.adjacentRatio>0 && o.adjacentRatio<1) || o.coarsenPasses<0 || o.coarsenPasses>10 || o.smoothingPasses<0 || o.smoothingPasses>10 || o.sizingRepairPasses<0 || o.sizingRepairPasses>3 || !o.terrainPolicy.valid())fail("option","Invalid job policy.");
        if(o.maximumSize>0 && o.minimumTarget>o.maximumSize)fail("size_range","Maximum size precedes the minimum target.");
        if(!(request.metadata.horizontalMetresPerUnit>0) || !(request.metadata.verticalMetresPerUnit>0) || !std::isfinite(request.metadata.horizontalMetresPerUnit) || !std::isfinite(request.metadata.verticalMetresPerUnit))fail("units","Invalid physical unit scales.");
        const double normalScale=request.metadata.verticalMetresPerUnit/request.metadata.horizontalMetresPerUnit;
        if(!(normalScale>0) || !std::isfinite(normalScale))fail("units","Physical normal scale is not representable.");
        std::uint64_t metadataBytes=0;
        auto metadataText=[&](const std::string &text){if(text.size()>limits.maxMetadataBytes-metadataBytes)fail("metadata_limit","Job metadata exceeds its limit.",RunStatus::ResourceLimit);metadataBytes+=text.size();if(!utf8(text))fail("utf8","Job metadata must be UTF-8.");};
        metadataText(request.metadata.crs);metadataText(request.metadata.description);metadataText(providers.identity);
        if(request.labels.size()>limits.maxMetadataBytes/4)fail("metadata_limit","Too many job labels.",RunStatus::ResourceLimit);
        std::unordered_set<std::int32_t> labelIds;for(const auto &label:request.labels){work.tick();metadataText(label.name);if(!labelIds.insert(label.id).second)fail("label","Duplicate job label id.");}
        for(const auto &seed:request.regions){work.tick();if(!finite(seed.point))fail("region_seed","Region seed is nonfinite.");}
        const bool terrain=o.terrainTolerance>0;
        if((terrain && (!providers.elevation || !providers.terrainWitness || !providers.terrainBound)) || (!terrain && (o.requireTerrainCertification || o.terrainPolicy.mode==TerrainToleranceMode::TargetSize)))fail("terrain_provider","Terrain certification requires elevation, witness, bound and positive reference tolerance.");
        report.providerIdentity=providers.identity;
        work.memory(request.points.size()*256ull);
        if(!limits.maxCells)fail("cell_limit","The cell limit is zero.",RunStatus::ResourceLimit);
        std::int64_t highestId=0;std::unordered_set<std::int64_t> ids;
        if(!request.pointIds.empty()){ids.reserve(request.pointIds.size());for(auto id:request.pointIds){work.tick();if(id<=0 || !ids.insert(id).second)fail("source_id","Job source ids must be unique and positive.");highestId=std::max(highestId,id);}}
        else highestId=request.points.size();
        std::uint64_t sourceEdges=0;bool hasOuter=false;
        for(const auto &path:request.paths){work.tick();hasOuter|=path.kind==PathKind::Outer;if(int(path.kind)>int(PathKind::Region))fail("path_kind","Unknown path kind.");
            if(path.vertices.size()<2)fail("path_geometry","Source path has fewer than two vertices.");
            for(auto index:path.vertices){work.tick();if(index>=request.points.size())fail("path_index","Source path index is out of range.");}
            sourceEdges+=path.vertices.size();if(sourceEdges>limits.maxEdges)fail("edge_limit","Source path limit exceeded.",RunStatus::ResourceLimit);}
        if(!hasOuter)fail("domain","An explicit outer path is required; no bounding box is inferred.");
        work.memory(request.points.size()*256ull+sourceEdges*128ull);
        auto maximumSize=[&](const Box2 &bounds){double h=providers.maximumSize?providers.maximumSize(bounds):0;
            if(!std::isfinite(h)||h<0)fail("size_provider","Invalid prescribed footprint size.");
            if(o.maximumSize>0)h=h>0?std::min(h,o.maximumSize):o.maximumSize;return h;};
        auto tolerance=[&](const Box2 &bounds){const double v=o.terrainPolicy.tolerance(o.terrainTolerance,maximumSize(bounds));if(!(v>0) || !std::isfinite(v))fail("terrain_policy","Invalid resolved footprint terrain tolerance.");return v;};
        auto elevation=[&](double x,double y){return providers.elevation?providers.elevation(x,y):o.flatElevation;};
        ConstrainedDelaunay cdt;ConstrainedDelaunay::Vector<int> inputMap;
        publish(EventKind::Stage,"triangulation",5);
        work.tick(request.points.size());
        if(!cdt.build(request.points,&inputMap,providers.cancelled,[&](std::uint64_t done){work.check();report.counts.workingVertices=request.points.size();publish(EventKind::Progress,"triangulation",5+int(25*done/request.points.size()));})){
            work.check();fail("triangulation",cdt.errorMsg());}
        work.check();
        std::vector<Index> first(cdt.inputVertexCount(),missingIndex);
        for(Index i=0;i<inputMap.size();++i){work.tick();if(first[inputMap[i]]==missingIndex)first[inputMap[i]]=i;}
        // Canonical identities are shared across independent paths. Consecutive
        // duplicate coordinates/closing records do not become zero-length edges.
        std::vector<Path> paths=request.paths;
        for(auto &path:paths){std::vector<Index> vertices;for(auto source:path.vertices){const auto canonical=first[inputMap[source]];if(vertices.empty() || vertices.back()!=canonical)vertices.push_back(canonical);}
            const bool closed=path.kind!=PathKind::Line || path.closed;
            if(closed && vertices.size()>1 && vertices.front()==vertices.back())vertices.pop_back();
            if(vertices.size()<(closed?3u:2u))fail("path_geometry","Source path collapses to fewer than the required distinct vertices.");
            if(closed){std::unordered_set<Index> distinct;for(auto v:vertices)if(!distinct.insert(v).second)fail("self_touching_ring","One ring repeats a vertex away from its closure; split the ring into unambiguous components.");}
            path.vertices=std::move(vertices);path.closed=closed;}
        DomainIndex domain(request.points,paths,work);
        publish(EventKind::Stage,"constraints",32);
        for(const auto &path:paths){const std::size_t count=path.vertices.size()-(path.closed?0:1);
            for(std::size_t k=0;k<count;++k){work.tick();if(!cdt.insertConstraint(inputMap[path.vertices[k]],inputMap[path.vertices[(k+1)%path.vertices.size()]]))fail("constraint",cdt.errorMsg());}}
        cdt.removeExterior();
        // Classify connected faces, not every triangle against every source
        // polygon. Region removal respects every constrained edge.
        const auto &faces=cdt.triangles();std::vector<std::uint8_t> visited(faces.size(),0);std::vector<int> stack;
        for(int t=0;t<faces.size();++t){work.tick();if(!faces[t].alive || visited[t])continue;
            const auto &cell=faces[t];const auto a=cdt.vertices()[cell.v[0]],b=cdt.vertices()[cell.v[1]],c=cdt.vertices()[cell.v[2]];const auto center=a+((b-a)+(c-a))/3;
            if(!domain.contains(center)){cdt.removeRegionAt(center);continue;}
            stack.push_back(t);visited[t]=1;while(!stack.empty()){work.tick();const int at=stack.back();stack.pop_back();const auto &f=faces[at];for(int k=0;k<3;++k){const int n=f.adj[k];if(!f.constrained[k] && n>=0 && faces[n].alive && !visited[n]){visited[n]=1;stack.push_back(n);}}}}
        visited.clear();visited.shrink_to_fit();stack.clear();stack.shrink_to_fit();
        if(cdt.liveTriangleCount()==0)fail("empty_domain","No cells remain inside the requested domain.");
        if(std::uint64_t(cdt.liveTriangleCount())>limits.maxCells)fail("cell_limit","Initial constrained triangulation exceeds the cell limit.",RunStatus::ResourceLimit);
        ConstrainedDelaunay::QualityOptions quality;
        quality.hAt=[&](double x,double y){return maximumSize({{x,y},{x,y}});};
        quality.maximumAreaForTriangle=[&](const Point2 *p){const double h=maximumSize(box(p,3));return h>0?.4330127018922193*h*h:0.;};
        quality.minAngleDeg=o.minimumAngleDegrees;
        const double floor=o.minimumTarget>0?o.minimumTarget:(o.maximumSize>0?o.maximumSize/4:0);
        quality.minEdge=std::max(o.minimumEdge,floor*.25);quality.terrainMinSpacing=floor;
        quality.maxTriangles=int(std::min<std::uint64_t>(limits.maxCells,std::numeric_limits<int>::max()-8));
        quality.maxInsertions=int(std::min<std::uint64_t>(limits.maxVertices,std::numeric_limits<int>::max()-8));
        quality.smoothingPasses=o.smoothingPasses;quality.coarsenPasses=o.coarsenPasses;quality.coarseningOrder=o.coarseningOrder;
        quality.coarseningTargetSize=maximumSize;quality.coarseningReferenceSize=o.coarseningReferenceSize;
        quality.normalVerticalToXY=normalScale;
        quality.maximumAdjacentRatio=o.adjacentRatio;quality.sizingRepairPasses=o.sizingRepairPasses;quality.maximumSizingRepairInsertions=int(std::min<std::uint32_t>(o.repairInsertions,std::numeric_limits<int>::max()));
        quality.cancelled=providers.cancelled;
        if(terrain){quality.terrainTolerance=o.terrainTolerance;quality.terrainToleranceForFootprint=tolerance;quality.terrainElevationAt=elevation;quality.terrainError=providers.terrainWitness;quality.terrainErrorBound=providers.terrainBound;quality.terrainWorstFirst=true;quality.terrainFinalCheck=false;}
        quality.onProgress=[&](const GenerationProgress &state){work.check();report.counts.workingVertices=state.workingVertices;report.counts.triangles=state.triangles;report.counts.quads=0;report.counts.inserted=state.inserted;report.counts.removed=state.removed;
            if(std::uint64_t(state.workingVertices)>limits.maxVertices)fail("vertex_limit","Working vertex limit exceeded.",RunStatus::ResourceLimit);
            work.memory(std::uint64_t(state.workingVertices)*128+std::uint64_t(state.triangles)*176+sourceEdges*128);
            publish(EventKind::Progress,"refinement",40);};
        publish(EventKind::Stage,"refinement",40);const auto stats=cdt.refineQuality(quality);
        report.inserted=stats.inserted;report.removed=stats.coarsenRemoved;report.removalAttempts=stats.coarsenAttempts;report.queuePeak=stats.coarsenQueuePeak;
        if(stats.cancelled){fail("cancelled","Cancelled.",RunStatus::Cancelled);}if(stats.capped || stats.sizingRepairLimited)fail("refinement_limit","Refinement/repair limit reached; no partial candidate returned.",RunStatus::ResourceLimit);
        if(stats.invalidPolicy)fail("policy","Invalid size/terrain/coarsening provider or policy.");
        work.check();work.memory(cdt.vertices().size()*128ull+cdt.triangles().size()*176ull+sourceEdges*128ull);
        publish(EventKind::Stage,"assembly",75);
        std::vector<Index> outMap(cdt.vertices().size(),missingIndex);
        for(const auto &cell:cdt.triangles())if(cell.alive)for(int v:cell.v)outMap[v]=0;
        auto appendId=[](std::vector<std::int64_t> &values,std::int64_t id,std::size_t index){if(values.empty() && id==std::int64_t(index+1))return;if(values.empty()){values.reserve(index+1);for(std::size_t i=0;i<index;++i)values.push_back(i+1);}values.push_back(id);};
        auto &mesh=result.mesh;mesh.metadata=request.metadata;mesh.labels=request.labels;
        for(int v=0;v<outMap.size();++v){work.tick();if(outMap[v]==missingIndex)continue;const auto p=cdt.vertices()[v];const double z=elevation(p.x(),p.y());if(!std::isfinite(z))fail("elevation","Elevation provider returned a nonfinite delivered height.");
            const auto index=Index(mesh.vertices.size());outMap[v]=index;mesh.vertices.push_back({p.x(),p.y(),z});
            std::int64_t id=0;std::int32_t marker=0;
            if(v<cdt.inputVertexCount()){const auto source=first[v];id=request.pointIds.empty()?std::int64_t(source+1):request.pointIds[source];marker=request.pointMarkers.empty()?0:request.pointMarkers[source];}
            else {if(highestId==std::numeric_limits<std::int64_t>::max())fail("external_id_limit","No positive id remains for a generated vertex.",RunStatus::ResourceLimit);id=++highestId;}
            appendId(mesh.vertexIds,id,index);
            if(marker || !mesh.vertexMarkers.empty()){if(mesh.vertexMarkers.empty())mesh.vertexMarkers.resize(index,0);mesh.vertexMarkers.push_back(marker);}}
        for(Index i=0;i<inputMap.size();++i){work.tick();const auto output=outMap[inputMap[i]];if(output==missingIndex)fail("unused_source_point","A required source point is outside the retained domain or a hole.");
            const auto primary=first[inputMap[i]];if(i!=primary)mesh.aliases.push_back({output,request.pointIds.empty()?std::int64_t(i+1):request.pointIds[i],request.pointMarkers.empty()?0:request.pointMarkers[i]});}
        if(o.retainInputMapping){result.inputVertexToOutput.resize(inputMap.size());for(std::size_t i=0;i<inputMap.size();++i)result.inputVertexToOutput[i]=outMap[inputMap[i]];}
        std::vector<Index> cellMap(cdt.triangles().size(),missingIndex);
        for(int t=0;t<cdt.triangles().size();++t){const auto &cell=cdt.triangles()[t];if(!cell.alive)continue;work.tick();cellMap[t]=mesh.cells.size();mesh.cells.push_back({{outMap[cell.v[0]],outMap[cell.v[1]],outMap[cell.v[2]],0},0,3});}
        for(std::size_t pi=0;pi<paths.size();++pi){const auto &path=paths[pi];Path delivered=path;delivered.vertices.clear();if(delivered.id==0)delivered.id=pi+1;
            const std::size_t count=path.vertices.size()-(path.closed?0:1);
            for(std::size_t k=0;k<count;++k){work.tick();const auto chain=cdt.constrainedChain(inputMap[path.vertices[k]],inputMap[path.vertices[(k+1)%path.vertices.size()]]);
                if(chain.size()<2)fail("constraint_chain","A required source edge is missing from delivery.");
                for(int j=0;j+1<chain.size();++j){const auto a=outMap[chain[j]],b=outMap[chain[j+1]];if(a==missingIndex || b==missingIndex || a==b)fail("constraint_domain","A required edge leaves the retained domain.");mesh.edges.push_back({a,b,path.marker});delivered.vertices.push_back(a);}
                if(!path.closed && k+1==count)delivered.vertices.push_back(outMap[chain.back()]);}
            mesh.paths.push_back(std::move(delivered));}
        // Region seeds flood across free faces only; conflicting tags fail.
        std::vector<bool> assigned(mesh.cells.size(),false);
        for(const auto &seed:request.regions){work.tick();const int t=cdt.locate(seed.point);if(t<0 || !cdt.triangles()[t].alive || cellMap[t]==missingIndex)fail("region_seed","Region seed is outside the retained domain.");
            const auto &seedCell=cdt.triangles()[t];
            for(int k=0;k<3;++k)if(seedCell.constrained[k] && ConstrainedDelaunay::orientExact(cdt.vertices()[seedCell.v[(k+1)%3]],cdt.vertices()[seedCell.v[(k+2)%3]],seed.point)==0)
                fail("region_seed_on_constraint","A region seed lies on a constraint; choose a strict interior point.");
            stack.push_back(t);while(!stack.empty()){work.tick();const int at=stack.back();stack.pop_back();const auto index=cellMap[at];if(assigned[index]){if(mesh.cells[index].region!=seed.region)fail("region_conflict","Conflicting region seeds in one connected component.");continue;}assigned[index]=true;mesh.cells[index].region=seed.region;
                const auto &cell=cdt.triangles()[at];for(int k=0;k<3;++k){const int n=cell.adj[k];if(n>=0 && !cell.constrained[k] && cdt.triangles()[n].alive)stack.push_back(n);}}
        }
        publish(EventKind::Stage,"validation",85);
        const auto validation=validateMesh(mesh,limits,providers.cancelled);if(!validation.complete())fail(validation.code,validation.message,validation.status);
        struct Face {std::uint64_t key;Index cell;};std::vector<Face> records;records.reserve(mesh.cells.size()*3);
        std::vector<double> sizes(mesh.cells.size());
        auto example=[&](const char *code,Index cell,double measured,double allowed){if(report.examples.size()<64)report.examples.push_back({code,cell,measured,allowed});};
        for(Index i=0;i<mesh.cells.size();++i){work.tick();const auto &cell=mesh.cells[i];Point2 p[3];double z[3];for(int k=0;k<3;++k){const auto &v=mesh.vertices[cell.vertices[k]];p[k]={v.x,v.y};z[k]=v.z;records.push_back({edgeKey(cell.vertices[k],cell.vertices[(k+1)%3]),i});}
            const auto b=p[1]-p[0],c=p[2]-p[0];const double area=(b.x()*c.y()-b.y()*c.x())*.5;const double h=std::sqrt(area/.4330127018922193);sizes[i]=h;
            if(o.minimumTarget>0 && h<o.minimumTarget*(1-1e-9))++report.belowMinimum;
            const double cap=maximumSize(box(p,3));if(cap>0 && h>cap*(1+1e-9)){++report.sizeConflicts;example("size",i,h,cap);}
            for(int k=0;k<3;++k){const auto e=p[(k+1)%3]-p[k];const double length=std::hypot(e.x(),e.y());if(o.minimumEdge>0 && length<o.minimumEdge*(1-1e-9)){++report.edgeConflicts;example("edge",i,length,o.minimumEdge);}}
            if(terrain){const double allowed=tolerance(box(p,3));const double error=providers.terrainBound(p,z);if(!std::isfinite(error) || error<0){++report.terrainUnknown;example("terrain_unknown",i,0,allowed);}else{report.largestTerrainRatio=std::max(report.largestTerrainRatio,error/allowed);if(error>allowed*(1+1e-9)){++report.terrainConflicts;example("terrain",i,error,allowed);}}}}
        std::sort(records.begin(),records.end(),[&](const Face &a,const Face &b){work.tick();return a.key!=b.key?a.key<b.key:a.cell<b.cell;});
        for(std::size_t at=0;at<records.size();){work.tick();std::size_t end=at+1;while(end<records.size() && records[end].key==records[at].key)++end;if(end-at>2)fail("nonmanifold","A delivered face has more than two cells.");
            if(end-at==2){const auto a=records[at].cell,b=records[at+1].cell;const double ratio=std::max(sizes[a],sizes[b])/std::min(sizes[a],sizes[b]);report.largestAdjacentRatio=std::max(report.largestAdjacentRatio,ratio);if(o.adjacentRatio>0 && ratio>o.adjacentRatio*(1+1e-9)){++report.transitionConflicts;example("transition",a,ratio,o.adjacentRatio);}}at=end;}
        for(const auto &edge:mesh.edges){work.tick();const auto key=edgeKey(edge.a,edge.b);const auto found=std::lower_bound(records.begin(),records.end(),key,[](const Face &face,std::uint64_t value){return face.key<value;});if(found==records.end() || found->key!=key)fail("unattached_constraint","A required edge is not incident to a delivered cell.");}
        if(o.strictSizing && (report.sizeConflicts || report.edgeConflicts || report.transitionConflicts))fail("unresolved_size","Requested size/edge/transition requirements remain unresolved.");
        if(o.requireTerrainCertification && (report.terrainConflicts || report.terrainUnknown))fail("unverified_terrain","Terrain tolerance or coverage could not be certified.");
        report.status=(report.belowMinimum || report.sizeConflicts || report.edgeConflicts || report.transitionConflicts || report.terrainConflicts || report.terrainUnknown)?RunStatus::CompleteWithExceptions:RunStatus::Complete;
        report.counts.vertices=mesh.vertices.size();report.counts.triangles=mesh.cells.size();report.counts.quads=0;report.code="complete";report.message="Mesh job complete.";
    }catch(const Stop &e){report.status=e.status;report.code=e.code;report.message=e.message;result.mesh={};result.inputVertexToOutput.clear();}
    catch(const std::bad_alloc &){report.status=RunStatus::ResourceLimit;report.code="allocation";report.message="Insufficient memory.";result.mesh={};result.inputVertexToOutput.clear();}
    catch(const std::exception &e){report.status=RunStatus::Failed;report.code="provider_exception";report.message=e.what();result.mesh={};result.inputVertexToOutput.clear();}
    catch(...){report.status=RunStatus::Failed;report.code="exception";report.message="Unknown job exception.";result.mesh={};result.inputVertexToOutput.clear();}
    report.seconds=seconds(start);
    if(events){Event event;event.kind=EventKind::Completed;event.stage="complete";event.status=report.status;event.code=report.code;event.message=report.message;event.counts=report.counts;event.percent=report.complete()?100:-1;event.severity=report.complete()?Severity::Info:Severity::Error;events->publish(std::move(event));}
    return result;
}
}
