#pragma once

#include "Token.h"
#include "../../Util/Ranges/Optional.h"
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <functional>

namespace Wide {
    namespace Lexer {
        class Token {
            Range location;
            TokenType type;
            std::string value;
        public:
            Token(Range r, TokenType t, std::string val)
                : location(r), type(t), value(std::move(val)) {}
            Range GetLocation() const { return location; }
            TokenType GetType() const { return type; }
            const std::string& GetValue() const { return value; }
        };
        class Arguments {
        public:
            enum Failure {
                UnterminatedStringLiteral,
                UnlexableCharacter,
                UnterminatedComment
            };
            std::unordered_map<char, TokenType> singles;
            std::unordered_map<char, std::unordered_map<char, TokenType>> doubles;
            std::unordered_set<char> whitespace;
            std::unordered_map<std::string, TokenType> keywords;
            std::unordered_set<TokenType> KeywordTypes;
            std::function<Token(Position, Failure)> OnError;
            std::function<void(Range)> OnComment;
            int tabsize;
            Arguments()
                : tabsize(4) 
            {
                OnError = [](Position, Failure) -> Token {
                    throw std::runtime_error("Fuckshitballs.");
                };
                OnComment = [](Range) {};

                // Aassumes that all doubles lead with a character that is a valid single.
                // If this assumption changes, must modify lexer body.
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
                doubles['-']['>'] = TokenType::PointerAccess;        
                doubles['-']['-'] = TokenType::Decrement;
                
                singles['<'] = TokenType::LT;
                doubles['<']['<'] = TokenType::LeftShift;
                doubles['<']['='] = TokenType::LTE;
                
                singles['>'] = TokenType::GT;
                doubles['>']['>'] = TokenType::RightShift;
                doubles['>']['='] = TokenType::GTE;
                
                singles[':'] = TokenType::Colon;
                doubles[':']['='] = TokenType::VarCreate;
                
                singles['='] = TokenType::Assignment;
                doubles['=']['='] = TokenType::EqCmp;
                
                singles['~'] = TokenType::Negate;
                doubles['~']['='] = TokenType::NotEqCmp;
                
                singles['+'] = TokenType::Plus;
                doubles['+']['+'] = TokenType::Increment;
        
                whitespace.insert('\r');
                whitespace.insert('\t');
                whitespace.insert('\n');
                whitespace.insert(' ');
        
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

                for(auto&& x : keywords)
                    KeywordTypes.insert(x.second);
            }
        };
        template<typename Range> class Invocation {
            Arguments* args;
            Range r;
            Position current_position;
            std::vector<Token> token_putbacks;
            Wide::Util::optional<std::pair<char, Position>> putback;

            std::string escape(std::string val) {
                std::string result;
                for(auto begin = val.begin(); begin != val.end(); ++begin) {
                    if (*begin == '\\' && begin + 1 != val.end()) {
                        switch(*(begin + 1)) {
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
                        }
                    }
                    result.push_back(*begin);
                }
                return result;
            }

            Wide::Util::optional<char> get() {
                if (putback) {
                    auto out = *putback;
                    putback = Wide::Util::none;
                    current_position = out.second;
                    return out.first;
                }
                auto val = r();
                if (val) {
                    switch(*val) {
                    case '\n':
                        current_position.column = 1;
                        current_position.line++;
                        break;
                    case '\t':
                        current_position.column += args->tabsize;
                        break;
                    default:
                        current_position.column++;
                    }
                    current_position.offset++;
                }
                return val;
            }

            Wide::Util::optional<Lexer::Token> ParseCComments(Position start) {
                // Already saw /*
                unsigned num = 1;
                while(auto val = get()) {
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
                }
                if (num == 0) {
                    args->OnComment(start + current_position);
                    return (*this)();
                }
                return args->OnError(start, Arguments::Failure::UnterminatedComment);
            }

        public:
            void clear() {
                putback = Wide::Util::none;
                token_putbacks.clear();
                current_position = Position();
            }

            Invocation(Arguments& arg, Range range)
                : args(&arg), r(range) {}

            Wide::Util::optional<Lexer::Token> operator()() {
                if (!token_putbacks.empty()) {
                    auto tok = token_putbacks.back();
                    token_putbacks.pop_back();
                    return tok;
                }
                
                auto begin_pos = current_position;
                auto val = get();
                if (!val) return Wide::Util::none;

                if (args->whitespace.find(*val) != args->whitespace.end())
                    return (*this)();

                if (*val == '/') {
                    auto old_pos = current_position;
                    auto next = get();
                    if (next && *next == '/' || *next == '*') {
                        if (*next == '/') {
                            Wide::Util::optional<char> val;
                            while(val = get()) {
                                if (*val == '\n')
                                    break;
                            }
                            if (val) {
                                args->OnComment(begin_pos + current_position);
                                return (*this)();
                            }
                            return args->OnError(begin_pos, Arguments::Failure::UnterminatedComment);
                        }
                        if (*next == '*') {
                            return ParseCComments(begin_pos);
                        }
                    } else {
                        if (next) {
                           putback = std::make_pair(*next, current_position);
                           current_position = old_pos;
                        }
                    }
                }

                // Aassumes that all doubles lead with a character that is a valid single.
                if (args->singles.find(*val) != args->singles.end()) {
                    auto this_token = [&] {
                        return Wide::Lexer::Token(begin_pos, args->singles[*val], std::string(*val, 1));
                    };
                    if (args->doubles.find(*val) == args->doubles.end())
                        return this_token();
                    auto old_pos = current_position;
                    auto second = get();
                    if (!second) return this_token();
                    if (args->doubles[*val].find(*second) != args->doubles[*val].end()) {
                        return Wide::Lexer::Token(begin_pos, args->doubles[*val][*second], std::string(*val, 1) + std::string(*second, 1));
                    }
                    putback = std::make_pair(*second, current_position);
                    current_position = old_pos;
                    return this_token();
                }

                // Variable-length tokens.
                std::string variable_length_value;
                if (*val == '"') {
                    Wide::Util::optional<char> next;
                    while(next = get()) {
                        if (*next == '"')
                            break;
                        variable_length_value.push_back(*next);
                    }
                    if (!next)
                        return args->OnError(begin_pos, Arguments::Failure::UnterminatedStringLiteral);
                    return Wide::Lexer::Token(begin_pos + current_position, TokenType::String, escape(variable_length_value));
                }

                TokenType result = TokenType::Integer;
                if (!((*val >= '0' && *val <= '9') || (*val >= 'a' && *val <= 'z') || (*val >= 'A' && *val <= 'Z') || *val == '_')) {
                    return args->OnError(begin_pos, Arguments::Failure::UnlexableCharacter);
                }
                auto old_pos = current_position;
                while(val) {
                    if (*val < '0' || *val > '9')
                        if ((*val >= 'a' && *val <= 'z') || (*val >= 'A' && *val <= 'Z') || *val == '_')
                            result = TokenType::Identifier;
                        else {
                            putback = std::make_pair(*val, current_position);
                            current_position = old_pos;
                            break;
                        }
                    variable_length_value.push_back(*val);
                    old_pos = current_position;
                    val = get();
                }
                if (args->keywords.find(variable_length_value) != args->keywords.end()) {
                    return Wide::Lexer::Token(begin_pos + current_position, args->keywords[variable_length_value], variable_length_value);
                }
                return Wide::Lexer::Token(begin_pos + current_position, result, variable_length_value);
            }

            void operator()(Lexer::Token t) {
                token_putbacks.push_back(t);
            }
        };
    }
}