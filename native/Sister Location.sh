#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/sl"
BIN="$GAMEDIR/Chowdren"

if [ ! -x "$BIN" ]; then
    chmod +x "$BIN" 2>/dev/null
fi

cd "$GAMEDIR"
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

export DEVICE_ARCH="${DEVICE_ARCH:-aarch64}"

# No PipeWire/RTKit session on dArkOS: without these, OpenAL Soft burns the
# startup on pw.conf failures before falling back to ALSA anyway.
export ALSOFT_DRIVERS=alsa
export ALSOFT_NO_CONFIG=1

if [ -f "${controlfolder}/libgl_${CFW_NAME}.txt" ]; then
  source "${controlfolder}/libgl_${CFW_NAME}.txt"
else
  source "${controlfolder}/libgl_default.txt"
fi

# Mali-G31 silently drops textured geometry through gl4es' GLES2 backend:
# the game keeps a steady 60 FPS while every draw is discarded, which reads as
# a black screen with no error in the log. The GLES1.1 backend draws it.
export LIBGL_ES=1

export LIBGL_FB=4

# The main menu is a screen-texture feedback effect: ~940 small quads per
# frame. Drawn one call at a time that is 6-11 FPS on this SoC; gl4es' batcher
# turns it into 5-14 ms and a locked 60. This is not optional.
export LIBGL_BATCH=100

if [ -n "${LIBGL_FB:-}" ] || [ -n "${LIBGL_ES:-}" ]; then
  export SDL_VIDEO_GL_DRIVER="$GAMEDIR/gl4es.aarch64/libGL.so.1"
  export SDL_VIDEO_EGL_DRIVER="$GAMEDIR/gl4es.aarch64/libEGL.so.1"
  # screenfix.so works around two faults that together produce a flat white
  # canvas at a healthy frame rate:
  #
  #  1. Chowdren renders the game into a 1280x720 framebuffer and composites it
  #     to the window. On this GLES1/gl4es stack that framebuffer never becomes
  #     complete (0x8CD7, MISSING_ATTACHMENT), so every draw into it is dropped
  #     and the composite samples an empty texture. The shim keeps the app's
  #     framebuffer bindings on the window and draws the canvas straight into
  #     the 640x360 letterbox instead - the same end state the fnaf4 port
  #     reaches by patching the engine.
  #  2. Chowdren never calls glEnable(GL_TEXTURE_2D); it expects its shader to
  #     sample textures. gl4es does not run that shader, so the quads come out
  #     solid white. The shim enables texturing before each draw.
  #
  # It must come before gl4es so its SDL_GL_GetProcAddress interposes.
  #
  # mousefix.so restores mouse input. This Chowdren build was linked against a
  # gamepad-only platform layer: platform_poll_events() handles quit, keyboard
  # and joystick but never mouse events, GameManager::on_mouse() has no callers
  # at all, and platform_get_mouse_pos() is a stub that returns 640/360 (the
  # centre of the 1280x720 canvas) no matter what. The game itself is driven by
  # the mouse, so without this shim the pointer is frozen in the middle of the
  # screen and no menu entry can ever be clicked. mousefix watches the real SDL
  # mouse events (produced by gptokeyb from the gamepad), keeps a cursor in game
  # space, patches the stub to report it and calls on_mouse on press/release.
  # It has to be first so it sees the events before anything else.
  export LD_PRELOAD="$GAMEDIR/mousefix.so:$GAMEDIR/screenfix.so:$GAMEDIR/gl4es.aarch64/libGL.so.1"
fi

# The panel can come up at 8/160 after a cold boot, which looks exactly like a
# dead port even though the game is drawing. Raise it if it is dim.
BL=/sys/class/backlight/backlight/brightness
if [ -r "$BL" ]; then
  CUR=$(cat "$BL" 2>/dev/null)
  case "$CUR" in
    ''|*[!0-9]*) ;;
    *) [ "$CUR" -lt 120 ] && echo 120 > "$BL" 2>/dev/null ;;
  esac
fi


# Left on purpose: the banner names the gl4es backend ("Using GLES 1.1
# backend"), which is the one line that tells a future reader why this runs.
# export LIBGL_NOBANNER=1

export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

export LD_LIBRARY_PATH="$GAMEDIR/libs.aarch64:$LD_LIBRARY_PATH"

$GPTOKEYB2 "Chowdren" -c "$GAMEDIR/controls.ini" &

pm_platform_helper "$BIN"

# stdbuf is not cosmetic: the game's stdout is a pipe into tee, so glibc
# block-buffers it and the first "Setting frame"/"Could not load" lines only
# appear once 4 KB of framerate noise has accumulated. Line-buffered output is
# what makes this log trustworthy while the game is still running.
$TASKSET stdbuf -oL -eL "$BIN"

$ESUDO kill -9 $(pidof gptokeyb2) 2>/dev/null

pm_finish
