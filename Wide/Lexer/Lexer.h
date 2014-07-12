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
        class Invocation {
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
                        current_position.column += tabsize;
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
                    OnComment(start + current_position);
                    return (*this)();
                }
                return OnError(start, Failure::UnterminatedComment, this);
            }
        public:
            enum Failure {
                UnterminatedStringLiteral,
                UnlexableCharacter,
                UnterminatedComment
            };
            std::function<Wide::Util::optional<char>()> r;
            std::function<Wide::Util::optional<Token>(Position, Failure, Invocation*)> OnError;
            std::function<void(Range)> OnComment;

            std::unordered_map<char, Lexer::TokenType> singles;
            std::unordered_map<char, std::unordered_map<char, Lexer::TokenType>> doubles;
            std::unordered_map<char, std::unordered_map<char, std::unordered_map<char, Lexer::TokenType>>> triples;
            std::unordered_set<char> whitespace;
            std::unordered_map<std::string, Lexer::TokenType> keywords;
            std::unordered_set<Lexer::TokenType> KeywordTypes;
            int tabsize;

            Invocation(std::function<Wide::Util::optional<char>()> range, std::shared_ptr<std::string> name);

            Wide::Util::optional<Lexer::Token> operator()();
        };
    }
}