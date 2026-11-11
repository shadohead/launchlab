"""Windows local build and guarded application-only update. No device access by default."""
from pathlib import Path
import argparse, datetime, hashlib, json, re, shutil, subprocess, sys, uuid
ROOT=Path(__file__).resolve().parents[1]
CACHE=ROOT/'firmware/.arduino'
BUILD=ROOT/'firmware/build/analysis'
CLI=CACHE/'bin/arduino-cli.exe'
CONFIG=CACHE/'launchlab-config.yaml'
SDK=CACHE/'pm-sdk/esp32-arduino-libs'
PYTHON=CACHE/'flash-venv/Scripts/python.exe'
FQBN='esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi'
RELEASE=ROOT/'firmware/releases/0.20.8-sticks3-songs-pm'
MANIFEST=BUILD/'local-build.json'
APP_OFFSET=0x10000
FLASH_SIZE=0x800000
APP_SIZE=0x330000

def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def run(args):
    subprocess.run([str(a) for a in args],check=True,cwd=ROOT)
def compile_logged(args):
    with (BUILD/'build.log').open('w',encoding='utf-8') as log:
        process=subprocess.Popen([str(a) for a in args],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
        for line in process.stdout:
            print(line,end='',flush=True);log.write(line);log.flush()
        if process.wait():raise RuntimeError('Build failed. See '+str(BUILD/'build.log'))
def source_digest():
    h=hashlib.sha256();base=ROOT/'firmware/LaunchLabMini'
    for path in sorted(base.rglob('*')):
        if path.is_file() and path.suffix in ('.ino','.h','.hpp','.c','.cpp'):
            h.update(path.relative_to(base).as_posix().encode());h.update(path.read_bytes())
    return h.hexdigest()
def build():
    BUILD.mkdir(parents=True,exist_ok=True)
    MANIFEST.unlink(missing_ok=True)
    print('Verifying complete power-management SDK...',flush=True)
    for name,digest in json.loads((SDK/'sha256-manifest.json').read_text()).items():
        if sha(SDK/name)!=digest:raise RuntimeError('SDK checksum mismatch: '+name)
    config=(SDK/'esp32s3/qio_opi/include/sdkconfig.h').read_text()
    for flag in ('CONFIG_PM_ENABLE','CONFIG_PM_PROFILING'):
        if '#define '+flag+' 1' not in config:raise RuntimeError('Missing '+flag)
    if '#define CONFIG_PM_DFS_INIT_AUTO 1' in config:raise RuntimeError('Unexpected SDK DFS configuration')
    for name,version in [('M5Unified','0.2.23'),('M5GFX','0.2.30')]:
        properties=(CACHE/'user/libraries'/name/'library.properties').read_text()
        if 'version='+version not in properties.splitlines():raise RuntimeError('Unexpected library version: '+name)
    before=source_digest()
    compile_logged([CLI,'compile','--config-file',CONFIG,'--fqbn',FQBN,'--jobs','4',
         '--build-property','tools.esp32-arduino-libs.path='+SDK.as_posix(),
         '--build-property','compiler.cpp.extra_flags=-DVQF_SINGLE_PRECISION -fstack-usage -mtext-section-literals -DLAUNCHLAB_MOTION_ADC_GATE=1 -DLAUNCHLAB_CPU_PM=1',
         '--build-path',BUILD,ROOT/'firmware/LaunchLabMini'])
    run([PYTHON,ROOT/'firmware/tools/check_stack_usage.py',BUILD])
    if before!=source_digest():raise RuntimeError('Source changed during compilation; rebuild')
    app=BUILD/'LaunchLabMini.ino.bin';part=BUILD/'LaunchLabMini.ino.partitions.bin'
    if not 0<app.stat().st_size<=APP_SIZE:raise RuntimeError('Application exceeds app0 capacity')
    if part.read_bytes()!=(RELEASE/'LaunchLabMini.ino.partitions.bin').read_bytes():raise RuntimeError('Unexpected partition layout')
    shutil.copy2(RELEASE/'boot_app0.bin',BUILD/'boot_app0.bin')
    data={'runtime':'0.20.8-analysis-dev-pm','fqbn':FQBN,'source_sha256':before,'application_offset':APP_OFFSET,
          'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'files':{name:{'sha256':sha(BUILD/name),'bytes':(BUILD/name).stat().st_size} for name in
                   ('LaunchLabMini.ino.bin','LaunchLabMini.ino.partitions.bin','boot_app0.bin')}}
    MANIFEST.write_text(json.dumps(data,indent=2)+'\n')
    print('BUILD READY:',app,'\nSHA256:',sha(app))
def verified_build():
    data=json.loads(MANIFEST.read_text())
    if data['source_sha256']!=source_digest():raise RuntimeError('Source changed since build; run build-local.ps1 again')
    for name,info in data['files'].items():
        if sha(BUILD/name)!=info['sha256']:raise RuntimeError('Build file checksum mismatch: '+name)
    return data

def validate_backup(path):
    data=path.read_bytes()
    if len(data)!=FLASH_SIZE:raise RuntimeError('Backup must contain the entire 8 MB flash')
    for name,offset in [('LaunchLabMini.ino.partitions.bin',0x8000),('boot_app0.bin',0xe000)]:
        expected=(BUILD/name).read_bytes()
        if data[offset:offset+len(expected)]!=expected:raise RuntimeError('Incompatible partition layout or boot selection; app-only flashing refused')
    if data[APP_OFFSET]!=0xe9:raise RuntimeError('No valid application header in app0')
    # An explicit development update, never a first-install tool.
    app=data[APP_OFFSET:APP_OFFSET+APP_SIZE]
    if b'LAUNCH_RESULT' not in app:raise RuntimeError('Existing application is not recognized as LaunchLab')
    return data

def esptool(port,*args,after='no-reset'):
    run([PYTHON,'-m','esptool','--chip','esp32s3','--port',port,'--baud','460800','--after',after,*args])
def backup(port):
    folder=CACHE/'device-backups';folder.mkdir(exist_ok=True)
    name=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8]
    path=folder/(name+'.bin')
    esptool(port,'read-flash','0',str(FLASH_SIZE),path,after='hard-reset')
    if path.stat().st_size!=FLASH_SIZE:raise RuntimeError('Incomplete backup')
    receipt={'sha256':sha(path),'bytes':FLASH_SIZE,'port':port,'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
    path.with_suffix('.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Full backup saved:',path,'\nSHA256:',receipt['sha256'])
    print('Backup complete. Flash preflight separately checks device compatibility.')
    return path

def flash_files():
    info=verified_build()
    names=('LaunchLabMini.ino.bin','LaunchLabMini.ino.partitions.bin','boot_app0.bin')
    files={name:(BUILD/name).read_bytes() for name in names}
    # Verify the actual in-memory bytes used below, including required artifact lengths.
    for name,data in files.items():
        entry=info['files'][name]
        if hashlib.sha256(data).hexdigest()!=entry['sha256'] or len(data)!=entry['bytes']:
            raise RuntimeError('Build file checksum/size mismatch: '+name)
    app=files[names[0]]
    if not 288<=len(app)<=APP_SIZE or app[0]!=0xe9:
        raise RuntimeError('Invalid application or app0 capacity exceeded')
    for name in names[1:]:
        if files[name]!=(RELEASE/name).read_bytes():
            raise RuntimeError('Unexpected partition layout or boot selection in build')
    return files

# A bounded identity probe covers LaunchLab's descriptor and identifying log string.
# This preserves the existing LaunchLab marker check; it is not authentication.
APP_PROBE_BYTES=65536

def flash_connected(api,esp,files):
    if esp.CHIP_NAME!='ESP32-S3':raise RuntimeError('Expected ESP32-S3; nothing written')
    api.attach_flash(esp)
    if api.detect_flash_size(esp)!='8MB':raise RuntimeError('Expected 8 MB flash; nothing written')
    for name,offset in [('LaunchLabMini.ino.partitions.bin',0x8000),('boot_app0.bin',0xe000)]:
        expected=files[name]
        actual=api.read_flash(esp,offset,len(expected),no_progress=True)
        if actual!=expected:raise RuntimeError('Incompatible partition layout or boot selection; nothing written')
    header=api.read_flash(esp,APP_OFFSET,APP_PROBE_BYTES,no_progress=True)
    if len(header)!=APP_PROBE_BYTES or header[0]!=0xe9 or header[32:36]!=b'\x32\x54\xcd\xab' or b'LAUNCH_RESULT' not in header:
        raise RuntimeError('Installed app0 is not recognized as LaunchLab; nothing written')
    app=files['LaunchLabMini.ino.bin']
    digest=hashlib.md5(app).hexdigest()
    if esp.flash_md5sum(APP_OFFSET,len(app)).lower()==digest:
        print('This exact application is already installed and verified; no write needed.')
    else:
        api.write_flash(esp,[(APP_OFFSET,app)],compress=True,erase_all=False)
        # Explicit full application verification in the same connection.
        api.verify_flash(esp,[(APP_OFFSET,app)])
        if esp.flash_md5sum(APP_OFFSET,len(app)).lower()!=digest:
            raise RuntimeError('Post-write checksum mismatch; installation not verified')
        print('Application written and verified. Bootloader, partition table, history and settings were not written.')
    esp.hard_reset()

def flash(port,execute=False):
    files=flash_files()
    if not execute:
        print('DRY RUN: check chip, flash size, layout, boot selection and LaunchLab identity; write ONLY app0 and verify. No backup or serial access.')
        return
    # Import lazily: preview/build/tests must not open a serial port.
    import esptool
    from esptool import cmds
    esp=esptool.detect_chip(port=port,baud=115200)
    try:
        if esp.CHIP_NAME!='ESP32-S3':raise RuntimeError('Expected ESP32-S3; nothing written')
        esp=cmds.run_stub(esp)
        esp.change_baud(460800)
        flash_connected(cmds,esp,files)
    finally:
        esp._port.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='command',required=True)
    sub.add_parser('build');sub.add_parser('plan');sub.add_parser('ports')
    b=sub.add_parser('backup');b.add_argument('--port',required=True)
    f=sub.add_parser('flash');f.add_argument('--port',required=True);f.add_argument('--execute',action='store_true')
    args=parser.parse_args()
    if args.command=='build':build()
    elif args.command=='plan':
        info=verified_build();print(json.dumps(info,indent=2));print('No device accessed. Next: ports, then flash --port COMx --execute. Optional separate backup: backup --port COMx.')
    elif args.command=='ports':
        from serial.tools import list_ports
        for port in list_ports.comports():print(port.device,port.description)
    elif args.command=='backup':backup(args.port)
    elif args.command=='flash':flash(args.port,args.execute)
if __name__=='__main__':
    try:main()
    except (RuntimeError,OSError,subprocess.CalledProcessError) as error:sys.exit(str(error))
