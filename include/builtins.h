// Регистрация встроенных функций общего назначения (математика, строки, массивы, типы).
#pragma once

#include "interpreter.h"

namespace umbrly {

void registerCoreBuiltins(Interpreter& interp);

}  // namespace umbrly
