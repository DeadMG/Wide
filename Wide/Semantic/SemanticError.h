#pragma once

#include <Wide/Lexer/Token.h>
#include <string>
#include <vector>
#include <stdexcept>

#ifndef _MSC_VER
#define WIDE_NOEXCEPT noexcept
#else
#define WIDE_NOEXCEPT
#endif

namespace Wide {
    namespace Semantic {
        struct Type;
        class Analyzer;
        class Error : public std::exception {
            Lexer::Range where;
        protected:
            std::string msg;
        public:
            Error(Lexer::Range loc)
                : where(loc) {}
            Lexer::Range location() { return where; }
            const char* what() const WIDE_NOEXCEPT {
                return msg.c_str();
            }
        };
        class NoMember : public Error {
            Type* which;
            std::string member;
            Type* context;
        public:
            NoMember(Type* what, Type* con, std::string name, Lexer::Range where, Analyzer& a);
            Type* GetType() { return which; }
            Type* GetContext() { return context; }
            std::string GetMember() { return member; }
        };

        class NotAType : public Error {
            Type* real_type;
        public:
            NotAType(Type* what, Lexer::Range loc, Analyzer& a);
            Type* GetType() { return real_type; }
        };

        class CantFindHeader : public Error {
            std::string which;
            std::vector<std::string> includes;
        public:
            std::string header() { return which; }
            std::vector<std::string> includepaths() { return includes; }
            CantFindHeader(std::string path, std::vector<std::string> paths, Lexer::Range loc);
        };
    }
}
