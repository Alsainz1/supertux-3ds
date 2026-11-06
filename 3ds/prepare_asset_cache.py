"""Prepare lossless GPU layouts and native PCM on the PC, not during gameplay.

Original images/audio stay unchanged. The native port falls back when a key is
absent, allowing unusual regions and modified assets to keep working.
"""
from pathlib import Path
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'host-tools'))
import numpy as np
from PIL import Image

DATA = ROOT.parent / 'data'
CACHE = ROOT / 'asset-cache-lean'


class Pack:
    def __init__(self, name):
        self.name = name
        self.file = (CACHE / (name + '.bin')).open('wb')
        self.entries = []
        self.blocks = {}

    def add(self, key, raw, compress=True):
        packed = zlib.compress(raw, 1) if compress else raw
        if len(packed) >= len(raw):
            packed = raw
        digest = hashlib.sha256(raw).digest()
        block = self.blocks.get(digest)
        if block is None:
            block = (self.file.tell(), len(packed), len(raw))
            self.file.write(packed)
            self.blocks[digest] = block
        self.entries.append((key, block))

    def close(self):
        self.file.close()
        with (CACHE / (self.name + '.idx')).open('wb') as index:
            index.write(struct.pack('<8sI', b'STPACK01', len(self.entries)))
            for key, (offset, packed, raw) in self.entries:
                name = key.encode('utf-8')
                index.write(struct.pack('<QIIH', offset, packed, raw, len(name)))
                index.write(name)
        return {'entries': len(self.entries), 'unique_blocks': len(self.blocks),
                'bytes': (CACHE / (self.name + '.bin')).stat().st_size}


def pow2(n):
    return max(8, 1 << (n - 1).bit_length())


def sample_for_asset(key, width, height):
    # Keep UI/fonts and the 1:1 world map sharp. Gameplay is normally displayed
    # at half its authored size, so prepare those pixels once on the host.
    preserve = any(key.startswith(prefix) for prefix in
                   ['/images/engine/', '/images/worldmap/', '/images/worlds/', '/images/powerups/'])
    sample = 1 if preserve or max(width, height) <= 16 else 2
    while max(width, height) > 512 * sample:
        sample *= 2
    return sample


def texture(pixels, sample=None):
    height, width = pixels.shape[:2]
    if not pixels[:, :, 3].any():
        return struct.pack('<4sI', b'TX02', 0)
    if sample is None:
        sample = 2 if max(width, height) >= 1024 else 1
    step = 512 * sample
    chunks = []
    for y in range(0, height, step):
        for x in range(0, width, step):
            w, h = min(step, width-x), min(step, height-y)
            cut = pixels[y:y+h, x:x+w]
            if sample > 1:
                cut = np.asarray(Image.fromarray(cut).resize(
                    ((w+sample-1)//sample, (h+sample-1)//sample), Image.Resampling.BOX))
            ph, pw = cut.shape[:2]
            tw, th = pow2(pw), pow2(ph)
            # Extrude POT padding offline so smooth backgrounds never need a
            # CPU pass over their texture during the first visible frame.
            padded = np.pad(cut[:, :, [3, 2, 1, 0]], ((0, th-ph), (0, tw-pw), (0, 0)), mode='edge')
            yy, xx = np.indices((th, tw), dtype=np.uint32)
            morton = ((xx & 1) | ((yy & 1) << 1) | ((xx & 2) << 1) |
                      ((yy & 2) << 2) | ((xx & 4) << 2) | ((yy & 4) << 3))
            position = (((yy//8)*(tw//8)+xx//8)*64+morton).ravel()
            swizzled = np.empty((th*tw, 4), dtype=np.uint8)
            swizzled[position] = padded.reshape(-1, 4)
            chunks.append(struct.pack('<7H', x, y, w, h, sample, tw, th) + swizzled.tobytes())
    return struct.pack('<4sI', b'TX02', len(chunks)) + b''.join(chunks)


class AtlasPacker:
    """Group small terrain/enemy/decoration images by directory, with edge gutters."""
    def __init__(self, pack):
        self.pack = pack
        self.groups = {}
        self.entries = 0
        self.pages = 0

    def add(self, key, pixels, sample):
        h, w = pixels.shape[:2]
        # Backgrounds and UI retain independent filtering/padding semantics.
        eligible = ('/images/tiles/' in key and '|' in key or
                    key.startswith(('/images/creatures/', '/images/decal/', '/images/objects/')))
        pw, ph = (w+sample-1)//sample, (h+sample-1)//sample
        if not eligible or max(pw, ph)>126 or not pixels[:, :, 3].any():
            self.pack.add(key, texture(pixels, sample))
            return
        cut = np.asarray(Image.fromarray(pixels).resize((pw, ph), Image.Resampling.BOX)) if sample>1 else pixels
        group = key.split('|')[0].rsplit('/', 1)[0]
        self.groups.setdefault(group, []).append((key, cut.copy(), w, h, sample))
        self.entries += 1

    def close(self):
        for group, items in sorted(self.groups.items()):
            # First-fit shelves, largest first, deterministic source-key tie-break.
            items.sort(key=lambda it: (-it[1].shape[0], -it[1].shape[1], it[0]))
            area=sum((it[1].shape[0]+2)*(it[1].shape[1]+2) for it in items)
            edge=max(max(it[1].shape[:2])+2 for it in items)
            page_size=min(256, max(32, pow2(max(edge, int(area**.5)+1))))
            page = np.zeros((page_size, page_size, 4), dtype=np.uint8)
            x = y = row_h = 0
            page_key = f'/3ds-atlas/{self.pages:04d}'
            used = False
            for key, cut, w, h, sample in items:
                ph, pw = cut.shape[:2]
                if x+pw+2>page_size:
                    x=0; y+=row_h; row_h=0
                if y+ph+2>page_size:
                    self.pack.add(page_key, texture(page, 1))
                    self.pages+=1; page_key=f'/3ds-atlas/{self.pages:04d}'
                    page=np.zeros((page_size,page_size,4),dtype=np.uint8);x=y=row_h=0
                page[y:y+ph+2, x:x+pw+2] = np.pad(cut, ((1,1),(1,1),(0,0)), mode='edge')
                ref=page_key.encode('utf-8')
                self.pack.add(key, struct.pack('<4s6H', b'TX03', w, h, sample, x+1, y+1, len(ref))+ref)
                x+=pw+2;row_h=max(row_h,ph+2);used=True
            if used:
                self.pack.add(page_key, texture(page, 1));self.pages+=1
        print(f'Shared atlases: {self.entries} regions in {self.pages} pages', flush=True)


def build():
    CACHE.mkdir(exist_ok=True)
    images = sorted(p for p in (DATA/'images').rglob('*')
                    if p.suffix.lower() in {'.png', '.jpg', '.jpeg', '.bmp', '.webp'})
    sounds = sorted(p for root in ['sounds', 'speech'] for p in (DATA/root).rglob('*')
                    if p.suffix.lower() in {'.wav', '.ogg'})
    fingerprint = hashlib.sha256(Path(__file__).read_bytes())
    for p in images + sounds:
        fingerprint.update(f'{p.relative_to(DATA)}:{p.stat().st_size}:{p.stat().st_mtime_ns}'.encode())
    stamp = fingerprint.hexdigest()
    manifest = CACHE / 'manifest.json'
    if manifest.exists() and json.loads(manifest.read_text()).get('fingerprint') == stamp:
        if all((CACHE / (name+ext)).exists() for name in ['3ds-gpu','3ds-audio'] for ext in ['.idx','.bin']):
            print('Prepared asset cache is current.', flush=True)
            return
    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg:
        raise RuntimeError('ffmpeg is required to prepare the 32000 Hz stereo PCM cache')
    gpu = Pack('3ds-gpu')
    atlases = AtlasPacker(gpu)
    for i, p in enumerate(images):
        with Image.open(p) as image:
            rgba = np.asarray(image.convert('RGBA'))
        key = '/' + p.relative_to(DATA).as_posix()
        atlases.add(key, rgba, sample_for_asset(key, rgba.shape[1], rgba.shape[0]))
        # Tile sheets use exact 32x32 regions; cache these too, avoiding a
        # complete PNG atlas decode to display one newly encountered tile.
        h, w = rgba.shape[:2]
        if '/images/tiles/' in key and w <= 2048 and h <= 2048:
            for y in range(0, h-31, 32):
                for x in range(0, w-31, 32):
                    atlases.add(f'{key}|{x},{y},32,32', rgba[y:y+32, x:x+32], sample_for_asset(key,32,32))
        if i % 500 == 0:
            print(f'Preparing GPU assets: {i}/{len(images)}', flush=True)
    atlases.close()
    graphics = gpu.close()
    audio = Pack('3ds-audio')
    for p in sounds:
        pcm = subprocess.run([ffmpeg, '-v', 'error', '-i', str(p), '-f', 's16le',
                              '-ar', '32000', '-ac', '2', '-'], check=True, capture_output=True).stdout
        if not pcm or len(pcm) % 4:
            raise RuntimeError(f'Invalid prepared PCM: {p}')
        audio.add('/'+p.relative_to(DATA).as_posix(), pcm, compress=True)
    sound = audio.close()
    manifest.write_text(json.dumps({'fingerprint': stamp, 'graphics': graphics, 'audio': sound}, indent=2)+'\n')
    print(f'Prepared assets: {graphics}, audio: {sound}', flush=True)


if __name__ == '__main__':
    build()
