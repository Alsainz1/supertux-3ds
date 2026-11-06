# Lid-close loading guard

Sleep requests are deferred from startup and the outermost loading scope until
the first normal frame and its GPU work have completed. The previous sleep
policy is then restored; libctru requests sleep automatically if the lid is
still closed. Nested loads share the guard, and exit/error cleanup restores it.
Normal gameplay sleep is unchanged. This avoids suspending NDSP/GPU resources
in the middle of a blocking load. Hardware lid-close verification is pending;
only compilation and package checks were run, as requested.

See BUILDING.md and GITHUB.md for building and preparing a source fork. PhysicsFS
now builds from the included devkitPro-patched source, without a local static
library. The patched vendor sources and required Squirrel patch belong in Git.

# Start Game / native CIA launch repair

The CIA requests 178 MB mode on New 3DS-family hardware. A native heap allocator
caps application RAM at the 96 MiB virtual window and linear RAM at 64 MiB,
avoiding the installed older libctru allocator's overflow in expanded mode.
Older consoles retain a smaller automatic split. Title GPU pages are released
before world-map construction. Missing/stale saved map paths fall back to the
campaign's default map without deleting saves. Map-construction failures now
show their actual error instead of silently leaving Start Game at the title.
The update includes campaign levels/maps/scripts and image definitions, not
only the launcher and GPU packs. Extract the entire update and reinstall its CIA.
This repair was compiled and checked structurally; no game runtime test was
performed, as requested.

# Decoration rendering / native CIA update

Console startup/restoration explicitly presents RGB565 after a format change.
The GPU finishes before console restoration; input stays valid after SDL teardown.
This prevents colored garbage overlapping a loading screen on errors or exit.

Terrain regions and small creature/object/decal images share GPU atlas pages.
9,742 regions use 254 pages with extruded borders; original logical dimensions,
prepared pixels, collision boxes and gameplay are preserved. Pages are loaded
on demand and shared until their last region is released. Small groups use
smaller pages to limit memory overhead. Offscreen sprites avoid transform-stack
work; current animation-frame lookups avoid copying their entire action.
These changes reduce texture switching; no real-hardware 60 FPS claim is made.

Both launch formats use the original SuperTux game icon. The native CIA uses a
static SuperTux logo banner (silent), title ID 000400000F707000. Install
cias/SuperTux-0.7.0-Lean.cia using your usual CIA installer. It is a native
application, not a forwarder, and still reads sd:/3ds/supertux070/data and
sd:/3ds/supertux070/save. Extract the full update ZIP over the SD root first:
the new launcher and BOTH GPU pack files must be updated together for atlases.
Saves are not included or replaced. CIA uses 178 MB mode and 804 MHz/L2 on New
3DS-family systems, 80 MB mode on older systems; hardware validation is pending.

CIA packaging also requires makerom 0.19.0 and bannertool 1.2.3 Windows tools in
3ds/host-tools/cia. Official sources: github.com/3DSGuy/Project_CTR and
github.com/carstene1ns/3ds-bannertool. Run python 3ds/build_cia.py after building
for CIA only; python 3ds/build_engine.py --package includes both formats.

# Audio continuity / cinematic framing update

Prepared compressed music is read into RAM before playback (up to 8 MiB per
track), removing SD reads from the real-time Ogg decoder. Only current/previous
music and the star theme are retained; normal track changes release older music.
NDSP queues four 32 ms fragments instead of three, adding up to 32 ms of audio
latency for extra underrun tolerance. The optional PCM meter is off in normal
play. Hardware confirmation of the reported tiny skips is still required.
Other cinematics use a 0.38 display scale (was 0.44) and a 48-world-pixel lower
view. The opening world1 intro retains its existing framing and scale.

# Latest interface update

Loading phases (startup warmup, level/map loads, retries, sector activation and
screen setup) show only a black screen with Loading.. on the top panel and Loading.. plus "this will take some time..." below. Audio is
muted/paused while blocking work runs, then restored. There is no loading
animation or progress meter. Normal cloud/sky background drift is stopped;
camera parallax is retained. Sparkles are restored without dynamic light.

Default gameplay controls: B jumps and Y runs/actions. A confirms menus.
In Options > Controls, tap Swap jump/run on the bottom screen to exchange B/Y.
The choice is saved per profile; the other Lean effects cuts remain in place.

# SuperTux 0.7.0 - Nintendo 3DS Lean build

INSTALL
Extract SuperTux-0.7.0-3DS-Lean.zip at the SD root for a fresh installation.
For an existing FastLoad/Adventure/DualScreen installation, extract the Lean
update ZIP over the SD root. This update MUST replace the launcher, GPU/audio packs and prepared music.
Extract the complete update ZIP; keep your saves.
The launcher alone does not provide the smaller artwork. Launch
sd:/3ds/supertux070/SuperTux-0.7.0-Lean.3dsx. DSP firmware is needed for audio.
Previous DualScreen archives and the original asset-cache directory are kept
for comparison/rollback; this build has its own lean-dist/asset-cache-lean.

OFFLINE GRAPHICS
Gameplay artwork is box-filtered to half resolution on the PC, with larger
power-of-two reduction as needed to fit each prepared image within 512x512.
Small UI, fonts, power-ups and worldmap artwork keep native resolution.
Power-of-two padding is prepared offline too. Original image files, logical
dimensions, collision boxes, map scale and level coordinates are unchanged.
Dynamic fonts and uncached fallbacks upload at native resolution without a
CPU resize. Normal GPU scaling still places the textured quad on screen.
TX03 regions reference shared TX02 atlas pages. The TX02 pack preserves original logical regions and records the sample factor;
the renderer also accepts older TX01 packs for compatibility.

SIMPLIFIED EFFECTS (3DS ONLY)
Decorative snow, rain, cloud/fog particles, ghost weather, custom emitters,
cosmetic SpriteParticle trails are disabled except for restored sparkles. Emitter objects
remain script-addressable; particle zones skip visual-only collisions.
Square impact bursts are limited to 8 particles. Enemy attacks, projectiles,
explosion damage, boss health, power-ups, normal backgrounds and story logic
are retained. Screen transitions, UI and camera shake remain. Desktop effect
paths are unchanged. The normal title/story/map/level-card flow is retained.

GPU pack: 129,974,682 -> 49,936,988 bytes (about 62% smaller). This is a storage
measurement, not an FPS result. Conversion tests validate channels, swizzling,
512px limits, sample policy and original geometry. Test on actual New 2DS;
real-hardware frame times and a full campaign are not yet validated.

INTERFACE / CONTROLS
Menus stay on top with setting help below. Gameplay has the bottom reserve
ring, title/author, Tux Doll and per-level coin/secret counts.
Boss-health icons appear bottom-center on the top screen. Credits retain only
the bottom background. The title bottom panel has its background and frame.
Options use larger text; help remains below. The map continues across both
screens at native size, with double-buffered bottom output and 30 Hz rendering
while simulation/audio remain 60 Hz. Gameplay rendering is unchanged. PC fonts are retained;
FPS is hidden. D-pad/circle pad moves; A confirms/map entry; B backs out in menus;
Y runs/actions; B jumps; A confirms menus; START pauses; SELECT or tapping the reserve ring drops its item.
Saves/config: sd:/3ds/supertux070/save. Startup/fatal console is standard native.
Music is prepared offline as 32 kHz stereo Vorbis q2 (original retained if
smaller): 150,011,881 -> 82,832,899 bytes. Effects use lossless zlib-packed PCM:
23,614,720 -> 18,385,677 bytes, inflated once into the playback cache.
The star theme uses compressed native-rate PCM, inflated once into RAM to avoid
Ogg decode/SD reads during playback. Original source audio is unchanged.
Bomb fuse volume is reduced to 30%. Level-entry lens transitions finish before
the loading screen starts. These changes require real-hardware verification.

Debug unlock: in Credits press D-pad UP UP DOWN DOWN LEFT RIGHT LEFT RIGHT B A.
Confirmation appears below; it persists per profile via debug-all-levels.unlock.
It bypasses campaign level/world/secret-path locks without awarding statistics
or Tux Dolls. No unlock marker or forced-test flags are included in the package.

BUILD
python 3ds/build_engine.py --package
Requires devkitPro/devkitARM, libctru/Citro2D/Citro3D, local dependency sources,
Python Pillow/NumPy and ffmpeg. python 3ds/tests/verify_asset_cache.py validates
prepared packs. Original images are preserved; only native packs are reduced.

LIMITATIONS / LICENSES
Experimental 3DS port; dynamic lighting, blur, editor, networking and add-ons
are unavailable. Audio pitch/Doppler is absent. Boss/forest performance still
requires hardware assessment. Original source/art/audio retain their licenses:
see LICENSE.txt, data/AUTHORS, artwork metadata and packaged licenses.
