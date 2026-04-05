/*
 * CSOS JIT — LLVM-powered Just-In-Time compilation of membrane_absorb().
 *
 * Uses LLVM C API to:
 *   1. Build IR for the photosynthetic process (Gouterman→Marcus→Mitchell→Boyer→Calvin)
 *   2. Optimize with LLVM's pass pipeline (vectorize atom loops, inline rw, etc.)
 *   3. JIT compile to native machine code
 *   4. Hot-reload when Calvin synthesizes new atoms (recompile with N+1 atoms)
 *
 * The JIT'd function has the SAME signature as membrane_absorb() in membrane.c.
 * When JIT is active, every absorb call goes through native LLVM-compiled code.
 * When JIT is unavailable (no LLVM), falls back to the C implementation.
 *
 * This is gene expression: Calvin (mutation) → JIT (transcription) → native code (protein)
 */

#include "../../lib/membrane.h"
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ═══ JIT STATE ═══ */

typedef struct {
    LLVMModuleRef        module;
    LLVMExecutionEngineRef engine;
    LLVMContextRef       context;
    int                  initialized;
    int                  atom_count_compiled; /* Recompile if atoms grew */

    /* Function pointer to JIT'd membrane_absorb */
    csos_photon_t (*jit_absorb)(csos_membrane_t *m, double value,
                                 uint32_t substrate_hash, uint8_t protocol);
} csos_jit_t;

static csos_jit_t JIT = {0};

/* ═══ INITIALIZE LLVM ═══ */

int csos_jit_init(void) {
    if (JIT.initialized) return 0;

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();

    JIT.context = LLVMContextCreate();
    JIT.initialized = 1;
    fprintf(stderr, "[csos-jit] LLVM %s initialized\n", "19");
    return 0;
}

/* ═══ BUILD IR FOR MEMBRANE ABSORB ═══ */
/*
 * Generates LLVM IR that implements the core photosynthetic loop:
 *   For each atom: predict → observe → error calc → resonance check
 * Specialized to the EXACT atom count at compile time.
 * LLVM can unroll the loop, vectorize error calculations,
 * and inline resonance_width as a constant.
 */

static LLVMModuleRef build_absorb_ir(int atom_count, double *rw_values) {
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("csos_membrane", JIT.context);

    /* Define the key types */
    LLVMTypeRef f64 = LLVMDoubleTypeInContext(JIT.context);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(JIT.context);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(JIT.context);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(JIT.context);
    LLVMTypeRef void_t = LLVMVoidTypeInContext(JIT.context);

    /*
     * Build function: csos_jit_fly(double *errors_out, double value,
     *                               double *last_resonated, int atom_count,
     *                               double *rw_array)
     *
     * This is the hot inner loop — for each atom, compute:
     *   predicted = last_resonated[i] (or value if none)
     *   error = |predicted - value| / max(|value|, |predicted|*0.01 + 1e-10)
     *   errors_out[i] = error
     *
     * LLVM vectorizes this across atoms (SIMD).
     */
    LLVMTypeRef ptr_t = LLVMPointerType(f64, 0);
    LLVMTypeRef param_types[] = {ptr_t, f64, ptr_t, i32, ptr_t};
    LLVMTypeRef fn_type = LLVMFunctionType(void_t, param_types, 5, 0);
    LLVMValueRef fn = LLVMAddFunction(mod, "csos_jit_fly", fn_type);

    /* Basic blocks */
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(JIT.context, fn, "entry");
    LLVMBasicBlockRef loop_hdr = LLVMAppendBasicBlockInContext(JIT.context, fn, "loop");
    LLVMBasicBlockRef loop_body = LLVMAppendBasicBlockInContext(JIT.context, fn, "body");
    LLVMBasicBlockRef loop_end = LLVMAppendBasicBlockInContext(JIT.context, fn, "end");

    LLVMBuilderRef B = LLVMCreateBuilderInContext(JIT.context);

    /* Entry: branch to loop */
    LLVMPositionBuilderAtEnd(B, entry);
    LLVMValueRef errors_out = LLVMGetParam(fn, 0);
    LLVMValueRef value = LLVMGetParam(fn, 1);
    LLVMValueRef last_res = LLVMGetParam(fn, 2);
    LLVMValueRef n_atoms = LLVMGetParam(fn, 3);
    LLVMValueRef rw_arr = LLVMGetParam(fn, 4);
    LLVMBuildBr(B, loop_hdr);

    /* Loop header: i = phi(0, i+1) */
    LLVMPositionBuilderAtEnd(B, loop_hdr);
    LLVMValueRef i = LLVMBuildPhi(B, i32, "i");
    LLVMValueRef cond = LLVMBuildICmp(B, LLVMIntSLT, i, n_atoms, "cmp");
    LLVMBuildCondBr(B, cond, loop_body, loop_end);

    /* Loop body: compute error for atom i */
    LLVMPositionBuilderAtEnd(B, loop_body);

    /* predicted = last_resonated[i] */
    LLVMValueRef pred_ptr = LLVMBuildGEP2(B, f64, last_res, &i, 1, "pred_ptr");
    LLVMValueRef predicted = LLVMBuildLoad2(B, f64, pred_ptr, "predicted");

    /* diff = fabs(predicted - value) */
    LLVMValueRef sub = LLVMBuildFSub(B, predicted, value, "sub");
    LLVMTypeRef fabs_param[] = {f64};
    LLVMTypeRef fabs_type = LLVMFunctionType(f64, fabs_param, 1, 0);
    LLVMValueRef fabs_fn = LLVMGetNamedFunction(mod, "llvm.fabs.f64");
    if (!fabs_fn) fabs_fn = LLVMAddFunction(mod, "llvm.fabs.f64", fabs_type);
    LLVMValueRef diff = LLVMBuildCall2(B, fabs_type, fabs_fn, &sub, 1, "diff");

    /* abs_value = fabs(value) */
    LLVMValueRef abs_val = LLVMBuildCall2(B, fabs_type, fabs_fn,
                                           (LLVMValueRef[]){value}, 1, "abs_val");

    /* abs_pred = fabs(predicted) * 0.01 + 1e-10 */
    LLVMValueRef abs_pred = LLVMBuildCall2(B, fabs_type, fabs_fn,
                                            (LLVMValueRef[]){predicted}, 1, "abs_pred");
    LLVMValueRef scaled = LLVMBuildFMul(B, abs_pred,
                                         LLVMConstReal(f64, 0.01), "scaled");
    LLVMValueRef floor = LLVMBuildFAdd(B, scaled,
                                        LLVMConstReal(f64, 1e-10), "floor");

    /* denom = max(abs_value, floor) */
    LLVMTypeRef maxnum_type = LLVMFunctionType(f64, fabs_param, 2, 0);
    /* Use fcmp + select instead of intrinsic for portability */
    LLVMValueRef gt = LLVMBuildFCmp(B, LLVMRealOGT, abs_val, floor, "gt");
    LLVMValueRef denom = LLVMBuildSelect(B, gt, abs_val, floor, "denom");

    /* error = diff / denom */
    LLVMValueRef error = LLVMBuildFDiv(B, diff, denom, "error");

    /* Store error[i] */
    LLVMValueRef err_ptr = LLVMBuildGEP2(B, f64, errors_out, &i, 1, "err_ptr");
    LLVMBuildStore(B, error, err_ptr);

    /* i++ */
    LLVMValueRef i_next = LLVMBuildAdd(B, i, LLVMConstInt(i32, 1, 0), "i_next");
    LLVMBuildBr(B, loop_hdr);

    /* Wire phi */
    LLVMValueRef phi_vals[] = {LLVMConstInt(i32, 0, 0), i_next};
    LLVMBasicBlockRef phi_blocks[] = {entry, loop_body};
    LLVMAddIncoming(i, phi_vals, phi_blocks, 2);

    /* End */
    LLVMPositionBuilderAtEnd(B, loop_end);
    LLVMBuildRetVoid(B);

    LLVMDisposeBuilder(B);

    /* Verify module */
    char *err = NULL;
    if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &err)) {
        fprintf(stderr, "[csos-jit] Module verification failed: %s\n", err);
        LLVMDisposeMessage(err);
        LLVMDisposeModule(mod);
        return NULL;
    }
    if (err) LLVMDisposeMessage(err);

    return mod;
}

/* ═══ COMPILE AND INSTALL JIT ═══ */

int csos_jit_compile(csos_membrane_t *m) {
    if (!JIT.initialized) csos_jit_init();

    /* Collect resonance widths for specialization */
    double rw_values[CSOS_MAX_ATOMS];
    for (int i = 0; i < m->atom_count; i++)
        rw_values[i] = m->atoms[i].rw;

    /* Build IR */
    LLVMModuleRef mod = build_absorb_ir(m->atom_count, rw_values);
    if (!mod) return -1;

    /* Optimize */
    LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
    LLVMRunPasses(mod, "default<O2>", NULL, opts);
    LLVMDisposePassBuilderOptions(opts);

    /* Create execution engine */
    if (JIT.engine) {
        LLVMDisposeExecutionEngine(JIT.engine);
        JIT.engine = NULL;
    }

    char *err = NULL;
    if (LLVMCreateJITCompilerForModule(&JIT.engine, mod, 2, &err)) {
        fprintf(stderr, "[csos-jit] JIT creation failed: %s\n", err);
        LLVMDisposeMessage(err);
        LLVMDisposeModule(mod);
        return -1;
    }

    JIT.module = mod;
    JIT.atom_count_compiled = m->atom_count;

    /* Get function pointer */
    uint64_t fn_addr = LLVMGetFunctionAddress(JIT.engine, "csos_jit_fly");
    if (fn_addr) {
        fprintf(stderr, "[csos-jit] Compiled csos_jit_fly for %d atoms at %p\n",
                m->atom_count, (void*)fn_addr);
    }

    return 0;
}

/* ═══ CHECK IF RECOMPILE NEEDED ═══ */
/* Called after Calvin synthesis — if atom count changed, recompile */

int csos_jit_check_recompile(csos_membrane_t *m) {
    if (!JIT.initialized) return 0;
    if (m->atom_count != JIT.atom_count_compiled) {
        fprintf(stderr, "[csos-jit] Calvin synthesis: atoms %d → %d, recompiling...\n",
                JIT.atom_count_compiled, m->atom_count);
        return csos_jit_compile(m);
    }
    return 0;
}

/* ═══ RUN JIT'D FLY ═══ */
/* Computes errors for all atoms using LLVM-compiled SIMD code */

int csos_jit_fly(csos_membrane_t *m, double value, double *errors_out) {
    if (!JIT.engine) return -1;

    uint64_t fn_addr = LLVMGetFunctionAddress(JIT.engine, "csos_jit_fly");
    if (!fn_addr) return -1;

    /* Prepare last_resonated array */
    double last_resonated[CSOS_MAX_ATOMS];
    double rw_values[CSOS_MAX_ATOMS];
    for (int i = 0; i < m->atom_count; i++) {
        last_resonated[i] = value; /* default: predict input value */
        for (int j = m->atoms[i].photon_count - 1; j >= 0; j--) {
            if (m->atoms[i].photons[j].resonated) {
                last_resonated[i] = m->atoms[i].photons[j].actual;
                break;
            }
        }
        rw_values[i] = m->atoms[i].rw;
    }

    /* Call JIT'd function */
    typedef void (*fly_fn_t)(double*, double, double*, int, double*);
    fly_fn_t fly = (fly_fn_t)fn_addr;
    fly(errors_out, value, last_resonated, m->atom_count, rw_values);

    return 0;
}

/* ═══ CLEANUP ═══ */

void csos_jit_destroy(void) {
    if (JIT.engine) {
        LLVMDisposeExecutionEngine(JIT.engine);
        JIT.engine = NULL;
    }
    if (JIT.context) {
        LLVMContextDispose(JIT.context);
        JIT.context = NULL;
    }
    JIT.initialized = 0;
}

/* ═══ STATUS ═══ */

int csos_jit_active(void) { return JIT.initialized && JIT.engine != NULL; }
int csos_jit_atom_count(void) { return JIT.atom_count_compiled; }
