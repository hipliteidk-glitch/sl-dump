# Sister Location on R36S — working context

_Last updated: 2026-10-20 (during build run 39413575606)_

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
| Port installed on console | yes — `/roms/ports/Sister Location.sh` + `/roms/ports/sl/` |
| First launch | **segfaults at startup** (audio, see below) |
| Fix | converter now transcodes MP3→OGG; build running |
| Console | online, ES running |


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

## The fix (committed, build in progress)

`ci/patch_chowdren.py` gained two patches:

- `transcode_mp3()` helper — writes the blob to a temp dir, runs
  `ffmpeg -c:a libvorbis -q:a 4`, returns OGG bytes.
- in the sound loop: if the Fusion extension is `mp3`, transcode and rename to `.ogg`
  so `add_sound()` tags it `OGG_TYPE` (2).

Workflow installs `ffmpeg` in the deps step.

Commit: `50c3257` — *sl: transcode Fusion MOD/MP3 sounds to OGG …*
Run: <https://github.com/hipliteidk-glitch/sl-dump/actions/runs/39413575606>

## Repo / CI

- Repo: `hipliteidk-glitch/sl-dump` (public), clone on the phone at `~/sl-dump`
- Workflow: `.github/workflows/sl-chowdren.yml` (workflow_dispatch, inputs `exe_url`, `title`)
- Converter patches: `ci/patch_chowdren.py`
- Artifact: `sl-chowdren-build` → `Chowdren` (~8.4 MB), `Assets.dat` (~220 MB), generated
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

1. Wait for run 39413575606 → download artifact to Windows.
2. Push `Chowdren` + `Assets.dat` to the console (Windows → phone → console; phone needs
   ~250 MB free, it is tight — delete old zips first).
3. Re-run under gdb / launch from the Ports menu; watch `/roms/ports/sl/log.txt`.
4. Expect possible follow-on runtime issues (events, shaders, group pointers);
   rebuild via `gh workflow run sl-chowdren.yml` after patching the converter.
5. Once it runs: tune `controls.ini` (gptokeyb2), keep the port local (do not publish
   converted game assets).

## Known warnings (not fatal yet)

- `WARNING: DeactivateGroup: unknown group pointer 7432` — group not in the current
  event list; skipped at conversion, runtime effect unknown.
