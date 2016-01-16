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
        struct Error;
        class Invocation {
            Position current_position;
            std::deque<std::pair<char, Position>> putback;
            std::function<Wide::Util::optional<char>()> r;

            std::string escape(std::string val);
            Wide::Util::optional<char> get();
            Wide::Util::optional<Lexer::Token> ParseCComments(Position start);
        public:
            std::function<Wide::Util::optional<Token>(Error)> OnError;

            std::unordered_map<char, Lexer::TokenType> singles;
            std::unordered_map<char, std::unordered_map<char, Lexer::TokenType>> doubles;
            std::unordered_map<char, std::unordered_map<char, std::unordered_map<char, Lexer::TokenType>>> triples;
            std::unordered_set<char> whitespace;
            std::unordered_map<std::string, Lexer::TokenType> keywords;
            std::unordered_set<Lexer::TokenType> KeywordTypes;
            int tabsize;

            Invocation(std::function<Wide::Util::optional<char>()> range, Position name);

            Wide::Util::optional<Lexer::Token> operator()();
        };
        extern const std::unordered_map<char, Lexer::TokenType> default_singles;
        extern const std::unordered_map<char, std::unordered_map<char, Lexer::TokenType>> default_doubles;
        extern const std::unordered_map<char, std::unordered_map<char, std::unordered_map<char, Lexer::TokenType>>> default_triples;
        extern const std::unordered_set<char> default_whitespace;
        extern const std::unordered_map<std::string, Lexer::TokenType> default_keywords;
        extern const std::unordered_set<Lexer::TokenType> default_keyword_types;
        extern const int default_tabsize;
    }
}