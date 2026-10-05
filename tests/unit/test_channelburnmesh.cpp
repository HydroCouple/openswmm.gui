// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "mesh/channelburnmesh.h"
#include "mesh/meshcavity.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshedgebcremap.h"
using namespace mesh;
static MeshResult grid(int n=20,double h=2) {
    MeshResult m;m.ok=true;
    for(int y=0;y<=n;++y)for(int x=0;x<=n;++x)m.vertices.append({QPointF(x*h,y*h),10});
    for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
        int a=y*(n+1)+x,b=a+1,c=a+n+2,d=a+n+1;
        MeshCell t;t.v0=a;t.v1=b;t.v2=c;t.tag="soil";t.mannings=0.04;t.initDepth=0.2;m.triangles.append(t);t.v1=c;t.v2=d;m.triangles.append(t);
    }
    return m;
}
static ChannelMeshBurnInputs channel(QPointF a={7,20},QPointF b={33,20}) {
    ChannelMeshBurnInputs in;in.options.channelCellSize=2;in.options.forceHalfWidth=20;in.options.removeBurnedFrom1D=false;
    ChannelInput source;source.conduitId="river";source.centerline={a,b};source.zUp=5;source.zDn=4;
    source.section.station={-4,-2,0,2,4};source.section.elevation={4,0,0,0,4};
    in.profiles={buildBurnProfile(source,in.options)};return in;
}
TEST(ChannelBurnMesh, StraightConformingQuadsAndUntouchedCells) {
    const auto old=grid();auto result=burnChannelsIntoMesh(old,channel());
    ASSERT_TRUE(result.ok)<<result.error.toStdString();
    EXPECT_EQ(result.fallbackCavities,0)<<result.warnings.join("\n").toStdString();
    EXPECT_GT(result.mesh.quadCount(),0);EXPECT_EQ(result.after.below10,0)<<result.after.minAngle;
    MeshCavityTopology top;ASSERT_TRUE(top.build(result.mesh))<<top.error.toStdString();
    double area=0;for(const auto &c:result.mesh.triangles) {auto g=cellGeom(result.mesh.vertices,c);EXPECT_GT(g.area,0);area+=g.area;}
    EXPECT_NEAR(area,1600,1e-6);
    for(int c=0;c<old.triangles.size();++c)if(result.oldToNewCell[c]>=0) {
        const auto &a=old.triangles[c],&b=result.mesh.triangles[result.oldToNewCell[c]];EXPECT_EQ(a.tag,b.tag);EXPECT_EQ(a.mannings,b.mannings);
        for(int k=0;k<a.vertexCount();++k) {const auto &v=old.vertices[a.vertex(k)],&w=result.mesh.vertices[b.vertex(k)];EXPECT_EQ(v.xy,w.xy);EXPECT_EQ(v.z,w.z);}
    }
}
TEST(ChannelBurnMesh, ObliqueTruncation) {
    auto result=burnChannelsIntoMesh(grid(),channel({-10,7},{50,29}));
    ASSERT_TRUE(result.ok)<<result.error.toStdString();EXPECT_EQ(result.fallbackCavities,0)<<result.warnings.join("\n").toStdString();
    double area=0;for(const auto &c:result.mesh.triangles)area+=cellGeom(result.mesh.vertices,c).area;EXPECT_NEAR(area,1600,1e-6);
}
TEST(ChannelBurnMesh, CancelDoesNotReturnPartialMesh) {
    auto result=burnChannelsIntoMesh(grid(),channel(),{},[]{return true;});EXPECT_TRUE(result.cancelled);EXPECT_FALSE(result.ok);EXPECT_TRUE(result.mesh.vertices.isEmpty());
}
TEST(ChannelBurnMesh, DirectedRegionRemoval) {
    ConstrainedDelaunay cdt;QVector<int> ids;ASSERT_TRUE(cdt.build({{0,0},{4,0},{4,4},{0,4},{2,0},{2,4}},&ids));
    for(auto e:QVector<QPair<int,int>>{{0,1},{1,2},{2,3},{3,0},{4,5}})ASSERT_TRUE(cdt.insertConstraint(ids[e.first],ids[e.second]));
    cdt.removeExterior();EXPECT_GT(cdt.removeRegionLeftOf(ids[4],ids[5]),0);EXPECT_GE(cdt.locate({3,2}),0);EXPECT_LT(cdt.locate({1,2}),0);
}
TEST(ChannelBurnMesh, WallSpacing) {
    auto in=channel();auto &p=in.profiles[0];p.offsets={-3.001,-3,0,3,3.001};p.section.station=p.offsets;p.section.relZ={4,0,0,0,4};p.section.sMin=-3.001;p.section.sMax=3.001;
    for(auto &row:p.relZ)row=p.section.relZ;
    QStringList warnings;
    auto lat=buildCorridorLattice(p,2,0,&warnings,nullptr,0,0.5);ASSERT_TRUE(lat.isValid());EXPECT_GE(lat.minAcrossSpacing,0.5-1e-10);
    // Symmetric wall expansion preserves this section's bankfull wetted area.
    // General clustered/narrow sections remain approximations and warn; this
    // equality is not a claim about their full stage-conveyance relationship.
    double authoredArea=0;
    for(int k=1;k<p.section.station.size();++k)
        authoredArea+=(p.section.station[k]-p.section.station[k-1])
            *(8-p.section.relZ[k]-p.section.relZ[k-1])*0.5;
    EXPECT_NEAR(authoredArea,24.004,1e-10);
    for(int row=0;row<lat.nAlong;++row) {
        const double bankfull=bedZAt(p,lat.chainage[row])+4;double area=0;
        for(int k=1;k<lat.nAcross;++k)
            area+=(lat.offsets[k]-lat.offsets[k-1])
                *(2*bankfull-lat.z[lat.at(row,k)]-lat.z[lat.at(row,k-1)])*0.5;
        EXPECT_NEAR(area,authoredArea,1e-10);
    }
    EXPECT_TRUE(warnings.join(" ").contains("regularized"));
}

TEST(ChannelBurnMesh, KeepsUnrelatedNodesUncoupledAndPreservesExistingRows) {
    auto old=grid();auto inputs=channel();
    old.cellCouplings.append({0,"existing",0.42,7.5});
    inputs.nodes={{"unrelated",{20,20}},{"existing",{1,0.5}}};
    auto result=burnChannelsIntoMesh(old,inputs);
    ASSERT_TRUE(result.ok)<<result.error.toStdString();
    ASSERT_EQ(result.mesh.cellCouplings.size(),1);
    EXPECT_EQ(result.mesh.cellCouplings[0].nodeId,"existing");
    EXPECT_EQ(result.mesh.cellCouplings[0].cd,0.42);
    EXPECT_EQ(result.mesh.cellCouplings[0].area,7.5);
    EXPECT_EQ(result.mesh.cellCouplings[0].tri,result.oldToNewCell[0]);
    for(const auto &v:result.mesh.vertices)EXPECT_NE(v.coupledNode,"unrelated");
}

TEST(ChannelBurnMesh, ChannelQuadsInheritRoughnessWhenTransectHasNone) {
    auto result=burnChannelsIntoMesh(grid(),channel());
    ASSERT_TRUE(result.ok)<<result.error.toStdString();
    ASSERT_GT(result.mesh.quadCount(),0);
    for(const auto &cell:result.mesh.triangles)EXPECT_DOUBLE_EQ(cell.mannings,0.04);
}

TEST(ChannelBurnMesh, CancellationDuringFinalChecksCannotReturnSuccess) {
    bool cancel=false;
    auto result=burnChannelsIntoMesh(grid(),channel(),[&](int progress,const QString &){if(progress>=90)cancel=true;},[&]{return cancel;});
    EXPECT_TRUE(result.cancelled);EXPECT_FALSE(result.ok);EXPECT_TRUE(result.mesh.vertices.isEmpty());
}

TEST(ChannelBurnMesh, CavityBudgetRetainsOriginalCellsAndTheirCouplings) {
    auto old=grid();auto inputs=channel();inputs.options.channelCellSize=0.1;inputs.maxCells=old.triangles.size();
    old.cellCouplings.append({380,"retained",0.32,9.0});
    auto result=burnChannelsIntoMesh(old,inputs);
    ASSERT_TRUE(result.ok)<<result.error.toStdString();
    EXPECT_GT(result.fallbackCavities,0);
    EXPECT_LE(result.mesh.triangles.size(),inputs.maxCells);
    EXPECT_GE(result.oldToNewCell[380],0);
    ASSERT_EQ(result.mesh.cellCouplings.size(),1);
    EXPECT_EQ(result.mesh.cellCouplings[0].tri,result.oldToNewCell[380]);
    EXPECT_EQ(result.mesh.cellCouplings[0].cd,0.32);EXPECT_EQ(result.mesh.cellCouplings[0].area,9.0);
}

TEST(ChannelBurnMesh, CavityBudgetIsSharedAcrossDisconnectedChannels) {
    auto old=grid(50,1);auto inputs=channel({5,10},{20,10});
    auto second=channel({30,40},{45,40});inputs.profiles+=second.profiles;inputs.profiles[1].conduitId="second";
    inputs.options.channelCellSize=0.3;inputs.clearance=0.5;inputs.maxCells=old.triangles.size()+100;
    auto result=burnChannelsIntoMesh(old,inputs);
    ASSERT_TRUE(result.ok)<<result.error.toStdString();
    EXPECT_GE(result.cavities,2);EXPECT_LE(result.mesh.triangles.size(),inputs.maxCells);
}

TEST(ChannelBurnMesh, RejectsNonConvexCellsAndUnknownElevations) {
    MeshResult old;old.vertices={{{0,0},0},{{4,0},0},{{1,1},0},{{0,4},0}};
    MeshCell c;c.v0=0;c.v1=1;c.v2=2;c.v3=3;old.triangles={c};
    MeshCavityTopology topology;EXPECT_FALSE(topology.build(old));
    old=grid();old.vertices[0].z=std::numeric_limits<double>::quiet_NaN();EXPECT_FALSE(topology.build(old));
}

TEST(ChannelBurnMesh, SplitBoundaryPreservesConditionAndConveyance) {
    MeshResult old;old.vertices={{{0,0},0},{{4,0},0},{{0,4},0}};
    MeshCell c;c.v0=0;c.v1=1;c.v2=2;old.triangles={c};old.boundaryEdges={{0,1,3,"outlet"}};
    QVector<MeshEdgeBC> bcs(edgeSlotCount(1));
    for(int e=0;e<3;++e){int a,b;edgeEndpoints(c,e,a,b);if((a==0&&b==1)||(a==1&&b==0)){bcs[edgeSlot(0,e)].flow=2.4;bcs[edgeSlot(0,e)].conveyance=0.3;}}
    auto split=old;split.vertices.append({{2,0},0});split.triangles[0].v1=3;c.v0=3;split.triangles.append(c);
    const auto result=remapEdgeBCs(old,bcs,split);
    EXPECT_EQ(result.droppedAssignments,0);EXPECT_EQ(result.labelledEdges.size(),2);
    int carried=0;for(const auto &bc:result.bcs)if(bc.conveyance==0.3){++carried;EXPECT_EQ(bc.flow,2.4);}
    EXPECT_EQ(carried,2);
}

TEST(ChannelBurnMesh, MixedSuccessSpliceDoesNotRequireOneFillPerCavity) {
    auto old=grid(2,1);old.cellCouplings={{0,"fallback",0.4,3.0}};
    QVector<bool> removed(old.triangles.size(),false);removed[6]=removed[7]=true;
    auto fill=grid(1,1);for(auto &v:fill.vertices)v.xy+=QPointF(1,1);
    auto result=spliceMeshCavities(old,removed,{fill});
    ASSERT_EQ(result.mesh.triangles.size(),old.triangles.size());
    ASSERT_EQ(result.mesh.cellCouplings.size(),1);EXPECT_EQ(result.mesh.cellCouplings[0].tri,result.oldToNewCell[0]);
    for(int i=0;i<6;++i)EXPECT_GE(result.oldToNewCell[i],0);
    EXPECT_EQ(result.oldToNewCell[6],-1);EXPECT_EQ(result.oldToNewCell[7],-1);
    MeshCavityTopology topology;EXPECT_TRUE(topology.build(result.mesh));
}

static void expectGeometry(const ChannelMeshBurnResult &result,double area) {
    ASSERT_TRUE(result.ok)<<result.error.toStdString();
    MeshCavityTopology topology;ASSERT_TRUE(topology.build(result.mesh))<<topology.error.toStdString();
    double sum=0;
    for(const auto &cell:result.mesh.triangles) {const auto g=cellGeom(result.mesh.vertices,cell);ASSERT_GT(g.area,0);sum+=g.area;}
    EXPECT_NEAR(sum,area,1e-6);
}

TEST(ChannelBurnMesh, ConfluencesAtThirtyFortyFiveAndNinetyDegrees) {
    for(double angle:{30.0,45.0,90.0}) {
        SCOPED_TRACE(angle);
        auto input=channel({7,20},{33,20});
        const double a=angle*std::acos(-1.0)/180;
        auto branch=channel(QPointF(20,20)+QPointF(-12*std::cos(a),12*std::sin(a)),{20,20});
        branch.profiles[0].conduitId="tributary";input.profiles+=branch.profiles;
        auto result=burnChannelsIntoMesh(grid(),input);expectGeometry(result,1600);
        EXPECT_EQ(result.fallbackCavities,0)<<result.warnings.join("\n").toStdString();
        EXPECT_EQ(result.after.below10,0)<<result.after.minAngle;
    }
}

TEST(ChannelBurnMesh, BuildingHoleBesideBankStaysEmpty) {
    auto old=grid();
    old.triangles.erase(std::remove_if(old.triangles.begin(),old.triangles.end(),[&](const auto &cell){const auto p=cellGeom(old.vertices,cell).centroid;return p.x()>16&&p.x()<24&&p.y()>24&&p.y()<30;}),old.triangles.end());
    auto result=burnChannelsIntoMesh(old,channel());expectGeometry(result,1600-8*6);
    EXPECT_EQ(result.fallbackCavities,0)<<result.warnings.join("\n").toStdString();
    MeshCavityTopology topology;ASSERT_TRUE(topology.build(result.mesh));EXPECT_LT(topology.cellAt(result.mesh,{20,26}),0);EXPECT_EQ(topology.domain.holes.size(),1);
}

TEST(ChannelBurnMesh, AmbientFinerAndCoarserThanChannel) {
    for(double h:{0.5,4.0}) {
        SCOPED_TRACE(h);auto result=burnChannelsIntoMesh(grid(int(40/h),h),channel());expectGeometry(result,1600);
        EXPECT_EQ(result.fallbackCavities,0)<<result.warnings.join("\n").toStdString();
        EXPECT_EQ(result.after.below10,0)<<result.after.minAngle;
    }
}

TEST(ChannelBurnMesh, RepeatedBurnKeepsSeparateEarlierChannel) {
    auto first=burnChannelsIntoMesh(grid(),channel({7,10},{33,10}));ASSERT_TRUE(first.ok);
    auto next=channel({7,30},{33,30});next.profiles[0].conduitId="new";
    auto second=burnChannelsIntoMesh(first.mesh,next);expectGeometry(second,1600);
    for(int i=0;i<first.mesh.triangles.size();++i)if(first.mesh.triangles[i].tag.startsWith("channel:")) {
        ASSERT_GE(second.oldToNewCell[i],0);const auto &original=first.mesh.triangles[i],&kept=second.mesh.triangles[second.oldToNewCell[i]];
        EXPECT_EQ(kept.tag,original.tag);EXPECT_EQ(kept.mannings,original.mannings);
        for(int k=0;k<original.vertexCount();++k) {const auto &a=first.mesh.vertices[original.vertex(k)],&b=second.mesh.vertices[kept.vertex(k)];EXPECT_EQ(a.xy,b.xy);EXPECT_EQ(a.z,b.z);}
    }
    auto repeat=burnChannelsIntoMesh(second.mesh,next);expectGeometry(repeat,1600);
}

TEST(ChannelBurnMesh, RejectsHangingEdgeAndOverlappingComponents) {
    MeshResult m;m.vertices={{{0,0},0},{{4,0},0},{{0,4},0},{{2,0},0},{{4,-2},0}};
    MeshCell a;a.v0=0;a.v1=1;a.v2=2;MeshCell b;b.v0=3;b.v1=4;b.v2=1;m.triangles={a,b};
    MeshCavityTopology topology;EXPECT_FALSE(topology.build(m));
    // A T-junction between otherwise independent closed components.
    m.vertices={{{0,0},0},{{4,0},0},{{0,4},0},{{2,0},0},{{2,-2},0},{{3,-2},0}};b.v0=3;b.v1=4;b.v2=5;m.triangles={a,b};EXPECT_FALSE(topology.build(m));
    // Two independently closed triangles overlap with crossing edges.
    m.vertices={{{0,0},0},{{4,0},0},{{0,4},0},{{1,-1},0},{{5,1},0},{{1,3},0}};b.v0=3;b.v1=4;b.v2=5;m.triangles={a,b};EXPECT_FALSE(topology.build(m));
    // A contained component overlaps without any edge crossing.
    m.vertices={{{0,0},0},{{4,0},0},{{0,4},0},{{0.5,0.5},0},{{1.5,0.5},0},{{0.5,1.5},0}};EXPECT_FALSE(topology.build(m));
}

TEST(ChannelBurnMesh, RefinementUsesPermittedElevationWhileRawDiagnosticRemains) {
    auto inputs=channel();auto lattice=buildCorridorLattice(inputs.profiles[0],2,0);
    BurnSurface surface;surface.build({lattice});
    QPointF xy[3]={{10,19},{12,19},{10,21}};double z[3]={9,9,9};
    EXPECT_GT(surface.error(xy,z,{}).maximum,3);
    EXPECT_DOUBLE_EQ(surface.sampledTargetError(xy,z,[](const QPointF &){return 9.;}).maximum,0);
    inputs.options.maxIncision=1;inputs.options.quadCorridor=false;
    auto result=burnChannelsIntoMesh(grid(),inputs);expectGeometry(result,1600);
    for(const auto &vertex:result.mesh.vertices)EXPECT_GE(vertex.z,9-1e-10);
    EXPECT_TRUE(result.warnings.join(" ").contains("Channel elevation tolerance exceeded"));
}

TEST(ChannelBurnMesh, ProjectedCoordinatesKeepConformityAndSurfaceDiagnostics) {
    const QPointF offset(600000,4700000);auto old=grid();auto inputs=channel();
    for(auto &vertex:old.vertices)vertex.xy+=offset;
    for(auto &profile:inputs.profiles)for(auto &point:profile.centerline)point+=offset;
    auto result=burnChannelsIntoMesh(old,inputs);expectGeometry(result,1600);
    EXPECT_EQ(result.fallbackCavities,0)<<result.warnings.join("\n").toStdString();
    EXPECT_EQ(result.after.below10,0);
}

TEST(ChannelBurnMesh, PointTouchingComponentsHaveSeparateOutlines) {
    MeshResult m;m.vertices={{{0,0},0},{{2,0},0},{{0,2},0},{{-2,0},0},{{0,-2},0}};
    MeshCell a;a.v0=0;a.v1=1;a.v2=2;MeshCell b;b.v0=0;b.v1=3;b.v2=4;m.triangles={a,b};
    for(int order=0;order<2;++order) {
        MeshCavityTopology top;ASSERT_TRUE(top.build(m))<<top.error.toStdString();
        EXPECT_EQ(top.domain.rings.size(),2);EXPECT_EQ(top.domain.holes.size(),0);
        EXPECT_TRUE(top.domain.contains({0.5,0.5}));EXPECT_TRUE(top.domain.contains({-0.5,-0.5}));
        EXPECT_FALSE(top.domain.contains({-0.5,0.5}));EXPECT_EQ(top.outlineEdges.size(),6);
        std::reverse(m.triangles.begin(),m.triangles.end());
        for(auto &cell:m.triangles)std::swap(cell.v1,cell.v2);
    }
}

TEST(ChannelBurnMesh, PointTouchingHolesAndOuterRingKeepTheirDomains) {
    for(bool touchesOuter:{false,true}) {
        SCOPED_TRACE(touchesOuter);
        auto m=grid(5,1);
        const QPointF first=touchesOuter?QPointF(0.5,0.5):QPointF(1.5,1.5);
        const QPointF second=first+QPointF(1,1);
        m.triangles.erase(std::remove_if(m.triangles.begin(),m.triangles.end(),[&](const auto &cell) {
            const auto p=cellGeom(m.vertices,cell).centroid;
            return (std::abs(p.x()-first.x())<0.5&&std::abs(p.y()-first.y())<0.5)
                ||(std::abs(p.x()-second.x())<0.5&&std::abs(p.y()-second.y())<0.5);
        }),m.triangles.end());
        MeshCavityTopology top;ASSERT_TRUE(top.build(m))<<top.error.toStdString();
        EXPECT_EQ(top.domain.rings.size(),1);EXPECT_EQ(top.domain.holes.size(),touchesOuter?1:2);
        EXPECT_FALSE(top.domain.contains(first));EXPECT_FALSE(top.domain.contains(second));
        for(const auto &cell:m.triangles)EXPECT_TRUE(top.domain.contains(cellGeom(m.vertices,cell).centroid));
        QVector<bool> removed(m.triangles.size(),true);
        const auto cavities=top.cavities(removed);
        ConstrainedDelaunay::QualityOptions options;
        for(const auto &part:cavities) {
            auto fill=fillMeshCavity(m,top,part,{}, {}, {},options,[](const QPointF &){return 10.;});
            ASSERT_TRUE(fill.error.isEmpty())<<fill.error.toStdString();
            MeshCavityTopology filled;ASSERT_TRUE(filled.build(fill.mesh))<<filled.error.toStdString();
            EXPECT_FALSE(filled.domain.contains(first));EXPECT_FALSE(filled.domain.contains(second));
        }
    }
}

TEST(ChannelBurnMesh, OutlineIntersectionsCannotCreateTinyFixedEdges) {
    auto inputs=channel({-10,20.001},{50,20.001});
    auto result=burnChannelsIntoMesh(grid(),inputs);expectGeometry(result,1600);
    EXPECT_EQ(result.fallbackCavities,0)<<result.warnings.join("\n").toStdString();
    EXPECT_GE(result.after.minEdge,inputs.options.channelCellSize/inputs.options.channelAspectMax-1e-8);
    EXPECT_EQ(result.after.below10,0)<<result.after.minAngle;
}

TEST(ChannelBurnMesh, NearbyOutlineSamplesRespectSpacingWithoutMovingBoundary) {
    const auto old=grid(1,10);MeshCavityTopology top;ASSERT_TRUE(top.build(old));
    ConstrainedDelaunay::QualityOptions quality;quality.minEdge=1;quality.minAngleDeg=28;
    const auto fill=fillMeshCavity(old,top,{0,1},{},{},{{0.001,0},{5,0},{5.001,0},{9.999,0}},quality,
        [](const QPointF &){return 10.;});
    ASSERT_TRUE(fill.mesh.ok)<<fill.error.toStdString();
    double area=0;
    for(const auto &cell:fill.mesh.triangles) {
        area+=cellGeom(fill.mesh.vertices,cell).area;
        for(int k=0;k<cell.vertexCount();++k) {
            const auto a=fill.mesh.vertices[cell.vertex(k)].xy,b=fill.mesh.vertices[cell.vertex((k+1)%cell.vertexCount())].xy;
            EXPECT_GE(std::hypot(a.x()-b.x(),a.y()-b.y()),1-1e-8);
        }
    }
    EXPECT_NEAR(area,100,1e-8);
    for(const auto &vertex:old.vertices)EXPECT_TRUE(std::any_of(fill.mesh.vertices.begin(),fill.mesh.vertices.end(),[&](const auto &v){return v.xy==vertex.xy;}));
}

TEST(ChannelBurnMesh, ExteriorOutlineCanRefineAroundNearbyInteriorSamples) {
    const auto old=grid(1,10);MeshCavityTopology top;ASSERT_TRUE(top.build(old));
    ConstrainedDelaunay::QualityOptions quality;quality.minEdge=1;quality.minAngleDeg=28;
    const auto fill=fillMeshCavity(old,top,{0,1},{},{{1,1}},{},quality,[](const QPointF &){return 10.;});
    ASSERT_TRUE(fill.mesh.ok)<<fill.error.toStdString();
    double minAngle=180,area=0;
    for(const auto &cell:fill.mesh.triangles) {
        area+=cellGeom(fill.mesh.vertices,cell).area;
        for(int k=0;k<cell.vertexCount();++k) {
            const auto p=fill.mesh.vertices[cell.vertex(k)].xy;
            const auto a=fill.mesh.vertices[cell.vertex((k+1)%cell.vertexCount())].xy-p;
            const auto b=fill.mesh.vertices[cell.vertex((k+cell.vertexCount()-1)%cell.vertexCount())].xy-p;
            minAngle=std::min(minAngle,std::acos(std::clamp(QPointF::dotProduct(a,b)/(std::hypot(a.x(),a.y())*std::hypot(b.x(),b.y())),-1.,1.))*180/std::acos(-1.));
        }
    }
    EXPECT_GE(minAngle,10);EXPECT_NEAR(area,100,1e-8);
    for(const auto &vertex:old.vertices)EXPECT_TRUE(std::any_of(fill.mesh.vertices.begin(),fill.mesh.vertices.end(),[&](const auto &v){return v.xy==vertex.xy;}));
}

TEST(ChannelBurnMesh, NearbySeparateCorridorsDoNotConstrainASliverGap) {
    auto inputs=channel({7,14},{33,14});auto second=channel({7,22.01},{33,22.01});
    second.profiles[0].conduitId="nearby";inputs.profiles+=second.profiles;
    const auto result=burnChannelsIntoMesh(grid(),inputs);expectGeometry(result,1600);
    EXPECT_EQ(result.fallbackCavities,0)<<result.warnings.join("\n").toStdString();
    EXPECT_EQ(result.after.below10,0)<<result.after.minAngle;
    EXPECT_GE(result.after.minEdge,inputs.options.channelCellSize/inputs.options.channelAspectMax-1e-8);
}

TEST(ChannelBurnMesh, ProjectedBoundarySplitsAlwaysCoupleBeforeRetirement) {
    for(double angle:{0.1,0.3,0.7}) {
        SCOPED_TRACE(angle);
        const auto transform=[&](QPointF p){return QPointF(760000,2945000)+QPointF(p.x()*std::cos(angle)-p.y()*std::sin(angle),p.x()*std::sin(angle)+p.y()*std::cos(angle));};
        auto old=grid();auto inputs=channel({-10,7.123},{50,29.555});
        for(auto &v:old.vertices)v.xy=transform(v.xy);
        for(auto &p:inputs.profiles)for(auto &v:p.centerline)v=transform(v);
        inputs.options.removeBurnedFrom1D=true;
        inputs.network.nodes={{"up"},{"down"}};inputs.network.links={{"river",0,1,{}}};
        inputs.nodes={{"up",inputs.profiles[0].centerline.first()},{"down",inputs.profiles[0].centerline.last()}};
        const auto result=burnChannelsIntoMesh(old,inputs);
        ASSERT_TRUE(result.ok)<<result.error.toStdString();ASSERT_EQ(result.surgery.splits.size(),2);
        QSet<QString> coupled;for(const auto &v:result.mesh.vertices)if(!v.coupledNode.isEmpty())coupled.insert(v.coupledNode);
        for(const auto &c:result.mesh.cellCouplings)coupled.insert(c.nodeId);
        for(const auto &split:result.surgery.splits)EXPECT_TRUE(coupled.contains(split.nodeId))<<split.nodeId.toStdString();
    }
}

TEST(ChannelBurnMesh, MissingRequiredInterfacePreventsNetworkRetirement) {
    auto inputs=channel();inputs.options.removeBurnedFrom1D=true;
    inputs.network.nodes={{"up"},{"down"},{"outside"}};
    inputs.network.links={{"river",0,1,{}},{"survives",2,0,{}}};
    // The interface is required, but its model coordinate is absent. Merely
    // omitting it from the mapping input must not silently retire its channel.
    for(bool hasOutsideCoordinate:{false,true}) {
        SCOPED_TRACE(hasOutsideCoordinate);
        if(hasOutsideCoordinate)inputs.nodes={{"up",{-0.001,20}}};
        const auto result=burnChannelsIntoMesh(grid(),inputs);
        EXPECT_FALSE(result.ok);EXPECT_TRUE(result.error.contains("interface"));
        EXPECT_TRUE(result.surgery.burnedConduits.isEmpty());
    }
}
