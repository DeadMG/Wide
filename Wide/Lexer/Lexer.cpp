#include <Wide/Lexer/Lexer.h>

using namespace Wide;
using namespace Lexer;

const std::string TokenTypes::OpenBracket = "(";
const std::string TokenTypes::CloseBracket = ")";
const std::string TokenTypes::Dot = ".";
const std::string TokenTypes::Semicolon = ";";
const std::string TokenTypes::Identifier = "identifier";
const std::string TokenTypes::String = "string";
const std::string TokenTypes::LeftShift = "<<";
const std::string TokenTypes::RightShift = ">>";
const std::string TokenTypes::OpenCurlyBracket = "{";
const std::string TokenTypes::CloseCurlyBracket = "}";
const std::string TokenTypes::Return = "return";
const std::string TokenTypes::Assignment = "=";
const std::string TokenTypes::VarCreate = ":=";
const std::string TokenTypes::Comma = ",";
const std::string TokenTypes::Integer = "integer";
const std::string TokenTypes::Using = "using";
const std::string TokenTypes::Module = "module";
const std::string TokenTypes::Break = "break";
const std::string TokenTypes::Continue = "continue";
const std::string TokenTypes::If = "if";
const std::string TokenTypes::Else = "else";
const std::string TokenTypes::EqCmp = "==";
const std::string TokenTypes::Exclaim = "!";
const std::string TokenTypes::While = "while";
const std::string TokenTypes::NotEqCmp = "!=";
const std::string TokenTypes::This = "this";
const std::string TokenTypes::Type = "type";
const std::string TokenTypes::Operator = "operator";
const std::string TokenTypes::Function = "function";
const std::string TokenTypes::OpenSquareBracket = "[";
const std::string TokenTypes::CloseSquareBracket = "]";
const std::string TokenTypes::Colon = ":";
const std::string TokenTypes::Star = "*";
const std::string TokenTypes::PointerAccess = "->";
const std::string TokenTypes::Negate = "~";
const std::string TokenTypes::Plus = "+";
const std::string TokenTypes::Increment = "++";
const std::string TokenTypes::Decrement = "--";
const std::string TokenTypes::Minus = "-";
const std::string TokenTypes::LT = "<";
const std::string TokenTypes::LTE = "<=";
const std::string TokenTypes::GT = ">";
const std::string TokenTypes::GTE = ">=";
const std::string TokenTypes::Or = "|";
const std::string TokenTypes::And = "&";
const std::string TokenTypes::Xor = "^";
const std::string TokenTypes::RightShiftAssign = ">>=";
const std::string TokenTypes::LeftShiftAssign = "<<=";
const std::string TokenTypes::MinusAssign = "-=";
const std::string TokenTypes::PlusAssign = "+=";
const std::string TokenTypes::AndAssign = "&=";
const std::string TokenTypes::OrAssign = "|=";
const std::string TokenTypes::MulAssign = "*=";
const std::string TokenTypes::Modulo = "%";
const std::string TokenTypes::ModAssign = "%=";
const std::string TokenTypes::Divide = "/";
const std::string TokenTypes::DivAssign = "/=";
const std::string TokenTypes::XorAssign = "^=";
const std::string TokenTypes::Ellipsis = "...";
const std::string TokenTypes::Lambda = "=>";
const std::string TokenTypes::Template = "template";
const std::string TokenTypes::Concept = "concept";
const std::string TokenTypes::ConceptMap = "concept_map";
const std::string TokenTypes::Public = "public";
const std::string TokenTypes::Private = "private";
const std::string TokenTypes::Protected = "protected";
const std::string TokenTypes::Dynamic = "dynamic";
const std::string TokenTypes::Decltype = "decltype";
const std::string TokenTypes::True = "true";
const std::string TokenTypes::False = "false";
const std::string TokenTypes::Typeid = "typeid";
const std::string TokenTypes::DynamicCast = "dynamic_cast";
const std::string TokenTypes::Try = "try";
const std::string TokenTypes::Catch = "catch";
const std::string TokenTypes::Throw = "throw";
const std::string TokenTypes::QuestionMark = "?";
const std::string TokenTypes::Abstract = "abstract";
const std::string TokenTypes::Delete = "delete";
const std::string TokenTypes::Default = "default";

Lexer::Invocation::Invocation(std::function<Wide::Util::optional<char>()> range, std::shared_ptr<std::string> name) 
: r(range), current_position(name) {
    singles['+'] = &TokenTypes::Plus;
    singles['.'] = &TokenTypes::Dot;
    singles['-'] = &TokenTypes::Minus;
    singles[','] = &TokenTypes::Comma;
    singles[';'] = &TokenTypes::Semicolon;
    singles['~'] = &TokenTypes::Negate;
    singles[')'] = &TokenTypes::CloseBracket;
    singles['('] = &TokenTypes::OpenBracket;
    singles[']'] = &TokenTypes::CloseSquareBracket;
    singles['['] = &TokenTypes::OpenSquareBracket;
    singles['{'] = &TokenTypes::OpenCurlyBracket;
    singles['}'] = &TokenTypes::CloseCurlyBracket;
    singles['>'] = &TokenTypes::GT;
    singles['<'] = &TokenTypes::LT;
    singles['&'] = &TokenTypes::And;
    singles['|'] = &TokenTypes::Or;
    singles['*'] = &TokenTypes::Star;
    singles['%'] = &TokenTypes::Modulo;
    singles['='] = &TokenTypes::Assignment;
    singles['!'] = &TokenTypes::Exclaim;
    singles['/'] = &TokenTypes::Divide;
    singles['^'] = &TokenTypes::Xor;
    singles[':'] = &TokenTypes::Colon;
    singles['?'] = &TokenTypes::QuestionMark;

    doubles['+']['+'] = &TokenTypes::Increment;
    doubles['-']['-'] = &TokenTypes::Decrement;
    doubles['>']['>'] = &TokenTypes::RightShift;
    doubles['<']['<'] = &TokenTypes::LeftShift;
    doubles['-']['='] = &TokenTypes::MinusAssign;
    doubles['+']['='] = &TokenTypes::PlusAssign;
    doubles['-']['>'] = &TokenTypes::PointerAccess;
    doubles['&']['='] = &TokenTypes::AndAssign;
    doubles['|']['='] = &TokenTypes::OrAssign;
    doubles['*']['='] = &TokenTypes::MulAssign;
    doubles['%']['='] = &TokenTypes::ModAssign;
    doubles['=']['='] = &TokenTypes::EqCmp;
    doubles['~']['='] = &TokenTypes::NotEqCmp;
    doubles['/']['='] = &TokenTypes::DivAssign;
    doubles['^']['='] = &TokenTypes::XorAssign;
    doubles['<']['='] = &TokenTypes::LTE;
    doubles['>']['='] = &TokenTypes::GTE;
    doubles[':']['='] = &TokenTypes::VarCreate;
    doubles['=']['>'] = &TokenTypes::Lambda;

    triples['>']['>']['='] = &TokenTypes::RightShiftAssign;
    triples['<']['<']['='] = &TokenTypes::LeftShiftAssign;
    triples['.']['.']['.'] = &TokenTypes::Ellipsis;

    whitespace.insert('\r');
    whitespace.insert('\t');
    whitespace.insert('\n');
    whitespace.insert(' ');

    keywords["return"] = &TokenTypes::Return;
    keywords["using"] = &TokenTypes::Using;
    keywords["module"] = &TokenTypes::Module;
    keywords["if"] = &TokenTypes::If;
    keywords["else"] = &TokenTypes::Else;
    keywords["while"] = &TokenTypes::While;
    keywords["this"] = &TokenTypes::This;
    keywords["type"] = &TokenTypes::Type;
    keywords["operator"] = &TokenTypes::Operator;
    keywords["function"] = &TokenTypes::Function;
    keywords["break"] = &TokenTypes::Break;
    keywords["continue"] = &TokenTypes::Continue;
    keywords["concept"] = &TokenTypes::Concept;
    keywords["template"] = &TokenTypes::Template;
    keywords["concept_map"] = &TokenTypes::ConceptMap;
    keywords["public"] = &TokenTypes::Public;
    keywords["private"] = &TokenTypes::Private;
    keywords["protected"] = &TokenTypes::Protected;
    keywords["dynamic"] = &TokenTypes::Dynamic;
    keywords["decltype"] = &TokenTypes::Decltype;
    keywords["true"] = &TokenTypes::True;
    keywords["false"] = &TokenTypes::False;
    keywords["typeid"] = &TokenTypes::Typeid;
    keywords["dynamic_cast"] = &TokenTypes::DynamicCast;
    keywords["try"] = &TokenTypes::Try;
    keywords["catch"] = &TokenTypes::Catch;
    keywords["throw"] = &TokenTypes::Throw;
    keywords["delete"] = &TokenTypes::Delete;
    keywords["abstract"] = &TokenTypes::Abstract;
    keywords["default"] = &TokenTypes::Default;

    for (auto&& x : keywords)
        KeywordTypes.insert(x.second);

    OnComment = [](Range) {};

    OnError = [](Position, Failure, Invocation* self) {
        return (*self)();
    };
}

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
Wide::Util::optional<Lexer::Token> Invocation::operator()() {
    auto begin_pos = current_position;
    auto val = get();
    if (!val) return Wide::Util::none;

    if (whitespace.find(*val) != whitespace.end())
        return (*this)();

    if (*val == '/') {
        auto old_pos = current_position;
        auto next = get();
        if (next && *next == '/' || *next == '*') {
            if (*next == '/') {
                // An //comment at the end of the file is NOT an unterminated comment.
                while (auto val = get()) {
                    if (*val == '\n')
                        break;
                }
                OnComment(begin_pos + current_position);
                return (*this)();
            }
            if (*next == '*') {
                return ParseCComments(begin_pos);
            }
        } else {
            if (next) {
                putback.push_back(std::make_pair(*next, current_position));
                current_position = old_pos;
            }
        }
    }

    {
        auto firstpos = current_position;
        auto second = get();
        if (second) {
            auto secpos = current_position;
            auto third = get();
            if (third) {
                if (triples.find(*val) != triples.end()) {
                    if (triples.at(*val).find(*second) != triples.at(*val).end()) {
                        if (triples.at(*val).at(*second).find(*third) != triples.at(*val).at(*second).end()) {
                            return Wide::Lexer::Token(begin_pos + current_position, triples.at(*val).at(*second).at(*third), std::string(1, *val) + std::string(1, *second) + std::string(1, *third));
                        }
                    }
                }
                putback.push_back(std::make_pair(*third, current_position));
                current_position = secpos;
            }
            if (doubles.find(*val) != doubles.end()) {
                if (doubles.at(*val).find(*second) != doubles.at(*val).end()) {
                    return Wide::Lexer::Token(begin_pos + current_position, doubles.at(*val).at(*second), std::string(1, *val) + std::string(1, *second));
                }
            }
            putback.push_back(std::make_pair(*second, current_position));
            current_position = firstpos;
        }
        if (singles.find(*val) != singles.end()) {
            return Wide::Lexer::Token(begin_pos + current_position, singles.at(*val), std::string(1, *val));
        }
    }

    // Variable-length tokens.
    std::string variable_length_value;
    if (*val == '"') {
        Wide::Util::optional<char> next;
        while (next = get()) {
            if (*next == '"')
                break;
            if (*next == '\\') {
                auto very_next = get();
                if (!very_next)
                    return OnError(current_position, Failure::UnterminatedStringLiteral, this);
                if (*very_next == '"') {
                    variable_length_value.push_back('"');
                    continue;
                }
                variable_length_value.push_back(*next);
                variable_length_value.push_back(*very_next);
                continue;
            }
            variable_length_value.push_back(*next);
        }
        if (!next)
            return OnError(begin_pos, Failure::UnterminatedStringLiteral, this);
        return Wide::Lexer::Token(begin_pos + current_position, &TokenTypes::String, escape(variable_length_value));
    }

    TokenType result = &TokenTypes::Integer;
    auto old_pos = current_position;
    if (*val == '@') {
        result = &TokenTypes::Identifier;
        val = get();
    } else if (!((*val >= '0' && *val <= '9') || (*val >= 'a' && *val <= 'z') || (*val >= 'A' && *val <= 'Z') || *val == '_')) {
        return OnError(begin_pos, Failure::UnlexableCharacter, this);
    }
    while (val) {
        if (*val < '0' || *val > '9')
        if ((*val >= 'a' && *val <= 'z') || (*val >= 'A' && *val <= 'Z') || *val == '_')
            result = &TokenTypes::Identifier;
        else {
            putback.push_back(std::make_pair(*val, current_position));
            current_position = old_pos;
            break;
        }
        variable_length_value.push_back(*val);
        old_pos = current_position;
        val = get();
    }
    auto lastpos = begin_pos + current_position;
    if (keywords.find(variable_length_value) != keywords.end()) {
        return Wide::Lexer::Token(lastpos, keywords.at(variable_length_value), variable_length_value);
    }
    return Wide::Lexer::Token(lastpos, result, variable_length_value);
}