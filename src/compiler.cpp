// Транспилятор Umbrly -> C++ (см. include/compiler.h для общей идеи).
#include "compiler.h"

#include <cstdio>
#include <sstream>
#include <unordered_map>

#include "errors.h"
#include "interpreter.h"

namespace umbrly {

namespace {

std::string escapeCppString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': break;  // просто отбрасываем — \n сам по себе разделяет строки
            default:   out += (char)c;
        }
    }
    return out;
}

std::string typeCppName(Type t) {
    switch (t) {
        case Type::INT:    return "umbrly::Type::INT";
        case Type::FLOAT:  return "umbrly::Type::FLOAT";
        case Type::STR:    return "umbrly::Type::STR";
        case Type::BOOL:   return "umbrly::Type::BOOL";
        case Type::ARRAY:  return "umbrly::Type::ARRAY";
        case Type::OBJECT: return "umbrly::Type::OBJECT";
        default:           return "umbrly::Type::NIL";
    }
}

std::string formatDoubleLiteral(double d) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", d);
    std::string s = buf;
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
        s.find("inf") == std::string::npos && s.find("nan") == std::string::npos) {
        s += ".0";
    }
    return s;
}

// Рекурсивно собирает имена, которым присваивают простым присваиванием (не по индексу и не
// через поле объекта) — они должны быть заранее объявлены как локальные C++-переменные Value.
// В FUNC/CLASS не спускаемся — у них своя область видимости, обрабатываются отдельно.
void collectAssignedNames(const Block& block, std::vector<std::string>& order,
                          std::unordered_map<std::string, bool>& seen) {
    auto add = [&](const std::string& n) {
        if (!seen.count(n)) { seen[n] = true; order.push_back(n); }
    };
    for (const auto& stmtPtr : block) {
        Stmt* s = stmtPtr.get();
        if (auto* a = dynamic_cast<AssignStmt*>(s)) {
            if (!a->index && !a->memberBase) add(a->name);
        } else if (auto* i = dynamic_cast<IfStmt*>(s)) {
            collectAssignedNames(i->thenBlock, order, seen);
            collectAssignedNames(i->elseBlock, order, seen);
        } else if (auto* w = dynamic_cast<WhileStmt*>(s)) {
            collectAssignedNames(w->body, order, seen);
        } else if (auto* f = dynamic_cast<ForStmt*>(s)) {
            add(f->varName);
            collectAssignedNames(f->body, order, seen);
        } else if (auto* t = dynamic_cast<TryStmt*>(s)) {
            collectAssignedNames(t->tryBlock, order, seen);
            if (!t->errVarName.empty()) add(t->errVarName);
            collectAssignedNames(t->catchBlock, order, seen);
        }
        // FuncDefStmt / ClassDefStmt: отдельная область видимости, здесь не собираем.
    }
}

class Compiler {
public:
    explicit Compiler(const Block& program) : program_(program) {
        for (const auto& stmt : program_) {
            if (auto* fd = dynamic_cast<FuncDefStmt*>(stmt.get())) {
                funcParamCount_[fd->name] = fd->params.size();
            }
        }
    }

    std::string generate(const std::string& originalSource) {
        out_ << "// Автоматически сгенерировано Umbrly (умbrly -c / -b) — не редактировать вручную.\n"
             << "#include <windows.h>\n\n"
             << "#include <iostream>\n"
             << "#include <string>\n"
             << "#include <vector>\n\n"
             << "#include \"errors.h\"\n"
             << "#include \"lexer.h\"\n"
             << "#include \"parser.h\"\n"
             << "#include \"interpreter.h\"\n"
             << "#include \"builtins.h\"\n"
             << "#include \"winapi_bindings.h\"\n\n";

        out_ << "static const char* kUmbrlySource =\n\"" << escapeCppString(originalSource) << "\";\n\n";

        // Переменные верхнего уровня — настоящие C++-глобалы (не локальные в main()!). В
        // интерпретаторе любая функция читает их через откат в глобальную область видимости
        // (Interpreter::getVar) — например, snake.umb полагается на то, что CELL_CHAR/BUILD_ROW
        // видят INT_BOARD_W/ARR_SNAKE, ни разу не получая их параметром. Обычная область
        // видимости C++ даёт это бесплатно, если объявить их на уровне файла. Присваивание
        // внутри функции по-прежнему создаёт только локальную тень (не трогает эти глобалы),
        // потому что emitFuncDef хостит для функции лишь то, что ЕЙ САМОЙ присваивают.
        indent_ = 0;
        emitHoistedLocals(program_, {});
        out_ << "\n";

        for (const auto& stmt : program_) {
            if (auto* fd = dynamic_cast<FuncDefStmt*>(stmt.get())) emitFuncProto(fd);
        }
        out_ << "\n";
        for (const auto& stmt : program_) {
            if (auto* fd = dynamic_cast<FuncDefStmt*>(stmt.get())) emitFuncDef(fd);
        }

        emitMain();
        return out_.str();
    }

private:
    const Block& program_;
    std::ostringstream out_;
    int indent_ = 1;
    std::unordered_map<std::string, size_t> funcParamCount_;
    int uid_ = 0;

    void ind() { out_ << std::string((size_t)indent_ * 4, ' '); }
    void emit(const std::string& s) { ind(); out_ << s << "\n"; }

    std::string sig(int line) { return std::to_string(line); }
    std::string functionCppName(const std::string& name) const { return "umbrly_fn_" + name; }

    void emitFuncProto(const FuncDefStmt* fd) {
        out_ << "umbrly::Value " << functionCppName(fd->name) << "(umbrly::Interpreter& interp";
        for (const auto& p : fd->params) out_ << ", umbrly::Value " << p << "_arg";
        out_ << ");\n";
    }

    void emitFuncDef(const FuncDefStmt* fd) {
        out_ << "umbrly::Value " << functionCppName(fd->name) << "(umbrly::Interpreter& interp";
        for (const auto& p : fd->params) out_ << ", umbrly::Value " << p << "_arg";
        out_ << ") {\n";
        indent_ = 1;
        for (const auto& p : fd->params) {
            emit("umbrly::Value " + p + " = interp.coerce(" + typeCppName(Interpreter::typeOfName(p, fd->line)) +
                 ", " + p + "_arg, " + sig(fd->line) + ");");
        }
        emitHoistedLocals(*fd->body, fd->params);
        genBlock(*fd->body);
        emit("return umbrly::Value::Nil();");
        out_ << "}\n\n";
    }

    void emitHoistedLocals(const Block& body, const std::vector<std::string>& exclude) {
        std::vector<std::string> order;
        std::unordered_map<std::string, bool> seen;
        for (const auto& e : exclude) seen[e] = true;
        collectAssignedNames(body, order, seen);
        for (const auto& n : order) emit("umbrly::Value " + n + ";");
    }

    void emitMain() {
        out_ <<
            "int main(int, char**) {\n"
            "    SetConsoleOutputCP(CP_UTF8);\n"
            "    SetConsoleCP(CP_UTF8);\n"
            "    umbrly::Interpreter interp;\n"
            "    umbrly::registerCoreBuiltins(interp);\n"
            "    umbrly::registerWinApiBuiltins(interp);\n"
            // _boot_prog должен жить всё время работы программы: Interpreter::classes_ хранит
            // невладеющие указатели на его узлы ClassDefStmt (методы выполняются интерпретацией
            // AST даже в скомпилированной программе) — если он уничтожится раньше, указатели
            // повиснут и любой вызов метода/создание объекта будет undefined behavior.
            "    umbrly::Block _boot_prog;\n"
            "    try {\n"
            "        std::vector<umbrly::Token> _boot_toks = umbrly::tokenize(kUmbrlySource);\n"
            "        _boot_prog = umbrly::parseProgram(_boot_toks);\n"
            "        interp.registerAllDefs(_boot_prog);\n"
            "    } catch (const umbrly::UmbrlyError& e) {\n"
            "        std::cerr << \"Ошибка инициализации: \" << e.what() << \"\\n\";\n"
            "        return 1;\n"
            "    }\n"
            "    try {\n";
        indent_ = 2;
        genBlock(program_);
        out_ <<
            "    } catch (const umbrly::UmbrlyError& e) {\n"
            "        std::cerr << \"Ошибка \" << e.what() << \"\\n\";\n"
            "        return 1;\n"
            "    }\n"
            "    return 0;\n"
            "}\n";
    }

    // ---------- Операторы ----------

    void genBlock(const Block& block) {
        for (const auto& s : block) genStmt(s.get());
    }

    void genStmt(const Stmt* s) {
        if (auto* p = dynamic_cast<const PrintStmt*>(s)) {
            if (p->value) emit("std::cout << (" + genExpr(p->value.get()) + ").toString() << \"\\n\";");
            else emit("std::cout << \"\\n\";");
        } else if (auto* ip = dynamic_cast<const InputPauseStmt*>(s)) {
            if (ip->prompt) emit("std::cout << (" + genExpr(ip->prompt.get()) + ").toString() << std::flush;");
            emit("{ std::string _dummy; std::getline(std::cin, _dummy); }");
        } else if (auto* a = dynamic_cast<const AssignStmt*>(s)) {
            genAssign(a);
        } else if (auto* e = dynamic_cast<const ExprStmt*>(s)) {
            emit(genExpr(e->expr.get()) + ";");
        } else if (auto* i = dynamic_cast<const IfStmt*>(s)) {
            emit("if ((" + genExpr(i->cond.get()) + ").truthy()) {");
            indent_++; genBlock(i->thenBlock); indent_--;
            if (!i->elseBlock.empty()) {
                emit("} else {");
                indent_++; genBlock(i->elseBlock); indent_--;
            }
            emit("}");
        } else if (auto* w = dynamic_cast<const WhileStmt*>(s)) {
            emit("while ((" + genExpr(w->cond.get()) + ").truthy()) {");
            indent_++; genBlock(w->body); indent_--;
            emit("}");
        } else if (auto* f = dynamic_cast<const ForStmt*>(s)) {
            genFor(f);
        } else if (dynamic_cast<const BreakStmt*>(s)) {
            emit("break;");
        } else if (dynamic_cast<const ContinueStmt*>(s)) {
            emit("continue;");
        } else if (auto* r = dynamic_cast<const ReturnStmt*>(s)) {
            emit(r->value ? ("return " + genExpr(r->value.get()) + ";") : "return umbrly::Value::Nil();");
        } else if (auto* t = dynamic_cast<const TryStmt*>(s)) {
            genTry(t);
        } else if (dynamic_cast<const FuncDefStmt*>(s) || dynamic_cast<const ClassDefStmt*>(s)) {
            // Зарегистрированы заранее (прототипы/CLASS через registerAllDefs) — здесь пропускаем.
        } else {
            failAt(s->line, "компилятор: неизвестный узел оператора (внутренняя ошибка)");
        }
    }

    void genFor(const ForStmt* f) {
        Type ty = Interpreter::typeOfName(f->varName, f->line);
        if (ty != Type::INT && ty != Type::FLOAT)
            failAt(f->line, "переменная цикла FOR должна быть INT_ или FLOAT_ (получено: " + f->varName + ")");
        std::string id = std::to_string(uid_++);
        emit("{");
        indent_++;
        emit("umbrly::Value _from" + id + " = interp.coerce(" + typeCppName(ty) + ", " + genExpr(f->fromExpr.get()) + ", " + sig(f->line) + ");");
        emit("umbrly::Value _to" + id + " = interp.coerce(" + typeCppName(ty) + ", " + genExpr(f->toExpr.get()) + ", " + sig(f->line) + ");");
        std::string stepExpr = f->stepExpr
            ? ("interp.coerce(" + typeCppName(ty) + ", " + genExpr(f->stepExpr.get()) + ", " + sig(f->line) + ")")
            : (ty == Type::INT ? "umbrly::Value::Int(1)" : "umbrly::Value::Float(1.0)");
        emit("umbrly::Value _step" + id + " = " + stepExpr + ";");
        emit("double _stepD" + id + " = _step" + id + ".num();");
        emit("if (_stepD" + id + " == 0.0) throw umbrly::UmbrlyError(\"(строка " + std::to_string(f->line) + "): шаг цикла FOR (STEP) не может быть равен нулю\");");
        emit("double _end" + id + " = _to" + id + ".num();");
        // Приращение — в самом update-выражении for(;;;), а не последней строкой тела: если
        // тело содержит "continue;" (из CONTINUE), нативный C++ continue прыгает сразу к этому
        // update-выражению, минуя всё, что написано в теле после места continue. Раньше
        // приращение стояло в конце тела и continue его пропускал — счётчик замирал навсегда.
        emit("for (double _cur" + id + " = _from" + id + ".num();" +
             " (_stepD" + id + " > 0 ? _cur" + id + " <= _end" + id + " : _cur" + id + " >= _end" + id + ");" +
             " _cur" + id + " += _stepD" + id + ") {");
        indent_++;
        emit(f->varName + " = " + (ty == Type::INT
                ? ("umbrly::Value::Int((long long)_cur" + id + ");")
                : ("umbrly::Value::Float(_cur" + id + ");")));
        genBlock(f->body);
        indent_--;
        emit("}");
        indent_--;
        emit("}");
    }

    void genTry(const TryStmt* t) {
        emit("try {");
        indent_++; genBlock(t->tryBlock); indent_--;
        emit("} catch (const umbrly::UmbrlyError& _e) {");
        indent_++;
        if (!t->errVarName.empty()) {
            emit(t->errVarName + " = interp.coerceAssign(\"" + t->errVarName + "\", umbrly::Value::Str(_e.what()), " +
                 sig(t->line) + ");");
        }
        genBlock(t->catchBlock);
        indent_--;
        emit("}");
    }

    void genAssign(const AssignStmt* a) {
        if (a->isInput) {
            std::string prompt = a->inputPrompt ? ("(" + genExpr(a->inputPrompt.get()) + ").toString()") : "std::string()";
            emit(a->name + " = interp.readInputTyped(" + typeCppName(Interpreter::typeOfName(a->name, a->line)) +
                 ", " + prompt + ", " + sig(a->line) + ");");
            return;
        }
        if (a->memberBase) {
            emit("interp.setMemberField(" + genExpr(a->memberBase.get()) + ", \"" + a->memberName + "\", " +
                 genExpr(a->value.get()) + ", " + sig(a->line) + ");");
            return;
        }
        if (a->index) {
            emit("interp.indexSet(" + a->name + ", " + genExpr(a->index.get()) + ", " + genExpr(a->value.get()) +
                 ", " + sig(a->line) + ");");
            return;
        }
        if (Interpreter::typeOfName(a->name, a->line) == Type::INT && isStaticInt(a->value.get()))
            emit(a->name + " = " + genExpr(a->value.get()) + ";");
        else
            emit(a->name + " = interp.coerceAssign(\"" + a->name + "\", " + genExpr(a->value.get()) + ", " + sig(a->line) + ");");
    }

    // ---------- Выражения ----------

    bool isStaticInt(const Expr* e) const {
        if (dynamic_cast<const IntLitExpr*>(e)) return true;
        if (auto* n = dynamic_cast<const NameExpr*>(e)) return n->name.rfind("INT_", 0) == 0;
        if (auto* u = dynamic_cast<const UnaryExpr*>(e))
            return u->op == "-" && isStaticInt(u->operand.get());
        if (auto* b = dynamic_cast<const BinaryExpr*>(e))
            return (b->op == "+" || b->op == "-" || b->op == "*" || b->op == "%") &&
                   isStaticInt(b->lhs.get()) && isStaticInt(b->rhs.get());
        return false;
    }

    std::string genExpr(const Expr* e) {
        if (auto* v = dynamic_cast<const IntLitExpr*>(e))
            return "umbrly::Value::Int(" + std::to_string(v->value) + "LL)";
        if (auto* v = dynamic_cast<const FloatLitExpr*>(e))
            return "umbrly::Value::Float(" + formatDoubleLiteral(v->value) + ")";
        if (auto* v = dynamic_cast<const BoolLitExpr*>(e))
            return std::string("umbrly::Value::Bool(") + (v->value ? "true" : "false") + ")";
        if (dynamic_cast<const NilLitExpr*>(e))
            return "umbrly::Value::Nil()";
        if (auto* v = dynamic_cast<const StringLitExpr*>(e))
            return genStringLit(v);
        if (auto* v = dynamic_cast<const NameExpr*>(e))
            return v->name;
        if (auto* v = dynamic_cast<const ArrayLitExpr*>(e))
            return genArrayLit(v);
        if (auto* v = dynamic_cast<const IndexExpr*>(e))
            return "interp.indexGet(" + genExpr(v->base.get()) + ", " + genExpr(v->index.get()) + ", " + sig(v->line) + ")";
        if (auto* v = dynamic_cast<const CallExpr*>(e))
            return genCall(v);
        if (auto* v = dynamic_cast<const UnaryExpr*>(e))
            return genUnary(v);
        if (auto* v = dynamic_cast<const BinaryExpr*>(e))
            return genBinary(v);
        if (auto* v = dynamic_cast<const MemberExpr*>(e))
            return "interp.getMemberField(" + genExpr(v->base.get()) + ", \"" + v->name + "\", " + sig(v->line) + ")";
        if (auto* v = dynamic_cast<const MethodCallExpr*>(e))
            return genMethodCall(v);
        failAt(e->line, "компилятор: неизвестный узел выражения (внутренняя ошибка)");
    }

    std::string genStringLit(const StringLitExpr* v) {
        if (v->segments.size() == 1 && !v->segments[0].expr)
            return "umbrly::Value::Str(std::string(\"" + escapeCppString(v->segments[0].literal) + "\"))";
        std::string s = "umbrly::Value::Str(std::string(\"\")";
        for (const auto& seg : v->segments) {
            if (seg.expr) s += " + (" + genExpr(seg.expr.get()) + ").toString()";
            else s += " + std::string(\"" + escapeCppString(seg.literal) + "\")";
        }
        s += ")";
        return s;
    }

    std::string genArrayLit(const ArrayLitExpr* v) {
        std::string s = "umbrly::Value::Array({";
        for (size_t k = 0; k < v->items.size(); ++k) {
            if (k) s += ", ";
            s += genExpr(v->items[k].get());
        }
        s += "})";
        return s;
    }

    std::string genCall(const CallExpr* c) {
        auto it = funcParamCount_.find(c->name);
        if (it != funcParamCount_.end()) {
            if (c->args.size() != it->second)
                failAt(c->line, "функция '" + c->name + "' ожидает " + std::to_string(it->second) +
                                    " аргумент(ов), получено " + std::to_string(c->args.size()));
            std::string s = functionCppName(c->name) + "(interp";
            for (const auto& a : c->args) s += ", " + genExpr(a.get());
            s += ")";
            return s;
        }
        std::string args;
        for (size_t k = 0; k < c->args.size(); ++k) {
            if (k) args += ", ";
            args += genExpr(c->args[k].get());
        }
        return "[&]() -> umbrly::Value { std::vector<umbrly::Value> _a{" + args + "}; return interp.callFunction(\"" +
               c->name + "\", _a, " + sig(c->line) + "); }()";
    }

    std::string genMethodCall(const MethodCallExpr* v) {
        std::string args;
        for (size_t k = 0; k < v->args.size(); ++k) {
            if (k) args += ", ";
            args += genExpr(v->args[k].get());
        }
        return "[&]() -> umbrly::Value { umbrly::Value _self = " + genExpr(v->base.get()) +
               "; std::vector<umbrly::Value> _a{" + args + "}; return interp.callMethodDynamic(_self, \"" +
               v->method + "\", _a, " + sig(v->line) + "); }()";
    }

    std::string genUnary(const UnaryExpr* v) {
        std::string operand = genExpr(v->operand.get());
        if (v->op == "-") return "interp.opNeg(" + operand + ", " + sig(v->line) + ")";
        if (v->op == "NOT") return "interp.opNot(" + operand + ")";
        failAt(v->line, "компилятор: неизвестный унарный оператор '" + v->op + "'");
    }

    std::string genBinary(const BinaryExpr* v) {
        std::string l = genExpr(v->lhs.get());
        std::string r = genExpr(v->rhs.get());
        if (v->op == "AND") return "((" + l + ").truthy() ? umbrly::Value::Bool((" + r + ").truthy()) : umbrly::Value::Bool(false))";
        if (v->op == "OR")  return "((" + l + ").truthy() ? umbrly::Value::Bool(true) : umbrly::Value::Bool((" + r + ").truthy()))";
        if (isStaticInt(v->lhs.get()) && isStaticInt(v->rhs.get())) {
            if (v->op == "+") return "umbrly::Value::Int((" + l + ").i + (" + r + ").i)";
            if (v->op == "-") return "umbrly::Value::Int((" + l + ").i - (" + r + ").i)";
            if (v->op == "*") return "umbrly::Value::Int((" + l + ").i * (" + r + ").i)";
            if (v->op == "%") {
                if (auto* lit = dynamic_cast<const IntLitExpr*>(v->rhs.get()); lit && lit->value != 0)
                    return "umbrly::Value::Int((" + l + ").i % " + std::to_string(lit->value) + "LL)";
            }
            if (v->op == "==") return "umbrly::Value::Bool((" + l + ").i == (" + r + ").i)";
            if (v->op == "!=") return "umbrly::Value::Bool((" + l + ").i != (" + r + ").i)";
            if (v->op == "<")  return "umbrly::Value::Bool((" + l + ").i < (" + r + ").i)";
            if (v->op == ">")  return "umbrly::Value::Bool((" + l + ").i > (" + r + ").i)";
            if (v->op == "<=") return "umbrly::Value::Bool((" + l + ").i <= (" + r + ").i)";
            if (v->op == ">=") return "umbrly::Value::Bool((" + l + ").i >= (" + r + ").i)";
        }
        static const std::unordered_map<std::string, std::string> kMethod = {
            {"+", "opAdd"}, {"-", "opSub"}, {"*", "opMul"}, {"/", "opDiv"}, {"%", "opMod"},
            {"==", "opEq"}, {"!=", "opNe"}, {"<", "opLt"}, {">", "opGt"}, {"<=", "opLe"}, {">=", "opGe"},
        };
        auto it = kMethod.find(v->op);
        if (it == kMethod.end()) failAt(v->line, "компилятор: неизвестный оператор '" + v->op + "'");
        return "interp." + it->second + "(" + l + ", " + r + ", " + sig(v->line) + ")";
    }
};

}  // namespace

std::string generateCpp(const Block& program, const std::string& originalSource) {
    Compiler c(program);
    return c.generate(originalSource);
}

}  // namespace umbrly
