// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "meshkit/mesh.hpp"
#include <chrono>
#include <stdexcept>
#include <cmath>
#include <limits>
#include <initializer_list>
#include <utility>

namespace meshkit::native_detail {
struct Stop {RunStatus status;std::string code,message;};
[[noreturn]] inline void fail(std::string code,std::string message,RunStatus status=RunStatus::Infeasible){throw Stop{status,std::move(code),std::move(message)};}
struct Work {
    const Limits &limits;const std::function<bool()> &cancelled;
    std::uint64_t used=0,maximum=0;
    Work(const Limits &l,const std::function<bool()> &c):limits(l),cancelled(c) {
        maximum=l.maxWork?l.maxWork:20000000000ull;
        check();
    }
    void check() const {if(cancelled && cancelled())fail("cancelled","Cancelled.",RunStatus::Cancelled);}
    void tick(std::uint64_t count=1){if(count>maximum-used)fail("work_limit","Operation work limit exceeded.",RunStatus::ResourceLimit);used+=count;if((used&4095)==0)check();}
    void memoryFor(std::initializer_list<std::pair<std::uint64_t,std::uint64_t>> terms) const {
        std::uint64_t total=0;
        for(auto term:terms){if(term.second && term.first>(std::numeric_limits<std::uint64_t>::max()-total)/term.second)fail("workspace_limit","Workspace estimate overflows.",RunStatus::ResourceLimit);total+=term.first*term.second;}
        memory(total);
    }
    void memory(std::uint64_t estimate) const {if(estimate>limits.maxWorkspaceBytes)fail("workspace_limit","Estimated workspace exceeds the configured limit.",RunStatus::ResourceLimit);}
};
inline std::uint64_t edgeKey(Index a,Index b){return std::uint64_t(std::min(a,b))<<32|std::max(a,b);}
inline Box2 box(const Point2 *p,int n){double x0=p[0].x(),x1=x0,y0=p[0].y(),y1=y0;for(int k=1;k<n;++k){x0=std::min(x0,p[k].x());x1=std::max(x1,p[k].x());y0=std::min(y0,p[k].y());y1=std::max(y1,p[k].y());}return {{x0,y0},{x1,y1}};}
inline bool meets(const Box2 &a,const Box2 &b){return a.left()<=b.right() && a.right()>=b.left() && a.top()<=b.bottom() && a.bottom()>=b.top();}
inline bool finite(Point2 p){return std::isfinite(p.x())&&std::isfinite(p.y());}
inline bool utf8(const std::string &s) {
    for(std::size_t i=0;i<s.size();) {
        const auto first=static_cast<unsigned char>(s[i++]);if(first<128)continue;
        int rest=0;std::uint32_t cp=0,minimum=0;
        if(first>=0xc2 && first<=0xdf){rest=1;cp=first&31;minimum=0x80;}
        else if(first>=0xe0 && first<=0xef){rest=2;cp=first&15;minimum=0x800;}
        else if(first>=0xf0 && first<=0xf4){rest=3;cp=first&7;minimum=0x10000;}else return false;
        if(i+rest>s.size())return false;
        while(rest--){const auto c=static_cast<unsigned char>(s[i++]);if((c&0xc0)!=0x80)return false;cp=cp<<6|(c&63);}
        if(cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return false;
    }return true;
}
inline double seconds(std::chrono::steady_clock::time_point start){return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();}
}
