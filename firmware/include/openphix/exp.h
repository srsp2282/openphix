/* Interpreter for the expression language of Exp.BIN
 * (docs/data-image-format.md, section 7).
 *
 * An expression converts the bytes of an ECU response (X1..Xn) into a
 * result Y: a number, a string id, or a piece of text. It is evaluated
 * straight from the source text, no compilation step, so the only RAM cost
 * is the text buffer and a small value stack. */
#ifndef OPENPHIX_EXP_H
#define OPENPHIX_EXP_H

#include <stddef.h>
#include <stdint.h>

#include "openphix/res.h"

#define EXP_TEXT_MAX 64      /* longest text a function may build */
#define EXP_SRC_MAX 2560     /* longest record in any image is 2204 bytes */
#define EXP_MAX_DEPTH 4      /* nesting of ID() */

enum exp_kind {
    EXP_NONE = 0,   /* nothing assigned (error or empty) */
    EXP_NUM,        /* numeric result */
    EXP_STRID,      /* a string id, result of string(a,b,c,d) */
    EXP_TEXT        /* text built by ASCII/SPRINTF/HEX/CHARCONVERT */
};

struct exp_value {
    enum exp_kind kind;
    double num;
    uint32_t strid;
    char text[EXP_TEXT_MAX];
};

struct exp_input {
    const uint8_t *x;    /* X1 is x[0] */
    int n;
};

/* Result of RANGE(lo, hi): set on the context when seen. */
struct exp_range {
    int valid;
    int boundless;
    double lo, hi;
};

struct exp_ctx {
    struct exp_input in;
    struct exp_range range;
    int depth;
    int error;               /* nonzero when a parse error was hit */
    const char *error_at;    /* position in the source, for diagnostics */
    /* hook for TRANSID(code, base, table): may be NULL */
    int (*transid)(struct exp_ctx *, const struct exp_value *code,
                   const struct exp_value *base, const struct exp_value *table,
                   struct exp_value *out);
};

/* Point the interpreter at the image's Exp.BIN. */
int exp_init(const struct res *image);

/* Evaluate the source text `src` with inputs `in`. Returns 0 when Y was
 * assigned, -1 on a parse error with nothing assigned. Parse errors after
 * an assignment are tolerated (the database has a few typos). */
int exp_eval_text(struct exp_ctx *ctx, const char *src, struct exp_value *out);

/* Evaluate the record `id` of Exp.BIN. Returns -1 when the id is unknown. */
int exp_eval(struct exp_ctx *ctx, uint32_t id, struct exp_value *out);

/* Fetch the source of a record into buf (cap bytes). Returns length or -1. */
int exp_source(uint32_t id, char *buf, size_t cap);

/* Format a value for display: numbers with up to 6 significant decimals,
 * string ids through the string tables, text as is. */
const char *exp_format(const struct exp_value *v, char *buf, size_t cap);

#endif
