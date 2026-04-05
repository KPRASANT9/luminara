/*
 * CSOS Codegen — Spec-driven LLVM IR generation.
 *
 * Reads .csos spec → parses atom definitions → generates LLVM IR
 * for each atom's compute expression. NO hardcoded equation functions.
 *
 * Before (Law I violation):
 *   create_gouterman() { ... hardcoded h*c/lambda IR ... }
 *   create_forster()   { ... hardcoded (1/t)*(R0/r)^6 IR ... }
 *   create_marcus()    { ... hardcoded exp(...) IR ... }
 *
 * After (Law I compliant):
 *   for each atom in spec:
 *     parse atom.compute → AST → emit LLVM IR generically
 *
 * The spec IS the code. Adding a new equation means adding an atom{}
 * block to eco.csos — zero C++ changes.
 */

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/raw_ostream.h>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <iostream>
#include <cstring>
#include <cctype>

namespace csos {

/* ═══ AST (from spec parser) ═══ */

struct Atom {
    std::string name;
    std::string formula;
    std::string compute;   /* The evaluable expression — this gets compiled */
    std::string source;
    std::map<std::string, double> params;
    double spectral[2] = {0, 10000};
    int broadband = 0;
    std::string role;
};

struct Ring {
    std::string name;
    std::vector<std::string> atoms;
    std::string purpose;
};

/* ═══ SPEC PARSER ═══ */

class SpecParser {
public:
    std::vector<Atom> atoms;
    std::vector<Ring> rings;

    bool parse(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        std::string line;
        bool in_atom = false, in_ring = false;
        Atom* cur_atom = nullptr;

        while (std::getline(file, line)) {
            trim(line);
            if (line.empty() || line.substr(0, 2) == "//") continue;

            if (line.substr(0, 5) == "atom " && line.find('{') != std::string::npos) {
                atoms.emplace_back();
                cur_atom = &atoms.back();
                cur_atom->name = extract_name(line, "atom ");
                in_atom = true;
                continue;
            }

            if (line.substr(0, 5) == "ring " && line.find('{') != std::string::npos) {
                Ring r;
                r.name = extract_name(line, "ring ");
                rings.push_back(r);
                in_ring = true;
                continue;
            }

            if (in_atom && cur_atom) {
                if (line.find('}') != std::string::npos) {
                    in_atom = false;
                    cur_atom = nullptr;
                    continue;
                }
                extract_atom_field(line, *cur_atom);
            }

            if (in_ring && line.find('}') != std::string::npos) {
                in_ring = false;
            }
        }
        return true;
    }

private:
    void extract_atom_field(const std::string& line, Atom& a) {
        if (has_key(line, "formula:"))
            a.formula = extract_value(line, "formula:");
        else if (has_key(line, "compute:"))
            a.compute = extract_value(line, "compute:");
        else if (has_key(line, "source:"))
            a.source = extract_quoted(line, "source:");
        else if (has_key(line, "role:"))
            a.role = extract_quoted(line, "role:");
        else if (has_key(line, "params:"))
            parse_params(line, a.params);
        else if (has_key(line, "spectral:"))
            parse_spectral(line, a.spectral);
        else if (has_key(line, "broadband:"))
            a.broadband = (line.find("true") != std::string::npos) ? 1 : 0;
    }

    bool has_key(const std::string& line, const std::string& key) {
        return line.find(key) != std::string::npos;
    }

    std::string extract_value(const std::string& line, const std::string& key) {
        auto pos = line.find(key);
        if (pos == std::string::npos) return "";
        auto start = pos + key.size();
        auto val = line.substr(start);
        trim(val);
        /* Remove trailing semicolons */
        while (!val.empty() && (val.back() == ';' || val.back() == ' '))
            val.pop_back();
        return val;
    }

    std::string extract_quoted(const std::string& line, const std::string& key) {
        auto pos = line.find(key);
        if (pos == std::string::npos) return "";
        auto q1 = line.find('"', pos);
        if (q1 == std::string::npos) return extract_value(line, key);
        auto q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) return "";
        return line.substr(q1 + 1, q2 - q1 - 1);
    }

    void parse_params(const std::string& line, std::map<std::string, double>& params) {
        auto b = line.find('{');
        auto e = line.find('}');
        if (b == std::string::npos || e == std::string::npos) return;
        auto body = line.substr(b + 1, e - b - 1);
        std::istringstream ss(body);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            auto colon = tok.find(':');
            if (colon == std::string::npos) continue;
            auto k = tok.substr(0, colon);
            auto v = tok.substr(colon + 1);
            trim(k); trim(v);
            params[k] = std::stod(v);
        }
    }

    void parse_spectral(const std::string& line, double (&spec)[2]) {
        auto b = line.find('[');
        auto e = line.find(']');
        if (b == std::string::npos || e == std::string::npos) return;
        auto body = line.substr(b + 1, e - b - 1);
        auto comma = body.find(',');
        if (comma == std::string::npos) return;
        spec[0] = std::stod(body.substr(0, comma));
        spec[1] = std::stod(body.substr(comma + 1));
    }

    std::string extract_name(const std::string& line, const std::string& prefix) {
        auto start = line.find(prefix) + prefix.size();
        auto end = line.find('{');
        auto name = line.substr(start, end - start);
        trim(name);
        return name;
    }

    void trim(std::string& s) {
        s.erase(0, s.find_first_not_of(" \t"));
        if (!s.empty()) s.erase(s.find_last_not_of(" \t") + 1);
    }
};

/* ═══ EXPRESSION PARSER → LLVM IR ═══ */
/*
 * Recursive descent parser that emits LLVM IR for any compute expression.
 * Same grammar as formula_eval.c / formula_jit.c:
 *   expr → term (('+' | '-') term)*
 *   term → power (('*' | '/') power)*
 *   power → unary ('**' unary)*
 *   unary → ('-' unary) | call
 *   call → IDENT '(' args ')' | primary
 *   primary → NUMBER | IDENT | '(' expr ')'
 */

class IREmitter {
    llvm::IRBuilder<>& B;
    llvm::Module* mod;
    llvm::Value* params_ptr;
    llvm::Value* signal_val;
    const std::map<std::string, double>& param_map;
    std::vector<std::string> param_order;
    const std::string& src;
    size_t pos;

    enum Tok { NUM, IDENT, PLUS, MINUS, STAR, SLASH, STARSTAR, LPAREN, RPAREN, COMMA, END };
    Tok cur_tok;
    double num_val;
    std::string ident_val;

    void advance() {
        while (pos < src.size() && isspace(src[pos])) pos++;
        if (pos >= src.size()) { cur_tok = END; return; }
        char c = src[pos];

        if (isdigit(c) || (c == '.' && pos + 1 < src.size() && isdigit(src[pos+1]))) {
            num_val = std::stod(src.substr(pos), nullptr);
            while (pos < src.size() && (isdigit(src[pos]) || src[pos] == '.' || src[pos] == 'e' || src[pos] == 'E')) pos++;
            cur_tok = NUM; return;
        }
        if (isalpha(c) || c == '_') {
            ident_val.clear();
            while (pos < src.size() && (isalnum(src[pos]) || src[pos] == '_')) ident_val += src[pos++];
            cur_tok = IDENT; return;
        }
        if (c == '*' && pos + 1 < src.size() && src[pos+1] == '*') { pos += 2; cur_tok = STARSTAR; return; }
        switch (c) {
            case '+': pos++; cur_tok = PLUS; return;
            case '-': pos++; cur_tok = MINUS; return;
            case '*': pos++; cur_tok = STAR; return;
            case '/': pos++; cur_tok = SLASH; return;
            case '(': pos++; cur_tok = LPAREN; return;
            case ')': pos++; cur_tok = RPAREN; return;
            case ',': pos++; cur_tok = COMMA; return;
        }
        pos++; advance();
    }

    llvm::Value* lookup(const std::string& name) {
        if (name == "signal") return signal_val;
        if (name == "input") {
            auto* fabs_fn = llvm::Intrinsic::getDeclaration(mod, llvm::Intrinsic::fabs, {B.getDoubleTy()});
            auto* abs_s = B.CreateCall(fabs_fn, {signal_val}, "abs_sig");
            return B.CreateMaximum(abs_s, llvm::ConstantFP::get(B.getDoubleTy(), 1e-10), "input");
        }
        if (name == "pi") return llvm::ConstantFP::get(B.getDoubleTy(), 3.14159265358979323846);

        /* Find param index and load from array */
        for (size_t i = 0; i < param_order.size(); i++) {
            if (param_order[i] == name) {
                auto* idx = llvm::ConstantInt::get(B.getInt32Ty(), i);
                auto* ptr = B.CreateGEP(B.getDoubleTy(), params_ptr, idx, "p_ptr");
                auto* val = B.CreateLoad(B.getDoubleTy(), ptr, "p_val");
                auto* fabs_fn = llvm::Intrinsic::getDeclaration(mod, llvm::Intrinsic::fabs, {B.getDoubleTy()});
                auto* abs_v = B.CreateCall(fabs_fn, {val}, "abs_p");
                return B.CreateMaximum(abs_v, llvm::ConstantFP::get(B.getDoubleTy(), 1e-10), "clamped_p");
            }
        }
        return llvm::ConstantFP::get(B.getDoubleTy(), 1e-10);
    }

    llvm::Value* emit_expr();

    llvm::Value* emit_primary() {
        if (cur_tok == NUM) {
            auto* v = llvm::ConstantFP::get(B.getDoubleTy(), num_val);
            advance();
            return v;
        }
        if (cur_tok == LPAREN) {
            advance();
            auto* v = emit_expr();
            if (cur_tok == RPAREN) advance();
            return v;
        }
        if (cur_tok == IDENT) {
            auto name = ident_val;
            advance();
            if (cur_tok == LPAREN) {
                advance();
                auto* arg1 = emit_expr();
                if (cur_tok == COMMA) {
                    advance();
                    auto* arg2 = emit_expr();
                    if (cur_tok == RPAREN) advance();
                    if (name == "min") return B.CreateMinimum(arg1, arg2, "fmin");
                    if (name == "max") return B.CreateMaximum(arg1, arg2, "fmax");
                    if (name == "pow") {
                        auto* pow_fn = llvm::Intrinsic::getDeclaration(mod, llvm::Intrinsic::pow, {B.getDoubleTy()});
                        return B.CreateCall(pow_fn, {arg1, arg2}, "fpow");
                    }
                    return arg1;
                }
                if (cur_tok == RPAREN) advance();
                if (name == "abs") {
                    auto* fn = llvm::Intrinsic::getDeclaration(mod, llvm::Intrinsic::fabs, {B.getDoubleTy()});
                    return B.CreateCall(fn, {arg1}, "fabs");
                }
                if (name == "exp") {
                    auto* lo = llvm::ConstantFP::get(B.getDoubleTy(), -20.0);
                    auto* hi = llvm::ConstantFP::get(B.getDoubleTy(), 20.0);
                    auto* clamped = B.CreateMaximum(B.CreateMinimum(arg1, hi), lo, "clamp_exp");
                    auto* fn = llvm::Intrinsic::getDeclaration(mod, llvm::Intrinsic::exp, {B.getDoubleTy()});
                    return B.CreateCall(fn, {clamped}, "fexp");
                }
                if (name == "sqrt") {
                    auto* safe = B.CreateMaximum(arg1, llvm::ConstantFP::get(B.getDoubleTy(), 0.0), "safe_sqrt");
                    auto* fn = llvm::Intrinsic::getDeclaration(mod, llvm::Intrinsic::sqrt, {B.getDoubleTy()});
                    return B.CreateCall(fn, {safe}, "fsqrt");
                }
                if (name == "log") {
                    auto* safe = B.CreateMaximum(arg1, llvm::ConstantFP::get(B.getDoubleTy(), 1e-10), "safe_log");
                    auto* fn = llvm::Intrinsic::getDeclaration(mod, llvm::Intrinsic::log, {B.getDoubleTy()});
                    return B.CreateCall(fn, {safe}, "flog");
                }
                return arg1;
            }
            return lookup(name);
        }
        advance();
        return llvm::ConstantFP::get(B.getDoubleTy(), 0.0);
    }

    llvm::Value* emit_unary() {
        if (cur_tok == MINUS) { advance(); return B.CreateFNeg(emit_unary(), "neg"); }
        return emit_primary();
    }

    llvm::Value* emit_power() {
        auto* left = emit_unary();
        while (cur_tok == STARSTAR) {
            advance();
            auto* right = emit_unary();
            auto* pow_fn = llvm::Intrinsic::getDeclaration(mod, llvm::Intrinsic::pow, {B.getDoubleTy()});
            left = B.CreateCall(pow_fn, {left, right}, "fpow");
        }
        return left;
    }

    llvm::Value* emit_term() {
        auto* left = emit_power();
        while (cur_tok == STAR || cur_tok == SLASH) {
            auto op = cur_tok; advance();
            auto* right = emit_power();
            if (op == STAR) left = B.CreateFMul(left, right, "fmul");
            else {
                auto* fabs_fn = llvm::Intrinsic::getDeclaration(mod, llvm::Intrinsic::fabs, {B.getDoubleTy()});
                auto* abs_r = B.CreateCall(fabs_fn, {right}, "abs_div");
                auto* safe = B.CreateMaximum(abs_r, llvm::ConstantFP::get(B.getDoubleTy(), 1e-10), "safe_d");
                /* Preserve sign of original divisor */
                auto* sign_bit = B.CreateFCmpOLT(right, llvm::ConstantFP::get(B.getDoubleTy(), 0.0), "neg_d");
                auto* signed_safe = B.CreateSelect(sign_bit, B.CreateFNeg(safe), safe, "signed_d");
                left = B.CreateFDiv(left, signed_safe, "fdiv");
            }
        }
        return left;
    }

public:
    IREmitter(llvm::IRBuilder<>& builder, llvm::Module* module,
              llvm::Value* params, llvm::Value* signal,
              const std::map<std::string, double>& pm, const std::string& source)
        : B(builder), mod(module), params_ptr(params), signal_val(signal),
          param_map(pm), src(source), pos(0), cur_tok(END) {
        /* Build ordered param list */
        for (auto& [k, v] : param_map) param_order.push_back(k);
        advance();
    }

    llvm::Value* emit() { return emit_expr(); }
};

llvm::Value* IREmitter::emit_expr() {
    auto* left = emit_term();
    while (cur_tok == PLUS || cur_tok == MINUS) {
        auto op = cur_tok; advance();
        auto* right = emit_term();
        if (op == PLUS) left = B.CreateFAdd(left, right, "fadd");
        else left = B.CreateFSub(left, right, "fsub");
    }
    return left;
}

/* ═══ SPEC-DRIVEN CODEGEN ═══ */

class Codegen {
    llvm::LLVMContext context;
    llvm::IRBuilder<> builder;
    std::unique_ptr<llvm::Module> module;

public:
    Codegen() : builder(context) {
        module = std::make_unique<llvm::Module>("csos_llvm", context);
    }

    /*
     * Generate one function per atom, driven entirely by its compute expression.
     * Signature: double csos_atom_<name>(double* params, int param_count, double signal)
     */
    llvm::Function* create_atom_fn(const Atom& atom) {
        auto* f64 = builder.getDoubleTy();
        auto* i32 = builder.getInt32Ty();
        auto* ptr = llvm::PointerType::get(f64, 0);
        llvm::Type* param_types[] = {ptr, i32, f64};
        auto* fn_type = llvm::FunctionType::get(f64, param_types, false);

        std::string fn_name = "csos_atom_" + atom.name;
        auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                           fn_name, module.get());

        auto* entry = llvm::BasicBlock::Create(context, "entry", fn);
        builder.SetInsertPoint(entry);

        auto* params_ptr = fn->getArg(0);
        auto* signal_val = fn->getArg(2);

        /* Parse compute expression → emit LLVM IR */
        IREmitter emitter(builder, module.get(), params_ptr, signal_val,
                          atom.params, atom.compute);
        auto* result = emitter.emit();
        builder.CreateRet(result);
        return fn;
    }

    /*
     * Generate membrane_absorb that orchestrates all atom functions.
     * This replaces the old hardcoded create_membrane_absorb().
     */
    llvm::Function* create_membrane_absorb(const std::vector<Atom>& atoms) {
        auto* f64 = builder.getDoubleTy();
        llvm::Type* param_types[] = {f64, f64}; /* signal, rw */
        auto* fn_type = llvm::FunctionType::get(f64, param_types, false);
        auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage,
                                           "membrane_absorb", module.get());

        auto* entry = llvm::BasicBlock::Create(context, "entry", fn);
        auto* compute = llvm::BasicBlock::Create(context, "compute", fn);
        auto* decide = llvm::BasicBlock::Create(context, "decide", fn);

        builder.SetInsertPoint(entry);
        auto* signal = fn->getArg(0);
        auto* rw = fn->getArg(1);
        builder.CreateBr(compute);

        builder.SetInsertPoint(compute);

        /* Call each atom function generically — the loop IS the physics */
        llvm::Value* speed = llvm::ConstantFP::get(f64, 0.0);
        for (auto& atom : atoms) {
            std::string fn_name = "csos_atom_" + atom.name;
            auto* atom_fn = module->getFunction(fn_name);
            if (!atom_fn) continue;

            /* Create default params on stack */
            std::vector<double> defaults;
            for (auto& [k, v] : atom.params) defaults.push_back(v);
            auto* arr = builder.CreateAlloca(f64, builder.getInt32(defaults.size()), "params");
            for (size_t i = 0; i < defaults.size(); i++) {
                auto* idx = builder.getInt32(i);
                auto* ptr = builder.CreateGEP(f64, arr, idx);
                builder.CreateStore(llvm::ConstantFP::get(f64, defaults[i]), ptr);
            }

            auto* result = builder.CreateCall(atom_fn,
                {arr, builder.getInt32(defaults.size()), signal}, atom.name + "_result");
            speed = builder.CreateFAdd(speed, result, "speed_acc");
        }
        builder.CreateBr(decide);

        builder.SetInsertPoint(decide);
        auto* cmp = builder.CreateFCmpOGE(speed, rw, "cmp");
        auto* decision = builder.CreateSelect(cmp,
            llvm::ConstantFP::get(f64, 1.0),
            llvm::ConstantFP::get(f64, 0.0), "decision");
        builder.CreateRet(decision);
        return fn;
    }

    void finalize() { llvm::verifyModule(*module); }

    std::string printIR() {
        std::string str;
        llvm::raw_string_ostream os(str);
        module->print(os, nullptr);
        return str;
    }
};

} // namespace csos

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: csos_codegen <spec.csos> [output.ll]" << std::endl;
        return 1;
    }

    std::string spec_path = argv[1];
    std::string output_path = argc > 2 ? argv[2] : "csos.ll";

    csos::SpecParser parser;
    if (!parser.parse(spec_path)) {
        std::cerr << "Failed to parse: " << spec_path << std::endl;
        return 1;
    }
    std::cerr << "Parsed " << parser.atoms.size() << " atoms, "
              << parser.rings.size() << " rings" << std::endl;

    /* Validate: every atom must have a compute expression */
    for (auto& atom : parser.atoms) {
        if (atom.compute.empty()) {
            std::cerr << "WARNING: atom '" << atom.name
                      << "' has no compute expression — skipping IR generation" << std::endl;
        }
    }

    csos::Codegen codegen;

    /* Generate one function per atom from its compute expression */
    for (auto& atom : parser.atoms) {
        if (atom.compute.empty()) continue;
        codegen.create_atom_fn(atom);
        std::cerr << "  Generated IR for atom: " << atom.name
                  << " [" << atom.compute << "]" << std::endl;
    }

    /* Generate orchestrating membrane_absorb */
    codegen.create_membrane_absorb(parser.atoms);
    codegen.finalize();

    std::string ir = codegen.printIR();
    std::ofstream out(output_path);
    out << ir;
    out.close();

    std::cerr << "Generated: " << output_path
              << " (" << ir.size() << " bytes)" << std::endl;
    return 0;
}
