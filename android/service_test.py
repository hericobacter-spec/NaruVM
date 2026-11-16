#!/usr/bin/env python3
"""Disk-free foreground-session recreation, scheduling, cancellation and cleanup fixture."""
import argparse
from pathlib import Path
import re
import subprocess
import time
import json

ROOT=Path(__file__).resolve().parents[1]
PACKAGE='dev.henge.research'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--install', action='store_true')
    a=p.parse_args()
    base=[str(ROOT/'.tools/android-sdk/platform-tools/adb'),'-s',a.serial]
    def adb(*args, check=True):
        return subprocess.run(base+list(args),check=check,capture_output=True).stdout.decode(errors='replace')
    def save(name, text): (a.output/name).write_text(text)
    def report(name): return adb('exec-out','run-as',PACKAGE,'cat','files/'+name,check=False)
    def wait_for(predicate, seconds=8):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            value=predicate()
            if value:return value
            time.sleep(.2)
        raise SystemExit('Expected session state absent; not a pass')
    listing=adb('shell','ps','-A','-o','NAME')
    if re.search(r'libhenge_(lab|gpu|reims_probe|clone|frames_test)\.so',listing):
        raise SystemExit('Native worker active; wait for normal completion')
    if 'isForeground=true' in adb('shell','dumpsys','activity','services',PACKAGE):
        raise SystemExit('Foreground session active; do not interrupt it')
    a.output.mkdir(parents=True,exist_ok=True)
    if a.install: adb('install','--no-incremental','-r',str(ROOT/'android/artifacts/Henge-Lab-arm64-debug.apk'))
    adb('shell','am','force-stop',PACKAGE)
    adb('shell','run-as',PACKAGE,'rm','-f','files/last-report.txt')
    adb('shell','am','start','-W','-n',PACKAGE+'/.MainActivity','--ez','run_service_test','true')
    wait_for(lambda:'VM_SERVICE_RUNNING' in report('live-report.txt'))
    first=adb('shell','pidof','libhenge_frames_test.so').strip()
    if not re.fullmatch(r'\d+',first):raise SystemExit('One authored fixture process required')
    cpuset=adb('shell','run-as',PACKAGE,'cat','/proc/'+first+'/cpuset').strip()
    status=adb('shell','run-as',PACKAGE,'cat','/proc/'+first+'/status')
    service=adb('shell','dumpsys','activity','services',PACKAGE)
    power=adb('shell','dumpsys','power')
    save('active-services.txt',service);save('active-worker.txt',status)
    save('active-wake.txt','\n'.join(x for x in power.splitlines() if 'HengeLab:LocalVm' in x)+'\n')
    if 'isForeground=true' not in service or not re.search(r'PARTIAL_WAKE_LOCK.*HengeLab:LocalVm',power):
        raise SystemExit('Foreground/wake state absent; not a pass')
    adb('shell','am','start','-W','-n',PACKAGE+'/.MainActivity','--ez','recreate_active','true')
    wait_for(lambda: report('live-report.txt').count('VM_SCREEN_ATTACHED')>=2)
    second=adb('shell','pidof','libhenge_frames_test.so').strip()
    if first != second:raise SystemExit('Child PID changed during Activity recreation')
    adb('shell','am','start','-W','-n',PACKAGE+'/.MainActivity','--ez','stop_fixture','true')
    final=wait_for(lambda: (r if 'EXIT_CODE=' in (r:=report('last-report.txt')) else ''))
    save('report.txt',final)
    required=['FRAME_SERVICE_STOP accepted=1','VM_SERVICE_CLEANUP foreground=0 wake_lock=0','EXIT_CODE=0']
    if not all(x in final for x in required):raise SystemExit('Normal stop or cleanup marker missing')
    boot=re.search(r'HOST_BOOT boot_id=([0-9a-fA-F-]{36}|unavailable) elapsed_realtime_ms=(\d+)', final)
    if not boot or int(boot[2]) <= 0:raise SystemExit('Current-boot evidence missing')
    shell_boot=adb('shell','cat','/proc/sys/kernel/random/boot_id').strip()
    if boot[1] != 'unavailable' and boot[1] != shell_boot:raise SystemExit('App/shell boot identity mismatch')
    wait_for(lambda:'isForeground=true' not in adb('shell','dumpsys','activity','services',PACKAGE))
    power=adb('shell','dumpsys','power')
    if re.search(r'PARTIAL_WAKE_LOCK.*HengeLab:LocalVm',power):raise SystemExit('Wake lock still active')
    if adb('shell','pidof','libhenge_frames_test.so',check=False).strip():raise SystemExit('Fixture still active')
    result={'failures':0,'same_worker_pid':first,'activity_attachments':final.count('VM_SCREEN_ATTACHED'),
            'active_cpuset':cpuset,'normal_stop':True,'foreground_released':True,'wake_released':True,
            'boot_id':boot[1], 'elapsed_realtime_ms':int(boot[2])}
    save('result.json',json.dumps(result,indent=2)+'\n');print(json.dumps(result))

if __name__=='__main__':main()
