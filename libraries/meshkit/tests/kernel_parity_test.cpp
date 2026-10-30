// SPDX-License-Identifier: Apache-2.0
#include "mesh/meshcdt.h"
#include "meshkit/cdt.hpp"
#include <stdexcept>
#include <iostream>
#include <cmath>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+":"+std::to_string(__LINE__));}while(false)
struct Snapshot {std::vector<double> xy;std::vector<int> connectivity;std::uint64_t inserted=0,removed=0;};
template<class CDT> Snapshot run(int mode){using P=typename CDT::Point;typename CDT::PointInput points;points.push_back(P(0,0));points.push_back(P(20,0));points.push_back(P(20,20));points.push_back(P(0,20));
 for(int y=1;y<20;++y)for(int x=1;x<20;++x)points.push_back(P(x+.07*std::sin(y+x),y+.04*std::cos(x-y)));
 if(mode==2)for(auto &p:points)p=P(p.x()+800000,p.y()+4500000);
 CDT cdt;typename CDT::template Vector<int> mapping;CHECK(cdt.build(points,&mapping));for(int i=0;i<4;++i)CHECK(cdt.insertConstraint(mapping[i],mapping[(i+1)%4]));cdt.removeExterior();
 typename CDT::QualityOptions o;o.minAngleDeg=22;o.hAt=[](double,double){return 1.8;};o.minEdge=.15;o.maxTriangles=100000;o.coarsenPasses=mode?3:0;
 if(mode){o.coarseningOrder=meshkit::CoarseningOrder::ResolutionAware;o.coarseningReferenceSize=2;o.coarseningTargetSize=[](const typename CDT::Box &){return 1.8;};o.terrainTolerance=.02;o.terrainElevationAt=[](double x,double y){return .00001*x+.00003*y;};
 o.terrainError=[](const P *,const double *,P *){return 0.;};o.terrainErrorBound=[](const P *,const double *){return 0.;};}
 auto stats=cdt.refineQuality(o);CHECK(!stats.cancelled && !stats.capped && !stats.invalidPolicy);Snapshot s;s.inserted=stats.inserted;s.removed=stats.coarsenRemoved;
 for(auto p:cdt.vertices()){s.xy.push_back(p.x());s.xy.push_back(p.y());}
 for(const auto &t:cdt.triangles()){s.connectivity.push_back(t.alive);if(t.alive)for(int i=0;i<3;++i){s.connectivity.push_back(t.v[i]);s.connectivity.push_back(t.adj[i]);s.connectivity.push_back(t.constrained[i]);}}
 return s;}
int main(){try{for(int mode=0;mode<3;++mode){auto a=run<meshkit::ConstrainedDelaunay>(mode),b=run<mesh::ConstrainedDelaunay>(mode);CHECK(a.xy==b.xy);CHECK(a.connectivity==b.connectivity);CHECK(a.inserted==b.inserted && a.removed==b.removed);}std::cout<<"native/Qt kernel parity passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
