// Динамическое значение Umbrly: INT, FLOAT, STR, BOOL, ARRAY, OBJECT, NIL.
#pragma once

#include <charconv>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "errors.h"

namespace umbrly {

enum class Type { INT, FLOAT, STR, BOOL, ARRAY, OBJECT, NIL };

// Имя типа для сообщений об ошибках и функции TYPEOF().
inline const char* typeName(Type t) {
    switch (t) {
        case Type::INT:    return "INT";
        case Type::FLOAT:  return "FLOAT";
        case Type::STR:    return "STR";
        case Type::BOOL:   return "BOOL";
        case Type::ARRAY:  return "ARR";
        case Type::OBJECT: return "OBJ";
        default:           return "NIL";
    }
}

struct Value;

// Данные экземпляра класса: имя класса (для поиска методов) + поля по имени.
// Живут в shared_ptr — присваивание OBJ_B = OBJ_A даёт две ссылки на один и тот
// же экземпляр (в отличие от массивов, которые при присваивании копируются).
struct ObjectData {
    std::string className;
    std::unordered_map<std::string, Value> fields;
};

struct Value {
    Type type = Type::NIL;
    long long i = 0;
    double f = 0.0;
    std::string s;
    std::shared_ptr<std::vector<Value>> arr;  // используется только при type == ARRAY
    std::shared_ptr<ObjectData> obj;          // используется только при type == OBJECT

    static Value Int(long long v)   { Value x; x.type = Type::INT;   x.i = v; return x; }
    static Value Float(double v)    { Value x; x.type = Type::FLOAT; x.f = v; return x; }
    static Value Str(std::string v) { Value x; x.type = Type::STR;   x.s = std::move(v); return x; }
    static Value Bool(bool v)       { Value x; x.type = Type::BOOL;  x.i = v ? 1 : 0; return x; }
    static Value Nil()              { return Value{}; }
    static Value Array(std::vector<Value> v = {}) {
        Value x;
        x.type = Type::ARRAY;
        x.arr = std::make_shared<std::vector<Value>>(std::move(v));
        return x;
    }
    static Value Object(std::string className) {
        Value x;
        x.type = Type::OBJECT;
        x.obj = std::make_shared<ObjectData>();
        x.obj->className = std::move(className);
        return x;
    }

    bool isNum()   const { return type == Type::INT || type == Type::FLOAT || type == Type::BOOL; }
    double num()   const {
        if (type == Type::FLOAT) return f;
        return (double)i;  // INT и BOOL хранятся в i
    }

    bool truthy() const {
        switch (type) {
            case Type::STR:    return !s.empty();
            case Type::ARRAY:  return arr && !arr->empty();
            case Type::OBJECT: return true;  // ссылка на объект всегда истинна
            case Type::NIL:    return false;
            default:           return num() != 0.0;
        }
    }

private:
    std::string toStringImpl(std::unordered_set<const void*>& activeArrays, size_t depth) const {
        static constexpr size_t kMaxStringifyDepth = 256;
        if (depth > kMaxStringifyDepth) return "<max-depth>";
        switch (type) {
            case Type::INT: {
                char buf[32];
                auto result = std::to_chars(buf, buf + sizeof(buf), i);
                return std::string(buf, result.ptr);
            }
            case Type::BOOL:  return i ? "TRUE" : "FALSE";
            case Type::FLOAT: {
                // snprintf вместо ostringstream: без локали, без аллокации потокового буфера —
                // toString() вызывается на каждом PRINT/конкатенации/подстановке.
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.6g", f);
                return std::string(buf);
            }
            case Type::STR:   return s;
            case Type::NIL:   return "NIL";
            case Type::OBJECT: return "<" + (obj ? obj->className : std::string("?")) + ">";
            case Type::ARRAY: {
                if (!arr) return "[]";
                const void* identity = arr.get();
                if (!activeArrays.insert(identity).second) return "<cycle>";
                std::string out = "[";
                for (size_t k = 0; k < arr->size(); ++k) {
                    if (k) out += ", ";
                    const Value& e = (*arr)[k];
                    out += (e.type == Type::STR) ? ("\"" + e.s + "\"")
                                                : e.toStringImpl(activeArrays, depth + 1);
                }
                out += "]";
                activeArrays.erase(identity);
                return out;
            }
        }
        return "";
    }

public:
    std::string toString() const {
        std::unordered_set<const void*> activeArrays;
        return toStringImpl(activeArrays, 0);
    }
};

}  // namespace umbrly
