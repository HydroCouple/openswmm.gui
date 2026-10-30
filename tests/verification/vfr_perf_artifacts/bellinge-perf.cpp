// Header-level timing of the 2D water-surface reconstruction on the Bellinge
// fixture. Plan: workplans/2D_VFR_PERF_RECOVERY_PLAN_2026-10-02.md (M1).
// Uses the production headers. Solver arrays are read only.
//
// Build from the repository root (same optimization as the Release app):
//   c++ -std=c++17 -O3 -DNDEBUG -include arm_acle.h -Iinclude \
//     -F/Users/calebbuahin/Qt/6.9.3/macos/lib \
//     -I/Users/calebbuahin/Qt/6.9.3/macos/lib/QtCore.framework/Headers \
//     -I/Users/calebbuahin/Qt/6.9.3/macos/lib/QtGui.framework/Headers \
//     -I/opt/homebrew/opt/hdf5/include -L/opt/homebrew/opt/hdf5/lib -lhdf5 \
//     -framework QtCore -framework QtGui \
//     -Wl,-rpath,/Users/calebbuahin/Qt/6.9.3/macos/lib \
//     tests/verification/vfr_perf_artifacts/bellinge-perf.cpp \
//     -o tests/verification/vfr_perf_artifacts/<before|after>/bellinge-perf
// Run:
//   <binary> tests/verification/vfr_slope_steps/bellinge-partial-2026-10-01.h5 [repeats]
//     > <before|after>/bellinge-perf.csv 2> <before|after>/bellinge-perf.log
//
// Columns 1-15 are deterministic (counters and bit hashes of the fields) and
// must be identical between builds that claim bit-identical results. The
// remaining columns are timings: the minimum over `repeats` runs, in ms.
//   surface_ms       per-cell VFR inversion (layer: surfaceForDepth loop)
//   fit_ms           flowingCornerDepths (neighborhood fit)
//   smooth_after_ms  smoothCornerDepths consuming the fit (current path)
//   smooth_before_ms smoothCornerDepths without a fit (pre-fit projection)
//   fill_ms          emulation of the layer's per-triangle corner fill
#include "layers/cellwatergeometry.h"
#include <hdf5.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>

template<class T> std::vector<T> read(hid_t file,const char* name,hid_t type) {
    const auto d=H5Dopen2(file,name,H5P_DEFAULT),s=H5Dget_space(d);
    std::vector<T> a(size_t(H5Sget_simple_extent_npoints(s)));
    if(H5Dread(d,type,H5S_ALL,H5S_ALL,H5P_DEFAULT,a.data())<0) std::abort();
    H5Sclose(s);H5Dclose(d);return a;
}
using Field=std::vector<CellWaterGeometry::CornerDepths>;
static std::uint64_t hashField(const Field& field) {
    std::uint64_t h=1469598103934665603ull;
    for(const auto& corners:field)for(double value:corners) {
        std::uint64_t bits;std::memcpy(&bits,&value,sizeof bits);
        for(int i=0;i<8;++i){h^=(bits>>(8*i))&0xffu;h*=1099511628211ull;}
    }
    return h;
}
static bool sameBits(const Field& a,const Field& b) {
    return a.size()==b.size() && (a.empty() || std::memcmp(a.data(),b.data(),a.size()*sizeof(a[0]))==0);
}
template<class F> double minMs(int repeats,F&& f) {
    double best=1e300;
    for(int r=0;r<repeats;++r) {
        const auto begin=std::chrono::steady_clock::now();f();
        const auto end=std::chrono::steady_clock::now();
        best=std::min(best,std::chrono::duration<double,std::milli>(end-begin).count());
    }
    return best;
}
int main(int argc,char** argv) {
    using namespace CellWaterGeometry;
    if(argc<2)return 1;
    const int repeats=argc>2?std::max(1,std::atoi(argv[2])):3;
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
    const VisibilityPolicy policy{.0001,false}; // Bellinge [2D_OPTIONS] DRY_DEPTH
    std::cout<<"frame,rows,variables,groups,gn,cg_rhs,cg_mass,line_search,bracket,bisection,fallback,"
               "fit_hash,smooth_hash,old_hash,stats_inert,"
               "surface_ms,fit_ms,smooth_after_ms,smooth_before_ms,fill_ms\n";
    double cold=0,warm=0,oldCold=0,sink=0;
    for(size_t f=0;f<times.size();++f) {
        std::vector<float> depths(h.begin()+f*n,h.begin()+(f+1)*n),flux(n*4,0);
        for(size_t c=0;c<n;++c)for(int k=0;k<3;++k)flux[4*c+k]=q[(f*n+c)*3+k];
        std::vector<Surface> surfaces(n);
        const double surfaceMs=minMs(repeats,[&]{
            for(size_t c=0;c<n;++c)surfaces[c]=reconstruct(cells[c],depths[c],z);});
        Field raw,counted,field,old;
        const double fitMs=minMs(repeats,[&]{
            raw=flowingCornerDepths(cells,surfaces,x,y,z,topology,depths,flux,policy);});
        NeighborhoodFit::Stats stats;
        counted=flowingCornerDepths(cells,surfaces,x,y,z,topology,depths,flux,policy,&stats);
        const double afterMs=minMs(repeats,[&]{
            smoothCornerDepths(cells,surfaces,z,topology,field,policy,raw);});
        const double beforeMs=minMs(repeats,[&]{
            smoothCornerDepths(cells,surfaces,z,topology,old,policy);});
        const double fillMs=minMs(repeats,[&]{
            float peak=0;
            for(size_t c=0;c<n;++c)for(int k=0;k<3;++k) {
                const auto& ids=cells[c].v;
                const auto it=std::find(ids.begin(),ids.end(),cells[c].sub[0][k]);
                const float d=float(field[c][size_t(it-ids.begin())]);
                if(std::isfinite(d))peak=std::max(peak,d);
            }
            sink+=peak;});
        std::cout<<f<<','<<stats.rows<<','<<stats.variables<<','<<stats.groups<<','<<stats.gaussNewton<<','
                 <<stats.cgRhs<<','<<stats.cgMass<<','<<stats.lineSearch<<','<<stats.bracket<<','
                 <<stats.bisection<<','<<int(stats.fallback)<<','<<std::hex<<hashField(raw)<<','
                 <<hashField(field)<<','<<hashField(old)<<std::dec<<','<<int(sameBits(raw,counted))<<','
                 <<std::setprecision(6)<<surfaceMs<<','<<fitMs<<','<<afterMs<<','<<beforeMs<<','<<fillMs<<'\n';
        const double frame=surfaceMs+fitMs+afterMs;
        cold+=frame;oldCold+=surfaceMs+beforeMs;if(f+1==times.size())warm=frame;
    }
    std::cerr<<"cells="<<n<<" frames="<<times.size()<<" repeats="<<repeats<<'\n'
             <<"envelope_cold_all_frames_ms="<<cold<<'\n'
             <<"envelope_warm_newest_frame_ms="<<warm<<'\n'
             <<"prefit_projection_all_frames_ms="<<oldCold<<'\n'
             <<"sink="<<sink<<'\n';
}
