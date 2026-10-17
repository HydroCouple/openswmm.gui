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
// Published histories share immutable pages. Only a page being written is
// copied, using spare storage reserved at settings/birth boundaries. A frame
// copy retains two pointers and ring indices, regardless of trail length.
template<class T> class FlowParticleRing {
    static constexpr size_t PageSize=16,Versions=6;
    using Page=std::array<T,PageSize>;
    struct Directory {std::vector<std::shared_ptr<Page>> pages;};
    struct Pool {
        std::vector<std::shared_ptr<Directory>> directories;
        std::vector<std::shared_ptr<Page>> pages;
        explicit Pool(size_t n) {
            pages.reserve(Versions*n);
            for(size_t i=0;i<Versions*n;++i)pages.push_back(std::make_shared<Page>());
            directories.resize(Versions);
            for(auto &d:directories) {d=std::make_shared<Directory>();d->pages.resize(n);}
            for(size_t i=0;i<n;++i)directories[0]->pages[i]=pages[i];
        }
    };
public:
    FlowParticleRing()=default;
    FlowParticleRing(std::initializer_list<T> values) {for(const auto &v:values)push_back(v);}
    FlowParticleRing(const FlowParticleRing &)=default;
    FlowParticleRing &operator=(const FlowParticleRing &)=default;
    FlowParticleRing(FlowParticleRing &&other) noexcept {swap(other);}
    FlowParticleRing &operator=(FlowParticleRing &&other) noexcept {
        if(this==&other)return *this;
        m_pool=std::move(other.m_pool);m_directory=std::move(other.m_directory);
        m_capacity=std::exchange(other.m_capacity,0);m_start=std::exchange(other.m_start,0);m_count=std::exchange(other.m_count,0);
        return *this;
    }
    void reserve(size_t capacity) {
        if(capacity<=m_capacity)return;
        auto pool=std::make_shared<Pool>((capacity+PageSize-1)/PageSize);
        auto directory=pool->directories[0];
        for(size_t i=0;i<m_count;++i)(*directory->pages[i/PageSize])[i%PageSize]=(*this)[i];
        m_pool=std::move(pool);m_directory=std::move(directory);
        m_capacity=m_directory->pages.size()*PageSize;m_start=0;
    }
    size_t size() const {return m_count;}
    size_t capacity() const {return m_capacity;}
    static size_t reservedBytes(size_t capacity) {
        const size_t n=(capacity+PageSize-1)/PageSize;
        // Include shared-pointer/control-block and directory storage, not
        // just samples. Two pointers cover each make_shared control block.
        return Versions*n*(sizeof(Page)+2*sizeof(void*)+sizeof(std::shared_ptr<Page>))
            +Versions*(sizeof(Directory)+2*sizeof(void*)+sizeof(std::shared_ptr<Directory>)
                       +n*sizeof(std::shared_ptr<Page>))+sizeof(Pool)+2*sizeof(void*);
    }
    bool empty() const {return !m_count;}
    const T &operator[](size_t i) const {
        const size_t index=(m_start+i)%m_capacity;
        return (*m_directory->pages[index/PageSize])[index%PageSize];
    }
    const T &front() const {return (*this)[0];}
    const T &back() const {return (*this)[m_count-1];}
    // Explicit writes prevent read-only integration queries from detaching a
    // page merely because the worker's ring itself is mutable.
    T &mutableAt(size_t i) {
        detachDirectory();const size_t index=(m_start+i)%m_capacity,page=index/PageSize;
        auto &data=m_directory->pages[page];
        if(data.use_count()>2) {
            auto free=std::find_if(m_pool->pages.begin(),m_pool->pages.end(),[](const auto &p){return p.use_count()==1;});
            if(free==m_pool->pages.end()) {
                // Extra direct history copies outside the bounded frame pool
                // still obey value semantics. Production needs no overflow.
                m_pool->pages.push_back(std::make_shared<Page>(*data));data=m_pool->pages.back();
            } else {**free=*data;data=*free;}
        }
        return (*data)[index%PageSize];
    }
    void clear() {m_start=0;m_count=0;}
    void pop_front() {if(++m_start==m_capacity)m_start=0;--m_count;}
    void push_back(const T &value) {
        if(m_count==m_capacity) {
            const T copy=value;reserve(std::max(PageSize,2*m_count));mutableAt(m_count)=copy;
        } else mutableAt(m_count)=value;
        ++m_count;
    }
    void swap(FlowParticleRing &other) {
        m_pool.swap(other.m_pool);m_directory.swap(other.m_directory);
        std::swap(m_capacity,other.m_capacity);std::swap(m_start,other.m_start);std::swap(m_count,other.m_count);
    }
    struct Iterator {
        using value_type=T;using difference_type=std::ptrdiff_t;
        using iterator_category=std::forward_iterator_tag;
        using reference=const T &;
        const FlowParticleRing *owner;size_t index;
        reference operator*() const {return (*owner)[index];}
        auto operator->() const {return &(*owner)[index];}
        Iterator &operator++() {++index;return *this;}
        bool operator==(const Iterator &) const=default;
    };
    auto begin() const {return Iterator{this,0};}auto end() const {return Iterator{this,m_count};}
private:
    void detachDirectory() {
        if(m_directory.use_count()==2)return;
        for(auto &d:m_pool->directories)if(d.use_count()==1) {
            // Release old spare-page references before copying the directory.
            std::fill(d->pages.begin(),d->pages.end(),nullptr);
            d->pages=m_directory->pages;m_directory=d;return;
        }
        m_pool->directories.push_back(std::make_shared<Directory>(*m_directory));
        m_directory=m_pool->directories.back();
    }
    std::shared_ptr<Pool> m_pool;
    std::shared_ptr<Directory> m_directory;
    size_t m_capacity=0,m_start=0,m_count=0;
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
    template<class Range> FlowParticleSlots &operator=(const Range &values) {
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
    double age=0,previousAge=0,lifetime=0,stoppedAt=-1;
    double visibleAfterAge=-1; // excludes history made obsolete by a dry update
    FlowParticleStop stop=FlowParticleStop::None;
    bool waitingForData=false,historyLimited=false;
    FlowParticleRing<FlowParticleSample> history;
};
// Numerical work state is private to the worker. Immutable publications need
// only the display header above, never cycle/reduction scratch or pending work.
struct FlowParticleState : FlowParticle {
    double publishedAge=0,previousPublishedAge=0;
    double historyFloorAge=-1; // oldest history lost to the display budget, never an integration limit
    size_t nextHistoryReduction=256; // amortize passes, rather than scanning every tick
    size_t samplesSinceReduction=0;
    bool historyReductionSaturated=false;
    double pending=0; // resumable integration work, never a lifetime limit
    FlowParticleCycles zeroCells; // exact-vertex cycle detection survives work slices
    QPointF crossingAnchor;
};
// Hot position/cell/time planes are contiguous. Histories and infrequent
// crossing/reduction work live in a separate cold plane. References let the
// integrator keep its existing equations without copying a particle header.
template<bool Const> struct FlowParticleReference {
    template<class T> using Value=std::conditional_t<Const,const T,T>;
    Value<quint64> &id;
    Value<QPointF> &position;
    Value<int> &cell,&crossings;
    Value<double> &age,&previousAge,&lifetime,&stoppedAt,&visibleAfterAge;
    Value<FlowParticleStop> &stop;
    Value<bool> &waitingForData,&historyLimited;
    Value<FlowParticleRing<FlowParticleSample>> &history;
    Value<double> &publishedAge,&previousPublishedAge,&historyFloorAge;
    Value<size_t> &nextHistoryReduction,&samplesSinceReduction;
    Value<bool> &historyReductionSaturated;
    Value<double> &pending;
    Value<FlowParticleCycles> &zeroCells;
    Value<QPointF> &crossingAnchor;
    operator FlowParticle() const {
        return {id,position,cell,crossings,age,previousAge,lifetime,stoppedAt,visibleAfterAge,
                stop,waitingForData,historyLimited,history};
    }
};
class FlowParticleStorage {
    struct Cold {
        quint64 id=0;int crossings=0;
        double visibleAfterAge=-1,publishedAge=0,previousPublishedAge=0,historyFloorAge=-1;
        FlowParticleStop stop=FlowParticleStop::None;
        bool waitingForData=false,historyLimited=false,historyReductionSaturated=false;
        size_t nextHistoryReduction=256,samplesSinceReduction=0;
        FlowParticleRing<FlowParticleSample> history;
        FlowParticleCycles zeroCells;
        QPointF crossingAnchor;
    };
    template<class Self> static auto at(Self &self,size_t i) {
        auto &p=self.m_cold[i];
        return FlowParticleReference<std::is_const_v<Self>>{p.id,self.m_position[i],self.m_cell[i],p.crossings,
            self.m_age[i],self.m_previousAge[i],self.m_lifetime[i],self.m_stoppedAt[i],p.visibleAfterAge,
            p.stop,p.waitingForData,p.historyLimited,p.history,p.publishedAge,p.previousPublishedAge,p.historyFloorAge,
            p.nextHistoryReduction,p.samplesSinceReduction,p.historyReductionSaturated,self.m_pending[i],p.zeroCells,p.crossingAnchor};
    }
public:
    size_t size() const {return m_cold.size();}bool empty() const {return m_cold.empty();}
    auto operator[](size_t i) {return at(*this,i);}auto operator[](size_t i) const {return at(*this,i);}
    auto front() const {return (*this)[0];}auto back() const {return (*this)[size()-1];}
    void reserve(size_t n) {
        m_position.reserve(n);m_cell.reserve(n);m_cold.reserve(n);
        for(auto *plane:{&m_age,&m_previousAge,&m_lifetime,&m_stoppedAt,&m_pending})plane->reserve(n);
    }
    void clear() {resize(0);}
    void resize(size_t n) {
        m_position.resize(n);m_cell.resize(n);m_cold.resize(n);
        for(auto *plane:{&m_age,&m_previousAge,&m_lifetime,&m_stoppedAt,&m_pending})plane->resize(n);
    }
    void move(size_t to,size_t from) {
        if(to==from)return;
        m_position[to]=m_position[from];m_cell[to]=m_cell[from];m_cold[to]=std::move(m_cold[from]);
        for(auto *plane:{&m_age,&m_previousAge,&m_lifetime,&m_stoppedAt,&m_pending})(*plane)[to]=(*plane)[from];
    }
    void push_back(FlowParticleState &&p) {
        const size_t i=size();resize(i+1);
        m_position[i]=p.position;m_cell[i]=p.cell;m_age[i]=p.age;m_previousAge[i]=p.previousAge;
        m_lifetime[i]=p.lifetime;m_stoppedAt[i]=p.stoppedAt;m_pending[i]=p.pending;
        m_cold[i]={p.id,p.crossings,p.visibleAfterAge,p.publishedAge,p.previousPublishedAge,p.historyFloorAge,p.stop,
            p.waitingForData,p.historyLimited,p.historyReductionSaturated,p.nextHistoryReduction,p.samplesSinceReduction,
            std::move(p.history),p.zeroCells,p.crossingAnchor};
    }
    template<bool Const> struct Iterator {
        using Owner=std::conditional_t<Const,const FlowParticleStorage,FlowParticleStorage>;
        using value_type=FlowParticle;using difference_type=std::ptrdiff_t;
        using iterator_category=std::forward_iterator_tag;
        Owner *owner;size_t index;
        auto operator*() const {return (*owner)[index];}
        Iterator &operator++() {++index;return *this;}
        bool operator==(const Iterator &) const=default;
    };
    auto begin() {return Iterator<false>{this,0};}auto end() {return Iterator<false>{this,size()};}
    auto begin() const {return Iterator<true>{this,0};}auto end() const {return Iterator<true>{this,size()};}
private:
    std::vector<QPointF> m_position;
    std::vector<int> m_cell;
    std::vector<double> m_age,m_previousAge,m_lifetime,m_stoppedAt,m_pending;
    std::vector<Cold> m_cold;
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
    // Clear hydraulic/time state while retaining bounded reusable storage.
    // Publications held by a display remain immutable.
    void reset();
    bool setSettings(const FlowParticleSettings &);
    bool setField(std::shared_ptr<const FlowParticleSnapshot>,double transitionSeconds=0);
    quint64 spawn(QPointF position,quint64 randomSeed=0,bool publishImmediately=true,int cellHint=-1,double initialAgeFraction=0);
    FlowParticleWork advance(double elapsed,int stepBudget=16384);
    void eraseFinished();
    int releaseOffscreen(const QRectF &view,int requestedSlots);
    // Retire the active particles nearest the end of their assigned lives, so
    // density can fall within one fade. A tick in progress defers to its end.
    int retire(int count);
    bool inTick() const {return m_inTick;}
    void setAppearance(double scale,FlowParticleAppearance appearance) {m_renderScale=scale;m_appearance=std::move(appearance);}
    std::shared_ptr<const FlowParticleFrame> frame() const { return m_frame; }
    const FlowParticleStorage &particles() const { return m_particles; }
    double clock() const {return m_clock;}
    double lagSeconds() const { return m_elapsed; }
    double interpolation() const;
private:
    int locate(QPointF,bool requireWet=true) const;
    bool integrate(FlowParticleReference<false> &,int &budget,int &steps);
    bool reduceHistory(FlowParticleReference<false> &,int &budget,int &steps);
    void stop(FlowParticleReference<false> &,FlowParticleStop);
    std::shared_ptr<FlowParticleFrame> writableFrame(bool excludeCurrent=false);
    void prepareFrame(FlowParticleFrame &);
    void captureParticle(FlowParticleFrame &,size_t index,bool regular);
    void publish(std::shared_ptr<FlowParticleFrame> prepared={});
    void appendHistory(FlowParticleReference<false> &,float speed);
    FlowParticleField fieldAt(int cell,double clock) const;
    std::shared_ptr<const FlowParticleSnapshot> m_field;
    std::shared_ptr<const FlowParticleSnapshot> m_previousField;
    double m_transitionClock=0,m_transitionSeconds=0;
    std::shared_ptr<const FlowParticleFrame> m_frame;
    FlowParticleSettings m_settings;
    FlowParticleAppearance m_appearance;
    double m_renderScale=0;
    FlowParticleStorage m_particles;
    std::vector<FlowParticleRing<FlowParticleSample>> m_freeHistories;
    std::array<std::shared_ptr<FlowParticleFrame>,4> m_frames;
    std::shared_ptr<FlowParticleFrame> m_stagingFrame;
    double m_clock=0,m_elapsed=0,m_tick=0,m_publishedClock=0,m_previousPublishedClock=0;
    size_t m_cursor=0;
    bool m_inTick=false;
    int m_pendingRetire=0;
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
