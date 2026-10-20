"""Render Chowdren straight to the screen instead of into its screen FBO.

Why
---
On this device (R36S / Mali-G31 behind gl4es' GLES1 backend) the runtime creates
a WINDOW_WIDTH x WINDOW_HEIGHT framebuffer in Framebuffer::init() and renders the
whole game into it, then composites it to the window. That FBO never becomes
complete here:

    glCheckFramebufferStatus(GL_FRAMEBUFFER) -> 0x8CD7
    GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT

so every draw into it is discarded (the GL error stream carries 0x506,
GL_INVALID_FRAMEBUFFER_OPERATION) and the final composite samples an empty
texture. The game itself runs fine - 30-60 FPS, textures uploaded, thousands of
quads submitted - but the screen is a uniform white canvas.

Evidence gathered on the console: with a probe on glDrawArrays, draws 1-3 go to
fbo=1 at viewport 1280x720 and are reported incomplete; draw 4 goes to fbo=0 and
reads back as pure white. The working fnaf4 build on the same device renders
every draw to fbo=0 and never touches an FBO.

Fix
---
The fnaf4 port solved this by disabling the screen FBO and drawing directly into
the scaled viewport (its platform.cpp carries `// [direct-to-screen]` markers).
This applies the same four edits to the stock Chowdren source:

  1. screen_fbo.init(...)   commented out
  2. screen_fbo.bind()      commented out
  3. screen_fbo.unbind()    commented out
  4. the composite draw_tex() of screen_fbo.get_tex() replaced by nothing,
     with the view set up so the game draws into the aspect-correct viewport

Every edit is asserted: if the upstream source does not match exactly, this
script fails immediately (before the ~11 minute conversion) instead of silently
producing another white-screen build.
"""
import re
import sys
from pathlib import Path

MARK = "// [direct-to-screen]"


def find_platforms(root: Path):
    return [p for p in root.rglob("platform.cpp") if p.parent.name == "desktop"]


def patch_one(path: Path) -> bool:
    src = path.read_text(encoding="utf-8", errors="surrogateescape")
    if MARK in src:
        print("  already patched: %s" % path)
        return True

    s = src

    # --- 1/2/3: the three screen_fbo calls -------------------------------
    for call in ("screen_fbo.init(WINDOW_WIDTH, WINDOW_HEIGHT);",
                 "screen_fbo.bind();",
                 "screen_fbo.unbind();"):
        old = "    " + call
        if old not in s:
            print("  MISSING expected line: %r" % old)
            return False
        s = s.replace(old, "    " + MARK + " " + call, 1)

    # --- 4: the composite pass that samples the (empty) FBO texture ------
    block = re.compile(r"^    Render::disable_blend\(\);\n[\s\S]*?^    Render::enable_blend\(\);\n",
                       re.MULTILINE)
    target = None
    for m in block.finditer(s):
        if "screen_fbo.get_tex()" in m.group(0):
            target = m
            break
    if target is None:
        print("  MISSING composite pass (disable_blend .. draw_tex .. enable_blend)")
        return False
    s = (s[:target.start()]
         + "    " + MARK + " composite from screen_fbo disabled\n"
         + s[target.end():])

    # --- viewport: draw the game at WINDOW_WIDTH/HEIGHT into the scaled
    #     rectangle the resize code already computed --------------------
    old_view = ("    // resize the window contents if necessary (fullscreen mode)\n"
                "    Render::set_view(0, 0, window_width, window_height);\n")
    new_view = ("    // resize the window contents if necessary (fullscreen mode)\n"
                "    " + MARK + " draw into the scaled viewport, not an FBO\n"
                "    Render::render_vp_x = draw_x_off;\n"
                "    Render::render_vp_y = draw_y_off;\n"
                "    Render::render_vp_w = draw_x_size;\n"
                "    Render::render_vp_h = draw_y_size;\n"
                "    Render::set_view(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);\n")
    if old_view not in s:
        print("  MISSING view setup after screen_fbo.unbind()")
        return False
    s = s.replace(old_view, new_view, 1)

    if "render_vp_x" not in src:
        print("  MISSING Render::render_vp_x in this source; direct viewport scaling "
              "is not available")
        return False

    path.write_text(s, encoding="utf-8", errors="surrogateescape", newline="")
    print("  patched: %s" % path)
    return True


def main():
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "rt")
    if not root.is_dir():
        print("no such directory: %s" % root)
        return 1

    targets = find_platforms(root)
    if not targets:
        print("no desktop/platform.cpp found under %s" % root)
        return 1

    ok = True
    for p in targets:
        print("patching %s" % p)
        ok = patch_one(p) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
