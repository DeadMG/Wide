#pragma once

#include <Wide/Lexer/Token.h>
#include <Wide/Parser/AST.h>
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
            Analyzer* a;
        protected:
            Error(Analyzer& a, Lexer::Range loc, std::string err);
            Error(const Error&);
            Error(Error&&);
        public:
            Lexer::Range location();
            const char* what() const WIDE_NOEXCEPT {
                return msg.c_str();
            }
            ~Error();
        };
        template<typename T> class SpecificError : public Error {
        public:
            SpecificError(Analyzer& a, Lexer::Range loc, std::string err)
                : Error(a, loc, err) {}
            SpecificError(const SpecificError& other)
                : Error(other) {}
            SpecificError(SpecificError&& other)
                : Error(std::move(other)) {}
        };
        struct SizeOverrideTooSmall {};
        struct AlignOverrideTooSmall {};
        struct ExportNonOverloadSet {};
        struct FunctionArgumentNotType {};
        struct ExplicitThisNoMember {};
        struct ExplicitThisDoesntMatchMember {};
        struct ExplicitThisNotFirstArgument {};
        struct TemplateArgumentNotAType {};
        struct ImportIdentifierLookupAmbiguous {};
        struct IdentifierLookupAmbiguous {};
        struct CouldNotCreateTemporaryFile {};
        struct MacroNameNotConstant {};
        struct FileNameNotConstant {};
        struct CPPWrongArgumentNumber {};
        struct AmbiguousCPPLookup {};
        struct UnknownCPPDecl {};
        struct TemplateTypeNoCPPConversion {};
        struct TemplateArgumentNoConversion {};
        struct CouldNotInstantiateTemplate {};
        struct CouldNotFindCPPFile {};
        struct CouldNotTranslateCPPFile {};
        struct CPPFileContainedErrors {};
        struct CouldNotFindMacro {};
        struct MacroNotValidExpression {};
        struct ArrayWrongArgumentNumber {};
        struct ArrayNotConstantSize {};
        struct NoMemberFound {};
        struct IdentifierLookupFailed {};
        struct DynamicCastNotPolymorphicType {};
        struct VTableLayoutIncompatible {};
        struct UnimplementedDynamicCast {};
        struct AddressOfNonLvalue {};
        struct CouldNotInferReturnType {};
        struct InvalidExportSignature {};
        struct ContinueNoControlFlowStatement {};
        struct BreakNoControlFlowStatement {};
        struct VariableAlreadyDefined {};
        struct ExplicitReturnNoType {};
        struct NoMemberToInitialize {};
        struct NoBaseToInitialize {};
        struct InitializerNotType {};
        struct UsingTargetNotConstant {};
        struct OverloadResolutionFailed {};
        struct UnsupportedBinaryExpression {};
        struct UnsupportedDeclarationExpression {};
        struct UnsupportedMacroExpression {};
        struct BaseNotAType {};
        struct RecursiveBase {};
        struct BaseFinal {};
        struct MemberNotAType {};
        struct RecursiveMember {};
        struct AmbiguousMemberLookup {};
        struct ExportNotSingleFunction {};
    }
}
