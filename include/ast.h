// Узлы AST Umbrly. Сам разбор — в parser.cpp, вычисление/выполнение — в ast.cpp
// (там уже подключается полный интерфейс Interpreter).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "value.h"

namespace umbrly {

class Interpreter;

// ---------- Выражения ----------

class Expr {
public:
    int line = 0;
    virtual ~Expr() = default;
    virtual Value eval(Interpreter& interp) const = 0;
};
using ExprPtr = std::unique_ptr<Expr>;

class IntLitExpr : public Expr {
public:
    long long value;
    explicit IntLitExpr(long long v) : value(v) {}
    Value eval(Interpreter& interp) const override;
};

class FloatLitExpr : public Expr {
public:
    double value;
    explicit FloatLitExpr(double v) : value(v) {}
    Value eval(Interpreter& interp) const override;
};

// Один фрагмент строкового литерала: либо кусок литерального текста, либо
// заранее разобранное [выражение]. Строится один раз в parser.cpp — при
// вычислении StringLitExpr больше не токенизирует и не парсит ничего заново.
struct StringSegment {
    std::string literal;  // используется, если expr == nullptr
    ExprPtr expr;          // используется, если это была подстановка [ ... ]
};

class StringLitExpr : public Expr {
public:
    std::vector<StringSegment> segments;
    Value eval(Interpreter& interp) const override;
};

class BoolLitExpr : public Expr {
public:
    bool value;
    explicit BoolLitExpr(bool v) : value(v) {}
    Value eval(Interpreter& interp) const override;
};

class NilLitExpr : public Expr {
public:
    Value eval(Interpreter& interp) const override;
};

class NameExpr : public Expr {
public:
    std::string name;
    mutable void* globalCache = nullptr;
    mutable int localIndex = -1;
    explicit NameExpr(std::string n) : name(std::move(n)) {}
    Value eval(Interpreter& interp) const override;
};

class ArrayLitExpr : public Expr {
public:
    std::vector<ExprPtr> items;
    Value eval(Interpreter& interp) const override;
};

class IndexExpr : public Expr {
public:
    ExprPtr base;
    ExprPtr index;
    Value eval(Interpreter& interp) const override;
};

class CallExpr : public Expr {
public:
    std::string name;
    std::vector<ExprPtr> args;
    // Инлайн-кэш разрешённой FUNC: заполняется Interpreter::callFunction при первом
    // вызове (тип стёрт до void*, чтобы ast.h не тянул приватные детали Interpreter) —
    // при повторных вызовах (в т.ч. рекурсивных) избегает хеш-лукапа имени функции по
    // строке на каждый вызов. Всегда указывает на актуальный FuncInfo даже после
    // переопределения той же функции в REPL — адрес узла хеш-таблицы не меняется при
    // присваивании существующему ключу.
    mutable void* callCache = nullptr;
    Value eval(Interpreter& interp) const override;
};

// obj.NAME — чтение поля объекта.
class MemberExpr : public Expr {
public:
    ExprPtr base;
    std::string name;
    Value eval(Interpreter& interp) const override;
};

// obj.METHOD(args) — вызов метода класса с неявным SELF = obj.
class MethodCallExpr : public Expr {
public:
    ExprPtr base;
    std::string method;
    std::vector<ExprPtr> args;
    Value eval(Interpreter& interp) const override;
};

class UnaryExpr : public Expr {
public:
    std::string op;  // "-" | "+" | "NOT"
    ExprPtr operand;
    Value eval(Interpreter& interp) const override;
};

class BinaryExpr : public Expr {
public:
    std::string op;  // + - * / % == != < > <= >= AND OR
    ExprPtr lhs, rhs;
    Value eval(Interpreter& interp) const override;
};

// ---------- Операторы ----------

// Результат выполнения оператора: как управление потоком уходит из блока.
// Заменяет прежний вариант на C++-исключениях (BreakSignal/ContinueSignal/
// ReturnSignal) — на каждой итерации цикла исключения не бросаются вообще,
// сигнал просто "всплывает" обычным возвращаемым значением.
enum class Signal { None, Break, Continue, Return };

class Stmt {
public:
    int line = 0;
    virtual ~Stmt() = default;
    virtual Signal exec(Interpreter& interp) const = 0;
};
using StmtPtr = std::unique_ptr<Stmt>;
using Block = std::vector<StmtPtr>;

class PrintStmt : public Stmt {
public:
    ExprPtr value;  // может быть nullptr — просто перевод строки
    Signal exec(Interpreter& interp) const override;
};

class InputPauseStmt : public Stmt {  // INPUT: "..." как самостоятельная команда (пауза)
public:
    ExprPtr prompt;
    Signal exec(Interpreter& interp) const override;
};

// Присваивание: name = value | name[index] = value | base.field = value | name = INPUT: "..."
class AssignStmt : public Stmt {
public:
    std::string name;
    mutable void* globalCache = nullptr;
    mutable int localIndex = -1;
    ExprPtr index;         // не nullptr для name[index] = value
    ExprPtr memberBase;    // не nullptr для base.field = value (base — любое выражение-объект)
    std::string memberName;
    ExprPtr value;         // используется, если !isInput
    bool isInput = false;
    ExprPtr inputPrompt;   // используется, если isInput (может быть nullptr — без приглашения)
    Signal exec(Interpreter& interp) const override;
};

class ExprStmt : public Stmt {  // выражение как оператор (обычно — вызов функции)
public:
    ExprPtr expr;
    Signal exec(Interpreter& interp) const override;
};

class IfStmt : public Stmt {
public:
    ExprPtr cond;
    Block thenBlock;
    Block elseBlock;  // может быть пустым
    Signal exec(Interpreter& interp) const override;
};

class WhileStmt : public Stmt {
public:
    ExprPtr cond;
    Block body;
    mutable std::shared_ptr<void> fastIntCache;
    Signal exec(Interpreter& interp) const override;
};

class ForStmt : public Stmt {
public:
    std::string varName;
    ExprPtr fromExpr, toExpr, stepExpr;  // stepExpr может быть nullptr (шаг = 1)
    Block body;
    mutable std::shared_ptr<void> fastIntCache;
    Signal exec(Interpreter& interp) const override;
};

class FuncDefStmt : public Stmt {
public:
    std::string name;
    std::vector<std::string> params;
    std::shared_ptr<Block> body;  // shared, т.к. интерпретатор хранит указатель в таблице функций
    Signal exec(Interpreter& interp) const override;  // регистрирует функцию
};

class ReturnStmt : public Stmt {
public:
    ExprPtr value;  // nullptr -> NIL
    Signal exec(Interpreter& interp) const override;
};

class BreakStmt : public Stmt {
public:
    Signal exec(Interpreter& interp) const override;
};

class ContinueStmt : public Stmt {
public:
    Signal exec(Interpreter& interp) const override;
};

class TryStmt : public Stmt {
public:
    Block tryBlock;
    std::string errVarName;  // может быть пустым, если CATCH без переменной
    Block catchBlock;
    Signal exec(Interpreter& interp) const override;
};

// LOAD ИМЯ — импорт модуля/библиотеки. Разрешается на этапе загрузки файла
// (main.cpp, resolveLoads): содержимое найденного ИМЯ.umb вставляется на
// место этого узла ещё до выполнения (и до кодогенерации компилятора), так
// что exec() здесь срабатывает только если LOAD оказался вложен внутрь
// IF/WHILE/FUNC/TRY и т.п. — там импорт не поддерживается (только верхний
// уровень файла).
class LoadStmt : public Stmt {
public:
    std::string moduleName;
    Signal exec(Interpreter& interp) const override;
};

// Поле класса с выражением значения по умолчанию (вычисляется при каждом создании объекта).
struct ClassField {
    std::string name;
    ExprPtr defaultValue;
};

// CLASS Имя: поля... методы (FUNC)... END
// Экземпляры создаются вызовом ИмяКласса(аргументы) — как обычный вызов функции
// (см. Interpreter::callFunction), метод INIT (если объявлен) играет роль конструктора.
// Инкапсуляция полей верхнего уровня, наследования нет — см. README.
class ClassDefStmt : public Stmt {
public:
    std::string name;
    std::vector<ClassField> fields;
    std::vector<std::unique_ptr<FuncDefStmt>> methods;
    Signal exec(Interpreter& interp) const override;  // регистрирует класс
};

}  // namespace umbrly
