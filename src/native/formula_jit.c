/*
 * CSOS Formula JIT — Compiles compute expressions to LLVM IR at runtime.
 *
 * This is gene expression: the compute string (mRNA) gets translated
 * into native machine code (protein) via LLVM (ribosome).
 *
 * For each atom's compute expression (e.g., "h * c / l"), we:
 *   1. Parse the expression into an AST
 *   2. Generate LLVM IR that evaluates it
 *   3. JIT compile to native SIMD code
 *   4. Cache the function pointer
 *
 * When Calvin synthesizes a new atom (N → N+1), we recompile all
 * formulas including the new one — hot-swap with zero downtime.
 *
 * The same recursive descent parser as formula_eval.c, but emitting
 * LLVM IR instead of computing directly.
 */

#ifdef CSOS_HAS_LLVM

#include "../../lib/membrane.h"
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ═══ FORMULA JIT STATE ═══ */

typedef double (*formula_fn_t)(const double *params, int param_count, double signal);

typedef struct {
    LLVMModuleRef          module;
    LLVMExecutionEngineRef engine;
    LLVMContextRef         context;
    int                    initialized;
    int                    atom_count_compiled;
    formula_fn_t           fns[CSOS_MAX_ATOMS]; /* One compiled function per atom */
} formula_jit_t;

static formula_jit_t FJIT = {0};

/* ═══ TOKENIZER (same as formula_eval.c) ═══ */

typedef enum {
    FTOK_NUM, FTOK_IDENT, FTOK_PLUS, FTOK_MINUS, FTOK_STAR, FTOK_SLASH,
    FTOK_STARSTAR, FTOK_LPAREN, FTOK_RPAREN, FTOK_COMMA, FTOK_EOF
} ftok_type_t;

typedef struct {
    ftok_type_t type;
    double      num_val;
    char        ident[64];
} ftok_t;

typedef struct {
    const char    *src;
    int            pos;
    ftok_t         cur;
    /* LLVM IR builder state */
    LLVMBuilderRef builder;
    LLVMModuleRef  module;
    LLVMContextRef context;
    LLVMValueRef   params_ptr;   /* Pointer to params array */
    LLVMValueRef   param_count;  /* i32 param count */
    LLVMValueRef   signal_val;   /* double signal */
    /* Atom metadata for variable resolution */
    const char   (*param_keys)[32];
    int            n_params;
} ir_parser_t;

static void fnext(ir_parser_t *p) {
    while (p->src[p->pos] && isspace((unsigned char)p->src[p->pos])) p->pos++;
    if (!p->src[p->pos]) { p->cur.type = FTOK_EOF; return; }
    char c = p->src[p->pos];

    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)p->src[p->pos+1]))) {
        char *end;
        p->cur.num_val = strtod(p->src + p->pos, &end);
        p->pos = (int)(end - p->src);
        p->cur.type = FTOK_NUM;
        return;
    }
    if (isalpha((unsigned char)c) || c == '_') {
        int i = 0;
        while ((isalnum((unsigned char)p->src[p->pos]) || p->src[p->pos] == '_') && i < 63)
            p->cur.ident[i++] = p->src[p->pos++];
        p->cur.ident[i] = 0;
        p->cur.type = FTOK_IDENT;
        return;
    }
    if (c == '*' && p->src[p->pos+1] == '*') { p->pos += 2; p->cur.type = FTOK_STARSTAR; return; }
    if (c == '+') { p->pos++; p->cur.type = FTOK_PLUS; return; }
    if (c == '-') { p->pos++; p->cur.type = FTOK_MINUS; return; }
    if (c == '*') { p->pos++; p->cur.type = FTOK_STAR; return; }
    if (c == '/') { p->pos++; p->cur.type = FTOK_SLASH; return; }
    if (c == '(') { p->pos++; p->cur.type = FTOK_LPAREN; return; }
    if (c == ')') { p->pos++; p->cur.type = FTOK_RPAREN; return; }
    if (c == ',') { p->pos++; p->cur.type = FTOK_COMMA; return; }
    p->pos++;
    fnext(p);
}

/* ═══ IR HELPERS ═══ */

static LLVMTypeRef f64_type(ir_parser_t *p) {
    return LLVMDoubleTypeInContext(p->context);
}

static LLVMValueRef const_f64(ir_parser_t *p, double v) {
    return LLVMConstReal(f64_type(p), v);
}

/* Load params[index], clamped to abs >= 1e-10 */
static LLVMValueRef load_param(ir_parser_t *p, int index) {
    LLVMValueRef idx = LLVMConstInt(LLVMInt32TypeInContext(p->context), index, 0);
    LLVMValueRef ptr = LLVMBuildGEP2(p->builder, f64_type(p), p->params_ptr, &idx, 1, "p_ptr");
    LLVMValueRef val = LLVMBuildLoad2(p->builder, f64_type(p), ptr, "p_val");

    /* abs(val) — call llvm.fabs */
    LLVMTypeRef fabs_param[] = {f64_type(p)};
    LLVMTypeRef fabs_type = LLVMFunctionType(f64_type(p), fabs_param, 1, 0);
    LLVMValueRef fabs_fn = LLVMGetNamedFunction(p->module, "llvm.fabs.f64");
    if (!fabs_fn) fabs_fn = LLVMAddFunction(p->module, "llvm.fabs.f64", fabs_type);
    LLVMValueRef abs_val = LLVMBuildCall2(p->builder, fabs_type, fabs_fn, &val, 1, "abs_p");

    /* max(abs_val, 1e-10) */
    LLVMValueRef eps = const_f64(p, 1e-10);
    LLVMValueRef gt = LLVMBuildFCmp(p->builder, LLVMRealOGT, abs_val, eps, "gt");
    return LLVMBuildSelect(p->builder, gt, abs_val, eps, "clamped_p");
}

/* Lookup variable by name → emit IR to load it */
static LLVMValueRef lookup_var_ir(ir_parser_t *p, const char *name) {
    if (strcmp(name, "signal") == 0) return p->signal_val;
    if (strcmp(name, "input") == 0) {
        /* abs(signal) clamped to >= 1e-10 */
        LLVMTypeRef fabs_param[] = {f64_type(p)};
        LLVMTypeRef fabs_type = LLVMFunctionType(f64_type(p), fabs_param, 1, 0);
        LLVMValueRef fabs_fn = LLVMGetNamedFunction(p->module, "llvm.fabs.f64");
        if (!fabs_fn) fabs_fn = LLVMAddFunction(p->module, "llvm.fabs.f64", fabs_type);
        LLVMValueRef abs_s = LLVMBuildCall2(p->builder, fabs_type, fabs_fn,
                                             &p->signal_val, 1, "abs_sig");
        LLVMValueRef eps = const_f64(p, 1e-10);
        LLVMValueRef gt = LLVMBuildFCmp(p->builder, LLVMRealOGT, abs_s, eps, "sig_gt");
        return LLVMBuildSelect(p->builder, gt, abs_s, eps, "input_val");
    }
    if (strcmp(name, "pi") == 0) return const_f64(p, 3.14159265358979323846);

    /* Parameter lookup */
    for (int i = 0; i < p->n_params; i++) {
        if (strcmp(p->param_keys[i], name) == 0)
            return load_param(p, i);
    }

    return const_f64(p, 1e-10); /* unknown → epsilon */
}

/* ═══ IR EMISSION (recursive descent → LLVM IR) ═══ */

static LLVMValueRef emit_expr(ir_parser_t *p);

static LLVMValueRef emit_primary(ir_parser_t *p) {
    if (p->cur.type == FTOK_NUM) {
        LLVMValueRef v = const_f64(p, p->cur.num_val);
        fnext(p);
        return v;
    }
    if (p->cur.type == FTOK_LPAREN) {
        fnext(p);
        LLVMValueRef v = emit_expr(p);
        if (p->cur.type == FTOK_RPAREN) fnext(p);
        return v;
    }
    if (p->cur.type == FTOK_IDENT) {
        char name[64];
        strncpy(name, p->cur.ident, 63); name[63] = 0;
        fnext(p);

        /* Function call? */
        if (p->cur.type == FTOK_LPAREN) {
            fnext(p);
            LLVMValueRef arg1 = emit_expr(p);

            if (p->cur.type == FTOK_COMMA) {
                fnext(p);
                LLVMValueRef arg2 = emit_expr(p);
                if (p->cur.type == FTOK_RPAREN) fnext(p);

                if (strcmp(name, "min") == 0) {
                    LLVMValueRef lt = LLVMBuildFCmp(p->builder, LLVMRealOLT, arg1, arg2, "lt");
                    return LLVMBuildSelect(p->builder, lt, arg1, arg2, "fmin");
                }
                if (strcmp(name, "max") == 0) {
                    LLVMValueRef gt = LLVMBuildFCmp(p->builder, LLVMRealOGT, arg1, arg2, "gt");
                    return LLVMBuildSelect(p->builder, gt, arg1, arg2, "fmax");
                }
                if (strcmp(name, "pow") == 0) {
                    LLVMTypeRef pow_params[] = {f64_type(p), f64_type(p)};
                    LLVMTypeRef pow_type = LLVMFunctionType(f64_type(p), pow_params, 2, 0);
                    LLVMValueRef pow_fn = LLVMGetNamedFunction(p->module, "llvm.pow.f64");
                    if (!pow_fn) pow_fn = LLVMAddFunction(p->module, "llvm.pow.f64", pow_type);
                    LLVMValueRef args[] = {arg1, arg2};
                    return LLVMBuildCall2(p->builder, pow_type, pow_fn, args, 2, "fpow");
                }
                return arg1;
            }

            if (p->cur.type == FTOK_RPAREN) fnext(p);

            /* Single-arg functions */
            if (strcmp(name, "abs") == 0) {
                LLVMTypeRef ft[] = {f64_type(p)};
                LLVMTypeRef fty = LLVMFunctionType(f64_type(p), ft, 1, 0);
                LLVMValueRef fn = LLVMGetNamedFunction(p->module, "llvm.fabs.f64");
                if (!fn) fn = LLVMAddFunction(p->module, "llvm.fabs.f64", fty);
                return LLVMBuildCall2(p->builder, fty, fn, &arg1, 1, "fabs");
            }
            if (strcmp(name, "exp") == 0) {
                /* Clamp to [-20, 20] before exp */
                LLVMValueRef lo = const_f64(p, -20.0);
                LLVMValueRef hi = const_f64(p, 20.0);
                LLVMValueRef gt_lo = LLVMBuildFCmp(p->builder, LLVMRealOGT, arg1, lo, "gt_lo");
                LLVMValueRef clamped = LLVMBuildSelect(p->builder, gt_lo, arg1, lo, "clamp_lo");
                LLVMValueRef lt_hi = LLVMBuildFCmp(p->builder, LLVMRealOLT, clamped, hi, "lt_hi");
                clamped = LLVMBuildSelect(p->builder, lt_hi, clamped, hi, "clamp_hi");

                LLVMTypeRef ft[] = {f64_type(p)};
                LLVMTypeRef fty = LLVMFunctionType(f64_type(p), ft, 1, 0);
                LLVMValueRef fn = LLVMGetNamedFunction(p->module, "llvm.exp.f64");
                if (!fn) fn = LLVMAddFunction(p->module, "llvm.exp.f64", fty);
                return LLVMBuildCall2(p->builder, fty, fn, &clamped, 1, "fexp");
            }
            if (strcmp(name, "sqrt") == 0) {
                /* max(arg, 0) before sqrt */
                LLVMValueRef zero = const_f64(p, 0.0);
                LLVMValueRef gt = LLVMBuildFCmp(p->builder, LLVMRealOGT, arg1, zero, "gt0");
                LLVMValueRef safe = LLVMBuildSelect(p->builder, gt, arg1, zero, "safe_sqrt");

                LLVMTypeRef ft[] = {f64_type(p)};
                LLVMTypeRef fty = LLVMFunctionType(f64_type(p), ft, 1, 0);
                LLVMValueRef fn = LLVMGetNamedFunction(p->module, "llvm.sqrt.f64");
                if (!fn) fn = LLVMAddFunction(p->module, "llvm.sqrt.f64", fty);
                return LLVMBuildCall2(p->builder, fty, fn, &safe, 1, "fsqrt");
            }
            if (strcmp(name, "log") == 0) {
                /* max(arg, 1e-10) before log */
                LLVMValueRef eps = const_f64(p, 1e-10);
                LLVMValueRef gt = LLVMBuildFCmp(p->builder, LLVMRealOGT, arg1, eps, "gt_eps");
                LLVMValueRef safe = LLVMBuildSelect(p->builder, gt, arg1, eps, "safe_log");

                LLVMTypeRef ft[] = {f64_type(p)};
                LLVMTypeRef fty = LLVMFunctionType(f64_type(p), ft, 1, 0);
                LLVMValueRef fn = LLVMGetNamedFunction(p->module, "llvm.log.f64");
                if (!fn) fn = LLVMAddFunction(p->module, "llvm.log.f64", fty);
                return LLVMBuildCall2(p->builder, fty, fn, &safe, 1, "flog");
            }
            return arg1;
        }

        /* Variable */
        return lookup_var_ir(p, name);
    }

    fnext(p);
    return const_f64(p, 0);
}

static LLVMValueRef emit_unary(ir_parser_t *p) {
    if (p->cur.type == FTOK_MINUS) {
        fnext(p);
        return LLVMBuildFNeg(p->builder, emit_unary(p), "neg");
    }
    return emit_primary(p);
}

static LLVMValueRef emit_power(ir_parser_t *p) {
    LLVMValueRef left = emit_unary(p);
    while (p->cur.type == FTOK_STARSTAR) {
        fnext(p);
        LLVMValueRef right = emit_unary(p);
        LLVMTypeRef pow_params[] = {f64_type(p), f64_type(p)};
        LLVMTypeRef pow_type = LLVMFunctionType(f64_type(p), pow_params, 2, 0);
        LLVMValueRef pow_fn = LLVMGetNamedFunction(p->module, "llvm.pow.f64");
        if (!pow_fn) pow_fn = LLVMAddFunction(p->module, "llvm.pow.f64", pow_type);
        LLVMValueRef args[] = {left, right};
        left = LLVMBuildCall2(p->builder, pow_type, pow_fn, args, 2, "fpow");
    }
    return left;
}

static LLVMValueRef emit_term(ir_parser_t *p) {
    LLVMValueRef left = emit_power(p);
    while (p->cur.type == FTOK_STAR || p->cur.type == FTOK_SLASH) {
        ftok_type_t op = p->cur.type;
        fnext(p);
        LLVMValueRef right = emit_power(p);
        if (op == FTOK_STAR)
            left = LLVMBuildFMul(p->builder, left, right, "fmul");
        else {
            /* Safe division: max(|right|, 1e-10) */
            LLVMTypeRef ft[] = {f64_type(p)};
            LLVMTypeRef fty = LLVMFunctionType(f64_type(p), ft, 1, 0);
            LLVMValueRef fabs_fn = LLVMGetNamedFunction(p->module, "llvm.fabs.f64");
            if (!fabs_fn) fabs_fn = LLVMAddFunction(p->module, "llvm.fabs.f64", fty);
            LLVMValueRef abs_r = LLVMBuildCall2(p->builder, fty, fabs_fn, &right, 1, "abs_div");
            LLVMValueRef eps = const_f64(p, 1e-10);
            LLVMValueRef gt = LLVMBuildFCmp(p->builder, LLVMRealOGT, abs_r, eps, "div_safe");
            LLVMValueRef safe_denom = LLVMBuildSelect(p->builder, gt, right, eps, "safe_d");
            left = LLVMBuildFDiv(p->builder, left, safe_denom, "fdiv");
        }
    }
    return left;
}

static LLVMValueRef emit_expr(ir_parser_t *p) {
    LLVMValueRef left = emit_term(p);
    while (p->cur.type == FTOK_PLUS || p->cur.type == FTOK_MINUS) {
        ftok_type_t op = p->cur.type;
        fnext(p);
        LLVMValueRef right = emit_term(p);
        if (op == FTOK_PLUS) left = LLVMBuildFAdd(p->builder, left, right, "fadd");
        else left = LLVMBuildFSub(p->builder, left, right, "fsub");
    }
    return left;
}

/* ═══ BUILD ONE FORMULA FUNCTION ═══ */
/*
 * Generates: double atom_<name>(double *params, int param_count, double signal)
 */
static LLVMValueRef build_formula_fn(LLVMModuleRef mod, LLVMContextRef ctx,
                                      const char *name, const char *compute,
                                      const char (*param_keys)[32], int n_params) {
    LLVMTypeRef f64 = LLVMDoubleTypeInContext(ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
    LLVMTypeRef ptr = LLVMPointerType(f64, 0);
    LLVMTypeRef param_types[] = {ptr, i32, f64};
    LLVMTypeRef fn_type = LLVMFunctionType(f64, param_types, 3, 0);

    char fn_name[128];
    snprintf(fn_name, sizeof(fn_name), "csos_atom_%s", name);
    LLVMValueRef fn = LLVMAddFunction(mod, fn_name, fn_type);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, fn, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);
    LLVMPositionBuilderAtEnd(builder, entry);

    /* Set up parser for IR emission */
    ir_parser_t p = {0};
    p.src = compute;
    p.pos = 0;
    p.builder = builder;
    p.module = mod;
    p.context = ctx;
    p.params_ptr = LLVMGetParam(fn, 0);
    p.param_count = LLVMGetParam(fn, 1);
    p.signal_val = LLVMGetParam(fn, 2);
    p.param_keys = param_keys;
    p.n_params = n_params;

    /* Parse and emit IR for the compute expression */
    fnext(&p);
    LLVMValueRef result = emit_expr(&p);
    LLVMBuildRet(builder, result);

    LLVMDisposeBuilder(builder);
    return fn;
}

/* ═══ COMPILE ALL FORMULAS FOR A MEMBRANE ═══ */

int csos_formula_jit_compile(csos_membrane_t *m) {
    if (!FJIT.initialized) {
        LLVMInitializeNativeTarget();
        LLVMInitializeNativeAsmPrinter();
        LLVMInitializeNativeAsmParser();
        FJIT.context = LLVMContextCreate();
        FJIT.initialized = 1;
    }

    /* Dispose old engine if recompiling */
    if (FJIT.engine) {
        LLVMDisposeExecutionEngine(FJIT.engine);
        FJIT.engine = NULL;
    }
    memset(FJIT.fns, 0, sizeof(FJIT.fns));

    /* Create fresh module */
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("csos_formulas", FJIT.context);

    /* Build a function for each atom's compute expression */
    for (int i = 0; i < m->atom_count; i++) {
        csos_atom_t *a = &m->atoms[i];
        if (!a->compute[0]) continue; /* no compute expr → skip */

        build_formula_fn(mod, FJIT.context, a->name, a->compute,
                         (const char (*)[32])a->param_keys, a->param_count);
    }

    /* Verify module */
    char *err = NULL;
    if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &err)) {
        fprintf(stderr, "[formula-jit] Module verification failed: %s\n", err);
        LLVMDisposeMessage(err);
        LLVMDisposeModule(mod);
        return -1;
    }
    if (err) LLVMDisposeMessage(err);

    /* Optimize */
    LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
    LLVMRunPasses(mod, "default<O2>", NULL, opts);
    LLVMDisposePassBuilderOptions(opts);

    /* Create execution engine */
    err = NULL;
    if (LLVMCreateJITCompilerForModule(&FJIT.engine, mod, 2, &err)) {
        fprintf(stderr, "[formula-jit] JIT creation failed: %s\n", err);
        LLVMDisposeMessage(err);
        LLVMDisposeModule(mod);
        return -1;
    }

    FJIT.module = mod;
    FJIT.atom_count_compiled = m->atom_count;

    /* Resolve function pointers */
    for (int i = 0; i < m->atom_count; i++) {
        if (!m->atoms[i].compute[0]) continue;
        char fn_name[128];
        snprintf(fn_name, sizeof(fn_name), "csos_atom_%s", m->atoms[i].name);
        uint64_t addr = LLVMGetFunctionAddress(FJIT.engine, fn_name);
        if (addr) FJIT.fns[i] = (formula_fn_t)addr;
    }

    fprintf(stderr, "[formula-jit] Compiled %d atom formulas to native code\n", m->atom_count);
    return 0;
}

/* ═══ EVALUATE USING JIT ═══ */

double csos_formula_jit_eval(int atom_index, const double *params,
                             int param_count, double signal) {
    if (atom_index >= 0 && atom_index < CSOS_MAX_ATOMS && FJIT.fns[atom_index])
        return FJIT.fns[atom_index](params, param_count, signal);

    /* Fallback: should not reach here if compile succeeded */
    return signal;
}

/* ═══ CHECK IF RECOMPILE NEEDED ═══ */

int csos_formula_jit_check(csos_membrane_t *m) {
    if (!FJIT.initialized) return 0;
    if (m->atom_count != FJIT.atom_count_compiled) {
        fprintf(stderr, "[formula-jit] Calvin synthesis: %d → %d atoms, recompiling formulas...\n",
                FJIT.atom_count_compiled, m->atom_count);
        return csos_formula_jit_compile(m);
    }
    return 0;
}

#endif /* CSOS_HAS_LLVM */
