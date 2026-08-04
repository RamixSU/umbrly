// CLI Umbrly: интерпретация, компиляция в C++/машинный код, отладочные режимы.
#include <windows.h>

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>

#include "builtins.h"
#include "compiler.h"
#include "errors.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "winapi_bindings.h"

using namespace umbrly;

namespace {

const char* kVersion = "Umbrly 0.2 \"Waffles\"";

void setupInterpreter(Interpreter& interp) {
    registerCoreBuiltins(interp);
    registerWinApiBuiltins(interp);
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) failAt(0, "не удалось открыть файл '" + path + "'");
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::wstring toWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

std::string toNarrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

std::wstring exeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf, n);
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}

std::string dirOf(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    return pos == std::string::npos ? std::string(".") : path.substr(0, pos);
}

bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

// Токенизирует+парсит один файл как есть, без разворачивания его LOAD-ов —
// используется и для входного файла, и рекурсивно для каждого загружаемого
// модуля внутри resolveLoads ниже.
Block parseFileRaw(const std::string& path, std::string* sourceOut = nullptr) {
    std::string source = readFile(path);
    if (sourceOut) *sourceOut = source;
    std::vector<Token> toks = tokenize(source);
    return parseProgram(toks);
}

// LOAD ИМЯ ищет ИМЯ.umb сначала рядом с файлом, в котором встретился LOAD
// (относительные импорты внутри проекта — библиотека может LOAD'ить свою
// соседнюю библиотеку), а если там нет — в lib/ рядом с самим umbrly.exe
// (общая для всех проектов "стандартная библиотека"). Порядок такой же, как
// в большинстве языков: локальное важнее системного.
std::string resolveModulePath(const std::string& name, const std::string& baseDir, int line) {
    std::string local = baseDir + "\\" + name + ".umb";
    if (fileExists(local)) return local;
    std::string lib = toNarrow(exeDir()) + "\\lib\\" + name + ".umb";
    if (fileExists(lib)) return lib;
    failAt(line, "модуль '" + name + "' не найден (искали " + local + " и " + lib + ")");
}

// Разворачивает LOAD и в дереве (block, нужно для выполнения/кодогенерации),
// и синхронно в тексте (source, нужно компилятору: он встраивает исходник в
// сгенерированный .cpp и заново парсит его в рантайме, чтобы восстановить
// таблицы функций/классов — без этой синхронизации функции из модулей были бы
// не видны уже собранному .exe). loaded — канонические пути уже подключённых
// модулей: второе LOAD того же модуля (в том числе по кругу, через цепочку
// других LOAD) тихо схлопывается в пустоту, как #pragma once.
void resolveLoads(Block& block, std::string& source, const std::string& baseDir, std::set<std::string>& loaded) {
    std::vector<std::string> lines;
    {
        std::istringstream iss(source);
        std::string ln;
        while (std::getline(iss, ln)) lines.push_back(ln);
    }

    for (size_t i = 0; i < block.size();) {
        auto* load = dynamic_cast<LoadStmt*>(block[i].get());
        if (!load) { i++; continue; }

        std::string path = resolveModulePath(load->moduleName, baseDir, load->line);
        std::string canon = path;
        for (char& c : canon) if (c == '/') c = '\\';

        int lineIdx = load->line - 1;
        std::string replacementText;

        if (!loaded.count(canon)) {
            loaded.insert(canon);
            std::string modSource;
            Block modBlock = parseFileRaw(path, &modSource);
            resolveLoads(modBlock, modSource, dirOf(path), loaded);

            block.erase(block.begin() + (long)i);
            block.insert(block.begin() + (long)i,
                         std::make_move_iterator(modBlock.begin()),
                         std::make_move_iterator(modBlock.end()));
            i += modBlock.size();
            replacementText = modSource;
        } else {
            block.erase(block.begin() + (long)i);
            // replacementText остаётся пустым: модуль уже подключён раньше.
        }

        if (lineIdx >= 0 && lineIdx < (int)lines.size()) lines[(size_t)lineIdx] = replacementText;
    }

    std::string out;
    for (auto& ln : lines) { out += ln; out += "\n"; }
    source = out;
}

Block parseFile(const std::string& path, std::string* sourceOut = nullptr) {
    std::string source;
    Block program = parseFileRaw(path, &source);

    std::set<std::string> loaded;
    std::string canonSelf = path;
    for (char& c : canonSelf) if (c == '/') c = '\\';
    loaded.insert(canonSelf);

    resolveLoads(program, source, dirOf(path), loaded);
    if (sourceOut) *sourceOut = source;
    return program;
}

// ---------- AST-дамп для -ir ----------

void dumpExpr(const Expr* e, int depth);
void dumpBlock(const Block& b, int depth);

void pad(int depth) { std::cout << std::string((size_t)depth * 2, ' '); }

void dumpExpr(const Expr* e, int depth) {
    pad(depth);
    if (auto* v = dynamic_cast<const IntLitExpr*>(e)) { std::cout << "Int(" << v->value << ")\n"; return; }
    if (auto* v = dynamic_cast<const FloatLitExpr*>(e)) { std::cout << "Float(" << v->value << ")\n"; return; }
    if (auto* v = dynamic_cast<const BoolLitExpr*>(e)) { std::cout << "Bool(" << (v->value ? "TRUE" : "FALSE") << ")\n"; return; }
    if (dynamic_cast<const NilLitExpr*>(e)) { std::cout << "Nil\n"; return; }
    if (auto* v = dynamic_cast<const StringLitExpr*>(e)) {
        std::cout << "String(" << v->segments.size() << " сегм.)\n";
        for (const auto& seg : v->segments) {
            if (seg.expr) dumpExpr(seg.expr.get(), depth + 1);
            else { pad(depth + 1); std::cout << "лит: \"" << seg.literal << "\"\n"; }
        }
        return;
    }
    if (auto* v = dynamic_cast<const NameExpr*>(e)) { std::cout << "Name(" << v->name << ")\n"; return; }
    if (auto* v = dynamic_cast<const ArrayLitExpr*>(e)) {
        std::cout << "ArrayLit[" << v->items.size() << "]\n";
        for (const auto& it : v->items) dumpExpr(it.get(), depth + 1);
        return;
    }
    if (auto* v = dynamic_cast<const IndexExpr*>(e)) {
        std::cout << "Index\n"; dumpExpr(v->base.get(), depth + 1); dumpExpr(v->index.get(), depth + 1); return;
    }
    if (auto* v = dynamic_cast<const CallExpr*>(e)) {
        std::cout << "Call(" << v->name << ")\n";
        for (const auto& a : v->args) dumpExpr(a.get(), depth + 1);
        return;
    }
    if (auto* v = dynamic_cast<const UnaryExpr*>(e)) { std::cout << "Unary(" << v->op << ")\n"; dumpExpr(v->operand.get(), depth + 1); return; }
    if (auto* v = dynamic_cast<const BinaryExpr*>(e)) {
        std::cout << "Binary(" << v->op << ")\n"; dumpExpr(v->lhs.get(), depth + 1); dumpExpr(v->rhs.get(), depth + 1); return;
    }
    if (auto* v = dynamic_cast<const MemberExpr*>(e)) { std::cout << "Member(." << v->name << ")\n"; dumpExpr(v->base.get(), depth + 1); return; }
    if (auto* v = dynamic_cast<const MethodCallExpr*>(e)) {
        std::cout << "MethodCall(." << v->method << ")\n"; dumpExpr(v->base.get(), depth + 1);
        for (const auto& a : v->args) dumpExpr(a.get(), depth + 1);
        return;
    }
    std::cout << "?\n";
}

void dumpStmt(const Stmt* s, int depth) {
    pad(depth);
    std::cout << "L" << s->line << ": ";
    if (auto* v = dynamic_cast<const PrintStmt*>(s)) {
        std::cout << "PRINT\n"; if (v->value) dumpExpr(v->value.get(), depth + 1); return;
    }
    if (auto* v = dynamic_cast<const InputPauseStmt*>(s)) {
        std::cout << "INPUT (пауза)\n"; if (v->prompt) dumpExpr(v->prompt.get(), depth + 1); return;
    }
    if (auto* v = dynamic_cast<const AssignStmt*>(s)) {
        std::cout << "Assign(" << v->name << (v->index ? "[..]" : (v->memberBase ? ("." + v->memberName) : "")) << ")\n";
        if (v->memberBase) dumpExpr(v->memberBase.get(), depth + 1);
        if (v->index) dumpExpr(v->index.get(), depth + 1);
        if (v->value) dumpExpr(v->value.get(), depth + 1);
        return;
    }
    if (auto* v = dynamic_cast<const ExprStmt*>(s)) { std::cout << "ExprStmt\n"; dumpExpr(v->expr.get(), depth + 1); return; }
    if (auto* v = dynamic_cast<const IfStmt*>(s)) {
        std::cout << "If\n"; dumpExpr(v->cond.get(), depth + 1);
        pad(depth + 1); std::cout << "then:\n"; dumpBlock(v->thenBlock, depth + 2);
        if (!v->elseBlock.empty()) { pad(depth + 1); std::cout << "else:\n"; dumpBlock(v->elseBlock, depth + 2); }
        return;
    }
    if (auto* v = dynamic_cast<const WhileStmt*>(s)) {
        std::cout << "While\n"; dumpExpr(v->cond.get(), depth + 1); dumpBlock(v->body, depth + 1); return;
    }
    if (auto* v = dynamic_cast<const ForStmt*>(s)) {
        std::cout << "For(" << v->varName << ")\n";
        dumpExpr(v->fromExpr.get(), depth + 1); dumpExpr(v->toExpr.get(), depth + 1);
        if (v->stepExpr) dumpExpr(v->stepExpr.get(), depth + 1);
        dumpBlock(v->body, depth + 1);
        return;
    }
    if (auto* v = dynamic_cast<const FuncDefStmt*>(s)) {
        std::cout << "Func(" << v->name << ", " << v->params.size() << " парам.)\n"; dumpBlock(*v->body, depth + 1); return;
    }
    if (auto* v = dynamic_cast<const ClassDefStmt*>(s)) {
        std::cout << "Class(" << v->name << ", " << v->fields.size() << " полей, " << v->methods.size() << " методов)\n";
        return;
    }
    if (auto* v = dynamic_cast<const LoadStmt*>(s)) {
        std::cout << "Load(" << v->moduleName << ")  # уже развёрнут при парсинге, если верхнего уровня\n";
        return;
    }
    if (auto* v = dynamic_cast<const ReturnStmt*>(s)) {
        std::cout << "Return\n"; if (v->value) dumpExpr(v->value.get(), depth + 1); return;
    }
    if (dynamic_cast<const BreakStmt*>(s)) { std::cout << "Break\n"; return; }
    if (dynamic_cast<const ContinueStmt*>(s)) { std::cout << "Continue\n"; return; }
    if (auto* v = dynamic_cast<const TryStmt*>(s)) {
        std::cout << "Try\n"; dumpBlock(v->tryBlock, depth + 1);
        pad(depth + 1); std::cout << "catch(" << v->errVarName << "):\n"; dumpBlock(v->catchBlock, depth + 2);
        return;
    }
    std::cout << "?\n";
}

void dumpBlock(const Block& b, int depth) {
    for (const auto& s : b) dumpStmt(s.get(), depth);
}

// ---------- Компиляция в машинный код ----------
// (toWide/exeDir теперь объявлены выше, рядом с parseFile — resolveModulePath
// тоже использует exeDir() для поиска lib/ рядом с umbrly.exe.)

int runProcessAndWait(std::wstring cmdLine) {
    STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(L'\0');
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

std::wstring quoteW(const std::wstring& s) { return L"\"" + s + L"\""; }

// cc1plus.exe (сам компилятор, которого запускает g++.exe) ищет свои DLL
// (libgmp/libmpc/libisl/libgcc_s_seh...) рядом в mingw64\bin — если этой папки нет в
// PATH процесса, cc1plus падает с "Системная ошибка", даже если g++.exe сам нашёлся.
// build.bat учитывал это, а вызов g++ из самого umbrly.exe (CreateProcessW ниже
// наследует PATH процесса) — нет. Дополняем PATH самого umbrly.exe перед вызовом,
// тогда порождённые g++/cc1plus унаследуют его.
void ensureMingwDllsOnPath() {
    const wchar_t* candidate = L"C:\\msys64\\mingw64\\bin";
    if (GetFileAttributesW(candidate) == INVALID_FILE_ATTRIBUTES) return;
    wchar_t buf[32768];
    DWORD n = GetEnvironmentVariableW(L"PATH", buf, 32768);
    std::wstring path = (n > 0 && n < 32768) ? std::wstring(buf, n) : std::wstring();
    if (path.find(candidate) != std::wstring::npos) return;  // уже есть — не дублируем
    std::wstring newPath = std::wstring(candidate) + L";" + path;
    SetEnvironmentVariableW(L"PATH", newPath.c_str());
}

// Собирает сгенерированный .cpp вместе с рантаймом интерпретатора в один .exe.
// Ищет g++ рядом с PATH, а если не найден — по типичному пути установки MSYS2.
bool buildNativeExe(const std::wstring& generatedCppPath, const std::wstring& outputExePath, std::string& errOut) {
    ensureMingwDllsOnPath();
    std::wstring dir = exeDir();
    std::wstring includeDir = dir + L"\\include";
    std::wstring srcDir = dir + L"\\src";

    auto tryCompiler = [&](const std::wstring& gpp) -> int {
        std::wstring cmd = quoteW(gpp) +
            L" -std=c++17 -O2 -I" + quoteW(includeDir) +
            L" " + quoteW(generatedCppPath) +
            L" " + quoteW(srcDir + L"\\lexer.cpp") +
            L" " + quoteW(srcDir + L"\\parser.cpp") +
            L" " + quoteW(srcDir + L"\\ast.cpp") +
            L" " + quoteW(srcDir + L"\\interpreter.cpp") +
            L" " + quoteW(srcDir + L"\\builtins.cpp") +
            L" " + quoteW(srcDir + L"\\winapi_bindings.cpp") +
            // -static: скомпилированная программа должна быть автономным .exe, не зависящим
            // от libgcc_s_seh-1.dll/libstdc++-6.dll/libwinpthread-1.dll из MinGW на машине
            // пользователя (без этого запуск на чужом ПК без MSYS2 падает с "не обнаружена DLL").
            L" -static -s -o " + quoteW(outputExePath) +
            L" -luser32 -lshell32 -lwinmm -ladvapi32";
        return runProcessAndWait(cmd);
    };

    int code = tryCompiler(L"g++");
    if (code == -1) code = tryCompiler(L"C:\\msys64\\mingw64\\bin\\g++.exe");
    if (code == -1) {
        errOut = "g++ не найден. Установите MinGW-w64 (например, через MSYS2) и добавьте его в PATH.";
        return false;
    }
    if (code != 0) {
        errOut = "g++ вернул код ошибки " + std::to_string(code) + " при сборке " +
                  std::string(generatedCppPath.begin(), generatedCppPath.end());
        return false;
    }
    return true;
}

std::string withExt(const std::string& path, const std::string& ext) {
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("\\/");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) return path.substr(0, dot) + ext;
    return path + ext;
}

// ---------- Режимы CLI ----------

void repl(Interpreter& interp) {
    std::cout << kVersion << " -- интерактивный режим. Введите EXIT для выхода.\n";
    std::string buffer;
    int depth = 0;
    std::string line;
    for (;;) {
        std::cout << (depth > 0 ? "... " : ">>> ") << std::flush;
        if (!std::getline(std::cin, line)) break;

        std::string trimmed = line;
        size_t b = trimmed.find_first_not_of(" \t\r");
        if (depth == 0 && b != std::string::npos) {
            std::string upper = trimmed.substr(b);
            if (upper == "EXIT" || upper == "QUIT") break;
        }

        try {
            std::vector<Token> lineToks = tokenize(line);
            if (!lineToks.empty() && lineToks[0].type == TokType::Ident) {
                const std::string& kw = lineToks[0].text;
                if (kw == "IF" || kw == "WHILE" || kw == "FOR" || kw == "FUNC" || kw == "TRY" || kw == "CLASS") depth++;
                else if (kw == "END") depth--;
            }
        } catch (const UmbrlyError&) {
        }

        buffer += line + "\n";

        if (depth <= 0) {
            try {
                std::vector<Token> toks = tokenize(buffer);
                Block program = parseProgram(toks);
                interp.run(program);
            } catch (const UmbrlyError& e) {
                std::cout << "Ошибка " << e.what() << "\n";
            }
            buffer.clear();
            depth = 0;
        }
    }
}

int cmdRun(const std::string& file, bool trace, uint64_t maxInstructions = 0, uint64_t timeoutMs = 0) {
    Interpreter interp;
    setupInterpreter(interp);
    interp.setExecutionLimits(maxInstructions, timeoutMs);
    if (trace) interp.setTraceHook([](int line) { std::cerr << "[trace] строка " << line << "\n"; });
    try {
        Block program = parseFile(file);
        interp.run(program);
    } catch (const UmbrlyError& e) {
        std::cerr << "Ошибка " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int cmdCheck(const std::string& file) {
    try {
        parseFile(file);
    } catch (const UmbrlyError& e) {
        std::cerr << "Ошибка " << e.what() << "\n";
        return 1;
    }
    std::cout << "OK: " << file << " — лексер и парсер не нашли ошибок.\n";
    return 0;
}

int cmdIr(const std::string& file) {
    try {
        Block program = parseFile(file);
        dumpBlock(program, 0);
    } catch (const UmbrlyError& e) {
        std::cerr << "Ошибка " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int cmdCompileCpp(const std::string& file, const std::string& outCpp) {
    try {
        std::string source;
        Block program = parseFile(file, &source);
        std::string cpp = generateCpp(program, source);
        std::ofstream out(outCpp, std::ios::binary);
        if (!out) { std::cerr << "Umbrly: не удалось создать файл '" << outCpp << "'\n"; return 1; }
        out << cpp;
        std::cout << "Готово: " << outCpp << "\n";
    } catch (const UmbrlyError& e) {
        std::cerr << "Ошибка " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int cmdBuild(const std::string& file, const std::string& outExe) {
    std::string tmpCpp = withExt(outExe, ".generated.cpp");
    if (cmdCompileCpp(file, tmpCpp) != 0) return 1;
    std::string err;
    if (!buildNativeExe(toWide(tmpCpp), toWide(outExe), err)) {
        std::cerr << "Ошибка сборки: " << err << "\n";
        return 1;
    }
    std::cout << "Готово: " << outExe << "\n";
    return 0;
}

void printHelp() {
    std::cout <<
        kVersion << " -- интерпретируемый и компилируемый язык для Windows.\n\n"
        "Использование:\n"
        "  umbrly                              интерактивный REPL\n"
        "  umbrly file.umb                     запуск через интерпретатор (как -r)\n"
        "  umbrly -r file.umb   | run file.umb          запуск через интерпретатор\n"
        "  umbrly -b file.umb [out.exe] | build file.umb [out.exe] | compile file.umb [out.exe]\n"
        "                                       компиляция в нативный .exe (нужен g++/MinGW-w64)\n"
        "  umbrly -c file.umb [out.cpp]         транспиляция в C++ без вызова g++ (отладочный вывод)\n"
        "  umbrly -t file.umb                   проверка синтаксиса (лексер+парсер), без выполнения\n"
        "  umbrly -ir file.umb                  вывод AST в текстовом виде\n"
        "  umbrly -d file.umb | -trace file.umb запуск с построчной трассировкой выполнения\n"
        "  umbrly -safe file.umb                безопасный запуск: 5 млн инструкций, 3000 мс\n"
        "  umbrly -limit STEPS MS file.umb      запуск с пользовательскими лимитами\n"
        "  umbrly -v | --version                версия\n"
        "  umbrly -h | --help                   эта справка\n"
        "  umbrly --info                        информация о языке и бэкенде\n\n"
        "LOAD ИМЯ  -- импорт модуля/библиотеки внутри .umb-файла (без двоеточия).\n"
        "  Ищет ИМЯ.umb сначала рядом со скриптом, затем в lib/ рядом с umbrly.exe.\n\n"
        "Примечание: -p/pack (упаковка проекта) пока не реализованы — отдельная задача.\n";
}

void printInfo() {
    std::cout <<
        kVersion << "\n"
        "Интерпретатор: дерево-обходящий (лексер -> парсер -> AST -> Interpreter), C++17.\n"
        "Компилятор: транспиляция AST в C++ (управляющий поток нативно; операторы, builtin-\n"
        "  функции и методы классов выполняются через тот же Interpreter как раннтайм),\n"
        "  затем сборка через внешний g++ (MinGW-w64).\n"
        "102 встроенные функции, из них 40 — обёртки над WinAPI.\n"
        "Классы: CLASS/SELF, без наследования. Методы всегда интерпретируются (даже у -b/-c).\n"
        "Модули: LOAD ИМЯ ищет ИМЯ.umb рядом со скриптом, затем в lib/ рядом с umbrly.exe;\n"
        "  разворачивается на этапе загрузки файла, до выполнения и до кодогенерации.\n";
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty()) {
        Interpreter interp;
        setupInterpreter(interp);
        repl(interp);
        return 0;
    }

    const std::string& a0 = args[0];

    if (a0 == "-v" || a0 == "--version") { std::cout << kVersion << "\n"; return 0; }
    if (a0 == "-h" || a0 == "--help") { printHelp(); return 0; }
    if (a0 == "--info") { printInfo(); return 0; }

    if (a0 == "-p" || a0 == "pack") {
        std::cerr << "Umbrly: упаковка проекта (-p/pack) пока не реализована.\n";
        return 1;
    }

    if (a0 == "-r" || a0 == "run") {
        if (args.size() < 2) { std::cerr << "Использование: umbrly -r file.umb\n"; return 1; }
        return cmdRun(args[1], false);
    }
    if (a0 == "-d") {
        if (args.size() < 2) { std::cerr << "Использование: umbrly -d file.umb\n"; return 1; }
        return cmdRun(args[1], true);
    }
    if (a0 == "-trace") {
        if (args.size() < 2) { std::cerr << "Использование: umbrly -trace file.umb\n"; return 1; }
        return cmdRun(args[1], true);
    }
    if (a0 == "-safe") {
        if (args.size() < 2) { std::cerr << "Usage: umbrly -safe file.umb\n"; return 1; }
        return cmdRun(args[1], false, 5000000, 3000);
    }
    if (a0 == "-limit") {
        if (args.size() < 4) { std::cerr << "Usage: umbrly -limit STEPS MS file.umb\n"; return 1; }
        try {
            return cmdRun(args[3], false, std::stoull(args[1]), std::stoull(args[2]));
        } catch (...) { std::cerr << "STEPS and MS must be non-negative integers\n"; return 1; }
    }
    if (a0 == "-t") {
        if (args.size() < 2) { std::cerr << "Использование: umbrly -t file.umb\n"; return 1; }
        return cmdCheck(args[1]);
    }
    if (a0 == "-ir") {
        if (args.size() < 2) { std::cerr << "Использование: umbrly -ir file.umb\n"; return 1; }
        return cmdIr(args[1]);
    }
    if (a0 == "-c") {
        if (args.size() < 2) { std::cerr << "Использование: umbrly -c file.umb [out.cpp]\n"; return 1; }
        std::string out = args.size() >= 3 ? args[2] : withExt(args[1], ".cpp");
        return cmdCompileCpp(args[1], out);
    }
    if (a0 == "-b" || a0 == "build" || a0 == "compile") {
        if (args.size() < 2) { std::cerr << "Использование: umbrly -b file.umb [out.exe]\n"; return 1; }
        std::string out = args.size() >= 3 ? args[2] : withExt(args[1], ".exe");
        return cmdBuild(args[1], out);
    }

    // Обратная совместимость: umbrly file.umb == umbrly -r file.umb.
    if (a0.size() > 4 && a0.substr(a0.size() - 4) == ".umb") {
        return cmdRun(a0, false);
    }

    std::cerr << "Umbrly: неизвестная команда '" << a0 << "'. См. umbrly -h\n";
    return 1;
}
