// SPDX-License-Identifier: GPL-3.0-or-later
#include "meshkit/io.hpp"
#include "meshkit/job.hpp"
#include <iostream>
#include <chrono>
#include <cmath>
#include <stdexcept>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif
using namespace meshkit;
static void require(bool ok,const OperationReport &r){if(!ok)throw std::runtime_error(r.code+": "+r.message);}
static double time(std::chrono::steady_clock::time_point t){return std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();}
static std::uint64_t peak(){
#if defined(__unix__) || defined(__APPLE__)
 struct rusage usage{};getrusage(RUSAGE_SELF,&usage);
#ifdef __APPLE__
 return usage.ru_maxrss;
#else
 return std::uint64_t(usage.ru_maxrss)*1024;
#endif
#else
 return 0;
#endif
}
int main(int argc,char **argv){try{const std::string mode=argc>1?argv[1]:"io";const int n=argc>2?std::stoi(argv[2]):100; if(n<2 || n>5000)throw std::runtime_error("side must be 2..5000");
 const auto start=std::chrono::steady_clock::now();
 if(mode=="job"){JobRequest r;r.points.reserve(std::size_t(n)*n);for(int y=0;y<n;++y)for(int x=0;x<n;++x)r.points.emplace_back(x,y);
  r.paths={{{0,Index(n-1),Index(n*n-1),Index(n*(n-1))},PathKind::Outer,1,1,true}};r.options.maximumSize=1.1;r.options.minimumTarget=.25;r.options.minimumAngleDegrees=20;r.options.limits.maxWorkspaceBytes=std::uint64_t(6)<<30;
  auto result=runJob(r);require(result.report.complete(),result.report);std::cout<<"native_job vertices="<<result.mesh.vertices.size()<<" cells="<<result.mesh.cells.size()<<" seconds="<<time(start)<<" peak_bytes="<<peak()<<" size_conflicts="<<result.report.sizeConflicts<<" work="<<result.report.work<<'\n';return 0;}
 if(mode!="io")throw std::runtime_error("mode must be io or job");
 const auto dir=std::filesystem::temp_directory_path()/("meshkit-scale-"+std::to_string(start.time_since_epoch().count()));std::filesystem::create_directories(dir);
 struct Cleanup {std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}}cleanup{dir};
 Mesh mesh;mesh.metadata.unitsKnown=false;mesh.vertices.reserve(std::size_t(n)*n);mesh.cells.reserve(std::size_t(n-1)*(n-1));
 for(int y=0;y<n;++y)for(int x=0;x<n;++x)mesh.vertices.push_back({double(x),double(y),.01*x+.02*y});
 for(int y=0;y<n-1;++y)for(int x=0;x<n-1;++x){const auto a=Index(y*n+x);mesh.cells.push_back({{a,a+1,a+1+Index(n),a+Index(n)},2,4});}
 const auto nv=mesh.vertices.size(),nc=mesh.cells.size();std::cout<<"native_io vertices="<<nv<<" cells="<<nc<<std::endl;
 for(const std::string format:{"mkb","2dm"}){const auto path=dir/("mesh."+format);auto t=std::chrono::steady_clock::now();auto w=format=="mkb"?writeBinary(path,mesh):write2dm(path,mesh);require(w.complete(),w);const auto writeSeconds=time(t);
  t=std::chrono::steady_clock::now();auto r=format=="mkb"?readBinary(path):read2dm(path);require(r.report.complete(),r.report);if(r.mesh.vertices.size()!=nv || r.mesh.cells.size()!=nc || r.mesh.vertices.back().z!=mesh.vertices.back().z)throw std::runtime_error("count/coordinate mismatch");
  std::cout<<format<<" bytes="<<w.bytes<<" write_seconds="<<writeSeconds<<" read_seconds="<<time(t)<<" peak_bytes="<<peak()<<std::endl;std::filesystem::remove(path);}
 std::cout<<"total_seconds="<<time(start)<<std::endl;return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
