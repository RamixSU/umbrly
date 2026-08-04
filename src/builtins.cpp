// Встроенные функции общего назначения: математика, строки, массивы, преобразования типов.
#include "builtins.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>
#include <unordered_set>

#include "errors.h"

namespace umbrly {

namespace {

std::string trimStr(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) b++;
    while (e > b && std::isspace((unsigned char)s[e - 1])) e--;
    return s.substr(b, e - b);
}

void checkArgc(std::vector<Value>& a, size_t n, const char* fn, int line) {
    if (a.size() != n)
        failAt(line, std::string(fn) + "(): ожидается " + std::to_string(n) +
                         " аргумент(ов), получено " + std::to_string(a.size()));
}

void checkArgcRange(std::vector<Value>& a, size_t lo, size_t hi, const char* fn, int line) {
    if (a.size() < lo || a.size() > hi)
        failAt(line, std::string(fn) + "(): ожидается от " + std::to_string(lo) + " до " +
                         std::to_string(hi) + " аргумент(ов), получено " + std::to_string(a.size()));
}

double numArg(const std::vector<Value>& a, size_t i, const char* fn, int line) {
    if (!a[i].isNum())
        failAt(line, std::string(fn) + "(): аргумент " + std::to_string(i + 1) +
                         " должен быть числом, получено " + typeName(a[i].type));
    return a[i].num();
}

const std::string& strArg(const std::vector<Value>& a, size_t i, const char* fn, int line) {
    if (a[i].type != Type::STR)
        failAt(line, std::string(fn) + "(): аргумент " + std::to_string(i + 1) +
                         " должен быть строкой, получено " + typeName(a[i].type));
    return a[i].s;
}

const std::shared_ptr<std::vector<Value>>& arrArg(const std::vector<Value>& a, size_t i, const char* fn, int line) {
    if (a[i].type != Type::ARRAY)
        failAt(line, std::string(fn) + "(): аргумент " + std::to_string(i + 1) +
                         " должен быть массивом (ARR_), получено " + typeName(a[i].type));
    return a[i].arr;
}

bool valuesEqual(const Value& l, const Value& r) {
    if (l.type == Type::STR || r.type == Type::STR) return l.type == r.type && l.s == r.s;
    if (l.type == Type::ARRAY || r.type == Type::ARRAY) return false;
    if (l.type == Type::NIL || r.type == Type::NIL) return l.type == r.type;
    return l.num() == r.num();
}

bool containsArrayIdentity(const Value& value, const void* target,
                           std::unordered_set<const void*>& visited) {
    if (value.type != Type::ARRAY || !value.arr) return false;
    const void* identity = value.arr.get();
    if (identity == target) return true;
    if (!visited.insert(identity).second) return false;
    for (const auto& child : *value.arr) {
        if (containsArrayIdentity(child, target, visited)) return true;
    }
    return false;
}

std::mt19937_64& rng() {
    static std::mt19937_64 gen(std::random_device{}());
    return gen;
}

}  // namespace

void registerCoreBuiltins(Interpreter& interp) {
    // ---------- Математика ----------
    interp.registerBuiltin("SQRT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "SQRT", line);
        double x = numArg(a, 0, "SQRT", line);
        if (x < 0) failAt(line, "SQRT: отрицательный аргумент");
        return Value::Float(std::sqrt(x));
    });
    interp.registerBuiltin("POW", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "POW", line);
        return Value::Float(std::pow(numArg(a, 0, "POW", line), numArg(a, 1, "POW", line)));
    });
    interp.registerBuiltin("ABS", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "ABS", line);
        if (a[0].type == Type::FLOAT) return Value::Float(std::fabs(a[0].f));
        return Value::Int(std::llabs((long long)numArg(a, 0, "ABS", line)));
    });
    interp.registerBuiltin("MIN", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "MIN", line);
        double x = numArg(a, 0, "MIN", line), y = numArg(a, 1, "MIN", line);
        bool bothInt = a[0].type != Type::FLOAT && a[1].type != Type::FLOAT;
        double m = x < y ? x : y;
        return bothInt ? Value::Int((long long)m) : Value::Float(m);
    });
    interp.registerBuiltin("MAX", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "MAX", line);
        double x = numArg(a, 0, "MAX", line), y = numArg(a, 1, "MAX", line);
        bool bothInt = a[0].type != Type::FLOAT && a[1].type != Type::FLOAT;
        double m = x > y ? x : y;
        return bothInt ? Value::Int((long long)m) : Value::Float(m);
    });
    interp.registerBuiltin("FLOOR", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "FLOOR", line);
        return Value::Int((long long)std::floor(numArg(a, 0, "FLOOR", line)));
    });
    interp.registerBuiltin("CEIL", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "CEIL", line);
        return Value::Int((long long)std::ceil(numArg(a, 0, "CEIL", line)));
    });
    interp.registerBuiltin("ROUND", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "ROUND", line);
        return Value::Int((long long)std::llround(numArg(a, 0, "ROUND", line)));
    });
    interp.registerBuiltin("RANDOM", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "RANDOM", line);
        long long lo = (long long)numArg(a, 0, "RANDOM", line);
        long long hi = (long long)numArg(a, 1, "RANDOM", line);
        if (lo > hi) std::swap(lo, hi);
        std::uniform_int_distribution<long long> dist(lo, hi);
        return Value::Int(dist(rng()));
    });
    interp.registerBuiltin("RANDOM_FLOAT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "RANDOM_FLOAT", line);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return Value::Float(dist(rng()));
    });

    // ---------- Строки ----------
    interp.registerBuiltin("LEN", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "LEN", line);
        if (a[0].type == Type::STR) return Value::Int((long long)a[0].s.size());
        if (a[0].type == Type::ARRAY) return Value::Int((long long)a[0].arr->size());
        failAt(line, "LEN(): аргумент должен быть строкой (STR_) или массивом (ARR_)");
    });
    interp.registerBuiltin("UPPER", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "UPPER", line);
        std::string s = strArg(a, 0, "UPPER", line);
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return Value::Str(s);
    });
    interp.registerBuiltin("LOWER", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "LOWER", line);
        std::string s = strArg(a, 0, "LOWER", line);
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return Value::Str(s);
    });
    interp.registerBuiltin("TRIM", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "TRIM", line);
        return Value::Str(trimStr(strArg(a, 0, "TRIM", line)));
    });
    interp.registerBuiltin("SUBSTR", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 3, "SUBSTR", line);
        const std::string& s = strArg(a, 0, "SUBSTR", line);
        long long start = (long long)numArg(a, 1, "SUBSTR", line);
        long long len = (long long)numArg(a, 2, "SUBSTR", line);
        if (start < 0) start = 0;
        if ((size_t)start >= s.size() || len <= 0) return Value::Str("");
        size_t take = std::min((size_t)len, s.size() - (size_t)start);
        return Value::Str(s.substr((size_t)start, take));
    });
    interp.registerBuiltin("FIND", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "FIND", line);
        const std::string& s = strArg(a, 0, "FIND", line);
        const std::string& sub = strArg(a, 1, "FIND", line);
        size_t p = s.find(sub);
        return Value::Int(p == std::string::npos ? -1 : (long long)p);
    });
    interp.registerBuiltin("REPLACE", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 3, "REPLACE", line);
        std::string s = strArg(a, 0, "REPLACE", line);
        const std::string& oldS = strArg(a, 1, "REPLACE", line);
        const std::string& newS = strArg(a, 2, "REPLACE", line);
        if (oldS.empty()) return Value::Str(s);
        std::string out;
        size_t pos = 0;
        while (true) {
            size_t p = s.find(oldS, pos);
            if (p == std::string::npos) { out += s.substr(pos); break; }
            out += s.substr(pos, p - pos) + newS;
            pos = p + oldS.size();
        }
        return Value::Str(out);
    });
    interp.registerBuiltin("SPLIT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "SPLIT", line);
        const std::string& s = strArg(a, 0, "SPLIT", line);
        const std::string& sep = strArg(a, 1, "SPLIT", line);
        std::vector<Value> parts;
        if (sep.empty()) {
            for (char c : s) parts.push_back(Value::Str(std::string(1, c)));
        } else {
            size_t pos = 0;
            while (true) {
                size_t p = s.find(sep, pos);
                if (p == std::string::npos) { parts.push_back(Value::Str(s.substr(pos))); break; }
                parts.push_back(Value::Str(s.substr(pos, p - pos)));
                pos = p + sep.size();
            }
        }
        return Value::Array(std::move(parts));
    });
    interp.registerBuiltin("JOIN", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "JOIN", line);
        auto arr = arrArg(a, 0, "JOIN", line);
        const std::string& sep = strArg(a, 1, "JOIN", line);
        std::string out;
        for (size_t i = 0; i < arr->size(); ++i) {
            if (i) out += sep;
            out += (*arr)[i].toString();
        }
        return Value::Str(out);
    });
    interp.registerBuiltin("CHR", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "CHR", line);
        long long code = (long long)numArg(a, 0, "CHR", line);
        return Value::Str(std::string(1, (char)code));
    });
    interp.registerBuiltin("ASC", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "ASC", line);
        const std::string& s = strArg(a, 0, "ASC", line);
        if (s.empty()) failAt(line, "ASC(): пустая строка");
        return Value::Int((unsigned char)s[0]);
    });

    // ---------- Преобразование типов ----------
    interp.registerBuiltin("TOINT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "TOINT", line);
        if (a[0].isNum()) return Value::Int((long long)a[0].num());
        if (a[0].type == Type::STR) {
            try { return Value::Int(std::stoll(trimStr(a[0].s))); }
            catch (...) { failAt(line, "TOINT(): не удаётся преобразовать \"" + a[0].s + "\" в число"); }
        }
        failAt(line, "TOINT(): нельзя преобразовать значение типа " + std::string(typeName(a[0].type)));
    });
    interp.registerBuiltin("TOFLOAT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "TOFLOAT", line);
        if (a[0].isNum()) return Value::Float(a[0].num());
        if (a[0].type == Type::STR) {
            try { return Value::Float(std::stod(trimStr(a[0].s))); }
            catch (...) { failAt(line, "TOFLOAT(): не удаётся преобразовать \"" + a[0].s + "\" в число"); }
        }
        failAt(line, "TOFLOAT(): нельзя преобразовать значение типа " + std::string(typeName(a[0].type)));
    });
    interp.registerBuiltin("TOSTR", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "TOSTR", line);
        return Value::Str(a[0].toString());
    });
    interp.registerBuiltin("TOBOOL", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "TOBOOL", line);
        return Value::Bool(a[0].truthy());
    });
    interp.registerBuiltin("TYPEOF", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "TYPEOF", line);
        return Value::Str(typeName(a[0].type));
    });

    // ---------- Массивы ----------
    interp.registerBuiltin("PUSH", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "PUSH", line);
        auto target = arrArg(a, 0, "PUSH", line);
        std::unordered_set<const void*> visited;
        if (containsArrayIdentity(a[1], target.get(), visited))
            failAt(line, "PUSH(): циклические ссылки между массивами запрещены");
        target->push_back(a[1]);
        return Value::Nil();
    });
    interp.registerBuiltin("POP", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "POP", line);
        auto arr = arrArg(a, 0, "POP", line);
        if (arr->empty()) failAt(line, "POP(): массив пуст");
        Value v = arr->back();
        arr->pop_back();
        return v;
    });
    interp.registerBuiltin("CONTAINS", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "CONTAINS", line);
        if (a[0].type == Type::ARRAY) {
            for (const auto& e : *a[0].arr)
                if (valuesEqual(e, a[1])) return Value::Bool(true);
            return Value::Bool(false);
        }
        if (a[0].type == Type::STR) {
            const std::string& sub = strArg(a, 1, "CONTAINS", line);
            return Value::Bool(a[0].s.find(sub) != std::string::npos);
        }
        failAt(line, "CONTAINS(): первый аргумент должен быть строкой или массивом");
    });
    interp.registerBuiltin("INDEX_OF", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "INDEX_OF", line);
        auto arr = arrArg(a, 0, "INDEX_OF", line);
        for (size_t i = 0; i < arr->size(); ++i)
            if (valuesEqual((*arr)[i], a[1])) return Value::Int((long long)i);
        return Value::Int(-1);
    });
    interp.registerBuiltin("REVERSE", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "REVERSE", line);
        auto arr = arrArg(a, 0, "REVERSE", line);
        std::reverse(arr->begin(), arr->end());
        return Value::Nil();
    });
    interp.registerBuiltin("SORT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "SORT", line);
        auto arr = arrArg(a, 0, "SORT", line);
        bool allStr = true, allNum = true;
        for (const auto& e : *arr) { if (e.type != Type::STR) allStr = false; if (!e.isNum()) allNum = false; }
        if (allStr) std::sort(arr->begin(), arr->end(), [](const Value& x, const Value& y) { return x.s < y.s; });
        else if (allNum) std::sort(arr->begin(), arr->end(), [](const Value& x, const Value& y) { return x.num() < y.num(); });
        else failAt(line, "SORT(): массив должен целиком состоять из чисел или целиком из строк");
        return Value::Nil();
    });
    interp.registerBuiltin("RANGE", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgcRange(a, 1, 2, "RANGE", line);
        long long from = 0, to;
        if (a.size() == 1) to = (long long)numArg(a, 0, "RANGE", line);
        else { from = (long long)numArg(a, 0, "RANGE", line); to = (long long)numArg(a, 1, "RANGE", line); }
        std::vector<Value> vals;
        for (long long i = from; i < to; ++i) vals.push_back(Value::Int(i));
        return Value::Array(std::move(vals));
    });

    // ---------- Математика (расширенная) ----------
    interp.registerBuiltin("SIN",   [](Interpreter&, std::vector<Value>& a, int line) -> Value { checkArgc(a, 1, "SIN", line);   return Value::Float(std::sin(numArg(a, 0, "SIN", line))); });
    interp.registerBuiltin("COS",   [](Interpreter&, std::vector<Value>& a, int line) -> Value { checkArgc(a, 1, "COS", line);   return Value::Float(std::cos(numArg(a, 0, "COS", line))); });
    interp.registerBuiltin("TAN",   [](Interpreter&, std::vector<Value>& a, int line) -> Value { checkArgc(a, 1, "TAN", line);   return Value::Float(std::tan(numArg(a, 0, "TAN", line))); });
    interp.registerBuiltin("ASIN",  [](Interpreter&, std::vector<Value>& a, int line) -> Value { checkArgc(a, 1, "ASIN", line);  return Value::Float(std::asin(numArg(a, 0, "ASIN", line))); });
    interp.registerBuiltin("ACOS",  [](Interpreter&, std::vector<Value>& a, int line) -> Value { checkArgc(a, 1, "ACOS", line);  return Value::Float(std::acos(numArg(a, 0, "ACOS", line))); });
    interp.registerBuiltin("ATAN",  [](Interpreter&, std::vector<Value>& a, int line) -> Value { checkArgc(a, 1, "ATAN", line);  return Value::Float(std::atan(numArg(a, 0, "ATAN", line))); });
    interp.registerBuiltin("ATAN2", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "ATAN2", line);
        return Value::Float(std::atan2(numArg(a, 0, "ATAN2", line), numArg(a, 1, "ATAN2", line)));
    });
    interp.registerBuiltin("HYPOT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "HYPOT", line);
        return Value::Float(std::hypot(numArg(a, 0, "HYPOT", line), numArg(a, 1, "HYPOT", line)));
    });
    interp.registerBuiltin("LOG", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "LOG", line);
        double x = numArg(a, 0, "LOG", line);
        if (x <= 0) failAt(line, "LOG: аргумент должен быть положительным");
        return Value::Float(std::log(x));
    });
    interp.registerBuiltin("LOG10", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "LOG10", line);
        double x = numArg(a, 0, "LOG10", line);
        if (x <= 0) failAt(line, "LOG10: аргумент должен быть положительным");
        return Value::Float(std::log10(x));
    });
    interp.registerBuiltin("EXP", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "EXP", line);
        return Value::Float(std::exp(numArg(a, 0, "EXP", line)));
    });
    interp.registerBuiltin("SIGN", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "SIGN", line);
        double x = numArg(a, 0, "SIGN", line);
        return Value::Int(x > 0 ? 1 : (x < 0 ? -1 : 0));
    });
    interp.registerBuiltin("CLAMP", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 3, "CLAMP", line);
        double x = numArg(a, 0, "CLAMP", line), lo = numArg(a, 1, "CLAMP", line), hi = numArg(a, 2, "CLAMP", line);
        bool allInt = a[0].type != Type::FLOAT && a[1].type != Type::FLOAT && a[2].type != Type::FLOAT;
        double r = x < lo ? lo : (x > hi ? hi : x);
        return allInt ? Value::Int((long long)r) : Value::Float(r);
    });
    interp.registerBuiltin("LERP", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 3, "LERP", line);
        double x = numArg(a, 0, "LERP", line), y = numArg(a, 1, "LERP", line), t = numArg(a, 2, "LERP", line);
        return Value::Float(x + (y - x) * t);
    });
    interp.registerBuiltin("GCD", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "GCD", line);
        long long x = std::llabs((long long)numArg(a, 0, "GCD", line));
        long long y = std::llabs((long long)numArg(a, 1, "GCD", line));
        while (y != 0) { long long t = y; y = x % y; x = t; }
        return Value::Int(x);
    });
    interp.registerBuiltin("LCM", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "LCM", line);
        long long x = std::llabs((long long)numArg(a, 0, "LCM", line));
        long long y = std::llabs((long long)numArg(a, 1, "LCM", line));
        if (x == 0 || y == 0) return Value::Int(0);
        long long gx = x, gy = y;
        while (gy != 0) { long long t = gy; gy = gx % gy; gx = t; }
        return Value::Int(x / gx * y);
    });
    interp.registerBuiltin("PI", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "PI", line);
        return Value::Float(3.14159265358979323846);
    });

    // ---------- Строки (расширенные) ----------
    interp.registerBuiltin("STARTS_WITH", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "STARTS_WITH", line);
        const std::string& s = strArg(a, 0, "STARTS_WITH", line);
        const std::string& p = strArg(a, 1, "STARTS_WITH", line);
        return Value::Bool(s.size() >= p.size() && s.compare(0, p.size(), p) == 0);
    });
    interp.registerBuiltin("ENDS_WITH", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "ENDS_WITH", line);
        const std::string& s = strArg(a, 0, "ENDS_WITH", line);
        const std::string& p = strArg(a, 1, "ENDS_WITH", line);
        return Value::Bool(s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0);
    });
    interp.registerBuiltin("REPEAT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "REPEAT", line);
        const std::string& s = strArg(a, 0, "REPEAT", line);
        long long n = (long long)numArg(a, 1, "REPEAT", line);
        std::string out;
        out.reserve(n > 0 ? s.size() * (size_t)n : 0);
        for (long long i = 0; i < n; ++i) out += s;
        return Value::Str(out);
    });
    interp.registerBuiltin("PAD_LEFT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 3, "PAD_LEFT", line);
        std::string s = strArg(a, 0, "PAD_LEFT", line);
        long long width = (long long)numArg(a, 1, "PAD_LEFT", line);
        const std::string& padStr = strArg(a, 2, "PAD_LEFT", line);
        char padCh = padStr.empty() ? ' ' : padStr[0];
        while ((long long)s.size() < width) s = padCh + s;
        return Value::Str(s);
    });
    interp.registerBuiltin("PAD_RIGHT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 3, "PAD_RIGHT", line);
        std::string s = strArg(a, 0, "PAD_RIGHT", line);
        long long width = (long long)numArg(a, 1, "PAD_RIGHT", line);
        const std::string& padStr = strArg(a, 2, "PAD_RIGHT", line);
        char padCh = padStr.empty() ? ' ' : padStr[0];
        while ((long long)s.size() < width) s += padCh;
        return Value::Str(s);
    });
    interp.registerBuiltin("STR_REVERSE", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "STR_REVERSE", line);
        const std::string& s = strArg(a, 0, "STR_REVERSE", line);
        return Value::Str(std::string(s.rbegin(), s.rend()));
    });
    interp.registerBuiltin("COUNT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "COUNT", line);
        const std::string& s = strArg(a, 0, "COUNT", line);
        const std::string& sub = strArg(a, 1, "COUNT", line);
        if (sub.empty()) return Value::Int(0);
        long long n = 0;
        size_t pos = 0;
        while ((pos = s.find(sub, pos)) != std::string::npos) { n++; pos += sub.size(); }
        return Value::Int(n);
    });

    // ---------- Массивы (расширенные) ----------
    interp.registerBuiltin("COPY", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "COPY", line);
        auto arr = arrArg(a, 0, "COPY", line);
        return Value::Array(*arr);
    });
    interp.registerBuiltin("SLICE", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 3, "SLICE", line);
        auto arr = arrArg(a, 0, "SLICE", line);
        long long start = (long long)numArg(a, 1, "SLICE", line);
        long long len = (long long)numArg(a, 2, "SLICE", line);
        if (start < 0) start = 0;
        if ((size_t)start >= arr->size() || len <= 0) return Value::Array();
        size_t take = std::min((size_t)len, arr->size() - (size_t)start);
        return Value::Array(std::vector<Value>(arr->begin() + start, arr->begin() + start + take));
    });
    interp.registerBuiltin("SUM", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "SUM", line);
        auto arr = arrArg(a, 0, "SUM", line);
        bool allInt = true;
        double total = 0;
        for (const auto& e : *arr) {
            if (!e.isNum()) failAt(line, "SUM(): все элементы массива должны быть числами");
            if (e.type == Type::FLOAT) allInt = false;
            total += e.num();
        }
        return allInt ? Value::Int((long long)total) : Value::Float(total);
    });
    interp.registerBuiltin("ARR_MIN", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "ARR_MIN", line);
        auto arr = arrArg(a, 0, "ARR_MIN", line);
        if (arr->empty()) failAt(line, "ARR_MIN(): массив пуст");
        Value best = (*arr)[0];
        if (!best.isNum()) failAt(line, "ARR_MIN(): все элементы массива должны быть числами");
        for (const auto& e : *arr) {
            if (!e.isNum()) failAt(line, "ARR_MIN(): все элементы массива должны быть числами");
            if (e.num() < best.num()) best = e;
        }
        return best;
    });
    interp.registerBuiltin("ARR_MAX", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "ARR_MAX", line);
        auto arr = arrArg(a, 0, "ARR_MAX", line);
        if (arr->empty()) failAt(line, "ARR_MAX(): массив пуст");
        Value best = (*arr)[0];
        if (!best.isNum()) failAt(line, "ARR_MAX(): все элементы массива должны быть числами");
        for (const auto& e : *arr) {
            if (!e.isNum()) failAt(line, "ARR_MAX(): все элементы массива должны быть числами");
            if (e.num() > best.num()) best = e;
        }
        return best;
    });
}

}  // namespace umbrly
