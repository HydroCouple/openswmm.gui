// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>

namespace meshkit {
class Point2 {
    std::array<double,2> xy_{};
public:
    constexpr Point2()=default;
    constexpr Point2(double x,double y):xy_{x,y} {}
    constexpr double x() const noexcept {return xy_[0];}
    constexpr double y() const noexcept {return xy_[1];}
    double *data() noexcept {return xy_.data();}
    const double *data() const noexcept {return xy_.data();}
    Point2 &operator+=(Point2 p){xy_[0]+=p.x();xy_[1]+=p.y();return *this;}
    Point2 &operator-=(Point2 p){xy_[0]-=p.x();xy_[1]-=p.y();return *this;}
    friend constexpr Point2 operator+(Point2 a,Point2 b){return {a.x()+b.x(),a.y()+b.y()};}
    friend constexpr Point2 operator-(Point2 a,Point2 b){return {a.x()-b.x(),a.y()-b.y()};}
    friend constexpr Point2 operator*(Point2 a,double s){return {a.x()*s,a.y()*s};}
    friend constexpr Point2 operator*(double s,Point2 a){return a*s;}
    friend constexpr Point2 operator/(Point2 a,double s){return {a.x()/s,a.y()/s};}
    friend constexpr bool operator==(Point2 a,Point2 b){return a.x()==b.x() && a.y()==b.y();}
    friend constexpr bool operator!=(Point2 a,Point2 b){return !(a==b);}
};
class Box2 {
    double x0_=0,y0_=0,w_=0,h_=0;
public:
    constexpr Box2()=default;
    constexpr Box2(Point2 a,Point2 b):x0_(a.x()),y0_(a.y()),w_(b.x()-a.x()),h_(b.y()-a.y()) {}
    constexpr double left() const {return x0_;}
    constexpr double right() const {return x0_+w_;}
    constexpr double top() const {return y0_;}
    constexpr double bottom() const {return y0_+h_;}
    constexpr double width() const {return w_;}
    constexpr double height() const {return h_;}
    constexpr Point2 center() const {return {x0_+w_*.5,y0_+h_*.5};}
    Box2 united(Box2 b) const {return {{std::min(x0_,b.x0_),std::min(y0_,b.y0_)},{std::max(right(),b.right()),std::max(bottom(),b.bottom())}};}
};
struct Point3 { double x=0,y=0,z=0; };
static_assert(std::is_standard_layout<Point3>::value && sizeof(Point3)==3*sizeof(double));
static_assert(std::is_standard_layout<Point2>::value && sizeof(Point2)==2*sizeof(double));
}
