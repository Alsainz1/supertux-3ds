"""Validate the shipped binary packs and compare conversion with tex3ds output."""
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import prepare_asset_cache as converter
from PIL import Image
import numpy as np


def check_pack(name, graphics):
    index = (ROOT/'asset-cache-lean'/f'{name}.idx').read_bytes()
    data = (ROOT/'asset-cache-lean'/f'{name}.bin').read_bytes()
    magic, count = struct.unpack_from('<8sI', index)
    assert magic == b'STPACK01' and count < 100000
    cursor, keys, blocks = 12, set(), set()
    atlas_refs=[]
    payloads={}
    payloads_by_block={}
    for _ in range(count):
        offset, packed, raw, length = struct.unpack_from('<QIIH', index, cursor)
        cursor += 18
        key = index[cursor:cursor+length].decode('utf-8')
        cursor += length
        assert key.startswith('/') and key not in keys
        keys.add(key)
        assert 0 < packed <= raw <= 64*1024*1024 and offset+packed <= len(data)
        if (offset, packed) in blocks:
            if graphics:payloads[key]=payloads_by_block[(offset,packed)]
            continue
        blocks.add((offset, packed))
        payload = data[offset:offset+packed]
        if packed != raw:
            payload = zlib.decompress(payload)
        assert len(payload) == raw
        if not graphics:
            assert raw % 4 == 0, 'Stereo signed-16 PCM must contain complete frames'
            continue
        payloads[key]=payload;payloads_by_block[(offset,packed)]=payload
        if payload[:4]==b'TX03':
            w,h,sample,x,y,length=struct.unpack_from('<6H',payload,4)
            page=payload[16:].decode('utf-8')
            assert len(payload)==16+length and page.startswith('/3ds-atlas/')
            assert w>0 and h>0 and sample in (1,2,4,8,16,32,64,128)
            atlas_refs.append((key,w,h,sample,x,y,page));continue
        tag, tiles = struct.unpack_from('<4sI', payload)
        assert tag in (b'TX01',b'TX02')
        pos = 8
        for _ in range(tiles):
            x, y, w, h, sample, tw, th = struct.unpack_from('<7H', payload, pos)
            pos += 14
            assert sample in (1,2,4,8,16,32,64,128) and w > 0 and h > 0
            assert 8 <= tw <= 512 and 8 <= th <= 512
            assert tw & (tw-1) == 0 and th & (th-1) == 0
            assert w <= tw*sample and h <= th*sample
            pos += tw*th*4
        assert pos == len(payload), 'Texture payload must fit its declared tiles exactly'
    assert cursor == len(index)
    decoded_pages={}
    for key,w,h,sample,x,y,page in atlas_refs:
        assert page in payloads
        rawpage=payloads[page]
        _,n=struct.unpack_from('<4sI',rawpage);assert n==1
        _,_,pw,ph,ps,tw,th=struct.unpack_from('<7H',rawpage,8)
        assert ps==1 and pw==tw and ph==th
        if page not in decoded_pages:
            yy,xx=np.indices((th,tw),dtype=np.uint32)
            morton=((xx&1)|((yy&1)<<1)|((xx&2)<<1)|((yy&2)<<2)|((xx&4)<<2)|((yy&4)<<3))
            positions=((yy//8)*(tw//8)+xx//8)*64+morton
            pixels=np.frombuffer(rawpage[22:],dtype=np.uint8).reshape((-1,4))[positions]
            decoded_pages[page]=pixels[:,:,[3,2,1,0]]
        imagekey,_,region=key.partition('|')
        with Image.open(converter.DATA/imagekey.lstrip('/')) as src:
            original=src.convert('RGBA')
            if region:
                rx,ry,rw,rh=map(int,region.split(','));original=original.crop((rx,ry,rx+rw,ry+rh))
            assert original.size==(w,h)
            dw,dh=(w+sample-1)//sample,(h+sample-1)//sample
            expected=np.asarray(original.resize((dw,dh),Image.Resampling.BOX)) if sample>1 else np.asarray(original)
        actual=decoded_pages[page][y:y+dh,x:x+dw]
        assert np.array_equal(actual,expected), key
        # Each edge is extruded into a gutter, so UV boundaries cannot bleed.
        assert np.array_equal(decoded_pages[page][y-1,x:x+dw],expected[0]),key
        assert np.array_equal(decoded_pages[page][y+dh,x:x+dw],expected[-1]),key
    if atlas_refs:print(f'PASS {len(atlas_refs)} atlas regions: original pixels/geometry, UV bounds, edge gutters')
    print(f'PASS {name}: {count} entries, {len(blocks)} unique blocks')


fixtures = ROOT/'tests/fixtures'
# Policy checks cover native 1:1 map/UI preservation and smaller gameplay
# assets; original logical dimensions remain encoded in every tile.
assert converter.sample_for_asset('/images/worldmap/common/tux.png',32,32)==1
assert converter.sample_for_asset('/images/engine/hud/coins-0.png',64,32)==1
assert converter.sample_for_asset('/images/creatures/tux/body.png',64,64)==2
assert converter.sample_for_asset('/images/background/forest/test.png',4096,2048)==8
for dims in [(32,32),(513,127),(2048,1024),(4096,2048)]:
    sample=converter.sample_for_asset('/images/background/test.png',*dims)
    payload=converter.texture(np.full((dims[1],dims[0],4),255,dtype=np.uint8),sample)
    tag,count=struct.unpack_from('<4sI',payload)
    assert tag==b'TX02' and count==1
    x,y,w,h,s,tw,th=struct.unpack_from('<7H',payload,8)
    assert (w,h)==dims and s==sample and max(tw,th)<=512
print('PASS Lean sampling policy, 512px texture cap and preserved geometry')
pixels = np.asarray(Image.open(fixtures/'tex3ds-pattern.png').convert('RGBA'))
converted = converter.texture(pixels)
# The independent devkitPro tex3ds reference has a four-byte format header.
reference = (fixtures/'tex3ds-reference.bin').read_bytes()
assert converted[22:] == reference[4:]
print('PASS GPU channels and Morton swizzle match the tex3ds reference')
check_pack('3ds-gpu', True)
check_pack('3ds-audio', False)
