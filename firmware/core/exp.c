/* Recursive descent interpreter for the Exp.BIN language.
 *
 * Grammar, as found in the data (section 7 of the format document):
 *
 *   program  := stmt*
 *   stmt     := 'if' '(' expr ')' stmt [ 'else' stmt ]
 *             | ('Y'|'y') '=' expr ';'
 *             | ';'
 *   expr     := or
 *   or       := and ( '||' and )*
 *   and      := bor ( '&&' bor )*
 *   bor      := band ( '|' band )*
 *   band     := eq ( '&' eq )*
 *   eq       := rel ( ('=='|'!=') rel )*
 *   rel      := shift ( ('<'|'>'|'<='|'>=') shift )*
 *   shift    := add ( ('>>'|'<<') add )*
 *   add      := mul ( ('+'|'-') mul )*
 *   mul      := unary ( ('*'|'/'|'%') unary )*
 *   unary    := ('-'|'!') unary | primary
 *   primary  := number | '[' text ']' | var | func '(' args ')' | '(' expr ')'
 *   var      := [Xx] digits? ( '.bit' digit )?
 *
 * Values carry a kind (number, string id, text). Arithmetic on text gives
 * 0; comparisons between text values compare the text.
 */
#include <ctype.h>
#include <stdlib.h>
#include <strings.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "openphix/exp.h"
#include "openphix/str.h"
#include "openphix/table.h"

static struct res exp_file;
static char src_buf[EXP_MAX_DEPTH][EXP_SRC_MAX];

int exp_init(const struct res *image)
{
    return res_open(image, "Exp.BIN", &exp_file);
}

int exp_source(uint32_t id, char *buf, size_t cap)
{
    struct res rec;
    uint16_t len;
    if (cap == 0 || table_find(&exp_file, id, &rec) != 0)
        return -1;
    len = res_u16(&rec, 0);
    if (len == 0)
        return -1;
    if ((size_t)len > cap)
        len = (uint16_t)cap;
    if (res_read(&rec, 2, buf, len) != 0)
        return -1;
    buf[len - 1] = 0;
    return (int)strlen(buf);
}

/* ---- parser state ------------------------------------------------------ */

struct parser {
    const char *p;
    struct exp_ctx *ctx;
    struct exp_value *y;
    int assigned;
};

static void skip_ws(struct parser *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\r' || *ps->p == '\n')
        ps->p++;
}

static int peek(struct parser *ps, const char *tok)
{
    size_t n = strlen(tok);
    skip_ws(ps);
    return strncmp(ps->p, tok, n) == 0;
}

static int accept(struct parser *ps, const char *tok)
{
    if (!peek(ps, tok))
        return 0;
    ps->p += strlen(tok);
    return 1;
}

static void fail(struct parser *ps)
{
    if (!ps->ctx->error) {
        ps->ctx->error = 1;
        ps->ctx->error_at = ps->p;
    }
}

static int keyword(struct parser *ps, const char *kw)
{
    size_t n = strlen(kw);
    skip_ws(ps);
    if (strncasecmp(ps->p, kw, n) != 0)
        return 0;
    if (isalnum((unsigned char)ps->p[n]) || ps->p[n] == '_')
        return 0;
    ps->p += n;
    return 1;
}

static void set_num(struct exp_value *v, double d)
{
    v->kind = EXP_NUM;
    v->num = d;
    v->strid = 0;
    v->text[0] = 0;
}

static double as_num(const struct exp_value *v)
{
    switch (v->kind) {
    case EXP_NUM: return v->num;
    case EXP_STRID: return (double)v->strid;
    default: return 0.0;
    }
}

static int truthy(const struct exp_value *v)
{
    return v->kind == EXP_TEXT ? v->text[0] != 0 : as_num(v) != 0.0;
}

static void parse_expr(struct parser *ps, struct exp_value *out);
static void parse_stmt(struct parser *ps);

/* Input byte k (1-based); out of range reads as 0. */
static double input_byte(struct parser *ps, int k)
{
    const struct exp_input *in = &ps->ctx->in;
    if (k < 1 || k > in->n)
        return 0.0;
    return (double)in->x[k - 1];
}

static double parse_number(struct parser *ps)
{
    const char *s = ps->p;
    double v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        while (isxdigit((unsigned char)*s)) {
            int c = *s++;
            v = v * 16 + (isdigit(c) ? c - '0' : (tolower(c) - 'a' + 10));
        }
    } else {
        while (isdigit((unsigned char)*s))
            v = v * 10 + (*s++ - '0');
        if (*s == '.' && isdigit((unsigned char)s[1])) {
            double f = 0.1;
            s++;
            while (isdigit((unsigned char)*s)) {
                v += (*s++ - '0') * f;
                f /= 10;
            }
        }
    }
    ps->p = s;
    return v;
}

/* [literal] */
static void parse_bracket_text(struct parser *ps, struct exp_value *out)
{
    size_t n = 0;
    ps->p++;                      /* '[' */
    out->kind = EXP_TEXT;
    out->num = 0;
    out->strid = 0;
    while (*ps->p && *ps->p != ']') {
        if (n + 1 < sizeof out->text)
            out->text[n++] = *ps->p;
        ps->p++;
    }
    out->text[n] = 0;
    if (*ps->p == ']')
        ps->p++;
    else
        fail(ps);
}

#define MAX_ARGS 48

static int parse_args(struct parser *ps, struct exp_value *args, int cap)
{
    int n = 0;
    if (!accept(ps, "(")) {
        fail(ps);
        return 0;
    }
    if (accept(ps, ")"))
        return 0;
    for (;;) {
        struct exp_value tmp;
        struct exp_value *dst = n < cap ? &args[n] : &tmp;
        if (peek(ps, "[")) {
            skip_ws(ps);
            parse_bracket_text(ps, dst);
        } else if (keyword(ps, "boundless")) {
            set_num(dst, INFINITY);
        } else {
            parse_expr(ps, dst);
        }
        n++;
        if (accept(ps, ",")) {
            if (accept(ps, ")"))   /* trailing comma: "X15,X16,)" */
                return n;
            continue;
        }
        if (!accept(ps, ")"))
            fail(ps);              /* tolerate a missing ')' */
        return n;
    }
}

static void append_char(struct exp_value *v, size_t *n, char c)
{
    if (*n + 1 < sizeof v->text)
        v->text[(*n)++] = c;
    v->text[*n] = 0;
}

/* Tiny printf for %c, %X, %02X, %d, %s as used by the database. */
static void do_sprintf(struct exp_value *out, const struct exp_value *args, int n)
{
    const char *f = args[0].kind == EXP_TEXT ? args[0].text : "";
    int ai = 1;
    size_t len = 0;
    char tmp[32];
    out->kind = EXP_TEXT;
    out->num = 0;
    out->strid = 0;
    out->text[0] = 0;
    while (*f) {
        if (*f != '%') {
            append_char(out, &len, *f++);
            continue;
        }
        f++;
        if (*f == '%') {
            append_char(out, &len, '%');
            f++;
            continue;
        }
        {
            int width = 0, zero = 0;
            if (*f == '0') { zero = 1; f++; }
            while (isdigit((unsigned char)*f))
                width = width * 10 + (*f++ - '0');
            if (*f == 0)
                break;
            {
                char conv = *f++;
                const struct exp_value *a = ai < n ? &args[ai] : NULL;
                ai++;
                if (!a)
                    continue;
                switch (conv) {
                case 'c':
                    append_char(out, &len, (char)(int)as_num(a));
                    break;
                case 'X': case 'x':
                    snprintf(tmp, sizeof tmp, zero ? "%0*X" : "%*X", width, (unsigned)(long)as_num(a));
                    for (const char *t = tmp; *t; t++) append_char(out, &len, *t);
                    break;
                case 's':
                    if (a->kind == EXP_TEXT)
                        for (const char *t = a->text; *t; t++) append_char(out, &len, *t);
                    break;
                default:
                    snprintf(tmp, sizeof tmp, zero ? "%0*ld" : "%*ld", width, (long)as_num(a));
                    for (const char *t = tmp; *t; t++) append_char(out, &len, *t);
                    break;
                }
            }
        }
    }
}

static void call(struct parser *ps, const char *name, size_t namelen, struct exp_value *out)
{
    struct exp_value args[MAX_ARGS];
    int n = parse_args(ps, args, MAX_ARGS);
    char fn[16];
    size_t i;
    for (i = 0; i < namelen && i + 1 < sizeof fn; i++)
        fn[i] = (char)tolower((unsigned char)name[i]);
    fn[i] = 0;

    if (strcmp(fn, "string") == 0) {
        out->kind = EXP_STRID;
        out->num = 0;
        out->text[0] = 0;
        out->strid = 0;
        for (int k = 0; k < 4 && k < n; k++)
            out->strid = (out->strid << 8) | ((uint32_t)(long)as_num(&args[k]) & 0xFF);
    } else if (strcmp(fn, "strcmp") == 0) {
        const char *a = n > 0 && args[0].kind == EXP_TEXT ? args[0].text : "";
        const char *b = n > 1 && args[1].kind == EXP_TEXT ? args[1].text : "";
        set_num(out, strcmp(a, b) == 0 ? 0 : 1);
    } else if (strcmp(fn, "ascii") == 0) {
        size_t len = 0;
        out->kind = EXP_TEXT;
        out->num = 0;
        out->strid = 0;
        out->text[0] = 0;
        for (int k = 0; k < n && k < MAX_ARGS; k++) {
            int c = (int)as_num(&args[k]);
            if (c == 0)
                break;
            append_char(out, &len, (char)c);
        }
    } else if (strcmp(fn, "sprintf") == 0 || strcmp(fn, "charconvert") == 0) {
        do_sprintf(out, args, n < MAX_ARGS ? n : MAX_ARGS);
    } else if (strcmp(fn, "hex") == 0) {
        size_t len = 0;
        char tmp[4];
        out->kind = EXP_TEXT;
        out->num = 0;
        out->strid = 0;
        out->text[0] = 0;
        for (int k = 0; k < n && k < MAX_ARGS; k++) {
            snprintf(tmp, sizeof tmp, "%02X", (unsigned)(long)as_num(&args[k]) & 0xFF);
            append_char(out, &len, tmp[0]);
            append_char(out, &len, tmp[1]);
        }
    } else if (strcmp(fn, "int") == 0) {
        set_num(out, n > 0 ? trunc(as_num(&args[0])) : 0);
    } else if (strcmp(fn, "id") == 0) {
        uint32_t id = n > 0 ? (uint32_t)(long)as_num(&args[0]) : 0;
        if (exp_eval(ps->ctx, id, out) != 0)
            set_num(out, 0);
    } else if (strcmp(fn, "transid") == 0) {
        if (n >= 3 && ps->ctx->transid &&
            ps->ctx->transid(ps->ctx, &args[0], &args[1], &args[2], out) == 0)
            ;
        else if (n >= 1)
            *out = args[0];
        else
            set_num(out, 0);
    } else if (strcmp(fn, "range") == 0) {
        ps->ctx->range.valid = 1;
        ps->ctx->range.lo = n > 0 ? as_num(&args[0]) : 0;
        ps->ctx->range.hi = n > 1 ? as_num(&args[1]) : INFINITY;
        ps->ctx->range.boundless = n > 1 && isinf(ps->ctx->range.hi);
        set_num(out, 1);
    } else {
        fail(ps);
        set_num(out, 0);
    }
}

static void parse_primary(struct parser *ps, struct exp_value *out)
{
    skip_ws(ps);
    if (isdigit((unsigned char)*ps->p)) {
        set_num(out, parse_number(ps));
        if (*ps->p == '(') {       /* "0.0072(x1*256+x2)": the '*' is missing */
            struct exp_value r;
            ps->p++;
            parse_expr(ps, &r);
            if (!accept(ps, ")"))
                fail(ps);
            set_num(out, out->num * as_num(&r));
        }
        return;
    }
    if (*ps->p == '[') {
        parse_bracket_text(ps, out);
        return;
    }
    if (accept(ps, "(")) {
        parse_expr(ps, out);
        if (!accept(ps, ")"))
            fail(ps);
        return;
    }
    if (isalpha((unsigned char)*ps->p) || *ps->p == '_') {
        const char *name = ps->p;
        size_t n = 0;
        while (isalnum((unsigned char)ps->p[n]) || ps->p[n] == '_')
            n++;
        skip_ws(ps);
        if (ps->p[n] == '(' || (ps->p[n] == ' ' && ps->p[n + 1] == '(')) {
            ps->p += n;
            call(ps, name, n, out);
            return;
        }
        /* variable: X, Xk, Y (reads the current result) */
        if ((name[0] == 'X' || name[0] == 'x') && (n == 1 || isdigit((unsigned char)name[1]))) {
            int k = 1;
            if (n > 1)
                k = atoi(name + 1);
            ps->p += n;
            set_num(out, input_byte(ps, k));
            if (ps->p[0] == '.' && strncasecmp(ps->p + 1, "bit", 3) == 0 && isdigit((unsigned char)ps->p[4])) {
                int bit = ps->p[4] - '0';
                ps->p += 5;
                set_num(out, ((int)out->num >> bit) & 1);
            }
            return;
        }
        if ((name[0] == 'Y' || name[0] == 'y') && n == 1) {
            ps->p += n;
            *out = *ps->y;
            if (out->kind == EXP_NONE)
                set_num(out, 0);
            return;
        }
        /* a hex number written without 0x (database typo "FF") */
        {
            size_t i;
            for (i = 0; i < n && isxdigit((unsigned char)name[i]); i++)
                ;
            if (i == n) {
                double v = 0;
                for (i = 0; i < n; i++) {
                    int c = name[i];
                    v = v * 16 + (isdigit(c) ? c - '0' : (tolower(c) - 'a' + 10));
                }
                ps->p += n;
                set_num(out, v);
                return;
            }
        }
        ps->p += n;
        fail(ps);
        set_num(out, 0);
        return;
    }
    fail(ps);
    set_num(out, 0);
    if (*ps->p)
        ps->p++;
}

static void parse_unary(struct parser *ps, struct exp_value *out)
{
    if (accept(ps, "-")) {
        parse_unary(ps, out);
        set_num(out, -as_num(out));
        return;
    }
    if (accept(ps, "!")) {
        parse_unary(ps, out);
        set_num(out, !truthy(out));
        return;
    }
    parse_primary(ps, out);
}

static void parse_mul(struct parser *ps, struct exp_value *out)
{
    parse_unary(ps, out);
    for (;;) {
        struct exp_value r;
        if (accept(ps, "*")) {
            parse_unary(ps, &r);
            set_num(out, as_num(out) * as_num(&r));
        } else if (accept(ps, "/")) {
            parse_unary(ps, &r);
            set_num(out, as_num(&r) != 0 ? as_num(out) / as_num(&r) : 0);
        } else if (accept(ps, "%")) {
            parse_unary(ps, &r);
            set_num(out, (long)as_num(&r) != 0 ? (double)((long)as_num(out) % (long)as_num(&r)) : 0);
        } else {
            return;
        }
    }
}

static void parse_add(struct parser *ps, struct exp_value *out)
{
    parse_mul(ps, out);
    for (;;) {
        struct exp_value r;
        if (accept(ps, "+")) {
            parse_mul(ps, &r);
            set_num(out, as_num(out) + as_num(&r));
        } else if (peek(ps, "-") ) {
            ps->p++;
            parse_mul(ps, &r);
            set_num(out, as_num(out) - as_num(&r));
        } else {
            return;
        }
    }
}

static void parse_shift(struct parser *ps, struct exp_value *out)
{
    parse_add(ps, out);
    for (;;) {
        struct exp_value r;
        if (accept(ps, ">>")) {
            parse_add(ps, &r);
            set_num(out, (double)((long)as_num(out) >> (int)as_num(&r)));
        } else if (accept(ps, "<<")) {
            parse_add(ps, &r);
            set_num(out, (double)((long)as_num(out) << (int)as_num(&r)));
        } else {
            return;
        }
    }
}

static void parse_rel(struct parser *ps, struct exp_value *out)
{
    parse_shift(ps, out);
    for (;;) {
        struct exp_value r;
        if (accept(ps, "<=")) {
            parse_shift(ps, &r);
            set_num(out, as_num(out) <= as_num(&r));
        } else if (accept(ps, ">=")) {
            parse_shift(ps, &r);
            set_num(out, as_num(out) >= as_num(&r));
        } else if (peek(ps, "<") && !peek(ps, "<<")) {
            ps->p++;
            parse_shift(ps, &r);
            set_num(out, as_num(out) < as_num(&r));
        } else if (peek(ps, ">") && !peek(ps, ">>")) {
            ps->p++;
            parse_shift(ps, &r);
            set_num(out, as_num(out) > as_num(&r));
        } else {
            return;
        }
    }
}

static int values_equal(const struct exp_value *a, const struct exp_value *b)
{
    if (a->kind == EXP_TEXT && b->kind == EXP_TEXT)
        return strcmp(a->text, b->text) == 0;
    return as_num(a) == as_num(b);
}

static void parse_eq(struct parser *ps, struct exp_value *out)
{
    parse_rel(ps, out);
    for (;;) {
        struct exp_value r;
        if (accept(ps, "==")) {
            parse_rel(ps, &r);
            set_num(out, values_equal(out, &r));
        } else if (accept(ps, "!=")) {
            parse_rel(ps, &r);
            set_num(out, !values_equal(out, &r));
        } else {
            return;
        }
    }
}

static void parse_band(struct parser *ps, struct exp_value *out)
{
    parse_eq(ps, out);
    while (peek(ps, "&") && !peek(ps, "&&")) {
        struct exp_value r;
        ps->p++;
        parse_eq(ps, &r);
        set_num(out, (double)((long)as_num(out) & (long)as_num(&r)));
    }
}

static void parse_bor(struct parser *ps, struct exp_value *out)
{
    parse_band(ps, out);
    while (peek(ps, "|") && !peek(ps, "||")) {
        struct exp_value r;
        ps->p++;
        parse_band(ps, &r);
        set_num(out, (double)((long)as_num(out) | (long)as_num(&r)));
    }
}

static void parse_and(struct parser *ps, struct exp_value *out)
{
    parse_bor(ps, out);
    while (accept(ps, "&&")) {
        struct exp_value r;
        parse_bor(ps, &r);
        set_num(out, truthy(out) && truthy(&r));
    }
}

static void parse_or(struct parser *ps, struct exp_value *out)
{
    parse_and(ps, out);
    while (accept(ps, "||")) {
        struct exp_value r;
        parse_and(ps, &r);
        set_num(out, truthy(out) || truthy(&r));
    }
}

static void parse_expr(struct parser *ps, struct exp_value *out)
{
    parse_or(ps, out);
}

/* Skip a statement without evaluating it (the untaken branch). */
static void skip_stmt(struct parser *ps)
{
    int depth = 0;
    skip_ws(ps);
    if (keyword(ps, "if")) {
        /* condition in parentheses */
        skip_ws(ps);
        if (*ps->p == '(') {
            depth = 0;
            do {
                if (*ps->p == '(') depth++;
                else if (*ps->p == ')') depth--;
                else if (*ps->p == 0) return;
                ps->p++;
            } while (depth > 0);
        }
        skip_stmt(ps);
        if (keyword(ps, "else"))
            skip_stmt(ps);
        return;
    }
    /* assignment: up to the ';' outside brackets/parentheses */
    depth = 0;
    while (*ps->p) {
        char c = *ps->p++;
        if (c == '(' || c == '[') depth++;
        else if (c == ')' || c == ']') depth--;
        else if (c == ';' && depth <= 0) return;
    }
}

static void parse_stmt(struct parser *ps)
{
    struct exp_value cond;
    skip_ws(ps);
    if (*ps->p == 0)
        return;
    if (accept(ps, ";"))
        return;
    if (keyword(ps, "if")) {
        if (!accept(ps, "(")) {
            fail(ps);
            return;
        }
        parse_expr(ps, &cond);
        if (!accept(ps, ")"))
            fail(ps);              /* tolerate */
        if (truthy(&cond)) {
            parse_stmt(ps);
            if (keyword(ps, "else"))
                skip_stmt(ps);
        } else {
            skip_stmt(ps);
            if (keyword(ps, "else"))
                parse_stmt(ps);
        }
        return;
    }
    if ((*ps->p == 'Y' || *ps->p == 'y') && (ps->p[1] == '=' || ps->p[1] == ' ')) {
        struct exp_value v;
        ps->p++;
        if (!accept(ps, "=")) {
            fail(ps);
            return;
        }
        if (peek(ps, "if")) {      /* "Y=if(...) Y=...;": the "Y=" is noise */
            parse_stmt(ps);
            return;
        }
        parse_expr(ps, &v);
        *ps->y = v;
        ps->assigned = 1;
        if (!accept(ps, ";") && *ps->p != 0) {
            /* tolerate a stray ')' or missing ';' */
            if (*ps->p == ')')
                ps->p++;
            else
                fail(ps);
        }
        return;
    }
    /* a bare expression as a statement ("else ID(0x01100026);"): its
     * value is the result */
    if (isalpha((unsigned char)*ps->p) || isdigit((unsigned char)*ps->p) || *ps->p == '(') {
        const char *before = ps->p;
        int had_error = ps->ctx->error;
        parse_expr(ps, &cond);
        if (ps->p != before && ps->ctx->error == had_error) {
            *ps->y = cond;
            ps->assigned = 1;
            accept(ps, ";");
            return;
        }
    }
    fail(ps);
    if (*ps->p)
        ps->p++;
}

int exp_eval_text(struct exp_ctx *ctx, const char *src, struct exp_value *out)
{
    struct parser ps;
    int guard = 0;
    ps.p = src;
    ps.ctx = ctx;
    ps.y = out;
    ps.assigned = 0;
    out->kind = EXP_NONE;
    out->num = 0;
    out->strid = 0;
    out->text[0] = 0;
    ctx->error = 0;
    ctx->error_at = NULL;
    for (;;) {
        const char *before;
        skip_ws(&ps);
        if (*ps.p == 0)
            break;
        before = ps.p;
        parse_stmt(&ps);
        if (ps.p == before) {
            /* no progress: skip a character so a typo cannot hang us */
            fail(&ps);
            ps.p++;
        }
        if (++guard > 4096)
            break;
    }
    return ps.assigned ? 0 : -1;
}

int exp_eval(struct exp_ctx *ctx, uint32_t id, struct exp_value *out)
{
    int rc;
    if (ctx->depth >= EXP_MAX_DEPTH)
        return -1;
    if (exp_source(id, src_buf[ctx->depth], EXP_SRC_MAX) < 0)
        return -1;
    ctx->depth++;
    {
        int err = ctx->error;
        const char *at = ctx->error_at;
        rc = exp_eval_text(ctx, src_buf[ctx->depth - 1], out);
        ctx->error |= err;
        if (err)
            ctx->error_at = at;
    }
    ctx->depth--;
    return rc;
}

const char *exp_format(const struct exp_value *v, char *buf, size_t cap)
{
    switch (v->kind) {
    case EXP_NUM:
        if (v->num == floor(v->num) && fabs(v->num) < 1e12)
            snprintf(buf, cap, "%ld", (long)v->num);
        else
            snprintf(buf, cap, "%.6g", v->num);
        break;
    case EXP_STRID:
        str_get_or_id(v->strid, buf, cap);
        break;
    case EXP_TEXT:
        snprintf(buf, cap, "%s", v->text);
        break;
    default:
        snprintf(buf, cap, "-");
        break;
    }
    return buf;
}
