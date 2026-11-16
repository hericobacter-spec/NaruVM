"""Bundle dependency license/notice files and SPDX provenance from Cargo's locked graph."""
import json
import os
from pathlib import Path
import subprocess

def bundle(archive, root, manifest, prefix, features=(), config=()):
    env = dict(os.environ, RUSTUP_HOME=str(root/'.tools/rustup'), CARGO_HOME=str(root/'.tools/cargo'),
               RUSTUP_TOOLCHAIN='1.98.1', RUSTUP_AUTO_INSTALL='0')
    env['PATH'] = str(root/'.tools/cargo/bin')+os.pathsep+env['PATH']
    command = [str(root/'.tools/cargo/bin/cargo'),'metadata',
        '--locked','--format-version','1','--filter-platform','aarch64-linux-android',
        '--manifest-path',str(manifest)]
    if features: command += ['--no-default-features','--features',','.join(features)]
    command += list(config)
    data = json.loads(subprocess.check_output(command,env=env))
    nodes = {node['id']:node for node in data['resolve']['nodes']}
    pending, reachable = [data['resolve']['root']], set()
    while pending:
        package_id = pending.pop()
        if package_id in reachable: continue
        reachable.add(package_id)
        pending.extend(d['pkg'] for d in nodes[package_id]['deps'])
    notices = []
    for package in data['packages']:
        if package['id'] not in reachable: continue
        folder = Path(package['manifest_path']).parent
        files = set()
        if package.get('license_file'): files.add(folder/package['license_file'])
        for base in [folder, *list(folder.parents)[:2]]:
            candidates = {p for pattern in ['LICENSE*','LICENCE*','COPYING*','NOTICE*'] for p in base.glob(pattern) if p.is_file()}
            if candidates:
                files |= candidates
                break
        key = package['name']+'-'+package['version']
        entries = []
        for index, path in enumerate(sorted(p for p in files if p.is_file())):
            entry = 'assets/licenses/'+prefix+'/'+key+'/'+str(index)+'-'+path.name
            # Cargo registry archives can carry Unix epoch timestamps, before ZIP's 1980 floor.
            archive.writestr(entry,path.read_bytes()); entries.append(entry)
        notices.append({'name':package['name'],'version':package['version'],'license':package['license'],
            'source':package['source'] or 'local-pinned-source','files':entries})
    archive.writestr('assets/licenses/'+prefix+'/dependencies.json',json.dumps(notices,indent=2)+'\n')
