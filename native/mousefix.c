/* mousefix.c - restore mouse input in the Cyanide/Chowdren desktop runtime.
 *
 * The converted binary was built for a gamepad-only platform layer:
 *
 *   platform_poll_events()  handles quit/keyboard/joystick, never mouse events
 *   GameManager::on_mouse() has no callers at all
 *   platform_get_mouse_pos() is a stub that stores 640/360, the centre of the
 *                           1280x720 canvas, on every call
 *
 * The game itself is mouse-driven (menus, looking around, clicking), so with
 * that build it can only ever click the exact centre of the screen. This shim
 * puts the missing pieces back:
 *
 *   - watches the real SDL mouse events (which gptokeyb produces from the
 *     gamepad) and keeps a cursor in game space,
 *   - patches platform_get_mouse_pos to return that cursor,
 *   - calls GameManager::on_mouse on press/release.
 *
 * Coordinates: the screen shim letterboxes the 1280x720 canvas into
 * (0,60,640,360), so window (x,y) maps to game (2x, 2(y-60)).
 *
 * Offsets are for the shipped binary (sha256 cd21020d...750a). The patch
 * verifies the original instruction before overwriting anything.
 *
 * LD_PRELOAD after screenfix.so, before gl4es.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>

static void dbg(const char *msg, uintptr_t value)
{
    char buf[160];
    int n = snprintf(buf, sizeof(buf), "mousefix: %s %p\n", msg, (void *)value);
    if (n > 0) write(2, buf, (size_t)n);
}

typedef unsigned int Uint32;
typedef unsigned char Uint8;

#define OFF_GET_MOUSE_POS 0x153a34u
#define OFF_ON_MOUSE      0x13ade4u
#define OFF_MANAGER       0x6834c8u

#define SDL_MOUSEMOTION    0x400
#define SDL_MOUSEBUTTONDOWN 0x401
#define SDL_MOUSEBUTTONUP  0x402

static const uint32_t PROLOGUE = 0xd10043ffu;   /* sub sp, sp, #0x10 */

static volatile int vx = 640;
static volatile int vy = 360;
static int patched = 0;
static long n_motion = 0, n_click = 0;

static void (*p_on_mouse)(void *, int, int) = NULL;
static void *p_manager = NULL;

/* Replaces platform_get_mouse_pos. */
void mf_get_mouse_pos(int *x, int *y)
{
    if (x) *x = vx;
    if (y) *y = vy;
}

static int find_base(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    if (info->dlpi_name == NULL || info->dlpi_name[0] == '\0') {
        *(uintptr_t *)data = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

static void patch_jump(void *addr, void *target, const char *name)
{
    uint32_t *p = (uint32_t *)addr;
    if (p[0] != PROLOGUE) {
        fprintf(stderr, "mousefix: %s has unexpected prologue %08x, not patching\n",
                name, p[0]);
        return;
    }

    uintptr_t page = (uintptr_t)addr & ~(uintptr_t)0xFFF;
    if (mprotect((void *)page, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        fprintf(stderr, "mousefix: mprotect failed for %s\n", name);
        return;
    }

    /* ldr x16, #8 ; br x16 ; .quad target */
    uint32_t code[2] = { 0x58000050u, 0xd61f0200u };
    memcpy(addr, code, sizeof(code));
    memcpy((char *)addr + 8, &target, sizeof(target));
    __builtin___clear_cache((char *)addr, (char *)addr + 16);

    mprotect((void *)page, 0x1000, PROT_READ | PROT_EXEC);
    fprintf(stderr, "mousefix: patched %s at %p\n", name, addr);
    patched++;
}

__attribute__((constructor))
static void mousefix_init(void)
{
    /* LD_PRELOAD is inherited by stdbuf/timeout and every helper the launcher
     * runs, and each of them would be patched at the game's offsets. Only act
     * inside the runtime itself. */
    char exe[256];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return;
    exe[n] = '\0';
    const char *base_name = strrchr(exe, '/');
    base_name = base_name ? base_name + 1 : exe;
    if (strcmp(base_name, "Chowdren") != 0) return;

    uintptr_t base = 0;
    dl_iterate_phdr(find_base, &base);
    if (base == 0) {
        dbg("could not find main executable base", 0);
        return;
    }

    uint32_t *p = (uint32_t *)(base + OFF_GET_MOUSE_POS);
    if (*p != PROLOGUE) {
        dbg("unexpected prologue, skipping patch", (uintptr_t)*p);
        return;
    }

    patch_jump((void *)p, (void *)mf_get_mouse_pos, "platform_get_mouse_pos");
    p_on_mouse = (void (*)(void *, int, int))(base + OFF_ON_MOUSE);
    p_manager = (void *)(base + OFF_MANAGER);
    dbg("manager", (uintptr_t)p_manager);
    dbg("ready, patches", (uintptr_t)patched);
}

static void set_pos(int mx, int my)
{
    int x = mx * 2;
    int y = (my - 60) * 2;
    if (y < 0) y = 0;
    if (y > 720) y = 720;
    if (x < 0) x = 0;
    if (x > 1280) x = 1280;
    vx = x;
    vy = y;
}

int SDL_PollEvent(void *event)
{
    static int (*real)(void *) = NULL;
    if (!real) real = (int (*)(void *))dlsym(RTLD_NEXT, "SDL_PollEvent");

    int r = real ? real(event) : 0;
    if (!r || !event) return r;

    char *e = (char *)event;
    Uint32 type = *(Uint32 *)(e + 0);

    if (type == SDL_MOUSEMOTION) {
        set_pos(*(int *)(e + 20), *(int *)(e + 24));
        n_motion++;
    } else if (type == SDL_MOUSEBUTTONDOWN || type == SDL_MOUSEBUTTONUP) {
        set_pos(*(int *)(e + 20), *(int *)(e + 24));
        int button = *(Uint8 *)(e + 16);
        int down = (type == SDL_MOUSEBUTTONDOWN);
        if (p_on_mouse && p_manager) p_on_mouse(p_manager, button, down);
        n_click++;
        if (n_click <= 12)
            fprintf(stderr, "mousefix: click button=%d down=%d at %d,%d (game %d,%d)\n",
                    button, down, *(int *)(e + 20), *(int *)(e + 24), vx, vy);
    }
    return r;
}

__attribute__((destructor))
static void mousefix_report(void)
{
    fprintf(stderr, "mousefix: TOTAL patched=%d motion=%ld clicks=%ld\n",
            patched, n_motion, n_click);
}
