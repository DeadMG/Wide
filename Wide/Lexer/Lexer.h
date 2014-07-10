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
        class Arguments {
        public:
            enum Failure {
                UnterminatedStringLiteral,
                UnlexableCharacter,
                UnterminatedComment
            };
            std::unordered_map<char, Lexer::TokenType> singles;
            std::unordered_map<char, std::unordered_map<char, Lexer::TokenType>> doubles;
            std::unordered_map<char, std::unordered_map<char, std::unordered_map<char, Lexer::TokenType>>> triples;
            std::unordered_set<char> whitespace;
            std::unordered_map<std::string, const std::string*> keywords;
            std::unordered_set<Lexer::TokenType> KeywordTypes;
            std::function<void(Range)> OnComment;
            int tabsize;
            Arguments();
        };
        template<typename Range> class Invocation {
            const Arguments* args;
            Position current_position;
            std::deque<std::pair<char, Position>> putback;

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

            Wide::Util::optional<char> get() {
                if (!putback.empty()) {
                    auto out = putback.back();
                    putback.pop_back();
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
            Invocation(const Arguments& arg, Range range, std::shared_ptr<std::string> name)
                : args(&arg), r(range), current_position(name) {

                OnError = [](Position, Arguments::Failure, Invocation* self) {
                    return (*self)();
                };
            }

            Wide::Util::optional<Lexer::Token> operator()() {                
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
                            if (args->triples.find(*val) != args->triples.end()) {
                                if (args->triples.at(*val).find(*second) != args->triples.at(*val).end()) {
                                    if (args->triples.at(*val).at(*second).find(*third) != args->triples.at(*val).at(*second).end()) {
                                        return Wide::Lexer::Token(begin_pos + current_position, args->triples.at(*val).at(*second).at(*third), std::string(1, *val) + std::string(1, *second) + std::string(1, *third));
                                    }
                                }
                            }
                            putback.push_back(std::make_pair(*third, current_position));
                            current_position = secpos;
                        }
                        if (args->doubles.find(*val) != args->doubles.end()) {
                            if (args->doubles.at(*val).find(*second) != args->doubles.at(*val).end()) {
                                return Wide::Lexer::Token(begin_pos + current_position, args->doubles.at(*val).at(*second), std::string(1, *val) + std::string(1, *second));
                            }
                        }
                        putback.push_back(std::make_pair(*second, current_position));
                        current_position = firstpos;
                    }
                    if (args->singles.find(*val) != args->singles.end()) {
                        return Wide::Lexer::Token(begin_pos + current_position, args->singles.at(*val), std::string(1, *val));
                    }
                }

                // Variable-length tokens.
                std::string variable_length_value;
                if (*val == '"') {
                    Wide::Util::optional<char> next;
                    while(next = get()) {
                        if (*next == '"')
                            break;
                        if (*next == '\\') {
                            auto very_next = get();
                            if (!very_next)
                                return OnError(current_position, Arguments::Failure::UnterminatedStringLiteral, this);
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
                        return OnError(begin_pos, Arguments::Failure::UnterminatedStringLiteral, this);
                    return Wide::Lexer::Token(begin_pos + current_position, &TokenTypes::String, escape(variable_length_value));
                }

                TokenType result = &TokenTypes::Integer;
                auto old_pos = current_position;
                if (*val == '@') {
                    result = &TokenTypes::Identifier;
                    val = get();
                } else if (!((*val >= '0' && *val <= '9') || (*val >= 'a' && *val <= 'z') || (*val >= 'A' && *val <= 'Z') || *val == '_')) {
                    return OnError(begin_pos, Arguments::Failure::UnlexableCharacter, this);
                }
                while(val) {
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
                if (args->keywords.find(variable_length_value) != args->keywords.end()) {
                    return Wide::Lexer::Token(lastpos, args->keywords.at(variable_length_value), variable_length_value);
                }
                return Wide::Lexer::Token(lastpos, result, variable_length_value);
            }
        };
    }
}