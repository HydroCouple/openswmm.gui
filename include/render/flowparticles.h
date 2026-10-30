// GPL-3.0-or-later

/**
 * @file      flowparticles.h
 * @brief     Flow particle state, stepping, and display parameters.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "render/flowparticlemesh.h"
#include "render/flowparticleribbons.h"
#include <cmath>
#include <algorithm>
#include <iterator>

namespace OpenSWMM::Render
{

// Built before publication. Geometry and fields are never borrowed from a
// mutable layer. Triangle diagonals are topology, not physical boundaries.
struct FlowParticleField {
    std::array<QPointF, 3> velocity{};       // scene displacement / flow second
    std::array<QPointF, 3> edgeCorrection{}; // quadratic, tangent to opposite face
    std::array<float, 3> speed{};            // SI magnitude for layer colouring
    std::array<float, 3> depth{{1, 1, 1}};
    std::array<double, 3> outwardFlux{{NAN, NAN, NAN}};
    double velocityDepth = 1; // converts native velocity back to discharge for time blending
    unsigned char exits = 7;
    unsigned char residentEdges = 7; // missing regional neighbor waits, never outflow
    bool available = true;
};
struct FlowParticleSnapshot {
    std::shared_ptr<const FlowParticleMesh> mesh;
    std::vector<FlowParticleField> fields;
    quint64 geometry = 0, revision = 0;
};
enum class FlowParticleStop {
    None,
    Lifetime,
    Dry,
    Outflow,
    ClosedFace,
    InvalidField,
    VectorConflict
};
/** @brief Map a stop reason to a stable English diagnostic label.
 * @param reason Reason enum; unknown integers map to unknown.
 * @return Borrowed static string literal, never null; caller must not free it. */
const char *flowParticleStopName(FlowParticleStop reason);
struct FlowParticleSample {
    QPointF position;
    double age = 0;
    int cell = -1;
    float speed = 0;
    double error =
        -1; // incoming segment's accumulated interpolation error; -1 is an original segment
};
// Published histories share immutable pages. Only a page being written is
// copied, using spare storage reserved at settings/birth boundaries. A frame
// copy retains two pointers and ring indices, regardless of trail length.
/** @brief Share paged history snapshots while detaching explicit writes.
 * @tparam T Default-constructible, copy-assignable element. One writer owns any
 * shared pool; published copies expose only const reads. Bounds are unchecked,
 * capacity can grow, and retained pages are not element-destroyed on clear(). */
template <class T> class FlowParticleRing
{
    static constexpr size_t PageSize = 16, Versions = 6;
    using Page = std::array<T, PageSize>;
    struct Directory {
        std::vector<std::shared_ptr<Page>> pages;
    };
    struct Pool {
        std::vector<std::shared_ptr<Directory>> directories;
        std::vector<std::shared_ptr<Page>> pages;
        /** @brief Allocate six reusable versions of each history page and directory.
         * @param n Number of sixteen-element pages per directory; arithmetic/allocation
         * sizes must fit size_t. Only the first directory is initially populated.
         * T must be default-constructible and copy-assignable; allocation errors propagate. */
        explicit Pool(size_t n)
        {
            pages.reserve(Versions * n);
            for (size_t i = 0; i < Versions * n; ++i)
                pages.push_back(std::make_shared<Page>());
            directories.resize(Versions);
            for (auto &d : directories) {
                d = std::make_shared<Directory>();
                d->pages.resize(n);
            }
            for (size_t i = 0; i < n; ++i)
                directories[0]->pages[i] = pages[i];
        }
    };

public:
    /** @brief Construct an empty ring with no allocated directory or capacity. */
    FlowParticleRing() = default;
    /** @brief Copy initial values in order using normal append/growth behavior.
     * @param values Borrowed initializer values copied synchronously; allocation/copy
     * exceptions propagate. No bounded maximum capacity is imposed by this container. */
    FlowParticleRing(std::initializer_list<T> values)
    {
        for (const auto &v : values)
            push_back(v);
    }
    /** @brief Copy ring indices while sharing immutable page storage and spare pools.
     * @param other Source ring whose directory/pages remain shared until an explicit
     * write detaches them. Use one writer per shared pool; readers only use const access. */
    FlowParticleRing(const FlowParticleRing &other) = default;
    /** @brief Replace this ring with a shared-storage value copy.
     * @param other Source ring; self-assignment is supported.
     * @return This ring. Previously borrowed element references may cease to belong to it. */
    FlowParticleRing &operator=(const FlowParticleRing &other) = default;
    /** @brief Transfer storage and indices by swapping with an initially empty ring.
     * @param other Source left empty with zero capacity/count. */
    FlowParticleRing(FlowParticleRing &&other) noexcept { swap(other); }
    /** @brief Replace storage by moving pointers and exchanging source indices with zero.
     * @param other Source left empty; self-move is an explicit no-op.
     * @return This ring; previous borrowed elements/iterators are invalidated. */
    FlowParticleRing &operator=(FlowParticleRing &&other) noexcept
    {
        if (this == &other)
            return *this;
        m_pool = std::move(other.m_pool);
        m_directory = std::move(other.m_directory);
        m_capacity = std::exchange(other.m_capacity, 0);
        m_start = std::exchange(other.m_start, 0);
        m_count = std::exchange(other.m_count, 0);
        return *this;
    }
    /** @brief Grow to a page-rounded capacity and copy current values into logical order.
     * @param capacity Requested minimum element capacity, rounded to a multiple of sixteen.
     * No-op if already sufficient; caller avoids size_t rounding/doubling overflow.
     * New storage owns six spare versions per page. Allocation/assignment failures
     * propagate before pointer replacement, preserving the old ring. Growth invalidates
     * borrowed references/iterators; logical size and element order are retained. */
    void reserve(size_t capacity)
    {
        if (capacity <= m_capacity)
            return;
        auto pool = std::make_shared<Pool>((capacity + PageSize - 1) / PageSize);
        auto directory = pool->directories[0];
        for (size_t i = 0; i < m_count; ++i)
            (*directory->pages[i / PageSize])[i % PageSize] = (*this)[i];
        m_pool = std::move(pool);
        m_directory = std::move(directory);
        m_capacity = m_directory->pages.size() * PageSize;
        m_start = 0;
    }
    /** @brief Count logical live elements.
     * @return Current count, independent of retained capacity/pages. */
    size_t size() const { return m_count; }
    /** @brief Inspect the allocated logical element capacity.
     * @return Page-rounded capacity, initially zero; may exceed the last reserve request. */
    size_t capacity() const { return m_capacity; }
    /** @brief Estimate storage for this ring's six-version page/directory pool.
     * @param capacity Nominal element capacity; rounded to sixteen-element pages,
     * with arithmetic required to fit size_t.
     * @return Estimated bytes including pointer/control-block approximations, not
     * measured allocator usage. Extra overflow versions from retained copies are excluded. */
    static size_t reservedBytes(size_t capacity)
    {
        const size_t n = (capacity + PageSize - 1) / PageSize;
        // Include shared-pointer/control-block and directory storage, not
        // just samples. Two pointers cover each make_shared control block.
        return Versions * n * (sizeof(Page) + 2 * sizeof(void *) + sizeof(std::shared_ptr<Page>)) +
               Versions * (sizeof(Directory) + 2 * sizeof(void *) +
                           sizeof(std::shared_ptr<Directory>) + n * sizeof(std::shared_ptr<Page>)) +
               sizeof(Pool) + 2 * sizeof(void *);
    }
    /** @brief Test the logical count only.
     * @return True after construction/clear() or removal of the final element. */
    bool empty() const { return !m_count; }
    /** @brief Borrow an element by logical index without detaching shared storage.
     * @param i Unchecked index in [0,size()); nonzero capacity/directory required.
     * @return Const element reference, invalidated by growth/replacement or writes to
     * its page. Do not hold references across mutations of this ring. */
    const T &operator[](size_t i) const
    {
        const size_t index = (m_start + i) % m_capacity;
        return (*m_directory->pages[index / PageSize])[index % PageSize];
    }
    /** @brief Borrow the first logical element.
     * @return operator[](0); ring must be nonempty, with no bounds checking. */
    const T &front() const { return (*this)[0]; }
    /** @brief Borrow the final logical element.
     * @return operator[](size()-1); ring must be nonempty, with no bounds checking. */
    const T &back() const { return (*this)[m_count - 1]; }
    // Explicit writes prevent read-only integration queries from detaching a
    // page merely because the worker's ring itself is mutable.
    /** @brief Detach shared directory/page as needed and borrow a writable slot.
     * @param i Unchecked logical slot below capacity(), normally below size(); append
     * also uses index==size() after reserving. Requires an allocated pool/directory.
     * @return Writable element. Spare versions are reused when available; retaining
     * more snapshots than the pool permits grows overflow storage. Allocation/copy
     * errors propagate. Do not retain this reference across copies, mutations or
     * publication: later writes through an old reference bypass copy-on-write isolation.
     * Shared pools support one writer; this method provides no thread synchronization. */
    T &mutableAt(size_t i)
    {
        detachDirectory();
        const size_t index = (m_start + i) % m_capacity, page = index / PageSize;
        auto &data = m_directory->pages[page];
        if (data.use_count() > 2) {
            auto free = std::find_if(m_pool->pages.begin(), m_pool->pages.end(),
                                     [](const auto &p) { return p.use_count() == 1; });
            if (free == m_pool->pages.end()) {
                // Extra direct history copies outside the bounded frame pool
                // still obey value semantics. Production needs no overflow.
                m_pool->pages.push_back(std::make_shared<Page>(*data));
                data = m_pool->pages.back();
            } else {
                **free = *data;
                data = *free;
            }
        }
        return (*data)[index % PageSize];
    }
    /** @brief Set logical start/count to zero while retaining pages, values and spare capacity.
     * Does not destroy retained T elements or change other shared ring copies. */
    void clear()
    {
        m_start = 0;
        m_count = 0;
    }
    /** @brief Advance the logical start and remove one counted element.
     * Requires a nonempty ring; no guard prevents underflow on empty input.
     * Retains underlying values/storage and does not mutate other shared copies. */
    void pop_front()
    {
        if (++m_start == m_capacity)
            m_start = 0;
        --m_count;
    }
    /** @brief Copy-append one element, growing by at least a page when full.
     * @param value Value copied to the new logical tail; when growth is needed an
     * intermediate copy protects aliases into this ring. Copy/assignment/allocation
     * errors propagate; size increments only after assignment, but successful growth
     * may already have replaced storage. No eviction or fixed capacity limit occurs. */
    void push_back(const T &value)
    {
        if (m_count == m_capacity) {
            const T copy = value;
            reserve(std::max(PageSize, 2 * m_count));
            mutableAt(m_count) = copy;
        } else
            mutableAt(m_count) = value;
        ++m_count;
    }
    /** @brief Exchange shared pointers and all logical indices/capacities.
     * @param other Peer ring; self-swap is harmless. Element storage is not copied;
     * borrowed references stay with their underlying storage, now owned by the peer. */
    void swap(FlowParticleRing &other)
    {
        m_pool.swap(other.m_pool);
        m_directory.swap(other.m_directory);
        std::swap(m_capacity, other.m_capacity);
        std::swap(m_start, other.m_start);
        std::swap(m_count, other.m_count);
    }
    struct Iterator {
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;
        using reference = const T &;
        const FlowParticleRing *owner;
        size_t index;
        /** @brief Borrow the current logical element without detaching.
         * @return Const reference; owner must live and index must be below its size(). */
        reference operator*() const { return (*owner)[index]; }
        /** @brief Address the current logical element without detaching.
         * @return Borrowed pointer; requires a valid non-end iterator and live owner. */
        auto operator->() const { return &(*owner)[index]; }
        /** @brief Advance the logical index by one without a bounds check.
         * @return This iterator; callers stop at end() before dereferencing. */
        Iterator &operator++()
        {
            ++index;
            return *this;
        }
        /** @brief Compare iterator owner and logical index exactly.
         * @param other Peer iterator, including an end sentinel.
         * @return True only when both stored fields match; owner is not dereferenced. */
        bool operator==(const Iterator &other) const = default;
    };
    /** @brief Construct a const forward iterator at logical index zero.
     * @return Borrowed iterator requiring this ring to outlive iteration. */
    auto begin() const { return Iterator{this, 0}; }
    /** @brief Construct the one-past-logical-end iterator.
     * @return Sentinel at size(); do not dereference. Mutations can change the logical range. */
    auto end() const { return Iterator{this, m_count}; }

private:
    /** @brief Acquire a directory version privately writable by this ring.
     * Requires an allocated pool/directory and exclusive writer ownership of the pool.
     * Reuses spare directories after releasing page references, otherwise copies a new
     * one. Does not itself detach individual pages; allocation errors propagate. */
    void detachDirectory()
    {
        if (m_directory.use_count() == 2)
            return;
        for (auto &d : m_pool->directories)
            if (d.use_count() == 1) {
                // Release old spare-page references before copying the directory.
                std::fill(d->pages.begin(), d->pages.end(), nullptr);
                d->pages = m_directory->pages;
                m_directory = d;
                return;
            }
        m_pool->directories.push_back(std::make_shared<Directory>(*m_directory));
        m_directory = m_pool->directories.back();
    }
    std::shared_ptr<Pool> m_pool;
    std::shared_ptr<Directory> m_directory;
    size_t m_capacity = 0, m_start = 0, m_count = 0;
};
// A snapshot retains spare slots when its population shrinks, so a reused
// frame does not allocate histories again as particles die and are replaced.
/** @brief Keep an active prefix of reusable prepared objects without destroying spare slots.
 * @tparam T Vector-compatible default-constructible assignable type. This container
 * is single-owner, has unchecked indexing, and provides no thread synchronization. */
template <class T> class FlowParticleSlots
{
public:
    /** @brief Borrow a mutable iterator to the first active slot.
     * @return Vector iterator; reserve/prepare growth can invalidate it. */
    auto begin() { return m_data.begin(); }
    /** @brief Borrow the mutable one-past-active-end iterator.
     * @return begin()+size(); prepared spare slots are excluded. */
    auto end() { return m_data.begin() + m_count; }
    /** @brief Borrow a const iterator to the first active slot.
     * @return Vector iterator; owner must remain alive and unchanged during iteration. */
    auto begin() const { return m_data.begin(); }
    /** @brief Borrow the const one-past-active-end iterator.
     * @return begin()+size(); prepared spare slots are excluded. */
    auto end() const { return m_data.begin() + m_count; }
    /** @brief Count active slots, excluding retained spare objects.
     * @return Logical count, initially zero. */
    size_t size() const { return m_count; }
    /** @brief Test active count without releasing spare objects.
     * @return True when size()==0. */
    bool empty() const { return !m_count; }
    /** @brief Borrow a mutable active slot without bounds checking.
     * @param i Index below size().
     * @return Reference invalidated by vector growth/replacement or object destruction. */
    T &operator[](size_t i) { return m_data[i]; }
    /** @brief Borrow a const active slot without bounds checking.
     * @param i Index below size().
     * @return Reference invalidated by vector growth/replacement or object destruction. */
    const T &operator[](size_t i) const { return m_data[i]; }
    /** @brief Borrow the first mutable active slot.
     * @return Slot zero; requires a nonempty container. */
    T &front() { return m_data[0]; }
    /** @brief Borrow the first const active slot.
     * @return Slot zero; requires a nonempty container. */
    const T &front() const { return m_data[0]; }
    /** @brief Borrow the final mutable active slot.
     * @return Slot size()-1; requires a nonempty container. */
    T &back() { return m_data[m_count - 1]; }
    /** @brief Borrow the final const active slot.
     * @return Slot size()-1; requires a nonempty container. */
    const T &back() const { return m_data[m_count - 1]; }
    /** @brief Reserve underlying vector capacity without constructing/activating slots.
     * @param n Minimum capacity; growth can invalidate all borrowed references/iterators. */
    void reserve(size_t n) { m_data.reserve(n); }
    /** @brief Default-construct enough retained slots without changing the active count.
     * @param n Minimum constructed-slot count; never shrinks the underlying vector.
     * Allocation/default-construction failures propagate with normal vector guarantees. */
    void prepare(size_t n)
    {
        if (n > m_data.size())
            m_data.resize(n);
    }
    /** @brief Prepare and select a new active prefix, retaining hidden spare values.
     * @param n New active count. Shrink does not destroy/reset removed objects; growing
     * within prepared storage reactivates their previous values, not fresh defaults. */
    void resize(size_t n)
    {
        prepare(n);
        m_count = n;
    }
    /** @brief Set active count to zero, retaining every prepared object and allocation. */
    void clear() { m_count = 0; }
    /** @brief Activate the next slot and copy-assign an element into it.
     * @param v Source, which must not alias an element invalidated by vector growth.
     * Assignment exceptions can leave the active count already increased. */
    void push_back(const T &v)
    {
        resize(m_count + 1);
        back() = v;
    }
    /** @brief Activate the next slot and move-assign an element into it.
     * @param v Source moved from; must not alias an element invalidated by growth.
     * Assignment exceptions can leave the active count already increased. */
    void push_back(T &&v)
    {
        resize(m_count + 1);
        back() = std::move(v);
    }
    /** @brief Replace the active prefix by copying a sized range.
     * @tparam Range Supplies size(), begin(), end() with matching element count.
     * @param values Borrowed range; avoid aliases invalidated/overwritten by resizing/copy.
     * @return This container. Spare objects beyond the new count remain retained;
     * copy exceptions can leave a partially replaced active prefix. */
    template <class Range> FlowParticleSlots &operator=(const Range &values)
    {
        resize(values.size());
        std::copy(values.begin(), values.end(), m_data.begin());
        return *this;
    }

private:
    std::vector<T> m_data;
    size_t m_count = 0;
};
// Preserve exact detection for common vertex fans, with Brent's bounded
// checkpoint for larger circuits. No heap growth on a pathological crossing.
class FlowParticleCycles
{
public:
    /** @brief Test whether any exact-history cell has been stored.
     * @return True initially/after clear(); overflow checkpoint state is reset with it. */
    bool empty() const { return !m_count; }
    /** @brief Reset the 64-cell exact prefix and bounded Brent-style checkpoint counters. */
    void clear()
    {
        m_count = 0;
        m_checkpoint = -1;
        m_power = 1;
        m_length = 0;
    }
    /** @brief Test the exact retained prefix and current overflow checkpoint.
     * @param cell Cell ID to compare. Production IDs are nonnegative; -1 collides
     * with the initial unused checkpoint sentinel and must not be queried as a real cell.
     * @return True for a stored prefix/checkpoint match. This is bounded cycle detection,
     * not complete membership of every cell visited after the first sixty-four. */
    bool contains(int cell) const
    {
        return std::find(m_cells.begin(), m_cells.begin() + m_count, cell) !=
                   m_cells.begin() + m_count ||
               cell == m_checkpoint;
    }
    /** @brief Record a visited cell without allocating unbounded history.
     * @param cell Nonnegative cell ID. First sixty-four entries remain exact; later
     * entries periodically replace a checkpoint with exponentially growing intervals. */
    void push_back(int cell)
    {
        if (m_count < m_cells.size())
            m_cells[m_count++] = cell;
        else {
            if (m_checkpoint < 0 || m_length >= m_power) {
                m_checkpoint = cell;
                m_length = 0;
                m_power = std::min(quint64(1) << 62, 2 * m_power);
            }
            ++m_length;
        }
    }

private:
    std::array<int, 64> m_cells{};
    size_t m_count = 0;
    int m_checkpoint = -1;
    quint64 m_power = 1, m_length = 0;
};
struct FlowParticle {
    quint64 id = 0;
    QPointF position;
    int cell = -1, crossings = 0;
    double age = 0, previousAge = 0, lifetime = 0, stoppedAt = -1;
    double visibleAfterAge = -1; // excludes history made obsolete by a dry update
    FlowParticleStop stop = FlowParticleStop::None;
    bool waitingForData = false, historyLimited = false;
    FlowParticleRing<FlowParticleSample> history;
};
// Numerical work state is private to the worker. Immutable publications need
// only the display header above, never cycle/reduction scratch or pending work.
struct FlowParticleState : FlowParticle {
    double publishedAge = 0, previousPublishedAge = 0;
    double historyFloorAge =
        -1; // oldest history lost to the display budget, never an integration limit
    size_t nextHistoryReduction = 256; // amortize passes, rather than scanning every tick
    size_t samplesSinceReduction = 0;
    bool historyReductionSaturated = false;
    double pending = 0;           // resumable integration work, never a lifetime limit
    FlowParticleCycles zeroCells; // exact-vertex cycle detection survives work slices
    QPointF crossingAnchor;
};
// Hot position/cell/time planes are contiguous. Histories and infrequent
// crossing/reduction work live in a separate cold plane. References let the
// integrator keep its existing equations without copying a particle header.
template <bool Const> struct FlowParticleReference {
    template <class T> using Value = std::conditional_t<Const, const T, T>;
    Value<quint64> &id;
    Value<QPointF> &position;
    Value<int> &cell, &crossings;
    Value<double> &age, &previousAge, &lifetime, &stoppedAt, &visibleAfterAge;
    Value<FlowParticleStop> &stop;
    Value<bool> &waitingForData, &historyLimited;
    Value<FlowParticleRing<FlowParticleSample>> &history;
    Value<double> &publishedAge, &previousPublishedAge, &historyFloorAge;
    Value<size_t> &nextHistoryReduction, &samplesSinceReduction;
    Value<bool> &historyReductionSaturated;
    Value<double> &pending;
    Value<FlowParticleCycles> &zeroCells;
    Value<QPointF> &crossingAnchor;
    /** @brief Copy the public display header while sharing immutable history pages.
     * @return Independent FlowParticle value excluding numerical scratch, pending work
     * and reduction/cycle state. Referenced storage must remain valid during the copy. */
    operator FlowParticle() const
    {
        return {id,
                position,
                cell,
                crossings,
                age,
                previousAge,
                lifetime,
                stoppedAt,
                visibleAfterAge,
                stop,
                waitingForData,
                historyLimited,
                history};
    }
};
/** @brief Store worker particles in aligned hot/cold planes with borrowed reference proxies.
 * One worker owns all access. Bounds are unchecked and mutations can invalidate
 * proxies/iterators; this storage is not an immutable published-frame container. */
class FlowParticleStorage
{
    struct Cold {
        quint64 id = 0;
        int crossings = 0;
        double visibleAfterAge = -1, publishedAge = 0, previousPublishedAge = 0,
               historyFloorAge = -1;
        FlowParticleStop stop = FlowParticleStop::None;
        bool waitingForData = false, historyLimited = false, historyReductionSaturated = false;
        size_t nextHistoryReduction = 256, samplesSinceReduction = 0;
        FlowParticleRing<FlowParticleSample> history;
        FlowParticleCycles zeroCells;
        QPointF crossingAnchor;
    };
    /** @brief Assemble a reference proxy over aligned hot/cold storage planes.
     * @tparam Self FlowParticleStorage or const FlowParticleStorage.
     * @param self Storage with equal plane lengths.
     * @param i Unchecked in-range element index.
     * @return Proxy borrowing every plane; cannot outlive storage/reallocation/shrink. */
    template <class Self> static auto at(Self &self, size_t i)
    {
        auto &p = self.m_cold[i];
        return FlowParticleReference<std::is_const_v<Self>>{p.id,
                                                            self.m_position[i],
                                                            self.m_cell[i],
                                                            p.crossings,
                                                            self.m_age[i],
                                                            self.m_previousAge[i],
                                                            self.m_lifetime[i],
                                                            self.m_stoppedAt[i],
                                                            p.visibleAfterAge,
                                                            p.stop,
                                                            p.waitingForData,
                                                            p.historyLimited,
                                                            p.history,
                                                            p.publishedAge,
                                                            p.previousPublishedAge,
                                                            p.historyFloorAge,
                                                            p.nextHistoryReduction,
                                                            p.samplesSinceReduction,
                                                            p.historyReductionSaturated,
                                                            self.m_pending[i],
                                                            p.zeroCells,
                                                            p.crossingAnchor};
    }

public:
    /** @brief Count particles using the cold-plane length.
     * @return Logical particle count; planes are kept aligned by successful mutations. */
    size_t size() const { return m_cold.size(); }
    /** @brief Test whether the cold plane has any particles.
     * @return True when size()==0. */
    bool empty() const { return m_cold.empty(); }
    /** @brief Borrow a mutable hot/cold reference proxy without bounds checking.
     * @param i Index below size().
     * @return Proxy invalidated by growth, resize/removal or destruction; writes change storage. */
    auto operator[](size_t i) { return at(*this, i); }
    /** @brief Borrow a const hot/cold reference proxy without bounds checking.
     * @param i Index below size().
     * @return Proxy invalidated by growth, resize/removal or destruction. */
    auto operator[](size_t i) const { return at(*this, i); }
    /** @brief Borrow a const proxy to particle zero.
     * @return First particle proxy; requires a nonempty storage. */
    auto front() const { return (*this)[0]; }
    /** @brief Borrow a const proxy to the final particle.
     * @return Last particle proxy; requires a nonempty storage. */
    auto back() const { return (*this)[size() - 1]; }
    /** @brief Reserve every hot/cold plane without changing their sizes.
     * @param n Minimum particle capacity. Allocations occur sequentially and may
     * invalidate proxies; an exception can leave differing capacities but unchanged lengths. */
    void reserve(size_t n)
    {
        m_position.reserve(n);
        m_cell.reserve(n);
        m_cold.reserve(n);
        for (auto *plane : {&m_age, &m_previousAge, &m_lifetime, &m_stoppedAt, &m_pending})
            plane->reserve(n);
    }
    /** @brief Resize all planes to zero, destroying particle headers/histories while retaining capacity. */
    void clear() { resize(0); }
    /** @brief Resize each plane to a common particle count.
     * @param n New count; grown values use each plane's value/default initialization.
     * Operations are sequential: allocation/construction exceptions can leave unequal
     * plane lengths, so caller must not continue normal access without restoring invariants. */
    void resize(size_t n)
    {
        m_position.resize(n);
        m_cell.resize(n);
        m_cold.resize(n);
        for (auto *plane : {&m_age, &m_previousAge, &m_lifetime, &m_stoppedAt, &m_pending})
            plane->resize(n);
    }
    /** @brief Move a particle into an existing slot during stable compaction.
     * @param to In-range destination slot.
     * @param from In-range source slot; equality is a no-op. Cold state/history is
     * moved, hot scalars copied; source slot remains but can contain moved-from cold state.
     * Does not resize or publish storage. */
    void move(size_t to, size_t from)
    {
        if (to == from)
            return;
        m_position[to] = m_position[from];
        m_cell[to] = m_cell[from];
        m_cold[to] = std::move(m_cold[from]);
        for (auto *plane : {&m_age, &m_previousAge, &m_lifetime, &m_stoppedAt, &m_pending})
            (*plane)[to] = (*plane)[from];
    }
    /** @brief Append a worker particle by splitting its header and scratch across planes.
     * @param p State whose history is moved; other scalar/coordinate fields are copied.
     * Source must not depend on proxies invalidated by resize. Allocation exceptions
     * can leave partial plane mutations; publication/transaction ownership stays with caller. */
    void push_back(FlowParticleState &&p)
    {
        const size_t i = size();
        resize(i + 1);
        m_position[i] = p.position;
        m_cell[i] = p.cell;
        m_age[i] = p.age;
        m_previousAge[i] = p.previousAge;
        m_lifetime[i] = p.lifetime;
        m_stoppedAt[i] = p.stoppedAt;
        m_pending[i] = p.pending;
        m_cold[i] = {p.id,
                     p.crossings,
                     p.visibleAfterAge,
                     p.publishedAge,
                     p.previousPublishedAge,
                     p.historyFloorAge,
                     p.stop,
                     p.waitingForData,
                     p.historyLimited,
                     p.historyReductionSaturated,
                     p.nextHistoryReduction,
                     p.samplesSinceReduction,
                     std::move(p.history),
                     p.zeroCells,
                     p.crossingAnchor};
    }
    template <bool Const> struct Iterator {
        using Owner = std::conditional_t<Const, const FlowParticleStorage, FlowParticleStorage>;
        using value_type = FlowParticle;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;
        Owner *owner;
        size_t index;
        /** @brief Borrow a proxy for the iterator's logical particle.
         * @return Const-qualified or mutable proxy according to Owner; requires live owner
         * and in-range non-end index, with no intervening reallocation/removal. */
        auto operator*() const { return (*owner)[index]; }
        /** @brief Advance the particle index without bounds checking.
         * @return This iterator; callers stop at end() before dereferencing. */
        Iterator &operator++()
        {
            ++index;
            return *this;
        }
        /** @brief Compare stored owner and index exactly.
         * @param other Peer iterator or end sentinel.
         * @return True only when both fields match; owner is not dereferenced. */
        bool operator==(const Iterator &other) const = default;
    };
    /** @brief Borrow a mutable forward iterator at particle zero.
     * @return Iterator invalidated by storage reallocation/shrink or destruction. */
    auto begin() { return Iterator<false>{this, 0}; }
    /** @brief Borrow a mutable one-past-particle-end sentinel.
     * @return Iterator at size(); do not dereference. */
    auto end() { return Iterator<false>{this, size()}; }
    /** @brief Borrow a const forward iterator at particle zero.
     * @return Iterator requiring stable, live storage during iteration. */
    auto begin() const { return Iterator<true>{this, 0}; }
    /** @brief Borrow a const one-past-particle-end sentinel.
     * @return Iterator at size(); do not dereference. */
    auto end() const { return Iterator<true>{this, size()}; }

private:
    std::vector<QPointF> m_position;
    std::vector<int> m_cell;
    std::vector<double> m_age, m_previousAge, m_lifetime, m_stoppedAt, m_pending;
    std::vector<Cold> m_cold;
};
struct FlowParticleSettings {
    double timestep = 1. / 60., speed = 1, minimumLifetime = 20, maximumLifetime = 40;
    double referenceSpeed = 0,
           speedExponent = 1; // optional display compression; physical fields stay unchanged
    double trailSeconds = 1, fadeSeconds = .5, cutoff = .001;
    double publicationInterval = 0; // zero publishes every numerical tick
    double historyTolerance = .001; // scene units; native layer derives this from a quarter pixel
    int maxParticles = 2000, maxHistory = 512;
};
struct FlowParticleFrame {
    double clock = 0, previousClock = 0, timestep = 1. / 60.;
    double publicationMs = 0;
    quint64 geometry = 0, revision = 0, births = 0, deaths = 0, unresolvedOrbits = 0;
    quint64 deferredPublications = 0;
    std::array<quint64, 7> stopCounts{};
    FlowParticleSlots<FlowParticle> particles;
    FlowParticleRibbon ribbon;
};
struct FlowParticleWork {
    int steps = 0, ticks = 0;
    bool pending = false;
};

// Single-owner integration core. A worker owns this object; the renderer sees
// only completed immutable frames. No mesh-wide operation occurs on a tick.
class FlowParticleSystem
{
public:
    /** @brief Construct a single-owner integrator and reserve default settings/frame storage.
     * Allocation errors propagate; no field is installed and no display frame is published. */
    FlowParticleSystem();
    // Clear hydraulic/time state while retaining bounded reusable storage.
    // Publications held by a display remain immutable.
    /** @brief Drop installed fields/publication and reset clocks, particles, IDs and counters.
     * Recycles current history pools and retains settings, appearance, frame-pool
     * allocations and externally held immutable publications. The queued-retirement
     * count is not explicitly reset here. No new empty frame is published.
     * Call on the owning worker with no concurrent access to this system. */
    void reset();
    /** @brief Validate settings, reserve bounded working storage and install them.
     * @param settings Copied configuration. All double fields must be finite. timestep,
     * minimumLifetime and trailSeconds must be positive; maximumLifetime>=minimumLifetime;
     * speed, fadeSeconds, cutoff, historyTolerance, referenceSpeed and publicationInterval
     * nonnegative; 0<speedExponent<=1; maxParticles>=1 and maxHistory>=3. Times are
     * animation/flow seconds, cutoff metres, tolerance scene units, and referenceSpeed
     * scene units per flow second. speed is a dimensionless display gain.
     * @return False for invalid input before mutation, true after copying settings.
     * Allocation/copy errors propagate and may have grown storage/history pools before
     * settings replacement. Existing particles are not immediately trimmed to reduced
     * capacity/history limits, and existing lifetimes are not reassigned. Owner serializes calls. */
    bool setSettings(const FlowParticleSettings &settings);
    /** @brief Install prepared immutable hydraulics, remapping regional cells when needed.
     * @param field Nonnull shared snapshot with mesh, matching field/triangle/neighbor
     * counts, consistent valid topology/grid and sorted local-to-global sourceTriangles
     * for regional replacement. Snapshot/mesh must remain immutable after publication.
     * No deep finite/index/coherence validation is performed here.
     * @param transitionSeconds Finite nonnegative duration in flow-clock seconds for
     * discharge/depth-aware blending; zero replaces velocities immediately. Newly
     * displayed wet support/closed faces remain authoritative during blending.
     * @return False for missing/count-invalid input, invalid transition or an in-progress
     * tick, before installation. True after accepting it, even if publication is deferred
     * because all frame buffers are retained. Geometry-ID changes clear particles/lag
     * without resetting the main clock/counters; same-geometry regional swaps preserve
     * world positions/ages and remap history cells, with missing residency waiting for data.
     * Historical wet support is rechecked to hide newly dry trail portions. Allocation
     * errors can leave partial remaps; no transactional rollback is promised. */
    bool setField(std::shared_ptr<const FlowParticleSnapshot> field, double transitionSeconds = 0);
    /** @brief Create one wet-supported particle with a deterministic lifetime and initial sample.
     * @param position Finite scene position inside an available triangle above cutoff.
     * @param randomSeed Integer combined with the new birth ID to select a lifetime
     * uniformly between the configured minimum/maximum; no shared RNG is advanced.
     * @param publishImmediately Request a new immutable frame after insertion; publication
     * can still defer if every pooled frame is externally retained.
     * @param cellHint Nonnegative local triangle index to test directly, or negative
     * to use the bounded point locator. A wrong nonnegative hint is rejected without a search.
     * @param initialAgeFraction Finite fraction in [0,1), multiplied by assigned lifetime;
     * initial previous/published ages match it and only the initial position is sampled.
     * @return New nonzero birth ID, or zero during a tick, at capacity, for invalid
     * fraction or missing/invalid/dry support. Histories/frame spare slots may allocate;
     * exceptions propagate with no strong rollback guarantee. Caller avoids birth-ID wrap. */
    quint64 spawn(QPointF position, quint64 randomSeed = 0, bool publishImmediately = true,
                  int cellHint = -1, double initialAgeFraction = 0);
    /** @brief Consume elapsed animation time through resumable fixed numerical ticks.
     * @param elapsed Finite nonnegative seconds added to retained lag; total accumulated
     * lag must remain representable. Zero can resume an unfinished tick or drain backlog.
     * @param stepBudget Positive bound on integration/reduction operations and completed
     * ticks for this call. Large work resumes at the saved particle/reduction cursor.
     * @return Steps consumed, ticks completed and whether numerical work remains pending.
     * Invalid input or no field returns zero work without adding lag. Tick completion
     * advances the clock, applies queued retirement and conditionally publishes immutable
     * frames. Retained display buffers can defer publication while integration continues.
     * There is no automatic spawn, offscreen release or eraseFinished() in this method.
     * Owner serializes calls; callbacks/allocations during publication may throw. */
    FlowParticleWork advance(double elapsed, int stepBudget = 16384);
    /** @brief Remove stopped particles after fadeSeconds+publicationInterval have elapsed.
     * No-op during an unfinished tick. Compacts survivors in birth order and recycles
     * history pools without changing birth/death counters or immediately publishing a
     * frame; caller arranges later publication. Allocation errors may propagate. */
    void eraseFinished();
    /** @brief Free enough wholly offscreen particles to make the requested capacity available.
     * @param view Valid finite scene rectangle; a head inside or overlapping history
     * bounds keeps a particle. This is an inclusive bounds test, not exact path clipping.
     * @param requestedSlots Positive number of free slots wanted, accounting for slots
     * already available under maxParticles.
     * @return Number removed, zero for invalid arguments, a tick in progress or sufficient
     * free capacity. Removes in storage order, recycles history, compacts survivors and
     * does not increment death/stop counters or publish. May remove active or stopped particles. */
    int releaseOffscreen(const QRectF &view, int requestedSlots);
    // Retire the active particles nearest the end of their assigned lives, so
    // density can fall within one fade. A tick in progress defers to its end.
    /** @param count Positive requested number of active particles to stop, selected by
     * smallest remaining lifetime with storage-index tie breaking. Nonpositive is a no-op.
     * @return Number newly stopped, or zero when deferred during a tick. Deferred requests
     * retain the maximum requested count rather than summing. Immediate retirement uses
     * Lifetime reason and the existing stop() clock convention; fading/removal/publication
     * remain separate. No lifetime/age is advanced merely by retirement. */
    int retire(int count);
    /** @brief Inspect whether numerical work is paused inside a fixed tick.
     * @return True while a tick has unprocessed particle work; worker-owned, not atomic. */
    bool inTick() const { return m_inTick; }
    /** @brief Replace appearance captured by later ribbon publications.
     * @param scale Positive finite pixels per scene unit to build ribbons; nonpositive
     * disables ribbon rebuilding. Supply finite input and valid capacity arithmetic.
     * @param appearance Value moved into storage; palette callbacks must own immutable
     * thread-safe data. No frame publication or Qt signal is triggered by this setter. */
    void setAppearance(double scale, FlowParticleAppearance appearance)
    {
        m_renderScale = scale;
        m_appearance = std::move(appearance);
    }
    /** @brief Acquire shared ownership of the last completed display publication.
     * @return Immutable snapshot, possibly null. Holding all pooled frames can defer
     * future publication; this accessor itself is not atomic and is called on the owner. */
    std::shared_ptr<const FlowParticleFrame> frame() const { return m_frame; }
    /** @brief Borrow live worker state, including particles inside an unfinished tick.
     * @return Owned storage view invalidated by resize/compaction; use completed frame()
     * for immutable display snapshots. Do not read concurrently with the worker. */
    const FlowParticleStorage &particles() const { return m_particles; }
    /** @brief Read completed numerical time since construction/reset.
     * @return Flow-clock seconds, excluding retained lag and an unfinished tick. */
    double clock() const { return m_clock; }
    /** @brief Read elapsed time not yet removed by completed fixed ticks.
     * @return Retained seconds, including time assigned to an unfinished tick. */
    double lagSeconds() const { return m_elapsed; }
    /** @brief Compute the clamped interpolation fraction for the displayed time interval.
     * @return [0,1]. With per-tick publication, uses lag/timestep; with an explicit
     * publication interval, uses delayed visible time relative to the last frame's
     * previous/current clock. Returns zero when that interval is unavailable/degenerate. */
    double interpolation() const;

private:
    /** @brief Search one spatial-index bucket for a containing supported triangle.
     * @param p Finite scene position.
     * @param requireWet Require available field and interpolated depth>cutoff when true;
     * false only checks geometry containment.
     * @return First matching local triangle ID, or -1 with no field/candidates, more
     * than 8,192 candidates, or no match. Prepared valid grid/indices are preconditions;
     * no whole-mesh fallback search occurs. */
    int locate(QPointF p, bool requireWet = true) const;
    /** @brief Resume one particle's pending fixed-tick motion with local face walking.
     * @param[in,out] p Borrowed current worker particle with nonempty valid history.
     * @param[in,out] budget Remaining numerical/reduction operation budget, decremented per step.
     * @param[in,out] steps Count incremented per consumed numerical/reduction operation.
     * @return False only when work pauses for exhausted budget, true when this particle
     * has completed, stopped or waits for missing residency. Requires installed prepared
     * field, valid settings and exclusive worker ownership. Midpoint stepping clips to
     * faces/wet cutoff and bounds movement by triangle altitude; dry/invalid/closed/
     * outflow/conflicting paths stop with counters, while absent regional support waits.
     * History reduction is resumable. Display-compressed unresolved tiny cycles may
     * consume age within tolerance without moving, rather than adding unbounded crossings. */
    bool integrate(FlowParticleReference<false> &p, int &budget, int &steps);
    /** @brief Resume bounded-error trail simplification without changing numerical motion.
     * @param[in,out] p Current particle with at least two history samples; only one
     * particle may own the system's retained reduction scratch at a time.
     * @param[in,out] budget Operation budget, decremented once per tested interior sample.
     * @param[in,out] steps Consumed-operation counter incremented alongside budget.
     * @return False while paused for budget, true after replacing history and resetting
     * scratch. Shortcuts require time/position/speed error bounds and known wet/face
     * support. If the retained path still exceeds the display budget, its oldest tail
     * is dropped and historyFloorAge records that visual limit; integration age survives. */
    bool reduceHistory(FlowParticleReference<false> &p, int &budget, int &steps);
    /** @brief Mark one terminal stop and book death/reason counters.
     * @param[in,out] p Active particle; caller ensures it has not already been stopped.
     * @param reason Valid terminal enum whose ordinal indexes the seven-element counter.
     * Clears pending time and stamps stoppedAt=m_clock+m_tick, using the current stored
     * tick duration even for callers outside an active tick. Does not append history
     * or remove/publish the particle; repeated calls would count repeated deaths. */
    void stop(FlowParticleReference<false> &p, FlowParticleStop reason);
    /** @brief Find a pooled frame with no external immutable readers.
     * @param excludeCurrent Exclude the current published buffer while preparing a
     * future tick; false can reuse it only when held solely by pool/current pointers.
     * @return Mutable shared buffer owned by this system, or null if all are retained.
     * Caller keeps exclusive mutation ownership and does not expose it before publish(). */
    std::shared_ptr<FlowParticleFrame> writableFrame(bool excludeCurrent = false);
    /** @brief Size a private frame header array and release retired shared histories.
     * @param[in,out] frame Exclusively writable pooled frame; active size becomes the
     * worker population. Hidden spare objects remain allocated; formerly active removed
     * slots are reset first so they release shared history pages. No header capture yet. */
    void prepareFrame(FlowParticleFrame &frame);
    /** @brief Copy one worker display header/history into a private publication slot.
     * @param[in,out] frame Exclusively writable prepared frame with an in-range slot.
     * @param index In-range index shared by worker storage and the frame.
     * @param regular Use latest published age for previousAge when explicitly throttled;
     * false retains previousPublishedAge for an intervening forced publication.
     * Trims the copied history prefix through visibleAfterAge without mutating the
     * worker's ring indices. Shared pages remain immutable under later worker writes. */
    void captureParticle(FlowParticleFrame &frame, size_t index, bool regular);
    /** @brief Hand off a completed immutable display snapshot when a pool buffer is free.
     * @param frame Optional exclusively writable prepared buffer, or null to select one.
     * A supplied buffer with timestep==publicationInterval is treated as pre-captured;
     * otherwise headers are prepared/copied here. External callers must not invoke this
     * private method with a retained/readable frame.
     * Stamps clocks/counters, advances published ages for regular cadence, and builds
     * ribbons when enabled. If no buffer is free, increments deferredPublications and
     * returns without disrupting integration. Allocation/palette exceptions propagate
     * with private-buffer changes; ownership exchange occurs only after preparation. */
    void publish(std::shared_ptr<FlowParticleFrame> frame = {});
    /** @brief Append the current motion state and enforce the display-history window/budget.
     * @param[in,out] p Particle whose position/age/cell have just advanced.
     * @param speed SI speed in m/s recorded at that sample; caller supplies valid data.
     * Drops old support beyond trailSeconds+publicationInterval while retaining needed
     * endpoints, appends a sample, updates reduction cadence and caps maxHistory.
     * Limited tails are display restrictions only; lifetime and numerical pending time remain. */
    void appendHistory(FlowParticleReference<false> &p, float speed);
    /** @brief Copy a cell field at the current hydraulic transition time.
     * @param cell Unchecked valid cell index in installed field and any previous blend.
     * @param clock Flow-clock seconds used to clamp transition interpolation.
     * @return New field retaining current depth/availability/residency, with velocity/
     * tangential correction blended through discharge and mean depth where both fields
     * are available. SI speed/finite face fluxes blend linearly; explicit current closed
     * faces remain authoritative. Requires positive coherent velocityDepth denominators. */
    FlowParticleField fieldAt(int cell, double clock) const;
    std::shared_ptr<const FlowParticleSnapshot> m_field;
    std::shared_ptr<const FlowParticleSnapshot> m_previousField;
    double m_transitionClock = 0, m_transitionSeconds = 0;
    std::shared_ptr<const FlowParticleFrame> m_frame;
    FlowParticleSettings m_settings;
    FlowParticleAppearance m_appearance;
    double m_renderScale = 0;
    FlowParticleStorage m_particles;
    std::vector<FlowParticleRing<FlowParticleSample>> m_freeHistories;
    std::array<std::shared_ptr<FlowParticleFrame>, 4> m_frames;
    std::shared_ptr<FlowParticleFrame> m_stagingFrame;
    double m_clock = 0, m_elapsed = 0, m_tick = 0, m_publishedClock = 0,
           m_previousPublishedClock = 0;
    size_t m_cursor = 0;
    bool m_inTick = false;
    int m_pendingRetire = 0;
    // Only the current particle can have a pending reduction. Publication
    // keeps the original history until the linear pass is complete.
    FlowParticleRing<FlowParticleSample> m_reducedHistory;
    size_t m_historyIndex = 0;
    double m_historyError = -1;
    quint64 m_nextId = 1, m_births = 0, m_deaths = 0, m_unresolvedOrbits = 0;
    std::array<quint64, 7> m_stopCounts{};
    quint64 m_deferredPublications = 0;
};
} // namespace OpenSWMM::Render
