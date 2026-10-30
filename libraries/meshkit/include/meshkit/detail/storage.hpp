// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "meshkit/geometry.hpp"
#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace meshkit::detail {
// Small internal storage vocabulary shared by the standard and Qt kernel
// specializations. Public jobs own ordinary standard C++ arrays. No GUI mirror
// or per-access conversion of the kernel's multi-million-entry buffers.
template<class T> class Vector:public std::vector<T> {
    using Base=std::vector<T>;
public:
    using Base::Base;
    std::ptrdiff_t size() const noexcept {return static_cast<std::ptrdiff_t>(Base::size());}
    bool isEmpty() const {return this->empty();}
    void append(const T &v){this->push_back(v);}
    void append(T &&v){this->push_back(std::move(v));}
    T takeLast(){T result=std::move(this->back());this->pop_back();return result;}
    T takeFirst(){T result=std::move(this->front());this->erase(this->begin());return result;}
    void removeLast(){this->pop_back();}
    void removeAt(int at){this->erase(this->begin()+at);}
    void fill(const T &v){std::fill(this->begin(),this->end(),v);}
    void fill(const T &v,std::size_t size){this->assign(size,v);}
    bool contains(const T &v) const {return std::find(this->begin(),this->end(),v)!=this->end();}
};
template<class K> struct Hash:std::hash<K> {};
template<class A,class B> struct Hash<std::pair<A,B>> {
    std::size_t operator()(const std::pair<A,B> &p) const {
        const auto a=Hash<A>{}(p.first),b=Hash<B>{}(p.second);
        return a^(b+std::size_t(0x9e3779b97f4a7c15ull)+(a<<6)+(a>>2));
    }
};
template<class K,class V> class Map {
    using Base=std::unordered_map<K,V,Hash<K>>;
    Base data_;
public:
    class ConstIterator {
        typename Base::const_iterator it_;
    public:
        explicit ConstIterator(typename Base::const_iterator it):it_(it){}
        const V &value() const {return it_->second;}
        const V &operator*() const {return it_->second;}
        bool operator==(const ConstIterator &b) const {return it_==b.it_;}
        bool operator!=(const ConstIterator &b) const {return !(*this==b);}
    };
    void clear(){data_.clear();}
    void reserve(std::size_t size){data_.reserve(size);}
    bool contains(const K &key) const {return data_.find(key)!=data_.end();}
    void insert(const K &key,const V &value){data_.insert_or_assign(key,value);}
    void remove(const K &key){data_.erase(key);}
    V &operator[](const K &key){return data_[key];}
    V value(const K &key,const V &fallback=V{}) const {auto it=data_.find(key);return it==data_.end()?fallback:it->second;}
    ConstIterator constFind(const K &key) const {return ConstIterator(data_.find(key));}
    ConstIterator constEnd() const {return ConstIterator(data_.end());}
    ConstIterator cend() const {return constEnd();}
};
template<class K,class V> class MultiMap {
    std::unordered_map<K,Vector<V>,Hash<K>> data_;
public:
    void clear(){data_.clear();}
    void remove(const K &key){data_.erase(key);}
    void insert(const K &key,const V &value){auto &values=data_[key];values.insert(values.begin(),value);}
    bool contains(const K &key,const V &value) const {auto it=data_.find(key);return it!=data_.end() && it->second.contains(value);}
    Vector<V> values(const K &key) const {auto it=data_.find(key);return it==data_.end()?Vector<V>{}:it->second;}
};
template<class K> class Set:public std::unordered_set<K,Hash<K>> {
public:
    bool isEmpty() const {return this->empty();}
    bool contains(const K &key) const {return this->find(key)!=this->end();}
};
struct StandardStorage {
    using Point=Point2;using Box=Box2;using String=std::string;
    using PointInput=std::vector<Point2>;
    template<class T> using Vector=detail::Vector<T>;
    template<class K,class V> using Hash=Map<K,V>;
    template<class K,class V> using MultiHash=MultiMap<K,V>;
    template<class K> using Set=detail::Set<K>;
    static String message(const char *s){return s;}
    static String invalidPointMessage(int i){return "cdt: non-finite input point "+std::to_string(i);}
};
template<class F> class ScopeExit { F f_;public:explicit ScopeExit(F f):f_(std::move(f)){}~ScopeExit(){f_();}ScopeExit(const ScopeExit &)=delete; };
template<class F> ScopeExit<F> scopeExit(F f){return ScopeExit<F>(std::move(f));}
class Timer {
    std::chrono::steady_clock::time_point start_;
public:
    void start(){start_=std::chrono::steady_clock::now();}
    std::int64_t nsecsElapsed() const {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start_).count();}
};
}
