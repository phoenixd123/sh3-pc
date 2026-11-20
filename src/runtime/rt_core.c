/* Guest memory, the exe loader, the guest heap and call dispatch. */
#ifdef _WIN32
#include "win_platform.h"  /* before the CRT-only headers, see win_platform.h */
#endif
#include "rt.h"
#include "../mods/mods.h"
#include "fs.h"  /* stat_t, S_ISDIR: is this folder the game? */
#include <stdarg.h>
#include <ctype.h>  /* tolower: ISO 9660 names are upper case */
#ifndef _WIN32
#include <pthread.h>
#include <strings.h>  /* strcasecmp, for the paths a player drags in */
#include <unistd.h>
#endif
#include "host.h"
#ifdef __SWITCH__
#include <switch.h>
#include <netinet/in.h>  /* __nxlink_host */
#endif
#ifdef _WIN32
#include <shellapi.h>  /* ShellExecute: the PCSX2 page */
#endif

/* ---- register file and runtime globals the generated code links against ---- */
#ifndef RECOMP_MMX_DEFINED  /* recomp_types.h only declares it for generated code */
#define RECOMP_MMX_DEFINED
typedef union RecompMmx { int8_t b[8]; uint8_t ub[8]; int16_t w[4]; uint16_t uw[4];
                          int32_t d[2]; uint32_t ud[2]; uint64_t q; } RecompMmx;
#endif
ptrdiff_t g_xbox_mem_offset;
uint32_t g_xbox_code_lo, g_xbox_code_hi;
int g_force_return;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
RECOMP_TLS uint32_t g_fs_base, g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top, g_fp_cmp, g_df;
RECOMP_TLS uint16_t g_fp_control_word = 0x027F, g_fp_cc;
RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3, g_xmm4, g_xmm5, g_xmm6, g_xmm7;
RECOMP_TLS RecompMmx g_mm0, g_mm1, g_mm2, g_mm3, g_mm4, g_mm5, g_mm6, g_mm7;
RECOMP_TLS volatile uint32_t g_icall_trace[ICALL_TRACE_SIZE];  /* per thread: see recomp_types.h */
RECOMP_TLS volatile uint32_t g_icall_trace_idx;
RECOMP_TLS volatile uint64_t g_icall_count;

int rt_trace;
char rt_game_dir[512] = ".";
int rt_shot_request;  /* F12 (user32.c rt_pump_events) asked for a screenshot at the next frame */
int rt_hd_textures = 1;  /* hd=0 in sh3.ini turns mod texture packs off */
int rt_aniso = 8;        /* aniso=1 in sh3.ini turns anisotropic filtering off */
int rt_ws = 1;           /* widescreen=0 in sh3.ini keeps the 4:3 framing */
int rt_filter;           /* filter=1 CRT, 2 VHS in sh3.ini; 0 (the default) blits as before */

/* log=1 in sh3.ini also writes sh3.log next to the game. On the Switch it is off unless asked for (launched
 * by nxlink, or log=1): writing it costs an SD card write per line. On PC the console always gets the output,
 * and log=1 adds the file, which is what the README has always promised. */
static int log_on;

void rt_log(const char *fmt, ...) {
#ifdef __SWITCH__
    if (!log_on) return;
#endif
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (log_on) {
        static FILE *log;  /* sh3.log in the game folder (the Switch has no console at all) */
        if (!log) log = fopen("sh3.log", "w");
        if (log) {
            va_start(ap, fmt);
            vfprintf(log, fmt, ap);
            va_end(ap);
            fputc('\n', log);
            fflush(log);
        }
    }
}

static void dump_icall_trace(FILE *f) {
    fprintf(f, "last indirect calls:");
    for (int i = 0; i < ICALL_TRACE_SIZE; i++)
        fprintf(f, " %08X", g_icall_trace[(g_icall_trace_idx + i) & (ICALL_TRACE_SIZE - 1)]);
    fprintf(f, "\nregs: eax=%08X ecx=%08X edx=%08X ebx=%08X esi=%08X edi=%08X esp=%08X\n",
            g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_esp);
}

/* Guest return addresses on the guest stack: the nearest thing to a guest backtrace. */
static void dump_guest_stack(FILE *f) {
    fprintf(f, "guest stack code addresses:");
    for (uint32_t a = g_esp, n = 0; n < 24 && a < g_esp + 0x2000; a += 4) {
        uint32_t v = MEM32(a);
        if (v >= g_xbox_code_lo && v < g_xbox_code_hi) { fprintf(f, " %08X", v); n++; }
    }
    fputc('\n', f);
}

/* Where this thread is, guest and host side: for hangs (SH3_DUMP_AFTER) and fatal errors. */
void rt_dump_thread(void) {
    dump_icall_trace(stderr);
    dump_guest_stack(stderr);
    host_backtrace(2);
}

/* Fatal errors go to stderr and to sh3-crash.log in the game folder (the Switch has no console). */
_Noreturn void rt_fatal(const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    FILE *log = fopen("sh3-crash.log", "a");  /* appends to what an exception handler wrote first */
    FILE *outs[2] = {stderr, log};
    for (int i = 0; i < 2 && outs[i]; i++) {
        fprintf(outs[i], "FATAL: %s\n", msg);
        dump_icall_trace(outs[i]);
        dump_guest_stack(outs[i]);
        fflush(outs[i]);
        host_backtrace(fileno(outs[i]));
    }
    if (log) fclose(log);
    _exit(1);
}

static void on_crash(uintptr_t a) {
    uintptr_t base = (uintptr_t)g_xbox_mem_offset;
    if (a >= base && a - base < 0x100000000ull) rt_fatal("crash at guest address %08X", (uint32_t)(a - base));
    rt_fatal("crash at host address %p", (void *)a);
}

/* ---- guest address space: one 4 GB arena, guest VA = offset into it ---- */
#define GUEST_LOW_GUARD 0x10000u     /* null pointer reads fault */
#define HEAP_LO 0x10000000u
#define HEAP_HI 0xF0000000u
#define HOSTFN_BASE 0xFE000000u      /* synthetic call targets, never dereferenced */
#define HOSTFN_STRIDE 16u

/* Only the exe image and the heap are backed; anything else the guest touches faults, on Linux
 * as on the Switch. */
static void arena_init(void) { g_xbox_mem_offset = (ptrdiff_t)host_arena_reserve(); }
static uint32_t image_lo, image_hi;  /* the loaded exe */

/* ---- guest heap ----
 * ponytail: power-of-two size classes with per-class free lists, never returned to the
 * system. Wastes up to half of each block; swap for a real allocator if Switch RAM runs out. */
typedef struct { uint32_t cls, req, magic, pad; } BlockHdr;
#define BLOCK_MAGIC 0x53483248u
static uint32_t heap_top = HEAP_LO, heap_committed = HEAP_LO, free_head[33];
#define HEAP_COMMIT_STEP (8u << 20)
static pthread_mutex_t heap_mx = PTHREAD_MUTEX_INITIALIZER;

uint32_t galloc(uint32_t size) {
    uint32_t cls = 5;
    while ((1ull << cls) < (uint64_t)size + sizeof(BlockHdr)) cls++;
    pthread_mutex_lock(&heap_mx);
    uint32_t blk = free_head[cls];
    if (blk) free_head[cls] = ((BlockHdr *)GPTR(blk))->pad;  /* the free list links through the header */
    else {
        if ((uint64_t)heap_top + (1ull << cls) > HEAP_HI) rt_fatal("guest heap exhausted (%u bytes)", size);
        blk = heap_top;
        heap_top += 1u << cls;
        if (heap_top > heap_committed) {
            uint32_t to = (heap_top + HEAP_COMMIT_STEP - 1) & ~(HEAP_COMMIT_STEP - 1);
            host_arena_commit(heap_committed, to - heap_committed);
            heap_committed = to;
        }
    }
    pthread_mutex_unlock(&heap_mx);
    BlockHdr *h = GPTR(blk);
    *h = (BlockHdr){cls, size, BLOCK_MAGIC, 0};
    memset(h + 1, 0, size);
    return blk + sizeof(BlockHdr);
}

static BlockHdr *hdr(uint32_t va) {
    BlockHdr *h = GPTR(va - sizeof(BlockHdr));
    if (va < HEAP_LO || h->magic != BLOCK_MAGIC) rt_fatal("bad guest heap pointer %08X", va);
    return h;
}

/* Freed blocks wait in a quarantine before they are reused, and their contents stay intact: the game
 * (written for Windows' heap, which does not hand a block back at once) still reads a block just after
 * freeing it, and an immediate LIFO reuse turned that into a crash at some resolutions (the screen-sized
 * image surface and the Konami logo texture share a size class at 1280x720). */
#define QUARANTINE_BYTES (64u << 20)
#define QUARANTINE_SLOTS 4096
static uint32_t quarantine[QUARANTINE_SLOTS], q_head, q_count, q_bytes;

static void release_block(uint32_t blk) {
    BlockHdr *h = GPTR(blk);
    h->pad = free_head[h->cls];
    free_head[h->cls] = blk;
}

void gfree(uint32_t va) {
    if (!va) return;
    BlockHdr *h = hdr(va);
    pthread_mutex_lock(&heap_mx);
    h->magic = 0;
    if (q_count == QUARANTINE_SLOTS || q_bytes + (1u << h->cls) > QUARANTINE_BYTES) {
        while (q_count && (q_count == QUARANTINE_SLOTS || q_bytes + (1u << h->cls) > QUARANTINE_BYTES)) {
            uint32_t old = quarantine[q_head];
            q_head = (q_head + 1) % QUARANTINE_SLOTS;
            q_count--;
            q_bytes -= 1u << ((BlockHdr *)GPTR(old))->cls;
            release_block(old);
        }
    }
    if (q_bytes + (1u << h->cls) > QUARANTINE_BYTES) release_block(va - sizeof(BlockHdr));  /* bigger than the quarantine */
    else {
        quarantine[(q_head + q_count++) % QUARANTINE_SLOTS] = va - sizeof(BlockHdr);
        q_bytes += 1u << h->cls;
    }
    pthread_mutex_unlock(&heap_mx);
}

uint32_t gsize(uint32_t va) { return hdr(va)->req; }
uint32_t rt_heap_used(void) { return heap_top - HEAP_LO; }

uint32_t grealloc(uint32_t va, uint32_t size) {
    if (!va) return galloc(size);
    BlockHdr *h = hdr(va);
    if ((uint64_t)size + sizeof(BlockHdr) <= (1ull << h->cls)) {
        if (size > h->req) memset((uint8_t *)GPTR(va) + h->req, 0, size - h->req);
        h->req = size;
        return va;
    }
    uint32_t n = galloc(size);
    memcpy(GPTR(n), GPTR(va), h->req);
    gfree(va);
    return n;
}

uint32_t gstrdup(const char *s) {
    uint32_t va = galloc(strlen(s) + 1);
    strcpy(GPTR(va), s);
    return va;
}

/* ---- host functions callable from the guest ---- */
typedef struct { const char *name; void (*fn)(void); } HostFn;
static HostFn hostfn[8192];
static uint32_t n_hostfn;

uint32_t rt_host_fn(const char *name, void (*fn)(void)) {
    if (n_hostfn == sizeof hostfn / sizeof *hostfn) rt_fatal("host function table full");
    hostfn[n_hostfn] = (HostFn){name, fn};
    return HOSTFN_BASE + HOSTFN_STRIDE * n_hostfn++;
}

uint32_t com_vtable(const ComEntry *e, int n) {
    uint32_t vt = galloc(4 * n);
    for (int i = 0; i < n; i++) MEM32(vt + 4 * i) = rt_host_fn(e[i].name, e[i].fn);
    return vt;
}

uint32_t com_new(uint32_t vtable, uint32_t size) {
    uint32_t o = galloc(size);
    MEM32(o) = vtable;
    return o;
}

static void trace_and_call(uint32_t i) {
    rt_log("[%x] %s(%08X, %08X, %08X, %08X) <- %08X", MEM32(g_fs_base + 0x24), hostfn[i].name,
           MEM32(g_esp + 4), MEM32(g_esp + 8), MEM32(g_esp + 12), MEM32(g_esp + 16), MEM32(g_esp));
}

/* ---- oracle snapshots (SH3_ORACLE=<hex va>[,<nth call>]): with the function built with
 * --trace-functions, its entry and exit state go to oracle_in.snap / oracle_out.snap, together with
 * what every bridged call returned in between. tools/oracle.py replays it on the original x86. ---- */
static uint32_t oracle_va, oracle_nth;
static __thread int oracle_depth, oracle_armed;
static __thread recomp_func_t oracle_pending;
static __thread FILE *oracle_calls;

static void snap_write(const char *path) {
    FILE *f = fopen(path, "wb");
    uint32_t regs[10] = {g_eax, g_ecx, g_edx, g_ebx, g_esp, g_seh_ebp, g_esi, g_edi, g_fs_base, (uint32_t)g_fp_top};
    fwrite(regs, 4, 10, f);
    fwrite(g_fp_stack, 8, 8, f);
    uint32_t ranges[3][2] = {{image_lo, image_hi}, {HEAP_LO, heap_top}, {0, 0}};
    for (int r = 0; ranges[r][1]; r++) {
        uint32_t lo = ranges[r][0], n = ranges[r][1] - lo;
        fwrite(&lo, 4, 1, f);
        fwrite(&n, 4, 1, f);
        fwrite(GPTR(lo), 1, n, f);
    }
    fclose(f);
}

/* SH3_COVER=frame (trace build): logs each function the first time it runs at or after that frame, with
 * the guest stack, to find what code an in-game event runs. SH3_COVER=-frame logs every function that runs
 * from that frame on, once, whether or not it ran before: what the game is doing in a given state. */
int rt_frame;
static int cover_from = -1, cover_all;
static uint8_t *cover;

void recomp_trace_enter(const char *name, uint32_t va) {
    (void)name;
    if (cover && va - g_xbox_code_lo < g_xbox_code_hi - g_xbox_code_lo && (!cover_all || rt_frame >= cover_from)) {
        uint32_t i = va - g_xbox_code_lo;
        if (!(cover[i >> 3] & (1 << (i & 7)))) {
            cover[i >> 3] |= 1 << (i & 7);
            if (rt_frame >= cover_from) { rt_log("cover frame %d: %08X", rt_frame, va); dump_guest_stack(stderr); }
        }
    }
    if (va != oracle_va) return;
    if (oracle_armed) { oracle_depth++; return; }
    if (oracle_nth--) return;
    rt_log("oracle: entering %08X, writing oracle_in.snap", va);
    snap_write("oracle_in.snap");
    oracle_calls = fopen("oracle_calls.txt", "w");
    oracle_armed = 1;
}

void recomp_trace_exit(const char *name, uint32_t va) {
    (void)name;
    if (va != oracle_va || !oracle_armed) return;
    if (oracle_depth) { oracle_depth--; return; }
    snap_write("oracle_out.snap");
    fclose(oracle_calls);
    rt_log("oracle: wrote oracle_out.snap, stopping");
    _exit(0);
}

void recomp_trace_esp(const char *name, const char *tag) { (void)name; (void)tag; }

static void oracle_record(void) {
    recomp_func_t fn = oracle_pending;
    uint32_t target = MEM32(g_esp);  /* the return address identifies the call site */
    fn();
    fprintf(oracle_calls, "%08X %08X %08X\n", target, g_eax, g_edx);
}

recomp_func_t recomp_lookup_kernel(uint32_t va) {
    uint32_t i = (va - HOSTFN_BASE) / HOSTFN_STRIDE;
    if (va < HOSTFN_BASE || i >= n_hostfn) return NULL;
    if (!hostfn[i].fn) rt_fatal("unimplemented import %s", hostfn[i].name);
    if (rt_trace) trace_and_call(i);
    if (oracle_armed) { oracle_pending = hostfn[i].fn; return oracle_record; }
    return hostfn[i].fn;
}


recomp_func_t rt_resolve(uint32_t va) {
    recomp_func_t fn = recomp_lookup_manual(va);
    if (!fn) fn = recomp_lookup(va);
    if (!fn) fn = recomp_lookup_kernel(va);
    if (!fn) rt_fatal("no code at guest address %08X", va);
    return fn;
}

uint32_t guest_call(uint32_t va, int nargs, const uint32_t *args, int stdcall) {
    recomp_func_t fn = rt_resolve(va);
    for (int i = nargs - 1; i >= 0; i--) PUSH32(g_esp, args[i]);
    PUSH32(g_esp, 0xFFFFFFF0u);  /* return address: guest code may read it, never jumps to it */
    fn();
    if (!stdcall) g_esp += 4 * nargs;
    return g_eax;
}

/* ---- non-local jumps: each guest jmp_buf is paired with a native one taken at the setjmp call
 * site (see recomp_types.h); a guest longjmp restores registers and jumps natively. ---- */
#define JMP_SLOTS 32
static __thread struct { uint32_t buf_va; jmp_buf native; } jmp_slot[JMP_SLOTS];
static __thread int jmp_used;

jmp_buf *recomp_setjmp_slot(uint32_t buf_va) {
    for (int i = 0; i < jmp_used; i++)
        if (jmp_slot[i].buf_va == buf_va) { jmp_used = i + 1; return &jmp_slot[i].native; }
    if (jmp_used == JMP_SLOTS) jmp_used--;
    jmp_slot[jmp_used].buf_va = buf_va;
    return &jmp_slot[jmp_used++].native;
}

int recomp_guest_longjmp(uint32_t buf_va, uint32_t value) {
    for (int i = jmp_used - 1; i >= 0; i--) {
        if (jmp_slot[i].buf_va != buf_va) continue;
        g_ebx = MEM32(buf_va + 4);
        g_edi = MEM32(buf_va + 8);
        g_esi = MEM32(buf_va + 12);
        g_esp = MEM32(buf_va + 16) + 4;
        g_seh_ebp = g_ebp = MEM32(buf_va);
        MEM32(g_fs_base) = MEM32(buf_va + 24);  /* SEH chain as it was at setjmp */
        jmp_used = i + 1;
        longjmp(jmp_slot[i].native, value ? (int)value : 1);
    }
    return 0;
}

/* -DRECOMP_ABI_CHECK: a call that came back with ebx/esi/edi changed or esp unpopped. */
void recomp_abi_violation_log(uint32_t va, uint32_t ebx0, uint32_t esi0, uint32_t edi0, uint32_t esp0) {
    static uint32_t seen[256];
    for (int i = 0; i < 256; i++) {
        if (seen[i] == va) return;
        if (!seen[i]) { seen[i] = va; break; }
    }
    rt_log("ABI: call to %08X changed ebx %08X->%08X esi %08X->%08X edi %08X->%08X esp %08X->%08X",
           va, ebx0, g_ebx, esi0, g_esi, edi0, g_edi, esp0, g_esp);
}

void recomp_icall_fail_log(uint32_t va) { rt_fatal("indirect call to untranslated address %08X", va); }
void recomp_icall_not_code_log(uint32_t va) { rt_fatal("indirect call to non-code address %08X", va); }

void recomp_unimpl(const char *text, uint32_t va) {
    static int n;
    if (n++ < 64) rt_log("unimplemented instruction %s at %08X", text, va);
}

/* The guest sees a Pentium III without MMX/SSE/3DNow!, so it takes its plain x87 paths. */
void recomp_cpuid(void) {
    uint32_t leaf = g_eax;
    g_eax = g_ebx = g_ecx = g_edx = 0;
    if (leaf == 0) { g_eax = 1; g_ebx = 0x756E6547; g_edx = 0x49656E69; g_ecx = 0x6C65746E; }  /* GenuineIntel */
    else if (leaf == 1) { g_eax = 0x683; g_edx = 0x1 | 0x10 | 0x100 | 0x8000; }            /* FPU TSC CX8 CMOV */
    else if (leaf == 0x80000000u) g_eax = 0x80000000u;                                       /* no extended leaves */
}

/* ---- loading sh3.exe at its own addresses ---- */
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

#if defined(_WIN32)
/* Filled by the bridges' constructors (BRIDGE_REG in rt.h): PE-COFF has no __start_/__stop_ for a section. */
#define MAX_BRIDGES 4096
static Bridge bridges[MAX_BRIDGES];
static int n_bridges;
void rt_bridge_add(const char *name, void (*fn)(void)) {
    if (n_bridges == MAX_BRIDGES) rt_fatal("too many bridges");
    bridges[n_bridges++] = (Bridge){name, fn};
}
static recomp_func_t find_bridge(const char *name) {
    for (int i = 0; i < n_bridges; i++)
        if (!strcmp(bridges[i].name, name)) return bridges[i].fn;
    return NULL;
}
#else
extern const Bridge __start_game_bridges[], __stop_game_bridges[];
static recomp_func_t find_bridge(const char *name) {
    for (const Bridge *b = __start_game_bridges; b < __stop_game_bridges; b++)
        if (!strcmp(b->name, name)) return b->fn;
    return NULL;
}
#endif

recomp_func_t rt_bridge(const char *name) { return find_bridge(name); }

static uint32_t load_exe(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) rt_fatal("cannot open %s/%s", rt_game_dir, path);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    uint8_t *d = malloc(n);
    fseek(f, 0, SEEK_SET);
    if (fread(d, 1, n, f) != (size_t)n) rt_fatal("cannot read %s", path);
    fclose(f);

    const uint8_t *pe = d + rd32(d + 0x3C), *opt = pe + 24;
    uint32_t base = rd32(opt + 28), entry = base + rd32(opt + 16);
    image_lo = base;
    image_hi = base + rd32(opt + 56);  /* SizeOfImage */
    host_arena_commit(base, image_hi - base);
    memcpy(GPTR(base), d, 0x1000);  /* PE headers: the CRT checks its own MZ/PE signature */
    unsigned nsec = pe[6] | pe[7] << 8;
    const uint8_t *sec = opt + (pe[20] | pe[21] << 8);
    for (unsigned i = 0; i < nsec; i++, sec += 40) {
        uint32_t vs = rd32(sec + 8), va = base + rd32(sec + 12), rs = rd32(sec + 16), ro = rd32(sec + 20);
        memcpy(GPTR(va), d + ro, rs < vs ? rs : vs);
        if (!memcmp(sec, ".text", 6)) { g_xbox_code_lo = va; g_xbox_code_hi = va + vs; }
    }
    free(d);

    /* Point every IAT slot at its bridge. */
    unsigned missing = 0;
    for (uint32_t desc = base + MEM32(base + MEM32(base + 0x3C) + 24 + 104); MEM32(desc + 12); desc += 20) {
        const char *dll = GSTR(base + MEM32(desc + 12));
        uint32_t ilt = base + (MEM32(desc) ? MEM32(desc) : MEM32(desc + 16)), iat = base + MEM32(desc + 16);
        for (; MEM32(ilt); ilt += 4, iat += 4) {
            uint32_t e = MEM32(ilt);
            char name[128];
            if (e & 0x80000000u) snprintf(name, sizeof name, "%.*s#%u", (int)strcspn(dll, "."), dll, e & 0xFFFF);
            else snprintf(name, sizeof name, "%s", GSTR(base + e + 2));
            recomp_func_t fn = find_bridge(name);
            if (!fn) { missing++; if (rt_trace) rt_log("no bridge for %s!%s", dll, name); }
            MEM32(iat) = rt_host_fn(strdup(name), fn);
        }
    }
    rt_log("loaded %s: entry %08X, %u imports without a bridge", path, entry, missing);
    return entry;
}

extern void xbe_entry_point(void);
void game_init(void);  /* src/game/sh3.c */
void launcher_run(void);  /* src/d3d8/launcher.c */

/* sh3.ini in the game folder: resolution=WxH (default 1280x720), cores=1 to keep the guest threads on one
 * core, trace=1 to log every bridged call, log=1 to write sh3.log on the Switch. */
uint32_t rt_res_w = 1280, rt_res_h = 720;
static void read_ini(void) {
    FILE *ini = fopen("sh3.ini", "r");
    char line[128];
    unsigned a, b, cores = 3;
    while (ini && fgets(line, sizeof line, ini)) {
        if (sscanf(line, " resolution = %ux%u", &a, &b) == 2 && a >= 320 && b >= 240 && a <= 1920 && b <= 1440)
            rt_res_w = a, rt_res_h = b;
        if (sscanf(line, " cores = %u", &a) == 1) cores = a;
        if (sscanf(line, " trace = %u", &a) == 1) rt_trace = a;
        if (sscanf(line, " hd = %u", &a) == 1) rt_hd_textures = a != 0;
        if (sscanf(line, " aniso = %u", &a) == 1) rt_aniso = a > 16 ? 16 : a < 1 ? 1 : a;
        if (sscanf(line, " widescreen = %u", &a) == 1) rt_ws = a != 0;
        if (sscanf(line, " filter = %u", &a) == 1) rt_filter = a > 2 ? 2 : a;
        if (sscanf(line, " log = %u", &a) == 1) log_on = a;
    }
    if (ini) fclose(ini);
    host_use_single_core(cores >= 3 ? -1 : 0);
}

/* Once the launcher is done (src/d3d8/launcher.c): its settings, the exe and the game's own setup. */
void rt_game_setup(void) {
    read_ini();
    load_exe("sh3.exe");
    game_init();
    if (cover_from >= 0) cover = calloc((g_xbox_code_hi - g_xbox_code_lo) / 8 + 1, 1);
}

/* The game runs on a thread of its own: lifted code recurses as deep as the guest and the main
 * thread's stack is not ours to size on every platform. */
static void game_thread(void *unused) {
    (void)unused;
    rt_thread_init_main(1u << 20);
    launcher_run();
    xbe_entry_point();
    rt_log("entry point returned");
    exit(0);
}

/* ---- the game folder ------------------------------------------------------------------------------
 * Where sh3.exe and data/ live. Without an argument the launcher finds it: the folder on the command line,
 * SH3_DIR, the one it saved beside its own executable, the executable's folder itself, the working directory,
 * and then a folder picker. A folder is the game's when it holds sh3.exe and data/. */
#define GAME_CONFIG "sh3-pc.ini"    /* the port's own settings, beside the port's executable: game=<folder> */
#define GAME_EXE_BYTES 3338240L     /* the build the recompiled code was generated from (docs/BUILD-WINDOWS.md) */

static int is_folder(const char *p) {
    stat_t st;
    return p && *p && stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static long exe_bytes(const char *dir) {
    char p[600];
    stat_t st;
    snprintf(p, sizeof p, "%s/sh3.exe", dir);
    return stat(p, &st) == 0 && !S_ISDIR(st.st_mode) ? (long)st.st_size : -1;
}

static int game_folder(const char *dir) {
    char p[600];
    if (!is_folder(dir)) return 0;
    snprintf(p, sizeof p, "%s/data", dir);
    return exe_bytes(dir) > 0 && is_folder(p);
}

/* The folder the port's own executable is in. On the Switch it is where the NRO and the game go. */
static int port_dir(char *out, size_t n) {
#ifdef _WIN32
    if (!GetModuleFileNameA(NULL, out, (DWORD)n) || !*out) return 0;
    char *slash = strrchr(out, '\\');
#elif !defined(__SWITCH__)
    ssize_t k = readlink("/proc/self/exe", out, n - 1);
    if (k <= 0 || (size_t)k >= n - 1) return 0;
    out[k] = 0;
    char *slash = strrchr(out, '/');
#else
    snprintf(out, n, "sdmc:/switch/sh3-pc");  /* no argv on the console: the NRO's folder is the game folder */
    return 1;
#endif
    if (!slash) return 0;
    *slash = 0;
    return 1;
}

static int config_path(char *out, size_t n) {
    char dir[512];
    if (!port_dir(dir, sizeof dir) || strlen(dir) + sizeof GAME_CONFIG + 2 >= n) return 0;
    snprintf(out, n, "%s/%s", dir, GAME_CONFIG);
    return 1;
}

/* The folder the launcher last used ("game=" on its own line, leading spaces and # comments allowed). */
static int saved_game(char *out, size_t n) {
    char cfg[600], line[600];
    if (!config_path(cfg, sizeof cfg)) return 0;
    FILE *f = fopen(cfg, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line, *e;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "game", 4)) continue;
        p += 4;
        while (*p == ' ' || *p == '\t') p++;
        if (*p++ != '=') continue;
        while (*p == ' ' || *p == '\t') p++;
        e = p + strlen(p);
        while (e > p && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        if (*p) {
            snprintf(out, n, "%s", p);
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static void save_game(const char *dir) {
    char cfg[600], old[600];
    if (!config_path(cfg, sizeof cfg)) return;
    if (saved_game(old, sizeof old) && !strcmp(old, dir)) return;  /* already how it was */
    FILE *f = fopen(cfg, "w");
    if (!f) { rt_log("cannot write %s", cfg); return; }
    fprintf(f, "# written by the launcher: the folder holding sh3.exe and data/\ngame=%s\n", dir);
    fclose(f);
    rt_log("launcher: the game folder is %s (saved in %s)", dir, cfg);
}

#ifndef __SWITCH__
/* ---- the path a player gives --------------------------------------------------------------------------
 * The launcher is handed a path: the game's folder, the game's own sh3.exe, or the thing a player is most likely
 * to have - a disc. A disc is read far enough to say which release it is, because Silent Hill 3 for the
 * PlayStation 2 is a PlayStation 2 disc: MIPS code and the console's own data formats, which this recompilation of
 * the 2003 PC build cannot run. */

#define PATH_SEP(c) ((c) == '/' || (c) == 92)  /* a separator: slash, or backslash by its code */

static int is_file(const char *p) {
    stat_t st;
    return p && *p && stat(p, &st) == 0 && !S_ISDIR(st.st_mode);
}

/* The name at the end of a path. */
static const char *file_name(const char *p) {
    const char *s = p + strlen(p);
    while (s > p && !PATH_SEP(s[-1])) s--;
    return s;
}

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* A PlayStation 2 program: little-endian MIPS (e_machine at 18), which is what the console's ELFs are. */
static int mips_elf(const char *path) {
    unsigned char h[20];
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(h, 1, sizeof h, f);
    fclose(f);
    return n == sizeof h && h[0] == 0x7F && h[1] == 'E' && h[2] == 'L' && h[3] == 'F' && h[18] == 8 && h[19] == 0;
}

/* ---- discs: ISO 9660 in 2048-byte sectors (.iso) or in the 2352-byte sectors of a .bin rip ---- */

typedef struct { FILE *f; long stride, data; } Disc;

static int disc_open(Disc *d, const char *path) {
    static const struct { long stride, data; } ways[] = {{2048, 0}, {2352, 16}, {2352, 24}};  /* .iso, Mode 1, Mode 2 */
    unsigned char v[8];
    d->f = fopen(path, "rb");
    if (!d->f) return 0;
    for (int i = 0; i < 3; i++) {
        d->stride = ways[i].stride;
        d->data = ways[i].data;
        if (!fseek(d->f, 16 * d->stride + d->data, SEEK_SET) && fread(v, 1, 8, d->f) == 8 && v[0] == 1 &&
            !memcmp(v + 1, "CD001", 5))
            return 1;  /* the primary volume descriptor, at sector 16 as the standard says */
    }
    fclose(d->f);
    d->f = NULL;
    return 0;
}

static int disc_read(Disc *d, uint32_t lba, void *buf, uint32_t len) {
    if (fseek(d->f, (long)lba * d->stride + d->data, SEEK_SET)) return 0;
    return fread(buf, 1, len, d->f) == len;
}

/* An ISO 9660 name without its ";1" version suffix. */
static int iso_name_is(const unsigned char *rec, const char *want) {
    int n = rec[32], w = (int)strlen(want);
    const char *s = (const char *)rec + 33;
    if (n >= 2 && s[n - 2] == ';') n -= 2;
    if (n != w) return 0;
    for (int i = 0; i < n; i++)
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)want[i])) return 0;
    return 1;
}

/* A file in the disc's root directory: its first sector and its length. */
static int disc_file(Disc *d, const char *name, uint32_t *lba, uint32_t *len) {
    unsigned char pvd[2048], sec[2048];
    uint32_t dir_lba, dir_len;
    if (!disc_read(d, 16, pvd, sizeof pvd)) return 0;
    dir_lba = le32(pvd + 156 + 2);   /* the root directory record inside the volume descriptor */
    dir_len = le32(pvd + 156 + 10);
    for (uint32_t done = 0; done < dir_len && done < (1u << 20); done += 2048) {
        uint32_t n = dir_len - done < 2048 ? dir_len - done : 2048;
        if (!disc_read(d, dir_lba + done / 2048, sec, n)) return 0;
        for (uint32_t i = 0; i + 33 < n && sec[i];) {
            if (sec[i + 32] && iso_name_is(sec + i, name)) {
                *lba = le32(sec + i + 2);
                *len = le32(sec + i + 10);
                return 1;
            }
            i += sec[i];
        }
    }
    return 0;
}

/* The program the disc starts, from SYSTEM.CNF's "BOOT2 = cdrom0:\SLUS_206.22;1". */
static int disc_boot(Disc *d, char *out, size_t n) {
    unsigned char b[600];
    uint32_t lba, len;
    char *v, *e;
    if (!disc_file(d, "SYSTEM.CNF", &lba, &len)) return 0;
    if (len > sizeof b - 1) len = sizeof b - 1;
    if (!disc_read(d, lba, b, len)) return 0;
    b[len] = 0;
    if (!(v = strstr((char *)b, "BOOT")) || !(v = strchr(v, '='))) return 0;
    v++;
    while (*v == ' ' || *v == '\t') v++;
    if ((e = strchr(v, ':')) != NULL) v = e + 1;  /* drop "cdrom0:" */
    while (*v == '\\' || *v == '/') v++;
    for (e = v; *e && *e != ';' && *e != ' ' && *e != '\t' && *e != '\r' && *e != '\n'; e++) {}
    *e = 0;
    if (!*v) return 0;
    snprintf(out, n, "%s", v);
    return 1;
}

/* The image a .cue describes: its FILE line, made relative to the .cue itself. */
static int cue_image(const char *cue, char *out, size_t n) {
    char line[512];
    int ok = 0;
    FILE *f = fopen(cue, "r");
    while (f && !ok && fgets(line, sizeof line, f)) {
        char *p = line, *e;
        while (*p == ' ' || *p == '\t') p++;
        if (strncasecmp(p, "FILE", 4)) continue;
        p += 4;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '"') {
            if (!(e = strrchr(++p, '"'))) continue;
        } else if (!(e = strrchr(p, ' '))) continue;  /* FILE name BINARY */
        *e = 0;
        if (*p == '/' || *p == '\\' || (p[0] && p[1] == ':'))
            snprintf(out, n, "%s", p);  /* a cue with an absolute path is used as it is */
        else
            snprintf(out, n, "%.*s%s", (int)(file_name(cue) - cue), cue, p);
        ok = is_file(out);
    }
    if (f) fclose(f);
    return ok;
}

/* What to do instead, at the end of every refusal that has an answer. */
static const char *const ask_for_pc =
    "\n\nGive the port the game's sh3.exe, the folder that holds it, or a Silent Hill 3 disc.";

int rt_last_ps2;                 /* rt_game_path: the path was a PlayStation 2 disc or program */
int rt_last_pc;                  /* rt_game_path: the path was the PC installation disc */
char rt_ps2_path[700];           /* and where it is (the .bin for a .cue) */

/* The folder a path means, when it is the game: NULL, and `dir` holds the folder. Otherwise a sentence saying what
 * the path was instead - this is where the launcher's wording lives, for the command line, the picker and the
 * Options screen alike. The sentence sits in a buffer this function owns: it holds until the next call. */
const char *rt_game_path(const char *given, char *dir, size_t n) {
    static char why[1200];
    char parent[700], image[700], boot[300];
    uint32_t lba, len;
    Disc d;
    rt_last_ps2 = 0;
    rt_last_pc = 0;
    rt_ps2_path[0] = 0;
    if (is_folder(given)) {
        if (game_folder(given)) {
            snprintf(dir, n, "%s", given);
            return NULL;
        }
        snprintf(why, sizeof why, "That folder is not a Silent Hill 3 install:\n\n%s\n\n"
                                  "Pick the folder that holds sh3.exe and data/.", given);
        return why;
    }
    if (!is_file(given)) {
        snprintf(why, sizeof why, "There is nothing at\n\n%s", given);
        return why;
    }
    /* The game's own executable, or a file from inside the install: the folder it is in is the game. */
    {
        const char *base = file_name(given);
        size_t k = (size_t)(base - given);
        if (k && k < sizeof parent) {
            memcpy(parent, given, k);
            parent[k] = 0;
            while (k > 1 && PATH_SEP(parent[k - 1]) && !(k == 3 && parent[1] == ':')) parent[--k] = 0;
            if (game_folder(parent)) {
                snprintf(dir, n, "%s", parent);
                return NULL;
            }
        }
    }
    if (mips_elf(given)) {
        rt_last_ps2 = 1;
        snprintf(rt_ps2_path, sizeof rt_ps2_path, "%s", given);
        snprintf(why, sizeof why, "That is a PlayStation 2 program (%s): MIPS code with the console's own data "
                                  "formats.\n\nThis port recompiles the 2003 PC release, so it cannot run it. Install "
                                  "the PC discs and give it the installed sh3.exe.", file_name(given));
        return why;
    }
    snprintf(image, sizeof image, "%s", given);
    {
        const char *dot = strrchr(given, '.');
        if (dot && !strcasecmp(dot + 1, "cue") && !cue_image(given, image, sizeof image)) {
            snprintf(why, sizeof why,
                     "The cue sheet\n\n%s\n\nnames a disc image that is not there. Keep the .bin and the .cue in "
                     "the same folder, with the .cue's FILE line matching the .bin's name; or give the port the .bin "
                     "itself.", given);
            return why;
        }
    }
    if (disc_open(&d, image)) {
        int ps2 = disc_boot(&d, boot, sizeof boot);
        int pc = !ps2 && (disc_file(&d, "SH3.EXE", &lba, &len) || disc_file(&d, "SETUP.EXE", &lba, &len));
        fclose(d.f);
        if (ps2) {
            rt_last_ps2 = 1;
            snprintf(rt_ps2_path, sizeof rt_ps2_path, "%s", image);
            snprintf(why, sizeof why, "That is Silent Hill 3 for the PlayStation 2: the disc starts %s, which is MIPS "
                                      "code with the console's own data formats.\n\nThis port recompiles the 2003 PC "
                                      "release, so it cannot run it. Install the PC discs and give it the installed "
                                      "sh3.exe, or the folder that holds it.", boot);
            return why;
        }
        if (pc) {
            rt_last_pc = 1;
            snprintf(why, sizeof why, "That is a Silent Hill 3 installation disc, not an installed copy.\n\n"
                                      "Install it (keep the Movies option: that is the FMVs), then give the port the "
                                      "folder that holds sh3.exe and data/.");
            return why;
        }
        snprintf(why, sizeof why, "That disc is not Silent Hill 3 (%s)%s", file_name(image), ask_for_pc);
        return why;
    }
    snprintf(why, sizeof why, "That file is not part of Silent Hill 3 (%s)%s", file_name(given), ask_for_pc);
    return why;
}

/* Says something in the log, and on screen when there is a screen for it. */
void rt_say(const char *text) {
    rt_log("%s", text);
#ifdef _WIN32
    win_message("Silent Hill 3", text);
#endif
}

#ifdef _WIN32
/* PCSX2, the PlayStation 2 emulator: the usual install folders, then PATH. */
static int find_pcsx2(char *out, size_t n) {
    static const char *const exes[] = {"pcsx2-qt.exe", "pcsx2.exe"};
    static const char *const envs[] = {"LOCALAPPDATA", "ProgramFiles", "ProgramFiles(x86)"};
    static const char *const subs[] = {"\\Programs\\PCSX2\\", "\\PCSX2\\", "\\PCSX2\\"};
    char path[700];
    for (size_t e = 0; e < sizeof envs / sizeof *envs; e++) {
        const char *root = getenv(envs[e]);
        if (!root || !*root) continue;
        for (size_t x = 0; x < sizeof exes / sizeof *exes; x++) {
            snprintf(path, sizeof path, "%s%s%s", root, subs[e], exes[x]);
            if (is_file(path)) { snprintf(out, n, "%s", path); return 1; }
        }
    }
    for (size_t x = 0; x < sizeof exes / sizeof *exes; x++)
        if (SearchPathA(NULL, exes[x], NULL, (DWORD)n, out, NULL)) return 1;
    return 0;
}

static int launch_process(const char *exe, const char *arg) {
    char cmd[1500];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    snprintf(cmd, sizeof cmd, "\"%s\" \"%s\"", exe, arg);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 0;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
}
#endif

/* A PlayStation 2 disc or program cannot be run by this port, but PCSX2 can run it. Offer that instead of only
 * refusing, and answer on screen either way so the caller does not put up a second box. */
int rt_ps2_offer(const char *disc) {
#ifdef _WIN32
    char exe[700], text[1400];
    if (find_pcsx2(exe, sizeof exe)) {
        snprintf(text, sizeof text,
                 "That is Silent Hill 3 for the PlayStation 2 (%s).\n\n"
                 "sh3-pc recompiles the 2003 PC release, so it cannot run PS2 code. Open it in PCSX2 instead?",
                 file_name(disc));
        if (!win_confirm("Silent Hill 3", text)) return 1;
        if (launch_process(exe, disc)) {
            rt_log("launcher: opened %s in PCSX2 (%s)", disc, exe);
            return 2;
        }
        win_message("Silent Hill 3", "PCSX2 was found but could not be started.");
        return 1;
    }
    snprintf(text, sizeof text,
             "That is Silent Hill 3 for the PlayStation 2 (%s).\n\n"
             "This port recompiles the 2003 PC release, so it cannot run PS2 discs or programs. PCSX2 can: open "
             "pcsx2.net to download it?", file_name(disc));
    if (win_confirm("Silent Hill 3", text)) rt_open_url("https://pcsx2.net/");
    return 1;
#else
    (void)disc;
    return 0;
#endif
}

/* ---- starting the copy's own executable ------------------------------------------------------------
 * The release is the launcher-only build: the recompiled game code belongs to a build made from the player's
 * own sh3.exe, and no release can contain it. The imported Full-Rip has that sh3.exe in its folder, so the
 * honest fallback is to ask, then start it there. Nothing is copied or replaced. */
static int same_path(const char *a, const char *b) {
    char x[700], y[700];
    size_t i;
    snprintf(x, sizeof x, "%s", a);
    snprintf(y, sizeof y, "%s", b);
    for (i = 0; x[i]; i++) if (x[i] == '/') x[i] = '\\';
    for (i = 0; y[i]; i++) if (y[i] == '/') y[i] = '\\';
    return !strcasecmp(x, y);
}

int rt_run_original(void) {
#ifdef _WIN32
    char exe[700], self[700], text[1200], why[1200];
    stat_t a, b;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    if (!rt_game_dir[0]) {
        rt_say("There is no game folder yet.\n\nIn Options, use Import a .zip download for the Full-Rip archive, "
               "or Game folder for a folder that already holds sh3.exe and data/.");
        return 0;
    }
    snprintf(exe, sizeof exe, "%s/sh3.exe", rt_game_dir);
    if (!is_file(exe) || stat(exe, &a) != 0) {
        snprintf(why, sizeof why, "There is no sh3.exe in\n\n%s", rt_game_dir);
        rt_say(why);
        return 0;
    }
    if (!GetModuleFileNameA(NULL, self, sizeof self)) self[0] = 0;
    if (same_path(self, exe)) {
        rt_say("That sh3.exe is this launcher. Keep the launcher in its own folder: the game's own sh3.exe is what "
               "it needs beside data/.");
        return 0;
    }
    if (stat(self, &b) == 0 && a.st_size == b.st_size) {
        rt_say("The sh3.exe in the game folder is a copy of this launcher, not the game's own executable. Put the "
               "launcher in its own folder and leave the game's sh3.exe where it is.");
        return 0;
    }
    snprintf(text, sizeof text,
             "This launcher has no recompiled game code, so it cannot start the port.\n\n"
             "Start the game's own sh3.exe in\n%s instead?", rt_game_dir);
    if (!win_confirm("Silent Hill 3", text)) return 0;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    snprintf(text, sizeof text, "\"%s\"", exe);
    if (!CreateProcessA(exe, text, NULL, NULL, FALSE, 0, NULL, rt_game_dir, &si, &pi)) {
        snprintf(why, sizeof why,
                 "The game's sh3.exe could not be started (error %lu).\n\nIt may need the compatibility mode the "
                 "download's notes describe, or the PC Fix.", (unsigned long)GetLastError());
        rt_say(why);
        return 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    rt_log("launcher: started the copy's own %s", exe);
    return 1;
#else
    return 0;
#endif
}

/* Whether PCSX2 is installed, so the launcher's note does not promise it when it is not. */
int rt_pcsx2_found(void) {
#ifdef _WIN32
    char exe[700];
    return find_pcsx2(exe, sizeof exe);
#else
    return 0;
#endif
}
#endif /* !__SWITCH__ */


/* dir made absolute, so the log, the saves and the shader cache name the same folder however the port was
 * started; dir itself when that fails. */
static const char *absolute_dir(const char *dir, char *out, size_t n) {
#ifdef _WIN32
    return _fullpath(out, dir, n) ? out : dir;
#elif !defined(__SWITCH__)
    return realpath(dir, out) ? out : dir;
#else
    (void)out;
    (void)n;
    return dir;
#endif
}

/* A .zip dropped on the port, or given as its argument: a mod to install, not a game path. */
static int zip_arg(const char *p) {
    size_t n = strlen(p);
    return n > 4 && !strcasecmp(p + n - 4, ".zip") && is_file(p);
}

/* The launcher's "Game location" rows: checks the folder, moves the port into it and remembers it. The launcher runs
 * before anything of the game is loaded, so that is the whole of the change; it reloads sh3.ini, mods/ and the
 * menu sounds itself. */
int rt_set_game_dir(const char *dir) {
    char full[700];
    const char *root = absolute_dir(dir, full, sizeof full);
    if (!game_folder(root) || chdir(root)) return 0;
    snprintf(rt_game_dir, sizeof rt_game_dir, "%s", root);
    save_game(rt_game_dir);
    return 1;
}

#ifdef _WIN32
/* Testing: SH3_PICK answers both pickers without a dialog, so a scripted run (SH3_KEYS) can walk Options. */
static int test_pick(char *out, size_t n) {
    const char *answer = getenv("SH3_PICK");
    if (!answer || !*answer) return 0;
    snprintf(out, n, "%s", answer);
    return 1;
}

int rt_pick_game_folder(const char *start, char *out, size_t n) {
    if (test_pick(out, n)) return 1;
    return win_pick_path("Where is Silent Hill 3? Pick the folder that holds sh3.exe and data/", start, out, n);
}

#define GAME_FILTER "Silent Hill 3 or its disc\0sh3.exe;*.iso;*.bin;*.cue;*.img\0All files\0*.*\0"
#define MOD_FILTER "Mod archive\0*.zip\0All files\0*.*\0"
#define GAME_ARCHIVE_FILTER "The downloaded game archive\0*.zip\0All files\0*.*\0"
#define CHECK_FILTER "A Silent Hill 3 copy or disc\0sh3.exe;*.zip;*.cue;*.bin;*.iso;*.img\0All files\0*.*\0"

int rt_pick_game_disc(const char *start, char *out, size_t n) {
    if (test_pick(out, n)) return 1;
    return win_pick_file("Where is Silent Hill 3? Pick sh3.exe or the disc", start, GAME_FILTER, out, n);
}

/* Check this copy: one dialog for sh3.exe, a disc image or a download, so the answer names the thing that was
 * picked. A folder can be dropped on the window instead (it is what the Game folder row is for). */
int rt_pick_copy(char *out, size_t n) {
    if (test_pick(out, n)) return 1;
    return win_pick_file("Check this copy: pick sh3.exe, a disc image, or the .zip", rt_game_dir[0] ? rt_game_dir : NULL,
                         CHECK_FILTER, out, n);
}

/* ---- importing a download ---------------------------------------------------------------------------
 * The Full-Rip is a zip of a pre-installed copy: sh3.exe and data/ inside, no installer and no registry.
 * Unpacking it beside itself and finding that folder is all "installing" it takes here. Windows ships
 * tar.exe (bsdtar), which reads zip and handles a 2.6 GB archive without loading it into memory; it runs in
 * its own console so the file list is visible while the launcher waits. */
static int find_tar(char *out, size_t n) {
    const char *root = getenv("SystemRoot");
    char p[700];
    if (root && *root) {
        snprintf(p, sizeof p, "%s\\System32\\tar.exe", root);
        if (is_file(p)) { snprintf(out, n, "%s", p); return 1; }
    }
    return SearchPathA(NULL, "tar.exe", NULL, (DWORD)n, out, NULL) != 0;
}

/* The install inside `root`, at most four folders down: a folder holding sh3.exe and data/. */
static int find_install(const char *root, int depth, char *out, size_t n) {
    if (game_folder(root)) { snprintf(out, n, "%s", root); return 1; }
    if (depth >= 4) return 0;
    char pat[800];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int found = 0;
    snprintf(pat, sizeof pat, "%s/*", root);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        char sub[900];
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        if (!strcasecmp(fd.cFileName, "savedata") || !strcasecmp(fd.cFileName, "mods")) continue;
        snprintf(sub, sizeof sub, "%s/%s", root, fd.cFileName);
        if (find_install(sub, depth + 1, out, n)) { found = 1; break; }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

static int unpack_zip(const char *zip, const char *dest, char *err, size_t errn) {
    char tar[700], cmd[2400];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD code = 0;
    if (!find_tar(tar, sizeof tar)) {
        snprintf(err, errn, "Windows' own tar.exe was not found. Extract %s with 7-Zip, then pick the folder "
                            "that holds sh3.exe and data/.", file_name(zip));
        return 0;
    }
    snprintf(cmd, sizeof cmd, "\"%s\" -xvf \"%s\" -C \"%s\"", tar, zip, dest);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        snprintf(err, errn, "The archive could not be unpacked: %s", file_name(zip));
        return 0;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code) {
        snprintf(err, errn, "%s could not be unpacked (tar stopped with %lu).", file_name(zip), (unsigned long)code);
        return 0;
    }
    return 1;
}

int rt_import_archive(const char *archive, char *out, size_t n, char *err, size_t errn) {
    const char *base = file_name(archive), *dot = strrchr(base, '.');
    char dest[700], stem[256];
    size_t dirlen = (size_t)(base - archive);
    if (!is_file(archive)) {
        snprintf(err, errn, "There is nothing at\n\n%s", archive);
        return 0;
    }
    if (!dot || strcasecmp(dot, ".zip")) {
        snprintf(err, errn, "That is not a .zip. The Full-Rip downloads as a zip; a .rar or .7z has to be "
                            "extracted with 7-Zip first, and then the folder that holds sh3.exe and data/ picked "
                            "in Options > Game folder.");
        return 0;
    }
    snprintf(stem, sizeof stem, "%.*s", (int)(dot - base), base);
    if (!stem[0]) snprintf(stem, sizeof stem, "silent-hill-3");
    if (dirlen + strlen(stem) + 2 >= sizeof dest) {
        snprintf(err, errn, "That path is too long to unpack beside the archive.");
        return 0;
    }
    snprintf(dest, sizeof dest, "%.*s%s", (int)dirlen, archive, stem);
    if (find_install(dest, 0, out, n)) return 1;  /* already unpacked: the folder is the game */
    if (mkdir(dest, 0755) && !is_folder(dest)) {
        snprintf(err, errn, "The archive cannot be unpacked beside itself. Extract it with 7-Zip, then pick the "
                            "folder that holds sh3.exe and data/.");
        return 0;
    }
    if (!unpack_zip(archive, dest, err, errn)) return 0;
    if (!find_install(dest, 0, out, n)) {
        snprintf(err, errn, "%s was unpacked into %s, but there is no sh3.exe with data/ in it. The Full-Rip has "
                            "them; a disc image does not.", file_name(archive), dest);
        return 0;
    }
    rt_log("launcher: unpacked %s into %s", archive, dest);
    return 1;
}

int rt_pick_game_archive(char *out, size_t n) {
    if (test_pick(out, n)) return 1;
    return win_pick_file("Where is the downloaded game archive?", NULL, GAME_ARCHIVE_FILTER, out, n);
}

int rt_pick_mod_zip(char *out, size_t n) {
    return win_pick_file("Install a mod: pick its .zip", rt_game_dir, MOD_FILTER, out, n);
}
#endif

/* A zip can be a mod or the whole downloaded game. The Full-Rip has sh3.exe and data/ in it; a mod does not.
 * This reads only the tail of the archive (its central directory), so a 2.6 GB download costs nothing. */
static int zip_has_game(const char *path) {
    FILE *f = fopen(path, "rb");
    long n, win, i;
    unsigned char *buf;
    int exe = 0, data = 0;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    win = n < (4L << 20) ? n : (4L << 20);
    buf = malloc((size_t)win);
    if (!buf || win <= 0 || fseek(f, n - win, SEEK_SET) || fread(buf, 1, (size_t)win, f) != (size_t)win) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    for (i = 0; i + 7 <= win; i++) {
        if (!exe && !strncasecmp((const char *)buf + i, "sh3.exe", 7)) exe = 1;
        if (!data && ((i + 6 <= win && !strncasecmp((const char *)buf + i, "/data/", 6)) ||
                      (i + 5 <= win && !strncasecmp((const char *)buf + i, "data/", 5)))) data = 1;
        if (exe && data) break;
    }
    free(buf);
    return exe && data;
}

int main(int argc, char **argv) {
    char dir[700] = "";
#ifdef __SWITCH__
    snprintf(dir, sizeof dir, "sdmc:/switch/sh3-pc");
    (void)argc; (void)argv;
    if (__nxlink_host.s_addr) {  /* launched by nxlink: stdout/stderr and the log go back to the PC */
        socketInitializeDefault();
        if (nxlinkStdio() >= 0) log_on = 1;
        else socketExit();
    }
#else
    /* An argument (or SH3_DIR) that is not the game is an error, not a reason to ask: it is what a shortcut, a
     * dropped file or a script passes, and quietly using another folder would hide the mistake. A path can be the
     * game's folder, the game's own sh3.exe, or a disc, which is read far enough to be named. */
    const char *given = argc > 1 && argv[1][0] ? argv[1] : getenv("SH3_DIR");
    /* A mod archive dropped on the port: install it into the game the port already knows and start that game, which
     * is what dropping it is for. It never becomes the game path. */
    if (given && zip_arg(given) && !zip_has_game(given)) {
        char zpath[700], target[700], name[128], err[600] = "";
        const char *zip = absolute_dir(given, zpath, sizeof zpath);
        given = NULL;  /* a mod archive is never the game path */
        if (!saved_game(target, sizeof target) || !game_folder(target)) {
            rt_say("That is a mod archive, and there is no game folder to install it into.\n\nStart the game once, "
                   "then drop the archive on the port again.");
            return 1;
        }
        if (chdir(target) || !mods_install(zip, name, sizeof name, err, sizeof err)) {
            rt_say(err[0] ? err : "That archive could not be installed.");
            return 1;
        }
        rt_log("launcher: installed the mod \"%s\"; starting the game", name);
    }
    if (given) {
        const char *why;
#ifdef _WIN32
        const char *suffix = strrchr(file_name(given), '.');
        if (suffix && !strcasecmp(suffix, ".zip")) {
            char unpacked[700], err[700];
            if (rt_import_archive(given, unpacked, sizeof unpacked, err, sizeof err)) {
                rt_log("launcher: %s is the game", unpacked);
                given = unpacked;
            } else {
                rt_say(err);
                return 1;
            }
        }
#endif
        why = rt_game_path(given, dir, sizeof dir);
        if (why) {
            if (!rt_last_ps2 || !rt_ps2_offer(rt_ps2_path)) rt_say(why);
            return 1;
        }
    } else {
        char here[700] = "", start[700] = "";
        if (saved_game(dir, sizeof dir) && !game_folder(dir)) dir[0] = 0;
        if (!dir[0] && port_dir(here, sizeof here) && game_folder(here)) snprintf(dir, sizeof dir, "%s", here);
        if (!dir[0] && game_folder(".")) snprintf(dir, sizeof dir, ".");
        if (!dir[0]) saved_game(start, sizeof start);  /* where the picker opens: the folder it used last */
#ifdef _WIN32
#ifndef PORT_LAUNCHER_ONLY
        /* First launch: the shell's picker, until it names something that is the game or is closed. */
        while (!dir[0]) {
            char picked[700];
            const char *why;
            if (!rt_pick_game_folder(start, picked, sizeof picked)) break;
            if (!(why = rt_game_path(picked, dir, sizeof dir))) break;
            if (!rt_last_ps2 || !rt_ps2_offer(rt_ps2_path)) rt_say(why);
            dir[0] = 0;
        }
#endif /* PORT_LAUNCHER_ONLY */
#endif
    }
#endif
    if (!dir[0]) {
#ifdef PORT_LAUNCHER_ONLY
        /* The launcher is this program's face: open it with no game, so Options can import the download, pick the
         * folder or read a disc, instead of a console that only says sh3.exe is missing. */
        rt_log("no game folder yet: opening the launcher");
        rt_game_dir[0] = 0;  /* "." is only a default; the launcher must know it has no game */
        launcher_run();
        return 0;
#else
        rt_log("no game folder: pass the Silent Hill 3 folder as the first argument, set SH3_DIR, or put the "
               "port's executable in the game folder (sh3.exe and data/ beside it)");
        return 1;
#endif
    }
    char full[700];
    const char *root = absolute_dir(dir, full, sizeof full);
    snprintf(rt_game_dir, sizeof rt_game_dir, "%s", root);
    if (chdir(rt_game_dir)) rt_fatal("cannot enter %s", rt_game_dir);
#ifndef __SWITCH__
    save_game(rt_game_dir);
#endif
    long bytes = exe_bytes(rt_game_dir);
    if (bytes != GAME_EXE_BYTES)
        rt_log("warning: sh3.exe is %ld bytes; this build's recompiled code is the %ld-byte copy, so a different "
               "executable will crash (see README, \"Where the game files come from\")", bytes, (long)GAME_EXE_BYTES);
    mods_init();  /* mods/ under the game folder, before anything loads an asset */
    rt_trace = getenv("SH3_TRACE") != NULL;
    if (getenv("SH3_COVER")) cover_from = abs(atoi(getenv("SH3_COVER"))), cover_all = atoi(getenv("SH3_COVER")) < 0;
    if (getenv("SH3_ORACLE")) sscanf(getenv("SH3_ORACLE"), "%x,%u", &oracle_va, &oracle_nth);

    host_crash_handlers(on_crash);
    arena_init();
    recomp_dispatch_init();
    host_thread_start(game_thread, NULL, 16u << 20);
    for (;;) sleep(1000);
}

#ifndef __SWITCH__  /* the launcher rows and the pickers are Windows-only */
/* What a path is, for the Options "Check this copy" row: rt_game_path does the following (it owns the folder,
 * sh3.exe and disc cases and their wording), and the two other kinds a player can drop are told apart here. */
int rt_check_copy(const char *path, char *dir, size_t n, char *msg, size_t mn) {
    const char *why = rt_game_path(path, dir, n);
    const char *base = file_name(path), *dot = strrchr(base, '.');
    if (!why) {
        snprintf(msg, mn,
                 "That is an installed copy of Silent Hill 3 for PC: sh3.exe and data/ are in\n\n%s\n\n"
                 "Use it as the game folder now?", dir);
        return RT_COPY_GAME;
    }
    if (rt_last_ps2) {
        snprintf(msg, mn, "%s", why);
        return RT_COPY_PS2;
    }
    if (rt_last_pc) {
        snprintf(msg, mn, "%s", why);
        return RT_COPY_PC_DISC;
    }
    if (dot && !strcasecmp(dot + 1, "zip")) {
        if (zip_has_game(path)) {
            snprintf(msg, mn,
                     "That is the Full-Rip download: sh3.exe and data/ are inside\n\n%s\n\n"
                     "Import it now? It unpacks beside the zip.", path);
            return RT_COPY_DOWNLOAD;
        }
        snprintf(msg, mn, "That .zip has no sh3.exe and data/: it is a mod archive, not the game.\n\n"
                          "Options > Install a mod installs it.");
        return RT_COPY_MOD;
    }
    snprintf(msg, mn, "%s", why);
    return RT_COPY_OTHER;
}

void rt_open_url(const char *url) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
    (void)url;
#endif
}
#endif /* !__SWITCH__ */


#if defined(_WIN32) && defined(PORT_GUI)
#include <windows.h>  /* WINAPI and the entry point's types */
/* The release is linked as a Windows program (-mwindows), so there is no console behind the launcher. mingw's
 * entry point calls this; it hands the command line to the same main the console builds use. */
int __stdcall WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)inst;
    (void)prev;
    (void)cmd;
    (void)show;
    return main(__argc, __argv);
}
#endif
