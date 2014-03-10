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
            std::string msg;
        public:
            Error(Lexer::Range loc, std::string err)
                : where(loc), msg(err) {}
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

        class MacroNotValidExpression : public Error {
            std::string expanded;
            std::string name;
        public:
            std::string GetExpandedMacro() { return expanded; }
            std::string GetName() { return name; }
            MacroNotValidExpression(std::string expanded, std::string name, Lexer::Range where);
        };

        class CannotCreateTemporaryFile : public Error {
        public:
            CannotCreateTemporaryFile(Lexer::Range where);
        };

        class UnqualifiedLookupFailure : public Error {
            std::string member;
            Type* context;
        public:
            UnqualifiedLookupFailure(Type* con, std::string name, Lexer::Range where, Analyzer& a);
            Type* GetContext() { return context; }
            std::string GetMember() { return member; }
        };

        class ClangLookupAmbiguous : public Error {
            std::string name;
            Type* which;
        public:
            ClangLookupAmbiguous(std::string name, Type* what, Lexer::Range where, Analyzer& a);
            std::string GetName() { return name; }
            Type* GetObject() { return which; }
        };

        class ClangUnknownDecl : public Error {
            std::string name;
            Type* which;
        public:
            ClangUnknownDecl(std::string name, Type* what, Lexer::Range where, Analyzer& a);
            std::string GetName() { return name; }
            Type* GetType() { return which; }
        };

        class InvalidTemplateArgument : public Error {
            Type* type;
        public:
            InvalidTemplateArgument(Type* t, Lexer::Range where, Analyzer& a);
            Type* GetType() { return type; }
        };

        class UnresolvableTemplate : public Error {
            Type* temp;
            std::vector<Type*> arguments;
            std::string clangdiag;
        public:
            UnresolvableTemplate(Type*, std::vector<Type*>, std::string diag, Lexer::Range, Analyzer& a);
            Type* GetTemplate() { return temp; }
            std::vector<Type*> GetArgumentTypes() { return arguments; }
            std::string GetClangDiagnostic() { return clangdiag; }
        };

        class UninstantiableTemplate : public Error {
        public:
            UninstantiableTemplate(Lexer::Range);
        };

        class CannotTranslateFile : public Error {
            std::string filepath;
        public:
            CannotTranslateFile(std::string filepath, Lexer::Range);
            std::string GetFile() { return filepath; }
        };

        class IncompleteClangType : public Error {
            Type* which;
        public:
            IncompleteClangType(Type* what, Lexer::Range where, Analyzer& a);
            Type* GetType() { return which; }
        };

        class ClangFileParseError : public Error {
            std::string filename;
            std::string errors;
        public:
            ClangFileParseError(std::string f, std::string e, Lexer::Range where);
            std::string GetFilename() { return filename; }
            std::string GetErrors() { return errors; }
        };

        class InvalidBase : public Error {
            Type* base;
        public:
            InvalidBase(Type* t, Lexer::Range where, Analyzer& a);
            Type* GetBaseType() { return base; }
        };

        class AmbiguousLookup : public Error {
            std::string name;
            Type* base1;
            Type* base2;
        public:
            AmbiguousLookup(std::string name, Type* b1, Type* b2, Lexer::Range where, Analyzer& a);
            std::string GetName();
            Type* GetFirstBase() { return base1; }
            Type* GetSecondBase() { return base2; }
        };

        class NoBooleanConversion : public Error {
            Type* object;
        public:
            NoBooleanConversion(Type* obj, Lexer::Range r, Analyzer& a);
            Type* GetObjectType() { return object; }
        };

        class AddressOfNonLvalue : public Error {
            Type* obj;
        public:
            AddressOfNonLvalue(Type* obj, Lexer::Range r, Analyzer& a);
            Type* GetObjectType() { return obj; }
        };

        class DecltypeArgumentMismatch : public Error {
            unsigned num;
        public:
            DecltypeArgumentMismatch(unsigned count, Lexer::Range where);
            unsigned GetNumArguments() { return num; }
        };
    }
}
