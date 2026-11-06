"""Build a native HOME Menu CIA from the engine ELF; assets/saves stay on SD."""
from pathlib import Path
import subprocess
from PIL import Image
ROOT=Path(__file__).resolve().parent
SOURCE=ROOT.parent
OUT=ROOT/'cia'
TOOLS=ROOT/'host-tools/cia'
NAME='SuperTux-0.7.0-Lean'

def run(args):
    subprocess.run([str(x) for x in args],check=True)

def metadata():
    OUT.mkdir(exist_ok=True)
    icon=Image.open(SOURCE/'data/images/engine/icons/supertux-256x256.png').convert('RGBA')
    icon.resize((48,48),Image.Resampling.LANCZOS).save(OUT/'icon.png')
    banner=Image.new('RGBA',(256,128),(14,29,51,255))
    logo=Image.open(SOURCE/'data/images/engine/menu/logo.png').convert('RGBA')
    logo.thumbnail((232,112),Image.Resampling.LANCZOS)
    banner.alpha_composite(logo,((256-logo.width)//2,(128-logo.height)//2))
    banner.convert('RGB').save(OUT/'banner.png')
    run([TOOLS/'bannertool.exe','makesmdh','-s','SuperTux 0.7.0','-l','SuperTux 0.7.0 - Nintendo 3DS','-p','SuperTux contributors','-i',OUT/'icon.png','-o',OUT/'icon.smdh','-r','regionfree','-f','visible,recordusage,nosavebackups'])
    run([TOOLS/'bannertool.exe','makebanner','-i',OUT/'banner.png','-a',OUT/'banner-audio.wav','-o',OUT/'banner.bnr'])

def build():
    metadata()
    run([TOOLS/'makerom.exe','-f','cia','-o',ROOT/(NAME+'.cia'),'-target','t','-rsf',OUT/'supertux.rsf','-elf',ROOT/'build-native/supertux070.elf','-icon',OUT/'icon.smdh','-banner',OUT/'banner.bnr','-exefslogo','-major','0','-minor','7','-micro','0'])
    print('Native CIA:',ROOT/(NAME+'.cia'))

if __name__=='__main__':build()
