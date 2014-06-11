#pragma once

#include <unordered_set>
#include <string>
#include <memory>

namespace Wide {
    namespace Lexer {
        enum class TokenType : int {
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
            Break,
            Continue,
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
            Negate,
            Plus,
            Increment,
            Decrement,
            Minus,

            LT,
            LTE,
            GT,
            GTE,
            Or,
            And,
            Xor,
            RightShiftAssign,
            LeftShiftAssign,
            MinusAssign,
            PlusAssign,
            AndAssign,
            OrAssign,
            MulAssign,
            Modulo,
            ModAssign,
            Divide,
            DivAssign,
            XorAssign,
            Ellipsis,
            Lambda,
            Template,
            Concept,
            ConceptMap,
            Public,
            Private,
            Protected,
            Dynamic,
            Decltype,
            True,
            False,
            Typeid,
            DynamicCast,
            Try,
            Catch,
            Throw,
        };
        enum Access {
            Public,
            Protected,
            Private,
        };
        struct Position {
            Position(std::shared_ptr<std::string> where)
                : line(1), column(1), offset(0), name(std::move(where)) {}
            unsigned line;
            unsigned column;
            unsigned offset;
            std::shared_ptr<std::string> name;
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
            Range(std::shared_ptr<std::string> where) : begin(where), end(where) {}
            Range(Position pos)
                : begin(pos), end(pos) {}
            Range(Position lhs, Position rhs)
                : begin(lhs), end(rhs) {}
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
        std::string to_string(Lexer::Position p);
        std::string operator+(std::string, Lexer::Position);
        std::string operator+(Lexer::Position, std::string);
        std::string to_string(Lexer::Range r);
        std::string operator+(std::string, Lexer::Range);
        std::string operator+(Lexer::Range, std::string);
    }
}
namespace std {
    template<> struct hash<Wide::Lexer::TokenType> {
        std::size_t operator()(Wide::Lexer::TokenType ty) const {
            return std::hash<int>()((int)ty);
        }
    };
}