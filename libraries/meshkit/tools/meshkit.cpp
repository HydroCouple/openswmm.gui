// SPDX-License-Identifier: GPL-3.0-or-later
#include "meshkit/job.hpp"
#include "meshkit/io.hpp"
#include <iostream>
#include <charconv>
#include <cstring>
#include <algorithm>
#include <csignal>
#include <limits>
#include <cmath>
namespace {volatile std::sig_atomic_t stopped=0;void stop(int){stopped=1;}
double number(const char *s){double v;auto end=s+std::strlen(s);auto r=std::from_chars(s,end,v);if(r.ec!=std::errc{} || r.ptr!=end || !std::isfinite(v))throw std::runtime_error("Invalid number.");return v;}
void report(const meshkit::OperationReport &r){std::cout<<meshkit::statusName(r.status)<<": "<<r.code<<": "<<r.message<<" ("<<r.seconds<<" s)\n";}
meshkit::ReadResult read(const std::filesystem::path &p,const meshkit::IOOptions &o){return p.extension()==".mkb"?meshkit::readBinary(p,o):meshkit::read2dm(p,o);}
meshkit::IOReport write(const std::filesystem::path &p,const meshkit::Mesh &m,const meshkit::IOOptions &o){if(p.extension()==".mkb")return meshkit::writeBinary(p,m,o);if(p.extension()==".2dm")return meshkit::write2dm(p,m,o);throw std::runtime_error("Output extension must be .mkb or .2dm.");}}
int main(int argc,char **argv){std::signal(SIGINT,stop);std::signal(SIGTERM,stop);meshkit::IOOptions io;io.cancelled=[]{return stopped!=0;};
 try {if(argc<3){std::cerr<<"meshkit inspect FILE\nmeshkit convert INPUT OUTPUT [--overwrite] [--allow-loss]\nmeshkit box X0 Y0 X1 Y1 MAX_SIZE OUTPUT [--overwrite] [--allow-loss]\nbox declares flat elevation zero and metre units. MKB preserves generic metadata.\n";return 2;}
 const std::string command=argv[1];const int required=command=="box"?8:command=="convert"?4:command=="inspect"?3:0;if(!required || argc<required)throw std::runtime_error("Invalid command/arguments; run meshkit for usage.");
 for(int i=required;i<argc;++i){std::string flag=argv[i];if(flag=="--overwrite")io.overwrite=true;else if(flag=="--allow-loss")io.allowAttributeLoss=true;else throw std::runtime_error("Unknown option: "+flag);}
 if(command=="inspect" || command=="convert"){auto r=read(argv[2],io);report(r.report);if(!r.report.complete())return 1;
  if(command=="inspect"){std::cout<<r.mesh.vertices.size()<<" vertices, "<<r.mesh.cells.size()<<" cells, "<<r.mesh.paths.size()<<" paths; units "<<(r.mesh.metadata.unitsKnown?"declared":"unknown")<<"\n";return 0;}
  auto w=write(argv[3],r.mesh,io);report(w);for(const auto &loss:w.losses)std::cout<<"attribute loss: "<<loss<<"\n";return w.complete()?0:1;}
 const double x0=number(argv[2]),y0=number(argv[3]),x1=number(argv[4]),y1=number(argv[5]),h=number(argv[6]);if(!(x1>x0 && y1>y0 && h>0))throw std::runtime_error("Require X1>X0, Y1>Y0, MAX_SIZE>0.");
 meshkit::JobRequest request;request.points={{x0,y0},{x1,y0},{x1,y1},{x0,y1}};request.paths={{{0,1,2,3},meshkit::PathKind::Outer,0,1,true}};request.options.maximumSize=h;request.options.minimumTarget=h/4;request.metadata.description="Flat rectangular domain; elevation 0; metre units.";
 meshkit::Providers providers;providers.cancelled=io.cancelled;providers.identity="meshkit-cli/flat-rectangle-v1";auto r=meshkit::runJob(request,providers);report(r.report);if(!r.report.complete())return 1;auto w=write(argv[7],r.mesh,io);report(w);for(const auto &loss:w.losses)std::cout<<"attribute loss: "<<loss<<"\n";return w.complete()?0:1;
 }catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 2;}}
