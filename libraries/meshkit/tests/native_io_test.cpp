// SPDX-License-Identifier: GPL-3.0-or-later
#include "meshkit/job.hpp"
#include "meshkit/io.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("check failed: ")+#x+":"+std::to_string(__LINE__));}while(false)
using namespace meshkit;
std::string bytes(const std::filesystem::path &p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void put(const std::filesystem::path &p,const std::string &s){std::ofstream out(p,std::ios::binary);out<<s;}
int main(){const auto dir=std::filesystem::temp_directory_path()/("meshkit-io-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(dir);
 try {Mesh m;m.vertices={{0,0,1},{2,0,2},{2,2,3},{0,2,4}};m.cells={{{0,1,2,3},8,4}};m.edges={{0,1,5}};m.paths={{{0,1,2},PathKind::Outer,7,99,true}};m.vertexIds={10,20,30,40};m.cellIds={900};m.edgeIds={60};m.vertexMarkers={1,0,2,0};m.labels={{8,"terrain λ"}};m.aliases={{0,50,88}};m.metadata.crs="EPSG:32618";m.metadata.description="fixture";
 auto p=dir/"mesh.mkb";auto w=writeBinary(p,m);CHECK(w.complete());auto raw=bytes(p);auto r=readBinary(p);CHECK(r.report.complete());CHECK(r.mesh.vertices.size()==4);CHECK(r.mesh.cells[0].count==4);CHECK(r.mesh.cellIds==m.cellIds);CHECK(r.mesh.vertexIds==m.vertexIds);CHECK(r.mesh.edgeIds==m.edgeIds);CHECK(r.mesh.labels[0].name==m.labels[0].name);CHECK(r.mesh.paths[0].id==99);CHECK(r.mesh.aliases[0].marker==88);CHECK(r.mesh.metadata.crs==m.metadata.crs);
 CHECK(!writeBinary(p,m).complete());CHECK(bytes(p)==raw);IOOptions overwrite;overwrite.overwrite=true;overwrite.cancelled=[]{return true;};CHECK(writeBinary(p,m,overwrite).status==RunStatus::Cancelled);CHECK(bytes(p)==raw);
 Mesh bad=m;bad.cells[0].vertices[1]=77;overwrite.cancelled={};CHECK(!writeBinary(p,bad,overwrite).complete());CHECK(bytes(p)==raw);
 auto damaged=raw;damaged[100]^=1;std::istringstream corrupt(damaged);auto broken=readBinary(corrupt);CHECK(!broken.report.complete());CHECK(broken.mesh.vertices.empty());
 for(auto n:{0u,7u,50u,150u,unsigned(raw.size()-1)}){std::istringstream shortInput(raw.substr(0,n));CHECK(!readBinary(shortInput).report.complete());}
 std::istringstream trailing(raw+"x");CHECK(readBinary(trailing).report.code=="trailing_data");IOOptions tiny;tiny.limits.maxVertices=2;CHECK(readBinary(p,tiny).report.status==RunStatus::ResourceLimit);
 auto text=dir/"mesh.2dm";CHECK(write2dm(text,m).code=="attribute_loss");IOOptions lossy;lossy.allowAttributeLoss=true;auto tw=write2dm(text,m,lossy);CHECK(tw.status==RunStatus::CompleteWithExceptions);auto tr=read2dm(text);CHECK(tr.report.complete());CHECK(tr.mesh.vertexIds==m.vertexIds);CHECK(tr.mesh.cellIds==m.cellIds);CHECK(!tr.mesh.metadata.unitsKnown);CHECK(tr.mesh.paths.size()==1);CHECK(tr.mesh.paths[0].closed);CHECK(tr.mesh.paths[0].vertices==m.paths[0].vertices);
 put(text,"MESH2D\nE3T 30 10 40 20 5\nND 10 0 0 0\nND 20 1 0 0\nND 40 0 1 0\nNS 10 20\nNS -40\n");tr=read2dm(text);CHECK(tr.report.complete());CHECK(tr.report.reorientedCells==1);CHECK(tr.mesh.paths[0].vertices.size()==3);CHECK(tr.mesh.cellIds[0]==30);
 auto valid=bytes(text);put(text,valid+"BOGUS data\n");CHECK(!read2dm(text).report.complete());IOOptions ignore;ignore.unknownRecords=UnknownRecords::IgnoreWithReport;auto ignored=read2dm(text,ignore);CHECK(ignored.report.complete());CHECK(ignored.report.ignoredRecords==1);
 for(const std::string extra:{"ND 10 8 8 8\n","ND 70 nan 0 0\n","NS 10 20\n","E3T 30 10 20 40 5\n","E3T 31 10 20 99 5\n"}){put(text,valid+extra);auto invalid=read2dm(text);CHECK(!invalid.report.complete());CHECK(invalid.mesh.vertices.empty());}
 put(text,valid+std::string(100,'a'));tiny={};tiny.limits.maxRecordBytes=50;CHECK(read2dm(text,tiny).report.code=="record_limit");
 put(text,"E3T 1 0 1 2 earth\nND 0 0 0 0\nND 1 1 0 0\nND 2 0 1 0\n");CHECK(!read2dm(text).report.complete());IOOptions legacy;legacy.legacy2dm=true;auto old=read2dm(text,legacy);CHECK(old.report.complete());CHECK(old.mesh.labels[0].name=="matearth");
 for(const auto &entry:std::filesystem::directory_iterator(dir))CHECK(entry.path().filename().string().find(".meshkit-")==std::string::npos);
 std::filesystem::remove_all(dir);std::cout<<"native streaming I/O checks passed\n";return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<"\n";std::filesystem::remove_all(dir);return 1;}}
