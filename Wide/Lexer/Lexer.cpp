#include <Wide/Lexer/Lexer.h>

using namespace Wide;
using namespace Lexer;

const std::unordered_map<char, TokenType> Arguments::singles = []() -> std::unordered_map<char, TokenType> {
    std::unordered_map<char, TokenType> singles;
    singles[';'] = TokenType::Semicolon;
    singles['.'] = TokenType::Dot;
    singles['}'] = TokenType::CloseCurlyBracket;
    singles['{'] = TokenType::OpenCurlyBracket;
    singles[')'] = TokenType::CloseBracket;
    singles['('] = TokenType::OpenBracket;
    singles[','] = TokenType::Comma;
    singles['!'] = TokenType::Exclaim;
    singles['|'] = TokenType::Or;
    singles['^'] = TokenType::Xor;
    singles['&'] = TokenType::And;
    singles['['] = TokenType::OpenSquareBracket;
    singles[']'] = TokenType::CloseSquareBracket;
    singles['*'] = TokenType::Dereference;
    singles['+'] = TokenType::Plus;
    singles['-'] = TokenType::Minus;
    singles['<'] = TokenType::LT;
    singles['>'] = TokenType::GT;
    singles[':'] = TokenType::Colon;    
    singles['='] = TokenType::Assignment;
    singles['~'] = TokenType::Negate;
    singles['+'] = TokenType::Plus;
    return singles;
}();

const std::unordered_map<char, std::unordered_map<char, TokenType>> Arguments::doubles = []() -> std::unordered_map<char, std::unordered_map<char, TokenType>> {
    // Aassumes that all doubles lead with a character that is a valid single.
    // If this assumption changes, must modify lexer body.

    std::unordered_map<char, std::unordered_map<char, TokenType>> doubles;
    doubles['-']['>'] = TokenType::PointerAccess;        
    doubles['-']['-'] = TokenType::Decrement;    
    doubles['<']['<'] = TokenType::LeftShift;
    doubles['<']['='] = TokenType::LTE;    
    doubles['>']['>'] = TokenType::RightShift;
    doubles['>']['='] = TokenType::GTE;    
    doubles[':']['='] = TokenType::VarCreate;    
    doubles['=']['='] = TokenType::EqCmp;    
    doubles['~']['='] = TokenType::NotEqCmp;    
    doubles['+']['+'] = TokenType::Increment;
    return doubles;
}();

const std::unordered_set<char> Arguments::whitespace = []() -> std::unordered_set<char> {
    std::unordered_set<char> whitespace;
    whitespace.insert('\r');
    whitespace.insert('\t');
    whitespace.insert('\n');
    whitespace.insert(' ');
    return whitespace;
}();

const std::unordered_map<std::string, TokenType> Arguments::keywords = []() -> std::unordered_map<std::string, TokenType> {
    std::unordered_map<std::string, TokenType> keywords;
    keywords["return"] = TokenType::Return;
    keywords["using"] = TokenType::Using;
    keywords["prolog"] = TokenType::Prolog;
    keywords["module"] = TokenType::Module;
    keywords["if"] = TokenType::If;
    keywords["else"] = TokenType::Else;
    keywords["while"] = TokenType::While;
    keywords["this"] = TokenType::This;
    keywords["type"] = TokenType::Type;
    keywords["operator"] = TokenType::Operator;
    keywords["function"] = TokenType::Function;
    return keywords;
}();

const std::unordered_set<TokenType> Arguments::KeywordTypes = []() -> std::unordered_set<TokenType> {
    std::unordered_set<TokenType> KeywordTypes;
    for(auto&& x : Arguments::keywords)
        KeywordTypes.insert(x.second);
    return KeywordTypes;
}();
