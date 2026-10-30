// SPDX-License-Identifier: GPL-3.0-or-later
#include "meshkit/io.hpp"
#include "file.hpp"
#include <fstream>
namespace meshkit {
namespace {constexpr std::array<unsigned char,8> magic={'M','E','S','H','K','I','T',0};}
IOReport writeBinary(const std::filesystem::path &path,const Mesh &mesh,const IOOptions &options){
    using namespace native_detail;IOReport report;const auto start=std::chrono::steady_clock::now();
    try {Work work(options.limits,options.cancelled);validated(mesh,options);Transaction transaction(path,options);Output out(transaction.file(),work);
        out.raw(magic.data(),magic.size());out.number(1,4);out.number(mesh.metadata.unitsKnown?1:0,4);
        for(auto count:{mesh.vertices.size(),mesh.cells.size(),mesh.edges.size(),mesh.paths.size(),mesh.labels.size(),mesh.aliases.size()})out.number(count,8);
        out.real(mesh.metadata.horizontalMetresPerUnit);out.real(mesh.metadata.verticalMetresPerUnit);out.text(mesh.metadata.crs);out.text(mesh.metadata.description);
        for(const auto &label:mesh.labels){work.tick();out.number(std::uint32_t(label.id),4);out.text(label.name);}
        for(std::size_t i=0;i<mesh.vertices.size();++i){work.tick();const auto &v=mesh.vertices[i];out.number(std::uint64_t(externalId(mesh.vertexIds,i)),8);out.real(v.x);out.real(v.y);out.real(v.z);out.number(mesh.vertexMarkers.empty()?0:std::uint32_t(mesh.vertexMarkers[i]),4);}
        for(std::size_t i=0;i<mesh.cells.size();++i){work.tick();const auto &c=mesh.cells[i];out.number(std::uint64_t(externalId(mesh.cellIds,i)),8);out.number(c.count,1);out.number(std::uint32_t(c.region),4);for(int k=0;k<c.count;++k)out.number(c.vertices[k],4);}
        for(std::size_t i=0;i<mesh.edges.size();++i){work.tick();const auto &e=mesh.edges[i];out.number(std::uint64_t(externalId(mesh.edgeIds,i)),8);out.number(e.a,4);out.number(e.b,4);out.number(std::uint32_t(e.marker),4);}
        for(const auto &p:mesh.paths){work.tick();out.number(std::uint64_t(p.id),8);out.number(std::uint32_t(p.marker),4);out.number(std::uint8_t(p.kind),1);out.number(p.closed?1:0,1);out.number(p.vertices.size(),8);for(auto v:p.vertices){work.tick();out.number(v,4);}}
        for(const auto &a:mesh.aliases){work.tick();out.number(a.vertex,4);out.number(std::uint64_t(a.sourceId),8);out.number(std::uint32_t(a.marker),4);}
        out.number(out.hash,8,false);out.flush();work.check();transaction.commit();report.bytes=out.bytes;report.work=work.used;report.status=RunStatus::Complete;report.code="written";report.message="Mesh written atomically.";
    }catch(const Stop &e){setError(report,e);}catch(const std::bad_alloc &e){setException(report,e);}catch(const std::exception &e){setException(report,e);}
    report.seconds=seconds(start);return report;
}
ReadResult readBinary(std::istream &stream,const IOOptions &options){
    using namespace native_detail;ReadResult result;auto &report=result.report;const auto start=std::chrono::steady_clock::now();
    try {Work work(options.limits,options.cancelled);Input in(stream,work);std::array<unsigned char,8> header{};in.raw(header.data(),header.size());if(header!=magic)fail("magic","Not a MeshKit binary file.",RunStatus::Failed);
        if(in.number(4)!=1)fail("version","Unsupported MeshKit binary version.",RunStatus::Failed);const auto flags=in.number(4);if(flags&~1u)fail("flags","Unsupported binary features.",RunStatus::Failed);
        const auto nv=in.number(8),nc=in.number(8),ne=in.number(8),np=in.number(8),nl=in.number(8),na=in.number(8);const auto &l=options.limits;
        if(nv>l.maxVertices || nv>=missingIndex || nc>l.maxCells || ne>l.maxEdges || np>l.maxEdges || nl>l.maxMetadataBytes/4 || na>l.maxVertices)fail("count_limit","Binary record counts exceed configured limits.",RunStatus::ResourceLimit);
        work.memoryFor({{nv,36},{nc,32},{ne,20},{np,64},{na,24},{nl,40},{l.maxMetadataBytes,1}});
        auto &m=result.mesh;m.metadata.unitsKnown=flags&1;m.metadata.horizontalMetresPerUnit=in.real();m.metadata.verticalMetresPerUnit=in.real();auto metadata=l.maxMetadataBytes;
        m.metadata.crs=in.text(metadata);m.metadata.description=in.text(metadata);m.labels.reserve(nl);
        for(std::uint64_t i=0;i<nl;++i){work.tick();const auto id=signed32(in.number(4));m.labels.push_back({id,in.text(metadata)});}
        m.vertices.reserve(nv);m.cells.reserve(nc);m.edges.reserve(ne);m.paths.reserve(np);m.aliases.reserve(na);
        for(std::uint64_t i=0;i<nv;++i){work.tick();++report.record;appendId(m.vertexIds,signed64(in.number(8)),i);Point3 p;p.x=in.real();p.y=in.real();p.z=in.real();m.vertices.push_back(p);appendMarker(m.vertexMarkers,signed32(in.number(4)),i);}
        for(std::uint64_t i=0;i<nc;++i){work.tick();++report.record;appendId(m.cellIds,signed64(in.number(8)),i);Cell cell;cell.count=std::uint8_t(in.number(1));cell.region=signed32(in.number(4));if(cell.count!=3 && cell.count!=4)fail("cell_type","Invalid binary cell type.",RunStatus::Failed);for(int k=0;k<cell.count;++k)cell.vertices[k]=Index(in.number(4));m.cells.push_back(cell);}
        for(std::uint64_t i=0;i<ne;++i){work.tick();++report.record;appendId(m.edgeIds,signed64(in.number(8)),i);Edge edge;edge.a=Index(in.number(4));edge.b=Index(in.number(4));edge.marker=signed32(in.number(4));m.edges.push_back(edge);}
        std::uint64_t connectivity=0;
        for(std::uint64_t i=0;i<np;++i){work.tick();++report.record;Path p;p.id=signed64(in.number(8));p.marker=signed32(in.number(4));p.kind=PathKind(in.number(1));const auto closed=in.number(1);if(closed>1)fail("path_flags","Invalid path flags.",RunStatus::Failed);p.closed=closed;
            const auto count=in.number(8);if(connectivity>l.maxEdges || count>l.maxEdges-connectivity)fail("path_limit","Path index limit exceeded.",RunStatus::ResourceLimit);connectivity+=count;
            work.memoryFor({{nv,36},{nc,32},{ne,20},{np,64},{na,24},{nl,40},{l.maxMetadataBytes,1},{connectivity,4}});p.vertices.reserve(count);for(std::uint64_t k=0;k<count;++k){work.tick();p.vertices.push_back(Index(in.number(4)));}m.paths.push_back(std::move(p));}
        for(std::uint64_t i=0;i<na;++i){work.tick();++report.record;VertexAlias a;a.vertex=Index(in.number(4));a.sourceId=signed64(in.number(8));a.marker=signed32(in.number(4));m.aliases.push_back(a);}
        const auto expected=in.hash;if(in.number(8,false)!=expected)fail("checksum","Binary checksum mismatch.",RunStatus::Failed);unsigned char extra;if(in.byte(extra,false))fail("trailing_data","Unexpected trailing binary data.",RunStatus::Failed);
        validated(m,options);report.bytes=in.bytes;report.work=work.used;report.counts.vertices=m.vertices.size();report.counts.triangles=0;report.counts.quads=0;for(const auto &cell:m.cells)(cell.count==4?report.counts.quads:report.counts.triangles)++;
        report.status=RunStatus::Complete;report.code="read";report.message="Mesh read and validated.";
    }catch(const Stop &e){setError(report,e);result.mesh={};}catch(const std::bad_alloc &e){setException(report,e);result.mesh={};}catch(const std::exception &e){setException(report,e);result.mesh={};}
    report.seconds=seconds(start);return result;
}
ReadResult readBinary(const std::filesystem::path &path,const IOOptions &options){std::ifstream stream(path,std::ios::binary);if(!stream){ReadResult r;r.report.code="open_input";r.report.message="Cannot open input file.";return r;}return readBinary(stream,options);}
}
