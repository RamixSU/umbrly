// Транспилятор Umbrly -> C++. Управляющие конструкции (IF/WHILE/FOR/FUNC/BREAK/RETURN/TRY)
// становятся настоящим нативным C++; операторы, builtin-функции и методы классов по-прежнему
// вызывают уже проверенный Interpreter как раннтайм — см. README, раздел "Компиляция".
#pragma once

#include <string>

#include "ast.h"

namespace umbrly {

// originalSource встраивается в сгенерированный файл как строковый литерал: скомпилированная
// программа на старте заново парсит его и вызывает Interpreter::registerAllDefs(), чтобы CLASS
// (методы которых по-прежнему интерпретируются через AST) и таблица FUNC были на месте.
std::string generateCpp(const Block& program, const std::string& originalSource);

}  // namespace umbrly
