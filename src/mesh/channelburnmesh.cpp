// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh/channelburnmesh.h"
#include "mesh/channelburn.h"
#include "mesh/meshcavity.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshedgebcremap.h"
#include "mesh/meshnodemapper.h"
#include <QLineF>
#include <QMap>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace mesh {
namespace {
bool crosses(const QPointF &a,const QPointF &b,const QPointF &c,const QPointF &d,QPointF *p=nullptr) {
    return QLineF(a,b).intersects(QLineF(c,d),p)==QLineF::BoundedIntersection;
}
bool overlaps(const QPolygonF &a,const QPolygonF &b) {
    if(!a.boundingRect().intersects(b.boundingRect()))return false;
    for(const auto &p:a)if(b.containsPoint(p,Qt::OddEvenFill))return true;
    for(const auto &p:b)if(a.containsPoint(p,Qt::OddEvenFill))return true;
    for(int i=0;i<a.size();++i)for(int j=0;j<b.size();++j)
        if(crosses(a[i],a[(i+1)%a.size()],b[j],b[(j+1)%b.size()]))return true;
    return false;
}
bool closerThan(const QPolygonF &a,const QPolygonF &b,double distance) {
    for(const auto &p:a)for(int j=0;j<b.size();++j)
        if(meshSegmentDistance(p,b[j],b[(j+1)%b.size()])<distance)return true;
    for(const auto &p:b)for(int j=0;j<a.size();++j)
        if(meshSegmentDistance(p,a[j],a[(j+1)%a.size()])<distance)return true;
    return false;
}
QPolygonF cellRing(const MeshResult &m,const MeshCell &c) {
    QPolygonF ring;for(int k=0;k<c.vertexCount();++k)ring.append(m.vertices[c.vertex(k)].xy);return ring;
}
void measure(ChannelMeshQuality &q,const MeshResult &m,const MeshCell &c) {
    ++q.cells;if(c.isQuad())++q.quads;double worst=180;
    for(int k=0;k<c.vertexCount();++k) {
        const auto p=m.vertices[c.vertex(k)].xy;
        const auto a=m.vertices[c.vertex((k+1)%c.vertexCount())].xy-p,b=m.vertices[c.vertex((k+c.vertexCount()-1)%c.vertexCount())].xy-p;
        double la=std::hypot(a.x(),a.y()),lb=std::hypot(b.x(),b.y());q.minEdge=std::min(q.minEdge,la);
        double angle=std::acos(std::clamp(QPointF::dotProduct(a,b)/(la*lb),-1.0,1.0))*180/std::acos(-1.0);
        worst=std::min(worst,angle);
    }
    q.minAngle=std::min(q.minAngle,worst);if(worst<25)++q.below25;if(worst<10)++q.below10;
}
}
ChannelMeshBurnResult burnChannelsIntoMesh(const MeshResult &old,ChannelMeshBurnInputs in,
    const std::function<void(int,const QString &)> &progress,const std::function<bool()> &cancelled) {
    ChannelMeshBurnResult out;
    out.warnings=in.warnings;
    auto stop=[&] {if(cancelled&&cancelled()) {out.cancelled=true;out.error=QStringLiteral("Cancelled.");out.mesh={};out.edgeBCs.clear();out.oldToNewCell.clear();out.oldToNewVertex.clear();return true;}return false;};
    auto report=[&](int p,const QString &s) {if(progress)progress(p,s);};
    if(stop())return out;
    if(!(in.options.channelAspectMax>=1) || !std::isfinite(in.options.channelAspectMax) || !(in.verticalUnitToSI>0) || !std::isfinite(in.verticalUnitToSI) || !(in.clearance>0) || !std::isfinite(in.clearance) || in.maxCells<1 || in.options.maxCorridorVertices<1) {out.error=QStringLiteral("Invalid channel spacing, aspect ratio, units or clearance.");return out;}
    report(2,QStringLiteral("Reading the existing mesh outline and surface…"));
    if(old.triangles.size()>in.maxCells) {out.error=QStringLiteral("The existing mesh exceeds the channel burn cell budget.");return out;}
    MeshCavityTopology top;if(!top.build(old,cancelled)) {if(stop())return out;out.error=top.error;return out;}
    double spacing=in.options.channelCellSize;
    if(!(spacing>0)) {
        QVector<double> sizes;
        for(const auto &profile:in.profiles)for(const auto &p:profile.centerline) {int c=top.cellAt(old,p);if(c>=0)sizes.append(top.cellSize[c]);}
        if(sizes.isEmpty())sizes=top.cellSize;
        std::sort(sizes.begin(),sizes.end());spacing=sizes[sizes.size()/2];
    }
    if(!(spacing>0)||!std::isfinite(spacing)) {out.error=QStringLiteral("Cannot determine the channel cell size.");return out;}
    const double floor=spacing/in.options.channelAspectMax;
    const double tolerance=in.options.geometryTolerance/in.verticalUnitToSI;
    BurnReplacementPlan plan;QVector<BurnLattice> lattices;
    report(8,QStringLiteral("Planning channel intervals and network changes…"));
    // Monotonic re-planning: each rejected source is removed, so chains of
    // inflow-fed headwaters terminate without an arbitrary iteration limit.
    for(;;) {
        if(stop())return out;
        plan=planBurnReplacement(in.profiles,in.options.removeBurnedFrom1D?in.network:BurnNetwork{},top.domain);
        if(!plan.error.isEmpty()) {out.error=plan.error;return out;}
        QSet<QString> rejected;lattices.clear();qint64 vertices=0;
        for(const auto &profile:plan.profiles) {
            if(stop())return out;
            QString error;QStringList notes;
            auto lat=buildCorridorLattice(profile,spacing,0,&notes,&error,0,floor);
            if(!lat.isValid()) {rejected.insert(plan.intervalSource.value(profile.conduitId,profile.conduitId));out.warnings<<error;}
            vertices+=lat.xy.size();
            if(vertices>in.options.maxCorridorVertices) {out.error=QStringLiteral("Channel lattices exceed the vertex budget; increase channel cell size.");return out;}
            out.warnings+=notes;lattices.append(lat);
        }
        if(vertices>in.options.maxCorridorVertices) {out.error=QStringLiteral("Channel lattices exceed the vertex budget; increase channel cell size.");return out;}
        if(in.options.removeBurnedFrom1D)for(const auto &node:plan.nodes)if(node.role==BurnNodeRole::Outfall&&node.survivingLinks==0) {
            for(const auto &link:plan.network.links)if(plan.replacedIds.contains(link.id)
                && ((link.from>=0&&plan.network.nodes[link.from].id==node.nodeId)||(link.to>=0&&plan.network.nodes[link.to].id==node.nodeId)))
                rejected.insert(plan.intervalSource.value(link.id,link.id));
        }
        if(rejected.isEmpty())break;
        out.warnings<<QStringLiteral("Kept in 1D (invalid corridor or an outfall without a surviving link): %1").arg(QStringList(rejected.values()).join(", "));
        in.profiles.erase(std::remove_if(in.profiles.begin(),in.profiles.end(),[&](const auto &p){return rejected.contains(p.conduitId);}),in.profiles.end());
    }
    if(plan.profiles.isEmpty()) {out.error=QStringLiteral("No eligible channel interval lies inside the active mesh.");return out;}
    out.warnings+=plan.notes;out.profiles=plan.profiles;out.lattices=lattices;
    BurnSurface surface;surface.build(lattices);
    auto elevation=[&](const QPointF &p) {
        double z=0;top.cellAt(old,p,&z);const auto hit=surface.sample(p);
        if(hit.profile>=0)burnPixel(z,!std::isfinite(z),hit.z,hit.offset,{in.options.forceHalfWidth,in.options.maxIncision},&z);
        return z;
    };
    struct Band {int profile,row;QPolygonF ring;bool quad=true;};
    QVector<Band> bands;QVector<QRectF> boxes;
    for(int p=0;p<lattices.size();++p)for(int row=0;row+1<lattices[p].nAlong;++row) {
        auto ring=corridorRing(latticeRows(lattices[p],row,row+1));bands.append({p,row,ring,in.options.quadCorridor});boxes.append(ring.boundingRect());
    }
    MeshBoxIndex bandIndex;bandIndex.build(boxes);
    QVector<QPointF> outlinePoints;
    for(int i=0;i<bands.size();++i) {
        if(stop())return out;
        auto &band=bands[i];
        for(const auto &p:band.ring)if(top.cellAt(old,p)<0)band.quad=false;
        const auto box=band.ring.boundingRect().adjusted(-floor,-floor,floor,floor);
        for(int oi:top.outlines.near(box)) {
            const auto &edge=top.edges[top.outlineEdges[oi]];const auto a=old.vertices[edge.a].xy,b=old.vertices[edge.b].xy;
            for(int j=0;j<band.ring.size();++j) {
                const auto p=band.ring[j],q=band.ring[(j+1)%band.ring.size()];QPointF hit;
                if(crosses(a,b,p,q,&hit)) {band.quad=false;outlinePoints.append(hit);}
                if(meshSegmentDistance(p,a,b)<floor || meshSegmentDistance(a,p,q)<floor)band.quad=false;
            }
            // Capture all longitudinal strings at the outline, especially
            // the thalweg and crest, without constraining FREE-band interiors.
            const auto &lat=lattices[band.profile];
            for(int k=0;k<lat.nAcross;++k) {QPointF hit;if(crosses(a,b,lat.xy[lat.at(band.row,k)],lat.xy[lat.at(band.row+1,k)],&hit))outlinePoints.append(hit);}
        }
        // Disjoint neighboring corridors also need a FREE transition when
        // their bank constraints would trap a gap narrower than the spacing
        // floor. Overlap alone misses these unavoidable fixed-edge slivers.
        for(int j:bandIndex.near(box))if(j!=i&&bands[j].profile!=band.profile
            &&(overlaps(band.ring,bands[j].ring)||closerThan(band.ring,bands[j].ring,floor))) {
            band.quad=false;break;
        }
    }
    report(20,QStringLiteral("Opening local cavities around the channels…"));
    QVector<bool> removed(old.triangles.size(),false),opened(old.vertices.size(),false),touched(old.triangles.size(),false);
    for(int v=0;v<old.vertices.size();++v) {
        if((v&4095)==0&&stop())return out;
        if(top.outlineVertex[v])continue;
        opened[v]=surface.sampleNear(old.vertices[v].xy,in.clearance*std::max(spacing,top.vertexSize[v])).profile>=0;
    }
    for(int c=0;c<old.triangles.size();++c) {
        if((c&4095)==0&&stop())return out;
        const auto ring=cellRing(old,old.triangles[c]);
        for(int bi:bandIndex.near(ring.boundingRect()))if(overlaps(ring,bands[bi].ring)) {touched[c]=true;break;}
        bool near=touched[c];for(int k=0;k<old.triangles[c].vertexCount();++k)near=near||opened[old.triangles[c].vertex(k)];
        removed[c]=near&&(!old.triangles[c].tag.startsWith("channel:")||touched[c]);
    }
    // Open small cavity wedges instead of forcing refinement to reproduce
    // them. Outline corners and earlier non-overlapping burns are fixed.
    bool changed=true;
    while(changed) {
        if(stop())return out;changed=false;
        for(int v=0;v<old.vertices.size();++v)if(!top.outlineVertex[v]) {
            double wedge=0;bool kept=false,protectedCell=false;
            for(int c:top.vertexCells[v]) {
                if(!removed[c]) {kept=true;protectedCell|=old.triangles[c].tag.startsWith("channel:");continue;}
                const auto &cell=old.triangles[c];int k=0;while(cell.vertex(k)!=v)++k;
                auto a=old.vertices[cell.vertex((k+1)%cell.vertexCount())].xy-old.vertices[v].xy;
                auto b=old.vertices[cell.vertex((k+cell.vertexCount()-1)%cell.vertexCount())].xy-old.vertices[v].xy;
                wedge+=std::acos(std::clamp(QPointF::dotProduct(a,b)/(std::hypot(a.x(),a.y())*std::hypot(b.x(),b.y())),-1.0,1.0))*180/std::acos(-1.0);
            }
            if(kept&&!protectedCell&&wedge>0&&wedge<50) {for(int c:top.vertexCells[v])removed[c]=true;changed=true;}
        }
    }
    const auto parts=top.cavities(removed);out.cavities=parts.size();QVector<MeshResult> fills;
    QVector<bool> measured=removed;
    qint64 currentCells=old.triangles.size();
    for(int pi=0;pi<parts.size();++pi) {
        if(stop())return out;const auto &part=parts[pi];QSet<int> selected(part.cbegin(),part.cend());
        QVector<PatchMesh> blocks;QVector<QPointF> freePoints,cuts;
        const auto boundary=top.boundary(part);
        auto inPart=[&](const QPointF &p){return selected.contains(top.cellAt(old,p));};
        QRectF partBox;
        QVector<QRectF> boundaryBoxes;
        for(auto edge:boundary) {
            const auto box=QRectF(old.vertices[edge.first].xy,old.vertices[edge.second].xy).normalized();
            boundaryBoxes.append(box);partBox=partBox.united(box.adjusted(-floor,-floor,floor,floor));
        }
        MeshBoxIndex boundaryIndex;boundaryIndex.build(boundaryBoxes);
        QMap<int,QVector<int>> localBands;
        for(int bi:bandIndex.near(partBox))localBands[bands[bi].profile].append(bi);
        // Adjacent eligible bands become a single block. Only inspect bands
        // near this cavity, and only nearby interface edges for each point.
        for(auto it=localBands.cbegin();it!=localBands.cend();++it) {
            const int p=it.key();int first=-1,last=-1;
            auto flush=[&] {if(first<0)return;QString error;auto block=corridorPatch(latticeRows(lattices[p],first,last+1),plan.profiles[p],in.options,&error);if(!block.quads.isEmpty())blocks.append(block);else out.warnings<<error;first=last=-1;};
            for(int bi:it.value()) {
                if(stop())return out;
                const auto &band=bands[bi];bool eligible=band.quad;
                if(first>=0&&band.row!=last+1)flush();
                for(const auto &point:band.ring)if(!inPart(point)) {eligible=false;break;}
                if(eligible)for(const auto &point:band.ring) {
                    for(int ei:boundaryIndex.near(QRectF(point-QPointF(floor,floor),point+QPointF(floor,floor)))) {
                        const auto e=boundary[ei];
                        if(meshSegmentDistance(point,old.vertices[e.first].xy,old.vertices[e.second].xy)<floor) {eligible=false;break;}
                    }
                    if(!eligible)break;
                }
                if(eligible) {if(first<0)first=band.row;last=band.row;}
                else {flush();for(const auto &point:band.ring)if(inPart(point))freePoints.append(point);}
            }
            flush();
        }
        for(const auto &p:outlinePoints) {
            if(stop())return out;
            for(int ei:boundaryIndex.near(QRectF(p-QPointF(1e-8,1e-8),p+QPointF(1e-8,1e-8)))) {
                const auto e=boundary[ei];
                if(meshSegmentDistance(p,old.vertices[e.first].xy,old.vertices[e.second].xy)<1e-8) {cuts.append(p);break;}
            }
        }
        ConstrainedDelaunay::QualityOptions quality;quality.minAngleDeg=28;quality.minEdge=floor;quality.smoothingPasses=4;
        const qint64 availableCells=in.maxCells-currentCells+part.size();
        qint64 quadCells=0;for(const auto &block:blocks)quadCells+=block.quads.size();
        quality.maxTriangles=int(std::max(qint64(0),availableCells-quadCells));quality.maxInsertions=quality.maxTriangles;
        quality.cancelled=cancelled;
        quality.hAt=[&](double x,double y){int c=top.cellAt(old,{x,y});return c<0?spacing:std::min(spacing,top.cellSize[c]);};
        quality.terrainElevationAt=[&](double x,double y){return elevation({x,y});};
        quality.terrainTolerance=tolerance;quality.terrainMinSpacing=floor;quality.terrainWorstFirst=true;quality.terrainFinalCheck=false;
        // Refinement must target the surface we can actually write, including
        // incision limits, force width and the lower envelope at confluences.
        // Keep the independent authored-section diagnostic after stitching.
        quality.terrainError=[&](const QPointF *xy,const double *z,QPointF *where){auto e=surface.sampledTargetError(xy,z,elevation,floor*0.01);if(where)*where=e.point;return e.maximum;};
        MeshCavityFill fill;
        if(quadCells>=availableCells)fill.error=QStringLiteral("The cavity exceeds the remaining mesh cell budget.");
        else fill=fillMeshCavity(old,top,part,blocks,freePoints,cuts,quality,elevation);
        if(fill.error.isEmpty()&&fill.mesh.triangles.size()>availableCells)fill.error=QStringLiteral("The cavity exceeds the remaining mesh cell budget.");
        if(stop())return out;
        if(!fill.error.isEmpty()) {
            ++out.fallbackCavities;out.warnings<<QStringLiteral("Cavity %1 kept its original cells; channel elevations were painted only: %2").arg(pi+1).arg(fill.error);
            for(int cell:part)removed[cell]=false;
        } else {
            for(int c=0;c<fill.mesh.triangles.size();++c) {
                if((c&4095)==0&&stop())return out;
                auto &cell=fill.mesh.triangles[c];auto centre=cellGeom(fill.mesh.vertices,cell).centroid;
                int source=top.cellAt(old,centre);
                if(source>=0) {
                    const auto &previous=old.triangles[source];cell.initDepth=previous.initDepth;
                    if(!std::isfinite(cell.mannings)||cell.mannings<=0)cell.mannings=previous.mannings;
                    if(cell.tag.isEmpty())cell.tag=previous.tag.startsWith("channel:")?QString():previous.tag;
                    if(old.infilOverrides.contains(source))fill.mesh.infilOverrides.insert(c,old.infilOverrides[source]);
                }
                const auto hit=surface.sample(centre);
                if(hit.profile>=0 && !cell.tag.startsWith("channel:")) {
                    const auto &profile=plan.profiles[hit.profile];cell.tag=QStringLiteral("channel:")+profile.conduitId;
                    if(in.options.roughnessFromTransect) {
                        const auto &section=profile.section;
                        double n=hit.offset<section.leftBank?section.nLeft:hit.offset>section.rightBank?section.nRight:section.nChannel;
                        if(std::isfinite(n)&&n>0)cell.mannings=n;
                    }
                }
            }
            currentCells+=fill.mesh.triangles.size()-part.size();
            fills.append(std::move(fill.mesh));
        }
        report(25+int(55.0*(pi+1)/parts.size()),QStringLiteral("Stitching channel cavity %1 of %2…").arg(pi+1).arg(parts.size()));
    }
    if(stop())return out;
    auto splice=spliceMeshCavities(old,removed,fills);out.mesh=std::move(splice.mesh);out.oldToNewVertex=splice.oldToNewVertex;out.oldToNewCell=splice.oldToNewCell;
    fills=QVector<MeshResult>{};
    if(out.mesh.triangles.size()>in.maxCells) {out.error=QStringLiteral("The burned mesh exceeds the cell budget.");return out;}
    for(int vi=0;vi<out.mesh.vertices.size();++vi) {
        if((vi&4095)==0&&stop())return out;
        auto &vertex=out.mesh.vertices[vi];if(surface.sample(vertex.xy).profile>=0)vertex.z=elevation(vertex.xy);
    }
    // Elevation painting is the last query of the original cell topology.
    // Keep only its outline certificate before building the full output
    // topology; two city-scale cell indexes and adjacency graphs need not
    // coexist during final validation.
    {
        MeshCavityTopology outlineOnly;
        outlineOnly.edges=std::move(top.edges);
        outlineOnly.outlineEdges=std::move(top.outlineEdges);
        outlineOnly.outlines=std::move(top.outlines);
        top=std::move(outlineOnly);
    }
    if(stop())return out;
    const auto edgeRemap=remapEdgeBCs(old,in.edgeBCs,out.mesh);
    out.mesh.boundaryEdges=edgeRemap.labelledEdges;
    out.edgeBCs=edgeRemap.bcs;
    if(edgeRemap.droppedAssignments)
        out.warnings<<QStringLiteral("%1 boundary-condition or conveyance assignments could not be carried onto the new mesh.").arg(edgeRemap.droppedAssignments);
    if(in.options.removeBurnedFrom1D) {
        out.surgery.splits=plan.splits;out.surgery.burnedConduits=QStringList(plan.replacedIds.values());std::sort(out.surgery.burnedConduits.begin(),out.surgery.burnedConduits.end());out.surgery.nodePlans=plan.nodes;
        if(!in.options.convertInterfaceNodes)for(auto &node:out.surgery.nodePlans)if(node.role==BurnNodeRole::Outfall)node.role=BurnNodeRole::CoupledJunction;
    }
    QSet<QString> retired;for(const auto &node:out.surgery.nodePlans)if(node.role==BurnNodeRole::Removed)retired.insert(node.nodeId);
    // Input coordinates include the whole model for lookup, not an invitation
    // to couple unrelated nodes that were deliberately left uncoupled.
    QSet<QString> eligibleNodes;
    for(const auto &node:out.surgery.nodePlans)if(node.role!=BurnNodeRole::Removed)eligibleNodes.insert(node.nodeId);
    for(const auto &v:old.vertices)if(!v.coupledNode.isEmpty())eligibleNodes.insert(v.coupledNode);
    for(const auto &c:old.cellCouplings)eligibleNodes.insert(c.nodeId);
    QVector<QPair<QString,QPointF>> nodes;
    QSet<QString> haveCoordinate;
    for(const auto &node:in.nodes)if(eligibleNodes.contains(node.first)) {nodes.append(node);haveCoordinate.insert(node.first);}
    for(const auto &split:out.surgery.splits)nodes.append({split.nodeId,split.xy});
    QHash<QString,QPair<double,double>> couplingParameters;
    for(const auto &v:old.vertices)if(!v.coupledNode.isEmpty()) {nodes.append({v.coupledNode,v.xy});couplingParameters.insert(v.coupledNode,{v.couplingCd,v.couplingArea});}
    for(const auto &c:old.cellCouplings)if(c.tri>=0&&c.tri<old.triangles.size()) {
        if(!haveCoordinate.contains(c.nodeId)) {nodes.append({c.nodeId,cellGeom(old.vertices,old.triangles[c.tri]).centroid});haveCoordinate.insert(c.nodeId);}
        couplingParameters.insert(c.nodeId,{c.cd,c.area});
    }
    for(auto &v:out.mesh.vertices)if(retired.contains(v.coupledNode))v.coupledNode.clear();
    out.mesh.cellCouplings.erase(std::remove_if(out.mesh.cellCouplings.begin(),out.mesh.cellCouplings.end(),[&](const auto &c){return retired.contains(c.nodeId);}),out.mesh.cellCouplings.end());
    QSet<QString> seen;nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[&](const auto &n){if(retired.contains(n.first)||seen.contains(n.first))return true;seen.insert(n.first);return false;}),nodes.end());
    if(stop())return out;
    auto mapped=mapNodesToMesh(out.mesh,nodes,1e-8,true);
    for(auto it=mapped.vertexMatches.cbegin();it!=mapped.vertexMatches.cend();++it) {
        auto &v=out.mesh.vertices[it.key()];v.coupledNode=it.value();if(couplingParameters.contains(it.value())) {v.couplingCd=couplingParameters[it.value()].first;v.couplingArea=couplingParameters[it.value()].second;}
    }
    for(auto c:mapped.cellMatches) {if(couplingParameters.contains(c.nodeId)) {c.cd=couplingParameters[c.nodeId].first;c.area=couplingParameters[c.nodeId].second;}out.mesh.cellCouplings.append(c);}
    report(90,QStringLiteral("Checking conformity, channel accuracy and cell quality…"));
    if(stop())return out;
    MeshCavityTopology check;if(!check.build(out.mesh,cancelled)) {if(stop())return out;out.error=check.error;return out;}
    // A stitched mesh may subdivide the original outline, but must not add
    // an interior crack or move any boundary outside its source segments.
    for(int ei:check.outlineEdges) {
        const auto &edge=check.edges[ei];const auto a=out.mesh.vertices[edge.a].xy,b=out.mesh.vertices[edge.b].xy;
        bool original=false;
        for(int oi:top.outlines.near(QRectF(a,b).normalized().adjusted(-1e-8,-1e-8,1e-8,1e-8))) {
            const auto &source=top.edges[top.outlineEdges[oi]];const auto p=old.vertices[source.a].xy,q=old.vertices[source.b].xy;
            if(meshSegmentDistance(a,p,q)<=1e-8&&meshSegmentDistance(b,p,q)<=1e-8) {original=true;break;}
        }
        if(!original) {out.error=QStringLiteral("The burned mesh contains an unexpected boundary or crack.");return out;}
    }
    // Every surviving node touched by retirement is a required hydraulic
    // interface. The generic mapper's area-scaled containment can reject a
    // mathematically on-edge split after rounding at large projected map
    // coordinates. Recover only required interfaces within the same strict
    // positional tolerance of a certified exterior segment, without moving
    // their model coordinates or snapping unrelated outside nodes.
    QSet<QString> missingInterfaces;
    for(const auto &node:out.surgery.nodePlans)
        if(node.role==BurnNodeRole::Outfall||node.role==BurnNodeRole::CoupledJunction)
            missingInterfaces.insert(node.nodeId);
    if(!missingInterfaces.isEmpty()) {
        for(const auto &vertex:out.mesh.vertices)missingInterfaces.remove(vertex.coupledNode);
        for(const auto &coupling:out.mesh.cellCouplings)
            if(coupling.tri>=0&&coupling.tri<out.mesh.triangles.size())missingInterfaces.remove(coupling.nodeId);
        const double couplingTolerance=1e-8;
        for(const auto &node:nodes)if(missingInterfaces.contains(node.first)) {
            if(stop())return out;
            int best=-1;
            const auto p=node.second;
            for(int oi:check.outlines.near(QRectF(p-QPointF(couplingTolerance,couplingTolerance),
                                                 p+QPointF(couplingTolerance,couplingTolerance)))) {
                const auto &edge=check.edges[check.outlineEdges[oi]];
                if(meshSegmentDistance(p,out.mesh.vertices[edge.a].xy,out.mesh.vertices[edge.b].xy)<=couplingTolerance)
                    if(best<0||edge.left<best)best=edge.left;
            }
            if(best>=0) {
                CellCoupling coupling{best,node.first,kCellCouplingDefaultCd,kCellCouplingDefaultArea};
                if(couplingParameters.contains(node.first)) {
                    const auto parameters=couplingParameters.value(node.first);
                    coupling.cd=parameters.first;coupling.area=parameters.second;
                }
                out.mesh.cellCouplings.append(coupling);
                missingInterfaces.remove(node.first);mapped.unmatched.removeAll(node.first);
            }
        }
        if(!missingInterfaces.isEmpty()) {
            QStringList ids(missingInterfaces.begin(),missingInterfaces.end());std::sort(ids.begin(),ids.end());
            out.error=QStringLiteral("Cannot couple required channel interfaces to the mesh: %1. No 1D channel replacement can be applied.").arg(ids.join(", "));
            out.surgery={};out.mesh={};out.edgeBCs.clear();out.oldToNewCell.clear();out.oldToNewVertex.clear();
            return out;
        }
    }
    if(!mapped.unmatched.isEmpty())out.warnings<<QStringLiteral("Nodes outside the burned mesh remain uncoupled: %1").arg(mapped.unmatched.join(", "));
    QSet<int> kept;
    for(int i=0;i<old.triangles.size();++i) {if(measured[i])measure(out.before,old,old.triangles[i]);else if(out.oldToNewCell[i]>=0)kept.insert(out.oldToNewCell[i]);}
    int inaccurate=0;double maxError=0;
    for(int i=0;i<out.mesh.triangles.size();++i)if(!kept.contains(i)) {
        if((i&1023)==0&&stop())return out;
        measure(out.after,out.mesh,out.mesh.triangles[i]);const auto geom=cellGeom(out.mesh.vertices,out.mesh.triangles[i]);
        for(int half=0;half<geom.nSub;++half) {QPointF xy[3];double z[3];for(int k=0;k<3;++k) {const auto &v=out.mesh.vertices[geom.sub[half][k]];xy[k]=v.xy;z[k]=v.z;}auto error=surface.error(xy,z,{},floor*0.01);maxError=std::max(maxError,error.maximum);if(error.maximum>tolerance)++inaccurate;}
    }
    if(inaccurate)out.warnings<<QStringLiteral("Channel elevation tolerance exceeded in %1 cell faces (maximum error %2 m). Review the channel surface.").arg(inaccurate).arg(maxError*in.verticalUnitToSI);
    if(out.after.below25)out.warnings<<QStringLiteral("%1 affected cells have angles below 25 degrees; %2 below 10 degrees.").arg(out.after.below25).arg(out.after.below10);
    if(stop())return out;
    out.ok=true;report(100,QStringLiteral("Channel burn complete."));return out;
}
}
