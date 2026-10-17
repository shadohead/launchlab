import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {APP_OFFSET, APP_IDENTITY_BYTES, validateCatalog, inspectInstalledApplication, flashVerified, verifiedDownload} from '../src/flasher.js';
import {catalog, images, base, localFetch, md5, fake, opts} from './helpers.js';
// This descriptor and checksum responder model a privately installed build.
// Its full image is deliberately absent from the public repository and assets.
const diagnostic=catalog.knownApplications.find(a=>a.sha256==='80c9bb99f6c5ce22313f38f75b3ed64bd37d95abe1a7ec39a9e679cf1448f3b6');
function installed(digest=diagnostic.md5) {
  const header=new Uint8Array(APP_IDENTITY_BYTES);header[0]=0xe9;
  new DataView(header.buffer).setUint32(32,0xabcd5432,true);
  header.set(Uint8Array.from(diagnostic.elfSha256.match(/../g),h=>parseInt(h,16)),176);
  const loader=fake({app:header});loader.digestQueries=[];
  loader.flashMd5sum=async function(address,size){
    this.digestQueries.push({address,size});
    if(!this.writes.length){assert.equal(address,APP_OFFSET);assert.equal(size,809680);return digest;}
    return md5(this.app.subarray(0,size));
  };
  return loader;
}
test('known diagnostic remains a distinct 0.12.1 identity and is not the 0.13.8 target',()=>{
  validateCatalog(catalog);assert.ok(diagnostic);assert.equal(diagnostic.version,'0.12.1');
  assert.equal(diagnostic.runtime,'0.12.1-sticks3-flip-colors');assert.equal(diagnostic.size,809680);
  assert.equal(diagnostic.md5,'95974ecdbc420a40d1de5a59ca451e5e');
  assert.equal(diagnostic.elfSha256,'990a1e38f05037cf9f1bd826cbec63a948e15cbefc58ab8778c9ba71df34ab54');
  assert.equal(diagnostic.buildId,'0.12.1-sticks3-private-recovery-diagnostic');
  const prior=catalog.knownApplications.filter(a=>a.version==='0.12.1');assert.equal(prior.length,3);
  assert.equal(new Set(prior.map(a=>a.buildId)).size,3);
  assert.equal(prior.find(a=>a.sha256.startsWith('a13d590f')).buildId,'0.12.1-sticks3-pulse-guard-device-verified');
  assert.equal(prior.find(a=>a.sha256.startsWith('25eb22f7')).buildId,'0.12.1-sticks3-flip-colors');
  const target=catalog.knownApplications.filter(a=>a.version==='0.13.8'&&a.sha256===catalog.files[3].sha256);
  assert.equal(target.length,1);assert.notEqual(target[0].elfSha256,diagnostic.elfSha256);
});
test('complete installed diagnostic checksum permits an ordinary application-only 0.13.8 update',async()=>{
  const loader=installed(),found=await inspectInstalledApplication(loader,catalog);
  assert.equal(found.state,'verified');assert.equal(found.application.sha256,diagnostic.sha256);
  assert.deepEqual(loader.digestQueries,[{address:APP_OFFSET,size:809680}]);
  assert.equal((await flashVerified(opts(loader))).status,'verified');
  assert.deepEqual(loader.writes[0].fileArray.map(f=>f.address),[APP_OFFSET]);
  assert.equal(loader.writes[0].eraseAll,false);
  assert.deepEqual(loader.digestQueries,[{address:APP_OFFSET,size:809680},{address:APP_OFFSET,size:809680},{address:APP_OFFSET,size:839760}]);
  assert.equal((await flashVerified(opts(loader))).status,'up-to-date');assert.equal(loader.writes.length,1);
  assert.deepEqual(loader.app,images[3].data);
});
test('a matching diagnostic descriptor cannot bypass a failed full-image checksum',async()=>{
  for(const digest of ['0'.repeat(32),undefined,'',diagnostic.md5.slice(0,-1)]){
    const loader=installed(digest===undefined?null:digest);
    await assert.rejects(flashVerified(opts(loader)),/unknown or incomplete/);
    assert.deepEqual(loader.digestQueries,[{address:APP_OFFSET,size:809680}]);assert.equal(loader.writes.length,0);
  }
});
test('recognized diagnostic still blocks accidental downgrade to the previous public default',async()=>{
  const prior=JSON.parse(await readFile(new URL('../public/firmware/0.11.2/catalog.json',import.meta.url)));
  const previousImages=await Promise.all(prior.files.map(f=>verifiedDownload(f,base,localFetch)));
  const loader=installed();
  await assert.rejects(flashVerified({...opts(loader),catalog:prior,images:previousImages}),/Downgrade blocked.*0.12.1.*0.11.2/);
  assert.equal(loader.writes.length,0);assert.deepEqual(loader.digestQueries,[{address:APP_OFFSET,size:809680}]);
});
