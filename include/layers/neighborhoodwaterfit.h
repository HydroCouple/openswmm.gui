/* Shared surface fit for connected wet neighborhoods. GPL-3.0-or-later.
 * Included inside CellWaterGeometry after its Surface/CornerDepths definitions. */
#ifndef OPENSWMM_NEIGHBORHOODWATERFIT_H
#define OPENSWMM_NEIGHBORHOODWATERFIT_H

namespace NeighborhoodFit {

// Integral and derivative of max(q,0) on a reference triangle. Clipping keeps
// barycentric coordinates, so the derivative is the wet-area shape-function
// integral, including partly wet triangles (not a finite-difference estimate).
inline double triangleStorage(const std::array<double,3>& q, std::array<double,3>& derivative)
{
    struct Point { std::array<double,3> w; double q; };
    derivative={0,0,0};
    if (*std::max_element(q.begin(),q.end())<=0) return 0;
    if (*std::min_element(q.begin(),q.end())>=0) {
        derivative={1.0/3,1.0/3,1.0/3};return (q[0]+q[1]+q[2])/3;
    }
    std::array<Point,5> polygon{};int count=0;
    for(int k=0;k<3;++k) {
        const int j=(k+1)%3;
        Point a{{0,0,0},q[k]},b{{0,0,0},q[j]};a.w[k]=1;b.w[j]=1;
        if(a.q>=0)polygon[count++]=a;
        if((a.q<0)!=(b.q<0)) {
            const double t=a.q/(a.q-b.q);Point p{{0,0,0},0};
            for(int v=0;v<3;++v)p.w[v]=a.w[v]+t*(b.w[v]-a.w[v]);
            polygon[count++]=p;
        }
    }
    double storage=0;
    for(int k=1;k+1<count;++k) {
        const auto &a=polygon[0],&b=polygon[k],&c=polygon[k+1];
        const double fraction=std::abs((b.w[1]-a.w[1])*(c.w[2]-a.w[2])
                                     -(b.w[2]-a.w[2])*(c.w[1]-a.w[1]));
        storage+=fraction*(a.q+b.q+c.q)/3;
        for(int v=0;v<3;++v)derivative[v]+=fraction*(a.w[v]+b.w[v]+c.w[v])/3;
    }
    return storage;
}

struct Cell {
    int source=0, component=0, count=3, halves=1;
    std::array<int,4> variable{};
    std::array<std::array<int,3>,2> local{};
    std::array<double,2> fraction{1,0};
    double area=0, depth=0, weight=0;
};

inline double storage(const Cell& cell,const std::vector<double>& field,
                      std::array<double,4>& derivative,double offset=0,
                      double cap=std::numeric_limits<double>::infinity())
{
    derivative={0,0,0,0};double mean=0;
    for(int s=0;s<cell.halves;++s) {
        std::array<double,3> q{},dq{};
        for(int k=0;k<3;++k)q[k]=std::min(cap,field[size_t(cell.variable[cell.local[s][k]])]+offset);
        mean+=cell.fraction[s]*triangleStorage(q,dq);
        for(int k=0;k<3;++k)derivative[cell.local[s][k]]+=cell.fraction[s]*dq[k];
    }
    return mean;
}

// Small matrix-free Gauss-Newton fit. One variable belongs to one connected
// vertex fan, shared by BOTH traces of every supported edge. Storage rows use
// original volumes and the original quad diagonal. A weak neighborhood prior
// selects the unsupported degrees of freedom; it is not a temporal filter.
inline std::vector<CornerDepths> reconstruct(
    const std::vector<VertexDepthReconstruct::CellSplit>& cells,
    const std::vector<Surface>& surfaces,const std::vector<double>& z,
    const SmoothTopology& topology,const std::vector<float>& depths,
    const std::vector<float>& flux,VisibilityPolicy policy)
{
    const size_t n=cells.size();
    if(depths.size()!=n || surfaces.size()!=n || flux.size()!=4*n)return {};
    const double nan=std::numeric_limits<double>::quiet_NaN();
    std::vector<CornerDepths> result(n,CornerDepths{nan,nan,nan,nan});
    std::vector<bool> eligible(n,false),connected(n,false);
    std::vector<int> parent(4*n),component(n);
    for(size_t i=0;i<parent.size();++i)parent[i]=int(i);
    for(size_t i=0;i<n;++i)component[i]=int(i);
    auto root=[](std::vector<int>& p,int i) {
        while(p[size_t(i)]!=i){p[size_t(i)]=p[size_t(p[size_t(i)])];i=p[size_t(i)];}return i;
    };
    auto join=[&](std::vector<int>& p,int a,int b) {
        a=root(p,a);b=root(p,b);if(a!=b)p[size_t(std::max(a,b))]=std::min(a,b);
    };
    double bound=0;
    for(size_t c=0;c<n;++c) {
        const auto& cell=cells[c];
        if(!visible(displayState(cell,surfaces[c],z,policy),policy))continue;
        const double area=cell.area[0]+(cell.nSub==2?cell.area[1]:0);
        if(!(area>0) || !std::isfinite(area) || !(depths[c]>0))continue;
        eligible[c]=true;
        for(int k=0;k<cell.vertexCount();++k) {
            result[c][k]=surfaces[c].signedDepth(z[size_t(cell.v[k])]);
            bound=std::max(bound,result[c][k]);
            eligible[c]=eligible[c] && std::isfinite(flux[4*c+k]);
        }
    }
    auto slot=[&](int a,int b) {
        const int nv=cells[size_t(a/4)].vertexCount();
        const int start=(a%4+1)%nv==b%4?a%4:b%4;
        return 4*(a/4)+(start+nv-1)%nv;
    };
    bool any=false;
    for(const auto& e:topology.edges) {
        const int a=e.a0/4,b=e.a1/4;
        if(!eligible[size_t(a)] || !eligible[size_t(b)])continue;
        const double qa=flux[size_t(slot(e.a0,e.b0))],qb=flux[size_t(slot(e.a1,e.b1))];
        if(!(qa*qb<0) || std::abs(qa+qb)>1e-4*std::max(std::abs(qa),std::abs(qb)))continue;
        join(parent,e.a0,e.a1);join(parent,e.b0,e.b1);join(component,a,b);
        connected[size_t(a)]=connected[size_t(b)]=true;any=true;
    }
    if(!any)return {};
    // Still-water connections use the existing positive-overlap rule. Flow
    // evidence additionally connects edges which the isolated VFR masks made
    // dry. A crest with neither support remains a separate vertex fan.
    for(const auto& e:topology.edges) {
        const size_t a=size_t(e.a0/4),b=size_t(e.a1/4);
        if(!eligible[a] || !eligible[b])continue;
        double lo=0,hi=1;
        auto wet=[&](double q0,double q1) {
            if(!(std::max(q0,q1)>0))return false;
            if(q0<0)lo=std::max(lo,q0/(q0-q1));
            if(q1<0)hi=std::min(hi,q0/(q0-q1));
            return hi>lo;
        };
        if(!wet(result[a][e.a0%4],result[a][e.b0%4])
           || !wet(result[b][e.a1%4],result[b][e.b1%4]))continue;
        join(parent,e.a0,e.a1);join(parent,e.b0,e.b1);join(component,int(a),int(b));
        connected[a]=connected[b]=true;
    }

    std::vector<int> rootVariable(4*n,-1),group(n,-1);
    std::vector<Cell> rows;
    std::vector<double> pond,flow,weight,prior;
    std::vector<int> variableGroup;
    int groups=0;
    for(size_t c=0;c<n;++c) {
        if(!connected[c])continue;
        const auto& source=cells[c];Cell row;
        row.source=int(c);row.count=source.vertexCount();row.halves=source.nSub;
        row.area=source.area[0]+(source.nSub==2?source.area[1]:0);row.depth=depths[c];
        const double scale=std::max({row.depth,policy.filmDepth,1e-6});
        row.weight=row.area/(scale*scale);
        const int comp=root(component,int(c));
        if(group[size_t(comp)]<0)group[size_t(comp)]=groups++;
        row.component=group[size_t(comp)];
        for(int k=0;k<row.count;++k) {
            const int r=root(parent,4*int(c)+k);
            if(rootVariable[size_t(r)]<0) {
                rootVariable[size_t(r)]=int(pond.size());
                pond.push_back(0);flow.push_back(0);weight.push_back(0);prior.push_back(0);
                variableGroup.push_back(row.component);
            }
            const int v=row.variable[k]=rootVariable[size_t(r)];
            const double w=row.area/row.count;
            pond[size_t(v)]+=w*result[c][k];flow[size_t(v)]+=w*row.depth;
            weight[size_t(v)]+=w;prior[size_t(v)]+=0.0025*row.weight/row.count;
        }
        for(int s=0;s<row.halves;++s) {
            row.fraction[s]=source.area[s]/row.area;
            for(int k=0;k<3;++k)row.local[s][k]=int(std::find(source.v.begin(),source.v.end(),source.sub[s][k])-source.v.begin());
        }
        rows.push_back(row);
    }
    for(size_t v=0;v<pond.size();++v){pond[v]/=weight[v];flow[v]/=weight[v];}
    std::vector<double> pondCost(pond.size(),0),flowCost(pond.size(),0),target(size_t(groups),0);
    for(const auto& row:rows) {
        std::array<double,4> derivative;
        const double a=storage(row,pond,derivative)-row.depth,b=storage(row,flow,derivative)-row.depth;
        for(int k=0;k<row.count;++k) {
            const size_t v=size_t(row.variable[k]);
            pondCost[v]+=row.weight*a*a/row.count;flowCost[v]+=row.weight*b*b/row.count;
        }
        target[size_t(row.component)]+=row.area*row.depth;
    }
    // A level lake's clipped-volume residual is zero; a moving uniform sheet's
    // depth residual is zero. Blend the two neighborhood references by their
    // measured storage residuals on the incident cell fan. Continuous weights avoid a
    // mode switch when their relative quality changes between saved frames.
    std::vector<double> reference(pond.size());
    for(size_t v=0;v<reference.size();++v) {
        const double total=pondCost[v]+flowCost[v];
        const double moving=total>0?pondCost[v]/total:0;
        reference[v]=pond[v]+moving*(flow[v]-pond[v]);
    }
    auto cost=[&](const std::vector<double>& field) {
        double sum=0;std::array<double,4> derivative;
        for(const auto& row:rows){const double r=storage(row,field,derivative)-row.depth;sum+=row.weight*r*r;}
        for(size_t v=0;v<field.size();++v)sum+=prior[v]*(field[v]-reference[v])*(field[v]-reference[v]);
        return sum;
    };
    std::vector<double> field=reference;
    std::vector<std::array<double,4>> jacobian(rows.size());
    const size_t nv=field.size();
    std::vector<double> rhs(nv),diagonal(nv),delta(nv),r(nv),p(nv),product(nv),preconditioned(nv);
    std::vector<double> massGradient(nv),massDirection(nv),massResidual(size_t(groups),0);
    auto dot=[](const std::vector<double>& a,const std::vector<double>& b) {
        double sum=0;for(size_t i=0;i<a.size();++i)sum+=a[i]*b[i];return sum;
    };
    for(int iteration=0;iteration<6;++iteration) {
        diagonal=prior;
        std::fill(massGradient.begin(),massGradient.end(),0);
        for(int g=0;g<groups;++g)massResidual[size_t(g)]=-target[size_t(g)];
        for(size_t v=0;v<nv;++v)rhs[v]=-prior[v]*(field[v]-reference[v]);
        for(size_t i=0;i<rows.size();++i) {
            const auto& row=rows[i];const double residual=storage(row,field,jacobian[i])-row.depth;
            massResidual[size_t(row.component)]+=row.area*(residual+row.depth);
            for(int k=0;k<row.count;++k) {
                const size_t v=size_t(row.variable[k]);const double j=jacobian[i][k];
                rhs[v]-=row.weight*j*residual;diagonal[v]+=row.weight*j*j;
                massGradient[v]+=row.area*j;
            }
        }
        auto multiply=[&](const std::vector<double>& a,std::vector<double>& b) {
            for(size_t v=0;v<nv;++v)b[v]=prior[v]*a[v];
            for(size_t i=0;i<rows.size();++i) {
                const auto& row=rows[i];double s=0;
                for(int k=0;k<row.count;++k)s+=jacobian[i][k]*a[size_t(row.variable[k])];
                for(int k=0;k<row.count;++k)b[size_t(row.variable[k])]+=row.weight*jacobian[i][k]*s;
            }
        };
        auto solve=[&](const std::vector<double>& source,std::vector<double>& solution) {
            std::fill(solution.begin(),solution.end(),0);r=source;
            for(size_t v=0;v<nv;++v)preconditioned[v]=r[v]/diagonal[v];
            p=preconditioned;double residual=dot(r,preconditioned),initial=residual;
            for(int cg=0;cg<40 && residual>initial*1e-12 && residual>1e-24;++cg) {
                multiply(p,product);const double denom=dot(p,product);if(!(denom>0))break;
                const double alpha=residual/denom;
                for(size_t v=0;v<nv;++v){solution[v]+=alpha*p[v];r[v]-=alpha*product[v];preconditioned[v]=r[v]/diagonal[v];}
                const double next=dot(r,preconditioned),beta=next/residual;
                for(size_t v=0;v<nv;++v)p[v]=preconditioned[v]+beta*p[v];residual=next;
            }
        };
        // Enforce the linearized component volume with a Lagrange multiplier.
        // A post-fit global offset alone would inundate shallow neighbours.
        solve(rhs,delta);solve(massGradient,massDirection);
        std::vector<double> numerator=massResidual,denominator(size_t(groups),0);
        for(size_t v=0;v<nv;++v) {
            const size_t g=size_t(variableGroup[v]);
            numerator[g]+=massGradient[v]*delta[v];denominator[g]+=massGradient[v]*massDirection[v];
        }
        for(size_t v=0;v<nv;++v) {
            const size_t g=size_t(variableGroup[v]);
            if(denominator[g]>0)delta[v]-=numerator[g]/denominator[g]*massDirection[v];
        }
        // Include conservation in the line search: a necessary volume repair
        // can temporarily increase the unconstrained storage residual.
        auto merit=[&](const std::vector<double>& candidate) {
            double value=cost(candidate);std::vector<double> residual(size_t(groups),0);
            for(const auto& row:rows) {
                std::array<double,4> derivative;
                residual[size_t(row.component)]+=row.area*(storage(row,candidate,derivative)-row.depth);
            }
            for(int g=0;g<groups;++g)if(denominator[size_t(g)]>0) {
                const double m=residual[size_t(g)],d=denominator[size_t(g)];
                value+=(2*std::abs(numerator[size_t(g)]*m)+m*m)/d;
            }
            return value;
        };
        const double previous=merit(field);bool accepted=false;
        for(double step=1;step>=1.0/128;step*=0.5) {
            std::vector<double> candidate(nv);
            for(size_t v=0;v<nv;++v)candidate[v]=std::min(bound,field[v]+step*delta[v]);
            if(merit(candidate)<previous){field.swap(candidate);accepted=true;break;}
        }
        if(!accepted || previous-merit(field)<1e-10*std::max(previous,1e-10))break;
    }

    // Close total stored volume per neighborhood. Scale its positive depth
    // field rather than adding a blanket film to every shallow cell. Negative
    // corners approach zero only when more storage is required. This monotone
    // closure retains shared traces and the frame's original depth bound.
    auto scaled=[&](double q,double scale) {
        return q>=0?std::min(bound,q*scale):q/std::max(1.0,scale);
    };
    auto massFor=[&](const std::vector<double>& scale,std::vector<double>& mass) {
        std::fill(mass.begin(),mass.end(),0);
        for(const auto& row:rows) {
            double mean=0;
            for(int s=0;s<row.halves;++s) {
                std::array<double,3> q{},derivative{};
                for(int k=0;k<3;++k)q[k]=scaled(field[size_t(row.variable[row.local[s][k]])],scale[size_t(row.component)]);
                mean+=row.fraction[s]*triangleStorage(q,derivative);
            }
            mass[size_t(row.component)]+=row.area*mean;
        }
    };
    std::vector<double> lo(size_t(groups),0),hi(size_t(groups),1),scale(size_t(groups),1),mass(size_t(groups),0);
    for(int iteration=0;iteration<40;++iteration) {
        massFor(hi,mass);bool bracketed=true;
        for(int g=0;g<groups;++g)if(mass[size_t(g)]<target[size_t(g)]) {
            hi[size_t(g)]*=2;bracketed=false;
        }
        if(bracketed)break;
    }
    massFor(hi,mass);
    bool fallback=false;
    for(size_t v=0;v<nv;++v) {
        const size_t g=size_t(variableGroup[v]);
        if(mass[g]<target[g]){field[v]=flow[v];hi[g]=1;fallback=true;}
    }
    if(fallback)for(int iteration=0;iteration<40;++iteration) {
        massFor(hi,mass);bool bracketed=true;
        for(int g=0;g<groups;++g)if(mass[size_t(g)]<target[size_t(g)]) {
            hi[size_t(g)]*=2;bracketed=false;
        }
        if(bracketed)break;
    }
    for(int iteration=0;iteration<40;++iteration) {
        for(int g=0;g<groups;++g)scale[size_t(g)]=(lo[size_t(g)]+hi[size_t(g)])/2;
        massFor(scale,mass);
        for(int g=0;g<groups;++g) {
            if(mass[size_t(g)]<target[size_t(g)])lo[size_t(g)]=scale[size_t(g)];else hi[size_t(g)]=scale[size_t(g)];
        }
    }
    for(const auto& row:rows)for(int k=0;k<row.count;++k)
        result[size_t(row.source)][k]=scaled(field[size_t(row.variable[k])],scale[size_t(row.component)]);
    return result;
}
} // namespace NeighborhoodFit
#endif
