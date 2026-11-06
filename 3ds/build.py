"""Build the initial native 3DS target. Run with Python 3 and Pillow installed."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parent
UPSTREAM = ROOT.parent
SDK = Path(os.environ.get('DEVKITPRO', 'C:/devkitPro'))
if os.name == 'nt' and not SDK.exists() and str(SDK).replace('\\', '/') == '/opt/devkitpro':
    SDK = Path('C:/devkitPro')
ARM = SDK / 'devkitARM/bin'
TOOLS = SDK / 'tools/bin'
BUILD = ROOT / 'build'
ROMFS = BUILD / 'romfs'
OUT = ROOT / 'dist'
SQ = UPSTREAM / 'external/simplesquirrel/libs/squirrel'
SEXP = UPSTREAM / 'external/sexp-cpp'

def run(command):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True)
    if result.stdout: print(result.stdout, end='')
    if result.stderr: print(result.stderr, end='')
    if result.returncode: raise RuntimeError(f'Command failed ({result.returncode}): {command[0]}')

def main():
    for path in [BUILD, ROMFS / 'levels', OUT]: path.mkdir(parents=True, exist_ok=True)
    for required in [SEXP / 'src/parser.cpp', SQ / 'squirrel/sqvm.cpp']:
        if not required.exists(): raise RuntimeError(f'Missing pinned dependency: {required}')
    level = UPSTREAM / 'data/levels/world1/welcome_antarctica.stl'
    shutil.copy2(level, ROMFS / 'levels' / level.name)
    logo = Image.open(UPSTREAM / 'data/images/engine/menu/logo.png').convert('RGBA')
    logo.thumbnail((360, 100), Image.Resampling.LANCZOS)
    logo.save(BUILD / 'logo.png')
    run([TOOLS / 'tex3ds.exe', BUILD / 'logo.png', '-o', ROMFS / 'logo.t3x'])

    sources = sorted((ROOT / 'source').glob('*.cpp'))
    sources += sorted((SEXP / 'src').glob('*.cpp'))
    sources += sorted((SQ / 'squirrel').glob('*.cpp'))
    sources += [SQ / 'sqstdlib/sqstdaux.cpp']
    arch = ['-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft']
    flags = arch + ['-O2', '-g', '-std=gnu++17', '-D__3DS__', '-Wall',
                    '-ffunction-sections', '-fdata-sections', '-fexceptions', '-frtti', '-fno-strict-aliasing']
    includes = [ROOT / 'source', SEXP / 'include', SQ / 'include', SDK / 'libctru/include']
    flags += ['-I' + str(x) for x in includes]
    objects = [BUILD / (str(p.relative_to(UPSTREAM)).replace('\\', '_').replace('/', '_') + '.o') for p in sources]
    def compile_one(pair):
        source, obj = pair
        run([ARM / 'arm-none-eabi-g++.exe', *flags, '-c', source, '-o', obj])
    with ThreadPoolExecutor(max_workers=min(4, os.cpu_count() or 1)) as pool:
        list(pool.map(compile_one, zip(sources, objects)))

    target = OUT / 'SuperTux-0.7.0-3DS-initial'
    run([ARM / 'arm-none-eabi-g++.exe', *arch, '-specs=3dsx.specs',
         '-Wl,--gc-sections', '-Wl,-Map,' + str(BUILD / 'supertux.map'),
         *objects, '-L' + str(SDK / 'libctru/lib'), '-lcitro2d', '-lcitro3d', '-lctru', '-lm',
         '-o', Path(str(target) + '.elf')])
    run([TOOLS / 'smdhtool.exe', '--create', 'SuperTux 0.7.0',
         '3DS initial port test - not gameplay', 'SuperTux contributors',
         SDK / 'libctru/default_icon.png', Path(str(target) + '.smdh')])
    run([TOOLS / '3dsxtool.exe', Path(str(target) + '.elf'), Path(str(target) + '.3dsx'),
         '--smdh=' + str(Path(str(target) + '.smdh')), '--romfs=' + str(ROMFS)])
    manifest = {
        'target': 'Nintendo 3DS only', 'upstream': '0.7.0',
        'stage': 'native bring-up; gameplay not integrated',
        'level_sha256': hashlib.sha256(level.read_bytes()).hexdigest(),
        'dependencies': json.loads((ROOT / 'upstream-dependencies.json').read_text()),
        '3dsx_sha256': hashlib.sha256(Path(str(target) + '.3dsx').read_bytes()).hexdigest(),
        'translation_units': len(sources), 'debug_screen': 'bottom',
    }
    (OUT / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    shutil.copy2(ROOT / 'README.md', OUT / 'README.md')
    print(f'Built {Path(str(target) + ".3dsx")}')

if __name__ == '__main__':
    main()
