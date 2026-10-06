#include "layers/cellwatergeometry.h"
#include <chrono>
#include <iostream>
int main() {
    using namespace CellWaterGeometry;
    using Cell=VertexDepthReconstruct::CellSplit;
    constexpr int n=150;
    std::vector<double> x,y,z;
    std::vector<Cell> cells;
    auto vertex=[](int j,int k){return j*(n+1)+k;};
    for(int j=0;j<=n;++j)for(int k=0;k<=n;++k){
        x.push_back(5*j);y.push_back(5*k);z.push_back(30-0.35*j+0.15*k);
    }
    for(int j=0;j<n;++j)for(int k=0;k<n;++k){
        const int a=vertex(j,k),b=vertex(j+1,k),c=vertex(j+1,k+1),d=vertex(j,k+1);
        Cell t;t.area[0]=12.5;t.v={a,b,c,-1};t.sub[0]={a,b,c};cells.push_back(t);
        t.v={a,c,d,-1};t.sub[0]={a,c,d};cells.push_back(t);
    }
    std::vector<float> depths(cells.size(),.01f),flux(cells.size()*4,0);
    std::vector<Surface> surfaces;
    for(size_t c=0;c<cells.size();++c){
        surfaces.push_back(reconstruct(cells[c],depths[c],z));
        for(int e=0;e<3;++e){
            const int a=cells[c].v[(e+1)%3],b=cells[c].v[(e+2)%3];
            flux[4*c+e]=float(.1*(y[b]-y[a])-.03*(x[b]-x[a]));
        }
    }
    const auto topology=smoothTopology(cells);
    const auto begin=std::chrono::steady_clock::now();
    const auto raw=flowingCornerDepths(cells,surfaces,x,y,z,topology,depths,flux);
    std::vector<CornerDepths> field;
    smoothCornerDepths(cells,surfaces,z,topology,field,{},raw);
    const auto end=std::chrono::steady_clock::now();
    double error=0;size_t dry=0,worst=0,bad=0;
    for(size_t c=0;c<cells.size();++c)for(int k=0;k<3;++k){
        const double err=std::abs(field[c][k]-depths[c]); if(err>error){error=err;worst=c;} if(err>1e-6)++bad;
        if(!(field[c][k]>0))++dry;
    }
    std::cout<<"Cells: "<<cells.size()<<"; max depth error (m): "<<error<<"; dry corners: "<<dry
        <<"; reconstruction and projection (ms): "<<std::chrono::duration<double,std::milli>(end-begin).count()<<'\n';
    std::cout<<"Worst cell "<<worst<<" at "<<x[cells[worst].v[0]]<<","<<y[cells[worst].v[0]]<<" raw "<<raw[worst][0]<<","<<raw[worst][1]<<","<<raw[worst][2]<<"; nonuniform corners "<<bad<<"\n";
    return raw.size()==cells.size() && error<1e-6 && dry==0 ? 0:1;
}
