/*
 * nb_runtime.c — Host runtime implementation (PC, no devkitPro)
 *
 * Uses stdio for console I/O. Graphics/audio/input functions are stubs
 * that print warnings on first use. This is enough to run text-mode
 * BASIC programs on PC for testing.
 */
#include "nb_runtime.h"

#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>  /* usleep */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#endif

/* ── Runtime state ────────────────────────────────────────────────── */

static int   g_initialized = 0;
static long  g_frame_count = 0;
static long  g_start_ms = 0;
static long  g_buttons_current = 0;
static long  g_buttons_previous = 0;

/* Room state — when active, nb_waitframe calls update */
static nb_room_fn g_room_init = NULL;
static nb_room_fn g_room_update = NULL;
static int g_room_active = 0;

/* ── Timing helpers ───────────────────────────────────────────────── */

static long now_ms(void) {
#ifdef _WIN32
    return (long)GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

/* ── Lifecycle ────────────────────────────────────────────────────── */

void nb_init(void) {
    g_initialized = 1;
    g_frame_count = 0;
    g_start_ms = now_ms();
    srand((unsigned)time(NULL));
    /* On host, no special init needed. Console targets will set up
     * video mode, audio, filesystem here. */
}

void nb_shutdown(void) {
    g_initialized = 0;
}

void nb_end(void) {
    nb_shutdown();
    exit(0);
}

/* ── Console / PRINT ──────────────────────────────────────────────── */

void nb_print_str(const char *s) {
    fputs(s ? s : "", stdout);
}

void nb_print_int(long v) {
    printf("%ld", v);
}

void nb_print_float(double v) {
    /* Print with up to 6 decimals, trimming trailing zeros */
    printf("%g", v);
}

void nb_print_tab(void) {
    /* Tab to next multiple of 8 columns */
    long pos = 0;
    /* We don't track cursor position on host — just emit a few spaces */
    fputs("    ", stdout);
    (void)pos;
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

/* Thread-local-ish temp buffers for string functions that return const char*.
 * Not actually thread-local for v1, but the host runtime is single-threaded. */
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
    /* On host, this returns the next key pressed or empty string.
     * For simplicity, return empty (non-blocking). */
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
    /* ANSI escape: ESC[row;colH */
    printf("\033[%ld;%ldH", row, col);
}

void nb_color(long fg, long bg) {
    (void)fg; (void)bg;
    /* ANSI color support could go here. Stub for now. */
}

void nb_cls(long color) {
    (void)color;
    fputs("\033[2J\033[H", stdout);  /* clear screen + home cursor */
}

long nb_csrlin(void) { return 1; }
long nb_pos(long dummy) { (void)dummy; return 1; }

/* ── Input ────────────────────────────────────────────────────────── */

/* On host, we don't have real gamepad input. For testing, we just
 * return 0 (no buttons held). Real keyboard reading would go here. */

long nb_button(long b) {
    /* Host: stub — no buttons held by default */
    (void)b;
    return 0;
}

long nb_button_down(long b) {
    (void)b;
    return 0;
}

long nb_button_up(long b) {
    (void)b;
    return 0;
}

long nb_touch_x(void) { return -1; }
long nb_touch_y(void) { return -1; }
long nb_touch_state(void) { return 0; }
long nb_touchdown(void) { return 0; }
long nb_touchpressed(void) { return 0; }
long nb_touchreleased(void) { return 0; }

void nb_gettouch(long *x, long *y) {
    if (x) *x = -1;
    if (y) *y = -1;
}

void nb_getcirclepad(long *dx, long *dy) {
    if (dx) *dx = 0;
    if (dy) *dy = 0;
}

/* ── Graphics (stubs) ─────────────────────────────────────────────── */

long nb_rgb(long r, long g, long b) {
    return (r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16);
}

long nb_point(long x, long y) { (void)x; (void)y; return 0; }
void nb_pixel(long x, long y, long color) { (void)x; (void)y; (void)color; }

void nb_text(long x, long y, const char *s) {
    /* On host, just print at column position */
    printf("\033[%ld;%ldH%s", y + 1, x + 1, s ? s : "");
}

void nb_line(long x1, long y1, long x2, long y2) {
    (void)x1; (void)y1; (void)x2; (void)y2;
}

void nb_rect(long x, long y, long w, long h, long fill) {
    (void)x; (void)y; (void)w; (void)h; (void)fill;
}

void nb_circle(long x, long y, long r, long fill) {
    (void)x; (void)y; (void)r; (void)fill;
}

/* ── Images (stubs) ───────────────────────────────────────────────── */

long nb_load_image(const char *filename) {
    fprintf(stderr, "[runtime] LOADIMAGE(\"%s\") stubbed on host\n",
            filename ? filename : "");
    return 0;
}

void nb_draw_image(long img, long x, long y) {
    (void)img; (void)x; (void)y;
}

void nb_draw_image_ex(long img, long x, long y, long scale, long angle) {
    (void)img; (void)x; (void)y; (void)scale; (void)angle;
}

void nb_free_image(long img) { (void)img; }
long nb_image_w(long img) { (void)img; return 16; }
long nb_image_h(long img) { (void)img; return 16; }

/* ── Sprite Sheets (stubs on host) ────────────────────────────────── */

long nb_load_sheet(const char *filename, long frame_w, long frame_h) {
    fprintf(stderr, "[runtime] LOADSHEET(\"%s\", %ld, %ld) stubbed on host\n",
            filename ? filename : "", frame_w, frame_h);
    return 0;
}

void nb_draw_sheet(long sheet, long x, long y, long frame) {
    (void)sheet; (void)x; (void)y; (void)frame;
}

void nb_draw_sheet_anim(long sheet, long x, long y, long start_frame, long fps) {
    (void)sheet; (void)x; (void)y; (void)start_frame; (void)fps;
}

void nb_free_sheet(long sheet) { (void)sheet; }
long nb_sheet_frame_count(long sheet) { (void)sheet; return 4; }

/* ── Audio (stubs on host) ────────────────────────────────────────── */

long nb_load_sound(const char *filename) {
    fprintf(stderr, "[runtime] LOADSOUND(\"%s\") stubbed on host\n",
            filename ? filename : "");
    return 0;
}

void nb_play_sound(long snd) { (void)snd; }
void nb_stop_sound(long snd) { (void)snd; }

long nb_load_music(const char *filename) {
    fprintf(stderr, "[runtime] LOADMUSIC(\"%s\") stubbed on host\n",
            filename ? filename : "");
    return 0;
}

void nb_play_music(long snd) { (void)snd; }
void nb_stop_music(void) {}
void nb_pause_music(void) {}
void nb_resume_music(void) {}
void nb_vol_music(long vol) { (void)vol; }

/* ── Drawing additions (stubs on host) ────────────────────────────── */

void nb_translate(long x, long y) { (void)x; (void)y; }
void nb_rotate(long angle_deg) { (void)angle_deg; }
void nb_resize(long scale_x, long scale_y) { (void)scale_x; (void)scale_y; }
void nb_text_size(long size) { (void)size; }
void nb_set_screen(long screen) { (void)screen; }
long nb_load_font(const char *filename) { (void)filename; return 0; }
void nb_set_font(long font) { (void)font; }

void nb_triangle(long x1, long y1, long x2, long y2, long x3, long y3, long fill) {
    (void)x1; (void)y1; (void)x2; (void)y2; (void)x3; (void)y3; (void)fill;
}

void nb_ellipse(long x, long y, long w, long h, long fill) {
    (void)x; (void)y; (void)w; (void)h; (void)fill;
}

/* ── Files (basic implementation) ─────────────────────────────────── */

static FILE *g_file = NULL;
static char g_read_buf[256];

void nb_open_write(const char *filename) {
    if (g_file) fclose(g_file);
    g_file = fopen(filename ? filename : "tmp.dat", "wb");
}
void nb_open_read(const char *filename) {
    if (g_file) fclose(g_file);
    g_file = fopen(filename ? filename : "tmp.dat", "rb");
}
void nb_write_int(long v) { if (g_file) fprintf(g_file, "%ld\n", v); }
void nb_write_float(double v) { if (g_file) fprintf(g_file, "%g\n", v); }
void nb_write_str(const char *s) { if (g_file && s) fprintf(g_file, "%s\n", s); }
long nb_read_num(void) {
    if (!g_file) return 0;
    if (!fgets(g_read_buf, sizeof(g_read_buf), g_file)) return 0;
    return (long)atol(g_read_buf);
}
const char *nb_read_line(void) {
    if (!g_file) return "";
    if (!fgets(g_read_buf, sizeof(g_read_buf), g_file)) return "";
    size_t len = strlen(g_read_buf);
    if (len > 0 && g_read_buf[len-1] == '\n') g_read_buf[len-1] = '\0';
    return g_read_buf;
}
void nb_close(void) { if (g_file) { fclose(g_file); g_file = NULL; } }

long nb_exists(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

void nb_delete(const char *filename) {
    remove(filename);
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
    fflush(stdout);
    fgetc(stdin);
}

void nb_waitframe(void) {
    g_frame_count++;
    g_buttons_previous = g_buttons_current;
    /* On host, no real frame rate. Just yield. */
#ifdef _WIN32
    Sleep(1);
#else
    usleep(1000);
#endif

    /* If a room is active, call its update */
    if (g_room_active && g_room_update) {
        g_room_update();
    }
}

void nb_sleep(long ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

/* ── Rooms ────────────────────────────────────────────────────────── */

void nb_gotoroom(nb_room_fn init, nb_room_fn update) {
    /* Free old room's assets (no-op on host for now) */
    g_room_init = init;
    g_room_update = update;
    g_room_active = 1;
    if (init) init();
}

/* ── Misc ─────────────────────────────────────────────────────────── */

void nb_screen(long n) { (void)n; }
void nb_update(void) {}
void nb_draw(void) {}
