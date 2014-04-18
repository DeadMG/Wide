#include <Wide/Lexer/Lexer.h>

using namespace Wide;
using namespace Lexer;

const std::unordered_map<char, TokenType> Arguments::singles = []() -> std::unordered_map<char, TokenType> {
    std::unordered_map<char, TokenType> singles;
    singles['+'] = TokenType::Plus;
    singles['.'] = TokenType::Dot;
    singles['-'] = TokenType::Minus;
    singles[','] = TokenType::Comma;
    singles[';'] = TokenType::Semicolon;
    singles['~'] = TokenType::Negate;
    singles[')'] = TokenType::CloseBracket;
    singles['('] = TokenType::OpenBracket;
    singles[']'] = TokenType::CloseSquareBracket;
    singles['['] = TokenType::OpenSquareBracket;
    singles['{'] = TokenType::OpenCurlyBracket;
    singles['}'] = TokenType::CloseCurlyBracket;
    singles['>'] = TokenType::GT;
    singles['<'] = TokenType::LT;
    singles['&'] = TokenType::And;
    singles['|'] = TokenType::Or;
    singles['*'] = TokenType::Dereference;
    singles['%'] = TokenType::Modulo;
    singles['='] = TokenType::Assignment;
    singles['!'] = TokenType::Exclaim;
    singles['/'] = TokenType::Divide;
    singles['^'] = TokenType::Xor;
    singles[':'] = TokenType::Colon;    
    return singles;
}();

const std::unordered_map<char, std::unordered_map<char, TokenType>> Arguments::doubles = []() -> std::unordered_map<char, std::unordered_map<char, TokenType>> {
    // Aassumes that all doubles lead with a character that is a valid single.
    // If this assumption changes, must modify lexer body.

    std::unordered_map<char, std::unordered_map<char, TokenType>> doubles;
    doubles['+']['+'] = TokenType::Increment;    
    doubles['-']['-'] = TokenType::Decrement;    
    doubles['>']['>'] = TokenType::RightShift; 
    doubles['<']['<'] = TokenType::LeftShift;
    doubles['-']['='] = TokenType::MinusAssign;
    doubles['+']['='] = TokenType::PlusAssign;
    doubles['-']['>'] = TokenType::PointerAccess;
    doubles['&']['='] = TokenType::AndAssign;
    doubles['|']['='] = TokenType::OrAssign;
    doubles['*']['='] = TokenType::MulAssign;
    doubles['%']['='] = TokenType::ModAssign;
    doubles['=']['='] = TokenType::EqCmp;    
    doubles['~']['='] = TokenType::NotEqCmp;    
    doubles['/']['='] = TokenType::DivAssign;
    doubles['^']['='] = TokenType::XorAssign;
    doubles['<']['='] = TokenType::LTE;    
    doubles['>']['='] = TokenType::GTE;    
    doubles[':']['='] = TokenType::VarCreate;    
    doubles['=']['>'] = TokenType::Lambda;
    return doubles;
}();

const std::unordered_map<char, std::unordered_map<char, std::unordered_map<char, TokenType>>> Arguments::triples = []() -> std::unordered_map<char, std::unordered_map<char, std::unordered_map<char, TokenType>>> {
    std::unordered_map<char, std::unordered_map<char, std::unordered_map<char, TokenType>>> triples;
    triples['>']['>']['='] = TokenType::RightShiftAssign;
    triples['<']['<']['='] = TokenType::LeftShiftAssign;
    triples['.']['.']['.'] = TokenType::Ellipsis;
    return triples;
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
    keywords["break"] = TokenType::Break;
    keywords["continue"] = TokenType::Continue;
    keywords["concept"] = TokenType::Concept;
    keywords["template"] = TokenType::Template;
    keywords["concept_map"] = TokenType::ConceptMap;
    keywords["public"] = TokenType::Public;
    keywords["private"] = TokenType::Private;
    keywords["protected"] = TokenType::Protected;
    keywords["dynamic"] = TokenType::Dynamic;
    return keywords;
}();

const std::unordered_set<TokenType> Arguments::KeywordTypes = []() -> std::unordered_set<TokenType> {
    std::unordered_set<TokenType> KeywordTypes;
    for(auto&& x : Arguments::keywords)
        KeywordTypes.insert(x.second);
    return KeywordTypes;
}();

std::string Lexer::to_string(Lexer::Position p) {
    return *p.name + ":" + std::to_string(p.line) + ":" + std::to_string(p.column);
}
std::string Lexer::to_string(Lexer::Range r) {
    return to_string(r.begin) + "-" + std::to_string(r.end.line) + ":" + std::to_string(r.end.column);
}
std::string Lexer::operator+(std::string s, Lexer::Range r) {
    return s + to_string(r);
}
std::string Lexer::operator+(Lexer::Range r, std::string s) {
    return to_string(r) + s;
}