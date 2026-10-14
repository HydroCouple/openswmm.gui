// GPL-3.0-or-later
#pragma once
#include "render/flowparticlemesh.h"
#include "render/flowparticleribbons.h"
#include <cmath>
#include <algorithm>
#include <iterator>

namespace OpenSWMM::Render {

// Built before publication. Geometry and fields are never borrowed from a
// mutable layer. Triangle diagonals are topology, not physical boundaries.
struct FlowParticleField {
    std::array<QPointF,3> velocity{}; // scene displacement / flow second
    std::array<QPointF,3> edgeCorrection{}; // quadratic, tangent to opposite face
    std::array<float,3> speed{}; // SI magnitude for layer colouring
    std::array<float,3> depth{{1,1,1}};
    std::array<double,3> outwardFlux{{NAN,NAN,NAN}};
    double velocityDepth=1; // converts native velocity back to discharge for time blending
    unsigned char exits=7;
    unsigned char residentEdges=7; // missing regional neighbor waits, never outflow
    bool available=true;
};
struct FlowParticleSnapshot {
    std::shared_ptr<const FlowParticleMesh> mesh;
    std::vector<FlowParticleField> fields;
    quint64 geometry=0,revision=0;
};
enum class FlowParticleStop { None, Lifetime, Dry, Outflow, ClosedFace, InvalidField, VectorConflict };
const char *flowParticleStopName(FlowParticleStop);
struct FlowParticleSample {
    QPointF position; double age=0; int cell=-1; float speed=0;
    double error=-1; // incoming segment's accumulated interpolation error; -1 is an original segment
};
// Reusable ring storage. Reserving happens at settings/birth boundaries;
// front eviction and snapshot reuse never allocate during integration.
template<class T> class FlowParticleRing {
public:
    FlowParticleRing()=default;
    FlowParticleRing(std::initializer_list<T> values) {for(const auto &v:values)push_back(v);}
    FlowParticleRing(const FlowParticleRing &other) {*this=other;}
    FlowParticleRing(FlowParticleRing &&)=default;
    FlowParticleRing &operator=(FlowParticleRing &&)=default;
    FlowParticleRing &operator=(const FlowParticleRing &other) {
        if(this==&other)return *this;
        reserve(other.size());m_start=0;m_count=other.size();
        const size_t first=std::min(m_count,other.m_data.size()-other.m_start);
        std::copy_n(other.m_data.begin()+other.m_start,first,m_data.begin());
        std::copy_n(other.m_data.begin(),m_count-first,m_data.begin()+first);return *this;
    }
    void reserve(size_t capacity) {
        if(capacity<=m_data.size())return;
        std::vector<T> data(capacity);
        for(size_t i=0;i<m_count;++i)data[i]=(*this)[i];
        m_data.swap(data);m_start=0;
    }
    size_t size() const {return m_count;}
    size_t capacity() const {return m_data.size();}
    bool empty() const {return !m_count;}
    T &operator[](size_t i) {const size_t index=m_start+i;return m_data[index<m_data.size()?index:index-m_data.size()];}
    const T &operator[](size_t i) const {const size_t index=m_start+i;return m_data[index<m_data.size()?index:index-m_data.size()];}
    T &front() {return (*this)[0];} const T &front() const {return (*this)[0];}
    T &back() {return (*this)[m_count-1];} const T &back() const {return (*this)[m_count-1];}
    void clear() {m_start=0;m_count=0;}
    void pop_front() {if(++m_start==m_data.size())m_start=0;--m_count;}
    void push_back(const T &value) {
        if(m_count==m_data.size())reserve(std::max(size_t(4),2*m_count));
        (*this)[m_count++]=value;
    }
    void swap(FlowParticleRing &other) {
        m_data.swap(other.m_data);std::swap(m_start,other.m_start);std::swap(m_count,other.m_count);
    }
    template<bool Const> struct Iterator {
        using Owner=std::conditional_t<Const,const FlowParticleRing,FlowParticleRing>;
        using value_type=T;using difference_type=std::ptrdiff_t;
        using iterator_category=std::forward_iterator_tag;
        using reference=std::conditional_t<Const,const T &,T &>;
        Owner *owner;size_t index;
        reference operator*() const {return (*owner)[index];}
        auto operator->() const {return &(*owner)[index];}
        Iterator &operator++() {++index;return *this;}
        bool operator==(const Iterator &) const=default;
    };
    auto begin() {return Iterator<false>{this,0};}auto end() {return Iterator<false>{this,m_count};}
    auto begin() const {return Iterator<true>{this,0};}auto end() const {return Iterator<true>{this,m_count};}
private:
    std::vector<T> m_data;size_t m_start=0,m_count=0;
};
// A snapshot retains spare slots when its population shrinks, so a reused
// frame does not allocate histories again as particles die and are replaced.
template<class T> class FlowParticleSlots {
public:
    auto begin() {return m_data.begin();}auto end() {return m_data.begin()+m_count;}
    auto begin() const {return m_data.begin();}auto end() const {return m_data.begin()+m_count;}
    size_t size() const {return m_count;}bool empty() const {return !m_count;}
    T &operator[](size_t i) {return m_data[i];}const T &operator[](size_t i) const {return m_data[i];}
    T &front() {return m_data[0];}const T &front() const {return m_data[0];}
    T &back() {return m_data[m_count-1];}const T &back() const {return m_data[m_count-1];}
    void reserve(size_t n) {m_data.reserve(n);}
    void prepare(size_t n) {if(n>m_data.size())m_data.resize(n);}
    void resize(size_t n) {prepare(n);m_count=n;}
    void clear() {m_count=0;}
    void push_back(const T &v) {resize(m_count+1);back()=v;}
    void push_back(T &&v) {resize(m_count+1);back()=std::move(v);}
    FlowParticleSlots &operator=(const std::vector<T> &values) {
        resize(values.size());std::copy(values.begin(),values.end(),m_data.begin());return *this;
    }
private:
    std::vector<T> m_data;size_t m_count=0;
};
// Preserve exact detection for common vertex fans, with Brent's bounded
// checkpoint for larger circuits. No heap growth on a pathological crossing.
class FlowParticleCycles {
public:
    bool empty() const {return !m_count;}
    void clear() {m_count=0;m_checkpoint=-1;m_power=1;m_length=0;}
    bool contains(int cell) const {
        return std::find(m_cells.begin(),m_cells.begin()+m_count,cell)!=m_cells.begin()+m_count || cell==m_checkpoint;
    }
    void push_back(int cell) {
        if(m_count<m_cells.size())m_cells[m_count++]=cell;
        else {
            if(m_checkpoint<0 || m_length>=m_power) {
                m_checkpoint=cell;m_length=0;m_power=std::min(quint64(1)<<62,2*m_power);
            }
            ++m_length;
        }
    }
private:
    std::array<int,64> m_cells{};size_t m_count=0;
    int m_checkpoint=-1;quint64 m_power=1,m_length=0;
};
struct FlowParticle {
    quint64 id=0;
    QPointF position;
    int cell=-1,crossings=0;
    double age=0,previousAge=0,lifetime=0,stoppedAt=-1,publishedAge=0,previousPublishedAge=0;
    double visibleAfterAge=-1; // excludes history made obsolete by a dry update
    FlowParticleStop stop=FlowParticleStop::None;
    bool waitingForData=false,historyLimited=false;
    double historyFloorAge=-1; // oldest history lost to the display budget, never an integration limit
    size_t nextHistoryReduction=256; // amortize passes, rather than scanning every tick
    size_t samplesSinceReduction=0;
    bool historyReductionSaturated=false;
    FlowParticleRing<FlowParticleSample> history;
    double pending=0; // resumable integration work, never a lifetime limit
    FlowParticleCycles zeroCells; // exact-vertex cycle detection survives work slices
    QPointF crossingAnchor;
};
struct FlowParticleSettings {
    double timestep=1./60.,speed=1,minimumLifetime=20,maximumLifetime=40;
    double referenceSpeed=0,speedExponent=1; // optional display compression; physical fields stay unchanged
    double trailSeconds=1,fadeSeconds=.5,cutoff=.001;
    double publicationInterval=0; // zero publishes every numerical tick
    double historyTolerance=.001; // scene units; native layer derives this from a quarter pixel
    int maxParticles=2000,maxHistory=512;
};
struct FlowParticleFrame {
    double clock=0,previousClock=0,timestep=1./60.;
    double publicationMs=0;
    quint64 geometry=0,revision=0,births=0,deaths=0,unresolvedOrbits=0;
    quint64 deferredPublications=0;
    std::array<quint64,7> stopCounts{};
    FlowParticleSlots<FlowParticle> particles;
    FlowParticleRibbon ribbon;
};
struct FlowParticleWork {
    int steps=0,ticks=0;
    bool pending=false;
};

// Single-owner integration core. A worker owns this object; the renderer sees
// only completed immutable frames. No mesh-wide operation occurs on a tick.
class FlowParticleSystem {
public:
    FlowParticleSystem();
    bool setSettings(const FlowParticleSettings &);
    bool setField(std::shared_ptr<const FlowParticleSnapshot>,double transitionSeconds=0);
    quint64 spawn(QPointF position,quint64 randomSeed=0,bool publishImmediately=true,int cellHint=-1);
    FlowParticleWork advance(double elapsed,int stepBudget=16384);
    void eraseFinished();
    void setAppearance(double scale,FlowParticleAppearance appearance) {m_renderScale=scale;m_appearance=std::move(appearance);}
    std::shared_ptr<const FlowParticleFrame> frame() const { return m_frame; }
    const std::vector<FlowParticle> &particles() const { return m_particles; }
    double lagSeconds() const { return m_elapsed; }
    double interpolation() const;
private:
    int locate(QPointF,bool requireWet=true) const;
    bool integrate(FlowParticle &,int &budget,int &steps);
    bool reduceHistory(FlowParticle &,int &budget,int &steps);
    void stop(FlowParticle &,FlowParticleStop);
    void publish();
    void appendHistory(FlowParticle &,float speed);
    FlowParticleField fieldAt(int cell,double clock) const;
    std::shared_ptr<const FlowParticleSnapshot> m_field;
    std::shared_ptr<const FlowParticleSnapshot> m_previousField;
    double m_transitionClock=0,m_transitionSeconds=0;
    std::shared_ptr<const FlowParticleFrame> m_frame;
    FlowParticleSettings m_settings;
    FlowParticleAppearance m_appearance;
    double m_renderScale=0;
    std::vector<FlowParticle> m_particles,m_freeParticles;
    std::array<std::shared_ptr<FlowParticleFrame>,4> m_frames;
    double m_clock=0,m_elapsed=0,m_tick=0,m_publishedClock=0,m_previousPublishedClock=0;
    size_t m_cursor=0;
    bool m_inTick=false;
    // Only the current particle can have a pending reduction. Publication
    // keeps the original history until the linear pass is complete.
    FlowParticleRing<FlowParticleSample> m_reducedHistory;
    size_t m_historyIndex=0;
    double m_historyError=-1;
    quint64 m_nextId=1,m_births=0,m_deaths=0,m_unresolvedOrbits=0;
    std::array<quint64,7> m_stopCounts{};
    quint64 m_deferredPublications=0;
};
} // namespace OpenSWMM::Render
