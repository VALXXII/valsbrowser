"""Pinned, project-local ThorVG build. No global install or PATH modification."""
from pathlib import Path
import hashlib, os, subprocess, sys, urllib.request, zipfile
ROOT = Path(__file__).resolve().parent
VERSION = '1.1.3'
SHA256 = '03850904c300e5969560d49446eab3e534c4160c379909f32bc49a32ea6f4072'
archive = ROOT / f'deps/thorvg-{VERSION}.zip'
source = ROOT / f'deps/thorvg-{VERSION}'
if not archive.exists():
    urllib.request.urlretrieve(f'https://github.com/thorvg/thorvg/archive/refs/tags/v{VERSION}.zip', archive)
if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
    raise SystemExit('ThorVG source checksum mismatch')
if not source.exists():
    with zipfile.ZipFile(archive) as z: z.extractall(ROOT / 'deps')
packages = ROOT / 'tools/lottie-build'
# Build tools pinned by hash, like every other input of the shipped binaries (audit SEC-16): wheels only, no dependencies.
REQUIREMENTS = '''meson==1.9.1 --hash=sha256:f824ab770c041a202f532f69e114c971918ed2daff7ea56583d80642564598d0
ninja==1.13.0 --hash=sha256:fb8ee8719f8af47fed145cced4a85f0755dd55d45b2bddaf7431fa89803c5f3e --hash=sha256:3c0b40b1f0bba764644385319028650087b4c1b18cdfa6f45cb39a3669b81aa9 --hash=sha256:8cfbb80b4a53456ae8a39f90ae3d7a2129f45ea164f43fadfa15dc38c4aef1c9
'''
if not (packages / 'mesonbuild').exists() or not (packages / 'ninja').exists():
    requirements = ROOT / 'build/lottie-build-requirements.txt'
    requirements.parent.mkdir(parents=True, exist_ok=True)
    requirements.write_text(REQUIREMENTS, encoding='ascii')
    subprocess.run([sys.executable, '-m', 'pip', 'install', '--target', str(packages), '--require-hashes',
                    '--only-binary=:all:', '--no-deps', '-r', str(requirements)], check=True)
env = os.environ.copy()
env['PYTHONPATH'] = str(packages)
env['PATH'] = str(packages / 'bin') + os.pathsep + env['PATH']
env['AR'] = str(ROOT / 'tools/w64devkit/bin/ar.exe')
env['STRIP'] = str(ROOT / 'tools/w64devkit/bin/strip.exe')
env['CC'] = str(ROOT / 'tools/w64devkit/bin/gcc.exe')
env['CXX'] = str(ROOT / 'tools/w64devkit/bin/g++.exe')
build = ROOT / 'build/lottie-backend'
subprocess.run([sys.executable, '-m', 'mesonbuild.mesonmain', 'setup', str(build), str(source),
                *(['--reconfigure'] if (build / 'build.ninja').exists() else []),
                '-Ddebug=false', '-Dbuildtype=minsize', '-Ddefault_library=static', '-Dstatic=true',
                '-Dengines=cpu', '-Dloaders=lottie,svg', '-Dsavers=', '-Dbindings=',
                '-Dtools=', '-Dextra=', '-Dthreads=false', '-Dsimd=false',
                '-Dpartial=false', '-Dfile=false', '-Dtests=false', '-Db_lto=false'], env=env, check=True)
subprocess.run([str(packages / 'bin/ninja.exe'), '-C', str(build), '-j', '4'], env=env, check=True)
print('ThorVG static library:', build / 'src/libthorvg-1.a')
