"""Cross-build the actual 0.7.0 engine; optionally assemble an SD-card package."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT.parent
SDK = Path('C:/devkitPro')
BUILD = ROOT / 'build-native'
STAGE = ROOT / 'lean-dist'
GAME = STAGE / '3ds/supertux070'
NAME = 'SuperTux-0.7.0-Lean'

def run(args):
    subprocess.run([str(x) for x in args], check=True)

def prepare_loading_label():
    import sys
    sys.path.insert(0, str(ROOT / 'host-tools'))
    from PIL import Image, ImageDraw, ImageFont
    font=ImageFont.truetype(str(SOURCE/'data/fonts/SuperTux-Medium.ttf'),18)
    box=font.getbbox('Loading..');title_w,title_h=box[2]-box[0],box[3]-box[1]
    subfont=ImageFont.truetype(str(SOURCE/'data/fonts/SuperTux-Medium.ttf'),16)
    subtitle='this will take some time...';subbox=subfont.getbbox(subtitle)
    sw,sh=subbox[2]-subbox[0],subbox[3]-subbox[1]
    w,h=max(title_w,sw),title_h+12+sh;title_x=(w-title_w)//2
    tw,th=max(8,1<<(w-1).bit_length()),max(8,1<<(h-1).bit_length())
    image=Image.new('RGBA',(tw,th));draw=ImageDraw.Draw(image)
    draw.text((title_x-box[0],-box[1]),'Loading..',font=font,fill='white')
    draw.text(((w-sw)//2-subbox[0],title_h+12-subbox[1]),subtitle,font=subfont,fill='white')
    pixels=image.load();out=bytearray(tw*th*4)
    for y in range(th):
        for x in range(tw):
            morton=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3)
            pos=((y//8)*(tw//8)+x//8)*64+morton
            r,g,b,a=pixels[x,y];out[pos*4:pos*4+4]=bytes((a,b,g,r))
    text='#pragma once\n// Generated from the original SuperTux font; no system-font dependency.\n'
    text+=f'constexpr int loading_label_w={w},loading_label_h={h},loading_label_tw={tw},loading_label_th={th};\n'
    text+=f'constexpr int loading_title_x={title_x},loading_title_w={title_w},loading_title_h={title_h};\n'
    text+='alignas(4) constexpr unsigned char loading_label_pixels[]={\n'
    text+='\n'.join(','.join(str(v) for v in out[i:i+32])+',' for i in range(0,len(out),32))+'\n};\n'
    target=ROOT/'engine/loading_label.hpp'
    if not target.exists() or target.read_text()!=text:target.write_text(text)


def cmake_tools():
    cmake = ROOT / 'host-tools/cmake/data/bin/cmake.exe'
    ninja = ROOT / 'host-tools/bin/ninja.exe'
    if not cmake.exists() or not ninja.exists():
        raise RuntimeError('Install native build tools: python -m pip install --target 3ds/host-tools cmake ninja')
    return cmake, ninja

def copy_changed(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and source.stat().st_size == destination.stat().st_size and source.stat().st_mtime_ns == destination.stat().st_mtime_ns:
        return
    shutil.copy2(source, destination)

def write_image_metadata(destination):
    from PIL import Image
    lines = []
    for path in sorted((SOURCE / 'data').rglob('*')):
        if path.suffix.lower() not in {'.png', '.jpg', '.jpeg', '.bmp', '.gif', '.webp'}:
            continue
        with Image.open(path) as image:
            width, height = image.size
        lines.append(f'{width} {height} /{path.relative_to(SOURCE / "data").as_posix()}')
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(lines) + '\n')

def package():
    run([os.sys.executable, ROOT / 'prepare_audio.py'])
    GAME.mkdir(parents=True, exist_ok=True)
    run([os.sys.executable, ROOT / 'prepare_asset_cache.py'])
    run([os.sys.executable, ROOT / 'build_cia.py'])
    copy_changed(ROOT/'cia/icon.smdh', GAME/(NAME+'.smdh'))
    copy_changed(ROOT/(NAME+'.cia'), STAGE/'cias'/(NAME+'.cia'))
    run([SDK / 'tools/bin/3dsxtool.exe', BUILD / 'supertux070.elf', GAME / (NAME + '.3dsx'),
         '--smdh=' + str(GAME / (NAME + '.smdh'))])
    # Native SD assets: avoid embedding the entire desktop data set in the 3DSX.
    # Include streamed music and effects alongside the native renderer's assets.
    for name in ['fonts', 'images', 'levels', 'locale', 'particles', 'scripts', 'shader', 'music', 'sounds', 'speech']:
        for item in (SOURCE / 'data' / name).rglob('*'):
            if item.is_file() and not {'3ds-lab', 'bonus1', 'revenge_in_redmond'}.intersection(item.relative_to(SOURCE / 'data').parts): copy_changed((ROOT/'audio-lean'/item.relative_to(SOURCE/'data')) if (ROOT/'audio-lean'/item.relative_to(SOURCE/'data')).exists() else item, GAME / 'data' / item.relative_to(SOURCE / 'data'))
    copy_changed(ROOT/'audio-lean/music/misc/invincible-3ds.pcmz',GAME/'data/music/misc/invincible-3ds.pcmz')
    for item in (SOURCE / 'data').iterdir():
        if item.is_file(): copy_changed(item, GAME / 'data' / item.name)
    for item in (ROOT / 'asset-cache-lean').glob('3ds-*'):
        copy_changed(item, GAME / 'data' / item.name)
    write_image_metadata(GAME / 'data/3ds-image-dimensions.txt')
    copy_changed(ROOT / 'ENGINE-README.md', STAGE / 'README.md')
    for doc in ['BUILDING.md','GITHUB.md']:copy_changed(ROOT/doc, STAGE/doc)
    license_dir = STAGE / 'licenses'
    for src, name in [(SOURCE / 'LICENSE.txt', 'SuperTux.txt'),
                      (SOURCE / 'external/sexp-cpp/LICENSE.txt', 'sexp-cpp.txt'),
                      (SOURCE / 'external/simplesquirrel/libs/squirrel/COPYRIGHT', 'Squirrel.txt'),
                      (ROOT / 'vendor/SDL/LICENSE.txt', 'SDL2.txt'),
                      (ROOT / 'vendor/SDL_image/LICENSE.txt', 'SDL2_image.txt'),
                      (ROOT / 'vendor/SDL_mixer/LICENSE.txt', 'SDL2_mixer.txt'),
                      (ROOT / 'vendor/freetype/docs/FTL.TXT', 'FreeType.txt'),
                      (ROOT / 'vendor/fmt/LICENSE', 'fmt.txt'),
                      (ROOT / 'vendor/physfs/LICENSE.txt', 'PhysicsFS.txt'),
                      (ROOT / 'vendor/glm/copying.txt', 'GLM.txt')]:
        if src.exists(): copy_changed(src, license_dir / name)
    binary = GAME / (NAME + '.3dsx')
    copy_changed(binary, ROOT/(NAME+'.3dsx'))
    manifest = {'version': '0.7.0', 'platform': 'Nintendo 3DS',
                'entry': 'Title screen -> Icy Island story intro -> worldmap -> level cards',
                'renderer': 'Citro2D GPU, top scene and bottom interface', 'debug_screen': 'startup/fatal console only; normal play uses bottom interface',
                'font': 'Original PC fonts; SuperTux TTF HUD counters and headers',
                'hud': 'Top: menus, gameplay, timed-level clock/coins. Bottom: help/subtitles, continuous map view, gameplay reserve/stats',
                'optimizations': ['Shared GPU atlas pages for terrain, creatures and small decorations', 'Early offscreen sprite culling', 'No current-frame animation-vector copies', 'Offline half-resolution gameplay art and 512px maximum textures', 'Decorative emitters/trails disabled; basic bursts capped at 8', 'Offline texture edge padding', 'Batched single-tile sprite rotation', 'Boss HUD and cinematic draw-state invalidation', 'Rounded-panel interior batching', 'Cached looping-source mixer settings', 'Bottom HUD redraw only on state changes', 'Bilinear cloud/background motion', 'Cached single-tile UV draw path', 'Collision group candidate lists', 'Adjacent sprite batching', 'Contiguous effect particles and linear compaction', 'Sort drawing requests once per canvas', 'Reuse camera shake easing', 'Ground-pound/melt GPU warmup', 'Priority animation prefetch', 'Explosion recipient filtering before line-of-sight tests', 'Explosion animation warmup', 'Asynchronous one-shot mixer commands', 'Secret-area sound preload', 'Bounded terrain look-ahead prefetch', 'Prepared GPU and PCM packs', 'Buffered SD logging'],
                'cia_title_id': '000400000F707000',
                'cia_assets': 'External sd:/3ds/supertux070/data; shared saves with 3DSX',
                'audio': 'NDSP; offline 32 kHz Vorbis music, lossless packed PCM effects, compressed PCM star theme',
                'sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
    (STAGE / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    archive = ROOT / 'SuperTux-0.7.0-3DS-Lean.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as out:
        for item in sorted(STAGE.rglob('*')):
            if item.is_file() and item.name!='invincible-3ds.wav':
                mode = zipfile.ZIP_STORED if item.name == '3ds-gpu.bin' else zipfile.ZIP_DEFLATED
                out.write(item, item.relative_to(STAGE), compress_type=mode)
    print(f'Package: {archive}')
    update = ROOT / 'SuperTux-0.7.0-3DS-Lean-update.zip'
    with zipfile.ZipFile(update, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as out:
        for item in sorted(STAGE.rglob('*')):
            if not item.is_file() or item.name=='invincible-3ds.wav': continue
            relative = item.relative_to(STAGE)
            if 'data' not in relative.parts or relative.as_posix().startswith(('3ds/supertux070/data/music/','3ds/supertux070/data/levels/')) or (relative.as_posix().startswith('3ds/supertux070/data/images/') and item.suffix in {'.strf','.sprite'}) or item.name=='3ds-image-dimensions.txt' or relative.as_posix() in {'3ds/supertux070/data/levels/world1/intro.nut','3ds/supertux070/data/music/misc/invincible-3ds.wav','3ds/supertux070/data/3ds-gpu.idx','3ds/supertux070/data/3ds-gpu.bin','3ds/supertux070/data/3ds-audio.idx','3ds/supertux070/data/3ds-audio.bin'}:
                mode = zipfile.ZIP_STORED if item.name == '3ds-gpu.bin' else zipfile.ZIP_DEFLATED
                out.write(item, relative, compress_type=mode)
    print(f'Update (requires existing FastLoad data/cache packs): {update}')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--package', action='store_true')
    parser.add_argument('--package-only', action='store_true')
    args = parser.parse_args()
    if not args.package_only:
        prepare_loading_label()
        cmake, ninja = cmake_tools()
        run([cmake, '-S', ROOT / 'engine', '-B', BUILD, '-G', 'Ninja',
             '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'engine/toolchain-windows.cmake'),
             '-DCMAKE_MAKE_PROGRAM=' + str(ninja), '-DCMAKE_BUILD_TYPE=Release'])
        run([cmake, '--build', BUILD, '-j', str(min(6, os.cpu_count() or 1))])
    if args.package or args.package_only: package()

if __name__ == '__main__':
    main()
