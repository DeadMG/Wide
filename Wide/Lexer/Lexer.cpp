#include <Wide/Lexer/Lexer.h>
#include <Wide/Lexer/LexerError.h>
#include <Wide/Util/Ranges/StringRange.h>

using namespace Wide;
using namespace Lexer;

Lexer::Invocation::Invocation(std::function<Wide::Util::optional<char>()> range, Position name)
: r(range), current_position(name) {
    singles = default_singles;
    doubles = default_doubles;
    triples = default_triples;
    whitespace = default_whitespace;
    keywords = default_keywords;
    KeywordTypes = default_keyword_types;

    OnError = [=](Error self) {
        return (*this)();
    };
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
                std::string comment = "";
                while (auto val = get()) {
                    if (*val == '\n')
                        break;
                    comment.push_back(*val);
                }
                return Lexer::Token(begin_pos + current_position, &Lexer::TokenTypes::Comment, comment);
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
                    return OnError({ current_position, Failure::UnterminatedStringLiteral });
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
            return OnError({ begin_pos, Failure::UnterminatedStringLiteral });
        return Wide::Lexer::Token(begin_pos + current_position, &TokenTypes::String, escape(variable_length_value));
    }

    TokenType result = &TokenTypes::Integer;
    auto old_pos = current_position;
    if (*val == '@') {
        result = &TokenTypes::Identifier;
        val = get();
    } else if (!((*val >= '0' && *val <= '9') || (*val >= 'a' && *val <= 'z') || (*val >= 'A' && *val <= 'Z') || *val == '_')) {
        return OnError({ begin_pos, Failure::UnlexableCharacter });
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
std::string Lexer::Invocation::escape(std::string val) {
    std::string result;
    for (auto begin = val.begin(); begin != val.end(); ++begin) {
        if (*begin == '\\' && begin + 1 != val.end()) {
            switch (*(begin + 1)) {
            case 'n':
                result.push_back('\n');
                ++begin;
                continue;
            case 'r':
                result.push_back('\r');
                ++begin;
                continue;
            case 't':
                result.push_back('\t');
                ++begin;
                continue;
            case '"':
                result.push_back('\"');
                ++begin;
                continue;
            }
        }
        result.push_back(*begin);
    }
    return result;
}
Wide::Util::optional<char> Lexer::Invocation::get() {
    if (!putback.empty()) {
        auto out = putback.back();
        putback.pop_back();
        current_position = out.second;
        return out.first;
    }
    auto val = r();
    if (val) {
        switch (*val) {
        case '\n':
            current_position.column = 1;
            current_position.line++;
            break;
        case '\t':
            current_position.column += tabsize;
            break;
        default:
            current_position.column++;
        }
        current_position.offset++;
    }
    return val;
}

Wide::Util::optional<Lexer::Token> Lexer::Invocation::ParseCComments(Position start) {
    // Already saw /*
    unsigned num = 1;
    std::string comment = "";
    while (auto val = get()) {
        if (*val == '*') {
            val = get();
            if (!val) break;
            if (*val == '/') {
                --num;
                if (num == 0)
                    break;
            }
        }
        if (*val == '/') {
            val = get();
            if (!val) break;
            if (*val == '*')
                ++num;
        }
        comment.push_back(*val);
    }
    if (num == 0) {
        Lexer::Token tok(start + current_position, &Lexer::TokenTypes::Comment, comment);
        return tok;
    }
    return OnError({ start, Failure::UnterminatedComment });
}

const std::unordered_map<char, Lexer::TokenType> Lexer::default_singles = {
    { '+', &TokenTypes::Plus },
    { '.', &TokenTypes::Dot },
    { '-', &TokenTypes::Minus },
    { ',', &TokenTypes::Comma },
    { ';', &TokenTypes::Semicolon },
    { '~', &TokenTypes::Negate },
    { ')', &TokenTypes::CloseBracket },
    { '(', &TokenTypes::OpenBracket },
    { ']', &TokenTypes::CloseSquareBracket },
    { '[', &TokenTypes::OpenSquareBracket },
    { '{', &TokenTypes::OpenCurlyBracket },
    { '}', &TokenTypes::CloseCurlyBracket },
    { '>', &TokenTypes::GT },
    { '<', &TokenTypes::LT },
    { '&', &TokenTypes::And },
    { '|', &TokenTypes::Or },
    { '*', &TokenTypes::Star },
    { '%', &TokenTypes::Modulo },
    { '=', &TokenTypes::Assignment },
    { '!', &TokenTypes::Exclaim },
    { '/', &TokenTypes::Divide },
    { '^', &TokenTypes::Xor },
    { ':', &TokenTypes::Colon },
    { '?', &TokenTypes::QuestionMark },
};

const std::unordered_map<char, std::unordered_map<char, Lexer::TokenType>> Lexer::default_doubles = {
    { '-', { { '-', &TokenTypes::Decrement }, { '>', &TokenTypes::PointerAccess }, { '=', &TokenTypes::MinusAssign } } },
    { '+', { { '=', &TokenTypes::PlusAssign }, { '+', &TokenTypes::Increment } } },
    { '>', { { '=', &TokenTypes::GTE }, { '>', &TokenTypes::RightShift } } },
    { '<', { { '<', &TokenTypes::LeftShift }, { '=', &TokenTypes::LTE } } },
    { '=', { { '=', &TokenTypes::EqCmp }, { '>', &TokenTypes::Lambda } } },
    { '&', { { '=', &TokenTypes::AndAssign }, { '&', &TokenTypes::DoubleAnd } } },
    { '|', { { '=', &TokenTypes::OrAssign }, { '|', &TokenTypes::DoubleOr } } },
    { '*', { { '=', &TokenTypes::MulAssign } } },
    { '%', { { '=', &TokenTypes::ModAssign } } },
    { '~', { { '=', &TokenTypes::NotEqCmp } } },
    { '/', { { '=', &TokenTypes::DivAssign } } },
    { '^', { { '=', &TokenTypes::XorAssign } } },
    { ':', { { '=', &TokenTypes::VarCreate } } },
}; 

const std::unordered_map<char, std::unordered_map<char, std::unordered_map<char, Lexer::TokenType>>> Lexer::default_triples = {
    { '>', { { '>', { { '=', &TokenTypes::RightShiftAssign } } } } },
    { '<', { { '<', { { '=', &TokenTypes::LeftShiftAssign } } } } },
    { '.', { { '.', { { '.', &TokenTypes::Ellipsis } } } } },
};

const std::unordered_set<char> Lexer::default_whitespace = {
    '\r',
    '\t',
    '\n',
    ' ',
};

const std::unordered_map<std::string, Lexer::TokenType> Lexer::default_keywords = {
    { "return", &TokenTypes::Return },
    { "using", &TokenTypes::Using },
    { "module", &TokenTypes::Module },
    { "if", &TokenTypes::If },
    { "else", &TokenTypes::Else },
    { "while", &TokenTypes::While },
    { "this", &TokenTypes::This },
    { "type", &TokenTypes::Type },
    { "operator", &TokenTypes::Operator },
    { "function", &TokenTypes::Function },
    { "break", &TokenTypes::Break },
    { "continue", &TokenTypes::Continue },
    { "concept", &TokenTypes::Concept },
    { "template", &TokenTypes::Template },
    { "concept_map", &TokenTypes::ConceptMap },
    { "public", &TokenTypes::Public },
    { "private", &TokenTypes::Private },
    { "protected", &TokenTypes::Protected },
    { "dynamic", &TokenTypes::Dynamic },
    { "decltype", &TokenTypes::Decltype },
    { "true", &TokenTypes::True },
    { "false", &TokenTypes::False },
    { "typeid", &TokenTypes::Typeid },
    { "dynamic_cast", &TokenTypes::DynamicCast },
    { "try", &TokenTypes::Try },
    { "catch", &TokenTypes::Catch },
    { "throw", &TokenTypes::Throw },
    { "delete", &TokenTypes::Delete },
    { "abstract", &TokenTypes::Abstract },
    { "default", &TokenTypes::Default },
    { "import", &TokenTypes::Import },
    { "from", &TokenTypes::From },
    { "hiding", &TokenTypes::Hiding },
};

const std::unordered_set<Lexer::TokenType> Lexer::default_keyword_types = [] {
    std::unordered_set<Lexer::TokenType> KeywordTypes;
    for (auto&& x : default_keywords)
        KeywordTypes.insert(x.second);
    return KeywordTypes;
}();

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
const std::string TokenTypes::DoubleOr = "||";
const std::string TokenTypes::And = "&";
const std::string TokenTypes::DoubleAnd = "&&";
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
const std::string TokenTypes::Import = "import";
const std::string TokenTypes::From = "from";
const std::string TokenTypes::Hiding = "hiding";
const std::string TokenTypes::Comment = "comment";

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
#if defined(__has_include)
#if __has_include(<emscripten/bind.h>)
#include <emscripten/bind.h>
using namespace emscripten;
namespace Wide {
    namespace Lexer {
        val Lex(std::string source) {
            int num = 0;
            auto result = val::array();
            Wide::Lexer::Invocation inv(Wide::Range::StringRange(source), Wide::Lexer::Position(std::make_shared<std::string>("test")));
            inv.OnError = [&](Wide::Lexer::Error err) {
                result.set(num++, err);
                return inv();
            };
            while (auto tok = inv())
                result.set(num++, *tok);
            return result;
        }
        bool IsKeyword(Wide::Lexer::Token& tok) {
            return default_keyword_types.find(tok.GetType()) != default_keyword_types.end();
        }
        bool IsLiteral(Wide::Lexer::Token& tok) {
            return tok.GetType() == &Wide::Lexer::TokenTypes::String || tok.GetType() == &Wide::Lexer::TokenTypes::Integer;
        }
        bool IsComment(Wide::Lexer::Token& tok) {
            return tok.GetType() == &Wide::Lexer::TokenTypes::Comment;
        }
        std::string DescribeError(const Wide::Lexer::Error& err) {
            if (err.What == Failure::UnterminatedStringLiteral)
                return "Unterminated string literal";
            if (err.What == Failure::UnlexableCharacter)
                return "Unlexable character";
            return "Unterminated comment";
        }
        Lexer::Range GetLocation(const Wide::Lexer::Error& err) {
            Lexer::Position end = err.Where;
            end.offset += 1;
            return Lexer::Range(err.Where, end);
        }
    }
}
EMSCRIPTEN_BINDINGS(dunno_what_goes_here) {
    register_vector<Wide::Lexer::Token>("TokenVector");
    class_<Wide::Lexer::Position>("Position")
        .property("line", &Wide::Lexer::Position::line)
        .property("column", &Wide::Lexer::Position::column)
        .property("offset", &Wide::Lexer::Position::offset);
    class_<Wide::Lexer::Range>("Range")
        .property("begin", &Wide::Lexer::Range::begin)
        .property("end", &Wide::Lexer::Range::end);
    class_<Wide::Lexer::Token>("Token")
        .property("where", &Wide::Lexer::Token::GetLocation)
        .function("GetValue", &Wide::Lexer::Token::GetValue)
        .function("IsKeyword", &Wide::Lexer::IsKeyword)
        .function("IsLiteral", &Wide::Lexer::IsLiteral)
        .function("IsComment", &Wide::Lexer::IsComment);
    class_<Wide::Lexer::Error>("Error")
        .property("where", &Wide::Lexer::GetLocation)
        .property("what", &Wide::Lexer::DescribeError);
    function("Lex", &Wide::Lexer::Lex);
}
#endif
#endif