"""Prepare smaller native audio without modifying original data assets."""
from pathlib import Path
import concurrent.futures,hashlib,json,shutil,struct,subprocess,zlib
ROOT=Path(__file__).resolve().parent
DATA=ROOT.parent/'data'
OUT=ROOT/'audio-lean'
RECIPE='vorbis-q2-32000-v1'
def build():
    ffmpeg=shutil.which('ffmpeg')
    if not ffmpeg:raise RuntimeError('ffmpeg required')
    OUT.mkdir(exist_ok=True)
    stampfile=OUT/'manifest.json'
    old=json.loads(stampfile.read_text()) if stampfile.exists() else {}
    inputs=sorted((DATA/'music').rglob('*.ogg'))
    def track(p):
        key=p.relative_to(DATA).as_posix();dst=OUT/key
        stamp=f'{RECIPE}:{p.stat().st_size}:{p.stat().st_mtime_ns}'
        if old.get(key,{}).get('stamp')!=stamp or not dst.exists():
            dst.parent.mkdir(parents=True,exist_ok=True)
            temp=dst.with_suffix('.tmp.ogg')
            subprocess.run([ffmpeg,'-v','error','-y','-threads','1','-i',str(p),'-vn','-ar','32000','-ac','2','-c:a','libvorbis','-q:a','2',str(temp)],check=True)
            if temp.stat().st_size<p.stat().st_size:temp.replace(dst)
            else:shutil.copy2(p,dst);temp.unlink()
        return key,{'stamp':stamp,'original':p.stat().st_size,'prepared':dst.stat().st_size}
    result={}
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for i,(key,value) in enumerate(pool.map(track,inputs),1):
            result[key]=value
            if i%10==0:print(f'Prepared music {i}/{len(inputs)}',flush=True)
    src=DATA/'music/misc/invincible.ogg';wav=OUT/'star-native.wav'
    blob=OUT/'music/misc/invincible-3ds.pcmz'
    stamp=f'{src.stat().st_size}:{src.stat().st_mtime_ns}'
    if old.get('star_stamp')!=stamp or not blob.exists():
        subprocess.run([ffmpeg,'-v','error','-y','-i',str(src),'-ar','32000','-ac','2','-c:a','pcm_s16le',str(wav)],check=True)
        data=wav.read_bytes();blob.write_bytes(b'MW01'+struct.pack('<I',len(data))+zlib.compress(data,3))
    result['star_stamp']=stamp
    stampfile.write_text(json.dumps(result,indent=2)+'\n')
    tracks=[v for v in result.values() if isinstance(v,dict)]
    print('Music bytes:',sum(v['original'] for v in tracks),'->',sum(v['prepared'] for v in tracks),flush=True)
    print('Star compressed PCM bytes:',blob.stat().st_size,flush=True)
if __name__=='__main__':build()
