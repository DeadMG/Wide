#include "Lexer.h"

extern "C" __declspec(dllexport) void LexWide(
    void* con,
    std::add_pointer<CEquivalents::OptionalChar(void*)>::type curr,
    std::add_pointer<void(Wide::Lexer::Range, void*)>::type comment,
    std::add_pointer<bool(CEquivalents::Position, Wide::Lexer::Arguments::Failure, void*)>::type err,
    std::add_pointer<bool(CEquivalents::Range, const char*, Wide::Lexer::TokenType, void*)>::type token
) {
    CEquivalents::LexerRange range;
    range.curr = curr;
    range.context = con;

    auto p = CEquivalents::LexerBody(range);

    p.args.OnComment = [=](Wide::Lexer::Range r) {
        comment(r, con);
    };

    p.inv.OnError = [=](Wide::Lexer::Position loc, Wide::Lexer::Arguments::Failure f, decltype(&p.inv) lex) -> Wide::Util::optional<Wide::Lexer::Token> {
        if (err(loc, f, con))
            return Wide::Util::none;
        return (*lex)();
    };

    p.TokenCallback = [=](CEquivalents::Range r, const char* str, Wide::Lexer::TokenType type) {
        return token(r, str, type, con);
    };
    
    while(auto tok = p.inv()) { 
        if (p.TokenCallback(tok->GetLocation(), tok->GetValue().c_str(), tok->GetType()))
            break;
    }
}

extern "C" __declspec(dllexport) bool IsKeywordType(Wide::Lexer::TokenType ty) {
    return Wide::Lexer::Arguments::KeywordTypes.find(ty) != Wide::Lexer::Arguments::KeywordTypes.end();
}