// Every listed release older than the current one updates to it with an application-only
// write, and going back is a deliberate repair that keeps the boot infrastructure.
import test from 'node:test';
import assert from 'node:assert/strict';
import {APP_OFFSET,compareVersions,flashVerified,inspectInstalledApplication} from '../src/flasher.js';
import {catalog,images,current,index,olderListed,preserved,downgradeBlocked,fake,opts} from './helpers.js';
// Releases that each had these two tests written out by hand while 0.20.8 was current.
const testedBefore=['0.12.2','0.13.5','0.13.7','0.13.8','0.13.9','0.15.0','0.18.0','0.20.2','0.20.4','0.20.5','0.20.6','0.20.7'];
test('the upgrade and rollback tests cover every listed older release, none tested before left out',()=>{
  assert.ok(olderListed.length>0);
  for(const version of testedBefore.filter(v=>compareVersions(v,current)<0))assert.ok(olderListed.includes(version),version+' is still tested');
});
for(const version of olderListed){
  const {catalog:prior,images:previous}=await preserved(version);
  test(version+' upgrades to the exact current app only and a second attempt is a no-op',async()=>{
    const loader=fake({app:previous[3].data});
    assert.equal((await inspectInstalledApplication(loader,catalog)).application.version,version);
    assert.equal((await flashVerified(opts(loader))).status,'verified');
    assert.deepEqual(loader.writes[0].fileArray.map(f=>f.address),[APP_OFFSET]);
    assert.equal(loader.writes[0].eraseAll,false);
    assert.deepEqual(loader.partitions,previous[1].data);assert.deepEqual(loader.boot,previous[2].data);
    assert.deepEqual(loader.app,images[3].data);
    assert.equal((await flashVerified(opts(loader))).status,'up-to-date');assert.equal(loader.writes.length,1);
  });
  test('the current release rolls back to '+version+' only with confirmation and preserves boot infrastructure',async()=>{
    const loader=fake({app:images[3].data});
    const target={...opts(loader),catalog:prior,images:previous};
    await assert.rejects(flashVerified(target),downgradeBlocked(current,version));
    await assert.rejects(flashVerified({...target,mode:'recovery'}),/Confirm intentional/);
    assert.equal(loader.writes.length,0);
    assert.equal((await flashVerified({...target,mode:'recovery',recoveryConfirmed:true})).status,'verified');
    assert.deepEqual(loader.writes[0].fileArray.map(f=>f.address),[APP_OFFSET]);assert.equal(loader.writes[0].eraseAll,false);
    assert.deepEqual(loader.partitions,images[1].data);assert.deepEqual(loader.boot,images[2].data);
    assert.deepEqual(loader.app,previous[3].data);
    assert.equal((await flashVerified(opts(loader))).status,'verified');
  });
}
// A beta newer than the current release: Update moves the current release to it, and an
// ordinary Update never takes the beta back to the current release.
if(index.beta!==null){
  const {catalog:betaCatalog,images:betaImages}=await preserved(index.beta);
  test('the current release upgrades to beta '+index.beta+' app only and stays there until a deliberate repair',async()=>{
    const loader=fake({app:images[3].data});
    assert.equal((await flashVerified({...opts(loader),catalog:betaCatalog,images:betaImages})).status,'verified');
    assert.deepEqual(loader.writes[0].fileArray.map(f=>f.address),[APP_OFFSET]);assert.equal(loader.writes[0].eraseAll,false);
    assert.deepEqual(loader.app,betaImages[3].data);
    assert.equal((await inspectInstalledApplication(loader,catalog)).application.version,index.beta);
    await assert.rejects(flashVerified(opts(loader)),downgradeBlocked(index.beta,current));assert.equal(loader.writes.length,1);
    assert.equal((await flashVerified({...opts(loader),mode:'recovery',recoveryConfirmed:true})).status,'verified');
    assert.deepEqual(loader.app,images[3].data);
  });
}
