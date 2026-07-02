/* sproutc.c — Sprout compiler driver
 *
 * Usage: sproutc <input.bas> [-o <output.c>] [--target=host|3ds|nds|wii]
 *
 * Reads a .bas file, lexes, parses, type-checks, and emits C code.
 * By default writes to stdout. Use -o to write to a file.
 *
 * The --target flag is currently informational — the emitted C is the
 * same for all targets (it calls into nb_runtime which is implemented
 * per-target). In the future, target-specific code paths may be added
 * (e.g. fixed-point math for NDS, dual-screen handling for 3DS).
 */
#define _GNU_SOURCE
#include "lexer.h"
#include "parser.h"
#include "typecheck.h"
#include "emit.h"

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

typedef enum {
    TARGET_HOST,
    TARGET_3DS,
    TARGET_NDS,
    TARGET_WII,
} NbTarget;

static char *read_file(const char *path, long *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) { perror("ftell"); fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fprintf(stderr, "oom\n"); fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        perror("fread"); free(buf); fclose(f); return NULL;
    }
    buf[size] = '\0';
    fclose(f);
    if (size_out) *size_out = size;
    return buf;
}

/* ── Multi-file support: IMPORT "file.bas" ────────────────────────── */
/* Resolves IMPORT statements by textually including the referenced file.
 * This happens BEFORE lexing — the parser just sees one big source file.
 *
 * Like C's #include but simpler. All variables and functions are global
 * across all files. No namespaces.
 *
 *   IMPORT "player.bas"     — includes player.bas at that point
 *   IMPORT "lib/math.bas"   — paths are relative to the importing file
 *
 * Circular imports are detected and skipped. */

#define MAX_INCLUDED 32

typedef struct {
    char paths[MAX_INCLUDED][512];
    int  count;
} IncludeTracker;

/* Check if a path is already in the tracker (circular import guard) */
static int already_included(IncludeTracker *t, const char *path) {
    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->paths[i], path) == 0) return 1;
    }
    return 0;
}

/* Case-insensitive string comparison */
static int starts_with_ci(const char *str, const char *prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*str) != tolower((unsigned char)*prefix))
            return 0;
        str++;
        prefix++;
    }
    return 1;
}

/* Resolve all IMPORT statements in source text.
 * Returns a new malloc'd string with all imports expanded.
 * base_dir is the directory of the file being processed (for relative paths). */
static char *resolve_imports(const char *source, const char *base_dir,
                              IncludeTracker *tracker) {
    /* We'll build the output in a dynamic buffer */
    size_t out_cap = strlen(source) + 1;
    char *out = (char *)malloc(out_cap);
    size_t out_len = 0;
    out[0] = '\0';

    const char *line_start = source;
    while (*line_start) {
        /* Find end of this line */
        const char *line_end = line_start;
        while (*line_end && *line_end != '\n') line_end++;

        size_t line_len = line_end - line_start;

        /* Skip leading whitespace */
        const char *p = line_start;
        while (p < line_end && (*p == ' ' || *p == '\t')) p++;

        /* Check if this line is an IMPORT statement */
        if (starts_with_ci(p, "IMPORT") &&
            (p[6] == ' ' || p[6] == '\t')) {
            /* Skip past "IMPORT" and whitespace */
            p += 6;
            while (p < line_end && (*p == ' ' || p[1] == '\t')) p++;

            /* Expect a quote */
            if (p < line_end && *p == '"') {
                p++;  /* skip opening quote */
                const char *name_start = p;
                while (p < line_end && *p != '"') p++;

                if (p < line_end && *p == '"') {
                    /* Extract filename */
                    size_t name_len = p - name_start;
                    char filename[512];
                    if (name_len >= sizeof(filename)) name_len = sizeof(filename) - 1;
                    memcpy(filename, name_start, name_len);
                    filename[name_len] = '\0';

                    /* Build full path relative to base_dir */
                    char full_path[1024];
                    if (base_dir && base_dir[0]) {
                        snprintf(full_path, sizeof(full_path), "%s/%s",
                                 base_dir, filename);
                    } else {
                        snprintf(full_path, sizeof(full_path), "%s", filename);
                    }

                    /* Get absolute path for circular check */
                    char abs_path[512];
                    if (!realpath(full_path, abs_path)) {
                        /* Try just the filename as-is */
                        strncpy(abs_path, full_path, sizeof(abs_path)-1);
                        abs_path[sizeof(abs_path)-1] = '\0';
                    }

                    /* Check for circular import */
                    if (already_included(tracker, abs_path)) {
                        /* Skip — already included */
                    } else {
                        /* Add to tracker */
                        if (tracker->count < MAX_INCLUDED) {
                            strncpy(tracker->paths[tracker->count],
                                    abs_path, 511);
                            tracker->paths[tracker->count][511] = '\0';
                            tracker->count++;
                        }

                        /* Read the included file */
                        long inc_size = 0;
                        char *inc_src = read_file(full_path, &inc_size);
                        if (inc_src) {
                            fprintf(stderr, "sproutc: including %s\n", full_path);

                            /* Get the included file's directory */
                            char inc_dir[512] = "";
                            strncpy(inc_dir, full_path, sizeof(inc_dir)-1);
                            inc_dir[sizeof(inc_dir)-1] = '\0';
                            char *last_slash = strrchr(inc_dir, '/');
                            if (last_slash) *last_slash = '\0';

                            /* Recursively resolve imports in the included file */
                            char *resolved = resolve_imports(inc_src, inc_dir, tracker);
                            free(inc_src);

                            if (resolved) {
                                /* Append resolved content to output */
                                size_t res_len = strlen(resolved);
                                while (out_len + res_len + 1 >= out_cap) {
                                    out_cap *= 2;
                                    out = (char *)realloc(out, out_cap);
                                }
                                memcpy(out + out_len, resolved, res_len);
                                out_len += res_len;
                                out[out_len] = '\0';
                                free(resolved);
                            }
                        } else {
                            fprintf(stderr, "sproutc: warning: could not read %s\n",
                                    full_path);
                        }
                    }

                    /* Skip the rest of this line (the IMPORT line itself) */
                    line_start = (*line_end == '\n') ? line_end + 1 : line_end;
                    continue;
                }
            }
        }

        /* Not an IMPORT line — copy it as-is */
        while (out_len + line_len + 2 >= out_cap) {
            out_cap *= 2;
            out = (char *)realloc(out, out_cap);
        }
        memcpy(out + out_len, line_start, line_len);
        out_len += line_len;
        if (*line_end == '\n') {
            out[out_len++] = '\n';
            line_start = line_end + 1;
        } else {
            line_start = line_end;
        }
        out[out_len] = '\0';
    }

    return out;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Sprout compiler\n"
        "\n"
        "Usage: %s <input.bas> [-o <output.c>] [--target=<t>]\n"
        "\n"
        "Options:\n"
        "  -o <file>            Write C output to <file> (default: stdout)\n"
        "  --target=<t>         Target platform (default: host)\n"
        "                         host  — PC (for testing)\n"
        "                         3ds   — Nintendo 3DS (via devkitARM + libctru)\n"
        "                         nds   — Nintendo DS (via devkitARM + libnds)\n"
        "                         wii   — Nintendo Wii (via devkitPPC + libogc)\n"
        "  --help               Show this help\n",
        prog);
}

int main(int argc, char **argv) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    NbTarget target = TARGET_HOST;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strncmp(argv[i], "--target=", 9) == 0) {
            const char *t = argv[i] + 9;
            if (strcmp(t, "host") == 0)      target = TARGET_HOST;
            else if (strcmp(t, "3ds") == 0)  target = TARGET_3DS;
            else if (strcmp(t, "nds") == 0)  target = TARGET_NDS;
            else if (strcmp(t, "wii") == 0)  target = TARGET_WII;
            else {
                fprintf(stderr, "Unknown target: %s\n", t);
                return 1;
            }
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        } else if (!input_path) {
            input_path = argv[i];
        } else {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!input_path) {
        usage(argv[0]);
        return 1;
    }

    /* ── Read source ──────────────────────────────────────────────── */
    long src_len = 0;
    char *src = read_file(input_path, &src_len);
    if (!src) return 1;

    /* ── Resolve IMPORT statements (multi-file support) ──────────── */
    /* Get the input file's directory for relative paths */
    char base_dir[512] = "";
    strncpy(base_dir, input_path, sizeof(base_dir) - 1);
    base_dir[sizeof(base_dir) - 1] = '\0';
    char *last_slash = strrchr(base_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
    } else {
        base_dir[0] = '\0';  /* no directory — use current dir */
    }

    IncludeTracker tracker;
    tracker.count = 0;
    /* Add the main file to the tracker to prevent self-import */
    char abs_main[512];
    if (realpath(input_path, abs_main)) {
        strncpy(tracker.paths[0], abs_main, 511);
        tracker.paths[0][511] = '\0';
        tracker.count = 1;
    }

    char *resolved_src = resolve_imports(src, base_dir, &tracker);
    free(src);
    src = resolved_src;
    src_len = (long)strlen(src);

    /* ── Lex ──────────────────────────────────────────────────────── */
    Lexer lx;
    lexer_init(&lx, src, (size_t)src_len);

    /* ── Parse ────────────────────────────────────────────────────── */
    Parser pr;
    parser_init(&pr, &lx);
    AstNode *prog = parser_parse_program(&pr);
    int parse_ok = !parser_had_error(&pr);
    parser_free(&pr);

    if (!parse_ok) {
        fprintf(stderr, "sproutc: parse errors encountered; continuing with "
                        "partial AST.\n");
    }

    /* ── Type check ───────────────────────────────────────────────── */
    NbTypeChecker tc;
    nb_tc_init(&tc);
    nb_tc_check_program(&tc, prog);
    if (tc.had_error) {
        fprintf(stderr, "sproutc: type errors encountered; continuing.\n");
    }

    /* ── Emit C ───────────────────────────────────────────────────── */
    FILE *out = output_path ? fopen(output_path, "wb") : stdout;
    if (!out) { perror(output_path); return 1; }

    const char *target_name = "host";
    switch (target) {
        case TARGET_HOST: target_name = "host"; break;
        case TARGET_3DS:  target_name = "3ds";  break;
        case TARGET_NDS:  target_name = "nds";  break;
        case TARGET_WII:  target_name = "wii";  break;
    }
    fprintf(stderr, "sproutc: compiling %s (target=%s)\n", input_path, target_name);
    nb_emit_program(out, prog, &tc);

    if (out != stdout) fclose(out);

    /* ── Clean up ─────────────────────────────────────────────────── */
    ast_free(prog);
    nb_tc_free(&tc);
    free(src);

    if (!parse_ok) return 1;
    return 0;
}

