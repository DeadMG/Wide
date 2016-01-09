#pragma once

#include <Wide/Lexer/Token.h>

namespace Wide {
    namespace Lexer {
        enum class Failure {
            UnterminatedStringLiteral,
            UnlexableCharacter,
            UnterminatedComment
        };
        struct Error {
            Lexer::Position Where;
            Failure What;
        };
    }
}