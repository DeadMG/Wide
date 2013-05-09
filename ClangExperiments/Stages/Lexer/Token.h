#pragma once

namespace Wide {
    namespace Lexer {
        enum TokenType {
            OpenBracket,
            CloseBracket,
            Dot,
            Semicolon,
            Identifier,
            String,
            LeftShift,
            RightShift,
            OpenCurlyBracket,
            CloseCurlyBracket,
            Return,
            Assignment,
            VarCreate,
            Comma,
            Integer,
            Using,
            Prolog,
            Module,
            If,
            Else,
            EqCmp,
            Exclaim,
            While,
            NotEqCmp,
            This,
            Type,
            Operator,
            Function,
            OpenSquareBracket,
            CloseSquareBracket,
            Colon,
            Dereference,
            PointerAccess,

            LT,
            LTE,
            GT,
            GTE,
            Or,
            And,
            Xor
        };    
        struct Position {
            Position()
                : line(1), column(1), offset(0) {}
            unsigned line;
            unsigned column;
            unsigned offset;
            bool before(Position other) const {
                return offset < other.offset;
            }
            bool after(Position other) const {
                return offset > other.offset;
            }
        };
        inline bool operator==(Position lhs, Position rhs) {
            return lhs.offset == rhs.offset;
        }
        struct Range {
            Range(Position lhs, Position rhs)
                : begin(lhs), end(rhs) {}
            Range() {}
            Range(Position pos)
                : begin(pos), end(pos) {}
            Position begin, end;
            Range operator+(Range other) const {
                Range out(*this);
                if (begin.after(other.begin)) {
                    // Their beginning is in front of ours- take theirs.
                    out.begin = other.begin;
                }
                if (end.before(other.end)) {
                    // Their end is past ours- take it.
                    out.end = other.end;
                }
                // Implicitly, both of these conditions can of course be true.
                return out;
            }
        };
        inline Range operator+(Position lhs, Position rhs) {
            if (lhs.before(rhs))
                return Range(lhs, rhs);
            else 
                return Range(rhs, lhs);
        }
    }
}