#include "render/resultscalargeometry.h"
#include <QFontMetricsF>
#include <cmath>
#include <limits>
#include <numeric>
namespace OpenSWMM::Render {
ResultScalarGeometry buildResultScalarGeometry(const QVector<QPointF>& points,
    const std::vector<std::array<int,4>>& cells,const std::vector<std::array<int,3>>& tris,
    const std::vector<int>& owners,const openswmmvis::io::Mesh2DScalarFrame& frame,
    const std::vector<double>& levels,bool labels) {
    ResultScalarGeometry out;using Status=openswmmvis::io::Mesh2DValueStatus;
    const auto fail=[&](const QString &error){out={};out.error=error;return out;};
    if(!frame.error.isEmpty())return fail(frame.error);
    if(frame.values.size()!=cells.size()||frame.status.size()!=cells.size()||tris.size()!=owners.size())return fail("Scalar geometry dimensions do not match.");
    if(levels.size()>256)return fail("Choose at most 256 contour levels.");
    for(double value:levels)if(!std::isfinite(value))return fail("A contour level is not finite.");
    std::vector<double> sum(size_t(points.size()),0),weight(size_t(points.size()),0);
    std::vector<bool> valid(cells.size(),false);
    for(size_t c=0;c<cells.size();++c){
        if(frame.status[c]!=Status::Valid||!std::isfinite(frame.values[c]))continue;
        const auto &ids=cells[c];const int n=ids[3]<0?3:4;
        for(int k=0;k<n;++k)if(ids[k]<0||ids[k]>=points.size()||!std::isfinite(points[ids[k]].x())||!std::isfinite(points[ids[k]].y()))return fail("Scalar cell geometry is invalid.");
        const auto origin=points[ids[0]];double twiceArea=0,cx=0,cy=0;
        for(int k=0;k<n;++k){const auto a=points[ids[k]]-origin,b=points[ids[(k+1)%n]]-origin;const double cross=a.x()*b.y()-a.y()*b.x();twiceArea+=cross;cx+=(a.x()+b.x())*cross;cy+=(a.y()+b.y())*cross;}
        if(!std::isfinite(twiceArea)||twiceArea==0)return fail("Scalar cell geometry has no finite area.");
        valid[c]=true;const double area=std::abs(twiceArea)*.5;
        if(!levels.empty())for(int k=0;k<n;++k){sum[size_t(ids[k])]+=double(frame.values[c])*area;weight[size_t(ids[k])]+=area;}
        if(labels)out.labels.append({origin+QPointF(cx/(3*twiceArea),cy/(3*twiceArea)),frame.values[c],int(c)});
    }
    if(levels.empty())return out;
    for(size_t v=0;v<sum.size();++v)sum[v]=weight[v]>0?sum[v]/weight[v]:std::numeric_limits<double>::quiet_NaN();
    std::vector<int> selected;selected.reserve(tris.size());
    for(size_t t=0;t<tris.size();++t){const int c=owners[t];if(c<0||size_t(c)>=cells.size())return fail("A display triangle has no scalar cell.");
        if(!valid[size_t(c)])continue;for(int v:tris[t])if(v<0||v>=points.size())return fail("A scalar triangle vertex is invalid.");selected.push_back(int(t));}
    // Bound both work and retained geometry, without silently omitting levels.
    if(!selected.empty()&&levels.size()>32000000/selected.size())return fail("Contour work exceeds 32 million triangle-level pairs; reduce levels or use cell labels.");
    const QPointF origin=points.isEmpty()?QPointF():points[0];
    for(size_t begin=0;begin<selected.size();begin+=1024) {
        const auto end=std::min(selected.size(),begin+1024);
        const std::vector<int> batch(selected.begin()+begin,selected.begin()+end);
        auto segments=OpenSWMM::Contour::marchingTriangles(batch,levels,[&](int t,QPointF&a,QPointF&b,QPointF&c,double&x,double&y,double&z){
            const auto &v=tris[size_t(t)];a=points[v[0]]-origin;b=points[v[1]]-origin;c=points[v[2]]-origin;x=sum[size_t(v[0])];y=sum[size_t(v[1])];z=sum[size_t(v[2])];});
        if(out.contours.size()+segments.size()>1500000)return fail("Contours exceed 1.5 million segments; reduce levels or use cell labels.");
        out.contours.insert(out.contours.end(),segments.begin(),segments.end());
    }
    for(auto &line:out.contours){line.a+=origin;line.b+=origin;}return out;
}
QVector<ResultScalarPlacedLabel> placeResultScalarLabels(const ResultScalarGeometry &geometry,
    const QTransform &transform,const QRectF &viewport,const QFont &font,int decimals,const QString &units,
    bool contours,int maximum) {
    QVector<ResultScalarPlacedLabel> result;if(!geometry.error.isEmpty()||maximum<=0)return result;
    const QFontMetricsF metrics(font);decimals=std::clamp(decimals,0,12);maximum=std::min(maximum,512);
    const auto add=[&](QPointF point,double value){
        if(result.size()>=maximum)return;
        const auto screen=transform.map(point);if(!viewport.isNull()&&!viewport.contains(screen))return;
        const QString text=QString::number(value,'f',decimals)+QStringLiteral(" [%1]").arg(units.isEmpty()?QStringLiteral("units unknown"):units);
        const QSizeF size(metrics.horizontalAdvance(text)+6,metrics.height()+4);const QRectF rect(screen-QPointF(size.width()/2,size.height()/2),size);
        if(!viewport.isNull()&&!viewport.contains(rect))return;
        for(const auto &placed:result)if(placed.rect.adjusted(-6,-4,6,4).intersects(rect))return;
        result.append({text,rect});
    };
    if(contours){for(const auto &line:geometry.contours){add((line.a+line.b)*.5,line.level);if(result.size()>=maximum)break;}}
    else for(const auto &label:geometry.labels){add(label.point,label.value);if(result.size()>=maximum)break;}
    return result;
}
}
