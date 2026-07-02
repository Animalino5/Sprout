/*
 * nb_runtime.c — 3DS runtime implementation with citro2d graphics
 *
 * Architecture:
 *   - Top screen (GFX_TOP): citro2d render target for game graphics
 *   - Bottom screen (GFX_BOTTOM): text console for PRINT (debug output)
 *
 * Graphics model:
 *   - CLS color: sets the background clear color for the next frame
 *   - TEXT/LINE/RECT/CIRCLE: draw immediately to the current frame's
 *     render target (which was started at the end of the previous
 *     WAITFRAME)
 *   - WAITFRAME: presents the current frame (C3D_FrameEnd), then starts
 *     the next frame (C3D_FrameBegin + C2D_TargetClear + C2D_SceneBegin)
 *
 * This means the BASIC game loop:
 *   WHILE TRUE
 *       CLS BLACK
 *       DRAWSTUFF
 *       WAITFRAME
 *   WEND
 *
 * Works correctly: CLS sets the bg color, draws appear on the back
 * buffer, WAITFRAME swaps and clears.
 */
#include "nb_runtime.h"

#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

/* 3DS-specific headers */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>
/* ndsp (DSP service) for audio — sound effects and music streaming */
#include <3ds/ndsp/ndsp.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Runtime state ────────────────────────────────────────────────── */

static PrintConsole g_console_bottom;
static int   g_initialized = 0;
static long  g_frame_count = 0;
static long  g_start_ms = 0;
static u32   g_buttons_current = 0;
static u32   g_buttons_previous = 0;

/* citro2d state — both screens are full drawing targets.
 * Top screen (GFX_TOP): 400x240, for game graphics
 * Bottom screen (GFX_BOTTOM): 320x240, for game graphics (touch-enabled)
 *
 * Each screen has its own console for PRINT, but citro2d draws go to
 * the "active screen" set by SETSCREEN. */
static C3D_RenderTarget* g_screen[2] = {NULL, NULL};  /* [0]=top, [1]=bottom */
static C2D_TextBuf g_text_buf = NULL;
static int g_active_screen = 0;       /* 0=top, 1=bottom */
static u32 g_bg_color = 0xFF000000;   /* Black, opaque (ABGR8) */
static u32 g_fg_color = 0xFFFFFFFF;   /* White, opaque (ABGR8) */
static int g_c2d_initialized = 0;
static int g_frame_active = 0;

/* Transform state — applied to all draw calls.
 * Order: scale → rotate → translate.
 *   g_sx, g_sy: scale factors (1.0 = no scaling)
 *   g_rot: rotation in radians (0 = no rotation)
 *   g_cos_rot, g_sin_rot: precomputed trig of g_rot
 *   g_tx, g_ty: translation in pixels
 * A vertex (x, y) becomes:
 *   x' = (x*sx)*cos - (y*sy)*sin + tx
 *   y' = (x*sx)*sin + (y*sy)*cos + ty
 */
static float g_tx = 0.0f;
static float g_ty = 0.0f;
static float g_rot = 0.0f;
static float g_cos_rot = 1.0f;  /* cos(0) = 1 */
static float g_sin_rot = 0.0f;  /* sin(0) = 0 */
static float g_sx = 1.0f;
static float g_sy = 1.0f;

/* Text size */
static float g_text_scale = 0.5f;

/* Image handle table. Sprout uses integer handles (0, 1, 2, ...) which
 * index into this array. Each entry holds a citro2d sprite sheet. */
#define MAX_IMAGES 64
static C2D_SpriteSheet g_images[MAX_IMAGES];
static int g_num_images = 0;

/* Sprite sheet table — like images, but with frame dimensions for
 * animation. A sprite sheet is one texture containing a grid of frames. */
#define MAX_SHEETS 32
typedef struct {
    C2D_SpriteSheet sheet;
    int frame_w;
    int frame_h;
    int cols;       /* frames per row */
    int rows;       /* frames per column */
    int frame_count;
} NbSpriteSheet;
static NbSpriteSheet g_sheets[MAX_SHEETS];
static int g_num_sheets = 0;

/* Custom font table — load .bcfnt fonts for custom typography */
#define MAX_FONTS 8
static C2D_Font g_fonts[MAX_FONTS];
static int g_num_fonts = 0;
static C2D_Font g_active_font = NULL;  /* NULL = system font */

/* Audio state — uses ndsp (DSP service) for both SFX and music.
 * SFX: loaded fully into RAM as raw PCM.
 * Music: streamed from disk in chunks. */
#define MAX_SOUNDS 32
typedef struct {
    u32 *data;       /* raw PCM data (stereo, 16-bit) */
    u32 size;        /* size in bytes */
    int channels;
    int sample_rate;
    int playing;     /* 1 if currently playing */
    ndspWaveBuf wavebuf;
} NbSound;
static NbSound g_sounds[MAX_SOUNDS];
static int g_num_sounds = 0;
static int g_ndsp_initialized = 0;

typedef struct {
    FILE *file;        /* open file handle for streaming */
    long size;         /* total file size */
    long pos;          /* current read position */
    int sample_rate;
    int channels;
    int playing;
    int looping;
    ndspWaveBuf wavebufs[2];  /* double-buffered */
    u8 *buffer[2];            /* decode buffers */
    int cur_buf;
} NbMusic;
static NbMusic g_music = {0};

/* Room state */
static nb_room_fn g_room_init = NULL;
static nb_room_fn g_room_update = NULL;
static int g_room_active = 0;

/* ── Color helpers ────────────────────────────────────────────────── */

/* Our RGB() returns a packed long. citro2d wants u32 ABGR8.
 * Ensure alpha is 0xFF. */
static u32 to_abgr8(long color) {
    return (u32)color | 0xFF000000;
}

/* ── Timing helpers ───────────────────────────────────────────────── */

static long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* ── Lifecycle ────────────────────────────────────────────────────── */

/* Lazily initialize citro2d. Called on first graphics operation.
 *
 * Init sequence matches the official devkitpro citro2d example:
 *   gfxInitDefault()  — in nb_init
 *   C3D_Init()        — GPU command buffer
 *   C2D_Init()        — citro2d object pool
 *   C2D_Prepare()     — citro2d rendering state
 *   consoleInit()     — in nb_init
 *   C2D_CreateScreenTarget() — render target
 *
 * Frame model:
 *   - CLS sets the bg color, starts a frame, clears to bg
 *   - Draw commands (TEXT, RECT, CIRCLE) draw to the active frame
 *   - WAITFRAME ends the frame (presents it to screen)
 *   - Next CLS starts a new frame
 */
static void ensure_c2d_init(void) {
    if (g_c2d_initialized) return;
    g_c2d_initialized = 1;

    /* CRITICAL: must call C3D_Init BEFORE C2D_Init, and C2D_Prepare after.
     * Without these, citro2d silently fails to render anything. */
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    /* Create render targets for BOTH screens */
    g_screen[0] = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_screen[1] = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_text_buf = C2D_TextBufNew(8192);
}

void nb_init(void) {
    if (g_initialized) return;
    g_initialized = 1;

    gfxInitDefault();

    /* Initialize RomFS so LOADIMAGE can access romfs:/ paths */
    Result rc = romfsInit();
    if (R_FAILED(rc)) {
        fprintf(stderr, "[sprout] romfsInit failed: %lu\n", (unsigned long)rc);
    }

    /* Initialize ndsp (DSP service) for audio.
     * Required for both sound effects (in RAM) and music (streaming). */
    rc = ndspInit();
    if (R_FAILED(rc)) {
        fprintf(stderr, "[sprout] ndspInit failed: %lu (audio disabled)\n",
                (unsigned long)rc);
    } else {
        g_ndsp_initialized = 1;
        /* Set up channel 0 for sound effects, channel 1 for music */
        ndspChnReset(0);
        ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
        ndspChnSetRate(0, 22050);
        ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
        ndspChnReset(1);
        ndspChnSetInterp(1, NDSP_INTERP_POLYPHASE);
        ndspChnSetRate(1, 44100);
        ndspChnSetFormat(1, NDSP_FORMAT_STEREO_PCM16);
    }

    /* No console init — both screens are citro2d targets now.
     * PRINT still works via printf, but goes to wherever the OS
     * directs stdout (usually not visible without a console). */

    g_frame_count = 0;
    g_start_ms = now_ms();
    srand((unsigned)time(NULL));
    g_num_images = 0;
    g_num_sheets = 0;
    g_num_sounds = 0;
    memset(&g_music, 0, sizeof(g_music));
    /* citro2d is lazy — initialized on first CLS/draw call */
}

void nb_shutdown(void) {
    if (!g_initialized) return;
    g_initialized = 0;

    if (g_c2d_initialized) {
        if (g_frame_active) {
            C2D_Flush();
            C3D_FrameEnd(0);
            g_frame_active = 0;
        }
        /* Free all loaded images */
        for (int i = 0; i < g_num_images; i++) {
            if (g_images[i]) {
                C2D_SpriteSheetFree(g_images[i]);
                g_images[i] = NULL;
            }
        }
        g_num_images = 0;
        /* Free all loaded sprite sheets */
        for (int i = 0; i < g_num_sheets; i++) {
            if (g_sheets[i].sheet) {
                C2D_SpriteSheetFree(g_sheets[i].sheet);
                g_sheets[i].sheet = NULL;
            }
        }
        g_num_sheets = 0;
        /* Free custom fonts */
        for (int i = 0; i < g_num_fonts; i++) {
            if (g_fonts[i]) {
                C2D_FontFree(g_fonts[i]);
                g_fonts[i] = NULL;
            }
        }
        g_num_fonts = 0;
        if (g_text_buf) {
            C2D_TextBufDelete(g_text_buf);
            g_text_buf = NULL;
        }
        C2D_Fini();
        C3D_Fini();
        g_c2d_initialized = 0;
    }

    /* Free all sound effects */
    for (int i = 0; i < g_num_sounds; i++) {
        if (g_sounds[i].data) {
            linearFree(g_sounds[i].data);
            g_sounds[i].data = NULL;
        }
    }
    g_num_sounds = 0;

    /* Stop and free music */
    if (g_music.playing) {
        ndspChnWaveBufClear(1);
        g_music.playing = 0;
    }
    if (g_music.file) {
        fclose(g_music.file);
        g_music.file = NULL;
    }
    if (g_music.buffer[0]) linearFree(g_music.buffer[0]);
    if (g_music.buffer[1]) linearFree(g_music.buffer[1]);

    if (g_ndsp_initialized) {
        ndspExit();
        g_ndsp_initialized = 0;
    }
    romfsExit();
    gfxExit();
}

void nb_end(void) {
    nb_shutdown();
    exit(0);
}

/* Start a frame if one isn't already active. Called by draw functions. */
static void ensure_frame_begin(void) {
    ensure_c2d_init();
    if (g_frame_active) return;
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(g_screen[g_active_screen], g_bg_color);
    C2D_SceneBegin(g_screen[g_active_screen]);
    g_frame_active = 1;
}

/* End the active frame (if any). Called by WAITFRAME/WAITKEY. */
static void end_frame_if_active(void) {
    if (!g_frame_active) return;
    C2D_Flush();
    C3D_FrameEnd(0);
    g_frame_active = 0;
}

/* ── Console / PRINT ──────────────────────────────────────────────── */
/* PRINT goes to the bottom screen console (debug output) */

void nb_print_str(const char *s) {
    fputs(s ? s : "", stdout);
}

void nb_print_int(long v) {
    printf("%ld", v);
}

void nb_print_float(double v) {
    printf("%g", v);
}

void nb_print_tab(void) {
    fputs("    ", stdout);
}

void nb_println(void) {
    fputc('\n', stdout);
    fflush(stdout);
}

/* ── Math ─────────────────────────────────────────────────────────── */

long nb_abs(long v) { return v < 0 ? -v : v; }
long nb_sgn(long v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); }
long nb_int(double v) { return (long)floor(v); }
long nb_fix(double v) { return (long)trunc(v); }
double nb_sqr(double v) { return sqrt(v); }
double nb_sin(double v) { return sin(v); }
double nb_cos(double v) { return cos(v); }
double nb_tan(double v) { return tan(v); }
double nb_atn(double v) { return atan(v); }
double nb_atan2(double y, double x) { return atan2(y, x); }
double nb_exp(double v) { return exp(v); }
double nb_log(double v) { return log(v); }

long nb_rnd(long n) {
    if (n <= 0) return 0;
    return rand() % n;
}

double nb_rndf(void) {
    return (double)rand() / (double)RAND_MAX;
}

long nb_min(long a, long b) { return a < b ? a : b; }
long nb_max(long a, long b) { return a > b ? a : b; }
long nb_clamp(long v, long lo, long hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
double nb_lerp(double a, double b, double t) { return a + (b - a) * t; }
double nb_dist(double x1, double y1, double x2, double y2) {
    double dx = x2 - x1, dy = y2 - y1;
    return sqrt(dx*dx + dy*dy);
}
double nb_deg(double rad) { return rad * 180.0 / M_PI; }
double nb_rad(double deg) { return deg * M_PI / 180.0; }
double nb_pow(double a, double b) { return pow(a, b); }
long nb_idiv(long a, long b) {
    if (b == 0) return 0;
    return a / b;
}
void nb_randomize(long seed) { srand((unsigned)seed); }

/* ── String ───────────────────────────────────────────────────────── */

static char g_str_buf[8][256];
static int  g_str_buf_idx = 0;

static char *next_str_buf(void) {
    char *buf = g_str_buf[g_str_buf_idx];
    g_str_buf_idx = (g_str_buf_idx + 1) % 8;
    return buf;
}

long nb_len(const char *s) { return (long)(s ? strlen(s) : 0); }

const char *nb_left(const char *s, long n) {
    char *buf = next_str_buf();
    if (!s || n < 0) { buf[0] = '\0'; return buf; }
    long len = (long)strlen(s);
    if (n > len) n = len;
    memcpy(buf, s, (size_t)n);
    buf[n] = '\0';
    return buf;
}

const char *nb_right(const char *s, long n) {
    char *buf = next_str_buf();
    if (!s || n < 0) { buf[0] = '\0'; return buf; }
    long len = (long)strlen(s);
    if (n > len) n = len;
    memcpy(buf, s + len - n, (size_t)n);
    buf[n] = '\0';
    return buf;
}

const char *nb_mid(const char *s, long start, long len) {
    char *buf = next_str_buf();
    if (!s || start < 0 || len < 0) { buf[0] = '\0'; return buf; }
    long slen = (long)strlen(s);
    if (start >= slen) { buf[0] = '\0'; return buf; }
    if (start + len > slen) len = slen - start;
    memcpy(buf, s + start, (size_t)len);
    buf[len] = '\0';
    return buf;
}

const char *nb_ucase(const char *s) {
    char *buf = next_str_buf();
    if (!s) { buf[0] = '\0'; return buf; }
    strncpy(buf, s, 255);
    buf[255] = '\0';
    for (char *p = buf; *p; p++) *p = toupper((unsigned char)*p);
    return buf;
}

const char *nb_lcase(const char *s) {
    char *buf = next_str_buf();
    if (!s) { buf[0] = '\0'; return buf; }
    strncpy(buf, s, 255);
    buf[255] = '\0';
    for (char *p = buf; *p; p++) *p = tolower((unsigned char)*p);
    return buf;
}

long nb_instr(const char *s, const char *sub) {
    if (!s || !sub) return 0;
    const char *p = strstr(s, sub);
    return p ? (long)(p - s) + 1 : 0;
}

long nb_val(const char *s) {
    if (!s) return 0;
    return (long)atol(s);
}

const char *nb_str(long v) {
    char *buf = next_str_buf();
    snprintf(buf, 256, "%ld", v);
    return buf;
}

const char *nb_chr(long code) {
    char *buf = next_str_buf();
    if (code < 0 || code > 255) code = 0;
    buf[0] = (char)code;
    buf[1] = '\0';
    return buf;
}

long nb_asc(const char *s) {
    if (!s || !*s) return 0;
    return (long)(unsigned char)s[0];
}

void nb_inkey(char *dst) {
    dst[0] = '\0';
}

void nb_str_assign(char *dst, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, 255);
    dst[255] = '\0';
}

const char *nb_strcat(const char *a, const char *b) {
    char *buf = next_str_buf();
    snprintf(buf, 256, "%s%s", a ? a : "", b ? b : "");
    return buf;
}

/* ── Console ──────────────────────────────────────────────────────── */

void nb_locate(long row, long col) {
    printf("\x1b[%ld;%ldH", row, col);
}

void nb_color(long fg, long bg) {
    g_fg_color = to_abgr8(fg);
    (void)bg;  /* bg not used for console on 3DS v1 */
}

void nb_cls(long color) {
    /* Set bg color and start a new frame (clears to bg).
     * If a frame is already active, end it first so we start fresh. */
    ensure_c2d_init();
    end_frame_if_active();
    g_bg_color = to_abgr8(color);
    ensure_frame_begin();
}

long nb_csrlin(void) { return 1; }
long nb_pos(long dummy) { (void)dummy; return 1; }

/* ── Input ────────────────────────────────────────────────────────── */

static u32 nb_btn_to_3ds(long b) {
    switch (b) {
        case NB_BTN_A:      return KEY_A;
        case NB_BTN_B:      return KEY_B;
        case NB_BTN_X:      return KEY_X;
        case NB_BTN_Y:      return KEY_Y;
        case NB_BTN_L:      return KEY_L;
        case NB_BTN_R:      return KEY_R;
        case NB_BTN_START:  return KEY_START;
        case NB_BTN_SELECT: return KEY_SELECT;
        case NB_BTN_UP:     return KEY_DUP;
        case NB_BTN_DOWN:   return KEY_DDOWN;
        case NB_BTN_LEFT:   return KEY_DLEFT;
        case NB_BTN_RIGHT:  return KEY_DRIGHT;
        default:            return 0;
    }
}

long nb_button(long b) {
    return (g_buttons_current & nb_btn_to_3ds(b)) ? 1 : 0;
}

long nb_button_down(long b) {
    u32 mask = nb_btn_to_3ds(b);
    return ((g_buttons_current & mask) && !(g_buttons_previous & mask)) ? 1 : 0;
}

long nb_button_up(long b) {
    u32 mask = nb_btn_to_3ds(b);
    return (!(g_buttons_current & mask) && (g_buttons_previous & mask)) ? 1 : 0;
}

long nb_touch_x(void) {
    touchPosition pos;
    hidTouchRead(&pos);
    return (long)pos.px;
}

long nb_touch_y(void) {
    touchPosition pos;
    hidTouchRead(&pos);
    return (long)pos.py;
}

long nb_touch_state(void) {
    return (g_buttons_current & KEY_TOUCH) ? 1 : 0;
}

long nb_touchdown(void) {
    return nb_touch_state();
}

long nb_touchpressed(void) {
    return ((g_buttons_current & KEY_TOUCH) && !(g_buttons_previous & KEY_TOUCH)) ? 1 : 0;
}

long nb_touchreleased(void) {
    return (!(g_buttons_current & KEY_TOUCH) && (g_buttons_previous & KEY_TOUCH)) ? 1 : 0;
}

void nb_gettouch(long *x, long *y) {
    touchPosition pos;
    hidTouchRead(&pos);
    if (x) *x = (long)pos.px;
    if (y) *y = (long)pos.py;
}

void nb_getcirclepad(long *dx, long *dy) {
    circlePosition pos;
    hidCircleRead(&pos);
    if (dx) *dx = (long)pos.dx;
    if (dy) *dy = (long)pos.dy;
}

/* ── Graphics — citro2d implementation ────────────────────────────── */

long nb_rgb(long r, long g, long b) {
    /* Pack as ABGR8 (citro2d format): 0xAA_BB_GG_RR */
    return (long)(0xFF000000 |
                  ((u32)(r & 0xFF)) |
                  ((u32)(g & 0xFF) << 8) |
                  ((u32)(b & 0xFF) << 16));
}

long nb_point(long x, long y) {
    (void)x; (void)y;
    return 0;  /* Reading pixels back from GPU isn't trivial */
}

void nb_translate(long x, long y) {
    g_tx = (float)x;
    g_ty = (float)y;
}

void nb_rotate(long angle_deg) {
    /* Normalize to [0, 360) for stable trig, then convert to radians.
     * citro2d's C2D_SpriteSetRotation rotates clockwise, matching the
     * conventional screen-coordinate system (y points down). */
    float a = (float)angle_deg;
    while (a < 0.0f)    a += 360.0f;
    while (a >= 360.0f) a -= 360.0f;
    g_rot = a * 3.14159265f / 180.0f;
    g_cos_rot = cosf(g_rot);
    g_sin_rot = sinf(g_rot);
}

void nb_resize(long scale_x, long scale_y) {
    /* Percent-based: 100 = 1x. Clamp to a sane range to avoid div-by-zero
     * or pathological blowups if the user passes 0 or huge values. */
    if (scale_x <= 0)    scale_x = 100;
    if (scale_y <= 0)    scale_y = 100;
    if (scale_x > 10000) scale_x = 10000;
    if (scale_y > 10000) scale_y = 10000;
    g_sx = (float)scale_x / 100.0f;
    g_sy = (float)scale_y / 100.0f;
}

/* ── Transform helpers ──────────────────────────────────────────────
 * Apply the current transform (scale → rotate → translate) to a point.
 * Inlined by the compiler at -O2; cheap enough to call per-vertex. */

static inline float tf_x(float x, float y) {
    float sx = x * g_sx;
    float sy = y * g_sy;
    return sx * g_cos_rot - sy * g_sin_rot + g_tx;
}

static inline float tf_y(float x, float y) {
    float sx = x * g_sx;
    float sy = y * g_sy;
    return sx * g_sin_rot + sy * g_cos_rot + g_ty;
}

/* True when no rotation or scaling is active — only translation (or
 * identity). Draw functions use this to take the fast existing path
 * (just add g_tx/g_ty) and skip vertex transforms. */
static inline int transform_is_affine_only(void) {
    return g_rot == 0.0f && g_sx == 1.0f && g_sy == 1.0f;
}

/* Draw a filled polygon (convex or fan-able) as a triangle fan.
 * Used to render rotated/scaled CIRCLE and ELLIPSE. citro2d has no
 * direct rotated-ellipse primitive, so we tessellate. */
static void draw_poly_fan(const float *xs, const float *ys, int n, u32 color) {
    /* C2D_DrawTriangle takes 3 vertices + colors + depth. Fan from
     * vertex 0 to (i, i+1) for i in [1, n-2]. */
    for (int i = 1; i < n - 1; i++) {
        C2D_DrawTriangle(xs[0], ys[0], color,
                         xs[i], ys[i], color,
                         xs[i + 1], ys[i + 1], color, 0.5f);
    }
}

static void draw_poly_outline(const float *xs, const float *ys, int n, u32 color) {
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        C2D_DrawLine(xs[i], ys[i], color,
                     xs[j], ys[j], color, 1.0f, 0.5f);
    }
}

void nb_text_size(long size) {
    if (size <= 1) g_text_scale = 0.5f;
    else if (size == 2) g_text_scale = 1.0f;
    else g_text_scale = 1.5f;
}

/* Custom font support — LOADFONT/FONT load .bcfnt fonts.
 * TTF files in assets/ are converted to .bcfnt by the build script.
 * Variables declared above with the other graphics state. */
long nb_load_font(const char *filename) {
    if (g_num_fonts >= MAX_FONTS) return -1;
    if (!filename) return -1;

    /* Convert filename to romfs path.
     * TTF → bcfnt (build script converts), or use as-is if already .bcfnt */
    char path[256];
    const char *base = filename;
    const char *slash = strrchr(filename, '/');
    if (slash) base = slash + 1;
    size_t len = strlen(base);

    if (len >= 4 && strcasecmp(base + len - 4, ".ttf") == 0) {
        /* TTF → bcfnt */
        snprintf(path, sizeof(path), "romfs:/%.*s.bcfnt", (int)(len - 4), base);
    } else if (len >= 6 && strcasecmp(base + len - 6, ".bcfnt") == 0) {
        /* Already bcfnt */
        snprintf(path, sizeof(path), "romfs:/%s", base);
    } else {
        /* Try as-is */
        snprintf(path, sizeof(path), "romfs:/%s", base);
    }

    C2D_Font font = C2D_FontLoad(path);
    if (!font) {
        fprintf(stderr, "[sprout] LOADFONT: failed to load %s (romfs: %s)\n",
                filename, path);
        return -1;
    }

    int idx = g_num_fonts;
    g_fonts[idx] = font;
    g_num_fonts++;
    return (long)idx;
}

void nb_set_font(long font) {
    if (font >= 0 && font < g_num_fonts) {
        g_active_font = g_fonts[font];
    } else {
        g_active_font = NULL;  /* Reset to system font */
    }
}

void nb_pixel(long x, long y, long color) {
    ensure_frame_begin();
    if (transform_is_affine_only()) {
        C2D_DrawRectSolid((float)x + g_tx, (float)y + g_ty, 0.5f, 1.0f, 1.0f, to_abgr8(color));
    } else {
        /* Treat the pixel as a 1×1 rect and transform its 4 corners. */
        u32 c = to_abgr8(color);
        float x0 = tf_x((float)x,       (float)y);
        float y0 = tf_y((float)x,       (float)y);
        float x1 = tf_x((float)x + 1.0f, (float)y);
        float y1 = tf_y((float)x + 1.0f, (float)y);
        float x2 = tf_x((float)x + 1.0f, (float)y + 1.0f);
        float y2 = tf_y((float)x + 1.0f, (float)y + 1.0f);
        float x3 = tf_x((float)x,       (float)y + 1.0f);
        float y3 = tf_y((float)x,       (float)y + 1.0f);
        C2D_DrawTriangle(x0, y0, c, x1, y1, c, x2, y2, c, 0.5f);
        C2D_DrawTriangle(x0, y0, c, x2, y2, c, x3, y3, c, 0.5f);
    }
}

void nb_text(long x, long y, const char *s) {
    if (!s || !*s) return;
    ensure_frame_begin();
    C2D_Text text;
    C2D_TextBufClear(g_text_buf);
    if (g_active_font) {
        C2D_TextFontParse(&text, g_active_font, g_text_buf, s);
    } else {
        C2D_TextParse(&text, g_text_buf, s);
    }
    /* Translate the text origin through the full transform. Text scale
     * is multiplied by g_sx/g_sy so RESIZE grows/shrinks text. Rotation
     * is NOT applied to the glyphs themselves (citro2d has no rotated-
     * text primitive), but the text anchor position is rotated, so a
     * label placed at (50, 0) under ROTATE 90 lands at (0, 50). */
    float px = tf_x((float)x, (float)y);
    float py = tf_y((float)x, (float)y);
    float sx = g_text_scale * g_sx;
    float sy = g_text_scale * g_sy;
    C2D_DrawText(&text, C2D_WithColor, px, py, 0.5f, sx, sy, g_fg_color);
}

void nb_line(long x1, long y1, long x2, long y2) {
    ensure_frame_begin();
    if (transform_is_affine_only()) {
        C2D_DrawLine((float)x1 + g_tx, (float)y1 + g_ty, g_fg_color,
                     (float)x2 + g_tx, (float)y2 + g_ty, g_fg_color,
                     1.0f, 0.5f);
    } else {
        C2D_DrawLine(tf_x((float)x1, (float)y1), tf_y((float)x1, (float)y1), g_fg_color,
                     tf_x((float)x2, (float)y2), tf_y((float)x2, (float)y2), g_fg_color,
                     1.0f, 0.5f);
    }
}

void nb_rect(long x, long y, long w, long h, long fill) {
    ensure_frame_begin();
    if (transform_is_affine_only()) {
        float fx = (float)x + g_tx, fy = (float)y + g_ty, fw = (float)w, fh = (float)h;
        if (fill) {
            C2D_DrawRectSolid(fx, fy, 0.5f, fw, fh, g_fg_color);
        } else {
            C2D_DrawRectSolid(fx,        fy,        0.5f, fw, 1.0f, g_fg_color);
            C2D_DrawRectSolid(fx,        fy+fh-1,   0.5f, fw, 1.0f, g_fg_color);
            C2D_DrawRectSolid(fx,        fy,        0.5f, 1.0f, fh, g_fg_color);
            C2D_DrawRectSolid(fx+fw-1,   fy,        0.5f, 1.0f, fh, g_fg_color);
        }
        return;
    }
    /* Rotated/scaled path: transform the 4 corners, draw as 2 triangles
     * (fill) or 4 lines (outline). */
    float x0 = tf_x((float)x,       (float)y);
    float y0 = tf_y((float)x,       (float)y);
    float x1 = tf_x((float)x + w,   (float)y);
    float y1 = tf_y((float)x + w,   (float)y);
    float x2 = tf_x((float)x + w,   (float)y + h);
    float y2 = tf_y((float)x + w,   (float)y + h);
    float x3 = tf_x((float)x,       (float)y + h);
    float y3 = tf_y((float)x,       (float)y + h);
    if (fill) {
        C2D_DrawTriangle(x0, y0, g_fg_color, x1, y1, g_fg_color, x2, y2, g_fg_color, 0.5f);
        C2D_DrawTriangle(x0, y0, g_fg_color, x2, y2, g_fg_color, x3, y3, g_fg_color, 0.5f);
    } else {
        C2D_DrawLine(x0, y0, g_fg_color, x1, y1, g_fg_color, 1.0f, 0.5f);
        C2D_DrawLine(x1, y1, g_fg_color, x2, y2, g_fg_color, 1.0f, 0.5f);
        C2D_DrawLine(x2, y2, g_fg_color, x3, y3, g_fg_color, 1.0f, 0.5f);
        C2D_DrawLine(x3, y3, g_fg_color, x0, y0, g_fg_color, 1.0f, 0.5f);
    }
}

void nb_circle(long x, long y, long r, long fill) {
    ensure_frame_begin();
    if (transform_is_affine_only()) {
        if (fill) {
            C2D_DrawCircleSolid((float)x + g_tx, (float)y + g_ty, 0.5f, (float)r, g_fg_color);
        } else {
            C2D_DrawCircle((float)x + g_tx, (float)y + g_ty, 0.5f, (float)r,
                           g_fg_color, g_fg_color, g_fg_color, g_fg_color);
        }
        return;
    }
    /* Rotated/scaled circle → tessellate as a 32-gon. Non-uniform scale
     * turns it into an ellipse; rotation spins that ellipse. */
    #define CIRCLE_SEGS 32
    float xs[CIRCLE_SEGS], ys[CIRCLE_SEGS];
    for (int i = 0; i < CIRCLE_SEGS; i++) {
        float a = (float)i * (2.0f * 3.14159265f / CIRCLE_SEGS);
        float px = (float)x + cosf(a) * (float)r;
        float py = (float)y + sinf(a) * (float)r;
        xs[i] = tf_x(px, py);
        ys[i] = tf_y(px, py);
    }
    if (fill) draw_poly_fan(xs, ys, CIRCLE_SEGS, g_fg_color);
    else      draw_poly_outline(xs, ys, CIRCLE_SEGS, g_fg_color);
    #undef CIRCLE_SEGS
}

void nb_triangle(long x1, long y1, long x2, long y2, long x3, long y3, long fill) {
    ensure_frame_begin();
    if (transform_is_affine_only()) {
        if (fill) {
            C2D_DrawTriangle((float)x1 + g_tx, (float)y1 + g_ty, g_fg_color,
                             (float)x2 + g_tx, (float)y2 + g_ty, g_fg_color,
                             (float)x3 + g_tx, (float)y3 + g_ty, g_fg_color, 0.5f);
        } else {
            C2D_DrawLine((float)x1 + g_tx, (float)y1 + g_ty, g_fg_color,
                         (float)x2 + g_tx, (float)y2 + g_ty, g_fg_color, 1.0f, 0.5f);
            C2D_DrawLine((float)x2 + g_tx, (float)y2 + g_ty, g_fg_color,
                         (float)x3 + g_tx, (float)y3 + g_ty, g_fg_color, 1.0f, 0.5f);
            C2D_DrawLine((float)x3 + g_tx, (float)y3 + g_ty, g_fg_color,
                         (float)x1 + g_tx, (float)y1 + g_ty, g_fg_color, 1.0f, 0.5f);
        }
        return;
    }
    float xa = tf_x((float)x1, (float)y1), ya = tf_y((float)x1, (float)y1);
    float xb = tf_x((float)x2, (float)y2), yb = tf_y((float)x2, (float)y2);
    float xc = tf_x((float)x3, (float)y3), yc = tf_y((float)x3, (float)y3);
    if (fill) {
        C2D_DrawTriangle(xa, ya, g_fg_color, xb, yb, g_fg_color, xc, yc, g_fg_color, 0.5f);
    } else {
        C2D_DrawLine(xa, ya, g_fg_color, xb, yb, g_fg_color, 1.0f, 0.5f);
        C2D_DrawLine(xb, yb, g_fg_color, xc, yc, g_fg_color, 1.0f, 0.5f);
        C2D_DrawLine(xc, yc, g_fg_color, xa, ya, g_fg_color, 1.0f, 0.5f);
    }
}

void nb_ellipse(long x, long y, long w, long h, long fill) {
    ensure_frame_begin();
    if (transform_is_affine_only()) {
        if (fill) {
            C2D_DrawEllipseSolid((float)x + g_tx, (float)y + g_ty, 0.5f,
                                 (float)w, (float)h, g_fg_color);
        } else {
            C2D_DrawEllipse((float)x + g_tx, (float)y + g_ty, 0.5f,
                            (float)w, (float)h,
                            g_fg_color, g_fg_color, g_fg_color, g_fg_color);
        }
        return;
    }
    /* Rotated/scaled ellipse → tessellate as a 32-gon. */
    #define ELLIPSE_SEGS 32
    float xs[ELLIPSE_SEGS], ys[ELLIPSE_SEGS];
    float rx = (float)w * 0.5f;
    float ry = (float)h * 0.5f;
    for (int i = 0; i < ELLIPSE_SEGS; i++) {
        float a = (float)i * (2.0f * 3.14159265f / ELLIPSE_SEGS);
        float px = (float)x + cosf(a) * rx;
        float py = (float)y + sinf(a) * ry;
        xs[i] = tf_x(px, py);
        ys[i] = tf_y(px, py);
    }
    if (fill) draw_poly_fan(xs, ys, ELLIPSE_SEGS, g_fg_color);
    else      draw_poly_outline(xs, ys, ELLIPSE_SEGS, g_fg_color);
    #undef ELLIPSE_SEGS
}

/* ── Images — citro2d sprite sheets loaded from RomFS ─────────────── */
/*
 * Workflow:
 *   1. User puts player.png in assets/ folder next to .bas
 *   2. Build script converts player.png → player.t3x using tex3ds
 *   3. Build script bundles the .t3x into RomFS
 *   4. LOADIMAGE("player.png") → converts to "romfs:/player.t3x"
 *   5. C2D_SpriteSheetLoad loads the .t3x
 *   6. DRAWIMAGE draws it via C2D_DrawImageAt
 */

long nb_load_image(const char *filename) {
    if (g_num_images >= MAX_IMAGES) {
        fprintf(stderr, "[sprout] LOADIMAGE: too many images (max %d)\n", MAX_IMAGES);
        return -1;
    }
    if (!filename) return -1;

    /* Convert filename to romfs path.
     * "player.png" → "romfs:/player.t3x"
     * "subdir/sprite.png" → "romfs:/sprite.t3x" (strip directory)
     * "data.txt" → "romfs:/data.txt" (non-PNG, use as-is) */
    char path[256];
    const char *base = filename;
    const char *slash = strrchr(filename, '/');
    if (slash) base = slash + 1;

    size_t len = strlen(base);
    if (len >= 4 && (strcasecmp(base + len - 4, ".png") == 0)) {
        /* PNG → t3x */
        snprintf(path, sizeof(path), "romfs:/%.*s.t3x", (int)(len - 4), base);
    } else {
        /* Use as-is */
        snprintf(path, sizeof(path), "romfs:/%s", base);
    }

    ensure_c2d_init();
    C2D_SpriteSheet sheet = C2D_SpriteSheetLoad(path);
    if (!sheet) {
        fprintf(stderr, "[sprout] LOADIMAGE: failed to load %s (romfs path: %s)\n",
                filename, path);
        return -1;
    }

    int handle = g_num_images;
    g_images[handle] = sheet;
    g_num_images++;
    return (long)handle;
}

void nb_draw_image(long img, long x, long y) {
    if (img < 0 || img >= g_num_images) return;
    if (!g_images[img]) return;
    ensure_frame_begin();
    C2D_Image image = C2D_SpriteSheetGetImage(g_images[img], 0);

    if (transform_is_affine_only()) {
        /* Fast path: (x, y) is the top-left corner. */
        C2D_DrawImageAt(image, (float)x + g_tx, (float)y + g_ty, 0.5f, NULL, 1.0f, 1.0f);
    } else {
        /* Transformed path: keep (x, y) as the local top-left, but place
         * the sprite's CENTER at the transformed local-center so the
         * whole image rotates/scales around its own middle. */
        float iw = (float)image.subtex->width;
        float ih = (float)image.subtex->height;
        float cx = tf_x((float)x + iw * 0.5f, (float)y + ih * 0.5f);
        float cy = tf_y((float)x + iw * 0.5f, (float)y + ih * 0.5f);
        C2D_Sprite sprite;
        C2D_SpriteFromImage(&sprite, image);
        C2D_SpriteSetPos(&sprite, cx, cy);
        C2D_SpriteSetScale(&sprite, g_sx, g_sy);
        C2D_SpriteSetRotation(&sprite, g_rot);
        C2D_SpriteSetDepth(&sprite, 0.5f);
        C2D_DrawSprite(&sprite);
    }
}

/* DRAWIMAGEEX img, x, y, scale, angle
 * scale: percentage (100 = original size, 50 = half, 200 = double)
 * angle: degrees (0-360, clockwise)
 * Rotation is around the CENTER of the image.
 *
 * The per-call scale and angle COMPOSE with the global transform:
 *   final_scale = g_sx * scale/100  (and similarly for sy)
 *   final_rot   = g_rot + angle_radians
 *   position    = tf_x(x, y), tf_y(x, y)
 * So a global RESIZE 200,200 plus DRAWIMAGEEX ...,50,0 draws the image
 * at 1x (2.0 * 0.5 = 1.0), and a global ROTATE 90 plus DRAWIMAGEEX
 * ...,0,90 draws it at 180°. */
void nb_draw_image_ex(long img, long x, long y, long scale, long angle) {
    if (img < 0 || img >= g_num_images) return;
    if (!g_images[img]) return;
    ensure_frame_begin();
    C2D_Image image = C2D_SpriteSheetGetImage(g_images[img], 0);

    float local_s = (float)scale / 100.0f;
    float local_rot = (float)angle * 3.14159265f / 180.0f;

    C2D_Sprite sprite;
    C2D_SpriteFromImage(&sprite, image);
    C2D_SpriteSetPos(&sprite, tf_x((float)x, (float)y), tf_y((float)x, (float)y));
    C2D_SpriteSetScale(&sprite, g_sx * local_s, g_sy * local_s);
    C2D_SpriteSetRotation(&sprite, g_rot + local_rot);
    C2D_SpriteSetDepth(&sprite, 0.5f);
    C2D_DrawSprite(&sprite);
}

void nb_free_image(long img) {
    if (img < 0 || img >= g_num_images) return;
    if (g_images[img]) {
        C2D_SpriteSheetFree(g_images[img]);
        g_images[img] = NULL;
    }
}

long nb_image_w(long img) {
    if (img < 0 || img >= g_num_images || !g_images[img]) return 0;
    C2D_Image image = C2D_SpriteSheetGetImage(g_images[img], 0);
    return (long)image.subtex->width;
}

long nb_image_h(long img) {
    if (img < 0 || img >= g_num_images || !g_images[img]) return 0;
    C2D_Image image = C2D_SpriteSheetGetImage(g_images[img], 0);
    return (long)image.subtex->height;
}

/* ── Sprite Sheets — citro2d sheets with frame dimensions ─────────── */
/*
 * A sprite sheet is one texture containing a grid of animation frames.
 *   LOADSHEET("hero.png", 16, 16) — 16x16 frames
 *   DRAWSHEET sheet, x, y, frame  — draw frame N at (x, y)
 *
 * The texture is loaded as a regular image, but we slice it into
 * sub-regions based on frame_w and frame_h. Frame 0 is top-left,
 * frame 1 is to its right, etc.
 */

long nb_load_sheet(const char *filename, long frame_w, long frame_h) {
    if (g_num_sheets >= MAX_SHEETS) {
        fprintf(stderr, "[sprout] LOADSHEET: too many sheets (max %d)\n", MAX_SHEETS);
        return -1;
    }
    if (!filename || frame_w <= 0 || frame_h <= 0) {
        fprintf(stderr, "[sprout] LOADSHEET: invalid args (fw=%ld, fh=%ld)\n",
                frame_w, frame_h);
        return -1;
    }

    /* Convert filename to romfs path (PNG → t3x) */
    char path[256];
    const char *base = filename;
    const char *slash = strrchr(filename, '/');
    if (slash) base = slash + 1;
    size_t len = strlen(base);
    if (len >= 4 && (strcasecmp(base + len - 4, ".png") == 0)) {
        snprintf(path, sizeof(path), "romfs:/%.*s.t3x", (int)(len - 4), base);
    } else {
        snprintf(path, sizeof(path), "romfs:/%s", base);
    }

    ensure_c2d_init();
    fprintf(stderr, "[sprout] LOADSHEET: loading %s (romfs: %s, fw=%ld, fh=%ld)\n",
            filename, path, frame_w, frame_h);
    C2D_SpriteSheet sheet = C2D_SpriteSheetLoad(path);
    if (!sheet) {
        fprintf(stderr, "[sprout] LOADSHEET: C2D_SpriteSheetLoad FAILED for %s\n", path);
        return -1;
    }

    /* Get texture dimensions to calculate frame grid.
     * Note: tex3ds may pad textures to power-of-2, so the subtex dimensions
     * might not match the original PNG size. We use the subtex's reported
     * width/height as the actual usable texture area. */
    C2D_Image img = C2D_SpriteSheetGetImage(sheet, 0);
    int tex_w = img.subtex->width;
    int tex_h = img.subtex->height;
    fprintf(stderr, "[sprout] LOADSHEET: texture is %dx%d, frames %ldx%ld\n",
            tex_w, tex_h, frame_w, frame_h);

    int idx = g_num_sheets;
    g_sheets[idx].sheet = sheet;
    g_sheets[idx].frame_w = (int)frame_w;
    g_sheets[idx].frame_h = (int)frame_h;
    g_sheets[idx].cols = tex_w / (int)frame_w;
    if (g_sheets[idx].cols < 1) g_sheets[idx].cols = 1;
    g_sheets[idx].rows = tex_h / (int)frame_h;
    if (g_sheets[idx].rows < 1) g_sheets[idx].rows = 1;
    g_sheets[idx].frame_count = g_sheets[idx].cols * g_sheets[idx].rows;
    if (g_sheets[idx].frame_count < 1) g_sheets[idx].frame_count = 1;
    fprintf(stderr, "[sprout] LOADSHEET: grid=%dx%d, frame_count=%d\n",
            g_sheets[idx].cols, g_sheets[idx].rows, g_sheets[idx].frame_count);
    g_num_sheets++;
    return (long)idx;
}

/* Internal: draw a specific frame from a sprite sheet */
static void draw_sheet_frame(NbSpriteSheet *ss, long x, long y, long frame) {
    /* Wrap frame to valid range */
    long f = frame % ss->frame_count;
    if (f < 0) f += ss->frame_count;

    int col = (int)(f % ss->cols);
    int row = (int)(f / ss->cols);

    ensure_frame_begin();
    C2D_Image image = C2D_SpriteSheetGetImage(ss->sheet, 0);

    /* For single-frame sheets, just draw the full image */
    if (ss->frame_count <= 1) {
        C2D_DrawImageAt(image, (float)x, (float)y, 0.5f, NULL, 1.0f, 1.0f);
        return;
    }

    /* Multi-frame: draw a sub-region of the texture.
     * Use a pool of static subtex structs — they must persist until
     * C3D_FrameEnd flushes the render queue. */
    static Tex3DS_SubTexture subtex_pool[16];
    static int subtex_idx = 0;
    Tex3DS_SubTexture *sub = &subtex_pool[subtex_idx];
    subtex_idx = (subtex_idx + 1) % 16;

    sub->width = ss->frame_w;
    sub->height = ss->frame_h;

    /* Compute UV coordinates from the original subtex's UV space.
     * citro2d textures store UVs that may not be 0..1 if the texture
     * was padded to power-of-2 by tex3ds. */
    float u0 = image.subtex->left;
    float v0 = image.subtex->top;
    float u_range = image.subtex->right - u0;
    float v_range = image.subtex->bottom - v0;
    float tex_w = (float)image.subtex->width;
    float tex_h = (float)image.subtex->height;

    sub->left   = u0 + (float)(col * ss->frame_w) / tex_w * u_range;
    sub->top    = v0 + (float)(row * ss->frame_h) / tex_h * v_range;
    sub->right  = u0 + (float)((col + 1) * ss->frame_w) / tex_w * u_range;
    sub->bottom = v0 + (float)((row + 1) * ss->frame_h) / tex_h * v_range;

    C2D_Image sub_image;
    sub_image.tex = image.tex;
    sub_image.subtex = sub;

    if (transform_is_affine_only()) {
        C2D_DrawImageAt(sub_image, (float)x + g_tx, (float)y + g_ty, 0.5f, NULL, 1.0f, 1.0f);
    } else {
        float fw = (float)ss->frame_w;
        float fh = (float)ss->frame_h;
        float cx = tf_x((float)x + fw * 0.5f, (float)y + fh * 0.5f);
        float cy = tf_y((float)x + fw * 0.5f, (float)y + fh * 0.5f);
        C2D_Sprite sprite;
        C2D_SpriteFromImage(&sprite, sub_image);
        C2D_SpriteSetPos(&sprite, cx, cy);
        C2D_SpriteSetScale(&sprite, g_sx, g_sy);
        C2D_SpriteSetRotation(&sprite, g_rot);
        C2D_SpriteSetDepth(&sprite, 0.5f);
        C2D_DrawSprite(&sprite);
    }
}

void nb_draw_sheet(long sheet, long x, long y, long frame) {
    if (sheet < 0 || sheet >= g_num_sheets || !g_sheets[sheet].sheet) return;
    NbSpriteSheet *ss = &g_sheets[sheet];
    if (ss->frame_count <= 0) return;
    draw_sheet_frame(ss, x, y, frame);
}

/* Auto-animating version: the runtime advances the frame based on
 * elapsed time and the specified FPS. Each sheet has its own animation
 * timer, so different sheets animate independently. */
void nb_draw_sheet_anim(long sheet, long x, long y, long start_frame, long fps) {
    if (sheet < 0 || sheet >= g_num_sheets || !g_sheets[sheet].sheet) return;
    NbSpriteSheet *ss = &g_sheets[sheet];
    if (ss->frame_count <= 0) return;
    if (fps <= 0) fps = 1;

    /* Compute current frame based on elapsed time.
     * Each sheet tracks its own start time on first call. */
    static long anim_start_time[32] = {0};
    static int anim_inited[32] = {0};
    if (sheet < 32 && !anim_inited[sheet]) {
        anim_start_time[sheet] = nb_mseconds();
        anim_inited[sheet] = 1;
    }

    long elapsed_ms;
    if (sheet < 32) {
        elapsed_ms = nb_mseconds() - anim_start_time[sheet];
    } else {
        elapsed_ms = 0;
    }

    /* frames_to_advance = elapsed_seconds * fps */
    long frames_advanced = (elapsed_ms * fps) / 1000;
    long current_frame = start_frame + frames_advanced;

    draw_sheet_frame(ss, x, y, current_frame);
}

void nb_free_sheet(long sheet) {
    if (sheet < 0 || sheet >= g_num_sheets) return;
    if (g_sheets[sheet].sheet) {
        C2D_SpriteSheetFree(g_sheets[sheet].sheet);
        g_sheets[sheet].sheet = NULL;
    }
}

long nb_sheet_frame_count(long sheet) {
    if (sheet < 0 || sheet >= g_num_sheets) return 0;
    return (long)g_sheets[sheet].frame_count;
}

/* ── Audio — sound effects (WAV in RAM) and music (streaming) ─────── */
/*
 * Sound effects:
 *   LOADSOUND("shoot.wav") — loads entire WAV into RAM
 *   PLAYSOUND snd          — plays on channel 0
 *   STOPSOUND snd          — stops if playing
 *
 * Music:
 *   LOADMUSIC("bgm.ogg")   — opens file for streaming
 *   PLAYMUSIC mus          — starts streaming on channel 1, loops
 *   STOPMUSIC              — stops and closes file
 *   PAUSEMUSIC / RESUMEMUSIC
 *
 * Note: WAV files must be PCM 16-bit. OGG requires a decoder — for v1
 * we support raw PCM streaming (rename .pcm or use simple WAV).
 * Full OGG support would need a Tremor/STB Vorbis decoder integration.
 */

/* Simple WAV loader: parses WAV header, returns raw PCM data.
 * Supports: PCM format, 16-bit, mono or stereo. */
static u32 *load_wav(const char *path, int *out_channels,
                     int *out_sample_rate, u32 *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    /* Read WAV header (simplified — assumes standard PCM WAV) */
    char riff[4];
    fread(riff, 1, 4, f);
    if (memcmp(riff, "RIFF", 4) != 0) { fclose(f); return NULL; }

    u32 chunk_size;
    fread(&chunk_size, 4, 1, f);
    char wave[4];
    fread(wave, 1, 4, f);
    if (memcmp(wave, "WAVE", 4) != 0) { fclose(f); return NULL; }

    /* Find fmt and data chunks */
    int channels = 1, sample_rate = 22050, bits_per_sample = 16;
    u32 data_size = 0;
    u8 *data = NULL;

    while (!feof(f)) {
        char chunk_id[4];
        u32 chunk_len;
        if (fread(chunk_id, 1, 4, f) != 4) break;
        if (fread(&chunk_len, 4, 1, f) != 1) break;

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            u16 audio_format;
            fread(&audio_format, 2, 1, f);
            fread(&channels, 2, 1, f);
            fread(&sample_rate, 4, 1, f);
            u32 byte_rate;
            fread(&byte_rate, 4, 1, f);
            u16 block_align;
            fread(&block_align, 2, 1, f);
            fread(&bits_per_sample, 2, 1, f);
            /* Skip any extra fmt bytes */
            if (chunk_len > 16) fseek(f, chunk_len - 16, SEEK_CUR);
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            data_size = chunk_len;
            data = (u8 *)linearAlloc(data_size);
            if (!data) { fclose(f); return NULL; }
            fread(data, 1, data_size, f);
        } else {
            /* Skip unknown chunk */
            fseek(f, chunk_len, SEEK_CUR);
        }
    }
    fclose(f);

    if (!data) return NULL;
    *out_channels = channels;
    *out_sample_rate = sample_rate;
    *out_size = data_size;
    return (u32 *)data;
}

long nb_load_sound(const char *filename) {
    if (!g_ndsp_initialized) return -1;
    if (g_num_sounds >= MAX_SOUNDS) return -1;
    if (!filename) return -1;

    /* Convert to romfs path */
    char path[256];
    const char *base = filename;
    const char *slash = strrchr(filename, '/');
    if (slash) base = slash + 1;
    snprintf(path, sizeof(path), "romfs:/%s", base);

    int channels, sample_rate;
    u32 size;
    u32 *data = load_wav(path, &channels, &sample_rate, &size);
    if (!data) {
        fprintf(stderr, "[sprout] LOADSOUND: failed to load %s\n", filename);
        return -1;
    }

    int idx = g_num_sounds;
    g_sounds[idx].data = data;
    g_sounds[idx].size = size;
    g_sounds[idx].channels = channels;
    g_sounds[idx].sample_rate = sample_rate;
    g_sounds[idx].playing = 0;
    memset(&g_sounds[idx].wavebuf, 0, sizeof(ndspWaveBuf));
    g_num_sounds++;
    return (long)idx;
}

void nb_play_sound(long snd) {
    if (!g_ndsp_initialized) return;
    if (snd < 0 || snd >= g_num_sounds) return;
    NbSound *s = &g_sounds[snd];
    if (!s->data) return;

    /* Set channel format based on channels */
    if (s->channels == 1) {
        ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);
    } else {
        ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    }
    ndspChnSetRate(0, s->sample_rate);

    /* Set up wave buffer */
    memset(&s->wavebuf, 0, sizeof(ndspWaveBuf));
    s->wavebuf.data_vaddr = s->data;
    s->wavebuf.nsamples = s->size / (2 * s->channels);  /* 16-bit = 2 bytes */
    s->wavebuf.looping = false;

    DSP_FlushDataCache(s->data, s->size);
    ndspChnWaveBufClear(0);
    ndspChnWaveBufAdd(0, &s->wavebuf);
    s->playing = 1;
}

void nb_stop_sound(long snd) {
    if (!g_ndsp_initialized) return;
    if (snd < 0 || snd >= g_num_sounds) return;
    ndspChnWaveBufClear(0);
    g_sounds[snd].playing = 0;
}

/* ── Music (streaming) ────────────────────────────────────────────── */
/* For v1, music streaming reads raw PCM from disk in chunks.
 * The file must be raw stereo 16-bit 44100Hz PCM (no WAV header).
 * Full OGG support needs a Vorbis decoder — future work.
 *
 * To use: convert your music to raw PCM:
 *   ffmpeg -i bgm.ogg -f s16le -acodec pcm_s16le -ar 44100 -ac 2 bgm.pcm
 * Then: LOADMUSIC("bgm.pcm")
 */

#define MUSIC_BUF_SIZE (16384 * 4)  /* 64KB per buffer — larger = less stutter */

long nb_load_music(const char *filename) {
    if (!g_ndsp_initialized) return -1;
    if (!filename) return -1;

    /* Stop any current music */
    if (g_music.playing) nb_stop_music();

    /* Convert to romfs path */
    char path[256];
    const char *base = filename;
    const char *slash = strrchr(filename, '/');
    if (slash) base = slash + 1;
    snprintf(path, sizeof(path), "romfs:/%s", base);

    g_music.file = fopen(path, "rb");
    if (!g_music.file) {
        fprintf(stderr, "[sprout] LOADMUSIC: failed to open %s\n", filename);
        return -1;
    }

    fseek(g_music.file, 0, SEEK_END);
    g_music.size = ftell(g_music.file);
    fseek(g_music.file, 0, SEEK_SET);
    g_music.pos = 0;
    g_music.sample_rate = 44100;
    g_music.channels = 2;
    g_music.playing = 0;
    g_music.looping = 1;
    g_music.cur_buf = 0;

    /* Allocate two decode buffers (double-buffered streaming) */
    g_music.buffer[0] = (u8 *)linearAlloc(MUSIC_BUF_SIZE);
    g_music.buffer[1] = (u8 *)linearAlloc(MUSIC_BUF_SIZE);
    if (!g_music.buffer[0] || !g_music.buffer[1]) {
        fprintf(stderr, "[sprout] LOADMUSIC: out of memory\n");
        if (g_music.buffer[0]) linearFree(g_music.buffer[0]);
        if (g_music.buffer[1]) linearFree(g_music.buffer[1]);
        fclose(g_music.file);
        g_music.file = NULL;
        return -1;
    }

    memset(&g_music.wavebufs[0], 0, sizeof(ndspWaveBuf));
    memset(&g_music.wavebufs[1], 0, sizeof(ndspWaveBuf));

    return 1;  /* Only one music stream at a time — return 1 as handle */
}

void nb_play_music(long mus) {
    (void)mus;  /* Only one music stream — handle is always 1 */
    if (!g_ndsp_initialized || !g_music.file) return;
    if (g_music.playing) return;

    /* Fill first buffer */
    size_t read = fread(g_music.buffer[0], 1, MUSIC_BUF_SIZE, g_music.file);
    if (read == 0) {
        if (g_music.looping) {
            fseek(g_music.file, 0, SEEK_SET);
            read = fread(g_music.buffer[0], 1, MUSIC_BUF_SIZE, g_music.file);
        }
        if (read == 0) return;
    }
    g_music.pos += read;

    g_music.wavebufs[0].data_vaddr = g_music.buffer[0];
    g_music.wavebufs[0].nsamples = read / 4;  /* stereo 16-bit = 4 bytes/sample */
    g_music.wavebufs[0].looping = false;
    DSP_FlushDataCache(g_music.buffer[0], read);

    ndspChnSetFormat(1, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetRate(1, g_music.sample_rate);
    ndspChnWaveBufClear(1);
    ndspChnWaveBufAdd(1, &g_music.wavebufs[0]);
    g_music.cur_buf = 0;
    g_music.playing = 1;

    /* Preload second buffer immediately so it's ready when first finishes */
    read = fread(g_music.buffer[1], 1, MUSIC_BUF_SIZE, g_music.file);
    if (read > 0) {
        g_music.pos += read;
        g_music.wavebufs[1].data_vaddr = g_music.buffer[1];
        g_music.wavebufs[1].nsamples = read / 4;
        g_music.wavebufs[1].looping = false;
        DSP_FlushDataCache(g_music.buffer[1], read);
        ndspChnWaveBufAdd(1, &g_music.wavebufs[1]);
    }
}

/* Stream more data when current buffer finishes. Called from nb_waitframe.
 * Fills as many buffers as needed in a loop to prevent gaps. */
static void update_music_stream(void) {
    if (!g_music.playing || !g_music.file) return;

    /* Check both buffers in a loop — fill any that are done */
    for (int check = 0; check < 2; check++) {
        if (g_music.wavebufs[g_music.cur_buf].status != NDSP_WBUF_DONE) break;

        int next_buf = 1 - g_music.cur_buf;

        /* Read next chunk */
        size_t read = fread(g_music.buffer[next_buf], 1, MUSIC_BUF_SIZE, g_music.file);
        if (read == 0) {
            if (g_music.looping) {
                fseek(g_music.file, 0, SEEK_SET);
                read = fread(g_music.buffer[next_buf], 1, MUSIC_BUF_SIZE, g_music.file);
            }
            if (read == 0) {
                g_music.playing = 0;
                return;
            }
        }
        g_music.pos += read;

        g_music.wavebufs[next_buf].data_vaddr = g_music.buffer[next_buf];
        g_music.wavebufs[next_buf].nsamples = read / 4;  /* stereo 16-bit = 4 bytes/sample */
        g_music.wavebufs[next_buf].looping = false;
        DSP_FlushDataCache(g_music.buffer[next_buf], read);

        ndspChnWaveBufAdd(1, &g_music.wavebufs[next_buf]);
        g_music.cur_buf = next_buf;
    }
}

void nb_stop_music(void) {
    if (!g_ndsp_initialized) return;
    if (!g_music.file) return;
    ndspChnWaveBufClear(1);
    g_music.playing = 0;
}

void nb_pause_music(void) {
    if (!g_ndsp_initialized || !g_music.playing) return;
    ndspChnSetPaused(1, true);
}

void nb_resume_music(void) {
    if (!g_ndsp_initialized || !g_music.playing) return;
    ndspChnSetPaused(1, false);
}

void nb_vol_music(long vol) {
    if (!g_ndsp_initialized) return;
    /* Clamp 0-100, convert to 0.0-1.0 */
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    float v = (float)vol / 100.0f;
    /* Set mix: front-left, front-right, others 0 */
    float mix[12] = {0};
    mix[0] = v;  /* front left */
    mix[1] = v;  /* front right */
    ndspChnSetMix(1, mix);
}

/* ── Files — sequential I/O (one open file at a time) ────────────── */
/* Files go to sdmc:/ (SD card) on 3DS. The runtime prefixes paths. */

static FILE *g_file = NULL;
static char g_read_buf[256];

static void make_sdmc_path(char *out, size_t out_size, const char *filename) {
    /* If already starts with sdmc:/ or romfs:/, use as-is */
    if (strncmp(filename, "sdmc:/", 6) == 0 ||
        strncmp(filename, "romfs:/", 7) == 0) {
        snprintf(out, out_size, "%s", filename);
    } else {
        snprintf(out, out_size, "sdmc:/%s", filename);
    }
}

void nb_open_write(const char *filename) {
    if (g_file) fclose(g_file);
    char path[256];
    make_sdmc_path(path, sizeof(path), filename);
    g_file = fopen(path, "wb");
    if (!g_file) {
        fprintf(stderr, "[sprout] OPENW: failed to open %s\n", path);
    }
}

void nb_open_read(const char *filename) {
    if (g_file) fclose(g_file);
    char path[256];
    make_sdmc_path(path, sizeof(path), filename);
    g_file = fopen(path, "rb");
    if (!g_file) {
        fprintf(stderr, "[sprout] OPENR: failed to open %s\n", path);
    }
}

void nb_write_int(long v) {
    if (!g_file) return;
    fprintf(g_file, "%ld\n", v);
}

void nb_write_float(double v) {
    if (!g_file) return;
    fprintf(g_file, "%g\n", v);
}

void nb_write_str(const char *s) {
    if (!g_file || !s) return;
    fprintf(g_file, "%s\n", s);
}

long nb_read_num(void) {
    if (!g_file) return 0;
    if (!fgets(g_read_buf, sizeof(g_read_buf), g_file)) return 0;
    return (long)atol(g_read_buf);
}

const char *nb_read_line(void) {
    if (!g_file) return "";
    if (!fgets(g_read_buf, sizeof(g_read_buf), g_file)) return "";
    /* Strip trailing newline */
    size_t len = strlen(g_read_buf);
    if (len > 0 && g_read_buf[len-1] == '\n') g_read_buf[len-1] = '\0';
    return g_read_buf;
}

void nb_close(void) {
    if (g_file) {
        fclose(g_file);
        g_file = NULL;
    }
}

long nb_exists(const char *filename) {
    char path[256];
    make_sdmc_path(path, sizeof(path), filename);
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

void nb_delete(const char *filename) {
    char path[256];
    make_sdmc_path(path, sizeof(path), filename);
    remove(path);
}

long nb_eof(long handle) { (void)handle; return g_file ? feof(g_file) : 1; }
long nb_lof(long handle) { (void)handle; return 0; }

/* ── Timing ───────────────────────────────────────────────────────── */

long nb_mseconds(void) {
    return now_ms() - g_start_ms;
}

long nb_framecount(void) {
    return g_frame_count;
}

void nb_waitkey(void) {
    end_frame_if_active();

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

void nb_waitframe(void) {
    /* Check if the 3DS wants us to exit */
    if (!aptMainLoop()) {
        end_frame_if_active();
        if (g_c2d_initialized) {
            C2D_Fini();
            C3D_Fini();
            g_c2d_initialized = 0;
        }
        gfxExit();
        exit(0);
    }

    /* Scan input */
    g_buttons_previous = g_buttons_current;
    hidScanInput();
    g_buttons_current = hidKeysHeld();

    /* End the active frame (presents it to screen).
     * C3D_FrameEnd already syncs to vblank when C3D_FRAME_SYNCDRAW was used,
     * so we only need gspWaitForVBlank for the non-citro2d path. */
    if (g_c2d_initialized) {
        end_frame_if_active();
    } else {
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    g_frame_count++;

    /* Stream more music data if needed (double-buffered playback) */
    if (g_ndsp_initialized) {
        update_music_stream();
    }

    /* If a room is active, call its update.
     * Next CLS will start a new frame. */
    if (g_room_active && g_room_update) {
        g_room_update();
    }
}

void nb_sleep(long ms) {
    long frames = ms / 17;
    for (long i = 0; i < frames; i++) {
        gspWaitForVBlank();
    }
}

/* ── Rooms ────────────────────────────────────────────────────────── */

void nb_gotoroom(nb_room_fn init, nb_room_fn update) {
    g_room_init = init;
    g_room_update = update;
    g_room_active = 1;
    if (init) init();
}

/* ── Misc ─────────────────────────────────────────────────────────── */

void nb_set_screen(long screen) {
    /* SETSCREEN 0 = top screen (400x240)
     * SETSCREEN 1 = bottom screen (320x240) */
    if (screen < 0 || screen > 1) return;

    if (screen != g_active_screen) {
        /* End the current frame on the old screen so it gets presented. */
        end_frame_if_active();
        g_active_screen = (int)screen;
        /* Don't start a new frame here — the next CLS or draw call
         * will call ensure_frame_begin() which starts it.
         * This avoids extra vblank syncs. */
    }
}

void nb_screen(long n) {
    /* Legacy alias for SETSCREEN */
    nb_set_screen(n);
}

void nb_update(void) {}
void nb_draw(void) {}
