#include "layers/cellwatergeometry.h"
#include <hdf5.h>
#include <iostream>
#include <fstream>
#include <numeric>
#include <chrono>
#include <iomanip>
template<class T> std::vector<T> read(hid_t file,const char* name,hid_t type) {
    const auto d=H5Dopen2(file,name,H5P_DEFAULT),s=H5Dget_space(d);
    std::vector<T> a(size_t(H5Sget_simple_extent_npoints(s)));
    if(H5Dread(d,type,H5S_ALL,H5S_ALL,H5P_DEFAULT,a.data())<0) std::abort();
    H5Sclose(s);H5Dclose(d);return a;
}
int main(int argc,char** argv) {
    using namespace CellWaterGeometry;
    if(argc!=2)return 1;
    const auto file=H5Fopen(argv[1],H5F_ACC_RDONLY,H5P_DEFAULT);if(file<0)return 2;
    auto x=read<double>(file,"Mesh2_node_x",H5T_NATIVE_DOUBLE);
    auto y=read<double>(file,"Mesh2_node_y",H5T_NATIVE_DOUBLE);
    auto z=read<double>(file,"Mesh2_node_z",H5T_NATIVE_DOUBLE);
    auto conn=read<int>(file,"Mesh2_face_nodes",H5T_NATIVE_INT);
    auto h=read<float>(file,"Mesh2_face_depth",H5T_NATIVE_FLOAT);
    auto q=read<float>(file,"Mesh2_edge_flux",H5T_NATIVE_FLOAT);
    auto times=read<double>(file,"time",H5T_NATIVE_DOUBLE);
    H5Fclose(file);
    const size_t n=conn.size()/3;
    std::vector<VertexDepthReconstruct::CellSplit> cells(n);
    for(size_t c=0;c<n;++c) {
        auto& t=cells[c];const int a=conn[3*c],b=conn[3*c+1],d=conn[3*c+2];
        t.v={a,b,d,-1};t.sub[0]={a,b,d};
        t.area[0]=0.5*std::abs((x[b]-x[a])*(y[d]-y[a])-(x[d]-x[a])*(y[b]-y[a]));
    }
    const auto topology=smoothTopology(cells);
    auto slot=[&](int a,int b){int start=(a%4+1)%3==b%4?a%4:b%4;return 4*(a/4)+(start+2)%3;};
    std::cout<<std::setprecision(12)<<"frame,wet_mean_gt_1mm,visible_vfr,throughflow,flow_edges,reciprocal_edges,changed_cells,partial_after,discontinuous_edges,tiny_mean_visible,stored_volume,displayed_volume,relative_rms,max_relative_error,absolute_rms,p95_absolute_error,max_absolute_error,volume_relative_error,fit_ms\n";
    for(size_t f=0;f<times.size();++f) {
        std::vector<float> depths(h.begin()+f*n,h.begin()+(f+1)*n),flux(n*4,0);
        std::vector<Surface> surfaces;
        std::vector<bool> active(n,false);
        size_t wet=0,visibleVfr=0,through=0,flowEdges=0,reciprocal=0,changed=0,partial=0,seams=0,tiny=0;
        VisibilityPolicy policy{.0001,false}; // Bellinge [2D_OPTIONS] DRY_DEPTH
        for(size_t c=0;c<n;++c) {
            surfaces.push_back(reconstruct(cells[c],depths[c],z));
            if(depths[c]>.001)++wet;
            bool vis=visible(displayState(cells[c],surfaces[c],z,policy),policy);
            if(vis)++visibleVfr;
            if(vis && depths[c]<=1e-5)++tiny;
            double in=0,out=0;
            for(int k=0;k<3;++k){double v=flux[4*c+k]=q[(f*n+c)*3+k];in=std::max(in,-v);out=std::max(out,v);}
            active[c]=vis && std::min(in,out)>64*std::numeric_limits<float>::epsilon()*std::max(in,out);
            if(active[c])++through;
        }
        for(auto e:topology.edges) {
            if(!active[e.a0/4] || !active[e.a1/4])continue;
            const double a=flux[slot(e.a0,e.b0)],b=flux[slot(e.a1,e.b1)];
            if(a*b<0){++flowEdges;if(std::abs(a+b)<=1e-4*std::max(std::abs(a),std::abs(b)))++reciprocal;}
        }
        const auto begin=std::chrono::steady_clock::now();
        const auto raw=flowingCornerDepths(cells,surfaces,x,y,z,topology,depths,flux,policy);
        const auto end=std::chrono::steady_clock::now();
        double stored=0,rendered=0,squares=0,maximum=0,area=0,absSquares=0,maxAbs=0;
        std::vector<double> absErrors;
        for(size_t c=0;c<n;++c)if(!raw.empty() && std::isfinite(raw[c][0])) {
            const auto& d=raw[c];double mean=VertexDepthReconstruct::triMeanDepthFromEta(0,-d[0],-d[1],-d[2]);
            double a=cells[c].area[0],e=std::abs(mean-depths[c])/std::max(double(depths[c]),policy.filmDepth);
            stored+=a*depths[c];rendered+=a*mean;squares+=a*e*e;area+=a;maximum=std::max(maximum,e);
            const double abs=std::abs(mean-depths[c]);absSquares+=a*abs*abs;maxAbs=std::max(maxAbs,abs);absErrors.push_back(abs);
        }
        std::vector<CornerDepths> field;smoothCornerDepths(cells,surfaces,z,topology,field,policy,raw);
        for(size_t c=0;c<n;++c) {
            bool differs=false;
            for(int k=0;k<3;++k)if(!raw.empty() && std::abs(raw[c][k]-surfaces[c].signedDepth(z[cells[c].v[k]]))>1e-6)differs=true;
            if(differs)++changed;
            if(displayState(field[c],3,policy)==DisplayState::PartiallyWet)++partial;
        }
        for(auto e:topology.edges) {
            const auto &a=field[e.a0/4],&b=field[e.a1/4];
            if(std::isfinite(a[0]) && std::isfinite(b[0]) &&
               (std::abs(a[e.a0%4]-b[e.a1%4])>1e-6 || std::abs(a[e.b0%4]-b[e.b1%4])>1e-6))++seams;
        }
        std::sort(absErrors.begin(),absErrors.end());
        std::cout<<f<<','<<wet<<','<<visibleVfr<<','<<through<<','<<flowEdges<<','<<reciprocal<<','<<changed<<','<<partial<<','<<seams<<','<<tiny<<','<<stored<<','<<rendered<<','<<std::sqrt(squares/area)<<','<<maximum<<','<<std::sqrt(absSquares/area)<<','<<absErrors[size_t(.95*(absErrors.size()-1))]<<','<<maxAbs<<','<<(rendered-stored)/stored<<','<<std::chrono::duration<double,std::milli>(end-begin).count()<<'\n';
    }
    size_t unchanged=0;
    for(size_t c=0;c<n;++c) {
        float low=h[3*n+c],high=low;
        for(size_t f=4;f<times.size();++f) {low=std::min(low,h[f*n+c]);high=std::max(high,h[f*n+c]);}
        if(high>0 && high-low<1e-7)++unchanged;
    }
    std::cerr<<"Positive cell means unchanged within 1e-7 m over frames 3..11: "<<unchanged<<'/'<<n<<'\n';
}
