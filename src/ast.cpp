// Реализация eval()/exec() для всех узлов AST — "семантика" языка Umbrly.
#include "ast.h"

#include <iostream>

#include "errors.h"
#include "interpreter.h"

namespace umbrly {

namespace {
enum class FastOp { Const, Load, Store, Add, Sub, Mul, Mod, Eq, Ne, Lt, Gt, Le, Ge,
                    JumpFalse, Jump, Continue, Break };
struct FastInstr { FastOp op; long long imm = 0; Value* slot = nullptr; };
struct FastIntBlock {
    std::vector<FastInstr> code;
    std::vector<long long> stack;
    long long result = 0;
    Signal run(Interpreter& interp, int line) {
        stack.clear(); size_t pc = 0;
        auto pop = [&]() { long long v=stack.back(); stack.pop_back(); return v; };
        while (pc < code.size()) {
            const auto& in=code[pc++];
            interp.consumeInstruction(line);
            switch(in.op) {
                case FastOp::Const: stack.push_back(in.imm); break;
                case FastOp::Load: stack.push_back(in.slot->i); break;
                case FastOp::Store: { auto v=pop(); in.slot->type=Type::INT; in.slot->i=v; break; }
                case FastOp::Add: { auto r=pop(),l=pop(); stack.push_back(l+r); break; }
                case FastOp::Sub: { auto r=pop(),l=pop(); stack.push_back(l-r); break; }
                case FastOp::Mul: { auto r=pop(),l=pop(); stack.push_back(l*r); break; }
                case FastOp::Mod: { auto r=pop(),l=pop(); if(!r) failAt(line,"деление на ноль (%)"); stack.push_back(l%r); break; }
                case FastOp::Eq: { auto r=pop(),l=pop(); stack.push_back(l==r); break; }
                case FastOp::Ne: { auto r=pop(),l=pop(); stack.push_back(l!=r); break; }
                case FastOp::Lt: { auto r=pop(),l=pop(); stack.push_back(l<r); break; }
                case FastOp::Gt: { auto r=pop(),l=pop(); stack.push_back(l>r); break; }
                case FastOp::Le: { auto r=pop(),l=pop(); stack.push_back(l<=r); break; }
                case FastOp::Ge: { auto r=pop(),l=pop(); stack.push_back(l>=r); break; }
                case FastOp::JumpFalse: if(!pop()) pc=(size_t)in.imm; break;
                case FastOp::Jump: pc=(size_t)in.imm; break;
                case FastOp::Continue: return Signal::Continue;
                case FastOp::Break: return Signal::Break;
            }
        }
        result = stack.empty() ? 0 : stack.back();
        return Signal::None;
    }
};
struct FastWhileCode { FastIntBlock cond; FastIntBlock body; };
bool fastExpr(const Expr* e, Interpreter& ip, std::vector<FastInstr>& out) {
    if(auto* v=dynamic_cast<const IntLitExpr*>(e)){out.push_back({FastOp::Const,v->value});return true;}
    if(auto* v=dynamic_cast<const BoolLitExpr*>(e)){out.push_back({FastOp::Const,v->value?1:0});return true;}
    if(auto* v=dynamic_cast<const NameExpr*>(e)){
        if(v->name.rfind("INT_",0)!=0 && v->name.rfind("BOOL_",0)!=0) return false;
        out.push_back({FastOp::Load,0,ip.ensureGlobalSlot(v->name)});return true;
    }
    auto* b=dynamic_cast<const BinaryExpr*>(e);
    if(!b || !fastExpr(b->lhs.get(),ip,out) || !fastExpr(b->rhs.get(),ip,out)) return false;
    FastOp op;
    if(b->op=="+")op=FastOp::Add;else if(b->op=="-")op=FastOp::Sub;else if(b->op=="*")op=FastOp::Mul;
    else if(b->op=="%")op=FastOp::Mod;else if(b->op=="==")op=FastOp::Eq;else if(b->op=="!=")op=FastOp::Ne;
    else if(b->op=="<")op=FastOp::Lt;else if(b->op==">")op=FastOp::Gt;else if(b->op=="<=")op=FastOp::Le;
    else if(b->op==">")op=FastOp::Gt;else if(b->op==">=")op=FastOp::Ge;else return false;
    out.push_back({op});return true;
}
bool fastBlock(const Block& block, Interpreter& ip, std::vector<FastInstr>& out) {
    for(const auto& s:block){
        if(auto* a=dynamic_cast<const AssignStmt*>(s.get())){
            if(a->isInput||a->index||a->memberBase||a->name.rfind("INT_",0)!=0||!fastExpr(a->value.get(),ip,out))return false;
            out.push_back({FastOp::Store,0,ip.ensureGlobalSlot(a->name)});
        }else if(auto* i=dynamic_cast<const IfStmt*>(s.get())){
            if(!fastExpr(i->cond.get(),ip,out))return false;
            size_t jf=out.size();out.push_back({FastOp::JumpFalse});
            if(!fastBlock(i->thenBlock,ip,out))return false;
            if(i->elseBlock.empty())out[jf].imm=(long long)out.size();
            else{size_t j=out.size();out.push_back({FastOp::Jump});out[jf].imm=(long long)out.size();
                 if(!fastBlock(i->elseBlock,ip,out))return false;out[j].imm=(long long)out.size();}
        }else if(dynamic_cast<const ContinueStmt*>(s.get()))out.push_back({FastOp::Continue});
        else if(dynamic_cast<const BreakStmt*>(s.get()))out.push_back({FastOp::Break});
        else return false;
    }return true;
}
} // namespace

namespace {

bool intLike(const Value& v) { return v.type == Type::INT || v.type == Type::BOOL; }
bool bothIntLike(const Value& l, const Value& r) { return intLike(l) && intLike(r); }

void requireNums(const Value& l, const Value& r, const char* op, int line) {
    if (!l.isNum() || !r.isNum())
        failAt(line, std::string("оператор ") + op + " работает только с числами");
}

int compareValues(const Value& l, const Value& r, int line) {
    if (l.type == Type::STR || r.type == Type::STR) {
        if (l.type != Type::STR || r.type != Type::STR)
            failAt(line, "нельзя сравнивать строку со значением другого типа");
        int c = l.s.compare(r.s);
        return (c > 0) - (c < 0);
    }
    if (l.type == Type::ARRAY || r.type == Type::ARRAY)
        failAt(line, "массивы (ARR_) нельзя сравнивать операторами сравнения");
    double a = l.num(), b = r.num();
    return (a > b) - (a < b);
}

}  // namespace

// ---------- Выражения ----------

Value IntLitExpr::eval(Interpreter&) const   { return Value::Int(value); }
Value FloatLitExpr::eval(Interpreter&) const { return Value::Float(value); }
Value BoolLitExpr::eval(Interpreter&) const  { return Value::Bool(value); }
Value NilLitExpr::eval(Interpreter&) const   { return Value::Nil(); }

Value StringLitExpr::eval(Interpreter& interp) const {
    // Сегменты уже разобраны один раз при парсинге (parser.cpp) — здесь только
    // подстановка значений, никакой повторной токенизации/парсинга.
    if (segments.size() == 1 && !segments[0].expr) return Value::Str(segments[0].literal);
    std::string out;
    for (const auto& seg : segments) {
        if (seg.expr) out += seg.expr->eval(interp).toString();
        else out += seg.literal;
    }
    return Value::Str(std::move(out));
}

Value NameExpr::eval(Interpreter& interp) const {
    return interp.getVarFast(name, line, &globalCache, &localIndex);
}

Value ArrayLitExpr::eval(Interpreter& interp) const {
    std::vector<Value> vals;
    vals.reserve(items.size());
    for (const auto& e : items) vals.push_back(e->eval(interp));
    return Value::Array(std::move(vals));
}

Value IndexExpr::eval(Interpreter& interp) const {
    Value b = base->eval(interp);
    if (b.type != Type::ARRAY) failAt(line, "значение не является массивом — индексация [ ] невозможна");
    Value idx = index->eval(interp);
    if (!idx.isNum()) failAt(line, "индекс массива должен быть числом");
    long long i = (long long)idx.num();
    if (i < 0 || (size_t)i >= b.arr->size())
        failAt(line, "индекс " + std::to_string(i) + " вне границ массива (размер " +
                          std::to_string(b.arr->size()) + ")");
    return (*b.arr)[(size_t)i];
}

Value CallExpr::eval(Interpreter& interp) const {
    std::vector<Value> argv;
    argv.reserve(args.size());
    for (const auto& a : args) argv.push_back(a->eval(interp));
    return interp.callFunction(name, argv, line, &callCache);
}

Value MemberExpr::eval(Interpreter& interp) const {
    Value b = base->eval(interp);
    if (b.type != Type::OBJECT) failAt(line, "значение не является объектом — обращение через '.' невозможно");
    auto it = b.obj->fields.find(name);
    if (it == b.obj->fields.end())
        failAt(line, "у объекта класса '" + b.obj->className + "' нет поля '" + name + "'");
    return it->second;
}

Value MethodCallExpr::eval(Interpreter& interp) const {
    Value b = base->eval(interp);
    if (b.type != Type::OBJECT) failAt(line, "значение не является объектом — вызов метода невозможен");
    std::vector<Value> argv;
    argv.reserve(args.size());
    for (const auto& a : args) argv.push_back(a->eval(interp));
    return interp.callMethod(b.obj->className, method, b, argv, line);
}

Value UnaryExpr::eval(Interpreter& interp) const {
    Value v = operand->eval(interp);
    if (op == "-") {
        if (!v.isNum()) failAt(line, "унарный минус применим только к числам");
        return v.type == Type::FLOAT ? Value::Float(-v.f) : Value::Int(-v.i);
    }
    if (op == "NOT") return Value::Bool(!v.truthy());
    failAt(line, "неизвестный унарный оператор '" + op + "'");
}

Value BinaryExpr::eval(Interpreter& interp) const {
    if (op == "AND") {
        if (!lhs->eval(interp).truthy()) return Value::Bool(false);
        return Value::Bool(rhs->eval(interp).truthy());
    }
    if (op == "OR") {
        if (lhs->eval(interp).truthy()) return Value::Bool(true);
        return Value::Bool(rhs->eval(interp).truthy());
    }

    Value l = lhs->eval(interp);
    Value r = rhs->eval(interp);

    if (op == "+") {
        if (l.type == Type::STR || r.type == Type::STR) return Value::Str(l.toString() + r.toString());
        if (l.type == Type::ARRAY || r.type == Type::ARRAY) failAt(line, "оператор + не поддерживает массивы");
        requireNums(l, r, "+", line);
        return bothIntLike(l, r) ? Value::Int(l.i + r.i) : Value::Float(l.num() + r.num());
    }
    if (op == "-") {
        requireNums(l, r, "-", line);
        return bothIntLike(l, r) ? Value::Int(l.i - r.i) : Value::Float(l.num() - r.num());
    }
    if (op == "*") {
        requireNums(l, r, "*", line);
        return bothIntLike(l, r) ? Value::Int(l.i * r.i) : Value::Float(l.num() * r.num());
    }
    if (op == "/") {
        requireNums(l, r, "/", line);
        if (r.num() == 0.0) failAt(line, "деление на ноль");
        if (bothIntLike(l, r) && r.i != 0 && l.i % r.i == 0) return Value::Int(l.i / r.i);
        return Value::Float(l.num() / r.num());
    }
    if (op == "%") {
        if (!bothIntLike(l, r)) failAt(line, "оператор % работает только с целыми числами");
        if (r.i == 0) failAt(line, "деление на ноль (%)");
        return Value::Int(l.i % r.i);
    }

    int c = compareValues(l, r, line);
    if (op == "==") return Value::Bool(c == 0);
    if (op == "!=") return Value::Bool(c != 0);
    if (op == "<")  return Value::Bool(c < 0);
    if (op == ">")  return Value::Bool(c > 0);
    if (op == "<=") return Value::Bool(c <= 0);
    if (op == ">=") return Value::Bool(c >= 0);

    failAt(line, "неизвестный оператор '" + op + "'");
}

// ---------- Операторы ----------
// exec() возвращает Signal вместо того, чтобы бросать исключение для
// BREAK/CONTINUE/RETURN — на каждой итерации цикла это ноль исключений.

Signal PrintStmt::exec(Interpreter& interp) const {
    if (value) std::cout << value->eval(interp).toString() << "\n";
    else std::cout << "\n";
    return Signal::None;
}

Signal InputPauseStmt::exec(Interpreter& interp) const {
    if (prompt) std::cout << prompt->eval(interp).toString() << std::flush;
    std::string dummy;
    std::getline(std::cin, dummy);
    return Signal::None;
}

Signal AssignStmt::exec(Interpreter& interp) const {
    if (isInput) {
        Type ty = Interpreter::typeOfName(name, line);
        std::string p = inputPrompt ? inputPrompt->eval(interp).toString() : "";
        Value v = interp.readInputTyped(ty, p, line);
        interp.setVarFast(name, std::move(v), line, &globalCache, &localIndex);
        return Signal::None;
    }
    if (memberBase) {
        Value obj = memberBase->eval(interp);
        if (obj.type != Type::OBJECT) failAt(line, "слева от '.' должен быть объект");
        Type ft = Interpreter::typeOfName(memberName, line);
        Value v = value->eval(interp);
        Value coerced = interp.coerce(ft, v, line);
        if (ft == Type::ARRAY && coerced.arr) coerced = Value::Array(*coerced.arr);
        obj.obj->fields[memberName] = std::move(coerced);
        return Signal::None;
    }
    if (index) {
        Value idxVal = index->eval(interp);
        Value v = value->eval(interp);
        interp.setIndexed(name, idxVal, std::move(v), line);
        return Signal::None;
    }
    Type ty = Interpreter::typeOfName(name, line);
    Value v = value->eval(interp);
    Value coerced = interp.coerce(ty, v, line);
    // Присваивание массива должно копировать, а не создавать alias: ARR_B = ARR_A
    // не должен делать ARR_B и ARR_A видом на один и тот же вектор. Передача массива
    // параметром функции по-прежнему работает по ссылке (как в Python/JS) — это
    // копирование затрагивает только простое присваивание переменной.
    if (ty == Type::ARRAY && coerced.arr) coerced = Value::Array(*coerced.arr);
    interp.setVarFast(name, std::move(coerced), line, &globalCache, &localIndex);
    return Signal::None;
}

Signal ExprStmt::exec(Interpreter& interp) const {
    expr->eval(interp);
    return Signal::None;
}

Signal IfStmt::exec(Interpreter& interp) const {
    return cond->eval(interp).truthy() ? interp.execBlock(thenBlock) : interp.execBlock(elseBlock);
}

Signal WhileStmt::exec(Interpreter& interp) const {
    std::shared_ptr<FastWhileCode> fast;
    if (interp.canUseFastGlobalVm()) {
        if (fastIntCache) fast = std::static_pointer_cast<FastWhileCode>(fastIntCache);
        else {
            auto candidate = std::make_shared<FastWhileCode>();
            candidate->cond.stack.reserve(16);
            candidate->body.stack.reserve(16);
            if (fastExpr(cond.get(), interp, candidate->cond.code) &&
                fastBlock(body, interp, candidate->body.code)) {
                fast = candidate;
                fastIntCache = candidate;
            }
        }
    }
    if (fast) {
        for (;;) {
            fast->cond.run(interp, line);
            if (!fast->cond.result) break;
            Signal s = fast->body.run(interp, line);
            if (s == Signal::Break) break;
        }
        return Signal::None;
    }
    while (cond->eval(interp).truthy()) {
        Signal s = interp.execBlock(body);
        if (s == Signal::Break) break;
        if (s == Signal::Return) return Signal::Return;
        // Signal::Continue и Signal::None оба просто идут на следующую проверку условия.
    }
    return Signal::None;
}

Signal ForStmt::exec(Interpreter& interp) const {
    Type ty = Interpreter::typeOfName(varName, line);
    if (ty != Type::INT && ty != Type::FLOAT)
        failAt(line, "переменная цикла FOR должна быть INT_ или FLOAT_ (получено: " + varName + ")");

    Value fromV = interp.coerce(ty, fromExpr->eval(interp), line);
    Value toV   = interp.coerce(ty, toExpr->eval(interp), line);
    Value stepV = stepExpr ? interp.coerce(ty, stepExpr->eval(interp), line)
                            : (ty == Type::INT ? Value::Int(1) : Value::Float(1.0));

    double step = stepV.num();
    if (step == 0.0) failAt(line, "шаг цикла FOR (STEP) не может быть равен нулю");
    double end = toV.num();
    double curD = fromV.num();

    auto makeVal = [&](double d) { return ty == Type::INT ? Value::Int((long long)d) : Value::Float(d); };

    std::shared_ptr<FastIntBlock> fast;
    Value* loopSlot = nullptr;
    if (ty == Type::INT && interp.canUseFastGlobalVm()) {
        loopSlot = interp.ensureGlobalSlot(varName);
        if (fastIntCache) fast = std::static_pointer_cast<FastIntBlock>(fastIntCache);
        else {
            auto candidate = std::make_shared<FastIntBlock>();
            candidate->stack.reserve(16);
            if (fastBlock(body, interp, candidate->code)) {
                fast = candidate;
                fastIntCache = candidate;
            }
        }
    }

    for (;;) {
        if (step > 0 ? curD > end : curD < end) break;
        Signal s;
        if (fast) {
            loopSlot->type = Type::INT;
            loopSlot->i = (long long)curD;
            s = fast->run(interp, line);
        } else {
            interp.setVar(varName, makeVal(curD), line);
            s = interp.execBlock(body);
        }
        if (s == Signal::Break) break;
        if (s == Signal::Return) return Signal::Return;
        curD += step;
    }
    return Signal::None;
}

Signal FuncDefStmt::exec(Interpreter& interp) const {
    interp.registerFunction(name, params, body);
    return Signal::None;
}

Signal ReturnStmt::exec(Interpreter& interp) const {
    Value v = value ? value->eval(interp) : Value::Nil();
    interp.setPendingReturn(std::move(v));
    return Signal::Return;
}

Signal BreakStmt::exec(Interpreter&) const { return Signal::Break; }
Signal ContinueStmt::exec(Interpreter&) const { return Signal::Continue; }

Signal TryStmt::exec(Interpreter& interp) const {
    try {
        return interp.execBlock(tryBlock);
    } catch (const UmbrlyError& e) {
        if (!errVarName.empty()) {
            Type ty = Interpreter::typeOfName(errVarName, line);
            interp.setVar(errVarName, interp.coerce(ty, Value::Str(e.what()), line), line);
        }
        return interp.execBlock(catchBlock);
    }
}

Signal ClassDefStmt::exec(Interpreter& interp) const {
    interp.registerClass(this);
    return Signal::None;
}

Signal LoadStmt::exec(Interpreter& interp) const {
    (void)interp;
    failAt(line, "LOAD допустим только на верхнем уровне файла, а не внутри блока (модуль '" + moduleName + "')");
}

}  // namespace umbrly
