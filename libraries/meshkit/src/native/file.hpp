// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "meshkit/io.hpp"
#include "operation.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <random>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif
namespace meshkit::native_detail
{
inline bool validPath(const std::filesystem::path &p)
{
    return !p.empty() && p.native().find(std::filesystem::path::value_type{}) ==
                             std::filesystem::path::string_type::npos;
}
constexpr std::uint64_t hashSeed = 14695981039346656037ull;
inline void hashBytes(std::uint64_t &hash, const unsigned char *p, std::size_t n)
{
    for (std::size_t i = 0; i < n; ++i) {
        hash ^= p[i];
        hash *= 1099511628211ull;
    }
}
class Transaction
{
    std::filesystem::path target_, temp_;
    bool overwrite_, durable_;
    std::FILE *file_ = nullptr;

public:
    Transaction(const std::filesystem::path &target, const IOOptions &options)
        : target_(target), overwrite_(options.overwrite), durable_(options.durable)
    {
        if (!validPath(target) || target.filename().empty() || target.filename() == "." ||
            target.filename() == "..")
            fail("path", "Invalid output path.", RunStatus::Failed);
        auto parent = target.parent_path();
        if (parent.empty())
            parent = ".";
        static std::atomic<std::uint64_t> sequence{0};
        std::random_device random;
        for (int attempt = 0; attempt < 32; ++attempt) {
            const auto nonce = (std::uint64_t(random()) << 32) ^ random() ^ ++sequence;
            temp_ = parent / (".meshkit-" + std::to_string(nonce) + ".tmp");
#ifdef _WIN32
            file_ = _wfopen(temp_.c_str(), L"wbx");
#else
            file_ = std::fopen(temp_.c_str(), "wbx");
#endif
            if (file_)
                return;
            if (errno != EEXIST)
                break;
        }
        fail("open_output",
             "Cannot create an exclusive output temporary: " + std::string(std::strerror(errno)),
             RunStatus::Failed);
    }
    ~Transaction()
    {
        if (file_)
            std::fclose(file_);
        if (!temp_.empty()) {
            std::error_code error;
            std::filesystem::remove(temp_, error);
        }
    }
    Transaction(const Transaction &) = delete;
    std::FILE *file() const { return file_; }
    void commit()
    {
        if (std::fflush(file_) != 0)
            fail("write", "Cannot flush output.", RunStatus::Failed);
        if (durable_) {
#ifdef _WIN32
            if (_commit(_fileno(file_)) != 0)
                fail("sync", "Cannot synchronize output.", RunStatus::Failed);
#else
            if (::fsync(fileno(file_)) != 0)
                fail("sync", "Cannot synchronize output.", RunStatus::Failed);
#endif
        }
        const int closed = std::fclose(file_);
        file_ = nullptr;
        if (closed != 0)
            fail("close", "Cannot close output.", RunStatus::Failed);
#ifdef _WIN32
        const DWORD flags =
            (overwrite_ ? MOVEFILE_REPLACE_EXISTING : 0) | (durable_ ? MOVEFILE_WRITE_THROUGH : 0);
        if (!MoveFileExW(temp_.c_str(), target_.c_str(), flags))
            fail("publish", "Cannot atomically publish output (destination may already exist).",
                 RunStatus::Failed);
#else
        if (overwrite_) {
            if (::rename(temp_.c_str(), target_.c_str()) != 0)
                fail("publish",
                     "Cannot atomically replace output: " + std::string(std::strerror(errno)),
                     RunStatus::Failed);
        } else {
            if (::link(temp_.c_str(), target_.c_str()) != 0)
                fail("publish",
                     "Cannot publish output without replacing an existing file: " +
                         std::string(std::strerror(errno)),
                     RunStatus::Failed);
            ::unlink(temp_.c_str());
        }
#endif
        temp_.clear();
    }
};
class Output
{
    std::FILE *file_;
    Work &work_;
    std::array<unsigned char, 65536> buffer_{};
    std::size_t used_ = 0;

public:
    std::uint64_t bytes = 0, hash = hashSeed;
    Output(std::FILE *file, Work &work) : file_(file), work_(work) {}
    void flush()
    {
        work_.check();
        if (used_ && std::fwrite(buffer_.data(), 1, used_, file_) != used_)
            fail("write", "Output write failed.", RunStatus::Failed);
        used_ = 0;
    }
    void raw(const void *data, std::size_t n, bool hashed = true)
    {
        auto p = static_cast<const unsigned char *>(data);
        if (hashed)
            hashBytes(hash, p, n);
        bytes += n;
        while (n) {
            const auto count = std::min(n, buffer_.size() - used_);
            std::memcpy(buffer_.data() + used_, p, count);
            used_ += count;
            p += count;
            n -= count;
            if (used_ == buffer_.size())
                flush();
        }
    }
    void number(std::uint64_t value, int width, bool hashed = true)
    {
        unsigned char data[8];
        for (int i = 0; i < width; ++i) {
            data[i] = value & 255;
            value >>= 8;
        }
        raw(data, width, hashed);
    }
    void real(double value)
    {
        std::uint64_t bits;
        static_assert(sizeof(value) == sizeof(bits));
        std::memcpy(&bits, &value, 8);
        number(bits, 8);
    }
    void text(const std::string &s)
    {
        number(s.size(), 8);
        raw(s.data(), s.size());
    }
};
class Input
{
    std::istream &stream_;
    Work &work_;
    std::array<unsigned char, 65536> buffer_{};
    std::size_t at_ = 0, end_ = 0;

public:
    std::uint64_t bytes = 0, hash = hashSeed;
    Input(std::istream &stream, Work &work) : stream_(stream), work_(work) {}
    bool byte(unsigned char &value, bool hashed = true)
    {
        if (at_ == end_) {
            work_.check();
            stream_.read(reinterpret_cast<char *>(buffer_.data()), buffer_.size());
            end_ = std::size_t(stream_.gcount());
            at_ = 0;
            if (!end_) {
                if (stream_.bad())
                    fail("read", "Input read failed.", RunStatus::Failed);
                return false;
            }
        }
        value = buffer_[at_++];
        ++bytes;
        if (hashed)
            hashBytes(hash, &value, 1);
        return true;
    }
    void raw(void *target, std::size_t n, bool hashed = true)
    {
        auto p = static_cast<unsigned char *>(target);
        for (std::size_t i = 0; i < n; ++i)
            if (!byte(p[i], hashed))
                fail("truncated", "Truncated input.", RunStatus::Failed);
    }
    std::uint64_t number(int width, bool hashed = true)
    {
        unsigned char data[8];
        raw(data, width, hashed);
        std::uint64_t value = 0;
        for (int i = 0; i < width; ++i)
            value |= std::uint64_t(data[i]) << (8 * i);
        return value;
    }
    double real()
    {
        const auto bits = number(8);
        double value;
        std::memcpy(&value, &bits, 8);
        return value;
    }
    std::string text(std::uint64_t &remaining)
    {
        auto n = number(8);
        if (n > remaining)
            fail("metadata_limit", "Metadata size exceeds the limit.", RunStatus::ResourceLimit);
        remaining -= n;
        std::string result(std::size_t(n), '\0');
        raw(result.data(), result.size());
        return result;
    }
};
inline void appendId(std::vector<std::int64_t> &values, std::int64_t id, std::size_t index)
{
    if (values.empty() && id == std::int64_t(index + 1))
        return;
    if (values.empty()) {
        values.reserve(index + 1);
        for (std::size_t i = 0; i < index; ++i)
            values.push_back(i + 1);
    }
    values.push_back(id);
}
inline void appendMarker(std::vector<std::int32_t> &values, std::int32_t marker, std::size_t index)
{
    if (!marker && values.empty())
        return;
    if (values.empty())
        values.resize(index, 0);
    values.push_back(marker);
}
inline std::int64_t externalId(const std::vector<std::int64_t> &ids, std::size_t index)
{
    return ids.empty() ? std::int64_t(index + 1) : ids[index];
}
inline std::int64_t signed64(std::uint64_t bits)
{
    std::int64_t value;
    std::memcpy(&value, &bits, 8);
    return value;
}
inline std::int32_t signed32(std::uint64_t bits)
{
    std::uint32_t raw = std::uint32_t(bits);
    std::int32_t value;
    std::memcpy(&value, &raw, 4);
    return value;
}
inline void setError(IOReport &r, const Stop &e)
{
    r.status = e.status;
    r.code = e.code;
    r.message = e.message;
}
inline void setException(IOReport &r, const std::bad_alloc &)
{
    r.status = RunStatus::ResourceLimit;
    r.code = "allocation";
    r.message = "Insufficient memory.";
}
inline void setException(IOReport &r, const std::exception &e)
{
    r.status = RunStatus::Failed;
    r.code = "exception";
    r.message = e.what();
}
inline void validated(const Mesh &mesh, const IOOptions &o)
{
    const auto r = validateMesh(mesh, o.limits, o.cancelled);
    if (!r.complete())
        fail(r.code, r.message, r.status);
}
}
