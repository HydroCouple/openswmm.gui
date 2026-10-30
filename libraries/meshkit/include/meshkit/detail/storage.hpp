// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "meshkit/geometry.hpp"
#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace meshkit::detail
{
// Small internal storage vocabulary shared by the standard and Qt kernel
// specializations. Public jobs own ordinary standard C++ arrays. No GUI mirror
// or per-access conversion of the kernel's multi-million-entry buffers.
template <class T> class Vector : public std::vector<T>
{
    using Base = std::vector<T>;

public:
    using Base::Base;
    Vector() = default;
    Vector(const std::vector<T> &v) : Base(v) {};
    Vector(std::vector<T> &&v) : Base(std::move(v)) {}
    decltype(auto) first() { return this->front(); }
    decltype(auto) first() const { return this->front(); }
    decltype(auto) last() { return this->back(); }
    decltype(auto) last() const { return this->back(); }
    const T *constData() const { return this->data(); }
    Vector mid(std::ptrdiff_t at, std::ptrdiff_t count = -1) const
    {
        auto end = count < 0 ? this->size() : std::min(this->size(), at + count);
        return {this->begin() + at, this->begin() + end};
    }
    void prepend(const T &v) { this->insert(this->begin(), v); }
    void append(const Vector &v) { this->insert(this->end(), v.begin(), v.end()); }
    Vector &operator+=(const Vector &v)
    {
        append(v);
        return *this;
    }
    Vector &operator<<(const T &v)
    {
        append(v);
        return *this;
    }

    std::ptrdiff_t size() const noexcept { return static_cast<std::ptrdiff_t>(Base::size()); }
    bool isEmpty() const { return this->empty(); }
    void append(const T &v) { this->push_back(v); }
    void append(T &&v) { this->push_back(std::move(v)); }
    T takeLast()
    {
        T result = std::move(this->back());
        this->pop_back();
        return result;
    }
    T takeFirst()
    {
        T result = std::move(this->front());
        this->erase(this->begin());
        return result;
    }
    void removeLast() { this->pop_back(); }
    void removeAt(int at) { this->erase(this->begin() + at); }
    void fill(const T &v) { std::fill(this->begin(), this->end(), v); }
    void fill(const T &v, std::size_t size) { this->assign(size, v); }
    bool contains(const T &v) const
    {
        return std::find(this->begin(), this->end(), v) != this->end();
    }
};
template <class K> struct Hash : std::hash<K> {
};
template <class A, class B> struct Hash<std::pair<A, B>> {
    std::size_t operator()(const std::pair<A, B> &p) const
    {
        const auto a = Hash<A>{}(p.first), b = Hash<B>{}(p.second);
        return a ^ (b + std::size_t(0x9e3779b97f4a7c15ull) + (a << 6) + (a >> 2));
    }
};
template <class K, class V> class Map
{
    using Base = std::unordered_map<K, V, Hash<K>>;
    Base data_;

public:
    template <bool Const> class Iterator
    {
        using It =
            std::conditional_t<Const, typename Base::const_iterator, typename Base::iterator>;
        It it_;

    public:
        explicit Iterator(It it) : it_(it) {}
        decltype(auto) value() const { return (it_->second); }
        const K &key() const { return it_->first; }
        decltype(auto) operator*() const { return (it_->second); }
        auto operator->() const { return &it_->second; }
        Iterator &operator++()
        {
            ++it_;
            return *this;
        }
        bool operator==(const Iterator &b) const { return it_ == b.it_; }
        bool operator!=(const Iterator &b) const { return it_ != b.it_; }
    };
    using ConstIterator = Iterator<true>;
    using MutableIterator = Iterator<false>;
    void clear() { data_.clear(); }
    void reserve(std::size_t n) { data_.reserve(n); }
    bool isEmpty() const { return data_.empty(); }
    std::ptrdiff_t size() const { return data_.size(); }
    bool contains(const K &k) const { return data_.find(k) != data_.end(); }
    MutableIterator insert(const K &k, const V &v)
    {
        return MutableIterator(data_.insert_or_assign(k, v).first);
    }
    bool remove(const K &k) { return data_.erase(k) != 0; }
    V &operator[](const K &k) { return data_[k]; }
    V value(const K &k, const V &fallback = V{}) const
    {
        auto it = data_.find(k);
        return it == data_.end() ? fallback : it->second;
    }
    MutableIterator find(const K &k) { return MutableIterator(data_.find(k)); }
    MutableIterator begin() { return MutableIterator(data_.begin()); }
    MutableIterator end() { return MutableIterator(data_.end()); }
    ConstIterator constFind(const K &k) const { return ConstIterator(data_.find(k)); }
    ConstIterator constEnd() const { return ConstIterator(data_.end()); }
    ConstIterator cbegin() const { return ConstIterator(data_.cbegin()); }
    ConstIterator cend() const { return ConstIterator(data_.cend()); }
    ConstIterator begin() const { return cbegin(); }
    ConstIterator end() const { return cend(); }
};
template <class K, class V> class MultiMap
{
    std::unordered_map<K, Vector<V>, Hash<K>> data_;

public:
    void clear() { data_.clear(); }
    void remove(const K &key) { data_.erase(key); }
    void insert(const K &key, const V &value)
    {
        auto &values = data_[key];
        values.insert(values.begin(), value);
    }
    bool contains(const K &key, const V &value) const
    {
        auto it = data_.find(key);
        return it != data_.end() && it->second.contains(value);
    }
    Vector<V> values(const K &key) const
    {
        auto it = data_.find(key);
        return it == data_.end() ? Vector<V>{} : it->second;
    }
};
template <class K> class Set : public std::unordered_set<K, Hash<K>>
{
public:
    bool remove(const K &k) { return this->erase(k) != 0; }
    bool isEmpty() const { return this->empty(); }
    bool contains(const K &key) const { return this->find(key) != this->end(); }
};
struct StandardStorage {
    using Point = Point2;
    using Box = Box2;
    using String = std::string;
    using PointInput = std::vector<Point2>;
    template <class T> using Vector = detail::Vector<T>;
    template <class K, class V> using Hash = Map<K, V>;
    template <class K, class V> using MultiHash = MultiMap<K, V>;
    template <class K> using Set = detail::Set<K>;
    static String message(const char *s) { return s; }
    static String invalidPointMessage(int i)
    {
        return "cdt: non-finite input point " + std::to_string(i);
    }
};
template <class F> class ScopeExit
{
    F f_;

public:
    explicit ScopeExit(F f) : f_(std::move(f)) {}
    ~ScopeExit() { f_(); }
    ScopeExit(const ScopeExit &) = delete;
};
template <class F> ScopeExit<F> scopeExit(F f)
{
    return ScopeExit<F>(std::move(f));
}
class Timer
{
    std::chrono::steady_clock::time_point start_;

public:
    void start() { start_ = std::chrono::steady_clock::now(); }
    std::int64_t nsecsElapsed() const
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now() - start_)
            .count();
    }
};
}
