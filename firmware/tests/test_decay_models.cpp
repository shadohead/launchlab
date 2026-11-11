#include "decay_models.h"
#include <cassert>
#include <iostream>
#include <memory>
using namespace Decay;
int main(){
 Config cfg;
 Params p;p.a=.03f;p.b=.006f;
 float x=predictRpm(Model::Linear,p,9000,20);
 float exact=1000*((9+p.a/p.b)*std::exp(-p.b*20)-p.a/p.b);
 assert(std::fabs(x-exact)<1);
 Result known;known.valid=true;known.model=Model::Linear;known.params=p;
 float stop=predictStopTime(known,9000);assert(std::fabs(stop-std::log(1+p.b*9/p.a)/p.b)<1);
 auto session=std::make_unique<Session>();auto work=std::make_unique<Workspace>();
 for(unsigned m=0;m<4;++m){
  Params truth;truth.a=.03f;truth.b=m==0?.006f:m==2?.003f:0;truth.c=m==1?.0015f:m==2?.001f:0;truth.k=.003f;truth.n=1.7f;
  session->count=0;session->launcherRpm=9000;session->launcherMs=200;session->hasStop=false;
  float current=9;
  for(unsigned i=0;i<=120;++i){if(i)current=advance(Model(m),truth,current,.2f,cfg);
    if(i%13==12)continue;float noise=20*std::sin(i*2.1f);
    session->samples[session->count++]={200+i*200,current*1000+noise+(i==40?4000:0),i==40};}
  auto result=fitAllModels(*session,*work,cfg);
  std::cout<<"source="<<m<<" best="<<result.best<<" n="<<result.sampleCount<<" spikes="<<result.spikes<<"\n";
  for(auto &fit:result.models)std::cout<<int(fit.model)<<" valid="<<fit.valid<<" conv="<<fit.converged<<" vconv="<<fit.validationConverged<<" rmse="<<fit.all.rmse<<" val="<<fit.validation.rmse<<" stop="<<fit.stopSeconds<<"\n";
  assert(result.spikes==1 && result.validationPossible);
  assert(result.models[m].valid && result.models[m].all.valid && result.models[m].all.rmse<100);
 }

 // Full observed range: recover curves and useful stop predictions under each law.
 for(unsigned m=0;m<4;++m){
  Params truth;truth.a=.05f;truth.b=.005f;truth.c=.001f;truth.k=.003f;truth.n=1.7f;
  session->count=0;session->hasStop=true;session->launcherRpm=9000;float current=9;
  Result ideal;ideal.valid=true;ideal.model=Model(m);ideal.params=truth;
  const float actual=predictStopTime(ideal,9000);
  session->stopMs=unsigned((actual+.2f)*1000);
  for(unsigned i=0;i<1500 && current>=3.05f;++i){
   if(i)current=advance(Model(m),truth,current,.2f,cfg);
   if(i%17==5)continue;
   session->samples[session->count++]={200+i*200,current*1000+10*std::sin(i*1.7f),false};
  }
  auto result=fitAllModels(*session,*work);auto &fit=result.models[m];
  std::cout<<"wide model="<<m<<" actual="<<actual<<" predicted="<<fit.stopSeconds<<" rmse="<<fit.all.rmse<<"\n";
  assert(fit.valid && fit.all.rmse<40 && fit.stopValid && !fit.stopErrorValid);
  for(const auto &model:result.models){
    assert(model.valid && std::fabs(predictStopTime(model,9000)-actual)<.15f);
    assert(predictRpm(model,9000,actual*.98f)>0);
  }
  session->launcherRpm=11000;session->stopMs+=10000;
  auto changed=fitAllModels(*session,*work);
  for(unsigned j=0;j<4;++j){
    const auto &model=changed.models[j];
    assert(model.valid && std::fabs(predictStopTime(model,11000)-(actual+10))<.15f);
    assert(model.loss!=result.models[j].loss);
  }
 }

 // Several consecutive bad observations are downweighted, not mistaken for new dynamics.
 session->launcherRpm=9000;session->count=120;session->hasStop=false;Params robustTruth;robustTruth.a=.04f;robustTruth.b=.006f;
 for(unsigned i=0;i<120;++i){float t=i*.2f;session->samples[i]={200+i*200,predictRpm(Model::Linear,robustTruth,9000,t)+(i>=50 && i<=52?5000:0),false};}
 auto robust=fitAllModels(*session,*work);assert(robust.models[0].valid);
 assert(std::fabs(predictRpm(robust.models[0],robust.initialRpm,20)-predictRpm(Model::Linear,robustTruth,9000,20))<150);

 // Check actual retained timestamps/RPMs for brief excursions and track replacement.
 session->launcherMs=0;
 auto classify=[&](std::initializer_list<float> readings){
   session->count=0;
   for(float rpm:readings){
     unsigned i=session->count++;
     session->samples[i]={i*200,rpm,false};
   }
   Results result;prepare(*session,*work,result,cfg);return result;
 };
 auto single=classify({6000,5500,5000,4500,4000,7000,3900,2800});
 assert(single.spikes==1 && single.sampleCount==7 && single.acousticStart==0 && work->points[6].x==2.8f);
 auto pair=classify({6000,5500,5000,4500,4000,7000,6900,3900,2800});
 assert(pair.spikes==2 && pair.sampleCount==7 && work->points[5].x==3.9f);
 auto trailing=classify({6000,5000,4000,7000,7000});
 assert(trailing.spikes==2 && trailing.sampleCount==3);
 auto plateau=classify({6000,5500,5000,4500,4000,7000,6600,6600,6600,3900});
 assert(plateau.sampleCount==6 && plateau.spikes==4);
 // A rebound below the older median must still be rejected.
 auto rebound=classify({6000,5000,4000,4400,3900});
 assert(rebound.spikes==1 && rebound.sampleCount==4 && work->points[3].x==3.9f);
 // Even small, widely separated rises cannot ratchet the last RPM upward.
 classify({6000,5000,4000,4010,4200,4500,3900});
 for(unsigned i=3;i<session->count;++i)session->samples[i].ms=i*5000;
 Results sparseRises;prepare(*session,*work,sparseRises,cfg);
 assert(sparseRises.spikes==3 && sparseRises.sampleCount==4 && work->points[3].x==3.9f);
 // Rejected trailing samples cannot move the Hermite tail's anchor.
 session->count=6;session->hasStop=true;session->stopMs=40000;
 auto tailCheck=fitAllModels(*session,*work);
 assert(tailCheck.tailRpm==4000 && tailCheck.lastTime==.4f);
 session->hasStop=false;
 auto noise=classify({6000,5000,4000,4040,3950});
 assert(noise.spikes==0 && noise.sampleCount==5);
 auto replacement=classify({6000,5500,5000,4500,4000,7000,6600,6200,5800,5000,4000,3000,2500});
 assert(replacement.sampleCount==8 && replacement.acousticStart==1 && work->points[0].x==7 && work->points[7].x==2.5f);
 auto lowOnly=classify({2900,2800,2700,2600,2500,2400,2300,2200});
 assert(lowOnly.sampleCount==8 && fitAllModels(*session,*work).models[0].valid);
 // Held-out short-session readings must not change the shared initial condition.
 session->count=14;
 for(unsigned i=0;i<14;++i)session->samples[i]={200+i*400,9000-60.f*i+30*std::sin(float(i)),false};
 Results shortA,shortB;prepare(*session,*work,shortA,cfg);
 for(unsigned i=11;i<14;++i)session->samples[i].rpm-=700;
 prepare(*session,*work,shortB,cfg);assert(shortA.initialRpm==shortB.initialRpm);
 // The final training weight must not depend on the next held-out timestamp.
 const unsigned train=work->count*4/5;const auto params=robustTruth;
 double lossA=objective(Model::Linear,params,shortB.initialRpm/1000,*work,train,cfg,nullptr,nullptr);
 work->points[train-1].w*=100;
 double lossB=objective(Model::Linear,params,shortB.initialRpm/1000,*work,train,cfg,nullptr,nullptr);assert(lossA==lossB);
 for(unsigned budget=1;budget<12;++budget){Config small;small.evaluations=budget;
   for(unsigned m=0;m<4;++m){auto fit=optimize(Model(m),*work,train,9,small,nullptr,nullptr);assert(fit.evaluations<=budget);}}
 // Unordered, duplicate, negative/nonfinite and below-floor inputs.
 session->count=7;session->hasStop=true;session->stopMs=0;
 session->samples[0]={400,2700,false};session->samples[1]={200,8000,false};session->samples[2]={200,8000,false};
 session->samples[3]={200,60000,false};session->samples[4]={300,-1,false};session->samples[5]={300,NAN,false};session->samples[6]={300,2900,false};
 Results prep;prepare(*session,*work,prep,cfg);assert(prep.sampleCount==3 && work->points[1].x==2.9f && prep.invalidStop && !prep.actualStopValid);
 auto few=fitAllModels(*session,*work);assert(few.best==-1 && !few.models[0].valid);
 session->count=40;session->hasStop=false;for(unsigned i=0;i<40;++i)session->samples[i]={i*200,6000,false};
 auto flat=fitAllModels(*session,*work);assert(!flat.validationPossible && flat.best==-1 && !flat.models[0].stopValid);
 Config none;none.evaluations=0;auto failure=fitAllModels(*session,*work,none);assert(!failure.models[0].valid);
 session->launcherRpm=12000;session->count=0;session->hasStop=true;session->stopMs=60000;
 auto sparse=fitAllModels(*session,*work);
 for(unsigned j=0;j<4;++j){Curve curve;makeCurve(sparse,j,curve);
   assert(curve.rpm[0]==12000 && curve.rpm[CURVE_POINTS-1]==0 && curve.end==60);
 }
 // Place the last accepted timestamp between uniform display samples and
 // follow it with a rejected high spike. Every model must hit the same knot.
 session->count=9;
 for(unsigned i=0;i<8;++i)session->samples[i]={1300+i*173,7500.f-i*300,false};
 session->samples[8]={2800,11000,true};
 auto joined=fitAllModels(*session,*work);
 assert(joined.tailRpm==5400 && joined.lastTime==2.511f && joined.tailSlope<0);
 for(unsigned m=0;m<4;++m){
   Curve curve;makeCurve(joined,m,curve);
   assert(curve.rpm[0]==12000 && curve.joinIndex && curve.seconds(curve.joinIndex)==joined.lastTime);
   assert(curve.rpm[curve.joinIndex]==joined.tailRpm && curve.rpm[CURVE_POINTS-1]==0);
   for(unsigned i=curve.joinIndex+1;i<CURVE_POINTS;++i)
     assert(curve.seconds(i)>curve.seconds(i-1) && curve.rpm[i]>=0 && curve.rpm[i]<=curve.rpm[i-1]);
 }
 session->hasStop=false;
 auto timeout=fitAllModels(*session,*work);Curve timeoutCurve;makeCurve(timeout,0,timeoutCurve);
 assert(timeout.tailRpm==0 && !timeoutCurve.joinIndex);
 session->count=40;session->hasStop=false;
 auto cancelled=fitAllModels(*session,*work,cfg,[](void*){return false;});assert(cancelled.cancelled);
 known.params={.0001f,0,0,0,1};assert(!std::isfinite(predictStopTime(known,9000)));
 Params equivalent;equivalent.a=.02f;equivalent.k=equivalent.b=.005f;equivalent.n=1;
 assert(std::fabs(predictRpm(Model::Linear,equivalent,9000,50)-predictRpm(Model::Power,equivalent,9000,50))<1);
 equivalent.c=equivalent.k;equivalent.n=2;
 assert(std::fabs(predictRpm(Model::Quadratic,equivalent,9000,50)-predictRpm(Model::Power,equivalent,9000,50))<1);
 equivalent.n=cfg.nMin;assert(validParams(Model::Power,equivalent));equivalent.n=cfg.nMax;assert(validParams(Model::Power,equivalent));equivalent.n=3.1f;assert(!validParams(Model::Power,equivalent));
 // Evaluate exact RK4 trajectories at irregular times through and beyond zero.
 // A one-RPM RMS tolerance checks the accelerated objective in physical units.
 for(unsigned m=0;m<4;++m){
   Params params;params.a=.03f;params.b=.005f;params.c=.002f;params.k=.005f;params.n=1.7f;
   work->count=700;work->startOffset=.13f;
   float time=-work->startOffset,rpm=9;
   for(unsigned i=0;i<work->count;++i){
     float next=i*.32f+(i%7)*.011f;
     rpm=advance(Model(m),params,rpm,next-time,cfg);
     work->points[i]={next,rpm,.32f};time=next;
   }
   assert(objective(Model(m),params,9,*work,work->count,cfg,nullptr,nullptr)<.0000005);
 }
 // Duplicate groups can be larger than the scratch space for initial slopes.
 session->hasStop=false;session->count=MAX_SAMPLES;
 for(unsigned i=0;i<MAX_SAMPLES;++i)session->samples[i]={1000,float(4000+MAX_SAMPLES-1-i),false};
 Results duplicates;prepare(*session,*work,duplicates,cfg);
 assert(work->count==1 && work->points[0].x==5.5f);
 // A short, clean high-speed arc can favor Power-Law and still imply a many-minute finish.
 session->launcherRpm=3847;session->launcherMs=0;session->count=80;session->hasStop=false;session->stopMs=21000;
 for(unsigned i=0;i<80;++i){
   const float t=(i+1)*.2f;
   session->samples[i]={200*(i+1),3847/std::sqrt(1+.25f*t)+18*std::sin(i*.71f),false,false};
 }
 auto shortArc=fitAllModels(*session,*work);
 assert(shortArc.best>=0 && !std::isfinite(liveFinishSeconds(shortArc,68)));
 // A well-observed synthetic spin with an 82-second stop can produce a finish time.
 session->count=300;session->stopMs=65000;
 for(unsigned i=0;i<300;++i){
   const float t=(i+1)*.2f;
   session->samples[i]={200*(i+1),3847*(1-t/82)+6*std::sin(i*1.7f),false,false};
 }
 auto longArc=fitAllModels(*session,*work);
 float estimate=liveFinishSeconds(longArc,65);
 assert(std::isfinite(estimate) && std::fabs(estimate-82)<3);
 assert(!std::isfinite(liveFinishSeconds(longArc,estimate+1)));
 auto conflict=longArc;
 auto &peer=conflict.models[(conflict.best+1)%MODEL_COUNT];
 peer=conflict.models[conflict.best];peer.stopSeconds+=30;
 assert(!std::isfinite(liveFinishSeconds(conflict,65)));
 std::cout<<"PASS: model reconstruction and robust fits\n";
}
