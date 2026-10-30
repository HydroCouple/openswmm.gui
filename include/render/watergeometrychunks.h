// GPL-3.0-or-later

/**
 * @file      watergeometrychunks.h
 * @brief     Chunked water geometry for mesh result rendering.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_RENDER_WATERGEOMETRYCHUNKS_H
#define OPENSWMM_RENDER_WATERGEOMETRYCHUNKS_H

#include <QtGlobal>
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

namespace OpenSWMM::Render
{

constexpr int waterGeometryChunkLimit = 65532;

template <class Vertex> struct WaterGeometryChunk {
    std::vector<Vertex> vertices;
    std::vector<quint16> indices;
};

// One bounded chunk of exact 12-byte attributes. The sink consumes each
// chunk immediately, either uploading it or retaining an immutable packet.
/** @brief Deduplicate exact vertex bytes within bounded 16-bit indexed chunks.
 * @tparam Vertex Exactly twelve-byte vertex representation with initialized,
 * stable object bytes; three quint32 words are hashed for bitwise equality.
 * Signed zero, NaN payloads and padding differences are distinct keys. Calls and
 * sink callbacks are serialized by the owner; callbacks must not reenter the builder.
 * Call finish() explicitly to publish pending data; destruction does not flush. */
template <class Vertex> class WaterGeometryBuilder
{
public:
    using Sink = std::function<void(const std::vector<Vertex> &, const std::vector<quint16> &)>;
    /** @brief Take the chunk-consumer callable without publishing a chunk.
     * @param sink Nonempty callable moved into storage. Receives borrowed vertex/
     * index vectors synchronously at automatic flushes and finish(); copy data
     * before returning if it must outlive the call. Callback exceptions propagate. */
    explicit WaterGeometryBuilder(Sink sink) : sink_(std::move(sink))
    {
        static_assert(sizeof(Vertex) == 12);
    }
    /** @brief Reserve capacity for one bounded vertex/index chunk without changing counts.
     * @param count Requested element capacity, capped at waterGeometryChunkLimit
     * for both vectors. The hash table is allocated lazily by push_back(). */
    void reserve(size_t count)
    {
        const size_t limit = std::min(count, size_t(waterGeometryChunkLimit));
        vertices_.reserve(limit);
        indices_.reserve(limit);
    }
    /** @brief Append one vertex reference, deduplicating its twelve exact bytes.
     * @param vertex Value copied only if its bitwise key is new in the current chunk.
     * Every call appends a quint16 index and advances the lifetime reference count.
     * Reaching 65,532 references publishes and clears that chunk synchronously;
     * deduplication does not span chunks. Allocation/callback errors propagate
     * without a strong rollback guarantee. Triangle callers append references in triples. */
    void push_back(const Vertex &vertex)
    {
        if (table_.empty())
            table_.resize(size_t(1) << 17);
        std::array<quint32, 3> key;
        std::memcpy(key.data(), &vertex, sizeof(vertex));
        quint32 hash = 2166136261u;
        for (quint32 word : key)
            hash = (hash ^ word) * 16777619u;
        hash ^= hash >> 16;
        hash *= 0x7feb352du;
        hash ^= hash >> 15;
        hash *= 0x846ca68bu;
        hash ^= hash >> 16;
        size_t slot = hash & (table_.size() - 1);
        while (table_[slot].generation == generation_ && table_[slot].key != key)
            slot = (slot + 1) & (table_.size() - 1);
        auto &entry = table_[slot];
        if (entry.generation != generation_) {
            entry.key = key;
            entry.generation = generation_;
            entry.index = quint16(vertices_.size());
            vertices_.push_back(vertex);
        }
        indices_.push_back(entry.index);
        ++references_;
        if (indices_.size() == size_t(waterGeometryChunkLimit))
            flush();
    }
    /** @brief Publish remaining references, or one empty chunk if nothing was ever flushed.
     * Repeated calls with no new references are no-ops after the first flush.
     * The builder remains usable afterward; statistics stay cumulative. */
    void finish()
    {
        if (!started_ || !indices_.empty())
            flush();
    }
    /** @brief Count unique vertices already sent to the sink across all chunks.
     * @return Cumulative flushed count, excluding pending vertices and counting
     * identical vertices again when they occur in different chunks. */
    [[nodiscard]] size_t size() const { return uploadedVertices_; }
    /** @brief Count vertex and index bytes already sent to the sink.
     * @return Cumulative flushed vertices*12 + indices*sizeof(quint16), excluding
     * pending buffers and the builder's hash/capacity overhead. */
    [[nodiscard]] size_t bytes() const { return uploadedBytes_; }
    /** @brief Test whether any reference has ever been appended.
     * @return True before the first push_back(), even after publishing an empty
     * chunk; false after any reference, including when pending vectors are empty. */
    [[nodiscard]] bool empty() const { return references_ == 0; }

private:
    struct Entry {
        std::array<quint32, 3> key{};
        quint32 generation = 0;
        quint16 index = 0;
    };
    /** @brief Invoke the sink before accumulating counts and clearing current buffers.
     * Successful callbacks advance the hash generation, preserving allocated
     * capacity and resetting the table on generation wrap. If the sink throws,
     * counts/clearing are not performed, so its external partial effects are not rolled back. */
    void flush()
    {
        sink_(vertices_, indices_);
        started_ = true;
        uploadedVertices_ += vertices_.size();
        uploadedBytes_ += vertices_.size() * sizeof(Vertex) + indices_.size() * sizeof(quint16);
        vertices_.clear();
        indices_.clear();
        if (++generation_ == 0) {
            for (auto &entry : table_)
                entry.generation = 0;
            generation_ = 1;
        }
    }
    Sink sink_;
    std::vector<Vertex> vertices_;
    std::vector<quint16> indices_;
    std::vector<Entry> table_;
    size_t references_ = 0, uploadedVertices_ = 0, uploadedBytes_ = 0;
    quint32 generation_ = 1;
    bool started_ = false;
};
} // namespace OpenSWMM::Render
#endif
