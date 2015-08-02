#pragma once

#include <Wide/Lexer/Lexer.h>

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

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
            , offset(pos.offset)
            , location(pos.name->c_str()) {}
        const char* location;
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
    };

    struct LexerBody {
        LexerBody(LexerRange r, std::shared_ptr<std::string> what)
            : inv(r, std::move(what)) {}
        Wide::Lexer::Invocation inv;
    };
    enum class BracketType : int
    {
        None,
        Open,
        Close
    };
}
