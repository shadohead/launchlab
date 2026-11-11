#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

// Independent of acquisition, display, RTOS and persistence. Units: kRPM, seconds.
namespace Decay {
constexpr unsigned MODEL_COUNT=4,MAX_SAMPLES=3000,CURVE_POINTS=128;
enum class Model:uint8_t {Linear,Quadratic,Riccati,Power};
inline const char *modelName(unsigned index){
  static const char *names[]={"Coulomb-Viscous Model","Coulomb-Quadratic Model","Riccati Model","Power-Law Damping Model"};
  return index<MODEL_COUNT?names[index]:"";
}
struct Params {float a=0,b=0,c=0,k=0,n=1;};
struct Config {
  float huber=.15f,step=.1f,aFloor=.0001f,maxCoefficient=10,nMin=.5f,nMax=3;
  float maxStop=3600,lossTolerance=.000001f,validationTieRpm=15;
  unsigned evaluations=480,stopResolution=256,stopMaxResolution=8192;
};
struct Sample {uint32_t ms=0;float rpm=0;bool rejected=false,excludedFromFit=false;};
struct Session {
  float launcherRpm=0;uint32_t launcherMs=0,stopMs=0;bool hasStop=false;
  unsigned count=0;Sample samples[MAX_SAMPLES];
};
struct Observation {float t=0,x=0,w=0;};
struct Workspace {
  Observation points[MAX_SAMPLES];
  static constexpr unsigned initialWindow=15;
  float scratch[initialWindow*(initialWindow-1)/2]; // One slope per pair in the initial window.
  unsigned count=0;
  float startOffset=0,stopDuration=0;
};
struct Metric {bool valid=false;float rmse=0,mae=0,maxError=0;};
struct Result {
  Model model=Model::Linear;Params params;
  bool valid=false,converged=false,validationConverged=false,boundary=false,unstable=false;
  bool stopValid=false,stopErrorValid=false;
  Metric training,validation,all;
  float stopSeconds=0,stopError=0,absoluteStopError=0,loss=0;
  unsigned evaluations=0,sampleCount=0;
};
struct Results {
  static constexpr uint32_t VERSION=1;
  Result models[MODEL_COUNT];
  float acousticStart=0,initialRpm=0,launcherRpm=0,launcherTime=0,lastTime=0,actualStop=0;
  float tailRpm=0,tailSlope=0; // Last included reading and incoming slope, RPM and RPM/s.
  bool opticalAnchor=false,manualAnchor=false,actualStopValid=false,invalidStop=false,validationPossible=false,cancelled=false;
  unsigned sourceCount=0,sampleCount=0,spikes=0;
  int best=-1;uint8_t ranking[MODEL_COUNT]={0,1,2,3};
};
struct Curve {
  float end=0,joinTime=0;
  unsigned joinIndex=0;
  float rpm[CURVE_POINTS]={};
  float seconds(unsigned i)const {
    const unsigned last=CURVE_POINTS-1;
    if(i==last)return end;
    if(!joinIndex)return end*i/last;
    if(i==joinIndex)return joinTime;
    if(i<joinIndex)return joinTime*(float(i)/joinIndex);
    return joinTime+(end-joinTime)*(float(i-joinIndex)/(last-joinIndex));
  }
};
using Continue=bool(*)(void*);
inline bool proceed(Continue fn,void *ctx){return !fn || fn(ctx);}
inline float deceleration(Model m,const Params &p,float x) {
  x=std::max(0.f,x);
  switch(m){case Model::Linear:return p.a+p.b*x;case Model::Quadratic:return p.a+p.c*x*x;
    case Model::Riccati:return p.a+p.b*x+p.c*x*x;default:return p.a+p.k*std::pow(x,p.n);}
}
inline bool validParams(Model m,const Params &p,const Config &cfg={}) {
  const float values[]={p.a,p.b,p.c,p.k,p.n};for(float v:values)if(!std::isfinite(v))return false;
  return p.a>=cfg.aFloor && p.a<=cfg.maxCoefficient && p.b>=0 && p.b<=cfg.maxCoefficient &&
    p.c>=0 && p.c<=cfg.maxCoefficient && p.k>=0 && p.k<=cfg.maxCoefficient &&
    (m!=Model::Power || (p.n>=cfg.nMin && p.n<=cfg.nMax));
}
inline float advance(Model m,const Params &p,float x,float dt,const Config &cfg,Continue fn=nullptr,void *ctx=nullptr) {
  if(!std::isfinite(x) || !std::isfinite(dt) || dt<0 || cfg.step<=0)return NAN;
  unsigned ticks=0;
  while(dt>0 && x>0){
    if(!(ticks++%32) && !proceed(fn,ctx))return NAN;
    // Bound relative change as well as wall time to avoid stiff-parameter overshoot.
    const float speed=deceleration(m,p,x);
    if(!std::isfinite(speed) || speed<=0)return NAN;
    if(x<.00001f){x=0;break;}
    const float h=std::min(dt,std::min(cfg.step,std::max(.00001f,.2f*x/speed)));
    const float k1=speed,k2=deceleration(m,p,std::max(0.f,x-h*k1/2)),
      k3=deceleration(m,p,std::max(0.f,x-h*k2/2)),k4=deceleration(m,p,std::max(0.f,x-h*k3));
    const float next=x-h*(k1+2*k2+2*k3+k4)/6;
    if(!std::isfinite(next))return NAN;
    x=std::max(0.f,next);dt-=h;
    if(ticks>200000)return NAN;
  }
  return x;
}
inline float predictRpm(Model m,const Params &p,float initialRpm,float elapsed,const Config &cfg={}) {
  if(!validParams(m,p,cfg) || !std::isfinite(initialRpm) || initialRpm<0)return NAN;
  return 1000*advance(m,p,initialRpm/1000,elapsed,cfg);
}
inline float predictRpm(const Result &r,float initialRpm,float elapsed,const Config &cfg={}) {
  return r.valid?predictRpm(r.model,r.params,initialRpm,elapsed,cfg):NAN;
}
inline float getDeceleration(const Result &r,float rpm){return r.valid?1000*deceleration(r.model,r.params,rpm/1000):NAN;}
inline float stopIntegral(Model m,const Params &p,float x0,unsigned n) {
  const double h=double(x0)/n;double sum=0;
  for(unsigned i=0;i<=n;++i){float f=deceleration(m,p,float(h*i));if(!std::isfinite(f) || f<=0)return NAN;
    sum+=(i==0 || i==n?1:i%2?4:2)/double(f);}
  return float(sum*h/3);
}
inline float predictStopTime(const Result &r,float initialRpm,const Config &cfg={}) {
  if(!r.valid || !validParams(r.model,r.params,cfg) || !std::isfinite(initialRpm) || initialRpm<=0)return NAN;
  unsigned n=std::max(2u,cfg.stopResolution);n+=n%2;
  float previous=stopIntegral(r.model,r.params,initialRpm/1000,n);
  for(n*=2;n<=cfg.stopMaxResolution;n*=2){
    const float next=stopIntegral(r.model,r.params,initialRpm/1000,n);
    if(!std::isfinite(next))return NAN;
    if(std::fabs(next-previous)<=.02f+.005f*next)return next<=cfg.maxStop?next:NAN;
    previous=next;
  }
  return NAN; // Resolution insufficient: don't publish an unreliable finite stop.
}
inline float median(float *v,unsigned n){std::nth_element(v,v+n/2,v+n);return v[n/2];}
// Classify upward jumps on the stopped trace, where later evidence is available.
// Observations are sorted and unique. Compact in place, always writing behind the reader.
inline unsigned selectDecayTrack(Observation *points,unsigned count,unsigned &spikes) {
  unsigned kept=0,read=0;
  while(read<count) {
    const auto first=points[read];
    const float last=kept?points[kept-1].x:0;
    constexpr float maxGapSeconds=2.f;
    const bool afterGap=kept && first.t-points[kept-1].t>maxGapSeconds;
    float baseline=last;
    if(kept>=3 && !afterGap){
      // On a falling trace the older median can hide a rebound. Retain
      // median protection for a lone downward glitch on a level trace.
      if(points[kept-3].x<=points[kept-2].x){
        float recent[]={points[kept-3].x,points[kept-2].x,last};
        baseline=median(recent,3);
      }
    }
    const float jump=first.x-baseline;
    // After silence even a small upward reading needs corroboration.
    // At normal cadence allow ordinary microphone jitter.
    const float tolerance=afterGap?0.f:std::max(.08f,.02f*baseline);
    if(!kept || jump<=tolerance) {
      points[kept++]=points[read++];
      continue;
    }

    const float noise=std::max(.08f,.02f*first.x);
    const float requiredDrop=std::max(.20f,.04f*first.x);
    unsigned end=read+1;
    bool confirmed=false;
    float low=first.x, lastDecline=first.x;
    unsigned declines=0;
    while(end<count) {
      const auto &next=points[end];
      // A jump straight back to the original track cannot confirm the high run.
      if(next.t-points[end-1].t>maxGapSeconds || next.x>low+noise ||
         (next.x<=baseline && declines<2) ||
         points[end-1].x-next.x>.75f*jump)break;
      low=std::min(low,next.x);
      ++end;
      if(lastDecline-low>=requiredDrop*.5f){
        ++declines;
        lastDecline=low;
      }
      if(end-read>=4 && declines>=2) {
        confirmed=true;
        break;
      }
    }
    if(confirmed) {
      kept=0;
      while(read<end)points[kept++]=points[read++];
    } else {
      spikes+=end-read;
      read=end;
    }
  }
  return kept;
}
inline void prepare(const Session &s,Workspace &w,Results &out,const Config &cfg) {
  w.count=0;out.sourceCount=std::min(s.count,MAX_SAMPLES);out.launcherRpm=s.launcherRpm;out.launcherTime=s.launcherMs/1000.f;
  out.opticalAnchor=std::isfinite(s.launcherRpm) && s.launcherRpm>0;
  const bool manual=s.hasStop && s.stopMs>s.launcherMs;
  w.startOffset=w.stopDuration=0;
  bool chronological=true;
  for(unsigned i=0;i<out.sourceCount;++i){
    const auto &sample=s.samples[i];
    if(!std::isfinite(sample.rpm) || sample.rpm<=0 || sample.rpm>65535 ||
       sample.ms<s.launcherMs || (manual && sample.ms>=s.stopMs))continue;
    const float time=sample.ms/1000.f;
    if(w.count && time<=w.points[w.count-1].t)chronological=false;
    w.points[w.count++]={time,sample.rpm/1000.f,0};
  }
  // Device snapshots have strictly increasing timestamps. Other callers can
  // supply unordered or repeated observations; keep the median for each time.
  if(!chronological){
    auto byTime=[](const Observation &a,const Observation &b){return a.t<b.t;};
    auto byRpm=[](const Observation &a,const Observation &b){return a.x<b.x;};
    std::sort(w.points,w.points+w.count,byTime);
    unsigned unique=0;
    for(unsigned first=0;first<w.count;){
      unsigned end=first+1;
      while(end<w.count && w.points[end].t==w.points[first].t)++end;
      const unsigned middle=first+(end-first)/2;
      if(end-first>1)std::nth_element(w.points+first,w.points+middle,w.points+end,byRpm);
      w.points[unique++]=w.points[middle];
      first=end;
    }
    w.count=unique;
  }
  const unsigned kept=selectDecayTrack(w.points,w.count,out.spikes);
  w.count=kept;
  out.sampleCount=kept;
  out.acousticStart=out.lastTime=out.launcherTime;out.actualStop=s.stopMs/1000.f;
  out.invalidStop=s.hasStop && !manual;
  out.actualStopValid=out.manualAnchor=manual;
  if(kept){
    out.acousticStart=w.points[0].t;
    out.lastTime=w.points[kept-1].t;
  }
  const float origin=out.opticalAnchor?out.launcherTime:out.acousticStart;
  w.startOffset=out.acousticStart-origin;
  if(manual)w.stopDuration=out.actualStop-origin;
  if(!kept)return;
  for(unsigned i=0;i<kept;++i)w.points[i].t-=out.acousticStart;
  // Detrend the first five observations to estimate the initial acoustic RPM robustly.
  unsigned rates=0;const unsigned window=std::min(Workspace::initialWindow,kept*4/5);
  for(unsigned i=0;i<window;++i)for(unsigned j=i+1;j<window;++j){float dt=w.points[j].t-w.points[i].t;
    if(dt>=.3f)w.scratch[rates++]=(w.points[i].x-w.points[j].x)/dt;}
  const float rate=rates?std::max(0.f,median(w.scratch,rates)):0;
  const unsigned initial=std::min(5u,kept);
  for(unsigned i=0;i<initial;++i)w.scratch[i]=w.points[i].x+rate*w.points[i].t;
  out.initialRpm=1000*median(w.scratch,initial);
  for(unsigned i=0;i<kept;++i){const float left=i?w.points[i].t-w.points[i-1].t:kept>1?w.points[1].t:1;
    const float right=i+1<kept?w.points[i+1].t-w.points[i].t:left;
    w.points[i].w=std::max(.001f,(left+right)/2);}
}
inline double huber(double residual,float delta){double e=std::fabs(residual);return e<=delta?.5*e*e:delta*(e-.5*delta);}
// For long traces, integrate dt = -dx / drag(x) on a fixed RPM grid.
// Both the grid and the observations are traversed once per trial. A cubic
// interpolant uses the ODE slopes at each end of a bin. Final metrics and
// display curves still use RK4 with the fitted coefficients.
inline double gridLoss(Model model,const Params &params,float initial,const Workspace &w,unsigned count,
                       const Config &cfg,Continue fn,void *ctx) {
  constexpr unsigned bins=128;
  unsigned observation=0;
  double loss=0,weight=0;
  auto accumulate=[&](float prediction){
    const auto &point=w.points[observation];
    const float density=observation+1==count && observation?
      std::max(.001f,point.t-w.points[observation-1].t):point.w;
    loss+=density*huber(prediction-point.x,cfg.huber);
    weight+=density;
    ++observation;
  };
  float high=initial,highRate=deceleration(model,params,high);
  double startTime=0;
  for(unsigned bin=bins;bin && observation<count;--bin){
    if(bin%16==0 && !proceed(fn,ctx))return 1e100;
    const float fraction=float(bin-1)/bins;
    const float low=initial*fraction*fraction;
    const float lowRate=deceleration(model,params,low);
    const float midRate=deceleration(model,params,.5f*(high+low));
    const float width=high-low;
    const float duration=width*(1/highRate+4/midRate+1/lowRate)/6;
    const double endTime=startTime+duration;
    while(observation<count){
      const double time=w.points[observation].t+w.startOffset;
      if(time>endTime)break;
      const float u=float((time-startTime)/duration),v=1-u;
      const float prediction=low+width*v*v*(1+2*u)+duration*u*v*(u*lowRate-v*highRate);
      accumulate(std::clamp(prediction,low,high));
    }
    high=low;highRate=lowRate;startTime=endTime;
  }
  while(observation<count)accumulate(0);
  return loss/weight;
}
inline double objective(Model m,const Params &p,float x0,const Workspace &w,unsigned count,const Config &cfg,Continue fn,void *ctx) {
  if(!validParams(m,p,cfg) || !proceed(fn,ctx))return 1e100;
  if(count>=48)return gridLoss(m,p,x0,w,count,cfg,fn,ctx);
  float x=x0,t=-w.startOffset;double sum=0,weight=0;
  for(unsigned i=0;i<count;++i){const auto &v=w.points[i];x=advance(m,p,x,v.t-t,cfg,fn,ctx);t=v.t;
    if(!std::isfinite(x))return 1e100;
    const float density=i+1==count && i?std::max(.001f,v.t-w.points[i-1].t):v.w;
    sum+=density*huber(v.x-x,cfg.huber);weight+=density;}
  return weight?sum/weight:1e100;
}
struct Candidate {float q[3]={};double loss=1e100;};
inline Params parameters(Model m,const float *q,float x0,float scale,const Config &cfg) {
  Params p;p.a=std::max(cfg.aFloor,q[0]*scale);
  if(m==Model::Linear)p.b=q[1]*scale/x0;
  else if(m==Model::Quadratic)p.c=q[1]*scale/(x0*x0);
  else if(m==Model::Riccati){p.b=q[1]*scale/x0;p.c=q[2]*scale/(x0*x0);}
  else {p.n=q[2];p.k=q[1]*scale/std::pow(x0,p.n);}
  return p;
}
inline Result optimize(Model m,const Workspace &w,unsigned count,float x0,const Config &cfg,Continue fn,void *ctx) {
  Result result;result.model=m;result.sampleCount=count;
  const bool anchored=w.stopDuration>0;
  const bool power=m==Model::Power;
  const unsigned exponentIndex=anchored?1:2;
  const unsigned dimension=2+(power || m==Model::Riccati)-unsigned(anchored);
  if(count<6 || w.points[count-1].t+w.startOffset<=0 || x0<=0 || !cfg.evaluations)return result;
  const float scale=std::max(.005f,(x0-w.points[count-1].x)/(w.points[count-1].t+w.startOffset));
  // Scaling every drag coefficient by the same factor divides time-to-zero
  // by that factor. Search only the independent shape parameters when timed.
  auto coefficients=[&](const float *q){
    if(!anchored)return parameters(m,q,x0,scale,cfg);
    Params p;p.a=1;
    const float drag=std::expm1(q[0]);
    if(m==Model::Linear || m==Model::Riccati)p.b=drag/x0;
    if(m==Model::Quadratic)p.c=drag/(x0*x0);
    if(m==Model::Riccati)p.c=std::expm1(q[1])/(x0*x0);
    if(power){p.n=q[1];p.k=drag/std::pow(x0,p.n);}
    float integral=x0;
    if(m==Model::Linear && drag>0)integral=x0*std::log1p(drag)/drag;
    else if(m==Model::Quadratic && drag>0)integral=x0*std::atan(std::sqrt(drag))/std::sqrt(drag);
    else if(m==Model::Riccati || power){
      float previous=stopIntegral(m,p,x0,16);
      integral=NAN;
      for(unsigned n=32;n<=cfg.stopMaxResolution;n*=2){
        if(!proceed(fn,ctx))break;
        const float next=stopIntegral(m,p,x0,n);
        if(std::fabs(next-previous)<=1e-5f*next){integral=next;break;}
        previous=next;
      }
    }
    const float factor=integral/w.stopDuration;
    p.a*=factor;p.b*=factor;p.c*=factor;p.k*=factor;
    return p;
  };
  Candidate global;bool globalConverged=false;float minimumStop=INFINITY,maximumStop=0;
  const unsigned starts=3,budget=cfg.evaluations/starts;
  if(budget<dimension+1)return result;
  auto evaluate=[&](Candidate &v){for(unsigned j=0;j<dimension;++j)v.q[j]=std::clamp(v.q[j],power && j==exponentIndex?cfg.nMin:0.f,power && j==exponentIndex?cfg.nMax:8.f);
    v.loss=objective(m,coefficients(v.q),x0,w,count,cfg,fn,ctx);++result.evaluations;};
  for(unsigned start=0;start<starts;++start){
    if(!proceed(fn,ctx))return result;
    Candidate simplex[4];simplex[0].q[0]=start==0?.8f:start==1?.2f:.45f;simplex[0].q[1]=1-simplex[0].q[0];
    simplex[0].q[2]=m==Model::Power?1.f+.5f*start:m==Model::Riccati?.3f:0;
    if(m==Model::Riccati)simplex[0].q[1]*=.5f;
    if(anchored){simplex[0].q[0]=.2f+start;simplex[0].q[1]=power?1.f+.5f*start:.4f;}
    evaluate(simplex[0]);for(unsigned j=1;j<=dimension;++j){simplex[j]=simplex[0];simplex[j].q[j-1]+=.2f;evaluate(simplex[j]);}
    unsigned calls=dimension+1;bool converged=false;
    while(calls+2<=budget){
      std::sort(simplex,simplex+dimension+1,[](const Candidate&a,const Candidate&b){return a.loss<b.loss;});
      float spread=0;for(unsigned i=1;i<=dimension;++i)for(unsigned j=0;j<dimension;++j)spread=std::max(spread,std::fabs(simplex[i].q[j]-simplex[0].q[j]));
      if(simplex[0].loss<1e90 && simplex[dimension].loss-simplex[0].loss<cfg.lossTolerance && spread<.03f){converged=true;break;}
      if(!proceed(fn,ctx))return result;
      float center[3]={};for(unsigned i=0;i<dimension;++i)for(unsigned j=0;j<dimension;++j)center[j]+=simplex[i].q[j]/dimension;
      Candidate reflected;for(unsigned j=0;j<dimension;++j)reflected.q[j]=2*center[j]-simplex[dimension].q[j];evaluate(reflected);++calls;
      if(reflected.loss<simplex[0].loss){Candidate expanded;for(unsigned j=0;j<dimension;++j)expanded.q[j]=center[j]+2*(reflected.q[j]-center[j]);evaluate(expanded);++calls;simplex[dimension]=expanded.loss<reflected.loss?expanded:reflected;}
      else if(reflected.loss<simplex[dimension-1].loss)simplex[dimension]=reflected;
      else {Candidate contracted;const Candidate &toward=reflected.loss<simplex[dimension].loss?reflected:simplex[dimension];
        for(unsigned j=0;j<dimension;++j)contracted.q[j]=center[j]+.5f*(toward.q[j]-center[j]);evaluate(contracted);++calls;
        if(contracted.loss<std::min(reflected.loss,simplex[dimension].loss))simplex[dimension]=contracted;
        else for(unsigned i=1;i<=dimension && calls<budget;++i){for(unsigned j=0;j<dimension;++j)simplex[i].q[j]=.5f*(simplex[i].q[j]+simplex[0].q[j]);evaluate(simplex[i]);++calls;}}
    }
    std::sort(simplex,simplex+dimension+1,[](const Candidate&a,const Candidate&b){return a.loss<b.loss;});
    if(simplex[0].loss<global.loss){global=simplex[0];globalConverged=converged;}
    if(anchored)continue;
    Result trial;trial.model=m;trial.params=coefficients(simplex[0].q);trial.valid=simplex[0].loss<1e90;
    const float stop=predictStopTime(trial,x0*1000,cfg);if(std::isfinite(stop)){minimumStop=std::min(minimumStop,stop);maximumStop=std::max(maximumStop,stop);}
  }
  result.params=coefficients(global.q);result.valid=global.loss<1e90;result.loss=result.valid?float(global.loss):0;result.converged=globalConverged;
  for(unsigned j=0;j<dimension;++j){float lo=power && j==exponentIndex?cfg.nMin:0,hi=power && j==exponentIndex?cfg.nMax:8;
    if(global.q[j]<=lo+.01f || global.q[j]>=hi-.01f)result.boundary=true;}
  result.unstable=maximumStop>0 && minimumStop<INFINITY && maximumStop>minimumStop*1.25f;
  return result;
}
struct Errors {
  double squares=0,absolute=0;
  float largest=0;
  unsigned count=0;

  void add(float error){
    ++count;
    squares+=double(error)*error;
    absolute+=error;
    largest=std::max(largest,error);
  }
  Metric rpm()const{
    if(!count)return {};
    return {true,float(1000*std::sqrt(squares/count)),float(1000*absolute/count),1000*largest};
  }
};

// Score both sides of the chronological split on one continuous RK4 trajectory.
inline void scoreFit(Result &fit,const Workspace &w,unsigned split,float x0,
                     const Config &cfg,Continue fn,void *ctx){
  if(!fit.valid)return;
  Errors errors;
  float x=x0,time=-w.startOffset;
  for(unsigned i=0;i<w.count;++i){
    const auto &point=w.points[i];
    x=advance(fit.model,fit.params,x,point.t-time,cfg,fn,ctx);
    if(!std::isfinite(x))return;
    time=point.t;
    errors.add(std::fabs(x-point.x));
    if(i+1==split && split<w.count){
      fit.training=errors.rpm();
      errors={};
    }
  }
  if(split==w.count)fit.all=errors.rpm();
  else fit.validation=errors.rpm();
}
inline Results fitAllModels(const Session &session,Workspace &work,const Config &cfg={},Continue fn=nullptr,void *ctx=nullptr) {
  Results out;for(unsigned m=0;m<MODEL_COUNT;++m)out.models[m].model=Model(m);
  prepare(session,work,out,cfg);const unsigned count=work.count,training=count*4/5;
  const float startRpm=out.opticalAnchor?out.launcherRpm:out.initialRpm;
  const float origin=out.opticalAnchor?out.launcherTime:out.acousticStart;
  if(out.manualAnchor && count && out.lastTime>origin && out.actualStop>out.lastTime){
    out.tailRpm=1000*work.points[count-1].x;
    const float secant=-out.tailRpm/(out.actualStop-out.lastTime);
    float slope=secant;
    // Estimate the incoming slope from up to five retained observations.
    // Pairwise median limits the influence of an unusually noisy last sample.
    if(count>=2){
      float rates[10];unsigned used=0;
      for(unsigned i=count-std::min(count,5u);i<count;++i){
        for(unsigned j=i+1;j<count;++j){
          const auto &a=work.points[i],&b=work.points[j];
          rates[used++]=1000*(b.x-a.x)/(b.t-a.t);
        }
      }
      slope=median(rates,used);
    }
    // For a flat final tangent this interval guarantees monotone cubic decay.
    out.tailSlope=std::clamp(slope,3*secant,0.f);
  }
  if(startRpm<=0)return out;
  if(count<6){
    // With too few acoustic observations only the two endpoints determine a line.
    if(work.stopDuration>0)for(auto &result:out.models){
      result.params.a=startRpm/(1000*work.stopDuration);
      result.valid=result.stopValid=true;
      result.stopSeconds=out.actualStop;
      result.sampleCount=count;
    }
    return out;
  }
  const unsigned edge=std::min(5u,count/3);float early=0,late=0;
  for(unsigned i=0;i<edge;++i){early+=work.points[i].x/edge;late+=work.points[count-edge+i].x/edge;}
  const bool coverage=early-late>=std::max(.5f,.08f*early) && work.points[count-1].t>=2;
  out.validationPossible=coverage && training>=8 && count-training>=3;
  const float x0=startRpm/1000;
  for(unsigned m=0;m<MODEL_COUNT;++m){
    if(!proceed(fn,ctx)){out.cancelled=true;return out;}
    Result validation;
    if(out.validationPossible){
      validation=optimize(Model(m),work,training,x0,cfg,fn,ctx);
      scoreFit(validation,work,training,x0,cfg,fn,ctx);
    }
    Result result=optimize(Model(m),work,count,x0,cfg,fn,ctx);
    result.training=validation.training;
    result.validation=validation.validation;
    result.validationConverged=validation.converged;
    scoreFit(result,work,count,x0,cfg,fn,ctx);
    result.sampleCount=count;
    if(out.actualStopValid && result.valid){
      result.stopSeconds=out.actualStop;
      result.stopValid=true;
    } else if(coverage){
      float remaining=predictStopTime(result,startRpm,cfg);
      result.stopValid=std::isfinite(remaining);
      if(result.stopValid)result.stopSeconds=origin+remaining;
    }
    out.models[m]=result;
  }
  if(!proceed(fn,ctx)){out.cancelled=true;return out;}
  // Sort by held-out evidence. Complexity breaks a near tie, not training loss.
  auto eligible=[&](unsigned i){const auto &r=out.models[i];return r.valid && r.converged && r.validationConverged && r.validation.valid;};
  std::sort(out.ranking,out.ranking+MODEL_COUNT,[&](unsigned a,unsigned b){const auto &x=out.models[a],&y=out.models[b];
    if(eligible(a)!=eligible(b))return eligible(a);
    if(!eligible(a))return a<b;
    if(x.validation.rmse!=y.validation.rmse)return x.validation.rmse<y.validation.rmse;
    return x.validation.mae<y.validation.mae;});
  if(eligible(out.ranking[0])){
    out.best=out.ranking[0];const auto bestError=out.models[out.best].validation.rmse;
    if(out.best>=2){int simpler=-1;
      for(unsigned i=0;i<2;++i)if(eligible(i) && out.models[i].validation.rmse<=bestError+cfg.validationTieRpm &&
        (simpler<0 || out.models[i].validation.rmse<out.models[simpler].validation.rmse))simpler=int(i);
      if(simpler>=0)out.best=simpler;
    }
    if(out.best!=out.ranking[0]){auto at=std::find(out.ranking,out.ranking+MODEL_COUNT,uint8_t(out.best));std::rotate(out.ranking,at,at+1);}
  }
  return out;
}
inline void makeCurve(const Results &fits,unsigned selected,Curve &curve,const Config &cfg={},Continue fn=nullptr,void *ctx=nullptr) {
  const auto &r=fits.models[selected];curve.end=std::max(10.f,std::max(fits.lastTime,fits.actualStopValid?fits.actualStop:0.f));
  if(!fits.manualAnchor)curve.end=std::max(curve.end,fits.actualStop); // Cover the recorded timeout interval.
  if(r.stopValid)curve.end=std::max(curve.end,std::min(600.f,r.stopSeconds));
  const float origin=fits.opticalAnchor?fits.launcherTime:fits.acousticStart;
  const bool recordedEnd=fits.manualAnchor;
  if(recordedEnd)curve.end=fits.actualStop;
  float x=(fits.opticalAnchor?fits.launcherRpm:fits.initialRpm)/1000,t=origin;
  curve.joinIndex=0;
  if(recordedEnd && fits.tailRpm>0 && fits.lastTime>origin && curve.end>fits.lastTime){
    curve.joinTime=fits.lastTime;
    curve.joinIndex=std::clamp(unsigned(std::lround((CURVE_POINTS-1)*curve.joinTime/curve.end)),1u,CURVE_POINTS-5);
  }
  for(unsigned i=0;i<CURVE_POINTS;++i){
    const float at=curve.seconds(i);
    if(!r.valid || at<origin){curve.rpm[i]=NAN;continue;}
    if(curve.joinIndex && i>=curve.joinIndex){
      // Cubic Hermite basis, factored for precision near the manual zero.
      const float duration=curve.end-curve.joinTime;
      const float u=(at-curve.joinTime)/duration;
      const float rest=1-u;
      curve.rpm[i]=std::max(0.f,rest*rest*((1+2*u)*fits.tailRpm+u*duration*fits.tailSlope));
      continue;
    }
    if(recordedEnd && i==CURVE_POINTS-1){curve.rpm[i]=0;break;}
    x=advance(r.model,r.params,x,at-t,cfg,fn,ctx);t=at;curve.rpm[i]=x*1000;}
}
}
