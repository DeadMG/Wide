#pragma once

#include <Wide/Lexer/Token.h>
#include <unordered_map>
#include <vector>
#include <string>

namespace Wide {
    namespace Parse {
        class Error : public std::exception {
            Wide::Lexer::Token previous;
            Wide::Util::optional<Wide::Lexer::Token> unexpected;
            std::unordered_set<Wide::Lexer::TokenType> expected;
            std::string err;
        public:
            Error(Wide::Lexer::Token previous, Wide::Util::optional<Wide::Lexer::Token> error, std::unordered_set<Wide::Lexer::TokenType> expected);
            const char* what() const
#ifndef _MSC_VER
                noexcept
#endif
            {
                return err.c_str();
            };
            Wide::Lexer::Token GetLastValidToken() const;
            Wide::Util::optional<Wide::Lexer::Token> GetInvalidToken() const;
            std::unordered_set<Wide::Lexer::TokenType> GetExpectedTokenTypes();
        };
    }
}
