import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {validateReleaseIndex, loadReleaseIndex, createReleaseSelection} from '../src/releases.js';
import {catalog,oldCatalog,base} from './helpers.js';
const index=JSON.parse(await readFile(new URL('../public/firmware/versions.json',import.meta.url)));
const reply=data=>({ok:true,json:async()=>data});

test('release list recommends 0.13.9 and excludes withdrawn 0.12.1',()=>{
  validateReleaseIndex(index);assert.equal(index.latest,'0.13.9');
  assert.deepEqual(index.releases.map(r=>r.version),['0.13.9','0.13.8','0.13.7','0.13.5','0.12.2','0.11.2','0.11.1','0.10.22','0.10.21','0.10.14','0.10.11','0.10.7','0.10.1']);
});
test('release list rejects duplicate, external, missing or older recommended targets',()=>{
  for(const mutate of [i=>i.releases.push(i.releases[0]),i=>i.releases[0].catalog='https://bad.example/catalog.json',i=>i.latest='0.10.1',i=>i.latest='0.11.0',i=>i.releases=[]]){
    const i=structuredClone(index);mutate(i);assert.throws(()=>validateReleaseIndex(i));
  }
});
test('release list fetch uses no-store and validates before selection',async()=>{
  const result=await loadReleaseIndex(base,async(url,opts)=>{assert.equal(opts.cache,'no-store');assert.equal(url.pathname.split('/').slice(-2).join('/'),'firmware/versions.json');return reply(index);});
  assert.equal(result.latest,'0.13.9');
  await assert.rejects(loadReleaseIndex(base,async()=>({ok:false})),/unavailable/);
});
test('selection loads the exact version catalog and invalidates old state while pending',async()=>{
  const seen=[];const selection=createReleaseSelection({index,baseUrl:base,fetchFn:async url=>reply(url.pathname.includes('/0.10.1/')?oldCatalog:catalog),onChange:s=>seen.push(s)});
  await selection.select(index.latest);assert.equal(selection.state.catalog.version,'0.13.9');
  await selection.select('0.10.1');assert.equal(selection.state.catalog.version,'0.10.1');
  assert.ok(seen.filter(s=>s.loading).every(s=>s.catalog===null));
});
test('slow earlier request cannot replace a newer selection',async()=>{
  let completeOld,completeNew;
  const selection=createReleaseSelection({index,baseUrl:base,fetchFn:url=>new Promise(resolve=>{if(url.pathname.includes('/0.10.1/'))completeOld=resolve;else completeNew=resolve;})});
  const old=selection.select('0.10.1'),next=selection.select('0.13.9');
  completeNew(reply(catalog));await next;completeOld(reply(oldCatalog));await old;
  assert.equal(selection.state.version,'0.13.9');assert.equal(selection.state.catalog.version,'0.13.9');
});
test('failed or mismatched catalog clears the usable release; retry reloads cleanly',async()=>{
  let response=reply(oldCatalog);
  const selection=createReleaseSelection({index,baseUrl:base,fetchFn:async()=>response});
  await selection.select('0.13.9');assert.equal(selection.state.catalog,null);assert.match(selection.state.error.message,/does not match/);
  response={ok:false};await selection.select('0.13.9');assert.equal(selection.state.catalog,null);
  response=reply(catalog);await selection.select('0.13.9');assert.equal(selection.state.error,null);assert.equal(selection.state.catalog.version,'0.13.9');
});
test('unknown requested version cannot reuse previously selected catalog',async()=>{
  const selection=createReleaseSelection({index,baseUrl:base,fetchFn:async()=>reply(catalog)});
  await selection.select('0.13.9');await selection.select('9.9.9');assert.equal(selection.state.catalog,null);assert.match(selection.state.error.message,/Unknown/);
});

test('withdrawn 0.12.1 cannot be selected through the updater index',async()=>{
  const selection=createReleaseSelection({index,baseUrl:base,fetchFn:async()=>{throw new Error('Withdrawn selection must not fetch');}});
  await selection.select('0.12.1');assert.equal(selection.state.catalog,null);assert.match(selection.state.error.message,/Unknown firmware release/);
});
