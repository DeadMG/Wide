#pragma once

#include <unordered_set>
#include <string>
#include <memory>
#include <Wide/Util/Hash.h>
#include <assert.h>

// Decorated name length exceeded
#pragma warning(disable : 4503)

namespace Wide {
    namespace Lexer {
        typedef const std::string* TokenType;
        namespace TokenTypes {
            extern const std::string OpenBracket;
            extern const std::string CloseBracket;
            extern const std::string Dot;
            extern const std::string Semicolon;
            extern const std::string Identifier;
            extern const std::string String;
            extern const std::string LeftShift;
            extern const std::string RightShift;
            extern const std::string OpenCurlyBracket;
            extern const std::string CloseCurlyBracket;
            extern const std::string Return;
            extern const std::string Assignment;
            extern const std::string VarCreate;
            extern const std::string Comma;
            extern const std::string Integer;
            extern const std::string Using;
            extern const std::string Module;
            extern const std::string Break;
            extern const std::string Continue;
            extern const std::string If;
            extern const std::string Else;
            extern const std::string EqCmp;
            extern const std::string Exclaim;
            extern const std::string While;
            extern const std::string NotEqCmp;
            extern const std::string This;
            extern const std::string Type;
            extern const std::string Operator;
            extern const std::string Function;
            extern const std::string OpenSquareBracket;
            extern const std::string CloseSquareBracket;
            extern const std::string Colon;
            extern const std::string Star;
            extern const std::string PointerAccess;
            extern const std::string Negate;
            extern const std::string Plus;
            extern const std::string Increment;
            extern const std::string Decrement;
            extern const std::string Minus;
            extern const std::string LT;
            extern const std::string LTE;
            extern const std::string GT;
            extern const std::string GTE;
            extern const std::string Or;
            extern const std::string DoubleOr;
            extern const std::string And;
            extern const std::string DoubleAnd;
            extern const std::string Xor;
            extern const std::string RightShiftAssign;
            extern const std::string LeftShiftAssign;
            extern const std::string MinusAssign;
            extern const std::string PlusAssign;
            extern const std::string AndAssign;
            extern const std::string OrAssign;
            extern const std::string MulAssign;
            extern const std::string Modulo;
            extern const std::string ModAssign;
            extern const std::string Divide;
            extern const std::string DivAssign;
            extern const std::string XorAssign;
            extern const std::string Ellipsis;
            extern const std::string Lambda;
            extern const std::string Template;
            extern const std::string Concept;
            extern const std::string ConceptMap;
            extern const std::string Public;
            extern const std::string Private;
            extern const std::string Protected;
            extern const std::string Dynamic;
            extern const std::string Decltype;
            extern const std::string True;
            extern const std::string False;
            extern const std::string Typeid;
            extern const std::string DynamicCast;
            extern const std::string Try;
            extern const std::string Catch;
            extern const std::string Throw;
            extern const std::string QuestionMark;
            extern const std::string Abstract;
            extern const std::string Delete;
            extern const std::string Default;
            extern const std::string Import;
            extern const std::string From;
            extern const std::string Hiding;
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
        inline bool operator==(const Position& lhs, const Position& rhs) {
            return lhs.name == rhs.name && lhs.offset == rhs.offset;
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
        inline bool operator==(const Range& lhs, const Range& rhs) {
            return lhs.begin == rhs.begin && lhs.end == rhs.end;
        }
    }
}
namespace std {
    template<> struct hash<Wide::Lexer::Position> {
        std::size_t operator()(const Wide::Lexer::Position& pos) const {
            return Wide::Util::hash(Wide::Util::HashPermuter(), pos.offset, pos.name).value;
        }
    };
    template<> struct hash<Wide::Lexer::Range> {
        std::size_t operator()(const Wide::Lexer::Range& r) const {
            return Wide::Util::hash(Wide::Util::HashPermuter(), r.begin, r.end).value;
        }
    };
}
namespace Wide {
    namespace Lexer {
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
        class Token {
            Range location;
            const std::string* type;
            std::string value;
        public:
            Token& operator=(const Token&) = default;
            Token(const Token&) = default;
            Token(Range r, const std::string* t, std::string val)
                : location(r), type(t), value(std::move(val)) 
            {
                assert(value != "");
            }
            Range GetLocation() const { return location; }
            const std::string* GetType() const { return type; }
            const std::string& GetValue() const { return value; }
        };
        inline std::string GetNameForOperator(Lexer::TokenType op) {
            return "operator" + *op;
        }
    }
}