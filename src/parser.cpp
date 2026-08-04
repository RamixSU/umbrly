#include "parser.h"

#include <unordered_set>

#include "errors.h"

namespace umbrly {

namespace {

const std::unordered_set<std::string>& statementKeywords() {
    static const std::unordered_set<std::string> kw = {
        "PRINT", "INPUT", "IF", "ELSE", "WHILE", "FOR", "TO", "STEP",
        "FUNC", "RETURN", "BREAK", "CONTINUE", "TRY", "CATCH", "END", "CLASS", "LOAD"
    };
    return kw;
}

class Parser {
public:
    explicit Parser(const std::vector<Token>& toks) : toks_(toks) {}

    Block parseProgram() {
        Block b = parseBlockUntil({});
        if (cur().type != TokType::Eof)
            failAt(cur().line, "неожиданный текст в конце программы: '" + cur().text + "'");
        return b;
    }

    // Точка входа для интерполяции строк — один изолированный кусок выражения.
    // tokenize() всегда добавляет завершающий Newline перед Eof — пропускаем его здесь.
    ExprPtr parseStandaloneExpr() {
        ExprPtr e = parseOr();
        while (cur().type == TokType::Newline) advance();
        if (cur().type != TokType::Eof)
            failAt(cur().line, "лишний текст в выражении: '" + cur().text + "'");
        return e;
    }

private:
    const std::vector<Token>& toks_;
    size_t pos_ = 0;
    int loopDepth_ = 0;  // для проверки, что BREAK/CONTINUE стоят внутри WHILE/FOR

    const Token& cur() const { return toks_[pos_]; }
    Token advance() { Token t = toks_[pos_]; if (pos_ + 1 < toks_.size()) pos_++; return t; }

    bool checkPunct(const char* p) const { return cur().type == TokType::Punct && cur().text == p; }
    bool checkIdentKw(const char* kw) const { return cur().type == TokType::Ident && cur().text == kw; }

    void expectPunct(const char* p) {
        if (!checkPunct(p)) failAt(cur().line, std::string("ожидался символ '") + p + "', получено: '" + cur().text + "'");
        advance();
    }

    std::string expectIdentName() {
        if (cur().type != TokType::Ident) failAt(cur().line, "ожидалось имя, получено: '" + cur().text + "'");
        return advance().text;
    }

    // Заканчивает простой оператор: требует перевод строки, EOF или начало терминатора блока.
    void finishStatement() {
        if (cur().type == TokType::Newline) { advance(); return; }
        if (cur().type == TokType::Eof) return;
        if (cur().type == TokType::Ident && statementKeywords().count(cur().text)) return;
        failAt(cur().line, "ожидался конец строки, получено: '" + cur().text + "'");
    }

    void skipNewlines() { while (cur().type == TokType::Newline) advance(); }

    Block parseBlockUntil(std::initializer_list<const char*> terminators) {
        Block block;
        for (;;) {
            skipNewlines();
            if (cur().type == TokType::Eof) return block;
            if (cur().type == TokType::Ident) {
                for (const char* t : terminators)
                    if (cur().text == t) return block;
            }
            block.push_back(parseStatement());
        }
    }

    // ---------- Операторы ----------

    StmtPtr parseStatement() {
        const Token& t = cur();
        if (t.type == TokType::Ident) {
            if (t.text == "PRINT")    return parsePrint();
            if (t.text == "INPUT")    return parseInputPause();
            if (t.text == "IF")       return parseIf();
            if (t.text == "WHILE")    return parseWhile();
            if (t.text == "FOR")      return parseFor();
            if (t.text == "FUNC")     return parseFuncDef();
            if (t.text == "RETURN")   return parseReturn();
            if (t.text == "BREAK") {
                if (loopDepth_ == 0) failAt(t.line, "BREAK встречается вне цикла (WHILE/FOR)");
                advance(); auto s = std::make_unique<BreakStmt>(); s->line = t.line; finishStatement(); return s;
            }
            if (t.text == "CONTINUE") {
                if (loopDepth_ == 0) failAt(t.line, "CONTINUE встречается вне цикла (WHILE/FOR)");
                advance(); auto s = std::make_unique<ContinueStmt>(); s->line = t.line; finishStatement(); return s;
            }
            if (t.text == "TRY")      return parseTry();
            if (t.text == "CLASS")    return parseClassDef();
            if (t.text == "LOAD")     return parseLoad();
            if (t.text == "END" || t.text == "ELSE" || t.text == "CATCH")
                failAt(t.line, "'" + t.text + "' без соответствующего открывающего блока");
        }
        return parseAssignOrExprStmt();
    }

    StmtPtr parsePrint() {
        int line = advance().line;  // PRINT
        expectPunct(":");
        auto s = std::make_unique<PrintStmt>();
        s->line = line;
        if (cur().type != TokType::Newline && cur().type != TokType::Eof) s->value = parseExpr();
        finishStatement();
        return s;
    }

    // LOAD ИМЯ — без двоеточия, просто одно имя модуля/библиотеки. Само разрешение
    // (поиск ИМЯ.umb и вставка его содержимого) происходит позже, в main.cpp,
    // после парсинга всей программы — здесь только фиксируем имя и номер строки.
    StmtPtr parseLoad() {
        int line = advance().line;  // LOAD
        std::string name = expectIdentName();
        auto s = std::make_unique<LoadStmt>();
        s->line = line;
        s->moduleName = name;
        finishStatement();
        return s;
    }

    StmtPtr parseInputPause() {
        int line = advance().line;  // INPUT
        expectPunct(":");
        auto s = std::make_unique<InputPauseStmt>();
        s->line = line;
        if (cur().type != TokType::Newline && cur().type != TokType::Eof) s->prompt = parseExpr();
        finishStatement();
        return s;
    }

    StmtPtr parseIf() {
        int line = advance().line;  // IF
        expectPunct(":");
        ExprPtr cond = parseExpr();
        Block thenB = parseBlockUntil({"ELSE", "END"});
        Block elseB;
        if (checkIdentKw("ELSE")) {
            advance();
            if (checkPunct(":")) advance();
            elseB = parseBlockUntil({"END"});
        }
        if (!checkIdentKw("END")) failAt(cur().line, "блок IF не закрыт — ожидается END");
        advance();
        finishStatement();
        auto s = std::make_unique<IfStmt>();
        s->line = line; s->cond = std::move(cond); s->thenBlock = std::move(thenB); s->elseBlock = std::move(elseB);
        return s;
    }

    StmtPtr parseWhile() {
        int line = advance().line;  // WHILE
        expectPunct(":");
        ExprPtr cond = parseExpr();
        loopDepth_++;
        Block body = parseBlockUntil({"END"});
        loopDepth_--;
        if (!checkIdentKw("END")) failAt(cur().line, "блок WHILE не закрыт — ожидается END");
        advance();
        finishStatement();
        auto s = std::make_unique<WhileStmt>();
        s->line = line; s->cond = std::move(cond); s->body = std::move(body);
        return s;
    }

    StmtPtr parseFor() {
        int line = advance().line;  // FOR
        expectPunct(":");
        std::string varName = expectIdentName();
        expectPunct("=");
        ExprPtr fromE = parseExpr();
        if (!checkIdentKw("TO")) failAt(cur().line, "ожидалось TO в заголовке FOR");
        advance();
        ExprPtr toE = parseExpr();
        ExprPtr stepE;
        if (checkIdentKw("STEP")) { advance(); stepE = parseExpr(); }
        loopDepth_++;
        Block body = parseBlockUntil({"END"});
        loopDepth_--;
        if (!checkIdentKw("END")) failAt(cur().line, "блок FOR не закрыт — ожидается END");
        advance();
        finishStatement();
        auto s = std::make_unique<ForStmt>();
        s->line = line; s->varName = varName;
        s->fromExpr = std::move(fromE); s->toExpr = std::move(toE); s->stepExpr = std::move(stepE);
        s->body = std::move(body);
        return s;
    }

    StmtPtr parseFuncDef() {
        int line = advance().line;  // FUNC
        std::string name = expectIdentName();
        expectPunct("(");
        std::vector<std::string> params;
        if (!checkPunct(")")) {
            params.push_back(expectIdentName());
            while (checkPunct(",")) { advance(); params.push_back(expectIdentName()); }
        }
        expectPunct(")");
        expectPunct(":");
        Block body = parseBlockUntil({"END"});
        if (!checkIdentKw("END")) failAt(cur().line, "блок FUNC не закрыт — ожидается END");
        advance();
        finishStatement();
        auto s = std::make_unique<FuncDefStmt>();
        s->line = line; s->name = name; s->params = std::move(params);
        s->body = std::make_shared<Block>(std::move(body));
        return s;
    }

    StmtPtr parseReturn() {
        int line = advance().line;  // RETURN
        auto s = std::make_unique<ReturnStmt>();
        s->line = line;
        bool atEndOfStmt = cur().type == TokType::Newline || cur().type == TokType::Eof ||
                            (cur().type == TokType::Ident && statementKeywords().count(cur().text));
        if (!atEndOfStmt) s->value = parseExpr();
        finishStatement();
        return s;
    }

    StmtPtr parseTry() {
        int line = advance().line;  // TRY
        expectPunct(":");
        Block tryB = parseBlockUntil({"CATCH"});
        if (!checkIdentKw("CATCH")) failAt(cur().line, "блок TRY должен содержать CATCH");
        advance();
        expectPunct(":");
        std::string errVar;
        if (cur().type == TokType::Ident && !statementKeywords().count(cur().text)) errVar = advance().text;
        Block catchB = parseBlockUntil({"END"});
        if (!checkIdentKw("END")) failAt(cur().line, "блок TRY/CATCH не закрыт — ожидается END");
        advance();
        finishStatement();
        auto s = std::make_unique<TryStmt>();
        s->line = line; s->tryBlock = std::move(tryB); s->errVarName = errVar; s->catchBlock = std::move(catchB);
        return s;
    }

    // CLASS Имя:
    //     ТИП_ПОЛЕ = значение_по_умолчанию   (0 и более, в любом порядке с методами)
    //     FUNC ИМЯ(параметры): ... END        (0 и более; INIT играет роль конструктора)
    // END
    StmtPtr parseClassDef() {
        int line = advance().line;  // CLASS
        std::string name = expectIdentName();
        expectPunct(":");

        std::vector<ClassField> fields;
        std::vector<std::unique_ptr<FuncDefStmt>> methods;

        for (;;) {
            skipNewlines();
            if (checkIdentKw("END")) break;
            if (cur().type == TokType::Eof) failAt(cur().line, "блок CLASS не закрыт — ожидается END");
            if (checkIdentKw("FUNC")) {
                StmtPtr raw = parseFuncDef();
                methods.emplace_back(static_cast<FuncDefStmt*>(raw.release()));
                continue;
            }
            std::string fname = expectIdentName();
            expectPunct("=");
            ExprPtr fval = parseExpr();
            finishStatement();
            fields.push_back(ClassField{fname, std::move(fval)});
        }
        advance();  // END
        finishStatement();

        auto s = std::make_unique<ClassDefStmt>();
        s->line = line; s->name = name;
        s->fields = std::move(fields);
        s->methods = std::move(methods);
        return s;
    }

    StmtPtr parseAssignOrExprStmt() {
        int line = cur().line;
        ExprPtr e = parseOr();

        if (checkPunct("=")) {
            advance();

            if (checkIdentKw("INPUT")) {
                advance();
                expectPunct(":");
                ExprPtr prompt;
                if (cur().type != TokType::Newline && cur().type != TokType::Eof) prompt = parseExpr();
                finishStatement();
                auto* nameExpr = dynamic_cast<NameExpr*>(e.get());
                if (!nameExpr) failAt(line, "INPUT можно присвоить только простой переменной");
                auto s = std::make_unique<AssignStmt>();
                s->line = line; s->name = nameExpr->name; s->isInput = true; s->inputPrompt = std::move(prompt);
                return s;
            }

            ExprPtr rhs = parseExpr();
            finishStatement();
            auto s = std::make_unique<AssignStmt>();
            s->line = line;
            if (auto* ne = dynamic_cast<NameExpr*>(e.get())) {
                s->name = ne->name;
            } else if (auto* ie = dynamic_cast<IndexExpr*>(e.get())) {
                auto* baseName = dynamic_cast<NameExpr*>(ie->base.get());
                if (!baseName) failAt(line, "слева от '=' должна быть переменная или элемент массива");
                s->name = baseName->name;
                s->index = std::move(ie->index);
            } else if (auto* me = dynamic_cast<MemberExpr*>(e.get())) {
                s->memberBase = std::move(me->base);
                s->memberName = me->name;
            } else {
                failAt(line, "слева от '=' должна быть переменная, элемент массива или поле объекта");
            }
            s->value = std::move(rhs);
            return s;
        }

        finishStatement();
        auto s = std::make_unique<ExprStmt>();
        s->line = line; s->expr = std::move(e);
        return s;
    }

    // Разбирает содержимое строкового литерала на фрагменты один раз, во время
    // парсинга — экранирование и [подстановки] здесь же превращаются в готовые
    // ExprPtr. Интерпретатор при каждом PRINT/вычислении больше не токенизирует
    // и не парсит одну и ту же строку заново — это раньше было главным тормозом
    // в циклах с интерполяцией.
    std::vector<StringSegment> buildStringSegments(const std::string& raw, int line) {
        std::vector<StringSegment> segs;
        std::string literalBuf;
        auto flushLiteral = [&]() {
            if (!literalBuf.empty()) {
                segs.push_back(StringSegment{std::move(literalBuf), nullptr});
                literalBuf.clear();
            }
        };

        for (size_t i = 0; i < raw.size(); ++i) {
            char c = raw[i];
            if (c == '\\') {
                if (i + 1 >= raw.size()) { literalBuf += '\\'; break; }
                char n = raw[++i];
                switch (n) {
                    case 'n':  literalBuf += '\n'; break;
                    case 't':  literalBuf += '\t'; break;
                    case 'r':  literalBuf += '\r'; break;
                    case '"':  literalBuf += '"';  break;
                    case '\\': literalBuf += '\\'; break;
                    case '[':  literalBuf += '[';  break;
                    case ']':  literalBuf += ']';  break;
                    default:   literalBuf += '\\'; literalBuf += n;
                }
                continue;
            }

            if (c != '[') { literalBuf += c; continue; }

            // Экранированные пары (\" \\ и т.п.) — часть содержимого, не влияют на глубину/строки.
            int depth = 1;
            size_t j = i + 1;
            bool inStr = false;
            for (; j < raw.size(); ++j) {
                char d = raw[j];
                if (d == '\\' && j + 1 < raw.size()) { j++; continue; }
                if (inStr) {
                    if (d == '"') inStr = false;
                } else if (d == '"') {
                    inStr = true;
                } else if (d == '[') {
                    depth++;
                } else if (d == ']') {
                    if (--depth == 0) break;
                }
            }
            if (depth != 0) failAt(line, "в строке нет закрывающей ']' для подстановки [выражения]");

            std::string sub = raw.substr(i + 1, j - i - 1);
            // Разэкранируем \" -> " и \\ -> \, чтобы вложенные строки внутри [выражения] были обычным кодом.
            std::string subUnescaped;
            for (size_t k = 0; k < sub.size(); ++k) {
                if (sub[k] == '\\' && k + 1 < sub.size() && (sub[k + 1] == '"' || sub[k + 1] == '\\')) {
                    subUnescaped += sub[k + 1];
                    ++k;
                } else {
                    subUnescaped += sub[k];
                }
            }

            flushLiteral();
            ExprPtr expr;
            try {
                std::vector<Token> toks = tokenize(subUnescaped);
                Parser sub_(toks);
                expr = sub_.parseStandaloneExpr();
            } catch (const UmbrlyError& e) {
                // Вложенный лексер/парсер считает строки внутри изолированного фрагмента
                // (обычно "строка 1") — подменяем префикс на настоящий номер строки в файле.
                std::string w = e.what();
                size_t close = w.rfind("): ");
                if (w.rfind("(строка", 0) == 0 && close != std::string::npos) w = w.substr(close + 3);
                failAt(line, w);
            }
            segs.push_back(StringSegment{std::string(), std::move(expr)});
            i = j;
        }

        flushLiteral();
        if (segs.empty()) segs.push_back(StringSegment{std::string(), nullptr});  // пустая строка ""
        return segs;
    }

    // ---------- Выражения (по возрастанию приоритета) ----------
    // OR -> AND -> NOT -> сравнения -> + - -> * / % -> унарный +/- -> постфикс [] () -> первичное

    ExprPtr parseExpr() { return parseOr(); }

    ExprPtr parseOr() {
        ExprPtr l = parseAnd();
        while (checkIdentKw("OR") || checkPunct("||")) {
            Token op = advance();
            ExprPtr r = parseAnd();
            auto b = std::make_unique<BinaryExpr>();
            b->op = "OR"; b->line = op.line; b->lhs = std::move(l); b->rhs = std::move(r);
            l = std::move(b);
        }
        return l;
    }

    ExprPtr parseAnd() {
        ExprPtr l = parseNot();
        while (checkIdentKw("AND") || checkPunct("&&")) {
            Token op = advance();
            ExprPtr r = parseNot();
            auto b = std::make_unique<BinaryExpr>();
            b->op = "AND"; b->line = op.line; b->lhs = std::move(l); b->rhs = std::move(r);
            l = std::move(b);
        }
        return l;
    }

    ExprPtr parseNot() {
        if (checkIdentKw("NOT") || checkPunct("!")) {
            Token op = advance();
            auto u = std::make_unique<UnaryExpr>();
            u->op = "NOT"; u->line = op.line; u->operand = parseNot();
            return u;
        }
        return parseCmp();
    }

    ExprPtr parseCmp() {
        ExprPtr l = parseAdd();
        static const char* ops[] = { "==", "!=", "<=", ">=", "<", ">" };
        for (const char* op : ops) {
            if (checkPunct(op)) {
                Token t = advance();
                ExprPtr r = parseAdd();
                auto b = std::make_unique<BinaryExpr>();
                b->op = op; b->line = t.line; b->lhs = std::move(l); b->rhs = std::move(r);
                return b;
            }
        }
        return l;
    }

    ExprPtr parseAdd() {
        ExprPtr l = parseMul();
        for (;;) {
            if (checkPunct("+") || checkPunct("-")) {
                Token op = advance();
                ExprPtr r = parseMul();
                auto b = std::make_unique<BinaryExpr>();
                b->op = op.text; b->line = op.line; b->lhs = std::move(l); b->rhs = std::move(r);
                l = std::move(b);
            } else return l;
        }
    }

    ExprPtr parseMul() {
        ExprPtr l = parseUnary();
        for (;;) {
            if (checkPunct("*") || checkPunct("/") || checkPunct("%")) {
                Token op = advance();
                ExprPtr r = parseUnary();
                auto b = std::make_unique<BinaryExpr>();
                b->op = op.text; b->line = op.line; b->lhs = std::move(l); b->rhs = std::move(r);
                l = std::move(b);
            } else return l;
        }
    }

    ExprPtr parseUnary() {
        if (checkPunct("-") || checkPunct("+")) {
            Token op = advance();
            if (op.text == "+") return parseUnary();
            auto u = std::make_unique<UnaryExpr>();
            u->op = "-"; u->line = op.line; u->operand = parseUnary();
            return u;
        }
        return parsePostfix();
    }

    ExprPtr parsePostfix() {
        ExprPtr e = parsePrimary();
        for (;;) {
            if (checkPunct("[")) {
                Token lb = advance();
                ExprPtr idx = parseExpr();
                expectPunct("]");
                auto ie = std::make_unique<IndexExpr>();
                ie->line = lb.line; ie->base = std::move(e); ie->index = std::move(idx);
                e = std::move(ie);
                continue;
            }
            if (checkPunct(".")) {
                Token dot = advance();
                std::string member = expectIdentName();
                if (checkPunct("(")) {
                    advance();
                    auto call = std::make_unique<MethodCallExpr>();
                    call->line = dot.line; call->base = std::move(e); call->method = member;
                    if (!checkPunct(")")) {
                        call->args.push_back(parseExpr());
                        while (checkPunct(",")) { advance(); call->args.push_back(parseExpr()); }
                    }
                    expectPunct(")");
                    e = std::move(call);
                } else {
                    auto mem = std::make_unique<MemberExpr>();
                    mem->line = dot.line; mem->base = std::move(e); mem->name = member;
                    e = std::move(mem);
                }
                continue;
            }
            break;
        }
        return e;
    }

    ExprPtr parsePrimary() {
        const Token t = cur();

        if (t.type == TokType::Punct && t.text == "(") {
            advance();
            ExprPtr e = parseExpr();
            expectPunct(")");
            return e;
        }

        if (t.type == TokType::Punct && t.text == "[") {
            advance();
            auto lit = std::make_unique<ArrayLitExpr>();
            lit->line = t.line;
            if (!checkPunct("]")) {
                lit->items.push_back(parseExpr());
                while (checkPunct(",")) { advance(); lit->items.push_back(parseExpr()); }
            }
            expectPunct("]");
            return lit;
        }

        if (t.type == TokType::StringLit) {
            advance();
            auto e = std::make_unique<StringLitExpr>();
            e->segments = buildStringSegments(t.text, t.line);
            e->line = t.line;
            return e;
        }
        if (t.type == TokType::IntLit) {
            advance();
            auto e = std::make_unique<IntLitExpr>(t.ival);
            e->line = t.line;
            return e;
        }
        if (t.type == TokType::FloatLit) {
            advance();
            auto e = std::make_unique<FloatLitExpr>(t.fval);
            e->line = t.line;
            return e;
        }

        if (t.type == TokType::Ident) {
            if (t.text == "TRUE")  { advance(); auto e = std::make_unique<BoolLitExpr>(true);  e->line = t.line; return e; }
            if (t.text == "FALSE") { advance(); auto e = std::make_unique<BoolLitExpr>(false); e->line = t.line; return e; }
            if (t.text == "NIL")   { advance(); auto e = std::make_unique<NilLitExpr>();       e->line = t.line; return e; }

            bool isCall = pos_ + 1 < toks_.size() &&
                          toks_[pos_ + 1].type == TokType::Punct && toks_[pos_ + 1].text == "(";
            if (isCall) {
                advance();  // имя
                advance();  // (
                auto call = std::make_unique<CallExpr>();
                call->name = t.text; call->line = t.line;
                if (!checkPunct(")")) {
                    call->args.push_back(parseExpr());
                    while (checkPunct(",")) { advance(); call->args.push_back(parseExpr()); }
                }
                expectPunct(")");
                return call;
            }

            if (statementKeywords().count(t.text))
                failAt(t.line, "неожиданное ключевое слово '" + t.text + "' внутри выражения");

            advance();
            auto e = std::make_unique<NameExpr>(t.text);
            e->line = t.line;
            return e;
        }

        failAt(t.line, "ожидалось выражение, получено: '" + (t.type == TokType::Eof ? std::string("<конец файла>") : t.text) + "'");
    }
};

}  // namespace

Block parseProgram(const std::vector<Token>& tokens) {
    Parser p(tokens);
    return p.parseProgram();
}

}  // namespace umbrly
