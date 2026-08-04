// Интерпретатор Umbrly: переменные, функции, встроенные функции, классы, выполнение AST.
#pragma once

#include <functional>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast.h"
#include "value.h"

namespace umbrly {

struct FastIntProgram;
struct FastValueProgram;

// Сигнатура встроенной (нативной) функции — как обычных билтинов, так и WinAPI-обёрток.
using NativeFn = std::function<Value(Interpreter&, std::vector<Value>&, int line)>;

class Interpreter {
public:
    Interpreter();

    // Регистрация: используется builtins.cpp и winapi_bindings.cpp при старте.
    void registerBuiltin(const std::string& name, NativeFn fn);

    // Запуск программы верхнего уровня: сначала регистрирует все FUNC и CLASS, затем исполняет операторы.
    void run(const Block& program);

    // ---- Используется узлами AST (ast.cpp) ----
    Value getVar(const std::string& name, int line) const;
    void  setVar(const std::string& name, Value v, int line);
    Value getVarCached(const std::string& name, int line, void** globalCache) const;
    void  setVarCached(const std::string& name, Value v, int line, void** globalCache);
    Value getVarFast(const std::string& name, int line, void** globalCache, int* localIndex) const;
    void  setVarFast(const std::string& name, Value v, int line, void** globalCache, int* localIndex);
    void  setIndexed(const std::string& name, const Value& index, Value v, int line);

    // Возвращает Signal::None, либо сигнал, всплывший из последнего выполненного оператора
    // (Break/Continue — гасятся ближайшим циклом, Return — доходит до callFunction).
    Signal execBlock(const Block& block);

    // Вызов по имени: сперва пользовательская FUNC, затем CLASS (создаёт экземпляр), затем builtin.
    // cache — необязательный инлайн-кэш вызывающей стороны (см. CallExpr::callCache в ast.h):
    // если не nullptr и уже указывает на разрешённую FUNC, пропускает поиск по имени.
    Value callFunction(const std::string& name, std::vector<Value>& args, int line, void** cache = nullptr);

    void registerFunction(const std::string& name, std::vector<std::string> params,
                           std::shared_ptr<Block> body);

    // ---- Классы ----
    void registerClass(const ClassDefStmt* def);
    Value instantiate(const std::string& className, std::vector<Value>& args, int line);
    Value callMethod(const std::string& className, const std::string& methodName,
                      Value selfObj, std::vector<Value>& args, int line);
    // Как callMethod, но className берётся из самого obj (с проверкой, что это объект) —
    // удобно для сгенерированного кода, где статический тип SELF неизвестен компилятору.
    Value callMethodDynamic(const Value& obj, const std::string& methodName,
                             std::vector<Value>& args, int line);
    bool isClassName(const std::string& name) const { return classes_.count(name) != 0; }

    // Определяет тип переменной по префиксу имени (INT_/FLOAT_/STR_/BOOL_/ARR_/OBJ_).
    static Type typeOfName(const std::string& name, int line);

    // Приводит значение к типу, ожидаемому переменной с данным именем.
    Value coerce(Type target, const Value& v, int line) const;

    // Приведение + клонирование массива при простом присваивании переменной с данным именем
    // (typeOfName(name) определяет целевой тип). Общая логика для интерпретируемого AssignStmt
    // и для сгенерированного компилятором кода (compiler.cpp).
    Value coerceAssign(const std::string& name, const Value& v, int line) const;

    // Читает число/строку с консоли, приводя к нужному типу (используется для INPUT: в присваивании).
    Value readInputTyped(Type t, const std::string& prompt, int line);

    // ---- Операторы как отдельные методы: переиспользуются интерпретируемым BinaryExpr/UnaryExpr
    // и сгенерированным C++ (compiler.cpp), чтобы поведение было гарантированно одинаковым. ----
    Value opAdd(const Value& l, const Value& r, int line) const;
    Value opSub(const Value& l, const Value& r, int line) const;
    Value opMul(const Value& l, const Value& r, int line) const;
    Value opDiv(const Value& l, const Value& r, int line) const;
    Value opMod(const Value& l, const Value& r, int line) const;
    Value opEq(const Value& l, const Value& r, int line) const;
    Value opNe(const Value& l, const Value& r, int line) const;
    Value opLt(const Value& l, const Value& r, int line) const;
    Value opGt(const Value& l, const Value& r, int line) const;
    Value opLe(const Value& l, const Value& r, int line) const;
    Value opGe(const Value& l, const Value& r, int line) const;
    Value opNeg(const Value& v, int line) const;
    Value opNot(const Value& v) const;

    // Индексация массива: чтение/запись по значению (не по имени переменной — нужно и для
    // компилятора, где массив уже лежит в обычной C++-переменной, а не в scopes_).
    Value indexGet(const Value& base, const Value& idx, int line) const;
    void  indexSet(const Value& base, const Value& idx, Value v, int line) const;

    // Поля объектов: то же самое разделение — работают с готовым Value, а не с именем переменной.
    Value getMemberField(const Value& obj, const std::string& fieldName, int line) const;
    void  setMemberField(const Value& obj, const std::string& fieldName, Value v, int line) const;

    // Только предрегистрация FUNC/CLASS без выполнения операторов — используется run() и
    // сгенерированной программой (main() встраивает исходник и регистрирует классы/функции
    // перед вызовом натив-скомпилированной верхнеуровневой логики).
    void registerAllDefs(const Block& program);

    // Хранилище значения RETURN на время всплытия Signal::Return до callFunction.
    void  setPendingReturn(Value v) { pendingReturn_ = std::move(v); }
    Value takePendingReturn() { return std::move(pendingReturn_); }

    // Вызывается перед каждым оператором в execBlock, если задан (используется CLI-флагами
    // -d/-trace для построчной трассировки). В скомпилированных программах не используется.
    void setTraceHook(std::function<void(int line)> hook) { traceHook_ = std::move(hook); }
    void setExecutionLimits(uint64_t maxInstructions, uint64_t timeoutMs);
    void consumeInstruction(int line);
    uint64_t executedInstructions() const { return instructionCount_; }
    bool canUseFastGlobalVm() const { return localDepth_ == 0 && !traceHook_; }
    Value* ensureGlobalSlot(const std::string& name) { return &globals_[name]; }

private:
    // Глобальная область — хеш-таблица (создаётся один раз при старте; лукап должен
    // оставаться быстрым даже при большом числе глобальных переменных).
    std::unordered_map<std::string, Value> globals_;

    // Локальные области вызовов (FUNC/методы) — пул переиспользуемых плоских векторов
    // вместо новой хеш-таблицы на каждый вызов. Внутри одного вызова имён обычно немного
    // (параметры + несколько присваиваний), поэтому линейный поиск быстрее хеширования,
    // а переиспользование фреймов между вызовами одной глубины рекурсии убирает аллокацию
    // из горячего пути — раньше это было главным источником медленных рекурсии/циклов.
    using LocalFrame = std::vector<std::pair<std::string, Value>>;
    std::vector<LocalFrame> localPool_;
    int localDepth_ = 0;  // 0 = сейчас нет активных вызовов (мы в глобальной области)

    void pushLocalScope();
    void popLocalScope();

    struct FuncInfo {
        std::vector<std::string> params;
        std::vector<Type> paramTypes;  // посчитано один раз в registerFunction, не на каждый вызов
        std::shared_ptr<Block> body;
        std::shared_ptr<FastIntProgram> fastInt;
        std::shared_ptr<FastValueProgram> fastValue;
    };
    std::unordered_map<std::string, FuncInfo> functions_;
    std::unordered_map<std::string, NativeFn> builtins_;

    // Не владеющий указатель: ClassDefStmt живёт в AST программы, которая переживает run().
    // Регистрация идемпотентна (как и у FuncDefStmt), поэтому повторный вызов — не проблема.
    std::unordered_map<std::string, const ClassDefStmt*> classes_;
    std::unordered_map<const FuncDefStmt*, std::shared_ptr<FastValueProgram>> methodPrograms_;
    std::unordered_map<const Expr*, std::shared_ptr<FastValueProgram>> fieldDefaultPrograms_;

    Value callMethodOn(const ClassDefStmt* def, const std::string& methodName,
                        Value selfObj, std::vector<Value>& args, int line);

    std::function<void(int)> traceHook_;
    Value pendingReturn_;
    int callDepth_ = 0;
    uint64_t maxInstructions_ = 0;
    uint64_t instructionCount_ = 0;
    uint64_t timeoutMs_ = 0;
    std::chrono::steady_clock::time_point deadline_{};
    static constexpr int kMaxCallDepth = 256;
};

}  // namespace umbrly
