export const APP_OFFSET = 0x10000;
export const APP_IDENTITY_BYTES = 1024;
// A sanity bound on a fetched catalog, not a product rule: a catalog recognizes every published
// application, stable and beta, plus the few private builds, so this exceeds both release lists together.
export const MAX_KNOWN_APPLICATIONS = 1200;
const EXPECTED_FILES = [['bootloader.bin', 0], ['partitions.bin', 0x8000], ['boot_app0.bin', 0xe000], ['application.bin', APP_OFFSET]];
// The partition table ends at 0x9000; the NVS area before boot_app0 is not
// part of its writable slot, even though it is absent from the image bundle.
const FILE_REGION_LIMITS = [0x8000, 0x9000, APP_OFFSET, APP_OFFSET + 0x330000];
const stop = (code, message) => Object.assign(new Error(message), {code});
const EXPECTED_PARTITIONS = [
  [1,2,0x9000,0x5000,'nvs'], [1,0,0xe000,0x2000,'otadata'],
  [0,16,APP_OFFSET,0x330000,'app0'], [0,17,0x340000,0x330000,'app1'],
  [1,130,0x670000,0x180000,'spiffs'], [1,3,0x7f0000,0x10000,'coredump']
];
function validatePartitionLayout(data, md5) {
  // These bundles support one reviewed 8 MB layout. A new valid checksum
  // must not silently move data, app0 or the OTA selection area.
  const fail = () => {throw stop('bundle-layout', 'The firmware bundle has an unsupported partition layout. No flash was written.');};
  if (data.length < 0xe0 || typeof md5 !== 'function') fail();
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  for (let i=0; i<EXPECTED_PARTITIONS.length; i++) {
    const pos=i*32, [type,subtype,offset,size,label]=EXPECTED_PARTITIONS[i];
    const name=new TextDecoder().decode(data.slice(pos+12,pos+28)).split('\0')[0];
    if (view.getUint16(pos,true)!==0x50aa || data[pos+2]!==type || data[pos+3]!==subtype || view.getUint32(pos+4,true)!==offset || view.getUint32(pos+8,true)!==size || name!==label || view.getUint32(pos+28,true)!==0) fail();
  }
  const digest=Array.from(data.slice(0xd0,0xe0), x=>x.toString(16).padStart(2,'0')).join('');
  if (data[0xc0]!==0xeb || data[0xc1]!==0xeb || !data.slice(0xc2,0xd0).every(x=>x===255) || md5(data.slice(0,0xc0))!==digest || !data.slice(0xe0).every(x=>x===255)) fail();
}
const hex = (value, length) => typeof value === 'string' && new RegExp('^[a-f0-9]{' + length + '}$').test(value);
export function compareVersions(a, b) {
  const parts = value => {
    if (typeof value !== 'string' || !/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(value)) throw new Error('Invalid release version.');
    const p = value.split('.').map(Number);
    if (p.some(n => !Number.isSafeInteger(n))) throw new Error('Invalid release version.');
    return p;
  };
  const x = parts(a), y = parts(b);
  for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] > y[i] ? 1 : -1;
  return 0;
}
export function validateCatalog(catalog) {
  compareVersions(catalog.version, catalog.version);
  if (catalog.board !== 'M5StickS3 K150' || catalog.chip !== 'ESP32-S3' || catalog.flashBytes !== 0x800000 || catalog.applicationOffset !== APP_OFFSET) throw new Error('Unsupported board or flash layout.');
  if (typeof catalog.release !== 'string' || !catalog.release.startsWith(catalog.version + '-sticks3-') || !/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(catalog.release.slice((catalog.version + '-sticks3-').length)) || typeof catalog.runtime !== 'string' || !catalog.runtime.startsWith(catalog.version + '-')) throw new Error('Release identity mismatch.');
  if (catalog.files?.length !== 4) throw new Error('Incomplete firmware bundle.');
  catalog.files.forEach((file, i) => {
    const [name, offset] = EXPECTED_FILES[i];
    if (file.path !== 'firmware/' + catalog.version + '/' + name || file.offset !== offset || !hex(file.sha256, 64) || !Number.isInteger(file.size) || file.size <= 0) throw new Error('Invalid firmware file or address.');
    const limit = FILE_REGION_LIMITS[i];
    if (offset + file.size > limit) throw new Error('Firmware exceeds its flash region.');
  });
  if (!Array.isArray(catalog.knownApplications) || !catalog.knownApplications.length || catalog.knownApplications.length > MAX_KNOWN_APPLICATIONS) throw new Error('Missing installed-application identities.');
  const applications = new Set(), elves = new Set();
  for (const app of catalog.knownApplications) {
    compareVersions(app.version, app.version);
    if (app.sensor !== 'qre1113' || !hex(app.elfSha256, 64) || !hex(app.sha256, 64) || !hex(app.md5, 32) || !Number.isInteger(app.size) || app.size < APP_IDENTITY_BYTES || app.size > 0x330000 || applications.has(app.sha256) || elves.has(app.elfSha256)) throw new Error('Invalid installed-application identity.');
    applications.add(app.sha256); elves.add(app.elfSha256);
  }
  const target = catalog.knownApplications.find(app => app.version === catalog.version && app.sha256 === catalog.files[3].sha256);
  if (!target || target.sha256 !== catalog.files[3].sha256 || target.size !== catalog.files[3].size || target.runtime !== catalog.runtime) throw new Error('Target application identity mismatch.');
  return catalog;
}
async function sha256(data) {
  return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', data)), x => x.toString(16).padStart(2, '0')).join('');
}
export async function verifiedDownload(file, baseUrl, fetchFn = fetch) {
  const response = await fetchFn(new URL(file.path, baseUrl), {cache: 'no-store'});
  if (!response.ok) throw new Error('Download failed (' + response.status + '): ' + file.path);
  const data = new Uint8Array(await response.arrayBuffer());
  if (data.length !== file.size || await sha256(data) !== file.sha256) throw new Error('Firmware checksum mismatch. No flash was written.');
  return {...file, data};
}
export async function inspectInstalledApplication(loader, catalog) {
  const data = new Uint8Array(await loader.readFlash(APP_OFFSET, APP_IDENTITY_BYTES));
  if (data.length !== APP_IDENTITY_BYTES) throw new Error('Incomplete installed-application read. No flash was written.');
  if (data[0] !== 0xe9 || new DataView(data.buffer, data.byteOffset, data.byteLength).getUint32(32, true) !== 0xabcd5432) return {state:'unknown'};
  const elf = Array.from(data.slice(176,208), x => x.toString(16).padStart(2,'0')).join('');
  const app = catalog.knownApplications.find(a => a.elfSha256 === elf);
  if (!app) return {state:'unknown'};
  // Arduino's descriptor version is a build label. Identify the archived ELF,
  // then verify the complete installed image, not just its descriptor.
  const digest = await loader.flashMd5sum(APP_OFFSET, app.size);
  return {state:typeof digest === 'string' && digest.toLowerCase() === app.md5 ? 'verified' : 'damaged', application:app};
}
export async function flashVerified({loader, catalog, images, mode, recoveryConfirmed=false, installConfirmed=false, md5, reportProgress, onInspect=()=>{}, onDevice=()=>{}, onWriteStart=()=>{}}) {
  validateCatalog(catalog);
  if (!['update', 'install', 'recovery'].includes(mode)) throw new Error('Unknown install mode.');
  if (mode === 'install' && installConfirmed !== true) throw new Error('Confirm first install and backup before continuing.');
  if (mode === 'recovery' && recoveryConfirmed !== true) throw new Error('Confirm intentional repair or downgrade and backup before continuing.');
  if (loader.chip?.CHIP_NAME !== 'ESP32-S3') throw stop('device', 'This firmware requires an M5StickS3 (ESP32-S3).');
  if (images.length !== catalog.files.length) throw new Error('Incomplete verified download.');
  for (let i=0; i<images.length; i++) {
    const image=images[i], file=catalog.files[i];
    if (!(image.data instanceof Uint8Array) || image.path !== file.path || image.offset !== file.offset || image.data.length !== file.size || await sha256(image.data) !== file.sha256) throw new Error('Incomplete or changed verified download. No flash was written.');
  }
  validatePartitionLayout(images[1].data, md5);
  // The catalog's declared capacity is not proof of the attached chip's size.
  // ESP32-S3 and 8 MB still do not identify a K150: human model confirmation
  // remains required in the UI. Refuse missing, unreadable or unexpected IDs.
  if (typeof loader.detectFlashSize !== 'function') throw stop('device', 'Flash capacity could not be checked. No flash was written.');
  const flashSize = await loader.detectFlashSize();
  if (flashSize !== '8MB') throw stop('device', 'Detected flash capacity is ' + (flashSize || 'unknown') + '; K150 requires 8MB. No flash was written.');
  onDevice({chip:'ESP32-S3', flashSize});
  let installed;
  if (mode !== 'install') {
    const same = (a,b) => a.length === b.length && a.every((byte,i)=>byte===b[i]);
    const partitions = new Uint8Array(await loader.readFlash(0x8000, images[1].data.length));
    if (!same(partitions, images[1].data)) throw stop('layout', 'The device has a different partition layout. Stop and back up; do not use First install to bypass this check.');
    // This updater writes app0 only. Do not write an inactive slot when an
    // outside OTA tool has changed boot selection.
    const bootSelection = new Uint8Array(await loader.readFlash(0xe000, images[2].data.length));
    if (!same(bootSelection, images[2].data)) throw stop('layout', 'The device has a different boot selection. Stop and back up; app-only update cannot safely continue.');
    installed = await inspectInstalledApplication(loader, catalog);
    onInspect(installed);
    if (mode === 'update') {
      if (installed.state !== 'verified') throw stop('application', 'Installed LaunchLab application is unknown or incomplete. Update stopped. Use Repair / rollback only after backup and an intentional version choice.');
      const comparison = compareVersions(catalog.version, installed.application.version);
      if (comparison < 0) throw new Error('Downgrade blocked: installed v' + installed.application.version + ', selected v' + catalog.version + '. Keep the newer release or deliberately use Repair / rollback.');
      if (comparison === 0 && installed.application.sha256 !== catalog.files[3].sha256) throw stop('application', 'A different build of the same version requires deliberate Repair / rollback.');
      if (comparison === 0) return {status:'up-to-date', installed};
    }
  }
  const selected = mode === 'install' ? images : [images[3]];
  onWriteStart();
  await loader.writeFlash({fileArray: selected.map(image => ({data:image.data, address:image.offset})), flashSize:'8MB', flashMode:'dio', flashFreq:'80m', eraseAll:false, compress:true, calculateMD5Hash:md5, reportProgress});
  const writtenDigest = await loader.flashMd5sum(APP_OFFSET, catalog.files[3].size);
  const target = catalog.knownApplications.find(app => app.sha256 === catalog.files[3].sha256);
  if (typeof writtenDigest !== 'string' || writtenDigest.toLowerCase() !== target.md5) throw stop('verification', 'Post-write device MD5 mismatch. Installation was not verified.');
  return {status:'verified', installed};
}
