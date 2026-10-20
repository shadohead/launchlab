import {compareVersions, validateCatalog} from './flasher.js';

// A release is beta once it passes the automated host and on-device (HIL) suites,
// and stable only after the owner hand-tests it. "latest" and "releases" hold stable
// releases alone, which is all an older reader looks at; betas sit in "betaReleases"
// and "beta" names the newest one while it is newer than latest. An index from before
// channels has neither field and every release in it is stable.
export const channelOf=release=>release.channel ?? 'stable';
export const listedReleases=index=>[...index.releases,...(index.betaReleases ?? [])].sort((a,b)=>compareVersions(b.version,a.version));

export function validateReleaseIndex(index) {
  if (!Array.isArray(index.releases) || !index.releases.length || index.releases.length > 20) throw new Error('Firmware release list unavailable.');
  if (index.betaReleases!==undefined && (!Array.isArray(index.betaReleases) || index.betaReleases.length > 20)) throw new Error('Firmware release list unavailable.');
  if ((index.beta===undefined)!==(index.betaReleases===undefined)) throw new Error('Firmware release list unavailable.');
  if (index.releases.some(r=>channelOf(r)!=='stable') || index.betaReleases?.some(r=>r.channel!=='beta')) throw new Error('Invalid release channel.');
  const seen = new Set();
  for (const release of listedReleases(index)) {
    compareVersions(release.version, release.version);
    if (seen.has(release.version) || release.catalog !== 'firmware/' + release.version + '/catalog.json') throw new Error('Invalid release selection.');
    seen.add(release.version);
  }
  if (!index.releases.some(r=>r.version===index.latest) || index.releases.some(release=>compareVersions(release.version,index.latest)>0)) throw new Error('Recommended release mismatch.');
  const newest=(index.betaReleases ?? []).map(r=>r.version).sort((a,b)=>compareVersions(b,a))[0];
  if ((index.beta ?? null)!==(newest && compareVersions(newest,index.latest)>0 ? newest : null)) throw new Error('Beta release mismatch.');
  return index;
}

export async function loadReleaseIndex(baseUrl, fetchFn=fetch) {
  const response=await fetchFn(new URL('firmware/versions.json',baseUrl),{cache:'no-store'});
  if (!response.ok) throw new Error('Firmware release list unavailable.');
  return validateReleaseIndex(await response.json());
}

// Invalidate the old catalog immediately. A slow response from an earlier
// selection must never enable a flash using the wrong release.
export function createReleaseSelection({index, baseUrl, fetchFn=fetch, onChange=()=>{}}) {
  validateReleaseIndex(index);
  let generation=0, state={loading:false,catalog:null,version:null,error:null};
  const set=next=>{state=next;onChange(state);};
  return {
    get state(){return state;},
    async select(version) {
      const request=++generation;
      set({loading:true,catalog:null,version,error:null});
      try {
        const release=listedReleases(index).find(r=>r.version===version);
        if (!release) throw new Error('Unknown firmware release.');
        const response=await fetchFn(new URL(release.catalog,baseUrl),{cache:'no-store'});
        if (!response.ok) throw new Error('Selected firmware catalog unavailable.');
        const catalog=validateCatalog(await response.json());
        if (catalog.version!==version) throw new Error('Selected firmware version does not match its catalog.');
        if (request===generation) set({loading:false,catalog,version,error:null});
      } catch(error) {
        if (request===generation) set({loading:false,catalog:null,version,error});
      }
      return state;
    }
  };
}
