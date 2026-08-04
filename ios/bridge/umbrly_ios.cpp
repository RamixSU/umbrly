// iOS translation of the Windows builtins.
//
// The Windows layer in src/winapi_bindings.cpp talks straight to Win32. iOS has
// no equivalent for a good part of it, so each builtin falls into one of three
// groups:
//
//   1. Portable      — pure C++, identical behaviour (time, sleep, files).
//   2. Host-backed   — needs the UI or the OS, routed through UmbrlyHost
//                      (console, alerts, clipboard, sound, screen metrics).
//   3. Unavailable   — cannot exist inside an iOS sandbox. These are still
//                      registered, and raise a catchable Umbrly error saying so
//                      rather than silently returning a fake value. A script
//                      that calls RUN() should fail loudly, not pretend.
//
// Group 3 is the honest part of the translation: process spawning, window
// manipulation and synthetic mouse input are not restricted by choice, they are
// absent from the platform.

#include "umbrly_ios.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "builtins.h"
#include "errors.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "value.h"

namespace {

std::atomic<bool> g_stopRequested{false};

const UmbrlyHost* g_host = nullptr;
std::filesystem::path g_sandbox;

// ---------------------------------------------------------------- argument helpers

void checkArgc(std::vector<umbrly::Value>& a, size_t n, const char* fn, int line) {
    if (a.size() != n) {
        umbrly::failAt(line, std::string(fn) + " ожидает " + std::to_string(n) +
                                 " аргумент(ов), получено " + std::to_string(a.size()));
    }
}

void checkArgcRange(std::vector<umbrly::Value>& a, size_t lo, size_t hi, const char* fn, int line) {
    if (a.size() < lo || a.size() > hi) {
        umbrly::failAt(line, std::string(fn) + " ожидает от " + std::to_string(lo) + " до " +
                                 std::to_string(hi) + " аргументов, получено " +
                                 std::to_string(a.size()));
    }
}

const std::string& strArg(const std::vector<umbrly::Value>& a, size_t i, const char* fn, int line) {
    if (i >= a.size() || a[i].type != umbrly::Type::STR) {
        umbrly::failAt(line, std::string(fn) + ": аргумент " + std::to_string(i + 1) +
                                 " должен быть STR");
    }
    return a[i].s;
}

long long intArg(const std::vector<umbrly::Value>& a, size_t i, const char* fn, int line) {
    if (i >= a.size() || !a[i].isNum()) {
        umbrly::failAt(line, std::string(fn) + ": аргумент " + std::to_string(i + 1) +
                                 " должен быть числом");
    }
    return static_cast<long long>(a[i].num());
}

/// Registers a builtin that exists on Windows but has no iOS counterpart.
/// Calling it is a normal catchable Umbrly error, so TRY/CATCH still works.
void registerUnavailable(umbrly::Interpreter& interp, const std::string& name,
                         const std::string& reason) {
    interp.registerBuiltin(name, [name, reason](umbrly::Interpreter&, std::vector<umbrly::Value>&,
                                                int line) -> umbrly::Value {
        umbrly::failAt(line, name + "() недоступна на iOS: " + reason);
    });
}

// ---------------------------------------------------------------- sandbox paths

/// Resolves a script-supplied path inside the app sandbox.
///
/// iOS gives the app one writable directory. Anything that climbs out of it via
/// ".." or an absolute path is refused here rather than failing later with a
/// confusing permission error from the OS.
std::filesystem::path resolvePath(const std::string& raw, const char* fn, int line) {
    if (raw.empty()) {
        umbrly::failAt(line, std::string(fn) + ": пустой путь");
    }

    std::filesystem::path candidate(raw);
    std::filesystem::path full = candidate.is_absolute() ? candidate : (g_sandbox / candidate);

    // lexically_normal resolves ".." textually, which is what we want here: the
    // file need not exist yet, and we must not follow symlinks out of the box.
    full = full.lexically_normal();

    const std::string sandbox = g_sandbox.lexically_normal().string();
    const std::string target = full.string();
    if (target.size() < sandbox.size() || target.compare(0, sandbox.size(), sandbox) != 0) {
        umbrly::failAt(line, std::string(fn) +
                                 ": доступ разрешён только внутри папки документов приложения");
    }
    return full;
}

// ---------------------------------------------------------------- stream plumbing

/// Sends everything the interpreter writes to std::cout on to the host.
///
/// PRINT: goes through std::cout, so this is what makes output appear live in
/// the console view instead of arriving in one lump when the script ends.
class HostOutBuf : public std::streambuf {
protected:
    int overflow(int ch) override {
        if (ch == traits_type::eof()) return traits_type::not_eof(ch);
        pending_.push_back(static_cast<char>(ch));
        if (ch == '\n' || pending_.size() >= 512) flushPending();
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        pending_.append(s, static_cast<size_t>(n));
        if (pending_.find('\n') != std::string::npos || pending_.size() >= 512) flushPending();
        return n;
    }

    int sync() override {
        flushPending();
        return 0;
    }

private:
    void flushPending() {
        if (pending_.empty() || !g_host || !g_host->write) return;
        g_host->write(g_host->ctx, pending_.c_str());
        pending_.clear();
    }

    std::string pending_;
};

/// Feeds std::cin from the host one line at a time.
///
/// INPUT: reads std::cin, so this blocks the interpreter thread while the user
/// types. Returning EOF is how a stopped script unwinds out of a pending read.
class HostInBuf : public std::streambuf {
protected:
    int underflow() override {
        if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
        if (!g_host || !g_host->readline) return traits_type::eof();

        std::vector<char> buf(4096);
        buf[0] = '\0';
        if (!g_host->readline(g_host->ctx, buf.data(), static_cast<int>(buf.size()))) {
            return traits_type::eof();
        }

        line_.assign(buf.data());
        line_.push_back('\n');  // the interpreter expects newline-terminated lines
        setg(line_.data(), line_.data(), line_.data() + line_.size());
        return traits_type::to_int_type(*gptr());
    }

private:
    std::string line_;
};

// ---------------------------------------------------------------- iOS builtins

void registerIosBuiltins(umbrly::Interpreter& interp) {
    using umbrly::Value;
    using Args = std::vector<Value>;

    // --- Group 2: dialogs -------------------------------------------------
    interp.registerBuiltin("MSGBOX", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgcRange(a, 1, 2, "MSGBOX", line);
        const std::string& text = strArg(a, 0, "MSGBOX", line);
        std::string title = a.size() >= 2 ? strArg(a, 1, "MSGBOX", line) : "Umbrly";
        if (g_host && g_host->msgbox) g_host->msgbox(g_host->ctx, title.c_str(), text.c_str());
        return Value::Nil();
    });

    interp.registerBuiltin("CONFIRM", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgcRange(a, 1, 2, "CONFIRM", line);
        const std::string& text = strArg(a, 0, "CONFIRM", line);
        std::string title = a.size() >= 2 ? strArg(a, 1, "CONFIRM", line) : "Umbrly";
        if (!g_host || !g_host->confirm) return Value::Bool(false);
        return Value::Bool(g_host->confirm(g_host->ctx, title.c_str(), text.c_str()) != 0);
    });

    // --- Group 2: console -------------------------------------------------
    interp.registerBuiltin("CLS", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 0, "CLS", line);
        if (g_host && g_host->cls) g_host->cls(g_host->ctx);
        return Value::Nil();
    });

    interp.registerBuiltin("COLOR", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgcRange(a, 1, 2, "COLOR", line);
        int fg = static_cast<int>(intArg(a, 0, "COLOR", line));
        int bg = a.size() >= 2 ? static_cast<int>(intArg(a, 1, "COLOR", line)) : 0;
        if (g_host && g_host->color) g_host->color(g_host->ctx, fg, bg);
        return Value::Nil();
    });

    interp.registerBuiltin("COLOR_RESET", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 0, "COLOR_RESET", line);
        if (g_host && g_host->color) g_host->color(g_host->ctx, -1, -1);
        return Value::Nil();
    });

    interp.registerBuiltin("GOTOXY", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 2, "GOTOXY", line);
        int x = static_cast<int>(intArg(a, 0, "GOTOXY", line));
        int y = static_cast<int>(intArg(a, 1, "GOTOXY", line));
        if (g_host && g_host->gotoxy) g_host->gotoxy(g_host->ctx, x, y);
        return Value::Nil();
    });

    interp.registerBuiltin("CONSOLE_TITLE", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 1, "CONSOLE_TITLE", line);
        const std::string& t = strArg(a, 0, "CONSOLE_TITLE", line);
        if (g_host && g_host->title) g_host->title(g_host->ctx, t.c_str());
        return Value::Nil();
    });

    // --- Group 2: keyboard ------------------------------------------------
    interp.registerBuiltin("GETKEY", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 0, "GETKEY", line);
        if (!g_host || !g_host->getkey) return Value::Int(0);
        return Value::Int(g_host->getkey(g_host->ctx, 1));
    });

    interp.registerBuiltin("KEY_PRESSED", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 0, "KEY_PRESSED", line);
        if (!g_host || !g_host->getkey) return Value::Bool(false);
        return Value::Bool(g_host->getkey(g_host->ctx, 0) != 0);
    });

    // --- Group 2: touch replaces the mouse --------------------------------
    interp.registerBuiltin("MOUSE_X", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 0, "MOUSE_X", line);
        if (!g_host || !g_host->touch_x) return Value::Int(0);
        return Value::Int(static_cast<long long>(g_host->touch_x(g_host->ctx)));
    });

    interp.registerBuiltin("MOUSE_Y", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 0, "MOUSE_Y", line);
        if (!g_host || !g_host->touch_y) return Value::Int(0);
        return Value::Int(static_cast<long long>(g_host->touch_y(g_host->ctx)));
    });

    // --- Group 2: sound ---------------------------------------------------
    interp.registerBuiltin("BEEP", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgcRange(a, 0, 2, "BEEP", line);
        int freq = a.size() >= 1 ? static_cast<int>(intArg(a, 0, "BEEP", line)) : 800;
        int ms = a.size() >= 2 ? static_cast<int>(intArg(a, 1, "BEEP", line)) : 200;
        if (g_host && g_host->beep) g_host->beep(g_host->ctx, freq, ms);
        return Value::Nil();
    });

    interp.registerBuiltin("PLAY_SOUND", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 1, "PLAY_SOUND", line);
        const std::string& name = strArg(a, 0, "PLAY_SOUND", line);
        if (g_host && g_host->play) g_host->play(g_host->ctx, name.c_str());
        return Value::Nil();
    });

    // --- Group 2: clipboard ----------------------------------------------
    interp.registerBuiltin("CLIPBOARD_SET", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 1, "CLIPBOARD_SET", line);
        const std::string& t = strArg(a, 0, "CLIPBOARD_SET", line);
        if (g_host && g_host->clipboard_set) g_host->clipboard_set(g_host->ctx, t.c_str());
        return Value::Nil();
    });

    interp.registerBuiltin("CLIPBOARD_GET", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 0, "CLIPBOARD_GET", line);
        if (!g_host || !g_host->clipboard_get) return Value::Str("");
        std::vector<char> buf(8192);
        buf[0] = '\0';
        if (!g_host->clipboard_get(g_host->ctx, buf.data(), static_cast<int>(buf.size()))) {
            return Value::Str("");
        }
        return Value::Str(std::string(buf.data()));
    });

    // --- Group 2: device --------------------------------------------------
    auto screenMetric = [](bool wantWidth) {
        return [wantWidth](umbrly::Interpreter&, Args& a, int line) -> Value {
            checkArgc(a, 0, wantWidth ? "SCREEN_WIDTH" : "SCREEN_HEIGHT", line);
            int w = 0, h = 0;
            if (g_host && g_host->metrics) g_host->metrics(g_host->ctx, &w, &h);
            return Value::Int(wantWidth ? w : h);
        };
    };
    interp.registerBuiltin("SCREEN_WIDTH", screenMetric(true));
    interp.registerBuiltin("SCREEN_HEIGHT", screenMetric(false));

    auto deviceName = [](const char* fn) {
        return [fn](umbrly::Interpreter&, Args& a, int line) -> Value {
            checkArgc(a, 0, fn, line);
            if (!g_host || !g_host->device_name) return Value::Str("iOS");
            std::vector<char> buf(256);
            buf[0] = '\0';
            if (!g_host->device_name(g_host->ctx, buf.data(), static_cast<int>(buf.size()))) {
                return Value::Str("iOS");
            }
            return Value::Str(std::string(buf.data()));
        };
    };
    // iOS has no separate machine and account name — both report the device.
    interp.registerBuiltin("COMPUTER_NAME", deviceName("COMPUTER_NAME"));
    interp.registerBuiltin("USER_NAME", deviceName("USER_NAME"));

    interp.registerBuiltin("SHELL_OPEN", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 1, "SHELL_OPEN", line);
        const std::string& url = strArg(a, 0, "SHELL_OPEN", line);
        if (!g_host || !g_host->open_url) return Value::Bool(false);
        return Value::Bool(g_host->open_url(g_host->ctx, url.c_str()) != 0);
    });

    // --- Group 1: portable, identical to Windows --------------------------
    interp.registerBuiltin("SLEEP", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 1, "SLEEP", line);
        long long ms = intArg(a, 0, "SLEEP", line);
        if (ms < 0) ms = 0;
        // Sliced so a stop request is noticed promptly instead of only after a
        // long SLEEP has run its course.
        const long long slice = 50;
        while (ms > 0 && !g_stopRequested.load()) {
            long long chunk = ms < slice ? ms : slice;
            std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
            ms -= chunk;
        }
        return Value::Nil();
    });

    interp.registerBuiltin("TICK_COUNT", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 0, "TICK_COUNT", line);
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        return Value::Int(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    });

    interp.registerBuiltin("CLOCK", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 0, "CLOCK", line);
        return Value::Float(static_cast<double>(std::clock()) / CLOCKS_PER_SEC);
    });

    interp.registerBuiltin("TIME_NOW", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 0, "TIME_NOW", line);
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
        return Value::Str(std::string(buf));
    });

    interp.registerBuiltin("CPU_COUNT", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 0, "CPU_COUNT", line);
        long count = sysconf(_SC_NPROCESSORS_ONLN);
        return Value::Int(count > 0 ? static_cast<long long>(count) : 1);
    });

    interp.registerBuiltin("EXIT", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgcRange(a, 0, 1, "EXIT", line);
        // An iOS app must not call exit() — that reads as a crash to the user
        // and is grounds for review rejection. Stop the script instead.
        g_stopRequested.store(true);
        umbrly::failAt(line, "EXIT(): выполнение остановлено");
    });

    // --- Group 1: files, confined to the sandbox --------------------------
    interp.registerBuiltin("FILE_READ", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 1, "FILE_READ", line);
        auto path = resolvePath(strArg(a, 0, "FILE_READ", line), "FILE_READ", line);
        std::ifstream in(path, std::ios::binary);
        if (!in) umbrly::failAt(line, "FILE_READ: не удалось открыть " + path.filename().string());
        std::ostringstream ss;
        ss << in.rdbuf();
        return Value::Str(ss.str());
    });

    auto writeFile = [](bool append) {
        const char* fn = append ? "FILE_APPEND" : "FILE_WRITE";
        return [append, fn](umbrly::Interpreter&, Args& a, int line) -> Value {
            checkArgc(a, 2, fn, line);
            auto path = resolvePath(strArg(a, 0, fn, line), fn, line);
            const std::string& text = strArg(a, 1, fn, line);
            std::ofstream out(path, append ? (std::ios::binary | std::ios::app) : std::ios::binary);
            if (!out) {
                umbrly::failAt(line, std::string(fn) + ": не удалось записать " +
                                         path.filename().string());
            }
            out << text;
            return Value::Bool(static_cast<bool>(out));
        };
    };
    interp.registerBuiltin("FILE_WRITE", writeFile(false));
    interp.registerBuiltin("FILE_APPEND", writeFile(true));

    interp.registerBuiltin("FILE_EXISTS", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 1, "FILE_EXISTS", line);
        auto path = resolvePath(strArg(a, 0, "FILE_EXISTS", line), "FILE_EXISTS", line);
        std::error_code ec;
        return Value::Bool(std::filesystem::is_regular_file(path, ec));
    });

    interp.registerBuiltin("FILE_DELETE", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 1, "FILE_DELETE", line);
        auto path = resolvePath(strArg(a, 0, "FILE_DELETE", line), "FILE_DELETE", line);
        std::error_code ec;
        return Value::Bool(std::filesystem::remove(path, ec));
    });

    interp.registerBuiltin("DIR_EXISTS", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 1, "DIR_EXISTS", line);
        auto path = resolvePath(strArg(a, 0, "DIR_EXISTS", line), "DIR_EXISTS", line);
        std::error_code ec;
        return Value::Bool(std::filesystem::is_directory(path, ec));
    });

    interp.registerBuiltin("DIR_CREATE", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 1, "DIR_CREATE", line);
        auto path = resolvePath(strArg(a, 0, "DIR_CREATE", line), "DIR_CREATE", line);
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return Value::Bool(!ec);
    });

    interp.registerBuiltin("ENV_GET", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 1, "ENV_GET", line);
        const char* v = std::getenv(strArg(a, 0, "ENV_GET", line).c_str());
        return Value::Str(v ? v : "");
    });

    interp.registerBuiltin("ENV_SET", [](umbrly::Interpreter&, Args& a, int line) -> Value {
        checkArgc(a, 2, "ENV_SET", line);
        const std::string& k = strArg(a, 0, "ENV_SET", line);
        const std::string& v = strArg(a, 1, "ENV_SET", line);
        // Process-local only: iOS has no persistent environment for an app.
        return Value::Bool(setenv(k.c_str(), v.c_str(), 1) == 0);
    });

    // --- Group 3: genuinely impossible on iOS -----------------------------
    registerUnavailable(interp, "RUN",
                        "приложение в песочнице не может запускать другие процессы");
    registerUnavailable(interp, "FIND_WINDOW",
                        "в iOS нет доступа к окнам других приложений");
    registerUnavailable(interp, "ACTIVATE_WINDOW",
                        "в iOS нет доступа к окнам других приложений");
    registerUnavailable(interp, "SET_MOUSE_POS",
                        "iOS не позволяет синтезировать ввод; используйте касания экрана");
    registerUnavailable(interp, "CLICK_MOUSE",
                        "iOS не позволяет синтезировать ввод; используйте касания экрана");
}

}  // namespace

// ---------------------------------------------------------------- public C API

extern "C" {

void umbrly_request_stop(void) { g_stopRequested.store(true); }

void umbrly_clear_stop(void) { g_stopRequested.store(false); }

UmbrlyStatus umbrly_run(const char* source, const char* sandboxRoot, const UmbrlyHost* host) {
    if (!source || !host) return UMBRLY_INTERNAL_ERROR;

    g_host = host;
    g_sandbox = sandboxRoot ? std::filesystem::path(sandboxRoot) : std::filesystem::path(".");

    HostOutBuf outBuf;
    HostInBuf inBuf;
    std::streambuf* oldOut = std::cout.rdbuf(&outBuf);
    std::streambuf* oldIn = std::cin.rdbuf(&inBuf);

    UmbrlyStatus status = UMBRLY_OK;
    try {
        std::vector<umbrly::Token> tokens = umbrly::tokenize(source);
        umbrly::Block program = umbrly::parseProgram(tokens);

        umbrly::Interpreter interpreter;
        umbrly::registerCoreBuiltins(interpreter);
        registerIosBuiltins(interpreter);

        // Same budget the Android sandbox uses. Without it a runaway loop would
        // pin the interpreter thread with no way back to the UI.
        interpreter.setExecutionLimits(5000000, 3000);
        interpreter.run(program);
    } catch (const umbrly::UmbrlyError& e) {
        std::cout << "\nОшибка " << e.what() << "\n";
        status = g_stopRequested.load() ? UMBRLY_STOPPED : UMBRLY_RUNTIME_ERROR;
    } catch (const std::exception& e) {
        std::cout << "\nОшибка: " << e.what() << "\n";
        status = UMBRLY_INTERNAL_ERROR;
    } catch (...) {
        std::cout << "\nОшибка: неизвестный сбой\n";
        status = UMBRLY_INTERNAL_ERROR;
    }

    std::cout.flush();
    std::cout.rdbuf(oldOut);
    std::cin.rdbuf(oldIn);
    g_host = nullptr;
    return status;
}

const char* umbrly_platform_report(void) {
    return
        "Трансляция WinAPI → iOS\n"
        "\n"
        "Работают как на Windows:\n"
        "  SLEEP, TICK_COUNT, CLOCK, TIME_NOW, CPU_COUNT,\n"
        "  FILE_READ/WRITE/APPEND/EXISTS/DELETE, DIR_EXISTS/CREATE,\n"
        "  ENV_GET, ENV_SET\n"
        "\n"
        "Переведены на возможности iOS:\n"
        "  MSGBOX, CONFIRM         — системные алерты\n"
        "  CLS, COLOR, GOTOXY,\n"
        "  CONSOLE_TITLE           — виртуальная консоль\n"
        "  GETKEY, KEY_PRESSED     — экранная клавиатура\n"
        "  MOUSE_X, MOUSE_Y        — координаты последнего касания\n"
        "  BEEP, PLAY_SOUND        — системные звуки\n"
        "  CLIPBOARD_SET/GET       — UIPasteboard\n"
        "  SCREEN_WIDTH/HEIGHT     — размер экрана в пунктах\n"
        "  COMPUTER_NAME/USER_NAME — имя устройства\n"
        "  SHELL_OPEN              — открытие URL системой\n"
        "  EXIT                    — останов скрипта, не приложения\n"
        "\n"
        "Невозможны на iOS — вызов даёт ошибку, которую ловит TRY/CATCH:\n"
        "  RUN             — песочница не запускает процессы\n"
        "  FIND_WINDOW     — нет доступа к чужим окнам\n"
        "  ACTIVATE_WINDOW — нет доступа к чужим окнам\n"
        "  SET_MOUSE_POS   — нельзя синтезировать ввод\n"
        "  CLICK_MOUSE     — нельзя синтезировать ввод\n"
        "\n"
        "Файловые функции работают только внутри папки документов приложения.\n";
}

}  // extern "C"
