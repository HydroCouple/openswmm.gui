// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "meshkit/detail/storage.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>
namespace meshkit::plan
{
using detail::Vector;
using detail::Map;
using detail::Set;
inline constexpr double pi = 3.141592653589793238462643383279502884;
class Text : public std::string
{
public:
    using std::string::string;
    Text() = default;
    Text(const std::string &v) : std::string(v) {};
    bool isEmpty() const { return empty(); }
    template <class T> Text arg(const T &value) const
    {
        std::ostringstream out;
        out << value;
        Text result = *this;
        std::size_t at = std::string::npos;
        int lowest = 100;
        for (std::size_t i = 0; i + 1 < size(); ++i)
            if ((*this)[i] == '%' && (*this)[i + 1] >= '1' && (*this)[i + 1] <= '9') {
                int n = (*this)[i + 1] - '0';
                if (n < lowest) {
                    lowest = n;
                    at = i;
                }
            }
        if (at != std::string::npos) {
            const auto key = "%" + std::to_string(lowest);
            std::size_t p = 0;
            while ((p = result.find(key, p)) != std::string::npos) {
                result.replace(p, key.size(), out.str());
                p += out.str().size();
            }
        }
        return result;
    }
    template <class T, class U> Text arg(const T &a, const U &b) const { return arg(a).arg(b); }
};
using Bytes = std::string;
class Polygon : public Vector<Point2>
{
public:
    using Vector<Point2>::Vector;
    Polygon() = default;
    Polygon(const Vector<Point2> &v) : Vector<Point2>(v) {};
    Polygon(const std::vector<Point2> &v) : Vector<Point2>(v) {}
    Box2 boundingRect() const
    {
        if (empty())
            return {};
        double x0 = front().x(), x1 = x0, y0 = front().y(), y1 = y0;
        for (auto p : *this) {
            x0 = std::min(x0, p.x());
            x1 = std::max(x1, p.x());
            y0 = std::min(y0, p.y());
            y1 = std::max(y1, p.y());
        }
        return {{x0, y0}, {x1, y1}};
    }
};
struct Line2 {
    Point2 a, b;
    Line2(Point2 x, Point2 y) : a(x), b(y) {}
    double length() const { return std::hypot(b.x() - a.x(), b.y() - a.y()); }
};
struct MeshVertex {
    Point2 xy;
    double z = 0;
};
struct MeshTriangle {
    int v0 = 0, v1 = 0, v2 = 0, v3 = -1;
    Text tag;
    bool isQuad() const { return v3 >= 0; }
    int vertexCount() const { return isQuad() ? 4 : 3; }
    int vertex(int k) const { return k == 0 ? v0 : k == 1 ? v1 : k == 2 ? v2 : k == 3 ? v3 : -1; }
    void setVertex(int k, int v)
    {
        if (k == 0)
            v0 = v;
        else if (k == 1)
            v1 = v;
        else if (k == 2)
            v2 = v;
        else if (k == 3)
            v3 = v;
    }
};
struct MeshEdge {
    int v0 = 0, v1 = 0, marker = 0;
    Text tag;
};
template <class T> class ReadArray : public Vector<T>
{
    const void *owner_ = nullptr;
    T (*read_)(const void *, std::size_t) = nullptr;
    std::ptrdiff_t count_ = 0;

public:
    using Vector<T>::Vector;
    void borrow(const void *owner, std::ptrdiff_t count, T (*read)(const void *, std::size_t))
    {
        owner_ = owner;
        count_ = count;
        read_ = read;
    }
    std::ptrdiff_t size() const { return owner_ ? count_ : Vector<T>::size(); }
    bool isEmpty() const { return size() == 0; }
    T operator[](std::size_t i) const
    {
        return owner_ ? read_(owner_, i) : Vector<T>::operator[](i);
    }
    struct Iterator {
        const ReadArray *array;
        std::size_t i;
        T operator*() const { return (*array)[i]; }
        Iterator &operator++()
        {
            ++i;
            return *this;
        }
        bool operator!=(const Iterator &b) const { return i != b.i; }
    };
    Iterator begin() const { return {this, 0}; }
    Iterator end() const { return {this, std::size_t(size())}; }
};
struct MeshResult {
    ReadArray<MeshVertex> vertices;
    ReadArray<MeshTriangle> triangles;
    ReadArray<MeshEdge> boundaryEdges;
    bool ok = false;
    Text errorMsg;
};
struct ConstraintSegment {
    Vector<Point2> path;
    int marker = 0;
    Text tag;
    double stripWidth = 0;
};
struct SteinerPoint {
    Point2 xy;
    int marker = 0;
    Text tag;
    double z = 0;
    bool hasZ = false;
};
inline bool cellIsConvex(const Vector<MeshVertex> &v, const MeshTriangle &c)
{
    if (!c.isQuad())
        return true;
    double before = 0;
    for (int i = 0; i < 4; ++i) {
        auto a = v[c.vertex(i)].xy, b = v[c.vertex((i + 1) % 4)].xy,
             d = v[c.vertex((i + 2) % 4)].xy;
        auto ab = b - a, bd = d - b;
        double turn = ab.x() * bd.y() - ab.y() * bd.x();
        if (!std::isfinite(turn) || turn == 0 || (before && (turn > 0) != (before > 0)))
            return false;
        before = turn;
    }
    return true;
}
}
