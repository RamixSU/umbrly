// Парсер Umbrly: строит AST (Block из Stmt) из потока токенов.
#pragma once

#include "ast.h"
#include "lexer.h"

namespace umbrly {

// Разбирает всю программу (используется main.cpp для файла/REPL-блока).
// [подстановки] внутри строковых литералов разбираются здесь же, один раз —
// см. Parser::buildStringSegments в parser.cpp.
Block parseProgram(const std::vector<Token>& tokens);

}  // namespace umbrly
