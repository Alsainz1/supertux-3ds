# Building the Nintendo 3DS port on Windows

This guide is for the native Windows / PowerShell build that this port uses.
The normal desktop CMake build and the historical `3ds/build.py` are different.
Run every Python command below from the SuperTux repository root.

If this local checkout has been prepared for GitHub, the installed host tools
are archived at `not for github/3ds/host-tools`. Restore that folder to
`3ds/host-tools` before building here, or follow the fresh clone setup below.
The scripts can regenerate build outputs and asset caches.


## Build the already configured project on this PC

```powershell
Set-Location C:\Users\Alex\Downloads\supertux-0.7.0
python 3ds/build_engine.py --package
```

That compiles the game, prepares the smaller 3DS assets on the PC, creates the
original game icon/banner, and builds both the 3DSX and unencrypted native CIA.
The first asset preparation takes longer; unchanged prepared assets are reused.
Original images/audio in `data/` are retained.

Outputs:

- `3ds/SuperTux-0.7.0-Lean.3dsx` (also staged under `lean-dist/3ds/supertux070/`)
- `3ds/SuperTux-0.7.0-Lean.cia` (also staged under `lean-dist/cias/`)
- `3ds/SuperTux-0.7.0-3DS-Lean.zip` (fresh SD installation)
- `3ds/SuperTux-0.7.0-3DS-Lean-update.zip` (existing installation)

For a source-only compile without packaging: `python 3ds/build_engine.py`.
To repackage an already compiled ELF: `python 3ds/build_engine.py --package-only`.
For CIA only after compiling: `python 3ds/build_cia.py`.
Packaging refreshes the standalone 3DSX beside the ZIPs as well as `lean-dist/`.
A plain compile produces the ELF only; run packaging to regenerate launchers.

## Set up a fresh Windows machine / fresh clone

1. Install Git, Python 3, and devkitPro's Windows installer with 3DS development
   tools. The checked-in native toolchain currently expects `C:/devkitPro`.
   In the devkitPro MSYS2 shell, install/update the compiler and portlibs:

   ```sh
   pacman -Syu
   pacman -S --needed 3ds-dev 3ds-libpng 3ds-zlib 3ds-libogg 3ds-libvorbisidec
   ```

2. Install ffmpeg and put the directory containing `ffmpeg.exe` on Windows PATH.
   Check `ffmpeg -version` in a new PowerShell window.

3. Clone the fork including submodules. Use the port branch based on v0.7.0,
   not a checkout of an unrelated newer upstream revision:

   ```powershell
   git clone --recurse-submodules https://github.com/Alsainz1/supertux-3ds.git SuperTux-3DS
   Set-Location SuperTux-3DS
   git submodule update --init --recursive
   ```

4. Install Python host tools and apply the required Squirrel ARM32 patch:

   ```powershell
   python -m pip install pillow numpy
   python -m pip install --target 3ds/host-tools cmake ninja
   python 3ds/prepare_dependencies.py
   ```

   This patch is idempotent. Do not replace the patched SDL or PhysicsFS sources
   inside `3ds/vendor/` with stock copies. PhysicsFS is now built from its
   included source; the old local `3ds/deps/lib/libphysfs.a` is not required.

5. Download makerom's Windows x86_64 release and bannertool's Windows release
   from their official projects. Extract the executables to exactly:

   ```text
   3ds/host-tools/cia/makerom.exe
   3ds/host-tools/cia/bannertool.exe
   ```

   The build used makerom 0.19.0 and bannertool 1.2.3. Keep their license files
   alongside them. Download tools from:
   - https://github.com/3DSGuy/Project_CTR/releases
   - https://github.com/carstene1ns/3ds-bannertool/releases
   - devkitPro setup: https://devkitpro.org/wiki/Getting_Started
   - ffmpeg: https://ffmpeg.org/download.html

6. Build:

   ```powershell
   python 3ds/build_engine.py --package
   ```

Build/asset/package checks (these do not launch the game):

```powershell
python 3ds/tests/verify_asset_cache.py
python 3ds/tests/verify_cia.py
```

## Install your build

Extract the complete update ZIP over the SD root, replacing its files. For the
HOME Menu version, install `cias/SuperTux-0.7.0-Lean.cia` with your usual CIA
installer. The CIA still needs `sd:/3ds/supertux070/data`; it does not embed all
assets. CIA and 3DSX share saves in `sd:/3ds/supertux070/save`. DSP firmware is
required for audio; it is not distributed with the port.

If source code alone changed, replacing/reinstalling the executable can be
enough. If preparation scripts or game data changed, use the complete update.
Keep saves; the release ZIPs do not contain save data or debug unlock markers.
