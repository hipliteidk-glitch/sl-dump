# Sister Location on R36S — working context

_Last updated: 2026-10-20 (playable: Night 1 running at 60 FPS on device)_

## Links

- Raw (always current): https://raw.githubusercontent.com/hipliteidk-glitch/sl-dump/main/CONTEXT.md
- GitHub view: https://github.com/hipliteidk-glitch/sl-dump/blob/main/CONTEXT.md
- Mirrors (snapshot): https://paste.rs/CxM9r · https://dpaste.org/vBWnf

## Goal

Run **FNAF: Sister Location** natively on the R36S (dArkOSRE, aarch64) by converting
the Clickteam Fusion game with PortMaster's bundled **Chowdren** aarch64 toolchain,
then install it as a PortMaster-style port.

## Current status

| Item | State |
|---|---|
| Conversion pipeline | working (CI, ~11.5 min, `ubuntu-24.04-arm`) |
| Port installed on console | yes - `/roms/ports/Sister Location.sh` + `/roms/ports/sl/` |
| Launch | works - warning screen, menu, Night 1 |
| Startup crash | fixed (MP3 sounds transcoded to OGG) |
| White canvas | fixed at runtime by `screenfix.so` (see below) |
| Menu / in-game performance | fixed - `LIBGL_BATCH=100` (see below) |
| Mouse input | fixed at runtime by `mousefix.so` (see below) |
| Playtest | **in game** - Night 1 + HandUnit keypad, 60 FPS |


## The startup crash (diagnosed)

`Chowdren` segfaults a second or two after launch, right after OpenAL init:

```
SIGSEGV in ChowdrenAudio::Sample::Sample(BaseFile&, Media::AudioType, unsigned long)
  <- SoundMemory::SoundMemory
  <- Media::add_cache
  <- Media::init
  <- GameManager::init
```

Root cause chain:

1. Fusion "MOD" type sounds are stored as **MP3**; Chowdren's asset writer tags
   them `NATIVE_TYPE = 3` (`assets.py`).
2. The runtime only implements WAV (1) and OGG (2) decoders.
   `create_decoder()` returns `NULL` for type 3.
3. `Sample::Sample()` does `file->channels` without a null check → SIGSEGV.

Verified on-device with gdb (breakpoint on `ChowdrenAudio::create_decoder`):

```
add_cache id=0..3
create_decoder type=3 size=508370
SIGSEGV
```

`Assets.dat` audit (92 sounds): **43 x type 3 (MP3, zero-padded)**, 49 x type 1 (WAV),
0 x OGG. ffmpeg decodes the padded MP3s fine (tested on the phone).

## The startup crash fix (committed)

`ci/patch_chowdren.py` gained two patches:

- `transcode_mp3()` helper — writes the blob to a temp dir, runs
  `ffmpeg -c:a libvorbis -q:a 4`, returns OGG bytes.
- in the sound loop: if the Fusion extension is `mp3`, transcode and rename to `.ogg`
  so `add_sound()` tags it `OGG_TYPE` (2).

Workflow installs `ffmpeg` in the deps step.

Commit: `50c3257` — *sl: transcode Fusion MOD/MP3 sounds to OGG …*
Run: <https://github.com/hipliteidk-glitch/sl-dump/actions/runs/39413575606>

## The white canvas (solved)

With the audio crash gone the game ran at a steady 30-60 FPS but presented a flat
white canvas. Two independent faults were found on-device, both fixed at runtime by
`screenfix.so` (`native/screenfix.c`, installed as `/roms/ports/sl/screenfix.so`):

**1. The screen FBO is never complete.** Chowdren renders into a 1280x720
framebuffer and composites it to the window. On this GLES1/gl4es stack:

```
glGenFramebuffersEXT -> 1
glBindFramebufferEXT(1)
glFramebufferTexture2DEXT(tex=1)  -> status 0x0, err 0x500 (GL_INVALID_ENUM)
later glCheckFramebufferStatus    -> 0x8CD7 (INCOMPLETE_MISSING_ATTACHMENT)
```

All draws into it are dropped (`0x506`), and the composite samples an empty
texture. A clear-colour probe confirmed the white is content, not a clear.
The fnaf4 port never uses an FBO, which is why it works.

`screenfix.so` interposes `SDL_GL_GetProcAddress` (Chowdren resolves the FBO entry
points through it, so an `LD_PRELOAD` of `glBindFramebufferEXT` is never reached),
keeps every framebuffer binding on the window, ignores the attachment, and reports
`GL_FRAMEBUFFER_COMPLETE`. It also remaps `glViewport(0,0,1280,720)` to the
aspect-correct `(0,60,640,360)` letterbox and suppresses the present-phase clear
and composite so they cannot erase the frame.

**2. Texturing is never enabled.** Chowdren calls `glBindTexture` but never
`glEnable(GL_TEXTURE_2D)`; it expects its shader to sample textures, and gl4es does
not run that shader. Every quad therefore paints solid `glColor` white. The shim
enables `GL_TEXTURE_2D` before each draw, after which textures sample correctly
(the composite went from white to the canvas' real contents).

Ruled out by measurement: RGBA4444 uploads, NPOT forcing, texture copy,
alpha/blend hacks, red-tinting, zero-filling NULL uploads, `LIBGL_FBOFORCETEX`,
shader compile errors, and the gl4es binary itself (byte-identical to fnaf4's).

Captured proof: warning screen (`sl-fix/f0420.png`), intro scene
(`sl-fix/f0840.png`), both 640x360 inside black bars.

Launcher changes: `LD_PRELOAD="$GAMEDIR/screenfix.so:$GAMEDIR/gl4es.aarch64/libGL.so.1"`
and a backlight raise to 120/160 (a cold boot can leave the panel at 8/160, which
looks identical to a dead port).

## The menu framerate (solved)

The main menu is a screen-texture feedback effect: it draws ~940 small quads
(6 vertices each) every frame, 260k draws in 276 frames. Issued one at a time
that costs 80-120 ms per frame on this SoC (`Draw took 0.09`), i.e. 6-11 FPS
exactly while the pointer is over the menu. gl4es' batcher collapses them:

```
export LIBGL_BATCH=100
LIBGL: Trying to batch subsequent glDrawXXXX of size between 0 and 10000 vertices
```

With batching the same scene draws in 5-14 ms and holds 60 FPS. Gameplay
(Night 1, keypad) is 60 FPS with `Draw took 0.010-0.013`.

Note: the effect *is* the menu picture. Suppressing the canvas-texture draws
makes the screen black, so batching (not removal) is the fix.

## Input (solved)

The runtime in this build was linked against a gamepad-only platform layer and
has **no working mouse code at all**:

- `platform_poll_events()` handles quit / keyboard / joystick and controller
  button events; there is no `SDL_MOUSEMOTION` or `SDL_MOUSEBUTTON*` case.
  It is the only caller of `SDL_PollEvent` in the binary.
- `GameManager::on_mouse(int, bool)` has **no callers** (verified with objdump).
- `platform_get_mouse_pos(int*, int*)` is a stub: it stores 640/360, the centre
  of the 1280x720 canvas, and returns. The binary imports no mouse-position API
  at all (only `SDL_ShowCursor`).
- `update_joystick()` is an empty stub.

So the pointer is frozen at the centre of the screen and menu entries can never
be clicked. The game is otherwise mouse-driven (look around, click, the HandUnit
keypad), and `controls.ini` maps A = `mouse_left`, d-pad = `mouse_movement`.

`mousefix.so` (`native/mousefix.c`) restores the missing pieces at runtime:

1. interposes `SDL_PollEvent`, watches the real mouse events that gptokeyb2
   produces from the gamepad, and keeps a cursor in game space
   (`game = (2x, 2(y-60))`, matching the letterboxed render);
2. patches `platform_get_mouse_pos` (file offset `0x153a34`) with a 16-byte
   `ldr x16,#8; br x16; .quad` jump to its own implementation, so the engine's
   mouse object reports the real cursor;
3. calls `GameManager::on_mouse(manager, button, pressed)` (offset `0x13ade4`,
   `this` = the global `manager` at `0x6834c8`) on press/release.

Constraints learned the hard way:

- `mousefix.so` must be **first** in `LD_PRELOAD` so it sees events before any
  other interposer.
- It only patches when `/proc/self/exe` is `Chowdren`; `LD_PRELOAD` is inherited
  by `stdbuf`/`timeout`/`tee` and patching those at the game's offsets crashes
  them instantly.
- `on_mouse` is a member function - passing a button number as `this` segfaults
  inside `InputList::add`.
- Press and release must be separated by frames: `on_mouse` only adds/removes
  the button in an InputList, so a press+release in one event batch cancels out.

Verified with synthetic input: clicking the visible NEW GAME at window
(462,166) starts the game (`Setting frame: 3`); clicking an empty spot
(100,400) does not, so the coordinate mapping is right.

Launcher changes: `LD_PRELOAD="$GAMEDIR/mousefix.so:$GAMEDIR/screenfix.so:<gl4es>"`
and `LIBGL_BATCH=100`.



- Repo: `hipliteidk-glitch/sl-dump` (public), clone on the phone at `~/sl-dump`
- Workflow: `.github/workflows/sl-chowdren.yml` (workflow_dispatch, inputs `exe_url`, `title`)
- Converter patches: `ci/patch_chowdren.py`
- Artifact: `sl-chowdren-build` → `Chowdren` (~8.4 MB), `Assets.dat` (~388 MB), generated
  `events_*.cpp`, headers, `build.log`
- Source exe used for conversion:
  `https://halifax-nor-bath-controversial.trycloudflare.com/sister-location-classic-edition.exe`
  (366,838,254 B)

Converter patches already in place (all necessary for SL):

1. pre-register event groups (forward `ActivateGroup`/`DeactivateGroup` refs)
2. tolerant `ActivateGroup` / `DeactivateGroup` / `GroupActivated`
3. `RandomRange` → `randrange`, `CompareAlterableValueDouble`
4. `Set/Add/SubtractGlobalValue` Int/2/Double/3 variants + compare variants
5. MP3 → OGG transcode (new)

## Deployment / test loop

Machine layout: the Windows box reaches the phone (`ssh android`), the phone reaches the
console (`ark@<LAN>`), the console has no direct route from Windows.

Helpers (Windows, `sl-fix\`):

- `dev.ps1 -Path script.sh` — runs a bash script on the console via the phone (base64)
- `phone.ps1 -Path script.sh` — runs a bash script on the phone (base64)
- `restore_es.sh` — kill port + `systemctl start emulationstation`

Manual test procedure (on console):

```bash
sudo systemctl stop emulationstation
cd /roms/ports/sl
export LIBGL_ES=2 LIBGL_GL=21 LIBGL_FB=4
export SDL_VIDEO_GL_DRIVER=$PWD/gl4es.aarch64/libGL.so.1
export SDL_VIDEO_EGL_DRIVER=$PWD/gl4es.aarch64/libEGL.so.1
export LD_LIBRARY_PATH=$PWD/libs.aarch64
gdb -batch -ex run -ex 'bt 30' --args ./Chowdren     # or ./Chowdren
sudo systemctl start emulationstation
```

Notes learned the hard way:

- ES must be stopped before launching, otherwise DRM master is taken.
- A segfaulting run can leave SSH wedged → hard power cycle.
- `fbgrab` exists on the device; `gdb` is installed; binary is not stripped (symbols, no DWARF).
- Device glibc 2.41, so the 2.34-linked binary is fine; `ldd sl/Chowdren` resolves everything.
- `/roms` is ext4, ~2.3 GB free.

Install path:

```
/roms/ports/Sister Location.sh
/roms/ports/sl/{Chowdren,Assets.dat,controls.ini,libs.aarch64,gl4es.aarch64}
```

## Next steps

1. Hand over to the user for a real-pad test: d-pad moves the cursor, A clicks,
   B is right click, X = space, Start = enter. Watch
   `/roms/ports/sl/log.txt` for `mousefix:` lines and `Setting frame:` changes.
2. Tune if needed: `controls.ini` has `mouse_scale = 6114`, `mouse_delay = 16`
   (cursor speed/acceleration) - lower `mouse_scale` for a slower cursor.
3. Push the repo. `ci/patch_direct.py`, `native/*`, the launcher copy and this
   document are committed locally but GitHub push needs credentials (the box has
   no stored token). If CI is ever rebuilt, the source-level patch removes the
   FBO entirely; `screenfix.so` would still be needed for the `GL_TEXTURE_2D`
   fault, and `mousefix.so` for input.
4. Keep the converted assets local - do not publish them.

Console state: `/roms` is at 98% (2.1 GB free) for unrelated content; the port
itself is 397 MB (`Assets.dat` + `Chowdren` + shims). Old assets and every
diagnostic shim have been removed.

## Known warnings (not fatal yet)

- `WARNING: DeactivateGroup: unknown group pointer 7432` — group not in the current
  event list; skipped at conversion, runtime effect unknown.

