#!/usr/bin/env python3
"""Exercise actual touch -> Java writer -> authored native input sink, without a Mac disk."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'dev.henge.research'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--output', type=Path, required=True)
    a = parser.parse_args()
    if a.output.exists(): raise SystemExit('Output directory must be new: '+str(a.output))
    base = [str(ROOT/'.tools/android-sdk/platform-tools/adb'), '-s', a.serial]
    def adb(*args):
        r = subprocess.run(base+list(args), capture_output=True)
        if r.returncode:
            raise RuntimeError('ADB failed: '+r.stderr.decode(errors='replace'))
        return r.stdout
    def report(name='live-report.txt'):
        r = subprocess.run(base+['exec-out', 'run-as', PACKAGE, 'cat', 'files/'+name], capture_output=True)
        if r.returncode and b'No such file' not in r.stderr:
            raise RuntimeError('ADB report read failed: '+r.stderr.decode(errors='replace'))
        return r.stdout.decode(errors='replace') if not r.returncode else ''
    def wait_for(check, seconds=10):
        end = time.monotonic()+seconds
        while time.monotonic() < end:
            value = check()
            if value: return value
            time.sleep(.2)
        raise RuntimeError('Expected current authored fixture state missing')
    listing = adb('shell', 'ps', '-A', '-o', 'PID,ARGS').decode()
    if re.search(r'libhenge_\w+\.so', listing):
        raise SystemExit('Native work active; no input or stop issued')
    if 'isForeground=true' in adb('shell', 'dumpsys', 'activity', 'services', PACKAGE).decode():
        raise SystemExit('Service active; no input or stop issued')
    previous = report()
    previous_signature = next((x for x in previous.splitlines() if x.startswith('=== authored_service_fixture')), '')
    a.output.mkdir(parents=True, exist_ok=False)
    # No -S is used until both active-work guards pass.
    adb('shell', 'am', 'start', '-S', '-W', '-n', PACKAGE+'/.MainActivity', '--ez', 'run_fullscreen_fixture', 'true')
    def fresh_fixture():
        r=report()
        signature=next((x for x in r.splitlines() if x.startswith('=== authored_service_fixture')), '')
        return r if signature and signature != previous_signature and 'VM_SERVICE_RUNNING' in r else ''
    live = wait_for(fresh_fixture)
    signature = next(x for x in live.splitlines() if x.startswith('=== authored_service_fixture'))
    def guarded_report():
        r=report()
        if signature not in r or 'boot-reims' in r:
            raise RuntimeError('Fixture identity changed; no further input allowed')
        return r
    wait_for(lambda: 'MAC_VIEWPORT width=2944 height=1840' in guarded_report())
    def click_count():
        return len(re.findall(r'FRAME_INPUT_BUTTON down=0 matched=1 held_ms=(\d+)', guarded_report()))
    def target_count():
        return len(re.findall(r"FRAME_INPUT_TAP_AT x=(\d+) y=(\d+)", guarded_report()))
    def touch_and_wait(command, expected):
        guarded_report()
        adb('shell', 'input', *command)
        wait_for(lambda: target_count() == expected)
    # The long stationary press previously failed the Activity's <300 ms rule.
    touch_and_wait(['touchscreen', 'swipe', '1200', '800', '1200', '800', '650'], 1)
    touch_and_wait(['tap', '1200', '800'], 2)
    def button(text):
        guarded_report()
        adb('shell', 'uiautomator', 'dump', '/data/local/tmp/henge-input-fixture.xml')
        tree=ET.fromstring(adb('exec-out', 'cat', '/data/local/tmp/henge-input-fixture.xml'))
        for n in tree.iter('node'):
            if n.get('text') == text:
                x1,y1,x2,y2=map(int,re.findall(r'\d+', n.get('bounds')))
                adb('shell','input','tap',str((x1+x2)//2),str((y1+y2)//2))
                return
        raise RuntimeError('Visible fixture button missing: '+text)
    button('도구')
    time.sleep(.3)
    button('입력: 직접 터치')
    touch_report=guarded_report()
    adb('shell','input','tap','1200','800')
    wait_for(lambda: click_count() == 1)
    button('클릭')
    wait_for(lambda: click_count() == 2)
    guarded_report()
    (a.output/'authored-tools.png').write_bytes(adb('exec-out','screencap','-p'))
    current=guarded_report()
    intervals=list(map(int,re.findall(r'FRAME_INPUT_BUTTON down=0 matched=1 held_ms=(\d+)',current)))
    if len(intervals)!=2 or any(ms<80 or ms>2000 for ms in intervals):
        raise RuntimeError('Click edges not separated at authored sink: '+repr(intervals))
    button('Mac 종료')
    final=wait_for(lambda: (r if signature in (r := report('last-report.txt')) and 'VM_SERVICE_CLEANUP foreground=0 wake_lock=0' in r and 'EXIT_CODE=0' in r else ''))
    (a.output/'report.txt').write_text(final)
    listing=adb('shell','ps','-A','-o','PID,ARGS').decode()
    if 'libhenge_frames_test.so' in listing: raise RuntimeError('Fixture worker still present')
    result={'label':'observed','date':time.strftime('%Y-%m-%dT%H:%M:%S%z'),'n':1,'serial':a.serial,
            'checks':['650ms_direct_touch','short_direct_tap','trackpad_fallback','toolbar_click','normal_stop'],
            'direct_targets':re.findall(r'FRAME_INPUT_TAP_AT x=(\d+) y=(\d+)',current),
            'held_ms_at_authored_native_sink':intervals,'failures':0,
            'actual_macos_click_verified':False,'macos_disk_opened':False}
    (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))

if __name__=='__main__': main()
