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
        struct ClangDiagnostic {
            enum Severity {
                Ignored,
                Note,
                Remark,
                Warning,
                Error,
                Fatal
            };
            Severity severity;
            std::string what;
            std::vector<Lexer::Range> where;
        };
        class Error : public std::exception {
            Lexer::Range where;
            std::string msg;
        public:
            Error(Lexer::Range loc, std::string err);
            Error(const Error&) = default;
            Error(Error&&) = default;
            Lexer::Range location() const { return where; }
            const char* what() const WIDE_NOEXCEPT {
                return msg.c_str();
            }
        };
        class AnalyzerError : public Error {
            Analyzer* a;
        protected:
            AnalyzerError(Analyzer& a, Lexer::Range loc, std::string err);
            AnalyzerError(const AnalyzerError&);
            AnalyzerError(AnalyzerError&&);
        public:
            ~AnalyzerError();
            void disconnect();
        };
        template<typename T> class SpecificError : public AnalyzerError {
        public:
            SpecificError(Analyzer& a, Lexer::Range loc, std::string err)
                : AnalyzerError(a, loc, err) {}
            SpecificError(const SpecificError& other) = default;
            SpecificError(SpecificError&& other) = default;
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
        struct AlignmentOverrideNotInteger {};
        struct AlignmentOverrideNotConstant {};
        struct SizeOverrideNotInteger {};
        struct SizeOverrideNotConstant {};
        struct ImportNotUnambiguousBase {};
        struct ImportNotOverloadSet {};
        struct ImportNotAType {};
        struct VirtualOverrideAmbiguous {};
        struct ClangCouldNotInterpretPath {};
    }
}
