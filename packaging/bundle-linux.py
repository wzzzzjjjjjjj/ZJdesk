"""Collect the Qt/GStreamer runtime and its ELF dependency closure for an AppDir."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

root, appdir = map(Path, sys.argv[1:])
appdir.chmod(0o755)
qt, gst = Path(os.environ['QT_ROOT']), Path(os.environ['GST_ROOT'])
lib = appdir / 'usr/lib'
lib.mkdir(parents=True)
(appdir / 'usr/bin').mkdir()
for name in ('zjdesk', 'zjdesk-server'):
    shutil.copy2(root / 'build/linux-release/bin' / name, appdir / 'usr/bin' / name)
shutil.copy2(gst / 'bin/gst-inspect-1.0', appdir / 'usr/bin')
for sdk in (qt, gst):
    for path in (sdk / 'lib').glob('*.so*'):
        if path.is_file():
            shutil.copy2(path, lib / path.name)
# Ubuntu's libsrtp uses NSS, which loads these crypto modules with dlopen.
# They do not appear in ldd's dependency closure, but SRTP initialization needs them.
dynamic_libraries = []
loader_cache = subprocess.check_output(['/sbin/ldconfig', '-p'], text=True)
nss = re.search(r'^\s*libnss3\.so \([^\n]*x86-64[^\n]*\) => (.+)$', loader_cache, re.M)
if not nss:
    raise RuntimeError('Missing x86_64 NSS runtime')
nss_base = Path(nss.group(1)).parent
for name in ('libsoftokn3.so', 'libfreeblpriv3.so', 'libfreebl3.so', 'libnssckbi.so'):
    source = nss_base / 'nss' / name
    if not source.exists():
        source = nss_base / name
    if not source.exists():
        raise RuntimeError(f'Missing NSS runtime module: {name}')
    shutil.copy2(source, lib / name)
    (lib / 'nss').mkdir(exist_ok=True)
    shutil.copy2(source, lib / 'nss' / name)
    check = source.with_suffix('.chk')
    if check.exists():
        shutil.copy2(check, lib / check.name)
        shutil.copy2(check, lib / 'nss' / check.name)
    dynamic_libraries.append(f'{name}\t{source}')
for folder in ('platforms', 'xcbglintegrations', 'imageformats', 'platforminputcontexts',
               'networkinformation', 'tls'):
    source = qt / 'plugins' / folder
    if source.exists():
        shutil.copytree(source, appdir / 'usr/plugins' / folder)
shutil.copytree(gst / 'lib/gstreamer-1.0', lib / 'gstreamer-1.0')
scanner = appdir / 'usr/libexec/gstreamer-1.0'
scanner.mkdir(parents=True)
shutil.copy2(gst / 'libexec/gstreamer-1.0/gst-plugin-scanner', scanner)
(appdir / 'usr/bin/qt.conf').write_text('[Paths]\nPrefix=..\nPlugins=plugins\n')

# Keep the host glibc and graphics driver stack; bundle the remaining shared libraries.
host = re.compile(r'^(ld-linux|lib(c|m|pthread|rt|dl|resolv|util|nss_.*|anl)\.so|'
                  r'lib(GL|EGL|OpenGL|GLES|GLX|GLdispatch|drm|gbm|vulkan|cuda|nvidia))')
environment = dict(os.environ, LD_LIBRARY_PATH=str(lib))
queue = [p for p in (appdir / 'usr').rglob('*') if p.is_file()]
seen = set()
manifest = dynamic_libraries
while queue:
    path = queue.pop()
    if path in seen:
        continue
    seen.add(path)
    with path.open('rb') as stream:
        magic = stream.read(4)
    if magic != b'\x7fELF':
        continue
    result = subprocess.run(['ldd', str(path)], env=environment, text=True, capture_output=True)
    if 'not found' in result.stdout:
        raise RuntimeError(f'{path}: {result.stdout}')
    for name, source in re.findall(r'^\s*(\S+) => (/[^(\s]+)', result.stdout, re.M):
        if host.match(name):
            continue
        destination = lib / name
        if not destination.exists():
            shutil.copy2(source, destination)
            manifest.append(f'{name}\t{source}')
            queue.append(destination)

licenses = appdir / 'usr/share/licenses'
licenses.mkdir(parents=True)
for sdk in (qt, gst):
    source = sdk / 'share/licenses'
    if source.exists():
        shutil.copytree(source, licenses / sdk.name, dirs_exist_ok=True)
# This machine's source-built SDKs keep license texts with their sources.
sources = qt.parent / 'src'
for name in ('qtbase-everywhere-src-6.8.3', 'qtwebsockets-everywhere-src-6.8.3', 'gstreamer-1.26.11'):
    for item in ('LICENSES', 'LICENSE', 'COPYING'):
        source = sources / name / item
        if source.is_dir():
            shutil.copytree(source, licenses / name / item, dirs_exist_ok=True)
        elif source.is_file():
            (licenses / name).mkdir(exist_ok=True)
            shutil.copy2(source, licenses / name / item)
for entry in manifest:
    source = entry.split('\t')[1]
    owner = subprocess.run(['dpkg-query', '-S', os.path.realpath(source)], text=True, capture_output=True)
    if owner.returncode != 0:
        owner = subprocess.run(['dpkg-query', '-S', source], text=True, capture_output=True)
    if owner.returncode == 0:
        package = owner.stdout.split(': ')[0].split(':')[0]
        notice = Path('/usr/share/doc') / package / 'copyright'
        if notice.exists():
            (licenses / 'system').mkdir(exist_ok=True)
            shutil.copy2(notice, licenses / 'system' / (package + '.copyright'))
(appdir / 'usr/share/bundled-libraries.txt').write_text('\n'.join(sorted(manifest)) + '\n')
for name in ('AppRun', 'zjdesk.desktop', 'zjdesk.svg', 'zjdesk.png'):
    shutil.copy2(root / 'packaging' / name, appdir / name)
(appdir / 'AppRun').chmod(0o755)
(appdir / '.DirIcon').symlink_to('zjdesk.png')
print(f'Bundled {len(seen)} files; collected {len(manifest)} system libraries.')
