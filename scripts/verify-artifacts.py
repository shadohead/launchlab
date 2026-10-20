from pathlib import Path
import hashlib, json, zipfile
root = Path(__file__).resolve().parents[1]
catalog = json.loads((root/'site/public/firmware/catalog.json').read_text())
count=0
for file in catalog['files']:
    data=(root/'site/public'/file['path']).read_bytes()
    assert len(data)==file['size'] and hashlib.sha256(data).hexdigest()==file['sha256'], file['path']
    count+=1
for manifest in (root/'firmware/releases').glob('*/manifest.json'):
    m=json.loads(manifest.read_text()); data=(manifest.parent/m.get('binary','LaunchLabMini.ino.bin')).read_bytes()
    assert hashlib.sha256(data).hexdigest()==m['sha256'],manifest
    count+=1
for directory in (root/'hardware').iterdir():
    if directory.is_dir() and (directory/'SHA256SUMS.txt').exists():
        for line in (directory/'SHA256SUMS.txt').read_text().splitlines():
            digest, name=line.split(maxsplit=1); p=directory/name
            assert p.exists() and hashlib.sha256(p.read_bytes()).hexdigest()==digest,p
            count+=1
for version in json.loads((root/'hardware/versions.json').read_text())['versions']:
    for file in version['files']:
        data=(root/file['path']).read_bytes()
        assert len(data)==file['size'] and hashlib.sha256(data).hexdigest()==file['sha256'],file['path']
        count+=1
for p in root.rglob('*.3mf'):
    if 'node_modules' in p.parts or 'dist' in p.parts: continue
    with zipfile.ZipFile(p) as z:
        assert z.testzip() is None,p
        assert '[Content_Types].xml' in z.namelist(),p
index=json.loads((root/'site/public/firmware/versions.json').read_text())
# Channels: latest and releases are stable only (all an older reader sees); betas sit in
# betaReleases, and beta names the newest one while it is newer than latest.
key=lambda v: tuple(map(int,v.split('.')))
assert all(e['channel']=='stable' for e in index['releases']) and all(e['channel']=='beta' for e in index['betaReleases'])
assert index['latest']==max((e['version'] for e in index['releases']),key=key)
newest=max((e['version'] for e in index['betaReleases']),key=key,default=None)
assert index['beta']==(newest if newest and key(newest)>key(index['latest']) else None)
assert catalog['version']==index['latest']
assert catalog==json.loads((root/f"site/public/firmware/{index['latest']}/catalog.json").read_text())
for entry in index['releases']+index['betaReleases']:
    listed=json.loads((root/'site/public'/entry['catalog']).read_text())
    assert listed['version']==entry['version']
    # A beta was published as one: its catalog and frozen manifest say so.
    if entry['channel']=='beta':
        assert listed.get('published_channel')=='beta',entry
        assert json.loads((root/'firmware/releases'/listed['release']/'manifest.json').read_text()).get('published_channel')=='beta',entry
# Withdrawal changes the recommendation, not the preserved source or payloads.
# Verify every versioned catalog, including releases removed from the picker.
for catalog_path in (root/'site/public/firmware').glob('*/catalog.json'):
    c=json.loads(catalog_path.read_text())
    assert c['version']==catalog_path.parent.name
    frozen=root/'firmware/releases'/c['release']
    m=json.loads((frozen/'manifest.json').read_text())
    assert c['runtime']==m['runtime_version']
    for f in c['files']:
        data=(root/'site/public'/f['path']).read_bytes()
        assert len(data)==f['size'] and hashlib.sha256(data).hexdigest()==f['sha256'], f['path']
    app=(root/'site/public'/c['files'][3]['path']).read_bytes()
    assert app==(frozen/m.get('binary','LaunchLabMini.ino.bin')).read_bytes()
    known=next(x for x in c['knownApplications'] if x['version']==c['version'] and x['sha256']==c['files'][3]['sha256'])
    assert known['sha256']==hashlib.sha256(app).hexdigest()
    assert known['md5']==hashlib.md5(app).hexdigest()
    assert known['elfSha256']==app[176:208].hex()
    assert known['size']==len(app)
    count+=len(c['files'])
source_version=json.loads((root/'site/package.json').read_text())['version']
source_catalog=json.loads((root/f'site/public/firmware/{source_version}/catalog.json').read_text())
current = root/'firmware/releases'/source_catalog['release']
assert (root/'site/public'/source_catalog['files'][3]['path']).read_bytes()==(current/'LaunchLabMini.ino.bin').read_bytes()
assert source_catalog['runtime']==json.loads((current/'manifest.json').read_text())['runtime_version']
# Verify the current release's metadata as well as its shipping application.
manifest=json.loads((current/'manifest.json').read_text())
for section,base in [('source_sha256',current/'source'),('artifact_sha256',current)]:
    for name,digest in manifest[section].items():
        assert hashlib.sha256((base/name).read_bytes()).hexdigest()==digest,(section,name)
for line in (current/'SHA256SUMS').read_text().splitlines():
    digest,name=line.split(maxsplit=1)
    assert hashlib.sha256((current/name).read_bytes()).hexdigest()==digest,name
for path in (root/'firmware/LaunchLabMini').rglob('*'):
    if path.is_file():
        # Working instructions can be corrected without changing a frozen
        # release. Keep exact equality for every production source file.
        if path == root/'firmware/LaunchLabMini/README.md':
            assert (current/'source/README.md').is_file()
            continue
        assert path.read_bytes()==(current/'source'/path.relative_to(root/'firmware/LaunchLabMini')).read_bytes(),path
for path in (current/'source').rglob('*'):
    if path.is_file() and path != current/'source/README.md':
        working=root/'firmware/LaunchLabMini'/path.relative_to(current/'source')
        assert working.is_file() and working.read_bytes()==path.read_bytes(),working
assert (root/'firmware/LaunchLabRpm/analog_tachometer.h').read_bytes()==(current/'source/analog_tachometer.h').read_bytes()
# Current hardware presentation and downloads are independently hash-pinned.
# Frozen firmware and historical hardware continue through the checks above.
hardware_provenance=root/'site/public/provenance/hardware.json'
assert hardware_provenance.is_file(), hardware_provenance
h=json.loads(hardware_provenance.read_text())
for entry in h['releases']:
    for f in entry['outputs']:
        data=(root/'site/public'/f['path']).read_bytes()
        assert len(data)==f['size'] and hashlib.sha256(data).hexdigest()==f['sha256'],f['path']
        count+=1
        if f['kind']=='package':
            with zipfile.ZipFile(root/'site/public'/f['path']) as archive:
                names=archive.namelist()
                assert len(names)==f['files'] and archive.testzip() is None,f['path']
                inventories=[n for n in names if n.endswith('/SHA256SUMS.txt')]
                assert len(inventories)==1,f['path']
                prefix=inventories[0].removesuffix('SHA256SUMS.txt')
                for line in archive.read(inventories[0]).decode().splitlines():
                    digest,name=line.split(maxsplit=1)
                    assert hashlib.sha256(archive.read(prefix+name)).hexdigest()==digest,(f['path'],name)
for entry in h['images']+h['models']:
    data=(root/'site/public'/entry['path']).read_bytes()
    assert hashlib.sha256(data).hexdigest()==entry['sha256'],entry['path']
    if 'size' in entry: assert len(data)==entry['size'],entry['path']
    count+=1
proof=root/'site/public'/h['pipeline']['sourceProof']
assert hashlib.sha256(proof.read_bytes()).hexdigest()==h['pipeline']['sourceProofSha256'],proof
assert json.loads(proof.read_text())['status']=='PASS'
print(f"PASS: {count} checksums; all 3MF archives intact; preserved catalog {catalog['version']} and source {source_version} independently match frozen firmware")
