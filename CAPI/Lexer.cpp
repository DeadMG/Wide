#include "Stages/Lexer/Lexer.h"

namespace CEquivalents {
    struct OptionalChar {
        char val;
        bool present;
    };
    struct LexerRange {
        void* context;
        std::add_pointer<CEquivalents::OptionalChar(void*)>::type curr;
        Wide::Util::optional<char> operator()() {
            auto val = curr(context);
            if (val.present)
                return val.val;
            return Wide::Util::none;
        }
    };
    struct Position {
        unsigned column;
        unsigned line;
        unsigned offset;
    };
    struct Range {
        Position begin, end;
    };

    struct Token {
        Range location;
        Wide::Lexer::TokenType type;
        char* value;
    };
    struct LexerResult {
        Token t;
        bool exists;
    };
    struct LexerBody {
        LexerBody(LexerRange r)
            : inv(args, r) {}
        Wide::Lexer::Arguments args;
        Wide::Lexer::Invocation<LexerRange> inv;
    };
}

extern "C" __declspec(dllexport) CEquivalents::LexerBody* CreateLexer(
    std::add_pointer<CEquivalents::OptionalChar(void*)>::type curr,
    void* con,
    std::add_pointer<void(Wide::Lexer::Range)>::type comment,
    std::add_pointer<CEquivalents::Token(CEquivalents::Position, Wide::Lexer::Arguments::Failure)>::type err
) {
    CEquivalents::LexerRange range;
    range.curr = curr;
    range.context = con;

    auto p = new CEquivalents::LexerBody(range);

    p->args.OnComment = [=](Wide::Lexer::Range r) {
        comment(r);
    };

    p->args.OnError = [=](Wide::Lexer::Position p, Wide::Lexer::Arguments::Failure f) -> Wide::Lexer::Token {
        CEquivalents::Position pos;
        pos.column = p.column;
        pos.line = p.line;
        pos.offset = p.offset;
        auto out = err(pos, f);
        if (!out.value)
            out.value = "";

        Wide::Lexer::Range r;
        r.begin.column = out.location.begin.column;
        r.begin.line = out.location.begin.line;
        r.begin.offset = out.location.begin.offset;

        r.end.column = out.location.end.column;
        r.end.line = out.location.end.line;
        r.end.offset = out.location.end.offset;

        return Wide::Lexer::Token(r, out.type, out.value);
    };
    
    return p;
}

extern "C" __declspec(dllexport) Wide::Lexer::Token* GetToken(CEquivalents::LexerBody* lexer) {
    auto tok = lexer->inv();
    if (!tok)
        return nullptr;
    return new Wide::Lexer::Token(std::move(*tok));
}

extern "C" __declspec(dllexport) const char* GetTokenValue(Wide::Lexer::Token* in) {
    return in->GetValue().c_str();
}

extern "C" __declspec(dllexport) unsigned GetTokenValueSize(Wide::Lexer::Token* in) {
    return in->GetValue().size();
}

extern "C" __declspec(dllexport) CEquivalents::Range GetTokenLocation(Wide::Lexer::Token* in) {
    CEquivalents::Range r;
    auto&& location = in->GetLocation();
    r.begin.column = location.begin.column;
    r.begin.line = location.begin.line;
    r.begin.offset = location.begin.offset;

    r.end.column = location.end.line;
    r.end.line = location.end.line;
    r.end.offset = location.end.offset;
    return r;
}

extern "C"__declspec(dllexport) Wide::Lexer::TokenType GetTokenType(Wide::Lexer::Token* in) {
    return in->GetType();
}

extern "C" __declspec(dllexport) void DeleteToken(Wide::Lexer::Token* in) {
    delete in;
}

extern "C" __declspec(dllexport) void DeleteLexer(CEquivalents::LexerBody* in) {
    delete in;
}

extern "C" __declspec(dllexport) bool IsKeywordType(CEquivalents::LexerBody* lexer, Wide::Lexer::TokenType ty) {
    return lexer->args.KeywordTypes.find(ty) != lexer->args.KeywordTypes.end();
}

extern "C" __declspec(dllexport) void ClearLexerState(CEquivalents::LexerBody* lexer) {
    lexer->inv.clear();
}