#include "lexer.h"

#include <cctype>
#include <cstring>

#include "errors.h"

namespace umbrly {

namespace {

bool isIdentStart(unsigned char c) { return std::isalpha(c) || c == '_'; }
bool isIdentChar(unsigned char c)  { return std::isalnum(c) || c == '_'; }

// Многосимвольные операторы должны проверяться раньше односимвольных.
const char* kMultiOps[] = { "==", "!=", "<=", ">=", "&&", "||" };
const char  kSingleOps[] = "=<>+-*/%()[],:!.";

}  // namespace

std::vector<Token> tokenize(const std::string& src) {
    std::vector<Token> out;
    size_t pos = 0;
    int line = 1;
    bool atLineStart = true;  // используется, чтобы не плодить пустые Newline-токены подряд

    auto push = [&](TokType t, std::string text = "") {
        Token tok;
        tok.type = t;
        tok.text = std::move(text);
        tok.line = line;
        out.push_back(std::move(tok));
    };

    size_t n = src.size();
    while (pos < n) {
        char c = src[pos];

        // Пропуск BOM в начале файла.
        if (pos == 0 && n >= 3 && (unsigned char)src[0] == 0xEF &&
            (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) {
            pos += 3;
            continue;
        }

        if (c == '\r') { pos++; continue; }

        if (c == '\n') {
            if (!atLineStart) { push(TokType::Newline); atLineStart = true; }
            line++;
            pos++;
            continue;
        }

        if (c == ' ' || c == '\t') { pos++; continue; }

        // Комментарии # и // — до конца строки.
        if (c == '#' || (c == '/' && pos + 1 < n && src[pos + 1] == '/')) {
            while (pos < n && src[pos] != '\n') pos++;
            continue;
        }

        if (c == '"') {
            int startLine = line;
            pos++;
            std::string raw;
            while (pos < n && src[pos] != '"') {
                if (src[pos] == '\\' && pos + 1 < n) {
                    raw += src[pos];
                    raw += src[pos + 1];
                    pos += 2;
                } else {
                    if (src[pos] == '\n') line++;
                    raw += src[pos++];
                }
            }
            if (pos >= n) failAt(startLine, "строка не закрыта кавычкой \"");
            pos++;  // закрывающая "
            Token tok;
            tok.type = TokType::StringLit;
            tok.text = raw;
            tok.line = startLine;
            out.push_back(std::move(tok));
            atLineStart = false;
            continue;
        }

        if (std::isdigit((unsigned char)c)) {
            size_t start = pos;
            while (pos < n && std::isdigit((unsigned char)src[pos])) pos++;
            bool isFloat = false;
            if (pos < n && src[pos] == '.' && pos + 1 < n && std::isdigit((unsigned char)src[pos + 1])) {
                isFloat = true;
                pos++;
                while (pos < n && std::isdigit((unsigned char)src[pos])) pos++;
            }
            std::string numStr = src.substr(start, pos - start);
            Token tok;
            tok.line = line;
            if (isFloat) { tok.type = TokType::FloatLit; tok.fval = std::stod(numStr); }
            else         { tok.type = TokType::IntLit;   tok.ival = std::stoll(numStr); }
            out.push_back(std::move(tok));
            atLineStart = false;
            continue;
        }

        if (isIdentStart((unsigned char)c)) {
            size_t start = pos;
            while (pos < n && isIdentChar((unsigned char)src[pos])) pos++;
            Token tok;
            tok.type = TokType::Ident;
            tok.text = src.substr(start, pos - start);
            tok.line = line;
            out.push_back(std::move(tok));
            atLineStart = false;
            continue;
        }

        bool matched = false;
        for (const char* op : kMultiOps) {
            size_t len = std::strlen(op);
            if (src.compare(pos, len, op) == 0) {
                push(TokType::Punct, op);
                pos += len;
                matched = true;
                atLineStart = false;
                break;
            }
        }
        if (matched) continue;

        if (std::strchr(kSingleOps, c)) {
            push(TokType::Punct, std::string(1, c));
            pos++;
            atLineStart = false;
            continue;
        }

        failAt(line, std::string("недопустимый символ в исходном коде: '") + c + "'");
    }

    if (!atLineStart) push(TokType::Newline);
    Token eof;
    eof.type = TokType::Eof;
    eof.line = line;
    out.push_back(eof);
    return out;
}

}  // namespace umbrly
