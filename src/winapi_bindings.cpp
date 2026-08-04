// Обёртки над WinAPI: консоль (цвет/курсор/заголовок), окна, мышь/клавиатура,
// буфер обмена, файлы, звук, процессы, системная информация.
#include "winapi_bindings.h"

#include <windows.h>

#include <conio.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <iostream>
#include <string>
#include <vector>

#include "errors.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "advapi32.lib")

namespace umbrly {

namespace {

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

std::wstring toWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

HANDLE conOut() { return GetStdHandle(STD_OUTPUT_HANDLE); }

}  // namespace

void registerWinApiBuiltins(Interpreter& interp) {
    // ---------- Диалоги ----------
    interp.registerBuiltin("MSGBOX", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgcRange(a, 1, 2, "MSGBOX", line);
        const std::string& text = strArg(a, 0, "MSGBOX", line);
        std::string title = a.size() >= 2 ? strArg(a, 1, "MSGBOX", line) : "Umbrly";
        MessageBoxW(nullptr, toWide(text).c_str(), toWide(title).c_str(), MB_OK | MB_ICONINFORMATION);
        return Value::Nil();
    });
    interp.registerBuiltin("CONFIRM", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgcRange(a, 1, 2, "CONFIRM", line);
        const std::string& text = strArg(a, 0, "CONFIRM", line);
        std::string title = a.size() >= 2 ? strArg(a, 1, "CONFIRM", line) : "Umbrly";
        int r = MessageBoxW(nullptr, toWide(text).c_str(), toWide(title).c_str(), MB_YESNO | MB_ICONQUESTION);
        return Value::Bool(r == IDYES);
    });

    // ---------- Звук и время ----------
    interp.registerBuiltin("BEEP", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "BEEP", line);
        Beep((DWORD)numArg(a, 0, "BEEP", line), (DWORD)numArg(a, 1, "BEEP", line));
        return Value::Nil();
    });
    interp.registerBuiltin("SLEEP", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "SLEEP", line);
        Sleep((DWORD)numArg(a, 0, "SLEEP", line));
        return Value::Nil();
    });
    interp.registerBuiltin("TICK_COUNT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "TICK_COUNT", line);
        return Value::Int((long long)GetTickCount64());
    });
    interp.registerBuiltin("TIME_NOW", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "TIME_NOW", line);
        SYSTEMTIME t;
        GetLocalTime(&t);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                      t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        return Value::Str(buf);
    });
    interp.registerBuiltin("PLAY_SOUND", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "PLAY_SOUND", line);
        const std::string& path = strArg(a, 0, "PLAY_SOUND", line);
        return Value::Bool(PlaySoundW(toWide(path).c_str(), nullptr, SND_FILENAME | SND_ASYNC) != FALSE);
    });

    // ---------- Консоль ----------
    interp.registerBuiltin("CLS", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "CLS", line);
        CONSOLE_SCREEN_BUFFER_INFO info;
        HANDLE h = conOut();
        if (!GetConsoleScreenBufferInfo(h, &info)) return Value::Nil();
        DWORD cells = (DWORD)info.dwSize.X * (DWORD)info.dwSize.Y;
        COORD origin = {0, 0};
        DWORD written;
        FillConsoleOutputCharacterW(h, L' ', cells, origin, &written);
        FillConsoleOutputAttribute(h, info.wAttributes, cells, origin, &written);
        SetConsoleCursorPosition(h, origin);
        return Value::Nil();
    });
    interp.registerBuiltin("COLOR", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "COLOR", line);
        int fg = (int)numArg(a, 0, "COLOR", line);
        int bg = (int)numArg(a, 1, "COLOR", line);
        if (fg < 0 || fg > 15 || bg < 0 || bg > 15)
            failAt(line, "COLOR(): цвета должны быть от 0 до 15");
        SetConsoleTextAttribute(conOut(), (WORD)(fg | (bg << 4)));
        return Value::Nil();
    });
    interp.registerBuiltin("COLOR_RESET", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "COLOR_RESET", line);
        SetConsoleTextAttribute(conOut(), 7);
        return Value::Nil();
    });
    interp.registerBuiltin("GOTOXY", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "GOTOXY", line);
        COORD c;
        c.X = (SHORT)numArg(a, 0, "GOTOXY", line);
        c.Y = (SHORT)numArg(a, 1, "GOTOXY", line);
        SetConsoleCursorPosition(conOut(), c);
        return Value::Nil();
    });
    interp.registerBuiltin("CONSOLE_TITLE", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "CONSOLE_TITLE", line);
        SetConsoleTitleW(toWide(strArg(a, 0, "CONSOLE_TITLE", line)).c_str());
        return Value::Nil();
    });

    // ---------- Клавиатура и мышь ----------
    interp.registerBuiltin("GETKEY", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "GETKEY", line);
        int c = _getch();
        if (c == 0 || c == 0xE0) c = _getch();  // клавиши-стрелки/функциональные — пропускаем префикс
        return Value::Str(std::string(1, (char)c));
    });
    interp.registerBuiltin("KEY_PRESSED", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "KEY_PRESSED", line);
        SHORT s = GetAsyncKeyState((int)numArg(a, 0, "KEY_PRESSED", line));
        return Value::Bool((s & 0x8000) != 0);
    });
    interp.registerBuiltin("MOUSE_X", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "MOUSE_X", line);
        POINT p; GetCursorPos(&p);
        return Value::Int(p.x);
    });
    interp.registerBuiltin("MOUSE_Y", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "MOUSE_Y", line);
        POINT p; GetCursorPos(&p);
        return Value::Int(p.y);
    });
    interp.registerBuiltin("SET_MOUSE_POS", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "SET_MOUSE_POS", line);
        SetCursorPos((int)numArg(a, 0, "SET_MOUSE_POS", line), (int)numArg(a, 1, "SET_MOUSE_POS", line));
        return Value::Nil();
    });
    interp.registerBuiltin("CLICK_MOUSE", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "CLICK_MOUSE", line);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        return Value::Nil();
    });

    // ---------- Буфер обмена ----------
    interp.registerBuiltin("CLIPBOARD_SET", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "CLIPBOARD_SET", line);
        std::wstring w = toWide(strArg(a, 0, "CLIPBOARD_SET", line));
        if (!OpenClipboard(nullptr)) failAt(line, "CLIPBOARD_SET(): не удалось открыть буфер обмена");
        EmptyClipboard();
        size_t bytes = (w.size() + 1) * sizeof(wchar_t);
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem) {
            void* p = GlobalLock(mem);
            memcpy(p, w.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
        }
        CloseClipboard();
        return Value::Nil();
    });
    interp.registerBuiltin("CLIPBOARD_GET", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "CLIPBOARD_GET", line);
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return Value::Str("");
        if (!OpenClipboard(nullptr)) failAt(line, "CLIPBOARD_GET(): не удалось открыть буфер обмена");
        std::string result;
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            wchar_t* p = (wchar_t*)GlobalLock(h);
            if (p) { result = toUtf8(std::wstring(p)); GlobalUnlock(h); }
        }
        CloseClipboard();
        return Value::Str(result);
    });

    // ---------- Файлы и папки ----------
    interp.registerBuiltin("FILE_READ", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "FILE_READ", line);
        std::wstring path = toWide(strArg(a, 0, "FILE_READ", line));
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) failAt(line, "FILE_READ(): не удалось открыть файл \"" + a[0].s + "\"");
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(h, &size) || size.QuadPart < 0) {
            CloseHandle(h);
            failAt(line, "FILE_READ(): не удалось определить размер файла");
        }
        static constexpr long long kMaxFileReadBytes = 256LL * 1024 * 1024;
        if (size.QuadPart > kMaxFileReadBytes) {
            CloseHandle(h);
            failAt(line, "FILE_READ(): файл превышает безопасный лимит 256 МБ");
        }
        std::string data((size_t)size.QuadPart, '\0');
        DWORD read = 0;
        if (size.QuadPart > 0 &&
            (!ReadFile(h, &data[0], (DWORD)size.QuadPart, &read, nullptr) ||
             read != (DWORD)size.QuadPart)) {
            CloseHandle(h);
            failAt(line, "FILE_READ(): ошибка при чтении файла");
        }
        CloseHandle(h);
        return Value::Str(data);
    });
    interp.registerBuiltin("FILE_WRITE", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "FILE_WRITE", line);
        std::wstring path = toWide(strArg(a, 0, "FILE_WRITE", line));
        const std::string& data = strArg(a, 1, "FILE_WRITE", line);
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) failAt(line, "FILE_WRITE(): не удалось создать файл \"" + a[0].s + "\"");
        DWORD written = 0;
        WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr);
        CloseHandle(h);
        return Value::Nil();
    });
    interp.registerBuiltin("FILE_APPEND", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "FILE_APPEND", line);
        std::wstring path = toWide(strArg(a, 0, "FILE_APPEND", line));
        const std::string& data = strArg(a, 1, "FILE_APPEND", line);
        HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, 0, nullptr, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) failAt(line, "FILE_APPEND(): не удалось открыть файл \"" + a[0].s + "\"");
        DWORD written = 0;
        WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr);
        CloseHandle(h);
        return Value::Nil();
    });
    interp.registerBuiltin("FILE_EXISTS", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "FILE_EXISTS", line);
        DWORD attr = GetFileAttributesW(toWide(strArg(a, 0, "FILE_EXISTS", line)).c_str());
        return Value::Bool(attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
    });
    interp.registerBuiltin("FILE_DELETE", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "FILE_DELETE", line);
        return Value::Bool(DeleteFileW(toWide(strArg(a, 0, "FILE_DELETE", line)).c_str()) != 0);
    });
    interp.registerBuiltin("DIR_EXISTS", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "DIR_EXISTS", line);
        DWORD attr = GetFileAttributesW(toWide(strArg(a, 0, "DIR_EXISTS", line)).c_str());
        return Value::Bool(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
    });
    interp.registerBuiltin("DIR_CREATE", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "DIR_CREATE", line);
        return Value::Bool(CreateDirectoryW(toWide(strArg(a, 0, "DIR_CREATE", line)).c_str(), nullptr) != 0);
    });

    // ---------- Окружение и система ----------
    interp.registerBuiltin("ENV_GET", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "ENV_GET", line);
        wchar_t buf[4096];
        DWORD n = GetEnvironmentVariableW(toWide(strArg(a, 0, "ENV_GET", line)).c_str(), buf, 4096);
        if (n == 0 || n > 4096) return Value::Str("");
        return Value::Str(toUtf8(std::wstring(buf, n)));
    });
    interp.registerBuiltin("ENV_SET", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 2, "ENV_SET", line);
        SetEnvironmentVariableW(toWide(strArg(a, 0, "ENV_SET", line)).c_str(),
                                 toWide(strArg(a, 1, "ENV_SET", line)).c_str());
        return Value::Nil();
    });
    interp.registerBuiltin("COMPUTER_NAME", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "COMPUTER_NAME", line);
        wchar_t buf[256]; DWORD n = 256;
        if (!GetComputerNameW(buf, &n)) return Value::Str("");
        return Value::Str(toUtf8(std::wstring(buf, n)));
    });
    interp.registerBuiltin("USER_NAME", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "USER_NAME", line);
        wchar_t buf[256]; DWORD n = 256;
        if (!GetUserNameW(buf, &n)) return Value::Str("");
        if (n > 0) n--;  // GetUserNameW включает завершающий ноль в счётчик
        return Value::Str(toUtf8(std::wstring(buf, n)));
    });
    interp.registerBuiltin("SCREEN_WIDTH", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "SCREEN_WIDTH", line);
        return Value::Int(GetSystemMetrics(SM_CXSCREEN));
    });
    interp.registerBuiltin("SCREEN_HEIGHT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "SCREEN_HEIGHT", line);
        return Value::Int(GetSystemMetrics(SM_CYSCREEN));
    });

    // ---------- Окна и процессы ----------
    interp.registerBuiltin("FIND_WINDOW", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "FIND_WINDOW", line);
        HWND h = FindWindowW(nullptr, toWide(strArg(a, 0, "FIND_WINDOW", line)).c_str());
        return Value::Int((long long)(intptr_t)h);
    });
    interp.registerBuiltin("ACTIVATE_WINDOW", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "ACTIVATE_WINDOW", line);
        HWND h = (HWND)(intptr_t)(long long)numArg(a, 0, "ACTIVATE_WINDOW", line);
        return Value::Bool(SetForegroundWindow(h) != 0);
    });
    interp.registerBuiltin("SHELL_OPEN", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "SHELL_OPEN", line);
        std::wstring path = toWide(strArg(a, 0, "SHELL_OPEN", line));
        auto r = (intptr_t)ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return Value::Bool(r > 32);
    });
    interp.registerBuiltin("RUN", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 1, "RUN", line);
        std::wstring cmd = toWide(strArg(a, 0, "RUN", line));
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(L'\0');
        STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
        PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
        if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
            failAt(line, "RUN(): не удалось запустить процесс \"" + a[0].s + "\"");
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return Value::Int((long long)code);
    });
    interp.registerBuiltin("EXIT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgcRange(a, 0, 1, "EXIT", line);
        int code = a.empty() ? 0 : (int)numArg(a, 0, "EXIT", line);
        std::cout.flush();
        ExitProcess((UINT)code);
    });

    // ---------- Производительность ----------
    interp.registerBuiltin("CLOCK", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "CLOCK", line);
        LARGE_INTEGER freq, counter;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&counter);
        return Value::Float((double)counter.QuadPart / (double)freq.QuadPart);
    });
    interp.registerBuiltin("CPU_COUNT", [](Interpreter&, std::vector<Value>& a, int line) -> Value {
        checkArgc(a, 0, "CPU_COUNT", line);
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return Value::Int((long long)si.dwNumberOfProcessors);
    });

}

}  // namespace umbrly
