# Putting this port in a SuperTux fork

Do not upload the entire working folder: it contains many old builds, duplicate
SD packages, caches and local tools. Keep a normal fork of upstream and commit
source changes. Publish executable/download ZIPs as GitHub Release assets.

## Start the fork from the version this port uses

The destination is [Alsainz1/supertux-3ds](https://github.com/Alsainz1/supertux-3ds).
If the repository still has no branches, clone it and fetch the upstream release:

```powershell
git clone https://github.com/Alsainz1/supertux-3ds.git SuperTux-3DS
Set-Location SuperTux-3DS
git remote add upstream https://github.com/SuperTux/supertux.git
git fetch upstream tag v0.7.0
git switch -c nintendo-3ds v0.7.0
git submodule update --init --recursive
```

Copy the port changes into that checkout. Keep its Git metadata and submodule
checkouts. The local `not for github` folder holds the archived builds,
generated files and local tools. The root ignore file excludes it.

## Include

| Path | What belongs in the fork |
| --- | --- |
| `src/` | All modified engine source; the 3DS changes are not confined to `3ds/`. |
| `data/` | Modified original assets, levels, scripts and fonts, with their credits/licenses. |
| `3ds/engine/` | Native renderer, audio, input/HUD/loading/memory support and CMake/toolchain source. |
| `3ds/source/` | Platform/console support also used by the full engine. |
| `3ds/vendor/` | Required patched third-party SOURCE and license files. This is intentionally no longer ignored. |
| `3ds/cia/supertux.rsf` | CIA settings. Its icon/banner pictures, audio and binary outputs are regenerated. |
| `3ds/*.py`, `3ds/*.json` | Build/preparation scripts, dependency versions and provenance. |
| `3ds/patches/` | Required Squirrel submodule patch and PhysicsFS platform patch. |
| `3ds/tests/` | Source checks and reference fixtures; generated logs/executables are ignored. |
| `3ds/*.md`, `3ds/.gitignore` | Port documentation and generated-file exclusions. |
| Other modified upstream files | Any root/CMake/config changes actually needed by the port. Keep upstream licenses and `.gitmodules`. |

The submodule `external/simplesquirrel` has a local ARM32 binding fix. Merely
copying its modified files into a fork does not commit that change to the parent
repository's gitlink. The fix is stored in `3ds/patches/simplesquirrel-binding.patch`
and reapplied by `python 3ds/prepare_dependencies.py` after submodule checkout.
Keep the upstream submodule revisions; do not stage a nonexistent local submodule
commit. Other `external/` dependencies already belong to upstream; preserve them.

## Exclude

- `3ds/build*`, `3ds/*-dist`, extracted release folders and all old releases.
- `3ds/asset-cache*`, `3ds/audio-lean`, `3ds/change-baselines`, lab output.
- `3ds/host-tools`, `3ds/deps`, local compiler/SDK files, caches and Python bytecode.
- Generated `.3dsx`, `.cia`, `.cxi`, `.elf`, `.smdh`, `.bnr`, `.exe`, `.o`, `.a`,
  archives and logs. The tiny checked-in tex3ds test fixture `.bin` is intentional.
- SD saves, personal settings, profiling flags, debug-unlock files and emulator
  NAND/firmware/DSP dumps. None belong in the source repository or release ZIPs.

The supplied `3ds/.gitignore` handles the port's outputs. It does not remove files
already committed; inspect `git status` and the staged file list before pushing.

## Commit and push

After copying the source directories into the new clone:

```powershell
git add src data 3ds
git status --short
git diff --cached --stat
git commit -m "Port SuperTux 0.7.0 to Nintendo 3DS"
git push -u origin nintendo-3ds
```

Also stage any genuinely modified upstream root files individually. Do not use
`git add -f` on caches/builds. A dirty Squirrel submodule after applying the patch
is expected; commit the patch file in the parent repo, not generated build data.
You can make `nintendo-3ds` the default branch of your fork for easier discovery.

In a GitHub Release, attach the full SD ZIP and update ZIP (they include the CIA),
and optionally the standalone CIA/3DSX. Explain that a standalone CIA still needs
the SD assets. Link the release to the exact source commit you built. Preserve
upstream code, artwork/audio and dependency licenses and attribution; do not
relicense the port as entirely your own work.

Use [BUILDING.md](BUILDING.md) for a new-clone build. No GitHub upload or push has
been performed by the assistant.
