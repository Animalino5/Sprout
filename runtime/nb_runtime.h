/*
 * nb_runtime.h — Sprout runtime library
 *
 * Public API. Each target (host, NDS, 3DS, Wii) implements these
 * functions. BASIC programs compile to C that calls these.
 *
 * The host implementation uses stdio + simple text mode. Console
 * targets use libnds / citro2d / libogc under the hood.
 */
#ifndef NB_RUNTIME_H
#define NB_RUNTIME_H

/* ── Lifecycle ────────────────────────────────────────────────────── */
void nb_init(void);
void nb_shutdown(void);
void nb_end(void);

/* ── Console / PRINT ──────────────────────────────────────────────── */
void nb_print_str(const char *s);
void nb_print_int(long v);
void nb_print_float(double v);
void nb_print_tab(void);
void nb_println(void);

/* ── Math ─────────────────────────────────────────────────────────── */
long  nb_abs(long v);
long  nb_sgn(long v);
long  nb_int(double v);
long  nb_fix(double v);
double nb_sqr(double v);
double nb_sin(double v);
double nb_cos(double v);
double nb_tan(double v);
double nb_atn(double v);
double nb_atan2(double y, double x);
double nb_exp(double v);
double nb_log(double v);
long  nb_rnd(long n);
double nb_rndf(void);
long  nb_min(long a, long b);
long  nb_max(long a, long b);
long  nb_clamp(long v, long lo, long hi);
double nb_lerp(double a, double b, double t);
double nb_dist(double x1, double y1, double x2, double y2);
double nb_deg(double rad);
double nb_rad(double deg);
double nb_pow(double a, double b);
long  nb_idiv(long a, long b);
void  nb_randomize(long seed);

/* ── String ───────────────────────────────────────────────────────── */
long  nb_len(const char *s);
const char *nb_left(const char *s, long n);
const char *nb_right(const char *s, long n);
const char *nb_mid(const char *s, long start, long len);
const char *nb_ucase(const char *s);
const char *nb_lcase(const char *s);
long  nb_instr(const char *s, const char *sub);
long  nb_val(const char *s);
const char *nb_str(long v);
const char *nb_chr(long code);
long  nb_asc(const char *s);
void  nb_inkey(char *dst);
void  nb_str_assign(char *dst, const char *src);
const char *nb_strcat(const char *a, const char *b);

/* ── Console / text ───────────────────────────────────────────────── */
void nb_locate(long row, long col);
void nb_color(long fg, long bg);
void nb_cls(long color);
long nb_csrlin(void);
long nb_pos(long dummy);

/* ── Screen selection ─────────────────────────────────────────────── */
/* SETSCREEN 0 — draw to top screen (400x240)
 * SETSCREEN 1 — draw to bottom screen (320x240)
 * PRINT also goes to whichever screen is active.
 * The bottom screen is no longer a debug-only console — it's a full
 * drawing target. CLS, TEXT, CIRCLE, etc. all go to the active screen. */
void nb_set_screen(long screen);

/* ── Input ────────────────────────────────────────────────────────── */
long nb_button(long b);
long nb_button_down(long b);
long nb_button_up(long b);
long nb_touch_x(void);
long nb_touch_y(void);
long nb_touch_state(void);
long nb_touchdown(void);
long nb_touchpressed(void);
long nb_touchreleased(void);
void nb_gettouch(long *x, long *y);
void nb_getcirclepad(long *dx, long *dy);

/* ── Graphics ─────────────────────────────────────────────────────── */
long nb_rgb(long r, long g, long b);
long nb_point(long x, long y);
void nb_pixel(long x, long y, long color);
void nb_text(long x, long y, const char *s);
void nb_text_size(long size);  /* 1=small, 2=medium, 3=large */

/* Custom fonts — load TTF font, set as active for TEXT commands */
long nb_load_font(const char *filename);
void nb_set_font(long font);
void nb_line(long x1, long y1, long x2, long y2);
void nb_rect(long x, long y, long w, long h, long fill);
void nb_circle(long x, long y, long r, long fill);
void nb_triangle(long x1, long y1, long x2, long y2, long x3, long y3, long fill);
void nb_ellipse(long x, long y, long w, long h, long fill);

/* Transform state — applied to all subsequent drawing.
 *
 *   TRANSLATE x, y      — offset origin (pixels)
 *   ROTATE   angle      — rotation in degrees (clockwise, 0 = no rotation)
 *   RESIZE   sx, sy     — scale factors as percent (100 = 1x, 50 = half,
 *                         200 = double). sx applies horizontally, sy vertically.
 *
 * Order of application to each draw vertex: scale → rotate → translate.
 * So TRANSLATE moves the final result, ROTATE spins around the local
 * origin (0,0), and RESIZE stretches coordinates before rotation.
 *
 * Reset all three to identity with:
 *   TRANSLATE 0, 0
 *   ROTATE 0
 *   RESIZE 100, 100
 */
void nb_translate(long x, long y);
void nb_rotate(long angle_deg);
void nb_resize(long scale_x, long scale_y);

/* ── Images ───────────────────────────────────────────────────────── */
long nb_load_image(const char *filename);
void nb_draw_image(long img, long x, long y);
void nb_draw_image_ex(long img, long x, long y, long scale, long angle);
void nb_free_image(long img);
long nb_image_w(long img);
long nb_image_h(long img);

/* ── Sprite Sheets ────────────────────────────────────────────────── */
/* A sprite sheet is one PNG/T3X containing multiple frames of the same
 * size, laid out in a grid. Used for animation. */
long nb_load_sheet(const char *filename, long frame_w, long frame_h);
void nb_draw_sheet(long sheet, long x, long y, long frame);
void nb_draw_sheet_anim(long sheet, long x, long y, long start_frame, long fps);
void nb_free_sheet(long sheet);
long nb_sheet_frame_count(long sheet);

/* ── Audio ────────────────────────────────────────────────────────── */
/* Sound effects: short, loaded fully into RAM. WAV format. */
long nb_load_sound(const char *filename);
void nb_play_sound(long snd);
void nb_stop_sound(long snd);

/* Music: long, streamed from disk. OGG format. */
long nb_load_music(const char *filename);
void nb_play_music(long mus);
void nb_stop_music(void);
void nb_pause_music(void);
void nb_resume_music(void);
void nb_vol_music(long vol);   /* 0-100 */

/* ── Files ────────────────────────────────────────────────────────── */
/* Sequential I/O — one open file at a time. Simple like classic BASIC.
 *
 *   OPENW "save.dat"       — open for writing
 *   WRITE score            — write a number
 *   WRITE name$            — write a string (emitter picks str vs int)
 *   CLOSE                  — close the file
 *
 *   OPENR "save.dat"       — open for reading
 *   score = READNUM()      — read a number
 *   name$ = READLINE$()    — read a string
 *   CLOSE                  — close the file
 */
void nb_open_write(const char *filename);
void nb_open_read(const char *filename);
void nb_write_int(long v);
void nb_write_float(double v);
void nb_write_str(const char *s);
long nb_read_num(void);
const char *nb_read_line(void);
void nb_close(void);
long nb_exists(const char *filename);
void nb_delete(const char *filename);
long nb_eof(long handle);
long nb_lof(long handle);

/* ── Timing ───────────────────────────────────────────────────────── */
long nb_mseconds(void);
long nb_framecount(void);
void nb_waitkey(void);
void nb_waitframe(void);
void nb_sleep(long ms);

/* ── Rooms ────────────────────────────────────────────────────────── */
typedef void (*nb_room_fn)(void);
void nb_gotoroom(nb_room_fn init, nb_room_fn update);

/* ── Misc ─────────────────────────────────────────────────────────── */
void nb_screen(long n);
void nb_update(void);
void nb_draw(void);

/* ── Constants (button codes) ─────────────────────────────────────── */
#define NB_BTN_A      1
#define NB_BTN_B      2
#define NB_BTN_X      3
#define NB_BTN_Y      4
#define NB_BTN_L      5
#define NB_BTN_R      6
#define NB_BTN_START  7
#define NB_BTN_SELECT 8
#define NB_BTN_UP     9
#define NB_BTN_DOWN   10
#define NB_BTN_LEFT   11
#define NB_BTN_RIGHT  12

#endif /* NB_RUNTIME_H */
