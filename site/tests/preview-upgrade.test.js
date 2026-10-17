import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {validateCatalog, inspectInstalledApplication, flashVerified, verifiedDownload} from '../src/flasher.js';
import {catalog as recommendedCatalog, images as recommendedImages, base, localFetch, md5, fake, opts as recommendedOpts} from './helpers.js';
// Preserve the archived release's identity tests independently of the default.
const catalog = JSON.parse(await readFile(new URL('../public/firmware/0.12.1/catalog.json',import.meta.url)));
const images = await Promise.all(catalog.files.map(f=>verifiedDownload(f,base,localFetch)));
const opts = loader => ({...recommendedOpts(loader),catalog,images});
const recovery = JSON.parse(await readFile(new URL('../public/firmware/0.11.1/catalog.json',import.meta.url)));
const recoveryImages = await Promise.all(recovery.files.map(f=>verifiedDownload(f,base,localFetch)));
const preview = catalog.knownApplications.find(app=>app.runtime==='0.11.1-sticks3-effectpreview-dev');
function previewDevice(identity = preview) {
  const header=new Uint8Array(1024);header[0]=0xe9;new DataView(header.buffer).setUint32(32,0xabcd5432,true);
  header.set(Uint8Array.from(identity.elfSha256.match(/../g),x=>parseInt(x,16)),176);
  const l=fake({app:header});
  l.flashMd5sum=async function(address,size){assert.equal(address,0x10000);if(!this.writes.length){assert.equal(size,identity.size);return identity.md5;}return md5(this.app.subarray(0,size));};
  return l;
}
test('0.11.1 released and preview identities are distinct and both retained',()=>{
  validateCatalog(catalog);validateCatalog(recovery);
  const prior=catalog.knownApplications.filter(app=>app.version==='0.11.1');assert.equal(prior.length,2);
  assert.notEqual(prior[0].sha256,prior[1].sha256);assert.notEqual(prior[0].elfSha256,prior[1].elfSha256);
  assert.equal(preview.sha256,'6ba3dfd7ce04824133ee770be77df66a40aadf2be1e1bde1f70dd06a1f078bd6');
});
test('verified preview identity updates to 0.12.1 with application-only write',async()=>{
  const l=previewDevice();assert.equal((await inspectInstalledApplication(l,catalog)).application.runtime,preview.runtime);
  assert.equal((await flashVerified(opts(l))).status,'verified');assert.deepEqual(l.writes[0].fileArray.map(f=>f.address),[0x10000]);assert.equal(l.writes[0].eraseAll,false);
});
test('same-version preview to released 0.11.1 requires intentional repair',async()=>{
  const l=previewDevice();await assert.rejects(flashVerified({...opts(l),catalog:recovery,images:recoveryImages}),/different build of the same version/);assert.equal(l.writes.length,0);
  assert.equal((await flashVerified({...opts(l),catalog:recovery,images:recoveryImages,mode:'recovery',recoveryConfirmed:true})).status,'verified');assert.deepEqual(l.writes[0].fileArray.map(f=>f.address),[0x10000]);
});
test('older private 0.12.1 remains recognized and requires repair to the pulse-guard build',async()=>{
  const previous=catalog.knownApplications.find(app=>app.sha256==='25eb22f75833dc4ce353a3c69aca1bfd3330ed0209396b4c9020d29cf831e7e5');
  assert.ok(previous);assert.equal(previous.version,'0.12.1');
  assert.equal(catalog.files[3].sha256,'a13d590f11c3c8e8f8ab84a0b8ebd8d69761cf1046e2a0f9fd1d9c162d01ad41');
  const l=previewDevice(previous);
  assert.equal((await inspectInstalledApplication(l,catalog)).application.sha256,previous.sha256);
  await assert.rejects(flashVerified(opts(l)),/different build of the same version/);
  assert.equal(l.writes.length,0);
  assert.equal((await flashVerified({...opts(l),mode:'recovery',recoveryConfirmed:true})).status,'verified');
  assert.deepEqual(l.writes[0].fileArray.map(f=>f.address),[0x10000]);
  assert.equal((await flashVerified(opts(l))).status,'up-to-date');
  assert.equal(l.writes.length,1);
});
test('post-write application corruption is reported as a verification failure',async()=>{
  const l=fake();const write=l.writeFlash;l.writeFlash=async function(options){await write.call(this,options);this.app[10000]^=1;};
  await assert.rejects(flashVerified(opts(l)),/Post-write device MD5 mismatch/);assert.equal(l.writes.length,1);
});
test('verified release can deliberately return to preserved 0.11.1 application',async()=>{
  const l=fake({app:images[3].data});assert.equal((await flashVerified({...opts(l),catalog:recovery,images:recoveryImages,mode:'recovery',recoveryConfirmed:true})).status,'verified');assert.deepEqual(l.writes[0].fileArray.map(f=>f.address),[0x10000]);assert.equal(l.writes[0].eraseAll,false);
});
test('0.13.8 default upgrades preserved 0.12.1 through an ordinary app-only update',async()=>{
  const l=fake({app:images[3].data});
  assert.equal((await inspectInstalledApplication(l,recommendedCatalog)).application.version,'0.12.1');
  assert.equal((await flashVerified({...recommendedOpts(l),catalog:recommendedCatalog,images:recommendedImages})).status,'verified');
  assert.deepEqual(l.writes[0].fileArray.map(f=>f.address),[0x10000]);
  assert.equal(l.writes[0].eraseAll,false);
});
