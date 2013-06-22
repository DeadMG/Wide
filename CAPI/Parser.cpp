#include "Lexer.h"
#include "Stages/Parser/Parser.h"

namespace CEquivalents {
    struct ParserLexer {
        CEquivalents::LexerBody* lexer;
        Wide::Util::optional<Wide::Lexer::Token> operator()() {
            auto tok = lexer->inv();
            if (tok && lexer->TokenCallback(tok->GetLocation(), tok->GetValue().c_str(), tok->GetType()))
                return tok;
            return Wide::Util::none;
        }
        void operator()(Wide::Lexer::Token t) {
            lexer->inv(std::move(t));
        }
    };

    enum OutliningType {
        Module,
        Function,
        Type
    };
    
    class Builder {
    public:     
        Builder(std::function<void(Wide::Lexer::Range, OutliningType)> outlining)
            : OutliningCallback(std::move(outlining)) {}

        Wide::Lexer::Range CreateIdentExpression(std::string name, Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateStringExpression(std::string val, Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateMemberAccessExpression(std::string mem, Wide::Lexer::Range e, Wide::Lexer::Range r) { return e + r; }
        Wide::Lexer::Range CreateLeftShiftExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateFunctionCallExpression(Wide::Lexer::Range e, std::nullptr_t, Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateReturn(Wide::Lexer::Range expr, Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateReturn(Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateVariableStatement(std::string name, Wide::Lexer::Range value, Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateVariableStatement(std::string name, Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateAssignmentExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateIntegerExpression(std::string val, Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateRightShiftExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateIfStatement(Wide::Lexer::Range cond, Wide::Lexer::Range true_br, Wide::Lexer::Range false_br, Wide::Lexer::Range loc) { return loc; }
        Wide::Lexer::Range CreateIfStatement(Wide::Lexer::Range cond, Wide::Lexer::Range true_br, Wide::Lexer::Range loc) { return loc; }
        Wide::Lexer::Range CreateCompoundStatement(std::nullptr_t true_br, Wide::Lexer::Range loc) { return loc; }
        Wide::Lexer::Range CreateEqCmpExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateNotEqCmpExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateMetaFunctionCallExpression(Wide::Lexer::Range, std::nullptr_t, Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateWhileStatement(Wide::Lexer::Range cond, Wide::Lexer::Range body, Wide::Lexer::Range loc) { return loc; }
        Wide::Lexer::Range CreateThisExpression(Wide::Lexer::Range loc) { return loc; }
        Wide::Lexer::Range CreateLambda(std::nullptr_t args, std::nullptr_t body, Wide::Lexer::Range loc, bool defaultref, std::nullptr_t caps) { return loc; }
        Wide::Lexer::Range CreateNegateExpression(Wide::Lexer::Range e, Wide::Lexer::Range loc) { return e + loc; }
        Wide::Lexer::Range CreateDereferenceExpression(Wide::Lexer::Range e, Wide::Lexer::Range loc) { return e + loc; }
        Wide::Lexer::Range CreatePointerAccessExpression(std::string mem, Wide::Lexer::Range e, Wide::Lexer::Range r) { return e + r; }

        Wide::Lexer::Range CreateOrExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateXorExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateAndExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateLTExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateLTEExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateGTExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateGTEExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreatePrefixIncrement(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreatePostfixIncrement(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateAdditionExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateMultiplyExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateAutoExpression(Wide::Lexer::Range loc) { return loc; }
        Wide::Lexer::Range CreatePrefixDecrement(Wide::Lexer::Range ex, Wide::Lexer::Range r) { return ex + r; }
        Wide::Lexer::Range CreatePostfixDecrement(Wide::Lexer::Range ex, Wide::Lexer::Range r) { return ex + r; }
        Wide::Lexer::Range CreateAddressOf(Wide::Lexer::Range ex, Wide::Lexer::Range r) { return ex + r; }

        std::nullptr_t CreateStatementGroup() { return nullptr; }
        std::nullptr_t CreateExpressionGroup() { return nullptr; }
        std::nullptr_t CreateCaptureGroup() { return nullptr; }
        std::nullptr_t CreateInitializerGroup() { return CreateCaptureGroup(); }
        std::nullptr_t CreateFunctionArgumentGroup() { return nullptr; }
        
        std::nullptr_t CreateModule(std::string val, std::nullptr_t p) { return nullptr; }
        std::nullptr_t CreateUsingDefinition(std::string val, Wide::Lexer::Range expr, std::nullptr_t p) { return nullptr; }
        void CreateFunction(std::string name, std::nullptr_t, std::nullptr_t, Wide::Lexer::Range r, std::nullptr_t p, std::nullptr_t, std::nullptr_t) {}
        void CreateFunction(std::string name, std::nullptr_t, std::nullptr_t, Wide::Lexer::Range r, Wide::Lexer::Range p, std::nullptr_t, std::nullptr_t) {}
        Wide::Lexer::Range CreateType(std::string name, std::nullptr_t p, Wide::Lexer::Range r) { return r; }        
        Wide::Lexer::Range CreateType(std::string name, Wide::Lexer::Range r) { return r; }

        void AddTypeField(Wide::Lexer::Range r, Wide::Lexer::Range) {}
        void AddArgumentToFunctionGroup(std::nullptr_t&, std::string, Wide::Lexer::Range) {}
        void AddArgumentToFunctionGroup(std::nullptr_t&, std::string) {}
        void AddCaptureToGroup(std::nullptr_t& l, Wide::Lexer::Range) {}
        void AddInitializerToGroup(std::nullptr_t& l, Wide::Lexer::Range b) { return AddCaptureToGroup(l, b); }
        void AddStatementToGroup(std::nullptr_t, Wide::Lexer::Range r) {}
        void SetTypeEndLocation(Wide::Lexer::Range& r, Wide::Lexer::Range val) { r = r + val; OutliningCallback(r, OutliningType::Type); }
        void AddExpressionToGroup(std::nullptr_t, Wide::Lexer::Range r) {}

        Wide::Lexer::Range GetLocation(Wide::Lexer::Range s) {
            return s;
        }

        typedef Wide::Lexer::Range StatementType;
        typedef Wide::Lexer::Range ExpressionType;

        std::function<void(Wide::Lexer::Range r, CEquivalents::OutliningType type)> OutliningCallback;
    };
}

extern "C" __declspec(dllexport) void Parse(
    CEquivalents::LexerBody* lexer,
    std::add_pointer<void(CEquivalents::Range r, CEquivalents::OutliningType)>::type OutliningCallback
) {
    CEquivalents::ParserLexer pl;
    pl.lexer = lexer;
    try {
        Wide::Parser::ParseGlobalModuleContents(pl, CEquivalents::Builder([=](Wide::Lexer::Range r, CEquivalents::OutliningType t) { return OutliningCallback(r, t); }), nullptr);
    } catch(...) {}
}