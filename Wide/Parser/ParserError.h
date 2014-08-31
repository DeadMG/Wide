#pragma once

#include <Wide/Lexer/Token.h>
#include <unordered_map>
#include <vector>
#include <string>

namespace Wide {
    namespace Parse {
        class ParserError : public std::exception {
            Wide::Lexer::Token previous;
            Wide::Lexer::Token unexpected;
            std::vector<Wide::Lexer::TokenType> expected;
        public:
            ParserError(Wide::Lexer::Token previous, Wide::Lexer::Token error, std::vector<Wide::Lexer::TokenType> expected)
                : previous(previous), unexpected(error), expected(expected) {}
            const char* what() const
#ifndef _MSC_VER
                noexcept
#endif
                ;
            Wide::Lexer::Token GetLastValidToken();
            Wide::Lexer::Token GetInvalidToken();
            std::vector<Wide::Lexer::TokenType> GetExpectedTokenTypes();
        };
    }
}
