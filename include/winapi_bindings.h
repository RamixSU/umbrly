// Регистрация встроенных функций-обёрток над WinAPI (консоль, окна, файлы, звук, мышь/клавиатура).
#pragma once

#include "interpreter.h"

namespace umbrly {

void registerWinApiBuiltins(Interpreter& interp);

}  // namespace umbrly
