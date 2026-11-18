#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace AcousticClock {
struct Scratch { std::array<double,190> slopes; };
template<class T>
inline T middle(T* values, unsigned size) {
    const unsigned mid=size/2;
    std::nth_element(values,values+mid,values+size);
    if (size%2) return values[mid];
    return (values[mid]+*std::max_element(values,values+mid))*T(.5);
}
inline double q() { return std::sqrt(4.*.00360*.00589-.00380*.00380); }
inline double angle(double rpm) { return std::atan((2.*.00589*rpm/1000.+.00380)/q()); }
struct Clock { double end=0, span=0, count=0; };

template<class Get>
Clock fitted(unsigned count, Get get, unsigned trusted, Scratch& scratch) {
    if (trusted<6) return {};
    const unsigned step=(trusted+19)/20, first=(trusted-1)%step;
    std::array<double,20> times{},rpms{},angles{};
    unsigned seen=0, used=0;
    for(unsigned i=0;i<count;++i) {
        const auto s=get(i);
        if (!(s.rpm>0) || s.recovered || s.rejected) continue;
        const unsigned k=seen++;
        if (k<first || (k-first)%step) continue;
        times[used]=s.ms/1000.;rpms[used]=s.rpm;angles[used]=angle(s.rpm);
        ++used;
    }
    unsigned pairs=0;
    for(unsigned i=0;i<used;++i) for(unsigned j=0;j<i;++j) {
        const double dt=times[i]-times[j];
        if(dt>=1.5) scratch.slopes[pairs++]=(angles[i]-angles[j])/dt;
    }
    if(!pairs) return {};
    const double slope=middle(scratch.slopes.data(),pairs),scale=-2*slope/q();
    if(scale<.3 || scale>4) return {};
    std::array<double,20> offsets{};
    for(unsigned i=0;i<used;++i) offsets[i]=angles[i]-slope*times[i];
    const double intercept=middle(offsets.data(),used),finish=(angle(578)-intercept)/slope;
    if(finish<10 || finish>300) return {};
    unsigned matched=0;
    double begin=0,end=0;
    for(unsigned i=0;i<used;++i){
        const double rpm=(q()*std::tan(intercept+slope*times[i])-.00380)/.00001178;
        if(std::abs(rpm-rpms[i])>150) continue;
        if(!matched) begin=times[i];
        end=times[i];++matched;
    }
    if(matched<4 || matched*10<used*7) return {};
    return {finish,end-begin,double(trusted)*matched/used};
}

}
