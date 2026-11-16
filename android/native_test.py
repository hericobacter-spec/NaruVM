#!/usr/bin/env python3
"""Run bounded native research commands on one explicit, authorized tablet."""
import argparse
import hashlib
from pathlib import Path
import re
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
ADB = ROOT / '.tools/android-sdk/platform-tools/adb'
PACKAGE = 'dev.henge.research'
ROM_HASH = 'fd4973e42e609751830df6f3dd2d2b2ad2309fe38f0ca0f5aba02dcbefe45f91'
IPSW_HASH = '3d87686b691ac765eb6a6b3082b2334e2af9710096a00432dd519af89ff2ea78'
IPSW_NAME = 'UniversalMac_15.6.1_24G90_Restore.ipsw'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', required=True)
    p.add_argument('--command', choices=['dfu','disk-test','disk-init','restore','net-probe','boot-disk','gpu-test','gpu-software-test','reims-test','reims-frame-test','reims-software-test','reims-software-frame-test','clone-test','clone-disk','boot-reims','reims-software-surface-test'], required=True)
    p.add_argument('--install', action='store_true')
    p.add_argument('--debug', action='store_true', help='Verbose virtual USB/restore diagnostics')
    p.add_argument('--memory-only', action='store_true', help='Run gpu-test shared-memory probes without shader fixtures')
    p.add_argument('--allow-user-stop', action='store_true', help='Accept an explicitly requested, clean boot-reims stop')
    p.add_argument('--no-frame-export', action='store_true', help='Leave guest frames on the tablet during user-owned setup')
    p.add_argument('--via-app', action='store_true', help='Run through Activity/ProcessBuilder rather than run-as')
    p.add_argument('--rom', type=Path)
    p.add_argument('--stage-ipsw', type=Path)
    p.add_argument('--ram', type=int, default=2048)
    p.add_argument('--cpus', type=int, choices=[1, 2], default=1, help='Experimental boot-reims guest CPU count')
    p.add_argument('--seconds', type=int, default=180)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if not 10 <= a.seconds <= 7200 or not 1024 <= a.ram <= 4096: p.error('Invalid duration/RAM')
    if a.via_app and a.command == 'disk-init': p.error('This command is CLI-only')
    if a.command != 'boot-reims' and a.cpus != 1: p.error('--cpus is only for boot-reims')
    if a.memory_only and a.command != 'gpu-test': p.error('--memory-only requires gpu-test')
    if a.via_app and a.command in ('restore','boot-disk','boot-reims') and a.ram != 2048:
        p.error('The Activity restore/boot experiment uses 2048MB')
    for asset, wanted in [(a.rom, ROM_HASH), (a.stage_ipsw, IPSW_HASH)]:
        if asset:
            with asset.open('rb') as f: digest = hashlib.file_digest(f, 'sha256').hexdigest()
            if digest != wanted: p.error(f'Unsupported SHA-256 for {asset}; no device changes made')
    base = [str(ADB), '-s', a.serial]
    def adb(*args, **kw): return subprocess.run(base + list(args), check=kw.pop('check', True), **kw)
    def capture(*args): return adb(*args, stdout=subprocess.PIPE).stdout.decode().strip()
    def app(command, **kw): return adb('shell', '-T', 'run-as ' + PACKAGE + ' sh -c ' + shlex.quote(command), **kw)
    if capture('get-state') != 'device': p.error('Device is not ready')
    if a.install: adb('install', '--no-incremental', '-r', str(ROOT/'android/artifacts/Henge-Lab-arm64-debug.apk'))
    if a.rom:
        with a.rom.open('rb') as f: app('cat > files/AVPBooter.vmapple2.bin', stdin=f)
    if a.stage_ipsw:
        # A known final image is reused only after hashing it on the device.
        final = 'files/' + IPSW_NAME
        existing = subprocess.run(base+['shell','run-as',PACKAGE,'sha256sum',final], capture_output=True, text=True)
        if existing.returncode or not existing.stdout.startswith(IPSW_HASH):
            print('Staging official IPSW into app-private files...', flush=True)
            # Report bytes accepted by ADB; final device hashing verifies delivery.
            transfer = subprocess.Popen(base+['shell','-T','run-as '+PACKAGE+' sh -c '+
                                        shlex.quote('cat > '+shlex.quote(final+'.part'))], stdin=subprocess.PIPE)
            sent, last = 0, time.monotonic()
            try:
                with a.stage_ipsw.open('rb') as f:
                    while block := f.read(1024 * 1024):
                        transfer.stdin.write(block)
                        sent += len(block)
                        if time.monotonic() - last >= 10:
                            print(f'ADB input: {sent / (1<<30):.2f} GiB / {a.stage_ipsw.stat().st_size / (1<<30):.2f} GiB',flush=True)
                            last = time.monotonic()
                transfer.stdin.close()
                if transfer.wait() != 0: raise SystemExit('IPSW transfer failed; partial file retained')
            finally:
                if transfer.poll() is None: transfer.terminate(); transfer.wait()
            digest = capture('shell','run-as',PACKAGE,'sha256sum',final+'.part').split()[0]
            if digest != IPSW_HASH: raise SystemExit('Device IPSW checksum mismatch; not promoted')
            app('mv '+shlex.quote(final+'.part')+' '+shlex.quote(final))
        print('Device IPSW SHA-256 matches', flush=True)
    package = capture('shell','dumpsys','package',PACKAGE)
    match = re.search(r'legacyNativeLibraryDir=(\S+)', package)
    if not match: raise SystemExit('Native library directory unavailable')
    worker = {'reims-software-surface-test':'henge_reims_probe','clone-test':'henge_clone','clone-disk':'henge_clone','gpu-test':'henge_gpu', 'gpu-software-test':'henge_gpu', 'reims-test':'henge_reims_probe', 'reims-frame-test':'henge_reims_probe', 'reims-software-test':'henge_reims_probe', 'reims-software-frame-test':'henge_reims_probe'}.get(a.command, 'henge_lab')
    binary = match[1] + '/arm64/lib' + worker + '.so'
    files = '/data/user/0/' + PACKAGE + '/files'
    args = {'reims-software-surface-test':[match[1]+'/arm64/libreims_vgpu_c.so','--surface','--loader='+match[1]+'/arm64/libhenge_lvp_loader.so'], 'boot-reims':['boot-reims',files+'/AVPBooter.vmapple2.bin',files+'/Sequoia-24G90-reims',str(a.ram),str(a.seconds),match[1]+'/arm64/libreims_vgpu_c.so',match[1]+'/arm64/libhenge_lvp_loader.so'], 'clone-test':['--selftest',files], 'clone-disk':[files+'/Sequoia-24G90',files+'/Sequoia-24G90-reims'], 'reims-software-test':[match[1]+'/arm64/libreims_vgpu_c.so','--loader='+match[1]+'/arm64/libhenge_lvp_loader.so'], 'reims-software-frame-test':[match[1]+'/arm64/libreims_vgpu_c.so','--efi','--loader='+match[1]+'/arm64/libhenge_lvp_loader.so'], 'gpu-software-test':[files,match[1]+'/arm64/libhenge_lvp_loader.so'],'reims-frame-test':[match[1]+'/arm64/libreims_vgpu_c.so','--efi'], 'reims-test':[match[1]+'/arm64/libreims_vgpu_c.so'], 'gpu-test':[files], 'net-probe':['net-probe'], 'dfu':['dfu', files+'/AVPBooter.vmapple2.bin','1024','25'],
            'disk-test':['disk-test',files],
            'disk-init':['disk-init',files+'/Sequoia-24G90'],
            'restore':['restore', files+'/AVPBooter.vmapple2.bin',files+'/'+IPSW_NAME,
                       files+'/Sequoia-24G90',str(a.ram),str(a.seconds)],
            'boot-disk':['boot-disk',files+'/AVPBooter.vmapple2.bin',files+'/Sequoia-24G90',
                         str(a.ram),str(a.seconds)]}[a.command]
    a.output.mkdir(parents=True, exist_ok=True)
    if a.memory_only: args.append('--memory-only')
    if a.command == 'boot-reims': args.append(str(a.cpus))
    output = a.output / (a.command+'.txt')
    command = shlex.join([binary]+args)
    if a.command in ('gpu-software-test','reims-software-test','reims-software-frame-test','reims-software-surface-test','boot-reims'):
        command = 'LD_LIBRARY_PATH=' + shlex.quote(match[1]+'/arm64') + ' ' + command
    if a.debug: command = 'HV_INSTALL_LOG=1 HV_LIBUSB_LOG=1 ' + command
    full_text = ''
    if a.via_app:
        extra = {'reims-software-surface-test':'run_reims_test','boot-reims':'run_boot_reims','dfu':'run_dfu','disk-test':'run_disk_test','net-probe':'run_net_probe',
                 'restore':'run_restore','boot-disk':'run_boot_disk','gpu-test':'run_gpu_test','gpu-software-test':'run_gpu_test','reims-test':'run_reims_test','reims-frame-test':'run_reims_test','reims-software-test':'run_reims_test','reims-software-frame-test':'run_reims_test','clone-test':'run_clone_test','clone-disk':'run_clone_disk'}[a.command]
        adb('shell','am','force-stop',PACKAGE)
        adb('shell','run-as',PACKAGE,'rm','-f','files/last-report.txt')
        launch = ['shell','am','start','-W','-n',PACKAGE+'/.MainActivity','--ez',extra,'true']
        if a.command in ('reims-frame-test','reims-software-frame-test'): launch += ['--ez','reims_efi','true']
        if a.command in ('reims-software-test','reims-software-frame-test','reims-software-surface-test'): launch += ['--ez','reims_software','true']
        if a.command == 'gpu-software-test': launch += ['--ez','gpu_software','true']
        if a.memory_only: launch += ['--ez','gpu_memory_only','true']
        if a.command == 'reims-software-surface-test': launch += ['--ez','reims_surface','true']
        if a.command == 'restore': launch += ['--ei','restore_seconds',str(a.seconds)]
        if a.command in ('boot-disk','boot-reims'): launch += ['--ei','boot_seconds',str(a.seconds)]
        if a.command == 'boot-reims': launch += ['--ei','boot_cpus',str(a.cpus)]
        if a.debug: launch += ['--ez','restore_debug','true']
        adb(*launch)
        until = time.monotonic() + a.seconds + 90
        report = None
        while time.monotonic() < until:
            found = adb('exec-out','run-as',PACKAGE,'cat','files/last-report.txt',
                        stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False)
            if found.returncode == 0 and b'EXIT_CODE=' in found.stdout:
                report = found.stdout; break
            time.sleep(1)
        if report is None:
            partial = adb('exec-out','run-as',PACKAGE,'cat','files/live-report.txt',
                          stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False)
            if partial.returncode == 0: output.with_suffix('.partial.txt').write_bytes(partial.stdout)
            adb('shell','am','force-stop',PACKAGE)
            raise SystemExit('No completed Activity report before deadline; own app stopped, not a pass')
        output.write_bytes(report)
        live = adb('exec-out','run-as',PACKAGE,'cat','files/live-report.txt',stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=False)
        if live.returncode == 0:
            output.with_suffix('.live.txt').write_bytes(live.stdout)
            full_text = live.stdout.decode(errors='replace')
        codes = re.findall(rb'EXIT_CODE=(-?\d+)', report)
        result = subprocess.CompletedProcess(command, int(codes[-1]))
    else:
        with output.open('wb') as log:
            result = app(command, stdout=log, stderr=subprocess.STDOUT, timeout=a.seconds+90, check=False)
    text = output.read_text(errors='replace')
    expected = {'reims-software-surface-test':'REIMS_SURFACE_RESULT failures=0','boot-reims':'BOOT_DISK_RESULT timeout=1','clone-test':'CLONE_TEST failures=0','clone-disk':'CLONE_RESULT failures=0','dfu':'DFU_RESULT failures=0','disk-test':'DISK_TEST failures=0',
                'disk-init':'DISK_READY','restore':'RESTORE_RESULT result=0 timeout=0',
                'net-probe':'NETWORK_RESULT failures=0','boot-disk':'BOOT_DISK_RESULT timeout=1',
                'gpu-test':'GPU_RESULT failures=0','gpu-software-test':'GPU_RESULT failures=0','reims-test':'REIMS_RESULT failures=0',
                'reims-frame-test':'REIMS_FRAME_RESULT failures=0', 'reims-software-test':'REIMS_RESULT failures=0', 'reims-software-frame-test':'REIMS_FRAME_RESULT failures=0'}[a.command]
    if a.memory_only: expected = 'ANDROID_MEMORY_RESULT failures=0'
    if not a.no_frame_export and a.command in ('boot-disk','boot-reims') and 'FRAMEBUFFER_CAPTURE ' in text:
        frame = adb('exec-out','run-as',PACKAGE,'cat','files/'+('Sequoia-24G90-reims' if a.command == 'boot-reims' else 'Sequoia-24G90')+'/boot-frame.bmp',
                    stdout=subprocess.PIPE).stdout
        (a.output/'boot-frame.bmp').write_bytes(frame)
    if not a.no_frame_export and a.command in ('gpu-test','gpu-software-test') and 'VULKAN_TEST_FRAME ' in text and result.returncode == 0:
        frame = adb('exec-out','run-as',PACKAGE,'cat','files/vulkan-test-frame.bmp',stdout=subprocess.PIPE).stdout
        (a.output/'vulkan-test-frame.bmp').write_bytes(frame)
    if not a.no_frame_export and a.command == 'boot-reims' and 'REIMS_GUEST_FRAME ' in (full_text or text):
        frame = adb('exec-out','run-as',PACKAGE,'cat','files/Sequoia-24G90-reims/reims-frame.bmp',stdout=subprocess.PIPE).stdout
        (a.output/'reims-frame.bmp').write_bytes(frame)
    print('Native exit:',result.returncode,'Log:',output.resolve(),flush=True)
    stopped_cleanly = (a.allow_user_stop and a.command == 'boot-reims' and
        re.search(r'VM_CONTROL_RESULT .*requested_stop=1\b', text) is not None and
        re.search(r'BOOT_DISK_RESULT timeout=0 .*panic_events=0 flush_failed=0\b', text) is not None)
    if result.returncode or (expected not in text and not stopped_cleanly):
        print('\n'.join(text.splitlines()[-35:]))
        raise SystemExit('Native command failed or completion marker absent; this is not a pass')

if __name__ == '__main__': main()
