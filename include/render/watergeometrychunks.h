// GPL-3.0-or-later
#ifndef OPENSWMM_RENDER_WATERGEOMETRYCHUNKS_H
#define OPENSWMM_RENDER_WATERGEOMETRYCHUNKS_H

#include <QtGlobal>
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

namespace OpenSWMM::Render {

constexpr int waterGeometryChunkLimit=65532;

template<class Vertex> struct WaterGeometryChunk {
    std::vector<Vertex> vertices;
    std::vector<quint16> indices;
};

// One bounded chunk of exact 12-byte attributes. The sink consumes each
// chunk immediately, either uploading it or retaining an immutable packet.
template<class Vertex> class WaterGeometryBuilder {
public:
    using Sink=std::function<void(const std::vector<Vertex>&,const std::vector<quint16>&)>;
    explicit WaterGeometryBuilder(Sink sink):sink_(std::move(sink)) {static_assert(sizeof(Vertex)==12);}
    void reserve(size_t count) {
        const size_t limit=std::min(count,size_t(waterGeometryChunkLimit));
        vertices_.reserve(limit);indices_.reserve(limit);
    }
    void push_back(const Vertex& vertex) {
        if(table_.empty())table_.resize(size_t(1)<<17);
        std::array<quint32,3> key;
        std::memcpy(key.data(),&vertex,sizeof(vertex));
        quint32 hash=2166136261u;
        for(quint32 word:key)hash=(hash^word)*16777619u;
        hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;hash*=0x846ca68bu;hash^=hash>>16;
        size_t slot=hash&(table_.size()-1);
        while(table_[slot].generation==generation_ && table_[slot].key!=key)
            slot=(slot+1)&(table_.size()-1);
        auto& entry=table_[slot];
        if(entry.generation!=generation_) {
            entry.key=key;entry.generation=generation_;entry.index=quint16(vertices_.size());
            vertices_.push_back(vertex);
        }
        indices_.push_back(entry.index);++references_;
        if(indices_.size()==size_t(waterGeometryChunkLimit))flush();
    }
    void finish() {if(!started_ || !indices_.empty())flush();}
    [[nodiscard]] size_t size() const {return uploadedVertices_;}
    [[nodiscard]] size_t bytes() const {return uploadedBytes_;}
    [[nodiscard]] bool empty() const {return references_==0;}
private:
    struct Entry {std::array<quint32,3> key{};quint32 generation=0;quint16 index=0;};
    void flush() {
        sink_(vertices_,indices_);started_=true;
        uploadedVertices_+=vertices_.size();
        uploadedBytes_+=vertices_.size()*sizeof(Vertex)+indices_.size()*sizeof(quint16);
        vertices_.clear();indices_.clear();
        if(++generation_==0) {for(auto& entry:table_)entry.generation=0;generation_=1;}
    }
    Sink sink_;
    std::vector<Vertex> vertices_;
    std::vector<quint16> indices_;
    std::vector<Entry> table_;
    size_t references_=0,uploadedVertices_=0,uploadedBytes_=0;
    quint32 generation_=1;
    bool started_=false;
};
} // namespace OpenSWMM::Render
#endif
