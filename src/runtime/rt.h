/* Host runtime for the recompiled sh3.exe: guest memory, threads and the Win32 bridges. */
#pragma once
#include "recomp_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Guest <-> host pointers. Guest address 0 stays NULL. */
#define GPTR(va) ((va) ? (void *)XBOX_PTR(va) : NULL)
static inline uint32_t GVA(const void *p) {
    return p ? (uint32_t)((uintptr_t)p - (uintptr_t)g_xbox_mem_offset) : 0;
}
#define GSTR(va) ((const char *)GPTR(va))

/* Arguments of a bridged call: the guest pushed them, then the return address. */
#define ARG(n) MEM32(g_esp + 4 + 4 * (n))
#define ARGF(n) MEMF(g_esp + 4 + 4 * (n))

/* A bridge is a void(void) guest-callable function, found by its export name.
 * WINAPI(cname, "export", n): stdcall, the bridge pops n arguments.
 * CDECL(cname, "export"): the caller pops the arguments.
 * The body reads ARG(i) and returns EAX. */
typedef struct Bridge { const char *name; void (*fn)(void); } Bridge;
/* On ELF the bridges are a linker section and the runtime walks __start_/__stop_. PE-COFF has no such
 * symbols, so on Windows each bridge adds itself to a list from a CRT initialiser instead. */
#if defined(_WIN32)
void rt_bridge_add(const char *name, void (*fn)(void));
#define BRIDGE_REG(cname, ename) \
    static void __attribute__((constructor)) cname##_reg_(void) { rt_bridge_add(ename, cname); }
#else
#define BRIDGE_REG(cname, ename) \
    static const Bridge cname##_reg __attribute__((used, section("game_bridges"), aligned(8))) = { ename, cname }
#endif
recomp_func_t rt_bridge(const char *name);  /* a bridged import by its export name, or NULL */
#define WINAPI(cname, ename, n) \
    static uint32_t cname##_impl(void); \
    static void cname(void) { uint32_t _r = cname##_impl(); g_eax = _r; g_esp += 4 + 4 * (n); } \
    BRIDGE_REG(cname, ename); \
    static uint32_t cname##_impl(void)
#define CDECL(cname, ename) WINAPI(cname, ename, 0)

/* x87 stack, for bridges that return a double in ST(0) or take one there. */
static inline void rt_fpush(double v) { g_fp_top = (g_fp_top + 7) & 7; g_fp_stack[g_fp_top] = v; }
static inline double rt_fpop(void) { double v = g_fp_stack[g_fp_top]; g_fp_top = (g_fp_top + 1) & 7; return v; }

/* rt_core.c */
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
_Noreturn void rt_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rt_dump_thread(void);
extern int rt_frame;                 /* frames presented so far */
extern int rt_trace;                 /* SH3_TRACE=1: log every bridged call */
extern int rt_shot_request;           /* F12 (user32.c): write a PNG of the next presented frame */
/* kernel32.c: the clock the guest sees. The freecam holds it still, so the game's own time does not move
 * while the camera does; rt_clock_step_ms lets a pose advance one frame at a time. */
uint64_t rt_clock_ns(void);
void rt_clock_freeze(int on);
void rt_clock_step_ms(int ms);
void rt_clock_present(void);          /* d3d8.c, every presented frame: the freeze watchdog's heartbeat */
extern char rt_game_dir[512];        /* folder with sh3.exe and data/ */
/* The launcher's Game location (rt_core.c). rt_game_path follows the path a player gives - a folder, the game's
 * own sh3.exe, or a disc - to the folder it means, or returns a sentence saying what the path was instead;
 * rt_set_game_dir then moves the port there and remembers it in sh3-pc.ini. */
const char *rt_game_path(const char *path, char *dir, size_t n);
int rt_set_game_dir(const char *dir);  /* 1 when the port now runs from that folder */
void rt_say(const char *text);         /* the log, and a message box when there is one */
#ifdef _WIN32
/* The Options rows' pickers: the folder dialog for the install, the open-file dialogs for sh3.exe or a disc and
 * for a mod archive. */
int rt_pick_game_folder(const char *start, char *out, size_t n);
int rt_pick_game_disc(const char *start, char *out, size_t n);
int rt_pick_mod_zip(char *out, size_t n);
#endif
/* A PlayStation 2 disc or program that rt_game_path refused: rt_ps2_offer explains that this port cannot run it
 * and, on Windows, offers to open it in PCSX2. 1 when the player was answered on screen. */
extern int rt_last_ps2;
extern char rt_ps2_path[700];
int rt_ps2_offer(const char *disc);
extern int rt_hd_textures;            /* hd= in sh3.ini: mods' texture packs replace the game's textures */
extern int rt_aniso;                  /* aniso= in sh3.ini: anisotropic filtering, 1 = off */
uint32_t galloc(uint32_t size);      /* guest heap, 16-byte aligned, zeroed */
void gfree(uint32_t va);
uint32_t grealloc(uint32_t va, uint32_t size);
uint32_t gsize(uint32_t va);
uint32_t gstrdup(const char *s);
uint32_t rt_heap_used(void);   /* bytes of the guest heap handed out so far */
recomp_func_t rt_resolve(uint32_t va);
/* Calls guest code at va with args (pushed right to left). cdecl: we pop them; stdcall: the callee does. */
uint32_t guest_call(uint32_t va, int nargs, const uint32_t *args, int stdcall);
/* A host function the guest can call through a pointer (vtables, callbacks). */
uint32_t rt_host_fn(const char *name, void (*fn)(void));

/* COM objects live in guest memory: vtable pointer at +0, then the object's own fields.
 * Methods are stdcall with `this` as ARG(0); METHOD(cname, n) pops n arguments including this. */
#define METHOD(cname, n) \
    static uint32_t cname##_impl(void); \
    static void cname(void) { uint32_t _r = cname##_impl(); g_eax = _r; g_esp += 4 + 4 * (n); } \
    static uint32_t cname##_impl(void)
#define THIS ARG(0)
typedef struct { const char *name; void (*fn)(void); } ComEntry;  /* fn NULL: stops the port when called */
uint32_t com_vtable(const ComEntry *e, int n);
uint32_t com_new(uint32_t vtable, uint32_t size);  /* zeroed object of size bytes, vtable set */

/* user32.c: vsnprintf with the arguments read from the guest va_list at args. */
int rt_format(char *out, size_t n, const char *fmt, uint32_t args);

/* user32.c */
extern struct SDL_Window *rt_window;
void rt_pump_events(void);
void rt_window_open(void);

/* kernel32.c */
void rt_thread_init_main(uint32_t stack_size);
/* Runs fn(arg) on a new host thread that has its own guest registers, stack and TIB, so fn can guest_call. */
void rt_spawn(void (*fn)(void *), void *arg, uint32_t stack_size);
/* Maps a guest path (backslashes, drive letters, any case) to a host path under rt_game_dir. */
const char *rt_path(const char *guest, char *out, size_t n);
void rt_set_last_error(uint32_t e);
