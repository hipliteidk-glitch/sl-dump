#!/usr/bin/env python3
"""Patch the bundled Chowdren converter so Sister Location's event groups
(forward references) resolve correctly.

Usage: python3 ci/patch_chowdren.py <runtime-dir>
"""
import io
import os
import sys

RUNTIME = sys.argv[1]
CONVERTER = os.path.join(RUNTIME, "chowdren", "Chowdren", "chowdren",
                         "converter.py")
SYSTEM = os.path.join(RUNTIME, "chowdren", "Chowdren", "chowdren", "writers",
                      "events", "system.py")


def patch(path, old, new, label):
    with io.open(path, encoding="utf-8", errors="surrogateescape") as f:
        text = f.read()
    count = text.count(old)
    if count != 1:
        raise SystemExit("patch %s: anchor found %d times" % (label, count))
    text = text.replace(old, new)
    with io.open(path, "w", encoding="utf-8", errors="surrogateescape",
                 newline="") as f:
        f.write(text)
    print("patched: %s" % label)


# ---------------------------------------------------------------------------
# 1. converter.py: register every group in the event list before writing any
#    event, so DeactivateGroup/ActivateGroup/GroupActivated can resolve groups
#    that are defined further down (Fusion allows these forward references).
# ---------------------------------------------------------------------------
old = """        event_items = events.items

        group_index = 1
"""
new = """        event_items = events.items

        # --- pre-register groups (fixes forward group references) ---------
        pre_container = None
        for pre_item in event_items:
            if not pre_item.conditions:
                continue
            pre_name = self.get_condition_name(pre_item.conditions[0])
            if pre_name == 'NewGroup':
                pre_loader = pre_item.conditions[0].items[0].loader
                if pre_loader.offset not in containers:
                    pre_new = EventContainer(pre_loader.name,
                        pre_loader.flags['Inactive'], pre_container, self)
                    if pre_container is not None:
                        pre_container.add_child(pre_new)
                    containers[pre_loader.offset] = pre_new
                    pre_container = pre_new
            elif pre_name == 'GroupEnd':
                if pre_container is not None:
                    pre_container = pre_container.parent

        group_index = 1
"""
patch(CONVERTER, old, new, "converter pre-register groups")

old = """            if name == 'NewGroup':
                group_loader = first_condition.items[0].loader
                new_container = EventContainer(group_loader.name,
                    group_loader.flags['Inactive'], current_container,
                    self)
                if current_container:
                    current_container.add_child(new_container)
                current_container = new_container
                containers[group_loader.offset] = current_container
"""
new = """            if name == 'NewGroup':
                group_loader = first_condition.items[0].loader
                new_container = containers.get(group_loader.offset)
                if new_container is None:
                    new_container = EventContainer(group_loader.name,
                        group_loader.flags['Inactive'], current_container,
                        self)
                    if current_container:
                        current_container.add_child(new_container)
                    containers[group_loader.offset] = new_container
                current_container = new_container
"""
patch(CONVERTER, old, new, "converter reuse pre-registered groups")

# ---------------------------------------------------------------------------
# 2. system.py: never crash on a group pointer that still isn't found; log
#    it and skip the action so the rest of the game still converts.
# ---------------------------------------------------------------------------
old = """class DeactivateGroup(ActionWriter):
    deactivated_container = None
    def write(self, writer):
        container = self.get_deactivated_container()
        writer.putln('%s = false;' % container.code_name)
"""
new = """class DeactivateGroup(ActionWriter):
    deactivated_container = None
    def write(self, writer):
        try:
            container = self.get_deactivated_container()
        except KeyError:
            print 'WARNING: DeactivateGroup: unknown group pointer %s' % (
                self.parameters[0].loader.pointer,)
            return
        writer.putln('%s = false;' % container.code_name)
"""
patch(SYSTEM, old, new, "system DeactivateGroup tolerant")

old = """    def write(self, writer):
        container = self.converter.containers[
            self.parameters[0].loader.pointer]
        writer.putlnc('if (!%s) {', container.code_name)
"""
new = """    def write(self, writer):
        try:
            container = self.converter.containers[
                self.parameters[0].loader.pointer]
        except KeyError:
            print 'WARNING: ActivateGroup: unknown group pointer %s' % (
                self.parameters[0].loader.pointer,)
            return
        writer.putlnc('if (!%s) {', container.code_name)
"""
patch(SYSTEM, old, new, "system ActivateGroup tolerant")

old = """class GroupActivated(ConditionWriter):
    def write(self, writer):
        container = self.converter.containers[
            self.parameters[0].loader.pointer]
        writer.put(self.converter.get_container_check(container))
"""
new = """class GroupActivated(ConditionWriter):
    def write(self, writer):
        try:
            container = self.converter.containers[
                self.parameters[0].loader.pointer]
        except KeyError:
            print 'WARNING: GroupActivated: unknown group pointer %s' % (
                self.parameters[0].loader.pointer,)
            writer.put('false')
            return
        writer.put(self.converter.get_container_check(container))
"""
patch(SYSTEM, old, new, "system GroupActivated tolerant")

# ---------------------------------------------------------------------------
# 3. system.py: SL uses expressions/conditions this Chowdren build never
#    mapped, so the generic fallback emits calls to non-existent functions.
#       RandomRange(min, max)          -> randrange(min, max)
#       CompareAlterableValueDouble    -> same as the int variant
# ---------------------------------------------------------------------------
old = """    'CompareAlterableValueInt' : make_comparison('alterables->values.get(%s)'),
"""
new = """    'CompareAlterableValueInt' : make_comparison('alterables->values.get(%s)'),
    'CompareAlterableValueDouble' : make_comparison('alterables->values.get(%s)'),
"""
patch(SYSTEM, old, new, "system CompareAlterableValueDouble")

old = """    'Random' : 'randrange_event',
"""
new = """    'Random' : 'randrange_event',
    'RandomRange' : 'randrange',
"""
patch(SYSTEM, old, new, "system RandomRange")

# ---------------------------------------------------------------------------
# 4. system.py: the Int/Double/'2'/'3' global-value variants (Fusion 2.5+
#    "set global value" family) are not mapped in this build either.
# ---------------------------------------------------------------------------
old = """    'SetGlobalValue' : 'global_values->set',
    'AddGlobalValue' : 'global_values->add',
    'AddGlobalValueInt' : 'global_values->add',
    'SubtractGlobalValue' : 'global_values->sub',
"""
new = """    'SetGlobalValue' : 'global_values->set',
    'SetGlobalValueInt' : 'global_values->set',
    'SetGlobalValue2' : 'global_values->set',
    'SetGlobalValueDouble' : 'global_values->set',
    'SetGlobalValue3' : 'global_values->set',
    'AddGlobalValue' : 'global_values->add',
    'AddGlobalValueInt' : 'global_values->add',
    'AddGlobalValue2' : 'global_values->add',
    'AddGlobalValueDouble' : 'global_values->add',
    'AddGlobalValue3' : 'global_values->add',
    'SubtractGlobalValue' : 'global_values->sub',
    'SubtractGlobalValueInt' : 'global_values->sub',
    'SubtractGlobalValue2' : 'global_values->sub',
    'SubtractGlobalValueDouble' : 'global_values->sub',
    'SubtractGlobalValue3' : 'global_values->sub',
"""
patch(SYSTEM, old, new, "system global value variants")

old = """    'CompareGlobalValue' : make_comparison('global_values->get(%s)'),
    'CompareGlobalValueIntEqual' : make_comparison('global_values->get(%s)'),
    'CompareGlobalValueIntNotEqual' : make_comparison('global_values->get(%s)'),
"""
new = """    'CompareGlobalValue' : make_comparison('global_values->get(%s)'),
    'CompareGlobalValueIntEqual' : make_comparison('global_values->get(%s)'),
    'CompareGlobalValueIntNotEqual' : make_comparison('global_values->get(%s)'),
    'CompareGlobalValueIntLessEqual' : make_comparison('global_values->get(%s)'),
    'CompareGlobalValueIntLess' : make_comparison('global_values->get(%s)'),
    'CompareGlobalValueIntGreaterEqual' : make_comparison('global_values->get(%s)'),
    'CompareGlobalValueIntGreater' : make_comparison('global_values->get(%s)'),
    'CompareGlobalValueDoubleEqual' : make_comparison('global_values->get(%s)'),
    'CompareGlobalValueDoubleNotEqual' : make_comparison('global_values->get(%s)'),
    'CompareGlobalValueDoubleLessEqual' : make_comparison('global_values->get(%s)'),
    'CompareGlobalValueDoubleLess' : make_comparison('global_values->get(%s)'),
    'CompareGlobalValueDoubleGreaterEqual' : make_comparison('global_values->get(%s)'),
    'CompareGlobalValueDoubleGreater' : make_comparison('global_values->get(%s)'),
"""
patch(SYSTEM, old, new, "system global value condition variants")

# ---------------------------------------------------------------------------
# 5. converter.py: Fusion stores 'MOD' sounds as MP3. The runtime decoder only
#    understands WAV/OGG and segfaults on anything else (create_decoder returns
#    NULL), so transcode every MP3 to OGG while converting.
# ---------------------------------------------------------------------------
old = """    fp = StringIO()
    wav = wave.open(fp, 'wb')
    wav.setparams((channels, 2, rate, frame_count, comptype, compname))
    wav.writeframesraw(frames)
    wav.close()
    return fp.getvalue()

class Converter(object):
"""
new = """    fp = StringIO()
    wav = wave.open(fp, 'wb')
    wav.setparams((channels, 2, rate, frame_count, comptype, compname))
    wav.writeframesraw(frames)
    wav.close()
    return fp.getvalue()

_ffmpeg_probed = []

def transcode_mp3(data):
    # ffmpeg skips the zero padding Fusion puts in front of the MP3 frames.
    import shutil
    import subprocess
    import tempfile

    # chowdren-build exports LD_LIBRARY_PATH with the runtime's own
    # toolchain/lib, which ships an old libfreetype. The system libharfbuzz
    # then fails to resolve FT_Get_Color_Glyph_Layer and ffmpeg dies with
    # exit 127 before it ever reads the input. ffmpeg needs none of the
    # runtime libraries, so give it a pristine loader path.
    env = dict(os.environ)
    for key in ('LD_LIBRARY_PATH', 'LD_PRELOAD', 'DYLD_LIBRARY_PATH'):
        env.pop(key, None)

    if not _ffmpeg_probed:
        _ffmpeg_probed.append(True)
        try:
            proc = subprocess.Popen(['ffmpeg', '-version'],
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, env=env)
            ver = proc.communicate()[0] or ''
            print 'ffmpeg: %s' % (ver.splitlines()[0] if ver else 'no output')
        except Exception as exc:
            print 'ERROR: ffmpeg is not usable: %s' % (exc,)

    folder = tempfile.mkdtemp(prefix='chowdren-mp3-')
    src = os.path.join(folder, 'in.mp3')
    dst = os.path.join(folder, 'out.ogg')
    try:
        fp = open(src, 'wb')
        fp.write(str(data))
        fp.close()
        cmd = ['ffmpeg', '-nostdin', '-v', 'error', '-y', '-i', src,
               '-c:a', 'libvorbis', '-q:a', '4', dst]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, env=env)
        _, err = proc.communicate()
        if proc.returncode != 0:
            raise RuntimeError('ffmpeg exited %s (input %d bytes): %s' % (
                proc.returncode, len(data), (err or '').strip()[:400]))
        fp = open(dst, 'rb')
        out = fp.read()
        fp.close()
    except Exception as exc:
        print 'WARNING: mp3->ogg transcode failed: %s' % (exc,)
        return None
    finally:
        shutil.rmtree(folder, ignore_errors=True)
    return out

class Converter(object):
"""
patch(CONVERTER, old, new, "converter transcode_mp3 helper")

old = """                filename = '%s.%s' % (sound.name, extension)
                data = fix_sound(sound.data, extension)
                filename, data = self.platform.get_sound(filename, data)
"""
new = """                filename = '%s.%s' % (sound.name, extension)
                data = fix_sound(sound.data, extension)
                if extension == 'mp3':
                    ogg = transcode_mp3(data)
                    if ogg is None:
                        mp3_kept.append(sound.name)
                    else:
                        data = ogg
                        filename = '%s.ogg' % (sound.name,)
                filename, data = self.platform.get_sound(filename, data)
"""
patch(CONVERTER, old, new, "converter mp3 to ogg")

# The transcode must never abort the conversion: one bad sound would otherwise
# hide the whole build (and the ffmpeg error) inside a 10 KB artifact.
old = """        sound_set = set()
        for game in self.games:
"""
new = """        sound_set = set()
        mp3_kept = []
        for game in self.games:
"""
patch(CONVERTER, old, new, "converter mp3 failure list")

old = """                self.assets.add_sound(sound.name, new_extension, data)

        # files
"""
new = """                self.assets.add_sound(sound.name, new_extension, data)

        if mp3_kept:
            print 'WARNING: %d sound(s) left as MP3, runtime cannot decode' \\
                ' them: %s' % (len(mp3_kept), ', '.join(mp3_kept[:10]))

        # files
"""
patch(CONVERTER, old, new, "converter mp3 failure summary")

print("all Chowdren patches applied")
