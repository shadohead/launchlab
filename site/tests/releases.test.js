import test from 'node:test';
import assert from 'node:assert/strict';
import {compareVersions} from '../src/flasher.js';
import {MAX_LISTED_RELEASES, validateReleaseIndex, loadReleaseIndex, createReleaseSelection, channelOf, listedReleases} from '../src/releases.js';
import {catalog,index,current,oldCatalog,base,preserved,newestFirst,listed} from './helpers.js';
const reply=data=>({ok:true,json:async()=>data});
// A release that does not exist yet (the minor after the newest one listed) and a version that was never published.
const next=(([major,minor])=>[major,minor+1,0].join('.'))(listed[0].split('.').map(Number));
const neverPublished='0.13.1';
// Every release the index offered when 0.20.8 became stable on 2026-11-06, with its application SHA-256.
// A stable release stays stable. These betas were all superseded by a newer stable release, so they stay betas.
// A later release needs no entry here; removing one is a withdrawal and has to be a deliberate edit.
const published={
  stable:{
    '0.20.8':'581de973164784a34afa91a1284b5d8729c3b93770846f72ae03cb7b71ca1741','0.20.5':'fac73207078db878a7eef0450cff063b6be498cc3c153697c85ec1d4c41ab730',
    '0.20.4':'db6fb35985db2587a8ad572afd431fbdab2c9101175a3b06699d7f4b516fcb28','0.20.2':'d8dd23138fdf0b720c4abfb7bc497a30902c89d0ceb7c9b6dc43fcd449d12b70',
    '0.18.0':'ec998fc91247f9a2f1a9780a5c493c043f18c7716cf42e32c43396c970ba78b8','0.15.0':'48b827825ee721ac42a4c1ebdcc4abdddd6d690b8cd5c090b791aa378726a99e',
    '0.13.9':'ec9f09ae41577bf96aefc1b308953480dd2fad5ab746afe2ade53faafd6cae6c','0.13.8':'a1d5c47ef9e46a19ec79e36f65aeee2186a62bfbb1cf59f01af4ae2f83407525',
    '0.13.7':'83aa6b7610241e45ae5ed2957b958576886967eacdef19e72c7ad5e836f96e52','0.13.5':'0203763a133de42bf09dd3c5a93a6cbe074c974e815d8d5aff6d2541f6a88edf',
    '0.12.2':'9548c322ae1f7383641e62ec5b81f7a2f54777c05034f8c5aac47a40b4f25b49','0.11.2':'edaec5c245b8bc0e9e543e53896885e00d4c78df3eca17de57175beeff99c01e',
    '0.11.1':'acba34e40d0832d8b46469443a824c3254a48af538004b0711c4d0a1bc5a4d5e','0.10.22':'8ff2005fcee9d10456312b6e5e1837938e81054e9669c63bbede12e7763ed8a9',
    '0.10.21':'dbf3c6e510564c9ae7a4d504737077491146d8501a0a6d1e7b25dca41a25cbea','0.10.14':'428543c2a012f5ee865999e62494e1608b77a9091a62b253fae9fb1e87ad2b25',
    '0.10.11':'4a49e2fc663efd89dca3ed527d1ba493c2cd5c39e03717fed2be263906751aaf','0.10.7':'54fe847cc82ac11b8f50669c424498267e079dfe967020271763e24ba5c808d2',
    '0.10.1':'bf3b6607729de89fccae3ddb15b4724216abfe2ce56bba5e8918b287d629fc66'},
  beta:{
    '0.20.7':'578cb43c6cbd56f4490e1f9d9a84102183fd572f93c2405e143730c9afaec06a','0.20.6':'2a26b884909a36aac44b8985b425189f1ed44fbf296b3d4c75f3188e725bbc60',
    '0.20.3':'d21cec9223582fa1d58d130025ad947aabf364d3f9d268a355233fa4897f8672','0.20.1':'c4b8ac8817730395571ca2eb3e985adc4ad88c68f99364906cf840eb0b0a2fb9',
    '0.20.0':'9e9d657a81a9fba8b88de7029aec7aae52335c64cfcea175c48126cf45a15dc6','0.19.0':'8a2ff3bf53130a48fa429e63bf1ab44d6d2c5373ce2e088633dac23f6f5736b6',
    '0.18.1':'4fa5060703ebfbe36ae782c550437c0c9f3e14a2439617cb4444cf78d3e83d89','0.17.0':'5b27a5585eab33a5082adf6cc4605ff76b6ad6d054246774bdab594604b7ff22',
    '0.16.0':'76793b16c9b75a0c5d04bc8a9139cdbfa8f57e49e646e37f1c1205cb7c0e25ba','0.14.0':'e3014ec527da2fa13dc29ed3d1f4c351d9d45d55e893eb07f047235b7b0e4659'}
};

test('release list is valid, newest first, and every entry has its own catalog',async()=>{
  validateReleaseIndex(index);
  for(const list of [index.releases,index.betaReleases]){
    const versions=list.map(r=>r.version);assert.deepEqual(versions,newestFirst(versions),'newest first');
    for(const r of list){assert.equal(r.catalog,'firmware/'+r.version+'/catalog.json');assert.equal((await preserved(r.version)).catalog.version,r.version);}
  }
  assert.equal(new Set(listed).size,listed.length,'no release is both stable and beta');
  assert.equal(index.latest,index.releases[0].version);assert.equal(current,index.latest,'the default catalog is the latest stable release');
  assert.ok(index.beta===null||(index.beta===index.betaReleases[0].version&&compareVersions(index.beta,index.latest)>0),'beta is nothing or newer than latest');
});
test('every release published by 2026-11-06 stays listed with its exact application; withdrawn 0.12.1 stays out',async()=>{
  for(const [channel,list] of [['stable',index.releases],['beta',index.betaReleases]])for(const [version,sha256] of Object.entries(published[channel])){
    assert.ok(list.some(r=>r.version===version),version+' stays listed as '+channel);
    assert.equal((await preserved(version)).catalog.files[3].sha256,sha256,version+' keeps its application');
  }
  assert.ok(!listed.includes('0.12.1'));
});
test('release list rejects duplicate, external, missing or older recommended targets',()=>{
  for(const mutate of [i=>i.releases.push(i.releases[0]),i=>i.releases[0].catalog='https://bad.example/catalog.json',i=>i.latest='0.10.1',i=>i.latest='0.11.0',i=>i.releases=[]]){
    const i=structuredClone(index);mutate(i);assert.throws(()=>validateReleaseIndex(i));
  }
});
test('release list fetch uses no-store and validates before selection',async()=>{
  const result=await loadReleaseIndex(base,async(url,opts)=>{assert.equal(opts.cache,'no-store');assert.equal(url.pathname.split('/').slice(-2).join('/'),'firmware/versions.json');return reply(index);});
  assert.equal(result.latest,current);
  await assert.rejects(loadReleaseIndex(base,async()=>({ok:false})),/unavailable/);
});
test('selection loads the exact version catalog and invalidates old state while pending',async()=>{
  const seen=[];const selection=createReleaseSelection({index,baseUrl:base,fetchFn:async url=>reply(url.pathname.includes('/0.10.1/')?oldCatalog:catalog),onChange:s=>seen.push(s)});
  await selection.select(index.latest);assert.equal(selection.state.catalog.version,current);
  await selection.select('0.10.1');assert.equal(selection.state.catalog.version,'0.10.1');
  assert.ok(seen.filter(s=>s.loading).every(s=>s.catalog===null));
});
test('slow earlier request cannot replace a newer selection',async()=>{
  let completeOld,completeNew;
  const selection=createReleaseSelection({index,baseUrl:base,fetchFn:url=>new Promise(resolve=>{if(url.pathname.includes('/0.10.1/'))completeOld=resolve;else completeNew=resolve;})});
  const old=selection.select('0.10.1'),latest=selection.select(current);
  completeNew(reply(catalog));await latest;completeOld(reply(oldCatalog));await old;
  assert.equal(selection.state.version,current);assert.equal(selection.state.catalog.version,current);
});
test('failed or mismatched catalog clears the usable release; retry reloads cleanly',async()=>{
  let response=reply(oldCatalog);
  const selection=createReleaseSelection({index,baseUrl:base,fetchFn:async()=>response});
  await selection.select(current);assert.equal(selection.state.catalog,null);assert.match(selection.state.error.message,/does not match/);
  response={ok:false};await selection.select(current);assert.equal(selection.state.catalog,null);
  response=reply(catalog);await selection.select(current);assert.equal(selection.state.error,null);assert.equal(selection.state.catalog.version,current);
});
test('unknown requested version cannot reuse previously selected catalog',async()=>{
  const selection=createReleaseSelection({index,baseUrl:base,fetchFn:async()=>reply(catalog)});
  await selection.select(current);await selection.select('9.9.9');assert.equal(selection.state.catalog,null);assert.match(selection.state.error.message,/Unknown/);
});

test('withdrawn 0.12.1 cannot be selected through the updater index',async()=>{
  const selection=createReleaseSelection({index,baseUrl:base,fetchFn:async()=>{throw new Error('Withdrawn selection must not fetch');}});
  await selection.select('0.12.1');assert.equal(selection.state.catalog,null);assert.match(selection.state.error.message,/Unknown firmware release/);
});

const beta=version=>({version,catalog:'firmware/'+version+'/catalog.json',channel:'beta'});
const withBeta=()=>{const i=structuredClone(index);i.betaReleases=[beta(next)];i.beta=next;return i;};
test('stable releases stay stable, latest is the newest stable and beta points at a newer beta or nothing',()=>{
  assert.ok(index.releases.every(r=>r.channel==='stable'));assert.equal(index.latest,index.releases[0].version);
  assert.ok(index.betaReleases.every(r=>r.channel==='beta'));
  const versions=index.betaReleases.map(r=>r.version);assert.deepEqual([...versions].sort((x,y)=>-x.localeCompare(y,undefined,{numeric:true})),versions,'betas are listed newest first');
  const newer=versions.find(v=>v.localeCompare(index.latest,undefined,{numeric:true})>0)??null;
  assert.equal(index.beta,newer);
  assert.ok(!index.betaReleases.some(r=>index.releases.some(s=>s.version===r.version)),'a promoted release is no longer listed as a beta');
});
test('a newer beta sits beside latest, which older readers keep following',()=>{
  const i=validateReleaseIndex(withBeta());
  assert.equal(i.latest,current);
  assert.ok(!i.releases.some(r=>r.version===next),'releases holds stable only');
  assert.deepEqual(listedReleases(i).slice(0,2).map(r=>[r.version,channelOf(r)]),[[next,'beta'],[current,'stable']]);
});
test('an index from before channels reads as all stable',()=>{
  const i=structuredClone(index);delete i.beta;delete i.betaReleases;i.releases.forEach(r=>delete r.channel);
  validateReleaseIndex(i);assert.ok(listedReleases(i).every(r=>channelOf(r)==='stable'));
});
test('channel lists and the beta pointer must agree',()=>{
  for(const mutate of [
    i=>{i.releases.unshift({...i.betaReleases[0]});i.betaReleases=[];i.beta=null;},
    i=>{i.betaReleases[0].channel='stable';},
    i=>{i.beta=null;},
    i=>{i.beta=current;},
    i=>{delete i.beta;},
    i=>{i.betaReleases.push(beta(current));},
    i=>{i.betaReleases[0]=beta(neverPublished);}
  ]){const i=withBeta();mutate(i);assert.throws(()=>validateReleaseIndex(i));}
  const older=withBeta();older.betaReleases[0]=beta(neverPublished);older.beta=null;
  validateReleaseIndex(older);
});
test('a beta release is selectable by version',async()=>{
  const i=structuredClone(index);const old=i.releases.pop();i.betaReleases=[{...old,channel:'beta'}];i.beta=null;
  const selection=createReleaseSelection({index:i,baseUrl:base,fetchFn:async()=>reply(oldCatalog)});
  await selection.select(old.version);assert.equal(selection.state.error,null);assert.equal(selection.state.catalog.version,old.version);
});
// The list bound is a sanity check on fetched data; the release cadence must never reach it.
test('a long release history validates and only an absurd list is refused',()=>{
  const row=(version,channel)=>({version,catalog:'firmware/'+version+'/catalog.json',channel});
  const history=(stable,betas)=>({latest:'1.'+stable+'.0',beta:betas?'1.'+betas+'.1':null,
    releases:Array.from({length:stable},(_,n)=>row('1.'+(stable-n)+'.0','stable')),betaReleases:Array.from({length:betas},(_,n)=>row('1.'+(betas-n)+'.1','beta'))});
  assert.equal(listedReleases(validateReleaseIndex(history(60,60))).length,120);
  validateReleaseIndex(history(MAX_LISTED_RELEASES,MAX_LISTED_RELEASES));
  for(const list of [index.releases,index.betaReleases])assert.ok(list.length<=MAX_LISTED_RELEASES/2,'the real list is half way to its bound: raise MAX_LISTED_RELEASES here and in the Beywatch copy well before it is reached');
  assert.throws(()=>validateReleaseIndex(history(MAX_LISTED_RELEASES+1,60)),/unavailable/);
  assert.throws(()=>validateReleaseIndex(history(60,MAX_LISTED_RELEASES+1)),/unavailable/);
  assert.throws(()=>validateReleaseIndex(history(100000,0)),/unavailable/);
});
