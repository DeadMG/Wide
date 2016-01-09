#include <Wide/CAPI/Lexer.h>
#include <Wide/Lexer/LexerError.h>

extern "C" DLLEXPORT void LexWide(
    void* con,
    std::add_pointer<CEquivalents::OptionalChar(void*)>::type curr,
    std::add_pointer<bool(CEquivalents::Range, const char*, Wide::Lexer::TokenType, Wide::Lexer::Invocation*, void*)>::type token,
    std::add_pointer<void(CEquivalents::Range, void*)>::type comment,
    std::add_pointer<bool(CEquivalents::Position, Wide::Lexer::Failure, void*)>::type err,
    const char* filename
) {
    CEquivalents::LexerRange range;
    range.curr = curr;
    range.context = con;

    CEquivalents::LexerBody p(range, std::make_shared<std::string>(filename));

    p.inv.OnComment = [=](Wide::Lexer::Range r) {
        CEquivalents::Range debug = r;
        if (comment)
            comment(debug, con);
    };

    p.inv.OnError = [=, &p](Wide::Lexer::Error error) -> Wide::Util::optional<Wide::Lexer::Token> {
        if (err)
            if (err(error.Where, error.What, con))
                return Wide::Util::none;
        return p.inv();
    };
        
    while(auto tok = p.inv()) { 
        if (token(tok->GetLocation(), tok->GetValue().c_str(), tok->GetType(), &p.inv, con))
            break;
    }
}

extern "C" DLLEXPORT bool IsKeyword(Wide::Lexer::Invocation* inv, Wide::Lexer::TokenType ty) {
    return inv->KeywordTypes.find(ty) != inv->KeywordTypes.end();
}
// TODO: Make these data structure lookups.
extern "C" DLLEXPORT bool IsLiteral(Wide::Lexer::Invocation* inv, Wide::Lexer::TokenType ty) {
    return ty == &Wide::Lexer::TokenTypes::String || ty == &Wide::Lexer::TokenTypes::Integer;
}
extern "C" DLLEXPORT CEquivalents::BracketType GetBracketType(Wide::Lexer::Invocation* inv, Wide::Lexer::TokenType ty) {
    if (ty == &Wide::Lexer::TokenTypes::OpenBracket || ty == &Wide::Lexer::TokenTypes::OpenSquareBracket || ty == &Wide::Lexer::TokenTypes::OpenCurlyBracket)
        return CEquivalents::BracketType::Open;
    if (ty == &Wide::Lexer::TokenTypes::CloseBracket || ty == &Wide::Lexer::TokenTypes::CloseSquareBracket || ty == &Wide::Lexer::TokenTypes::CloseCurlyBracket)
        return CEquivalents::BracketType::Close;
    return CEquivalents::BracketType::None;
}
extern "C" DLLEXPORT int GetBracketNumber(Wide::Lexer::Invocation* inv, Wide::Lexer::TokenType ty) {
    if (ty == &Wide::Lexer::TokenTypes::OpenBracket || ty == &Wide::Lexer::TokenTypes::CloseBracket)
        return 0;
    if (ty == &Wide::Lexer::TokenTypes::OpenCurlyBracket || ty == &Wide::Lexer::TokenTypes::CloseCurlyBracket)
        return 1;
    if (ty == &Wide::Lexer::TokenTypes::OpenSquareBracket || ty == &Wide::Lexer::TokenTypes::CloseSquareBracket)
        return 2;
    return 0;
}