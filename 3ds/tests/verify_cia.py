"""Check native CIA integrity, unencrypted content, metadata and ExeFS files."""
from pathlib import Path
import struct,hashlib
ROOT=Path(__file__).resolve().parents[1]
data=(ROOT/'SuperTux-0.7.0-Lean.cia').read_bytes()
header,_,_,cert,ticket,tmd_size,meta,content_size=struct.unpack_from('<IHHIIIIQ',data)
align=lambda n:(n+63)&~63
tmd_pos=align(header)+align(cert)+align(ticket)
content_pos=tmd_pos+align(tmd_size)
tmd=data[tmd_pos:tmd_pos+tmd_size]
assert tmd[:4]==bytes.fromhex('00010004')
body=0x140
assert struct.unpack_from('>Q',tmd,body+0x4c)[0]==0x000400000F707000
count=struct.unpack_from('>H',tmd,body+0x9e)[0];assert count==1
record=body+0x9c4
content_id,index,flags,size=struct.unpack_from('>IHHQ',tmd,record)
assert index==0 and not flags&1, 'CIA content is encrypted'
cxi=data[content_pos:content_pos+size]
assert len(cxi)==size==content_size
assert hashlib.sha256(cxi).digest()==tmd[record+16:record+48]
assert cxi[0x100:0x104]==b'NCCH' and cxi[0x18f]&4, 'NCCH must have NoCrypto set'
assert struct.unpack_from('<Q',cxi,0x118)[0]==0x000400000F707000
assert cxi[0x40f]==0x30, 'Invalid native main-thread priority'
assert cxi[0x40c]&3==3, 'New 3DS clock/L2 flags missing'
assert cxi[0x40d]&3==2, 'Expanded New 3DS memory mode missing'
exefs_offset,exefs_size=struct.unpack_from('<II',cxi,0x1a0)
exefs=cxi[exefs_offset*512:(exefs_offset+exefs_size)*512]
entries={}
for i in range(10):
    name,offset,length=struct.unpack_from('<8sII',exefs,i*16)
    name=name.rstrip(bytes(1)).decode()
    if not name:continue
    content=exefs[512+offset:512+offset+length]
    assert len(content)==length and hashlib.sha256(content).digest()==exefs[0xc0+(9-i)*32:0xe0+(9-i)*32], name
    entries[name]=content
assert {'.code','icon','banner'}<=entries.keys()
assert entries['icon']==(ROOT/'cia/icon.smdh').read_bytes()
assert entries['banner']==(ROOT/'cia/banner.bnr').read_bytes()
print('PASS native CIA: unencrypted CIA/NCCH, title ID, content hash, ExeFS hashes, SuperTux icon/banner, launch settings')
