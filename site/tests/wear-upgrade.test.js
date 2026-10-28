import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {APP_OFFSET,flashVerified,inspectInstalledApplication,verifiedDownload} from '../src/flasher.js';
import {catalog,images,base,localFetch,fake,opts} from './helpers.js';
const prior=JSON.parse(await readFile(new URL('../public/firmware/0.20.2/catalog.json',import.meta.url)));
const previous=await Promise.all(prior.files.map(f=>verifiedDownload(f,base,localFetch)));
test('0.20.2 upgrades to exact 0.20.4 app only and a second attempt is a no-op',async()=>{
  const loader=fake({app:previous[3].data});
  assert.equal((await inspectInstalledApplication(loader,catalog)).application.version,'0.20.2');
  assert.equal((await flashVerified(opts(loader))).status,'verified');
  assert.deepEqual(loader.writes[0].fileArray.map(f=>f.address),[APP_OFFSET]);
  assert.equal(loader.writes[0].eraseAll,false);
  assert.deepEqual(loader.partitions,previous[1].data);assert.deepEqual(loader.boot,previous[2].data);
  assert.deepEqual(loader.app,images[3].data);
  assert.equal((await flashVerified(opts(loader))).status,'up-to-date');assert.equal(loader.writes.length,1);
});
test('0.20.4 rollback to 0.20.2 requires confirmation and preserves boot infrastructure',async()=>{
  const loader=fake({app:images[3].data});
  const target={...opts(loader),catalog:prior,images:previous};
  await assert.rejects(flashVerified(target),/Downgrade blocked.*0.20.4.*0.20.2/);
  await assert.rejects(flashVerified({...target,mode:'recovery'}),/Confirm intentional/);
  assert.equal(loader.writes.length,0);
  assert.equal((await flashVerified({...target,mode:'recovery',recoveryConfirmed:true})).status,'verified');
  assert.deepEqual(loader.writes[0].fileArray.map(f=>f.address),[APP_OFFSET]);assert.equal(loader.writes[0].eraseAll,false);
  assert.deepEqual(loader.partitions,images[1].data);assert.deepEqual(loader.boot,images[2].data);
  assert.deepEqual(loader.app,previous[3].data);
  assert.equal((await flashVerified(opts(loader))).status,'verified');
});
