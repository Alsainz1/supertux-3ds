"""Apply the ARM32 Squirrel binding fix in a fresh upstream submodule checkout."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parent
submodule=ROOT.parent/'external/simplesquirrel'
patch=ROOT/'patches/simplesquirrel-binding.patch'
if not (submodule/'include/simplesquirrel/binding.hpp').is_file():
    raise SystemExit('Run git submodule update --init --recursive first.')
def check(reverse=False):
    args=['git','-C',str(submodule),'apply','--check']
    if reverse:args.append('--reverse')
    return subprocess.run(args+[str(patch)],capture_output=True).returncode==0
if check():
    subprocess.run(['git','-C',str(submodule),'apply',str(patch)],check=True)
    print('Applied required ARM32 Squirrel binding fix.')
elif check(True):print('ARM32 Squirrel binding fix already applied.')
else:raise SystemExit('Squirrel differs from the pinned 0.7.0 version; inspect the patch before building.')
