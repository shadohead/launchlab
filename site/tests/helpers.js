import {readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import {APP_OFFSET, APP_IDENTITY_BYTES, compareVersions, verifiedDownload} from '../src/flasher.js';
const json=async path=>JSON.parse(await readFile(new URL(path,import.meta.url)));
// The default catalog and the release index are the only places that name the current
// release. Tests take its version, size and digests from them, so publishing or
// promoting a release changes no test; pinned facts about past releases stay literal.
export const catalog=await json('../public/firmware/catalog.json');
export const index=await json('../public/firmware/versions.json');
export const current=catalog.version;
export const oldCatalog=await json('../public/firmware/0.10.1/catalog.json');
export const base=new URL('../public/',import.meta.url);
export const localFetch=async url=>({ok:true,arrayBuffer:async()=>{const b=await readFile(url);return b.buffer.slice(b.byteOffset,b.byteOffset+b.byteLength);}});
export const images=await Promise.all(catalog.files.map(f=>verifiedDownload(f,base,localFetch)));
export const oldImages=await Promise.all(oldCatalog.files.map(f=>verifiedDownload(f,base,localFetch)));
// A preserved release's own catalog and its verified images.
const kept=new Map();
export const preserved=version=>{
  if(!kept.has(version))kept.set(version,json('../public/firmware/'+version+'/catalog.json').then(async c=>({catalog:c,images:await Promise.all(c.files.map(f=>verifiedDownload(f,base,localFetch)))})));
  return kept.get(version);
};
export const newestFirst=versions=>[...versions].sort((a,b)=>compareVersions(b,a));
// Every release the index offers, stable or beta, and those older than the current one.
export const listed=newestFirst([...index.releases,...index.betaReleases].map(r=>r.version));
export const olderListed=listed.filter(v=>compareVersions(v,current)<0);
// The exact refusal an ordinary Update gives when the device runs a newer release than the selected one.
export const downgradeBlocked=(installed,selected)=>new RegExp('Downgrade blocked: installed v'+installed.replaceAll('.','\\.')+', selected v'+selected.replaceAll('.','\\.')+'\\.');
export const md5=data=>createHash('md5').update(data).digest('hex');
export function fake({app=oldImages[3].data,chip='ESP32-S3',flashSize='8MB',partitions=images[1].data,boot=images[2].data}={}) {
  const l={writes:[],reads:[],capacityChecks:0,chip:{CHIP_NAME:chip},app:app.slice(),partitions:partitions.slice(),boot:boot.slice(),
    async detectFlashSize(){this.capacityChecks++;return flashSize;},
    async readFlash(address,size){
      this.reads.push({address,size});
      if(address===0x8000){assert.equal(size,this.partitions.length);return this.partitions.slice();}
      if(address===0xe000){assert.equal(size,this.boot.length);return this.boot.slice();}
      assert.equal(address,APP_OFFSET);assert.equal(size,APP_IDENTITY_BYTES);
      const data=new Uint8Array(size).fill(255);data.set(this.app.subarray(0,size));return data;
    },
    async flashMd5sum(address,size){assert.equal(address,APP_OFFSET);return md5(this.app.subarray(0,size));},
    async writeFlash(options){
      this.writes.push(options);
      for(const f of options.fileArray){
        if(f.address===APP_OFFSET)this.app=f.data.slice();
        if(f.address===0x8000)this.partitions=f.data.slice();
        if(f.address===0xe000)this.boot=f.data.slice();
      }
    },
    async main(){},async after(){}
  };
  return l;
}
export const opts=loader=>({loader,catalog,images,mode:'update',md5,reportProgress(){}});
