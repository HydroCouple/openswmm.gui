// SPDX-License-Identifier: GPL-3.0-or-later
#include "meshkit/io.hpp"
#include "meshkit/cdt.hpp"
#include "file.hpp"
#include <fstream>
#include <charconv>
#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <limits>
namespace meshkit {
namespace {
using namespace native_detail;
struct Records {
    std::ifstream input;Work &work;std::string line;std::uint64_t number=0,bytes=0,hash=hashSeed,limit;
    Records(const std::filesystem::path &path,Work &w):input(path,std::ios::binary),work(w),limit(w.limits.maxRecordBytes){if(!input)fail("open_input","Cannot open input file.",RunStatus::Failed);}
    bool next(){line.clear();char c;bool any=false;
        while(input.get(c)){any=true;++bytes;hashBytes(hash,reinterpret_cast<const unsigned char *>(&c),1);if(c=='\n')break;if(line.size()>=limit)fail("record_limit","2DM line exceeds the record limit.",RunStatus::ResourceLimit);line.push_back(c);if((bytes&65535)==0)work.check();}
        if(input.bad())fail("read","Input read failed.",RunStatus::Failed);if(!any)return false;++number;work.tick();return true;}
    void rewind(){input.clear();input.seekg(0);if(!input)fail("seek","Input is not seekable.",RunStatus::Failed);number=bytes=0;hash=hashSeed;}
};
struct Tokens {
    std::string_view text;
    std::string_view next(){const auto begin=text.find_first_not_of(" \t\r");if(begin==text.npos){text={};return {};}
        text.remove_prefix(begin);const auto end=text.find_first_of(" \t\r");auto value=text.substr(0,end);text=end==text.npos?std::string_view{}:text.substr(end);return value;}
    void end(){if(!next().empty())fail("extra_fields","Unexpected 2DM fields; attributes cannot be silently discarded.",RunStatus::Failed);}
};
std::int64_t integer(std::string_view text){std::int64_t value=0;if(!text.empty() && text.front()=='+')text.remove_prefix(1);const auto r=std::from_chars(text.data(),text.data()+text.size(),value);if(text.empty() || r.ec!=std::errc{} || r.ptr!=text.data()+text.size())fail("integer","Invalid 2DM integer.",RunStatus::Failed);return value;}
double real(std::string_view text){double value=0;if(!text.empty() && text.front()=='+')text.remove_prefix(1);const auto r=std::from_chars(text.data(),text.data()+text.size(),value,std::chars_format::general);if(text.empty() || r.ec!=std::errc{} || r.ptr!=text.data()+text.size() || !std::isfinite(value))fail("number","Invalid/nonfinite 2DM coordinate.",RunStatus::Failed);return value;}
std::int64_t identifier(Tokens &t,const IOOptions &o){const auto id=integer(t.next());if(!o.legacy2dm && id<=0)fail("identifier","2DM identifiers must be positive.",RunStatus::Failed);return id;}
void loss(IOReport &report,std::string code){if(std::find(report.losses.begin(),report.losses.end(),code)==report.losses.end() && report.losses.size()<64)report.losses.push_back(std::move(code));}
struct Lookup {
    const Mesh &mesh;std::vector<std::pair<std::int64_t,Index>> sparse;
    Lookup(const Mesh &m,Work &work):mesh(m){if(m.vertexIds.empty())return;work.memory(m.vertices.size()*80);sparse.reserve(m.vertices.size());for(std::size_t i=0;i<m.vertices.size();++i){work.tick();sparse.emplace_back(m.vertexIds[i],Index(i));}
        std::uint64_t comparisons=0;std::sort(sparse.begin(),sparse.end(),[&](auto a,auto b){if((++comparisons&16383)==0)work.check();return a.first<b.first;});
        for(std::size_t i=1;i<sparse.size();++i){work.tick();if(sparse[i-1].first==sparse[i].first)fail("duplicate_id","Duplicate node identifier.",RunStatus::Failed);}}
    Index get(std::int64_t id) const {if(sparse.empty()){if(id>0 && std::uint64_t(id)<=mesh.vertices.size())return Index(id-1);}else {auto at=std::lower_bound(sparse.begin(),sparse.end(),std::pair<std::int64_t,Index>{id,0});if(at!=sparse.end() && at->first==id)return at->second;}fail("missing_node","Unknown node identifier.",RunStatus::Failed);}
};
void orient(Cell &cell,const Mesh &m,IOReport &report){auto point=[&](int i){const auto &v=m.vertices[cell.vertices[i]];return Point2(v.x,v.y);};
    if(ConstrainedDelaunay::orientExact(point(0),point(1),point(2))<0){std::reverse(cell.vertices.begin(),cell.vertices.begin()+cell.count);++report.reorientedCells;}
    for(int i=0;i<cell.count;++i)if(!(ConstrainedDelaunay::orientExact(point(i),point((i+1)%cell.count),point((i+2)%cell.count))>0))fail("cell_geometry","Degenerate or nonconvex 2DM cell.",RunStatus::Failed);
}
void token(Output &out,std::string_view s){out.raw(s.data(),s.size(),false);}
void number(Output &out,std::int64_t v){char b[32];const auto r=std::to_chars(b,b+sizeof(b),v);token(out,{b,std::size_t(r.ptr-b)});}
void coordinate(Output &out,double v){char b[64];const auto r=std::to_chars(b,b+sizeof(b),v,std::chars_format::general,std::numeric_limits<double>::max_digits10);if(r.ec!=std::errc{})fail("number","Cannot encode coordinate.",RunStatus::Failed);token(out,{b,std::size_t(r.ptr-b)});}
}
ReadResult read2dm(const std::filesystem::path &path,const IOOptions &options){
    ReadResult result;auto &report=result.report;const auto start=std::chrono::steady_clock::now();
    try {Work work(options.limits,options.cancelled);Records records(path,work);auto &m=result.mesh;const auto &l=options.limits;m.metadata.unitsKnown=false;std::uint64_t cellCount=0;
        while(records.next()){report.record=records.number;Tokens t{records.line};auto card=t.next();if(card.empty() || card.front()=='#' || card.substr(0,2)==";;")continue;
            if(card=="MESH2D"){t.end();continue;}
            if(card=="ND"){const auto id=identifier(t,options);Point3 p{real(t.next()),real(t.next()),real(t.next())};t.end();
                if(m.vertices.size()>=l.maxVertices || m.vertices.size()+1>=missingIndex)fail("count_limit","Too many 2DM nodes.",RunStatus::ResourceLimit);work.memory((m.vertices.size()+1)*96+cellCount*40);appendId(m.vertexIds,id,m.vertices.size());m.vertices.push_back(p);}
            else if(card=="E3T" || card=="E4Q"){if(++cellCount>l.maxCells)fail("count_limit","Too many 2DM cells.",RunStatus::ResourceLimit);}
            else if(card=="NS"){}
            else {if(options.unknownRecords==UnknownRecords::Reject)fail("unsupported_card","Unsupported 2DM card: "+std::string(card),RunStatus::Failed);++report.ignoredRecords;loss(report,"ignored_card:"+std::string(card.substr(0,64)));}
        }
        if(m.vertices.empty() || !cellCount)fail("empty_mesh","2DM requires nodes and cells.",RunStatus::Failed);
        const auto firstHash=records.hash,firstBytes=records.bytes;Lookup lookup(m,work);work.memory(m.vertices.size()*96+cellCount*48);m.cells.reserve(cellCount);
        std::unordered_map<std::string,std::int32_t> legacyMaterials;std::uint64_t metadata=0,pathIndices=0;Path pending;pending.kind=PathKind::Line;
        records.rewind();while(records.next()){report.record=records.number;Tokens t{records.line};const auto card=t.next();
            if(card=="E3T" || card=="E4Q"){const auto id=identifier(t,options);Cell cell;cell.count=card=="E4Q"?4:3;for(int k=0;k<cell.count;++k)cell.vertices[k]=lookup.get(identifier(t,options));const auto material=t.next();
                if(options.legacy2dm){std::string key(material);auto found=legacyMaterials.find(key);if(found==legacyMaterials.end()){metadata+=key.size()+32;if(metadata>l.maxMetadataBytes || legacyMaterials.size()>=std::size_t(INT32_MAX))fail("metadata_limit","Material label limit exceeded.",RunStatus::ResourceLimit);const auto region=std::int32_t(legacyMaterials.size()+1);found=legacyMaterials.emplace(key,region).first;m.labels.push_back({region,key.empty()?"":"mat"+key});}cell.region=found->second;}
                else {const auto mat=integer(material);if(mat<INT32_MIN || mat>INT32_MAX)fail("material","Material id exceeds int32.",RunStatus::Failed);cell.region=std::int32_t(mat);}t.end();orient(cell,m,report);appendId(m.cellIds,id,m.cells.size());m.cells.push_back(cell);}
            else if(card=="NS"){auto value=t.next();if(value.empty())fail("node_string","Empty node string record.",RunStatus::Failed);
                while(!value.empty()){const auto raw=integer(value);if(!raw || raw==INT64_MIN)fail("node_string","Invalid node string identifier.",RunStatus::Failed);if(pathIndices>=l.maxEdges || pathIndices>=l.maxWorkspaceBytes/8)fail("path_limit","Node string limit exceeded.",RunStatus::ResourceLimit);++pathIndices;pending.vertices.push_back(lookup.get(raw<0?-raw:raw));
                    if(raw<0){t.end();if(pending.vertices.size()<2)fail("node_string","Node string needs two nodes.",RunStatus::Failed);pending.id=std::int64_t(m.paths.size()+1);if(pending.vertices.size()>2 && pending.vertices.front()==pending.vertices.back()){pending.closed=true;pending.vertices.pop_back();}m.paths.push_back(std::move(pending));pending=Path{};break;}value=t.next();}}
        }
        if(!pending.vertices.empty())fail("node_string","Unterminated node string.",RunStatus::Failed);
        if(firstHash!=records.hash || firstBytes!=records.bytes)fail("source_changed","2DM changed during its two-pass read.",RunStatus::Failed);
        validated(m,options);report.bytes=firstBytes;report.work=work.used;report.counts.vertices=m.vertices.size();for(const auto &c:m.cells)(c.count==4?report.counts.quads:report.counts.triangles)++;
        report.status=report.ignoredRecords?RunStatus::CompleteWithExceptions:RunStatus::Complete;report.code="read";report.message="2DM read and validated; physical units require declaration.";
    }catch(const native_detail::Stop &e){native_detail::setError(report,e);result.mesh={};}catch(const std::bad_alloc &e){native_detail::setException(report,e);result.mesh={};}catch(const std::exception &e){native_detail::setException(report,e);result.mesh={};}
    report.seconds=native_detail::seconds(start);return result;
}
IOReport write2dm(const std::filesystem::path &path,const Mesh &m,const IOOptions &options){
    IOReport report;const auto start=std::chrono::steady_clock::now();
    try {Work work(options.limits,options.cancelled);validated(m,options);
        if(m.vertices.empty() || m.cells.empty())fail("empty_mesh","2DM requires nodes and cells.");
        if(m.metadata.unitsKnown || !m.metadata.crs.empty() || !m.metadata.description.empty())loss(report,"units_crs_description");
        if(!m.vertexMarkers.empty())loss(report,"vertex_markers");if(!m.labels.empty())loss(report,"labels");if(!m.aliases.empty())loss(report,"source_aliases");if(!m.edges.empty())loss(report,"edge_attributes");
        for(const auto &p:m.paths)if(p.kind!=PathKind::Line || p.marker || p.id!=std::int64_t(&p-m.paths.data()+1))loss(report,"path_attributes");
        for(std::size_t i=0;i<m.vertices.size();++i)if(externalId(m.vertexIds,i)<=0)fail("identifier","2DM requires positive node ids.");
        for(std::size_t i=0;i<m.cells.size();++i)if(externalId(m.cellIds,i)<=0)fail("identifier","2DM requires positive element ids.");
        if(!report.losses.empty() && !options.allowAttributeLoss)fail("attribute_loss","2DM cannot preserve all attributes; use MKB or explicitly allow reported losses.");
        Transaction transaction(path,options);Output out(transaction.file(),work);token(out,"MESH2D\n");
        for(std::size_t i=0;i<m.cells.size();++i){work.tick();const auto &c=m.cells[i];token(out,c.count==4?"E4Q ":"E3T ");number(out,externalId(m.cellIds,i));for(int k=0;k<c.count;++k){token(out," ");number(out,externalId(m.vertexIds,c.vertices[k]));}token(out," ");number(out,c.region);token(out,"\n");}
        for(std::size_t i=0;i<m.vertices.size();++i){work.tick();const auto &v=m.vertices[i];token(out,"ND ");number(out,externalId(m.vertexIds,i));token(out," ");coordinate(out,v.x);token(out," ");coordinate(out,v.y);token(out," ");coordinate(out,v.z);token(out,"\n");}
        for(const auto &p:m.paths){const auto count=p.vertices.size()+(p.closed?1:0);for(std::size_t k=0;k<count;++k){work.tick();if(k%10==0)token(out,"NS");token(out," ");const auto id=externalId(m.vertexIds,p.vertices[k%p.vertices.size()]);number(out,k+1==count?-id:id);if(k%10==9 || k+1==count)token(out,"\n");}}
        out.flush();work.check();transaction.commit();report.bytes=out.bytes;report.work=work.used;report.status=report.losses.empty()?RunStatus::Complete:RunStatus::CompleteWithExceptions;report.code="written";report.message="2DM written atomically.";
    }catch(const Stop &e){setError(report,e);}catch(const std::bad_alloc &e){setException(report,e);}catch(const std::exception &e){setException(report,e);}
    report.seconds=seconds(start);return report;
}
}
