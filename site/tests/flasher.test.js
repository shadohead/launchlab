import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFile} from 'node:fs/promises';
import {APP_OFFSET, APP_IDENTITY_BYTES, compareVersions, validateCatalog, verifiedDownload, flashVerified, inspectInstalledApplication} from '../src/flasher.js';
import {catalog,oldCatalog,base,localFetch,images,oldImages,fake,opts} from './helpers.js';
const archivedCatalog=JSON.parse(await readFile(new URL('../public/firmware/0.12.1/catalog.json',import.meta.url)));
const archivedImages=await Promise.all(archivedCatalog.files.map(file=>verifiedDownload(file,base,localFetch)));
test('default catalog selects exact 0.15.0 while withdrawn 0.12.1 bytes are preserved',()=>{
  assert.equal(catalog.version,'0.15.0');assert.equal(images[3].data.length,863408);
  assert.equal(oldCatalog.version,'0.10.1');assert.equal(oldImages[3].data.length,731728);
  assert.equal(catalog.files[3].sha256,'48b827825ee721ac42a4c1ebdcc4abdddd6d690b8cd5c090b791aa378726a99e');
  assert.equal(archivedImages[3].data.length,808064);
  assert.equal(archivedCatalog.files[3].sha256,'a13d590f11c3c8e8f8ab84a0b8ebd8d69761cf1046e2a0f9fd1d9c162d01ad41');
});
test('release downloads have exact sizes and SHA-256',()=>assert.equal(images.length,4));
test('versions compare numerically, not lexically or as decimals',()=>{
  assert.equal(compareVersions('0.10.7','0.10.1'),1);assert.equal(compareVersions('0.10.7','0.10.10'),-1);
  assert.equal(compareVersions('0.10.7','0.9.9'),1);assert.equal(compareVersions('1.0.0','1.0.0'),0);
  for(const value of ['0.10','0.10.7-beta','00.10.7','../0.10.7',null,'999999999999999999.0.0'])assert.throws(()=>compareVersions(value,'0.10.7'));
});
test('older verified application updates app only and never erases all',async()=>{
  const l=fake();const result=await flashVerified(opts(l));assert.equal(result.status,'verified');
  assert.deepEqual(l.writes[0].fileArray.map(f=>f.address),[APP_OFFSET]);assert.equal(l.writes[0].eraseAll,false);
  assert.equal(typeof l.writes[0].calculateMD5Hash,'function');
});
test('known newer installed application blocks an older selected target',async()=>{
  const l=fake({app:images[3].data});
  await assert.rejects(flashVerified({...opts(l),catalog:oldCatalog,images:oldImages}),/Downgrade blocked.*0.15.0.*0.10.1/);
  assert.equal(l.writes.length,0);
});
test('already installed exact version is a no-op; repeated update stays a no-op',async()=>{
  const l=fake();await flashVerified(opts(l));
  for(let i=0;i<2;i++)assert.equal((await flashVerified(opts(l))).status,'up-to-date');
  assert.equal(l.writes.length,1);
});
test('Arduino descriptor build label is not interpreted as LaunchLab version',async()=>{
  const descriptor=images[3].data.slice(48,80);assert.notEqual(new TextDecoder().decode(descriptor).split('\0')[0],catalog.version);
  const l=fake({app:images[3].data});const found=await inspectInstalledApplication(l,catalog);
  assert.equal(found.state,'verified');assert.equal(found.application.version,'0.15.0');
});
test('complete device image checksum is required beyond matching ELF descriptor',async()=>{
  const app=images[3].data.slice();app[10000]^=1;const l=fake({app});
  await assert.rejects(flashVerified(opts(l)),/unknown or incomplete/);assert.equal(l.writes.length,0);
});
test('unknown future or factory application blocks normal update',async()=>{
  for(const app of [new Uint8Array(APP_IDENTITY_BYTES).fill(255),images[3].data.slice()]){
    if(app[0]===0xe9)app[176]^=1;
    const l=fake({app});await assert.rejects(flashVerified(opts(l)),/unknown or incomplete/);assert.equal(l.writes.length,0);
  }
});
test('repair and rollback require explicit confirmation',async()=>{
  const l=fake({app:images[3].data});
  for(const confirmed of [undefined,false,'true'])await assert.rejects(flashVerified({...opts(l),catalog:oldCatalog,images:oldImages,mode:'recovery',recoveryConfirmed:confirmed}),/Confirm intentional/);
  assert.equal(l.writes.length,0);
  const result=await flashVerified({...opts(l),catalog:oldCatalog,images:oldImages,mode:'recovery',recoveryConfirmed:true});
  assert.equal(result.status,'verified');assert.deepEqual(l.writes[0].fileArray.map(f=>f.address),[APP_OFFSET]);
  assert.equal((await inspectInstalledApplication(l,catalog)).application.version,'0.10.1');
});
test('interrupted image blocks update; explicit app-only repair recovers; next update is no-op',async()=>{
  const l=fake();const write=l.writeFlash;
  l.writeFlash=async function(o){await write.call(this,o);this.app[10000]^=1;throw new Error('USB unplugged');};
  await assert.rejects(flashVerified(opts(l)),/USB unplugged/);
  l.writeFlash=write;
  await assert.rejects(flashVerified(opts(l)),/unknown or incomplete/);
  await flashVerified({...opts(l),mode:'recovery',recoveryConfirmed:true});
  assert.equal((await flashVerified(opts(l))).status,'up-to-date');assert.equal(l.writes.length,2);
  assert.ok(l.writes.every(w=>w.fileArray.every(f=>f.address===APP_OFFSET)));
});
test('first install uses exact offsets and requires explicit backup confirmation',async()=>{
  const l=fake({app:new Uint8Array(1024).fill(255)});
  await assert.rejects(flashVerified({...opts(l),mode:'install'}),/Confirm first install/);
  await flashVerified({...opts(l),mode:'install',installConfirmed:true});
  assert.deepEqual(l.writes[0].fileArray.map(f=>f.address),[0,0x8000,0xe000,APP_OFFSET]);assert.equal(l.writes[0].eraseAll,false);
  assert.equal(l.reads.length,0);
});
test('wrong chip refused in every mode before writes',async()=>{
  for(const mode of ['update','install','recovery']){
    const l=fake({chip:'ESP32-S2'});await assert.rejects(flashVerified({...opts(l),mode,installConfirmed:true,recoveryConfirmed:true}),/M5StickS3/);assert.equal(l.writes.length,0);
  }
});
test('partition mismatch is refused for update and repair',async()=>{
  const p=images[1].data.slice();p[0]^=1;
  for(const mode of ['update','recovery']){
    const l=fake({partitions:p});await assert.rejects(flashVerified({...opts(l),mode,recoveryConfirmed:true}),/partition layout/);assert.equal(l.writes.length,0);
  }
});
test('different OTA boot selection is refused before inactive-slot writes',async()=>{
  const b=images[2].data.slice();b[0]^=1;
  for(const mode of ['update','recovery']){
    const l=fake({boot:b});await assert.rejects(flashVerified({...opts(l),mode,recoveryConfirmed:true}),/boot selection/);assert.equal(l.writes.length,0);
  }
});
test('tampered download refused',async()=>{
  await assert.rejects(verifiedDownload(catalog.files[3],base,async()=>({ok:true,arrayBuffer:async()=>new Uint8Array([1,2]).buffer})),/checksum mismatch/);
});
test('HTTP failure reported',async()=>{await assert.rejects(verifiedDownload(catalog.files[3],base,async()=>({ok:false,status:404})),/404/);});
test('catalog cannot write NVS, use external URLs, mix versions or invent target identity',()=>{
  for(const mutate of [c=>c.files[3].offset=0x9000,c=>c.files[3].path='https://bad.example/app.bin',c=>c.files[0].size=0x9000,c=>c.files[3].sha256='bad',c=>c.files[0].path='firmware/0.10.1/bootloader.bin',c=>c.runtime='0.10.1-sticks3-tiltreplay',c=>c.knownApplications=[],c=>c.knownApplications.push(c.knownApplications[0])]){
    const c=structuredClone(catalog);mutate(c);assert.throws(()=>validateCatalog(c));
  }
});
test('same-length changed in-memory image is refused before any device read or write',async()=>{
  const changed=images.map(f=>({...f,data:f.data.slice()}));changed[3].data[10000]^=1;
  const l=fake();await assert.rejects(flashVerified({...opts(l),images:changed}),/changed verified/);assert.equal(l.writes.length,0);assert.equal(l.reads.length,0);
});
test('missing images or invalid mode cannot flash',async()=>{
  const l=fake();await assert.rejects(flashVerified({...opts(l),images:[]}),/Incomplete/);
  await assert.rejects(flashVerified({...opts(l),mode:'erase'}),/Unknown/);assert.equal(l.writes.length,0);
});
test('read interruption refuses write; a fresh subsequent attempt checks again',async()=>{
  const l=fake();const read=l.readFlash;l.readFlash=async()=>{throw new Error('read disconnected');};
  await assert.rejects(flashVerified(opts(l)),/read disconnected/);assert.equal(l.writes.length,0);
  l.readFlash=read;await flashVerified(opts(l));assert.equal(l.writes.length,1);
});
test('write verification failure is propagated',async()=>{
  const l=fake();l.writeFlash=async()=>{throw new Error('MD5 mismatch');};
  await assert.rejects(flashVerified(opts(l)),/MD5 mismatch/);
});
test('fresh first install skips old-layout reads and never addresses NVS',async()=>{
  const l=fake();l.readFlash=async()=>{throw new Error('Factory device must not be treated as update');};
  await flashVerified({...opts(l),mode:'install',installConfirmed:true});
  assert.equal(l.writes[0].flashMode,'dio');assert.equal(l.writes[0].flashSize,'8MB');
  for(const f of l.writes[0].fileArray)assert.ok(f.address+f.data.length<=0x9000||f.address>=0xe000);
});
test('first install refuses partition tables extending into NVS before any device read or write',async()=>{
  for(const size of [0x1001,0x6000]){
    const c=structuredClone(catalog);
    const changed=images.map(f=>({...f,data:f.data.slice()}));
    const table=new Uint8Array(size).fill(255);table.set(changed[1].data);
    c.files[1].size=size;c.files[1].sha256=createHash('sha256').update(table).digest('hex');
    changed[1].data=table;
    const l=fake();
    await assert.rejects(flashVerified({...opts(l),catalog:c,images:changed,mode:'install',installConfirmed:true}),/flash region/);
    assert.equal(l.writes.length,0);assert.equal(l.reads.length,0);
  }
});
test('a partition table filling exactly its 4 KB slot remains permitted',async()=>{
  const c=structuredClone(catalog);
  const changed=images.map(f=>({...f,data:f.data.slice()}));
  const table=new Uint8Array(0x1000).fill(255);table.set(changed[1].data);
  c.files[1].size=table.length;c.files[1].sha256=createHash('sha256').update(table).digest('hex');
  changed[1].data=table;
  const l=fake();
  assert.equal((await flashVerified({...opts(l),catalog:c,images:changed,mode:'install',installConfirmed:true})).status,'verified');
  assert.equal(l.writes[0].fileArray.find(f=>f.address===0x8000).data.length,0x1000);
});
test('physical capacity is checked in every mode; non-8 MB and unknown flash refuse before reads or writes',async()=>{
  for(const mode of ['update','install','recovery']) for(const flashSize of ['4MB','16MB','unknown',null]){
    const l=fake({flashSize});
    await assert.rejects(flashVerified({...opts(l),mode,installConfirmed:true,recoveryConfirmed:true}),/flash capacity/);
    assert.equal(l.capacityChecks,1);assert.equal(l.reads.length,0);assert.equal(l.writes.length,0);
  }
});
test('missing capacity API and flash-ID read failure cannot trigger a write',async()=>{
  for(const mode of ['update','install','recovery']){
    const l=fake();delete l.detectFlashSize;
    await assert.rejects(flashVerified({...opts(l),mode,installConfirmed:true,recoveryConfirmed:true}),/capacity could not/);
    l.detectFlashSize=async()=>{throw new Error('flash ID disconnected');};
    await assert.rejects(flashVerified({...opts(l),mode,installConfirmed:true,recoveryConfirmed:true}),/flash ID disconnected/);
    assert.equal(l.reads.length,0);assert.equal(l.writes.length,0);
  }
});
test('capacity is rechecked on a fresh attempt and a same-version no-op',async()=>{
  const l=fake();await flashVerified(opts(l));assert.equal((await flashVerified(opts(l))).status,'up-to-date');
  assert.equal(l.capacityChecks,2);assert.equal(l.writes.length,1);
});
test('checksummed bundles cannot move a partition, corrupt its embedded digest or append an entry',async()=>{
  for(const mutate of [p=>p[4]^=1,p=>p[0x48]^=1,p=>p[0xd0]^=1,p=>p[0xe0]=0xaa]){
    const c=structuredClone(catalog), changed=images.map(f=>({...f,data:f.data.slice()}));
    mutate(changed[1].data);c.files[1].sha256=createHash('sha256').update(changed[1].data).digest('hex');
    for(const mode of ['update','install','recovery']){
      const l=fake();
      await assert.rejects(flashVerified({...opts(l),catalog:c,images:changed,mode,installConfirmed:true,recoveryConfirmed:true}),/unsupported partition layout/);
      assert.equal(l.capacityChecks,0);assert.equal(l.reads.length,0);assert.equal(l.writes.length,0);
    }
  }
});

test('every preserved older catalog recognizes 0.15.0 and blocks its downgrade',async()=>{
  for(const version of ['0.10.1','0.10.7','0.10.11','0.10.14','0.10.21','0.10.22','0.11.1','0.11.2','0.12.1','0.12.2','0.13.5','0.13.7','0.13.8','0.13.9','0.14.0']){
    const previous=JSON.parse(await readFile(new URL('../public/firmware/'+version+'/catalog.json',import.meta.url)));
    const previousImages=await Promise.all(previous.files.map(file=>verifiedDownload(file,base,localFetch)));
    const loader=fake({app:images[3].data});
    await assert.rejects(flashVerified({...opts(loader),catalog:previous,images:previousImages}),/Downgrade blocked.*0.15.0/);
    assert.equal(loader.writes.length,0);
  }
});

// Sensor-neutral release names retain all version, path and board guards.
test('release identity permits original QRE names and current shared-profile names only',()=>{
  assert.equal(validateCatalog(oldCatalog),oldCatalog);
  for(const selected of [catalog,archivedCatalog]){
    assert.equal(validateCatalog(selected),selected);
    const version=selected.version;
    for(const release of [`${version}-sticks3-`,`${version}-sticks3-..`,`${version}-sticks3-flip-colors/extra`,`${version}-sticks3-../bad`,`${version}-stickc-flip-colors`,'0.12.0-sticks3-flip-colors',`https://example.com/${version}-sticks3-flip-colors`,`${version}-sticks3-Flip-Colors`]){
      const c=structuredClone(selected);c.release=release;assert.throws(()=>validateCatalog(c),/Release identity/);
    }
  }
});
