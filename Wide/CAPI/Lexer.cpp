#include <Wide/CAPI/Lexer.h>

extern "C" DLLEXPORT void LexWide(
    void* con,
    std::add_pointer<CEquivalents::OptionalChar(void*)>::type curr,
    std::add_pointer<bool(CEquivalents::Range, const char*, Wide::Lexer::TokenType, void*)>::type token,
    std::add_pointer<void(CEquivalents::Range, void*)>::type comment,
    std::add_pointer<bool(CEquivalents::Position, Wide::Lexer::Arguments::Failure, void*)>::type err,
    const char* filename
) {
    CEquivalents::LexerRange range;
    range.curr = curr;
    range.context = con;

    CEquivalents::LexerBody p(range, std::make_shared<std::string>(filename));

    p.args.OnComment = [=](Wide::Lexer::Range r) {
        CEquivalents::Range debug = r;
        if (comment)
            comment(debug, con);
    };

    p.inv.OnError = [=](Wide::Lexer::Position loc, Wide::Lexer::Arguments::Failure f, decltype(&p.inv) lex) -> Wide::Util::optional<Wide::Lexer::Token> {
        if (err)
            if (err(loc, f, con))
                return Wide::Util::none;
        return (*lex)();
    };
        
    while(auto tok = p.inv()) { 
        if (token(tok->GetLocation(), tok->GetValue().c_str(), tok->GetType(), con))
            break;
    }
}

extern "C" DLLEXPORT bool IsKeywordType(Wide::Lexer::TokenType ty) {
    return Wide::Lexer::Arguments::KeywordTypes.find(ty) != Wide::Lexer::Arguments::KeywordTypes.end();
}
