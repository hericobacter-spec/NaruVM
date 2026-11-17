#!/usr/bin/env python3
"""Build a local research APK using only the SDK, NDK, CMake and JDK (no Gradle downloads)."""
import os
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'android'
BUILD = APP / 'build'
SDK = Path(os.environ.get('ANDROID_SDK_ROOT', ROOT / '.tools/android-sdk'))
JDK = Path(os.environ.get('JAVA_HOME', ROOT / '.tools/jdk-21/Contents/Home'))
NDK = SDK / 'ndk/30.0.16248370'
BT = SDK / 'build-tools/36.0.0'
ENV = dict(os.environ, JAVA_HOME=str(JDK), PATH=str(JDK / 'bin') + os.pathsep + os.environ['PATH'])


def run(*args):
    print('+ ' + ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=ROOT, env=ENV, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gpu', action='store_true', help='Build and bundle the optional Metal-to-Vulkan diagnostic')
    parser.add_argument('--reims', action='store_true', help='Also bundle the experimental virtual-GPU C API probe; implies --gpu')
    parser.add_argument('--software', action='store_true', help='Also bundle the optional CPU Vulkan diagnostic; implies --gpu')
    parser.add_argument('--host-copy-probe', action='store_true', help='Bundle the isolated experimental copied-memory test; does not accelerate macOS')
    parser.add_argument('--panvk-probe', action='store_true', help='Bundle the pinned private PanVK authored-shader diagnostic; does not change the guest backend')
    parser.add_argument('--panvk-userptr-probe', action='store_true', help='Bundle the source-built PanVK host-pointer ICD and diagnostics; used by TB373FU Auto and Mali PanVK guest selections')
    parser.add_argument('--panvk-asan', action='store_true', help='Stage the separately built AddressSanitizer ICD; diagnostic only')
    parser.add_argument('--turnip-manifest', type=Path, help='Optional self-contained AArch64 Turnip driver with SHA256/source pin/license/build recipe; runtime support remains experimental')
    import release_signing
    release_signing.configure(parser)
    args = parser.parse_args()
    try:
        release_signing.validate(args)
    except ValueError as error:
        parser.error(str(error))
    artifacts = APP / 'artifacts'
    output = args.output or artifacts / ('NaruVM-arm64-' + args.version_name + '.apk' if args.release else 'NaruVM-arm64-debug.apk')
    if args.output and output.exists() or args.release and output.exists():
        parser.error('Output exists; choose a new filename')
    turnip = None
    if args.turnip_manifest:
        from turnip_bundle import validate
        turnip = validate(args.turnip_manifest)
    if args.panvk_asan: args.panvk_userptr_probe = True
    if args.reims or args.software or args.host_copy_probe or args.panvk_probe or args.panvk_userptr_probe: args.gpu = True
    for item in [NDK / 'build/cmake/android.toolchain.cmake', BT / 'aapt2', JDK / 'bin/javac']:
        if not item.exists():
            raise SystemExit('Missing ' + str(item) + '; run python3 tools/bootstrap_android.py first.')
    from installer_sources import verify
    verify(ROOT)
    BUILD.mkdir(exist_ok=True)
    native = BUILD / 'native'
    run(sys.executable, APP / 'build_network.py')
    network = json.loads((BUILD / 'network-libraries.json').read_text())
    if args.reims:
        from build_reims import SOURCE, verify_source
        verify_source()
    run('cmake', '-S', APP, '-B', native, '-DCMAKE_TOOLCHAIN_FILE=' + str(NDK / 'build/cmake/android.toolchain.cmake'),
        '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-28', '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
        '-DHENGE_ANDROID_REIMS=' + ('ON' if args.reims else 'OFF'),
        '-DHENGE_SLIRP_INCLUDE=' + network['include'],
        '-DHENGE_REIMS_INCLUDE=' + (str(SOURCE/'crates/reims-vgpu-c/include') if args.reims else ''))
    run('cmake', '--build', native, '-j', str(min(os.cpu_count() or 2, 8)))
    if args.gpu: run(sys.executable, APP / 'build_gpu.py')
    if args.host_copy_probe: run(sys.executable, APP / 'build_host_copy.py')
    if args.panvk_probe: run(sys.executable, APP / 'build_panvk_probe.py')
    if args.panvk_userptr_probe:
        run(sys.executable, APP / 'build_panvk_userptr_probe.py', *(['--asan'] if args.panvk_asan else []))
    if args.reims: run(sys.executable, APP / 'build_reims.py')
    software = None
    software_manifest = BUILD/'software-libraries.json'
    known_software = set(json.loads(software_manifest.read_text())['libraries']) if software_manifest.exists() else set()
    if args.software:
        run(sys.executable, APP / 'build_software.py')
        software = json.loads((BUILD/'software-libraries.json').read_text())
    classes, dex = BUILD / 'classes', BUILD / 'dex'
    for folder in [classes, dex]:
        if folder.exists(): shutil.rmtree(folder)
        folder.mkdir()
    android = SDK / 'platforms/android-36/android.jar'
    unsigned = BUILD / 'henge-unsigned.apk'
    resources = BUILD / 'resources.zip'
    generated = BUILD / 'generated-resources'
    if generated.exists(): shutil.rmtree(generated)
    generated.mkdir()
    run(BT / 'aapt2', 'compile', '--dir', APP / 'res', '-o', resources)
    manifest = APP / 'AndroidManifest.xml'
    if args.release:
        manifest = release_signing.manifest(manifest, BUILD / 'release/AndroidManifest.xml', args.version_name, args.version_code)
    run(BT / 'aapt2', 'link', '-I', android, '--manifest', manifest, '--java', generated, '-o', unsigned, resources)
    java = sorted((APP / 'src').rglob('*.java')) + sorted(generated.rglob('*.java'))
    run(JDK / 'bin/javac', '--release', '8', '-classpath', android, '-encoding', 'UTF-8', '-d', classes, *java)
    run(BT / 'd8', '--min-api', '28', '--lib', android, '--output', dex, *sorted(classes.rglob('*.class')))
    # Source archives may use an epoch timestamp. ZIP's minimum year is 1980;
    # clamp metadata rather than requiring a private checkout's file mtimes.
    with zipfile.ZipFile(unsigned, 'a', compression=zipfile.ZIP_DEFLATED, strict_timestamps=False) as archive:
        archive.write(dex / 'classes.dex', 'classes.dex')
        release_native = []
        for library in sorted((native / 'bin').glob('lib*.so')):
            if library.name == 'libhenge_turnip_driver.so': continue  # never reuse a stale optional driver
            if library.name == 'libhenge_host_copy_probe.so' and not args.host_copy_probe: continue
            if library.name == 'libhenge_hashmap_probe.so': continue
            if library.name.startswith('libhenge_panvk_userptr_'):
                if not args.panvk_userptr_probe: continue
            elif library.name.startswith('libhenge_panvk_') and not args.panvk_probe: continue
            if library.name.startswith('libhenge_net_') and library.name not in network['libraries']: continue
            if library.name == 'libhenge_gpu.so' and not args.gpu: continue
            if library.name in ('libreims_vgpu_c.so', 'libhenge_reims_probe.so') and not args.reims: continue
            # Network and software Vulkan share some support libraries. Disabling
            # software Vulkan must not remove the network's required dependencies.
            if (library.name.startswith('libhenge_lvp_') or library.name in known_software) and library.name not in network['libraries'] and (not software or library.name not in software['libraries']): continue
            payload = library
            if args.release:
                external = set(network['libraries']) | (set(software['libraries']) - {'libhenge_lvp_loader.so'} if software else set())
                payload, record = release_signing.native_payload(library, BUILD / 'release-native/bin',
                    NDK / 'toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip', library.name not in external)
                release_native.append(record)
            archive.write(payload, 'lib/arm64-v8a/' + library.name)
        if turnip:
            archive.write(turnip[0], 'lib/arm64-v8a/libhenge_turnip_driver.so')
            for name, content in turnip[1].items():
                archive.writestr('assets/licenses/turnip/' + name, content)
        missing_network = set(network['libraries']) - {
            Path(name).name for name in archive.namelist() if name.startswith('lib/arm64-v8a/')
        }
        if missing_network:
            raise SystemExit('APK missing required network libraries: ' + ', '.join(sorted(missing_network)))
        archive.write(ROOT / 'third_party/lzfse/LICENSE', 'assets/licenses/lzfse-LICENSE.txt')
        archive.write(APP / 'installer/dependencies.json', 'assets/licenses/lzfse-source.json')
        archive.write(ROOT / 'LICENSE', 'assets/Henge-LICENSE.txt')
        for notice in sorted((ROOT / 'LICENSES').rglob('*')):
            if notice.is_file():
                if args.release and notice.relative_to(ROOT / 'LICENSES').as_posix() == 'source-info.txt':
                    archive.writestr('assets/licenses/project/source-info.txt', release_signing.source_information(args.version_name))
                    continue
                archive.write(notice, 'assets/licenses/project/' + notice.relative_to(ROOT / 'LICENSES').as_posix())
        if (ROOT / 'NOTICE').is_file(): archive.write(ROOT / 'NOTICE', 'assets/licenses/project/NOTICE.txt')
        archive.write(APP / 'publication/license-selection.json', 'assets/licenses/project/license-selection.json')
        archive.write(ROOT / 'third_party/mbedtls/LICENSE', 'assets/MbedTLS-LICENSE.txt')
        archive.write(APP / 'network/dependencies.json', 'assets/licenses/network/dependencies.json')
        if args.panvk_probe:
            archive.write(APP / 'panvk/dependencies.json', 'assets/licenses/panvk/dependencies.json')
            archive.write(BUILD / 'panvk-release-v1.6.0/NOTICE.txt', 'assets/licenses/panvk/NOTICE.txt')
        if args.panvk_userptr_probe:
            # Keep local build-machine paths out of a potentially shared APK.
            build_record = json.loads((BUILD / 'panvk-userptr-libraries.json').read_text())
            public_record = {key: build_record[key] for key in ['source_commit', 'output_sha256', 'patch_sha256',
                'address_sanitizer', 'libraries', 'published_binary', 'staged_driver_sha256', 'stripped_debug'] if key in build_record}
            archive.writestr('assets/licenses/panvk-userptr/build.json', json.dumps(public_record, indent=2)+'\n')
            archive.write(APP / 'panvk/userptr-driver.patch', 'assets/licenses/panvk-userptr/userptr-driver.patch')
            panvk_source = ROOT / '.tools/mali-panvk-android'
            for relative in ['LICENSE', 'docs/license.rst', 'subprojects/libdrm-2.4.133/xf86drm.c',
                             'subprojects/libdrm-2.4.133/xf86drmHash.c',
                             'subprojects/libdrm-2.4.133/xf86drmRandom.c',
                             'subprojects/libdrm-2.4.133/xf86drmSL.c',
                             'subprojects/zlib-1.3.1/README', 'subprojects/expat-2.5.0/COPYING']:
                license_file = panvk_source / relative
                if license_file.is_file():
                    archive.write(license_file, 'assets/licenses/panvk-userptr/' + relative)
        network_notices = Path(network['attributions'])
        for notice in sorted(network_notices.rglob('*')):
            if notice.is_file() and '/share/doc/' in str(notice):
                archive.writestr('assets/licenses/network/' + str(notice.relative_to(network_notices)), notice.read_bytes())
        for name in ['libplist', 'libimobiledevice-glue', 'libusbmuxd', 'libimobiledevice', 'libtatsu',
                     'libirecovery', 'usbmuxd', 'idevicerestore', 'libzip']:
            folder = ROOT / 'third_party' / name
            for license in sorted(set(folder.glob('COPYING*')) | set(folder.glob('LICENSE*'))):
                if license.is_file(): archive.write(license, 'assets/licenses/' + name + '-' + license.name + '.txt')
        archive.write(ROOT / '.tools/curl-8.23.0/COPYING', 'assets/licenses/curl-COPYING.txt')
        if args.gpu:
            archive.write(APP/'gpu/attachment-rog.patch','assets/licenses/metal2vulkan-android/attachment-rog.patch')
            archive.write(APP/'gpu/dependencies.json','assets/licenses/metal2vulkan-android/source.json')
            from gpu_licenses import bundle
            bundle(archive, ROOT, APP/'gpu/Cargo.toml', 'metal-vulkan')
            if args.reims:
                from build_reims import SOURCE, cargo_patch_args
                bundle(archive, ROOT, SOURCE/'crates/reims-vgpu-c/Cargo.toml', 'reims-vgpu', ['vulkan'], cargo_patch_args())
        if software:
            archive.write(APP/'software/dependencies.json','assets/licenses/lavapipe/dependencies.json')
            attributions = Path(software['attributions'])
            for notice in sorted(attributions.rglob('*')):
                if notice.is_file() and '/share/doc/' in str(notice):
                    archive.writestr('assets/licenses/lavapipe/'+str(notice.relative_to(attributions)),notice.read_bytes())
    if args.release:
        (BUILD / 'release-native-map.json').write_text(json.dumps(release_native, indent=2) + '\n')
    aligned = BUILD / 'henge-aligned.apk'
    run(BT / 'zipalign', '-f', '-P', '16', '4', unsigned, aligned)
    keystore = args.keystore if args.release else ROOT / '.tools/research-debug.keystore'
    if not args.release and not keystore.exists():
        run(JDK / 'bin/keytool', '-genkeypair', '-keystore', keystore, '-storepass', 'android', '-keypass', 'android',
            '-alias', 'androiddebugkey', '-dname', 'CN=Henge Research Debug', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000')
    output.parent.mkdir(parents=True, exist_ok=True)
    if args.release:
        run(BT / 'apksigner', 'sign', *release_signing.signing_arguments(args), '--out', output, aligned)
    else:
        run(BT / 'apksigner', 'sign', '--ks', keystore, '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', output, aligned)
    run(BT / 'apksigner', 'verify', '--verbose', output)
    run(BT / 'zipalign', '-c', '-P', '16', '4', output)
    print('APK: ' + str(output))


if __name__ == '__main__':
    main()
