/*
 * CSOS Formula Evaluator — Runtime expression evaluation (non-JIT fallback).
 *
 * Recursive descent parser for compute expressions like:
 *   "h * c / l"
 *   "exp(-(dG + l) ** 2 / (4 * l * V)) * input"
 *   "flux * n / 3"
 *
 * This is the C equivalent of Python's _safe_eval(). It evaluates ANY
 * equation generically — no name-based dispatch. The equation IS the code.
 *
 * When LLVM JIT is available, this is only used as a verification fallback.
 * The JIT compiles the same expressions to native SIMD for production use.
 *
 * Grammar:
 *   expr     → term (('+' | '-') term)*
 *   term     → power (('*' | '/') power)*
 *   power    → unary ('**' unary)*
 *   unary    → ('-' unary) | call
 *   call     → IDENT '(' args ')' | primary
 *   primary  → NUMBER | IDENT | '(' expr ')'
 */
#include "../../lib/membrane.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/* ═══ TOKENIZER ═══ */

typedef enum {
    TOK_NUM, TOK_IDENT, TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH,
    TOK_STARSTAR, TOK_LPAREN, TOK_RPAREN, TOK_COMMA, TOK_EOF
} tok_type_t;

typedef struct {
    tok_type_t type;
    double     num_val;
    char       ident[64];
} token_t;

/* Max nesting depth for recursive descent.
 * Prevents stack overflow on malicious formulas like "(((((...)))))" × 10000.
 * Derived: deepest foundation expression (Marcus) has ~6 levels of nesting.
 * 64 is generous — legitimate compute expressions never exceed 10 levels. */
#define CSOS_PARSE_MAX_DEPTH 64

typedef struct {
    const char *src;
    int         pos;
    token_t     cur;
    /* Variable bindings */
    const double     *params;
    const char      (*param_keys)[32];
    int               param_count;
    double            signal;
    /* Recursion depth guard */
    int               depth;
} parser_t;

static void next_token(parser_t *p) {
    while (p->src[p->pos] && isspace((unsigned char)p->src[p->pos])) p->pos++;

    if (!p->src[p->pos]) { p->cur.type = TOK_EOF; return; }

    char c = p->src[p->pos];

    /* Number */
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)p->src[p->pos+1]))) {
        char *end;
        p->cur.num_val = strtod(p->src + p->pos, &end);
        p->pos = (int)(end - p->src);
        p->cur.type = TOK_NUM;
        return;
    }

    /* Identifier */
    if (isalpha((unsigned char)c) || c == '_') {
        int i = 0;
        while ((isalnum((unsigned char)p->src[p->pos]) || p->src[p->pos] == '_') && i < 63)
            p->cur.ident[i++] = p->src[p->pos++];
        p->cur.ident[i] = 0;
        p->cur.type = TOK_IDENT;
        return;
    }

    /* Operators */
    if (c == '*' && p->src[p->pos+1] == '*') { p->pos += 2; p->cur.type = TOK_STARSTAR; return; }
    if (c == '+') { p->pos++; p->cur.type = TOK_PLUS; return; }
    if (c == '-') { p->pos++; p->cur.type = TOK_MINUS; return; }
    if (c == '*') { p->pos++; p->cur.type = TOK_STAR; return; }
    if (c == '/') { p->pos++; p->cur.type = TOK_SLASH; return; }
    if (c == '(') { p->pos++; p->cur.type = TOK_LPAREN; return; }
    if (c == ')') { p->pos++; p->cur.type = TOK_RPAREN; return; }
    if (c == ',') { p->pos++; p->cur.type = TOK_COMMA; return; }

    /* Unknown — skip */
    p->pos++;
    next_token(p);
}

/* ═══ VARIABLE LOOKUP ═══ */

static double lookup_var(parser_t *p, const char *name) {
    /* Built-in variables */
    if (strcmp(name, "signal") == 0) return p->signal;
    if (strcmp(name, "input") == 0) return fabs(p->signal) > 1e-10 ? fabs(p->signal) : 1e-10;
    if (strcmp(name, "pi") == 0) return 3.14159265358979323846;

    /* Parameter lookup — return abs(value) per physics convention */
    for (int i = 0; i < p->param_count; i++) {
        if (strcmp(p->param_keys[i], name) == 0) {
            double v = fabs(p->params[i]);
            return v > 1e-10 ? v : 1e-10;
        }
    }

    /* Unknown variable — return small epsilon to avoid div-by-zero */
    return 1e-10;
}

/* ═══ RECURSIVE DESCENT PARSER ═══ */

static double parse_expr(parser_t *p);

/* Depth guard macro — returns 0.0 (safe fallback) on overflow */
#define DEPTH_CHECK(p) do { \
    if ((p)->depth >= CSOS_PARSE_MAX_DEPTH) return 0.0; \
    (p)->depth++; \
} while(0)
#define DEPTH_RETURN(p) do { (p)->depth--; } while(0)

static double parse_primary(parser_t *p) {
    DEPTH_CHECK(p);
    double result = 0.0;

    if (p->cur.type == TOK_NUM) {
        result = p->cur.num_val;
        next_token(p);
        DEPTH_RETURN(p);
        return result;
    }
    if (p->cur.type == TOK_LPAREN) {
        next_token(p); /* skip ( */
        result = parse_expr(p);
        if (p->cur.type == TOK_RPAREN) next_token(p); /* skip ) */
        DEPTH_RETURN(p);
        return result;
    }
    if (p->cur.type == TOK_IDENT) {
        char name[64];
        strncpy(name, p->cur.ident, 63); name[63] = 0;
        next_token(p);

        /* Function call? */
        if (p->cur.type == TOK_LPAREN) {
            next_token(p); /* skip ( */
            double arg1 = parse_expr(p);

            if (p->cur.type == TOK_COMMA) {
                /* Two-argument function */
                next_token(p); /* skip , */
                double arg2 = parse_expr(p);
                if (p->cur.type == TOK_RPAREN) next_token(p);

                if (strcmp(name, "min") == 0) result = arg1 < arg2 ? arg1 : arg2;
                else if (strcmp(name, "max") == 0) result = arg1 > arg2 ? arg1 : arg2;
                else if (strcmp(name, "pow") == 0) result = pow(arg1, arg2);
                else result = arg1; /* unknown function */
                DEPTH_RETURN(p);
                return result;
            }

            if (p->cur.type == TOK_RPAREN) next_token(p);

            /* One-argument functions — clamped for safety like Python _safe_eval */
            if (strcmp(name, "abs") == 0) result = fabs(arg1);
            else if (strcmp(name, "exp") == 0) {
                double clamped = arg1 > CSOS_EXP_CLAMP ? CSOS_EXP_CLAMP : (arg1 < -CSOS_EXP_CLAMP ? -CSOS_EXP_CLAMP : arg1);
                result = exp(clamped);
            }
            else if (strcmp(name, "sqrt") == 0) result = sqrt(arg1 > 0 ? arg1 : 0);
            else if (strcmp(name, "log") == 0) result = log(arg1 > 1e-10 ? arg1 : 1e-10);
            else result = arg1; /* unknown function */
            DEPTH_RETURN(p);
            return result;
        }

        /* Variable reference */
        result = lookup_var(p, name);
        DEPTH_RETURN(p);
        return result;
    }

    /* Fallback */
    next_token(p);
    DEPTH_RETURN(p);
    return 0;
}

static double parse_unary(parser_t *p) {
    DEPTH_CHECK(p);
    double result;
    if (p->cur.type == TOK_MINUS) {
        next_token(p);
        result = -parse_unary(p);
    } else {
        result = parse_primary(p);
    }
    DEPTH_RETURN(p);
    return result;
}

static double parse_power(parser_t *p) {
    DEPTH_CHECK(p);
    double left = parse_unary(p);
    while (p->cur.type == TOK_STARSTAR) {
        next_token(p);
        double right = parse_unary(p);
        left = pow(left, right);
    }
    DEPTH_RETURN(p);
    return left;
}

static double parse_term(parser_t *p) {
    DEPTH_CHECK(p);
    double left = parse_power(p);
    while (p->cur.type == TOK_STAR || p->cur.type == TOK_SLASH) {
        tok_type_t op = p->cur.type;
        next_token(p);
        double right = parse_power(p);
        if (op == TOK_STAR) left *= right;
        else left /= (fabs(right) > 1e-10 ? right : 1e-10);
    }
    DEPTH_RETURN(p);
    return left;
}

static double parse_expr(parser_t *p) {
    DEPTH_CHECK(p);
    double left = parse_term(p);
    while (p->cur.type == TOK_PLUS || p->cur.type == TOK_MINUS) {
        tok_type_t op = p->cur.type;
        next_token(p);
        double right = parse_term(p);
        if (op == TOK_PLUS) left += right;
        else left -= right;
    }
    DEPTH_RETURN(p);
    return left;
}

/* ═══ PUBLIC API ═══ */

double csos_formula_eval(const char *compute, const double *params,
                         const char param_keys[][32], int param_count,
                         double signal) {
    if (!compute || !compute[0]) return signal;

    parser_t p = {0};
    p.src = compute;
    p.pos = 0;
    p.params = params;
    p.param_keys = param_keys;
    p.param_count = param_count;
    p.signal = signal;

    next_token(&p);
    double result = parse_expr(&p);

    /* Safety: if result is NaN or Inf, fall back to product of params */
    if (result != result || result == 1.0/0.0 || result == -1.0/0.0) {
        double prod = 1.0;
        for (int i = 0; i < param_count; i++)
            prod *= fabs(params[i]) > 1e-10 ? fabs(params[i]) : 1e-10;
        return prod;
    }

    return result;
}
