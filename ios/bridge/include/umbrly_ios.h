// C bridge between the Umbrly C++ interpreter and the iOS host application.
//
// The interpreter runs on a background thread and talks to the UI only through
// the callbacks in UmbrlyHost. Callbacks that need an answer (input, confirm,
// getkey) block the interpreter thread until the host supplies one.
#ifndef UMBRLY_IOS_H
#define UMBRLY_IOS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UmbrlyHost {
    void* ctx;

    /// Any text produced by PRINT: or by console builtins. Not newline framed.
    void (*write)(void* ctx, const char* utf8);

    /// CLS() — clear the virtual console.
    void (*cls)(void* ctx);
    /// COLOR(fg, bg) — Windows console colour indices 0..15. -1 means reset.
    void (*color)(void* ctx, int fg, int bg);
    /// GOTOXY(x, y) — zero-based cell coordinates in the virtual console.
    void (*gotoxy)(void* ctx, int x, int y);
    /// CONSOLE_TITLE(text)
    void (*title)(void* ctx, const char* utf8);

    /// MSGBOX(text, title) — returns once the alert has been dismissed.
    void (*msgbox)(void* ctx, const char* title, const char* text);
    /// CONFIRM(text, title) — 1 for yes, 0 for no.
    int (*confirm)(void* ctx, const char* title, const char* text);

    /// INPUT: — one line without the trailing newline. Returns 0 at end of
    /// input (user stopped the script), 1 on success.
    int (*readline)(void* ctx, char* buf, int cap);

    /// GETKEY() blocking, KEY_PRESSED() non-blocking. Returns 0 when no key.
    int (*getkey)(void* ctx, int blocking);

    /// BEEP(freq, ms) and PLAY_SOUND(name).
    void (*beep)(void* ctx, int freq, int ms);
    void (*play)(void* ctx, const char* name);

    void (*clipboard_set)(void* ctx, const char* text);
    int (*clipboard_get)(void* ctx, char* buf, int cap);

    /// SCREEN_WIDTH() / SCREEN_HEIGHT() in points.
    void (*metrics)(void* ctx, int* w, int* h);
    /// COMPUTER_NAME() / USER_NAME().
    int (*device_name)(void* ctx, char* buf, int cap);

    /// SHELL_OPEN(url) — 1 if iOS accepted the URL.
    int (*open_url)(void* ctx, const char* url);

    /// MOUSE_X() / MOUSE_Y() — last touch point, in points.
    double (*touch_x)(void* ctx);
    double (*touch_y)(void* ctx);
} UmbrlyHost;

typedef enum UmbrlyStatus {
    UMBRLY_OK = 0,
    UMBRLY_RUNTIME_ERROR = 1,
    UMBRLY_STOPPED = 2,
    UMBRLY_INTERNAL_ERROR = 3
} UmbrlyStatus;

/// Runs `source` to completion on the calling thread.
///
/// `sandboxRoot` is the directory that relative paths in FILE_*/DIR_* builtins
/// resolve against — the app's Documents directory. Paths escaping it are
/// rejected, because an iOS app cannot reach them anyway.
///
/// Diagnostics are delivered through host->write, so the caller only needs the
/// status.
UmbrlyStatus umbrly_run(const char* source,
                        const char* sandboxRoot,
                        const UmbrlyHost* host);

/// Asks the running script to stop at the next instruction. Thread-safe.
void umbrly_request_stop(void);

/// Clears a previous stop request. Call before umbrly_run.
void umbrly_clear_stop(void);

/// Which Windows builtins are emulated on iOS and which cannot exist here.
/// UTF-8, owned by the library.
const char* umbrly_platform_report(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // UMBRLY_IOS_H
