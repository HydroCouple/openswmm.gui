// SPDX-License-Identifier: Apache-2.0
#include "meshkit/raster.hpp"
#include <stdexcept>
#include <iostream>
#include <cmath>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(){try{meshkit::RasterData d;d.columns=d.rows=2;d.heights={0,0,0,1};meshkit::RasterSurface surface(d);meshkit::Point2 p[3]={{0,0},{1,0},{0,1}};double z[3]={0,0,0};auto q=surface.query(p,z);CHECK(q.valid);CHECK(q.bound>=.25 && q.bound<.250000001);CHECK(std::abs(q.witness.x()-.5)<1e-12 && std::abs(q.witness.y()-.5)<1e-12);
 for(int i=0;i<=100;++i)for(int j=0;j<=100-i;++j){const double x=i/100.,y=j/100.;CHECK(std::abs(surface.elevation(x,y))<=q.bound);}
 d.heights[3]=std::numeric_limits<double>::quiet_NaN();CHECK(!meshkit::RasterSurface(d).query(p,z).valid);
 d.columns=d.rows=4;d.heights.resize(16);for(int y=0;y<4;++y)for(int x=0;x<4;++x)d.heights[y*4+x]=2*x+3*y;meshkit::RasterSurface plane(d);meshkit::Point2 tri[3]={{.2,.3},{2.6,.7},{.5,2.8}};double h[3];for(int i=0;i<3;++i)h[i]=plane.elevation(tri[i].x(),tri[i].y());q=plane.query(tri,h);CHECK(q.valid && q.bound<1e-10);d.maximumCellsPerQuery=1;CHECK(meshkit::RasterSurface(d).query(tri,h).resourceLimited);
 std::cout<<"Native bilinear terrain certificate checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
