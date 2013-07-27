#pragma once

#include <Wide/Util/Ranges/Optional.h>
#include <Wide/Lexer/Token.h>
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
            static const std::unordered_map<char, TokenType> singles;
            static const std::unordered_map<char, std::unordered_map<char, TokenType>> doubles;
            static const std::unordered_set<char> whitespace;
            static const std::unordered_map<std::string, TokenType> keywords;
            static const std::unordered_set<TokenType> KeywordTypes;
            std::function<void(Range)> OnComment;
            int tabsize;
            Arguments()
                : tabsize(4) 
            {
                OnComment = [](Range) {};
            }
        };
        template<typename Range> class Invocation {
            const Arguments* args;
            Position current_position;
            std::deque<Token> token_putbacks;
            Wide::Util::optional<std::pair<char, Position>> putback;
            Lexer::Range lastpos;

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
                return OnError(start, Arguments::Failure::UnterminatedComment, this);
            }

        public:
            Range r;
            std::function<Wide::Util::optional<Token>(Position, Arguments::Failure, Invocation*)> OnError;

            // Used only for some error handling in the parser
            Lexer::Range GetLastPosition() {
                return lastpos;
            }

            Invocation(const Arguments& arg, Range range)
                : args(&arg), r(range) {

                OnError = [](Position, Arguments::Failure, Invocation* self) {
                    return (*self)();
                };
            }

            Wide::Util::optional<Lexer::Token> operator()() {
                if (!token_putbacks.empty()) {
                    auto tok = token_putbacks.back();
                    token_putbacks.pop_back();
                    lastpos = tok.GetLocation();
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
                            // An //comment at the end of the file is NOT an unterminated comment.
                            while(auto val = get()) {
                                if (*val == '\n')
                                    break;
                            }
                            args->OnComment(begin_pos + current_position);
                            return (*this)();
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
                        lastpos = begin_pos;
                        return Wide::Lexer::Token(begin_pos, args->singles.at(*val), std::string(1, *val));
                    };
                    if (args->doubles.find(*val) == args->doubles.end())
                        return this_token();
                    auto old_pos = current_position;
                    auto second = get();
                    if (!second) return this_token();
                    if (args->doubles.at(*val).find(*second) != args->doubles.at(*val).end()) {
                        lastpos = Lexer::Range(begin_pos);
                        return Wide::Lexer::Token(begin_pos, args->doubles.at(*val).at(*second), std::string(1, *val) + std::string(1, *second));
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
                        return OnError(begin_pos, Arguments::Failure::UnterminatedStringLiteral, this);
                    lastpos = begin_pos + current_position;
                    return Wide::Lexer::Token(begin_pos + current_position, TokenType::String, escape(variable_length_value));
                }

                TokenType result = TokenType::Integer;
                if (!((*val >= '0' && *val <= '9') || (*val >= 'a' && *val <= 'z') || (*val >= 'A' && *val <= 'Z') || *val == '_')) {
                    return OnError(begin_pos, Arguments::Failure::UnlexableCharacter, this);
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
                lastpos = begin_pos + current_position;
                if (args->keywords.find(variable_length_value) != args->keywords.end()) {
                    return Wide::Lexer::Token(begin_pos + current_position, args->keywords.at(variable_length_value), variable_length_value);
                }
                return Wide::Lexer::Token(begin_pos + current_position, result, variable_length_value);
            }
        };
    }
}