#include <Wide/CAPI/Lexer.h>
#include <Wide/Parser/Parser.h>

namespace CEquivalents {
    struct LexerResult {
        Range location;
        Wide::Lexer::TokenType type;
        char* value;
        bool exists;
    };
    struct ParserLexer {
        void* context;
        std::add_pointer<CEquivalents::LexerResult(void*)>::type TokenCallback;
        Wide::Lexer::Range lastpos;
        std::deque<Wide::Lexer::Token> putback;
        Wide::Util::optional<Wide::Lexer::Token> operator()() {
            if (!putback.empty()) {
                auto val = putback.back();
                putback.pop_back();
                lastpos = val.GetLocation();
                return std::move(val);
            }
            auto tok = TokenCallback(context);
            if (tok.exists) {
                lastpos = tok.location;
                return Wide::Lexer::Token(tok.location, tok.type, tok.value);
            }
            return Wide::Util::none;
        }
        void operator()(Wide::Lexer::Token t) {
            putback.push_back(std::move(t));
        }
        Wide::Lexer::Range GetLastPosition() {
            return lastpos;
        }
    };

    enum OutliningType {
        Module,
        Function,
        Type
    };
    
    class Builder {
    public:     
        Wide::Lexer::Range CreateIdentExpression(std::string name, Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateStringExpression(std::string val, Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateMemberAccessExpression(std::string mem, Wide::Lexer::Range e, Wide::Lexer::Range r) { return e + r; }
        Wide::Lexer::Range CreateLeftShiftExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs) { return lhs + rhs; }
        Wide::Lexer::Range CreateFunctionCallExpression(Wide::Lexer::Range e, std::nullptr_t, Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateReturn(Wide::Lexer::Range expr, Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateReturn(Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateVariableStatement(std::string name, Wide::Lexer::Range value, Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateVariableStatement(std::string name, Wide::Lexer::Range r) { return r; }
        Wide::Lexer::Range CreateAssignmentExpression(Wide::Lexer::Range lhs, Wide::Lexer::Range rhs, Wide::Lexer::TokenType) { return lhs + rhs; }
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
        Wide::Lexer::Range CreateModulusExpression(Wide::Lexer::Range l, Wide::Lexer::Range r) { return l + r; }
        Wide::Lexer::Range CreateSubtractionExpression(Wide::Lexer::Range l, Wide::Lexer::Range r) { return l + r; }
        Wide::Lexer::Range CreateDivisionExpression(Wide::Lexer::Range l, Wide::Lexer::Range r) { return l + r; }

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
        Wide::Lexer::Range CreateErrorExpression(Wide::Lexer::Range where) { return where; }

        std::nullptr_t CreateStatementGroup() { return nullptr; }
        std::nullptr_t CreateExpressionGroup() { return nullptr; }
        std::nullptr_t CreateCaptureGroup() { return nullptr; }
        std::nullptr_t CreateInitializerGroup() { return CreateCaptureGroup(); }
        std::nullptr_t CreateFunctionArgumentGroup() { return nullptr; }
        
        Wide::Lexer::Range CreateModule(std::string val, Wide::Lexer::Range p, Wide::Lexer::Range r) { return r; }
        std::nullptr_t CreateUsingDefinition(std::string val, Wide::Lexer::Range expr, Wide::Lexer::Range p) { return nullptr; }
        void CreateFunction(std::string name, std::nullptr_t, std::nullptr_t, Wide::Lexer::Range r, Wide::Lexer::Range p, std::nullptr_t, std::nullptr_t) { OutliningCallback(r, OutliningType::Function); }
        void CreateOverloadedOperator(Wide::Lexer::TokenType name, std::nullptr_t, std::nullptr_t, Wide::Lexer::Range r, Wide::Lexer::Range p, std::nullptr_t) { OutliningCallback(r, OutliningType::Function); }
        Wide::Lexer::Range CreateType(std::string name, Wide::Lexer::Range p, Wide::Lexer::Range r) { return r; }       
        Wide::Lexer::Range CreateType(std::string name, Wide::Lexer::Range r) { return r; }

        void AddTypeField(Wide::Lexer::Range r, Wide::Lexer::Range) {}
        void AddArgumentToFunctionGroup(std::nullptr_t&, std::string, Wide::Lexer::Range) {}
        void AddArgumentToFunctionGroup(std::nullptr_t&, std::string) {}
        void AddCaptureToGroup(std::nullptr_t& l, Wide::Lexer::Range) {}
        void AddInitializerToGroup(std::nullptr_t& l, Wide::Lexer::Range b) { return AddCaptureToGroup(l, b); }
        void AddStatementToGroup(std::nullptr_t, Wide::Lexer::Range r) {}
        void SetTypeEndLocation(Wide::Lexer::Range& r, Wide::Lexer::Range val) {  r = r + val; OutliningCallback(r, OutliningType::Type); }
        void SetModuleEndLocation(Wide::Lexer::Range& r, Wide::Lexer::Range val) { r = r + val; OutliningCallback(r, OutliningType::Module); }
        void AddExpressionToGroup(std::nullptr_t, Wide::Lexer::Range r) {}

        Wide::Lexer::Range GetLocation(Wide::Lexer::Range s) {
            return s;
        }
        
        typedef Wide::Lexer::Range StatementType;
        typedef Wide::Lexer::Range ExpressionType;

        std::function<void(Wide::Lexer::Range r, CEquivalents::OutliningType type)> OutliningCallback;
        std::function<void(Wide::Lexer::Range, Wide::Parser::Error)> ErrorCallback;
        std::function<void(Wide::Lexer::Range, Wide::Parser::Warning)> WarningCallback;

        void Error(Wide::Lexer::Range where, Wide::Parser::Error what) {
            ErrorCallback(where, what);
        }
        void Warning(Wide::Lexer::Range where, Wide::Parser::Warning what) {
            WarningCallback(where, what);
        }
    };
}

extern "C" __declspec(dllexport) void ParseWide(
    void* context,
    std::add_pointer<CEquivalents::LexerResult(void*)>::type TokenCallback,
    std::add_pointer<void(CEquivalents::Range, CEquivalents::OutliningType, void*)>::type OutliningCallback,
    std::add_pointer<void(CEquivalents::Range, Wide::Parser::Error, void*)>::type ErrorCallback,
    std::add_pointer<void(CEquivalents::Range, Wide::Parser::Warning, void*)>::type WarningCallback
) {
    CEquivalents::Builder builder;
    builder.OutliningCallback = [=](Wide::Lexer::Range r, CEquivalents::OutliningType t) { return OutliningCallback(r, t, context); };
    builder.ErrorCallback = [=](Wide::Lexer::Range where, Wide::Parser::Error what) { return ErrorCallback(where, what, context); };
    builder.WarningCallback = [=](Wide::Lexer::Range where, Wide::Parser::Warning what) { return WarningCallback(where, what, context); };
    CEquivalents::ParserLexer pl;
    pl.context = context;
    pl.TokenCallback = TokenCallback;
    try {
        Wide::Parser::ParseGlobalModuleContents(pl, builder, Wide::Lexer::Range());
    } catch(Wide::Parser::ParserError& e) {
        ErrorCallback(e.where(), e.error(), context);
    } catch(...) {
    }
}

extern "C" __declspec(dllexport) const char* GetParserErrorString(Wide::Parser::Error err) {
    if (Wide::Parser::ErrorStrings.find(err) == Wide::Parser::ErrorStrings.end())
        return nullptr;
    return Wide::Parser::ErrorStrings.at(err).c_str();
}

extern "C" __declspec(dllexport) const char* GetParserWarningString(Wide::Parser::Warning err) {
    if (Wide::Parser::WarningStrings.find(err) == Wide::Parser::WarningStrings.end())
        return nullptr;
    return Wide::Parser::WarningStrings.at(err).c_str();
}