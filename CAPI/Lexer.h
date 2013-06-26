#include "Stages/Lexer/Lexer.h"

namespace CEquivalents {
    struct OptionalChar {
        char val;
        bool present;
    };
    struct LexerRange {
        void* context;
        std::add_pointer<CEquivalents::OptionalChar(void*)>::type curr;
        Wide::Util::optional<char> operator()() {
            auto val = curr(context);
            if (val.present)
                return val.val;
            return Wide::Util::none;
        }
    };
    struct Position {
        Position(Wide::Lexer::Position pos)
            : column(pos.column)
            , line(pos.line)
            , offset(pos.offset) {}

        operator Wide::Lexer::Position() {
            Wide::Lexer::Position p;
            p.column = column;
            p.line = line;
            p.offset = offset;
            return p;
        }
        unsigned column;
        unsigned line;
        unsigned offset;
    };
    struct Range {
        Range(Position first, Position last)
            : begin(first), end(last) {}
        Range(Wide::Lexer::Range r)
            : begin(r.begin), end(r.end) {}
        Position begin, end;
        
        operator Wide::Lexer::Range() {
            return Wide::Lexer::Range(begin, end);
        }
    };
    struct LexerBody {
        LexerBody(LexerRange r)
            : inv(args, r) {}
        Wide::Lexer::Arguments args;
        Wide::Lexer::Invocation<LexerRange> inv;
        std::function<bool(CEquivalents::Range, const char*, Wide::Lexer::TokenType)> TokenCallback;
    };
}