// Лексер Umbrly: превращает исходный текст в поток токенов.
#pragma once

#include <string>
#include <vector>

namespace umbrly {

enum class TokType {
    Eof,
    Newline,
    Ident,      // идентификаторы и ключевые слова (PRINT, IF, INT_X, AND, ...)
    IntLit,
    FloatLit,
    StringLit,  // "сырое" содержимое строки — экранирование/интерполяция разбираются позже
    Punct       // операторы и знаки препинания: == != <= >= = < > + - * / % ( ) [ ] , :
};

struct Token {
    TokType type = TokType::Eof;
    std::string text;   // текст идентификатора/пунктуации, сырое содержимое строки
    long long ival = 0;
    double fval = 0.0;
    int line = 0;
};

// Разбивает весь исходный текст программы на токены.
// Комментарии (# и //, вне строк) вырезаются лексером.
std::vector<Token> tokenize(const std::string& source);

}  // namespace umbrly
