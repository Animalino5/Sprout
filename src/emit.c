/*
 * emit.c — C code emitter
 *
 * Translates the AST into C99 that links against libsprout.
 *
 * Layout of emitted file:
 *   1. #include "nb_runtime.h"
 *   2. Forward declarations for user functions
 *   3. Global variable declarations (from symbol table)
 *   4. Array declarations (from DIM statements)
 *   5. User function definitions
 *   6. Room init/update functions
 *   7. nb_user_main() — top-level statements
 *   8. main() — calls nb_init(), nb_user_main(), nb_shutdown()
 */
#include "emit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* ── Builtin name mapping ─────────────────────────────────────────── */

typedef struct {
    const char *basic_name;
    const char *c_name;
} BuiltinMap;

static const BuiltinMap BUILTIN_MAP[] = {
    /* Math */
    {"ABS",       "nb_abs"},
    {"SGN",       "nb_sgn"},
    {"INT",       "nb_int"},
    {"FIX",       "nb_fix"},
    {"SQR",       "nb_sqr"},
    {"SIN",       "nb_sin"},
    {"COS",       "nb_cos"},
    {"TAN",       "nb_tan"},
    {"ATN",       "nb_atn"},
    {"ATAN2",     "nb_atan2"},
    {"EXP",       "nb_exp"},
    {"LOG",       "nb_log"},
    {"RND",       "nb_rnd"},
    {"RNDF",      "nb_rndf"},
    {"MIN",       "nb_min"},
    {"MAX",       "nb_max"},
    {"CLAMP",     "nb_clamp"},
    {"LERP",      "nb_lerp"},
    {"DIST",      "nb_dist"},
    {"DEG",       "nb_deg"},
    {"RAD",       "nb_rad"},

    /* String */
    {"LEN",       "nb_len"},
    {"LEFT$",     "nb_left"},
    {"RIGHT$",    "nb_right"},
    {"MID$",      "nb_mid"},
    {"UCASE$",    "nb_ucase"},
    {"LCASE$",    "nb_lcase"},
    {"INSTR",     "nb_instr"},
    {"VAL",       "nb_val"},
    {"STR$",      "nb_str"},
    {"CHR$",      "nb_chr"},
    {"ASC",       "nb_asc"},
    {"INKEY$",    "nb_inkey"},

    /* Console */
    {"CSRLIN",    "nb_csrlin"},
    {"POS",       "nb_pos"},
    {"LOCATE",    "nb_locate"},
    {"COLOR",     "nb_color"},
    {"CLS",       "nb_cls"},

    /* Input */
    {"BUTTON",    "nb_button"},
    {"BUTTONDOWN","nb_button_down"},
    {"BUTTONUP",  "nb_button_up"},
    {"INPUT_TOUCH_X",   "nb_touch_x"},
    {"INPUT_TOUCH_Y",   "nb_touch_y"},
    {"INPUT_TOUCH_STATE", "nb_touch_state"},
    {"TOUCHDOWN", "nb_touchdown"},
    {"TOUCHPRESSED",  "nb_touchpressed"},
    {"TOUCHRELEASED", "nb_touchreleased"},
    {"GETTOUCH",  "nb_gettouch"},
    {"GETCIRCLEPAD", "nb_getcirclepad"},

    /* Graphics */
    {"RGB",       "nb_rgb"},
    {"POINT",     "nb_point"},
    {"PIXEL",     "nb_pixel"},
    {"TEXT",      "nb_text"},
    {"LINE",      "nb_line"},
    {"RECT",      "nb_rect"},
    {"CIRCLE",    "nb_circle"},
    {"LOADIMAGE", "nb_load_image"},
    {"DRAWIMAGE", "nb_draw_image"},
    {"FREEIMAGE", "nb_free_image"},
    {"IMAGEW",    "nb_image_w"},
    {"IMAGEH",    "nb_image_h"},
    {"LOADSHEET", "nb_load_sheet"},
    {"DRAWSHEET", "nb_draw_sheet"},
    {"DRAWIMAGEEX", "nb_draw_image_ex"},

    /* Audio */
    {"LOADSOUND", "nb_load_sound"},
    {"PLAYSOUND", "nb_play_sound"},
    {"STOPSOUND", "nb_stop_sound"},
    {"LOADMUSIC", "nb_load_music"},
    {"PLAYMUSIC", "nb_play_music"},
    {"STOPMUSIC", "nb_stop_music"},
    {"PAUSEMUSIC", "nb_pause_music"},
    {"RESUMEMUSIC", "nb_resume_music"},
    {"VOLMUSIC",   "nb_vol_music"},

    /* Files */
    /* Graphics — new shapes */
    {"TRIANGLE",  "nb_triangle"},
    {"ELLIPSE",   "nb_ellipse"},
    {"TEXTSIZE",  "nb_text_size"},
    {"TRANSLATE", "nb_translate"},
    {"ROTATE",    "nb_rotate"},
    {"RESIZE",    "nb_resize"},
    {"SETSCREEN", "nb_set_screen"},
    {"LOADFONT",  "nb_load_font"},
    {"FONT",      "nb_set_font"},

    /* Files — sequential I/O */
    {"OPENW",     "nb_open_write"},
    {"OPENR",     "nb_open_read"},
    {"WRITE",     "nb_write_int"},   /* emitter overrides for strings */
    {"CLOSE",     "nb_close"},
    {"READNUM",   "nb_read_num"},
    {"READLINE$", "nb_read_line"},

    {"EXISTS",    "nb_exists"},
    {"DELETE",    "nb_delete"},
    {"EOF",       "nb_eof"},
    {"LOF",       "nb_lof"},

    /* Timing */
    {"MSECONDS",  "nb_mseconds"},
    {"FRAMECOUNT", "nb_framecount"},
    {"WAITKEY",   "nb_waitkey"},
    {"WAITFRAME", "nb_waitframe"},
    {"SLEEP",     "nb_sleep"},

    /* Misc */
    {"END",       "nb_end"},
    {"SCREEN",    "nb_screen"},
    {"RANDOMIZE", "nb_randomize"},

    /* Module-specific — but treated as no-op for v1 host runtime */
    {"UPDATE",    "nb_update"},
    {"DRAW",      "nb_draw"},
};
static const int NUM_BUILTIN_MAP = (int)(sizeof(BUILTIN_MAP) / sizeof(BUILTIN_MAP[0]));

static const char *basic_to_c_name(const char *basic_name) {
    for (int i = 0; i < NUM_BUILTIN_MAP; i++) {
        if (strcasecmp(basic_name, BUILTIN_MAP[i].basic_name) == 0) {
            return BUILTIN_MAP[i].c_name;
        }
    }
    return NULL;
}

/* Sanitize a BASIC identifier for use as a C identifier.
 * Strips type suffixes ($ % & ! #) and replaces other invalid chars. */
static void sanitize_ident(char *buf, size_t buf_size, const char *src) {
    size_t len = strlen(src);
    /* Strip trailing type suffix */
    if (len > 0) {
        char last = src[len - 1];
        if (last == '$' || last == '%' || last == '&' ||
            last == '!' || last == '#') {
            len--;
        }
    }
    if (len >= buf_size) len = buf_size - 1;
    for (size_t i = 0; i < len; i++) {
        char c = src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_') {
            buf[i] = c;
        } else {
            buf[i] = '_';
        }
    }
    buf[len] = '\0';
}

/* Check if a name is a known BASIC constant (colors, buttons, etc.).
 * IMPORTANT: single-letter button names (A, B, X, Y, L, R) are only
 * treated as constants when they're UPPERCASE. This lets users use
 * lowercase 'a', 'b', etc. as regular variables without conflict. */
static int is_constant_name(const char *name) {
    /* Colors — case-insensitive (BLACK, White, red all work) */
    if (strcasecmp(name, "BLACK")   == 0) return 1;
    if (strcasecmp(name, "WHITE")   == 0) return 1;
    if (strcasecmp(name, "RED")     == 0) return 1;
    if (strcasecmp(name, "GREEN")   == 0) return 1;
    if (strcasecmp(name, "BLUE")    == 0) return 1;
    if (strcasecmp(name, "YELLOW")  == 0) return 1;
    if (strcasecmp(name, "CYAN")    == 0) return 1;
    if (strcasecmp(name, "MAGENTA") == 0) return 1;
    if (strcasecmp(name, "GRAY")    == 0) return 1;
    if (strcasecmp(name, "GREY")    == 0) return 1;
    /* FILL — case-insensitive */
    if (strcasecmp(name, "FILL")    == 0) return 1;
    /* Multi-letter buttons — case-insensitive */
    if (strcasecmp(name, "START")   == 0) return 1;
    if (strcasecmp(name, "SELECT")  == 0) return 1;
    if (strcasecmp(name, "UP")      == 0) return 1;
    if (strcasecmp(name, "DOWN")    == 0) return 1;
    if (strcasecmp(name, "LEFT")    == 0) return 1;
    if (strcasecmp(name, "RIGHT")   == 0) return 1;
    /* Single-letter buttons — CASE-SENSITIVE (uppercase only!)
     * This lets 'a', 'b', 'x', 'y', 'l', 'r' be regular variables. */
    if (strcmp(name, "A") == 0) return 1;
    if (strcmp(name, "B") == 0) return 1;
    if (strcmp(name, "X") == 0) return 1;
    if (strcmp(name, "Y") == 0) return 1;
    if (strcmp(name, "L") == 0) return 1;
    if (strcmp(name, "R") == 0) return 1;
    return 0;
}

/* Emit a C expression for a named constant. */
static void emit_constant(FILE *out, const char *name) {
    /* Colors — RGB888 packed */
    if (strcasecmp(name, "BLACK")   == 0) { fputs("nb_rgb(0,0,0)",     out); return; }
    if (strcasecmp(name, "WHITE")   == 0) { fputs("nb_rgb(255,255,255)",out); return; }
    if (strcasecmp(name, "RED")     == 0) { fputs("nb_rgb(255,0,0)",    out); return; }
    if (strcasecmp(name, "GREEN")   == 0) { fputs("nb_rgb(0,255,0)",    out); return; }
    if (strcasecmp(name, "BLUE")    == 0) { fputs("nb_rgb(0,0,255)",    out); return; }
    if (strcasecmp(name, "YELLOW")  == 0) { fputs("nb_rgb(255,255,0)",  out); return; }
    if (strcasecmp(name, "CYAN")    == 0) { fputs("nb_rgb(0,255,255)",  out); return; }
    if (strcasecmp(name, "MAGENTA") == 0) { fputs("nb_rgb(255,0,255)",  out); return; }
    if (strcasecmp(name, "GRAY")    == 0 ||
        strcasecmp(name, "GREY")    == 0) { fputs("nb_rgb(128,128,128)",out); return; }
    /* Buttons */
    if (strcasecmp(name, "A")      == 0) { fputs("NB_BTN_A",      out); return; }
    if (strcasecmp(name, "B")      == 0) { fputs("NB_BTN_B",      out); return; }
    if (strcasecmp(name, "X")      == 0) { fputs("NB_BTN_X",      out); return; }
    if (strcasecmp(name, "Y")      == 0) { fputs("NB_BTN_Y",      out); return; }
    if (strcasecmp(name, "L")      == 0) { fputs("NB_BTN_L",      out); return; }
    if (strcasecmp(name, "R")      == 0) { fputs("NB_BTN_R",      out); return; }
    if (strcasecmp(name, "START")  == 0) { fputs("NB_BTN_START",  out); return; }
    if (strcasecmp(name, "SELECT") == 0) { fputs("NB_BTN_SELECT", out); return; }
    if (strcasecmp(name, "UP")     == 0) { fputs("NB_BTN_UP",     out); return; }
    if (strcasecmp(name, "DOWN")   == 0) { fputs("NB_BTN_DOWN",   out); return; }
    if (strcasecmp(name, "LEFT")   == 0) { fputs("NB_BTN_LEFT",   out); return; }
    if (strcasecmp(name, "RIGHT")  == 0) { fputs("NB_BTN_RIGHT",  out); return; }
    if (strcasecmp(name, "FILL")   == 0) { fputs("1",             out); return; }
    /* Fallback */
    fputs("0", out);
}

/* Check if a variable name is a string type (has $ suffix or inferred STRING). */
static int is_string_var(const NbTypeChecker *tc, const char *name) {
    /* Suffix check */
    size_t len = strlen(name);
    if (len > 0 && name[len - 1] == '$') return 1;
    /* Symbol table check */
    const NbSymbol *s = nb_tc_lookup(tc, name);
    if (s && s->type == TY_STRING) return 1;
    return 0;
}

/* ── Emitter state ────────────────────────────────────────────────── */

typedef struct {
    FILE *out;
    const NbTypeChecker *tc;
    int   indent;
    int   in_function;       /* 1 if inside a function body */
    int   in_room_update;    /* 1 if inside a room's update body */
} Emitter;

static void emit_indent(Emitter *e) {
    for (int i = 0; i < e->indent; i++) fputc(' ', e->out);
}

/* ── Forward declarations ─────────────────────────────────────────── */

static void emit_expr(Emitter *e, AstNode *expr);
static void emit_stmt(Emitter *e, AstNode *stmt);
static void emit_stmt_list(Emitter *e, AstNode **stmts, int n);
static void emit_c_string(FILE *out, const char *s);

/* ── C string emission ────────────────────────────────────────────── */

/* Emit a string literal in C syntax. Escapes special characters. */
static void emit_c_string(FILE *out, const char *s) {
    fputc('"', out);
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out);  break;
            case '\r': fputs("\\r", out);  break;
            case '\t': fputs("\\t", out);  break;
            default:
                if ((unsigned char)*p < 0x20) {
                    fprintf(out, "\\x%02x", (unsigned char)*p);
                } else {
                    fputc(*p, out);
                }
        }
    }
    fputc('"', out);
}

/* ── Expression emission ──────────────────────────────────────────── */

static const char *op_symbol(TokenType op) {
    switch (op) {
        case TOK_PLUS:      return "+";
        case TOK_MINUS:     return "-";
        case TOK_STAR:      return "*";
        case TOK_SLASH:     return "/";
        case TOK_BACKSLASH: return "/";  /* C has no integer-only div operator; will use nb_idiv */
        case TOK_CARET:     return "^";  /* C has no power op; we'll use nb_pow */
        case TOK_EQ:        return "=="; /* equality in expressions */
        case TOK_NE:        return "!=";
        case TOK_LT:        return "<";
        case TOK_GT:        return ">";
        case TOK_LE:        return "<=";
        case TOK_GE:        return ">=";
        case TOK_KW_AND:    return "&";   /* bitwise for now */
        case TOK_KW_OR:     return "|";
        case TOK_KW_NOT:    return "!";   /* logical not — but NOT in BASIC is bitwise... */
        case TOK_KW_MOD:    return "%";
        default:            return "?";
    }
}

/* Recursively check if an expression produces a string.
 * Handles: string literals, $ variables, string-returning builtins
 * (STR$, LEFT$, etc.), and nested + concatenations. */
static int is_string_expr(const NbTypeChecker *tc, AstNode *expr) {
    if (!expr) return 0;
    if (expr->type == AST_STRING_LIT) return 1;
    if (expr->type == AST_IDENT && is_string_var(tc, expr->u.text)) return 1;
    if (expr->type == AST_CALL) {
        NbType rt = nb_tc_builtin_return_type(expr->u.call.name);
        if (rt == TY_STRING) return 1;
    }
    if (expr->type == AST_BINARY_OP && expr->u.binary.op == TOK_PLUS) {
        return is_string_expr(tc, expr->u.binary.left) ||
               is_string_expr(tc, expr->u.binary.right);
    }
    return 0;
}

static void emit_expr(Emitter *e, AstNode *expr) {
    if (!expr) { fputs("0", e->out); return; }
    char buf[256];
    switch (expr->type) {
        case AST_INT_LIT:
            fprintf(e->out, "%ldL", expr->u.ival);
            break;
        case AST_FLOAT_LIT:
            fprintf(e->out, "%g", expr->u.fval);
            break;
        case AST_STRING_LIT:
            emit_c_string(e->out, expr->u.text ? expr->u.text : "");
            break;
        case AST_IDENT:
            /* Check for color/button constants FIRST, before the symbol table.
             * This is because our symbol table is case-insensitive, so a
             * variable named 'a' would otherwise shadow the 'A' button constant.
             * Constants like A, B, X, Y, LEFT, RIGHT, BLACK, WHITE are reserved. */
            if (is_constant_name(expr->u.text)) {
                emit_constant(e->out, expr->u.text);
            }
            /* If it's a declared variable, emit sanitized name. */
            else if (nb_tc_lookup(e->tc, expr->u.text)) {
                sanitize_ident(buf, sizeof(buf), expr->u.text);
                fputs(buf, e->out);
            }
            /* Fallback: treat as a variable anyway */
            else {
                sanitize_ident(buf, sizeof(buf), expr->u.text);
                fputs(buf, e->out);
            }
            break;
        case AST_BINARY_OP: {
            TokenType op = expr->u.binary.op;
            /* String concat: + with at least one string operand.
             * Must check recursively — nested + and string-returning
             * functions like STR$() also produce strings. */
            int left_is_string  = is_string_expr(e->tc, expr->u.binary.left);
            int right_is_string = is_string_expr(e->tc, expr->u.binary.right);
            if (op == TOK_PLUS && (left_is_string || right_is_string)) {
                fputs("nb_strcat(", e->out);
                emit_expr(e, expr->u.binary.left);
                fputs(", ", e->out);
                emit_expr(e, expr->u.binary.right);
                fputs(")", e->out);
                return;
            }
            if (op == TOK_CARET) {
                fputs("nb_pow(", e->out);
                emit_expr(e, expr->u.binary.left);
                fputs(", ", e->out);
                emit_expr(e, expr->u.binary.right);
                fputs(")", e->out);
                return;
            }
            if (op == TOK_BACKSLASH) {
                fputs("nb_idiv(", e->out);
                emit_expr(e, expr->u.binary.left);
                fputs(", ", e->out);
                emit_expr(e, expr->u.binary.right);
                fputs(")", e->out);
                return;
            }
            /* Standard binary op */
            fputc('(', e->out);
            emit_expr(e, expr->u.binary.left);
            fprintf(e->out, " %s ", op_symbol(op));
            emit_expr(e, expr->u.binary.right);
            fputc(')', e->out);
            break;
        }
        case AST_UNARY_OP:
            fputc('(', e->out);
            fputs(op_symbol(expr->u.unary.op), e->out);
            emit_expr(e, expr->u.unary.operand);
            fputc(')', e->out);
            break;
        case AST_CALL: {
            /* Check if this is actually an array access, not a function call.
             * The parser can't tell the difference between foo(x) as a call
             * and foo(x) as an array index — both produce AST_CALL.
             * We consult the symbol table: if the name is a declared array,
             * emit array access; otherwise emit a function call. */
            const NbSymbol *sym = nb_tc_lookup(e->tc, expr->u.call.name);
            if (sym && sym->is_array) {
                /* Array access: name[idx] */
                sanitize_ident(buf, sizeof(buf), expr->u.call.name);
                fputs(buf, e->out);
                fputc('[', e->out);
                for (int i = 0; i < expr->u.call.num_args; i++) {
                    if (i > 0) fputs("][", e->out);
                    emit_expr(e, expr->u.call.args[i]);
                }
                fputc(']', e->out);
                break;
            }
            /* Otherwise it's a function call */
            const char *c_name = basic_to_c_name(expr->u.call.name);
            if (c_name) {
                fputs(c_name, e->out);
            } else {
                /* User-defined function */
                sanitize_ident(buf, sizeof(buf), expr->u.call.name);
                fprintf(e->out, "user_%s", buf);
            }
            fputc('(', e->out);
            for (int i = 0; i < expr->u.call.num_args; i++) {
                if (i > 0) fputs(", ", e->out);
                emit_expr(e, expr->u.call.args[i]);
            }
            fputc(')', e->out);
            break;
        }
        case AST_INDEX:
            sanitize_ident(buf, sizeof(buf), expr->u.index.name);
            fputs(buf, e->out);
            fputc('[', e->out);
            for (int i = 0; i < expr->u.index.num_indices; i++) {
                if (i > 0) fputs("][", e->out);
                emit_expr(e, expr->u.index.indices[i]);
            }
            fputc(']', e->out);
            break;
        case AST_MEMBER:
            sanitize_ident(buf, sizeof(buf), expr->u.member.module);
            fputs(buf, e->out);
            fputs("_", e->out);
            sanitize_ident(buf, sizeof(buf), expr->u.member.member);
            fputs(buf, e->out);
            break;
        default:
            fputs("0", e->out);
            break;
    }
}

/* ── Statement emission ───────────────────────────────────────────── */

/* For PRINT: emit each part, with nb_print_int / nb_print_float / nb_print_str
 * calls. Separators: ';' = no space, ',' = tab. End with nb_println(). */
static void emit_print(Emitter *e, AstNode *stmt) {
    if (stmt->u.print.num_parts == 0) {
        emit_indent(e);
        fputs("nb_println();\n", e->out);
        return;
    }
    for (int i = 0; i < stmt->u.print.num_parts; i++) {
        AstNode *part = stmt->u.print.parts[i];
        emit_indent(e);
        /* Simple heuristic: string literal → print_str, float literal → print_float,
         * otherwise print_int (with auto-promotion in runtime) */
        if (part->type == AST_STRING_LIT) {
            fputs("nb_print_str(", e->out);
            emit_expr(e, part);
            fputs(");\n", e->out);
        } else if (part->type == AST_FLOAT_LIT) {
            fputs("nb_print_float(", e->out);
            emit_expr(e, part);
            fputs(");\n", e->out);
        } else if (part->type == AST_INT_LIT) {
            fputs("nb_print_int(", e->out);
            emit_expr(e, part);
            fputs(");\n", e->out);
        } else if (part->type == AST_CALL) {
            NbType rt = nb_tc_builtin_return_type(part->u.call.name);
            if (rt == TY_STRING) {
                fputs("nb_print_str(", e->out);
            } else if (rt == TY_FLOAT) {
                fputs("nb_print_float(", e->out);
            } else {
                fputs("nb_print_int(", e->out);
            }
            emit_expr(e, part);
            fputs(");\n", e->out);
        } else {
            /* Variable or complex expression — look up type */
            const char *print_fn = "nb_print_int";
            if (part->type == AST_IDENT) {
                const NbSymbol *s = nb_tc_lookup(e->tc, part->u.text);
                if (s) {
                    if (s->type == TY_STRING) print_fn = "nb_print_str";
                    else if (s->type == TY_FLOAT) print_fn = "nb_print_float";
                }
            } else if (part->type == AST_BINARY_OP) {
                /* If it's a string concat, use print_str */
                if (part->u.binary.op == TOK_PLUS &&
                    (part->u.binary.left->type == AST_STRING_LIT ||
                     part->u.binary.right->type == AST_STRING_LIT)) {
                    print_fn = "nb_print_str";
                } else if (part->u.binary.left->type == AST_FLOAT_LIT ||
                           part->u.binary.right->type == AST_FLOAT_LIT) {
                    print_fn = "nb_print_float";
                }
            }
            fprintf(e->out, "%s(", print_fn);
            emit_expr(e, part);
            fputs(");\n", e->out);
        }
        /* Separator handling — ';' = nothing, ',' = tab */
        if (i < stmt->u.print.num_parts - 1) {
            char sep = stmt->u.print.seps ? stmt->u.print.seps[i + 1] : ';';
            if (sep == ',') {
                emit_indent(e);
                fputs("nb_print_tab();\n", e->out);
            }
        }
    }
    emit_indent(e);
    fputs("nb_println();\n", e->out);
}

static void emit_assign(Emitter *e, AstNode *stmt) {
    AstNode *target = stmt->u.assign.target;
    /* Special: STR$ assignment needs strcpy */
    if (target->type == AST_IDENT) {
        if (is_string_var(e->tc, target->u.text)) {
            /* String assignment — use nb_str_assign */
            char vname[256];
            sanitize_ident(vname, sizeof(vname), target->u.text);
            emit_indent(e);
            fprintf(e->out, "nb_str_assign(%s, ", vname);
            emit_expr(e, stmt->u.assign.value);
            fputs(");\n", e->out);
            return;
        }
    }
    /* Default: regular assignment */
    emit_indent(e);
    emit_expr(e, target);
    fputs(" = ", e->out);
    emit_expr(e, stmt->u.assign.value);
    fputs(";\n", e->out);
}

static void emit_if(Emitter *e, AstNode *stmt) {
    for (int i = 0; i < stmt->u.if_stmt.num_branches; i++) {
        AstBranch *br = &stmt->u.if_stmt.branches[i];
        emit_indent(e);
        if (i == 0) {
            fputs("if (", e->out);
            emit_expr(e, br->cond);
            fputs(") {\n", e->out);
        } else if (br->cond) {
            fputs("else if (", e->out);
            emit_expr(e, br->cond);
            fputs(") {\n", e->out);
        } else {
            fputs("else {\n", e->out);
        }
        e->indent++;
        emit_stmt_list(e, br->body, br->num_body);
        e->indent--;
        emit_indent(e);
        fputs("}\n", e->out);
    }
}

static void emit_while(Emitter *e, AstNode *stmt) {
    emit_indent(e);
    fputs("while (", e->out);
    emit_expr(e, stmt->u.while_stmt.cond);
    fputs(") {\n", e->out);
    e->indent++;
    emit_stmt_list(e, stmt->u.while_stmt.body, stmt->u.while_stmt.num_body);
    e->indent--;
    emit_indent(e);
    fputs("}\n", e->out);
}

static void emit_for(Emitter *e, AstNode *stmt) {
    char varname[256];
    sanitize_ident(varname, sizeof(varname), stmt->u.for_stmt.var_name);
    emit_indent(e);
    fprintf(e->out, "for (%s = ", varname);
    emit_expr(e, stmt->u.for_stmt.start);
    fputs("; ", e->out);
    fputs(varname, e->out);
    if (stmt->u.for_stmt.step) {
        if (stmt->u.for_stmt.step->type == AST_INT_LIT &&
            stmt->u.for_stmt.step->u.ival < 0) {
            fputs(" >= ", e->out);
        } else {
            fputs(" <= ", e->out);
        }
    } else {
        fputs(" <= ", e->out);
    }
    emit_expr(e, stmt->u.for_stmt.end);
    fputs("; ", e->out);
    fputs(varname, e->out);
    fputs(" += ", e->out);
    if (stmt->u.for_stmt.step) {
        emit_expr(e, stmt->u.for_stmt.step);
    } else {
        fputs("1", e->out);
    }
    fputs(") {\n", e->out);
    e->indent++;
    emit_stmt_list(e, stmt->u.for_stmt.body, stmt->u.for_stmt.num_body);
    e->indent--;
    emit_indent(e);
    fputs("}\n", e->out);
}

static void emit_do_loop(Emitter *e, AstNode *stmt) {
    if (stmt->u.do_loop.pre_cond && stmt->u.do_loop.cond) {
        emit_indent(e);
        fputs(stmt->u.do_loop.is_until ? "while (!(" : "while (", e->out);
        emit_expr(e, stmt->u.do_loop.cond);
        fputs(stmt->u.do_loop.is_until ? ")) {\n" : ") {\n", e->out);
        e->indent++;
        emit_stmt_list(e, stmt->u.do_loop.body, stmt->u.do_loop.num_body);
        e->indent--;
        emit_indent(e);
        fputs("}\n", e->out);
    } else if (stmt->u.do_loop.cond) {
        /* Post-condition */
        emit_indent(e);
        fputs("do {\n", e->out);
        e->indent++;
        emit_stmt_list(e, stmt->u.do_loop.body, stmt->u.do_loop.num_body);
        e->indent--;
        emit_indent(e);
        fputs(stmt->u.do_loop.is_until ? "} while (!(" : "} while (", e->out);
        emit_expr(e, stmt->u.do_loop.cond);
        fputs(stmt->u.do_loop.is_until ? "));\n" : ");\n", e->out);
    } else {
        /* Infinite */
        emit_indent(e);
        fputs("while (1) {\n", e->out);
        e->indent++;
        emit_stmt_list(e, stmt->u.do_loop.body, stmt->u.do_loop.num_body);
        e->indent--;
        emit_indent(e);
        fputs("}\n", e->out);
    }
}

static void emit_func_def(Emitter *e, AstNode *stmt) {
    /* Determine return type from symbol table */
    const NbSymbol *s = nb_tc_lookup(e->tc, stmt->u.func_def.name);
    NbType rt = s ? s->type : TY_VOID;
    if (rt == TY_UNKNOWN) rt = TY_VOID;
    char fname[256];
    sanitize_ident(fname, sizeof(fname), stmt->u.func_def.name);
    fprintf(e->out, "%s user_%s(", nb_type_c_name(rt), fname);
    for (int i = 0; i < stmt->u.func_def.num_params; i++) {
        char pname[256];
        sanitize_ident(pname, sizeof(pname), stmt->u.func_def.params[i]);
        if (i > 0) fputs(", ", e->out);
        int is_arr = stmt->u.func_def.param_is_array ?
                     stmt->u.func_def.param_is_array[i] : 0;
        if (is_arr) {
            fputs("long *", e->out);  /* array parameter → pointer */
        } else {
            fputs("long ", e->out);
        }
        fputs(pname, e->out);
    }
    fputs(") {\n", e->out);
    e->indent++;
    e->in_function = 1;
    emit_stmt_list(e, stmt->u.func_def.body, stmt->u.func_def.num_body);
    /* If void, emit return */
    if (rt == TY_VOID) {
        emit_indent(e);
        fputs("return 0;\n", e->out);
    }
    e->in_function = 0;
    e->indent--;
    fputs("}\n\n", e->out);
}

static void emit_return(Emitter *e, AstNode *stmt) {
    emit_indent(e);
    fputs("return ", e->out);
    if (stmt->u.return_stmt.expr) {
        emit_expr(e, stmt->u.return_stmt.expr);
    } else {
        fputs("0", e->out);
    }
    fputs(";\n", e->out);
}

static void emit_dim(Emitter *e, AstNode *stmt) {
    /* Array declaration — emitted as part of globals, so this is a no-op
     * in the body. (We could support local arrays later.) */
    (void)e; (void)stmt;
}

static void emit_room(Emitter *e, AstNode *stmt) {
    /* Sanitize room name for use in function name */
    char fn_name[256];
    snprintf(fn_name, sizeof(fn_name), "room_%s", stmt->u.room.name);
    /* Replace non-alphanumeric with _ */
    for (char *p = fn_name; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') *p = '_';
    }

    /* Init function */
    fprintf(e->out, "void %s_init(void) {\n", fn_name);
    e->indent++;
    emit_stmt_list(e, stmt->u.room.init_body, stmt->u.room.num_init);
    e->indent--;
    fputs("}\n\n", e->out);

    /* Update function */
    fprintf(e->out, "void %s_update(void) {\n", fn_name);
    e->indent++;
    e->in_room_update = 1;
    emit_stmt_list(e, stmt->u.room.update_body, stmt->u.room.num_update);
    e->in_room_update = 0;
    e->indent--;
    fputs("}\n\n", e->out);
}

static void emit_gotoroom(Emitter *e, AstNode *stmt) {
    char fn_name[256];
    snprintf(fn_name, sizeof(fn_name), "room_%s", stmt->u.gotoroom.name);
    for (char *p = fn_name; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') *p = '_';
    }
    emit_indent(e);
    /* If the room isn't defined in this file, emit forward declarations
     * as weak stubs so the C compiles. The linker will resolve them
     * if the room exists in another file; otherwise they'll fail at
     * link time with a clear error. */
    fprintf(e->out, "/* GOTOROOM \"%s\" */\n", stmt->u.gotoroom.name);
    emit_indent(e);
    fprintf(e->out,
            "extern void %s_init(void); extern void %s_update(void);\n",
            fn_name, fn_name);
    emit_indent(e);
    fprintf(e->out, "nb_gotoroom(%s_init, %s_update);\n", fn_name, fn_name);
}

static void emit_import(Emitter *e, AstNode *stmt) {
    /* For v1, imports are processed at parse time (textual inclusion).
     * This is a no-op in the emitter. */
    (void)e; (void)stmt;
}

static void emit_expr_stmt(Emitter *e, AstNode *stmt) {
    AstNode *call = stmt->u.assign.target;
    if (call->type != AST_CALL) {
        emit_indent(e);
        emit_expr(e, call);
        fputs(";\n", e->out);
        return;
    }

    /* Special-case certain builtins that need different emission */
    const char *c_name = basic_to_c_name(call->u.call.name);

    /* Special cases for builtins with variable argument counts.
     * These functions have fixed C signatures but BASIC allows
     * optional arguments. We fill in defaults. */
    if (c_name) {
        int n = call->u.call.num_args;

        /* CLS: 0 args → nb_cls(0L), 1 arg → nb_cls(arg) */
        if (strcasecmp(call->u.call.name, "CLS") == 0) {
            emit_indent(e);
            fputs("nb_cls(", e->out);
            if (n == 0) fputs("0L", e->out);
            else emit_expr(e, call->u.call.args[0]);
            fputs(");\n", e->out);
            return;
        }

        /* COLOR: 1 arg → nb_color(arg, 0L), 2 args → nb_color(a, b) */
        if (strcasecmp(call->u.call.name, "COLOR") == 0) {
            emit_indent(e);
            fputs("nb_color(", e->out);
            if (n >= 1) emit_expr(e, call->u.call.args[0]);
            else fputs("0L", e->out);
            fputs(", ", e->out);
            if (n >= 2) emit_expr(e, call->u.call.args[1]);
            else fputs("0L", e->out);
            fputs(");\n", e->out);
            return;
        }

        /* RECT: 4 args → nb_rect(x,y,w,h,0L), 5 args → nb_rect(x,y,w,h,fill) */
        if (strcasecmp(call->u.call.name, "RECT") == 0 && (n == 4 || n == 5)) {
            emit_indent(e);
            fputs("nb_rect(", e->out);
            for (int i = 0; i < 4; i++) {
                if (i > 0) fputs(", ", e->out);
                emit_expr(e, call->u.call.args[i]);
            }
            fputs(", ", e->out);
            if (n == 5) emit_expr(e, call->u.call.args[4]);
            else fputs("0L", e->out);
            fputs(");\n", e->out);
            return;
        }

        /* CIRCLE: 3 args → nb_circle(x,y,r,0L), 4 args → nb_circle(x,y,r,fill) */
        if (strcasecmp(call->u.call.name, "CIRCLE") == 0 && (n == 3 || n == 4)) {
            emit_indent(e);
            fputs("nb_circle(", e->out);
            for (int i = 0; i < 3; i++) {
                if (i > 0) fputs(", ", e->out);
                emit_expr(e, call->u.call.args[i]);
            }
            fputs(", ", e->out);
            if (n == 4) emit_expr(e, call->u.call.args[3]);
            else fputs("0L", e->out);
            fputs(");\n", e->out);
            return;
        }

        /* DRAWSHEET: 4 args → manual frame, 5 args → auto-animate with FPS */
        if (strcasecmp(call->u.call.name, "DRAWSHEET") == 0 && (n == 4 || n == 5)) {
            emit_indent(e);
            if (n == 5) {
                /* Auto-animate: nb_draw_sheet_anim(sheet, x, y, start_frame, fps) */
                fputs("nb_draw_sheet_anim(", e->out);
                for (int i = 0; i < 5; i++) {
                    if (i > 0) fputs(", ", e->out);
                    emit_expr(e, call->u.call.args[i]);
                }
            } else {
                /* Manual: nb_draw_sheet(sheet, x, y, frame) */
                fputs("nb_draw_sheet(", e->out);
                for (int i = 0; i < 4; i++) {
                    if (i > 0) fputs(", ", e->out);
                    emit_expr(e, call->u.call.args[i]);
                }
            }
            fputs(");\n", e->out);
            return;
        }

        /* WRITE: if arg is a string, use nb_write_str; otherwise nb_write_int */
        if (strcasecmp(call->u.call.name, "WRITE") == 0 && n == 1) {
            AstNode *arg = call->u.call.args[0];
            emit_indent(e);
            int is_str = 0;
            if (arg->type == AST_STRING_LIT) is_str = 1;
            else if (arg->type == AST_IDENT && is_string_var(e->tc, arg->u.text)) is_str = 1;
            else if (arg->type == AST_CALL) {
                /* STR$, READLINE$, CHR$, LEFT$, etc. return strings */
                NbType rt = nb_tc_builtin_return_type(arg->u.call.name);
                if (rt == TY_STRING) is_str = 1;
            }
            else if (arg->type == AST_BINARY_OP && arg->u.binary.op == TOK_PLUS) {
                /* String concatenation */
                if (arg->u.binary.left->type == AST_STRING_LIT ||
                    arg->u.binary.right->type == AST_STRING_LIT) is_str = 1;
                else if (arg->u.binary.left->type == AST_IDENT &&
                         is_string_var(e->tc, arg->u.binary.left->u.text)) is_str = 1;
            }
            if (is_str) {
                fputs("nb_write_str(", e->out);
            } else {
                fputs("nb_write_int(", e->out);
            }
            emit_expr(e, arg);
            fputs(");\n", e->out);
            return;
        }

        /* TRIANGLE: 6 args → outline, 7 args → with fill flag */
        if (strcasecmp(call->u.call.name, "TRIANGLE") == 0 && (n == 6 || n == 7)) {
            emit_indent(e);
            fputs("nb_triangle(", e->out);
            for (int i = 0; i < 6; i++) {
                if (i > 0) fputs(", ", e->out);
                emit_expr(e, call->u.call.args[i]);
            }
            fputs(", ", e->out);
            if (n == 7) emit_expr(e, call->u.call.args[6]);
            else fputs("0L", e->out);
            fputs(");\n", e->out);
            return;
        }

        /* ELLIPSE: 4 args → outline, 5 args → with fill flag */
        if (strcasecmp(call->u.call.name, "ELLIPSE") == 0 && (n == 4 || n == 5)) {
            emit_indent(e);
            fputs("nb_ellipse(", e->out);
            for (int i = 0; i < 4; i++) {
                if (i > 0) fputs(", ", e->out);
                emit_expr(e, call->u.call.args[i]);
            }
            fputs(", ", e->out);
            if (n == 5) emit_expr(e, call->u.call.args[4]);
            else fputs("0L", e->out);
            fputs(");\n", e->out);
            return;
        }

        /* GETTOUCH and GETCIRCLEPAD: pass args by reference.
         * Use temp long variables to avoid type mismatch when the
         * Sprout variable is typed as double. */
        if (strcasecmp(call->u.call.name, "GETTOUCH") == 0) {
            emit_indent(e);
            if (n >= 1) {
                char vname[256];
                sanitize_ident(vname, sizeof(vname), call->u.call.args[0]->u.text);
                if (n >= 2) {
                    char vname2[256];
                    sanitize_ident(vname2, sizeof(vname2), call->u.call.args[1]->u.text);
                    fprintf(e->out, "{ long _tx, _ty; nb_gettouch(&_tx, &_ty); %s = _tx; %s = _ty; }\n",
                            vname, vname2);
                } else {
                    fprintf(e->out, "{ long _tx; nb_gettouch(&_tx, NULL); %s = _tx; }\n", vname);
                }
            } else {
                fputs("nb_gettouch(NULL, NULL);\n", e->out);
            }
            return;
        }
        if (strcasecmp(call->u.call.name, "GETCIRCLEPAD") == 0) {
            emit_indent(e);
            if (n >= 2) {
                char vname[256];
                char vname2[256];
                sanitize_ident(vname, sizeof(vname), call->u.call.args[0]->u.text);
                sanitize_ident(vname2, sizeof(vname2), call->u.call.args[1]->u.text);
                fprintf(e->out, "{ long _dx, _dy; nb_getcirclepad(&_dx, &_dy); %s = _dx; %s = _dy; }\n",
                        vname, vname2);
            } else {
                fputs("nb_getcirclepad(NULL, NULL);\n", e->out);
            }
            return;
        }

        /* DRAWIMAGEEX: img, x, y, scale, angle (5 args) */
        if (strcasecmp(call->u.call.name, "DRAWIMAGEEX") == 0 && n == 5) {
            emit_indent(e);
            fputs("nb_draw_image_ex(", e->out);
            for (int i = 0; i < 5; i++) {
                if (i > 0) fputs(", ", e->out);
                emit_expr(e, call->u.call.args[i]);
            }
            fputs(");\n", e->out);
            return;
        }

        /* General case: call with exact args */
        emit_indent(e);
        fputs(c_name, e->out);
        fputc('(', e->out);
        for (int i = 0; i < n; i++) {
            if (i > 0) fputs(", ", e->out);
            emit_expr(e, call->u.call.args[i]);
        }
        fputs(");\n", e->out);
        return;
    }

    /* User function call as statement */
    emit_indent(e);
    fprintf(e->out, "user_%s(", call->u.call.name);
    for (int i = 0; i < call->u.call.num_args; i++) {
        if (i > 0) fputs(", ", e->out);
        emit_expr(e, call->u.call.args[i]);
    }
    fputs(");\n", e->out);
}

static void emit_stmt(Emitter *e, AstNode *stmt) {
    if (!stmt) return;
    switch (stmt->type) {
        case AST_PRINT:       emit_print(e, stmt); break;
        case AST_ASSIGN:      emit_assign(e, stmt); break;
        case AST_IF:          emit_if(e, stmt); break;
        case AST_WHILE:       emit_while(e, stmt); break;
        case AST_FOR:         emit_for(e, stmt); break;
        case AST_DO_LOOP:     emit_do_loop(e, stmt); break;
        case AST_FUNCTION_DEF: emit_func_def(e, stmt); break;
        case AST_RETURN:      emit_return(e, stmt); break;
        case AST_DIM:         emit_dim(e, stmt); break;
        case AST_ROOM:        emit_room(e, stmt); break;
        case AST_GOTOROOM:    emit_gotoroom(e, stmt); break;
        case AST_IMPORT:      emit_import(e, stmt); break;
        case AST_EXPR_STMT:   emit_expr_stmt(e, stmt); break;
        default:
            emit_indent(e);
            fputs("/* unhandled stmt type */\n", e->out);
            break;
    }
}

static void emit_stmt_list(Emitter *e, AstNode **stmts, int n) {
    for (int i = 0; i < n; i++) {
        emit_stmt(e, stmts[i]);
    }
}

/* ── Top-level emission ───────────────────────────────────────────── */

/* First pass: collect function defs and room defs so we can emit
 * forward declarations before the main body. */
typedef struct {
    AstNode **funcs;
    int       num_funcs;
    AstNode **rooms;
    int       num_rooms;
    AstNode **other_stmts;  /* non-func, non-room top-level statements */
    int       num_other;
} TopLevelInfo;

static void collect_toplevel(AstNode *prog, TopLevelInfo *tli) {
    tli->funcs = NULL; tli->num_funcs = 0;
    tli->rooms = NULL; tli->num_rooms = 0;
    tli->other_stmts = NULL; tli->num_other = 0;

    for (int i = 0; i < prog->u.program.num_stmts; i++) {
        AstNode *s = prog->u.program.stmts[i];
        if (s->type == AST_FUNCTION_DEF) {
            tli->funcs = (AstNode **)realloc(tli->funcs,
                sizeof(AstNode *) * (tli->num_funcs + 1));
            tli->funcs[tli->num_funcs++] = s;
        } else if (s->type == AST_ROOM) {
            tli->rooms = (AstNode **)realloc(tli->rooms,
                sizeof(AstNode *) * (tli->num_rooms + 1));
            tli->rooms[tli->num_rooms++] = s;
        } else {
            tli->other_stmts = (AstNode **)realloc(tli->other_stmts,
                sizeof(AstNode *) * (tli->num_other + 1));
            tli->other_stmts[tli->num_other++] = s;
        }
    }
}

int nb_emit_program(FILE *out, AstNode *prog, const NbTypeChecker *tc) {
    Emitter e;
    e.out = out;
    e.tc = tc;
    e.indent = 0;
    e.in_function = 0;
    e.in_room_update = 0;

    TopLevelInfo tli;
    collect_toplevel(prog, &tli);

    /* ── Header ───────────────────────────────────────────────────── */
    fputs("/* Generated by Sprout compiler. Do not edit by hand. */\n",
          out);
    fputs("#include \"nb_runtime.h\"\n\n", out);

    /* ── Forward declarations for user functions ──────────────────── */
    if (tli.num_funcs > 0) {
        fputs("/* Forward declarations */\n", out);
        for (int i = 0; i < tli.num_funcs; i++) {
            AstNode *fn = tli.funcs[i];
            const NbSymbol *s = nb_tc_lookup(tc, fn->u.func_def.name);
            NbType rt = s ? s->type : TY_VOID;
            if (rt == TY_UNKNOWN) rt = TY_VOID;
            char fn_name[256];
            sanitize_ident(fn_name, sizeof(fn_name), fn->u.func_def.name);
            fprintf(out, "%s user_%s(",
                    nb_type_c_name(rt), fn_name);
            for (int j = 0; j < fn->u.func_def.num_params; j++) {
                char pname[256];
                sanitize_ident(pname, sizeof(pname), fn->u.func_def.params[j]);
                if (j > 0) fputs(", ", out);
                int is_arr = fn->u.func_def.param_is_array ?
                             fn->u.func_def.param_is_array[j] : 0;
                if (is_arr) {
                    fputs("long *", out);
                } else {
                    fputs("long ", out);
                }
                fputs(pname, out);
            }
            fputs(");\n", out);
        }
        fputs("\n", out);
    }

    /* ── Forward declarations for room functions ──────────────────── */
    if (tli.num_rooms > 0) {
        fputs("/* Room forward declarations */\n", out);
        for (int i = 0; i < tli.num_rooms; i++) {
            AstNode *r = tli.rooms[i];
            char fn_name[256];
            snprintf(fn_name, sizeof(fn_name), "room_%s", r->u.room.name);
            for (char *p = fn_name; *p; p++) {
                if (!isalnum((unsigned char)*p) && *p != '_') *p = '_';
            }
            fprintf(out, "void %s_init(void);\n", fn_name);
            fprintf(out, "void %s_update(void);\n", fn_name);
        }
        fputs("\n", out);
    }

    /* ── Global variables ─────────────────────────────────────────── */
    if (tc->num_symbols > 0) {
        fputs("/* Global variables */\n", out);
        for (int i = 0; i < tc->num_symbols; i++) {
            const NbSymbol *s = &tc->symbols[i];
            /* Skip function symbols (those will be emitted as forward decls) */
            int is_function = 0;
            for (int j = 0; j < tli.num_funcs; j++) {
                if (strcasecmp(tli.funcs[j]->u.func_def.name, s->name) == 0) {
                    is_function = 1;
                    break;
                }
            }
            if (is_function) continue;

            char vname[256];
            sanitize_ident(vname, sizeof(vname), s->name);

            if (s->is_array) {
                int size = s->array_size > 0 ? s->array_size : 1;
                fprintf(out, "long %s[%d];\n", vname, size);
            } else if (s->type == TY_STRING) {
                fprintf(out, "char %s[256];\n", vname);
            } else {
                NbType t = s->type == TY_UNKNOWN ? TY_INT : s->type;
                fprintf(out, "%s %s;\n", nb_type_c_name(t), vname);
            }
        }
        fputs("\n", out);
    }

    /* ── User function definitions ────────────────────────────────── */
    for (int i = 0; i < tli.num_funcs; i++) {
        emit_func_def(&e, tli.funcs[i]);
    }

    /* ── Room init/update functions ───────────────────────────────── */
    for (int i = 0; i < tli.num_rooms; i++) {
        emit_room(&e, tli.rooms[i]);
    }

    /* ── nb_user_main() — top-level statements (non-func, non-room) ─ */
    fputs("void nb_user_main(void) {\n", out);
    e.indent = 1;
    emit_stmt_list(&e, tli.other_stmts, tli.num_other);
    e.indent = 0;
    fputs("}\n\n", out);

    /* ── main() ───────────────────────────────────────────────────── */
    fputs("int main(int argc, char **argv) {\n", out);
    fputs("    (void)argc; (void)argv;\n", out);
    fputs("    nb_init();\n", out);
    fputs("    nb_user_main();\n", out);
    fputs("    nb_shutdown();\n", out);
    fputs("    return 0;\n", out);
    fputs("}\n", out);

    /* Free temp arrays */
    free(tli.funcs);
    free(tli.rooms);
    free(tli.other_stmts);

    return 0;
}
