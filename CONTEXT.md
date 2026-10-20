# Sister Location on R36S — working context

_Last updated: 2026-10-20 (runtime fix verified on device)_

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
| Launch | works - warning screen + intro scene render at ~30 FPS |
| Startup crash | fixed (MP3 sounds transcoded to OGG) |
| White canvas | fixed at runtime by `screenfix.so` (see below) |
| Controls | `controls.ini` maps A = left click, d-pad/left stick = mouse |
| Playtest | in progress - advanced past the warning screen to frame 1 |


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

## Repo / CI

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

1. Playtest past the intro: the warning screen waits for a click; A advances it.
   Watch `/roms/ports/sl/log.txt` for `Setting frame:` changes and crashes.
2. Clean up the console: `/roms` is at 98% (1.9 GB free). Remove
   `Assets.dat.old-mp3` (219 MB), `Chowdren.old`, the diagnostic shims
   (`capshim/clearprobe/drawshim/enabletex/fbprobe/inject/nulltex/pfb/potshim/
   redtint/rgba4444/stateshim/texcheck/texshim .so`), `/roms/cap/*.ppm`, and the
   `/roms/*.c|*.py|*.log` scratch files. Keep `screenfix.so` + `screenfix.c`.
3. Push the repo. `ci/patch_direct.py` + the workflow step are committed locally
   but GitHub push needs credentials (the box has no stored token). If CI is ever
   rebuilt, the source-level patch removes the FBO entirely; `screenfix.so` would
   still be needed for the `GL_TEXTURE_2D` fault.
4. Keep the converted assets local - do not publish them.

## Known warnings (not fatal yet)

- `WARNING: DeactivateGroup: unknown group pointer 7432` — group not in the current
  event list; skipped at conversion, runtime effect unknown.

