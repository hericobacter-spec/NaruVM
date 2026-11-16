#!/usr/bin/env python3
"""Host-only trace regression; uses installed NDK headers, never contacts a device."""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from gpu_trace import read_trace, HEADER, EVENT

ROOT = Path(__file__).resolve().parents[1]

def main():
    headers = ROOT/'.tools/android-sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/sysroot/usr/include'
    if not (headers/'vulkan/vulkan.h').is_file():
        raise SystemExit('Pinned NDK headers missing; run tools/bootstrap_android.py first.')
    compiler = shutil.which('clang')
    if not compiler:
        raise SystemExit('Host clang missing; Xcode command line tools are required.')
    with tempfile.TemporaryDirectory(prefix='henge-trace-test-') as temp:
        temp = Path(temp)
        for folder in ('vulkan','vk_video'):
            (temp/folder).symlink_to(headers/folder, target_is_directory=True)
        binary, trace = temp/'test', temp/'trace.bin'
        subprocess.run([compiler,'-O2','-Wall','-Wextra','-Werror','-I'+str(temp),
                        str(ROOT/'android/native/test_panvk_trace.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary),str(trace)],check=True)
        result = read_trace(trace)
        assert result['retained_events']==8192 and result['torn_slots']==0
        assert result['last_sequence']-result['first_sequence']==8191
        assert result['host_api_ms']['submit']['calls']==2
        assert len(result['errors'])==1 and result['errors'][0]['result']==-4
        data = bytearray(trace.read_bytes())
        slot = (result['last_sequence']-1)%8192
        struct.pack_into('<Q',data,HEADER.size+slot*EVENT.size+EVENT.size-8,0)
        trace.write_bytes(data)
        assert read_trace(trace)['torn_slots']==1
        trace.write_bytes(b'truncated')
        try:
            read_trace(trace)
        except ValueError:
            pass
        else:
            raise AssertionError('Truncated trace accepted')
    print('GPU_TRACE_HOST_RESULT failures=0 forwarding=pass concurrency=pass wrap=pass errors=pass torn=pass')

if __name__=='__main__':
    main()
