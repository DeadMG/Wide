#include <Wide/Semantic/ClangInclude.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/StringType.h>
#include <Wide/Semantic/Util.h>
#include <Wide/Codegen/Generator.h>
#include <fstream>

#pragma warning(push, 0)
#include <clang/Parse/Parser.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Sema/Sema.h>
#include <clang/Lex/Preprocessor.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FileUtilities.h>
#pragma warning(pop)

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

Wide::Util::optional<ConcreteExpression> ClangIncludeEntity::AccessMember(ConcreteExpression, std::string name, Context c) {
    if (name == "mangle") {
        if (MangleOverloadSet)
            return MangleOverloadSet->BuildValueConstruction({}, c);
        struct NameMangler : OverloadResolvable, Callable {
            unsigned GetArgumentCount() override final { return 1; }
            Type* MatchParameter(Type* t, unsigned, Analyzer& a, Type* source) override final {
                if (dynamic_cast<OverloadSet*>(t->Decay()))
                    return t;
                return nullptr;
            }
            Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
            std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
            ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final { 
                auto name = dynamic_cast<OverloadSet*>(args[0].t->Decay())->GetCPPMangledName();
                return ConcreteExpression(c->GetTypeForString(name), c->gen->CreateStringExpression(name));
            }
        };
        return (MangleOverloadSet = c->GetOverloadSet(c->arena.Allocate<NameMangler>()))->BuildValueConstruction({}, c);
    }
    if (name == "literal") {
        if (LiteralOverloadSet)
            return LiteralOverloadSet->BuildValueConstruction({}, c);
        struct LiteralIncluder : OverloadResolvable, Callable {
            unsigned GetArgumentCount() override final { return 1; }
            Type* MatchParameter(Type* t, unsigned, Analyzer& a, Type* source) override final { 
                if (dynamic_cast<StringType*>(t->Decay())) return t; 
                return nullptr; 
            }
            Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
            std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
            ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final {
                auto str = dynamic_cast<StringType*>(args[0].BuildValue(c).t);
                llvm::SmallVector<char, 30> fuck_out_parameters;
                auto error = llvm::sys::fs::createTemporaryFile("", "", fuck_out_parameters);
                if (error) throw std::runtime_error("Fuck error codes.");
                std::string path(fuck_out_parameters.begin(), fuck_out_parameters.end());
                std::ofstream file(path, std::ios::out);
                file << str->GetValue();
                file.flush();
                file.close();
                auto clangtu = c->LoadCPPHeader(std::move(path), c.where);
                return c->GetClangNamespace(*clangtu, clangtu->GetDeclContext())->BuildValueConstruction({}, c);
            }            
        };
        return (LiteralOverloadSet = c->GetOverloadSet(c->arena.Allocate<LiteralIncluder>()))->BuildValueConstruction({}, c);
    }
    if (name == "macro") {
        struct ClangMacroHandler : public MetaType {
            ConcreteExpression BuildCall(ConcreteExpression, std::vector<ConcreteExpression> args, Context c) override final{
                if (args.size() < 2)
                    throw std::runtime_error("Attempt to access a macro but no TU or macro name was passed.");
                // Should be a ClangNamespace as first argument.
                auto gnamespace = dynamic_cast<ClangNamespace*>(args[0].t->Decay());
                if (!gnamespace)
                    throw std::runtime_error("Attempted to access a macro, but the first argument was not a Clang translation unit.");
                auto str = dynamic_cast<Codegen::StringExpression*>(args[1].BuildValue(c).Expr);
                if (!str)
                    throw std::runtime_error("Attempted to access a macro, but the second argument was not a string literal.");
                auto tu = gnamespace->GetTU();
                auto&& pp = tu->GetSema().getPreprocessor();
                auto info = pp.getMacroInfo(tu->GetIdentifierInfo(str->GetContents()));
                    
                class SwallowConsumer : public clang::ASTConsumer {
                public:
                    SwallowConsumer() {}
                    bool HandleTopLevelDecl(clang::DeclGroupRef arg) { return true; }
                }; 

                SwallowConsumer consumer;
                clang::Sema s(pp, tu->GetASTContext(), consumer);
                clang::Parser p(tu->GetSema().getPreprocessor(), s, true);
                std::vector<clang::Token> tokens;
                for(std::size_t num = 0; num < info->getNumTokens(); ++num)
                    tokens.push_back(info->getReplacementToken(num));
                tokens.emplace_back();
                clang::Token& eof = tokens.back();
                eof.setKind(clang::tok::TokenKind::eof);
                eof.setLocation(tu->GetFileEnd());
                eof.setIdentifierInfo(nullptr);
                pp.EnterTokenStream(tokens.data(), 2, false, false);
                p.Initialize();
                auto expr = p.ParseExpression();
                if (expr.isUsable()) {
                    return InterpretExpression(expr.get(), *tu, c);
                }
                throw std::runtime_error("Only support constexpr integral macros right now.");
            }
        };
        return c->arena.Allocate<ClangMacroHandler>()->BuildValueConstruction({}, c);
    }
    return Wide::Util::none;
}

ConcreteExpression ClangIncludeEntity::BuildCall(ConcreteExpression e, std::vector<ConcreteExpression> args, Context c) {
    if (args.size() != 1)
        throw std::runtime_error("Attempted to call the Clang Include Entity with the wrong number of arguments.");
    auto str = dynamic_cast<StringType*>(args[0].BuildValue(c).t);
    auto name = str->GetValue();
    if (name.size() > 1 && name[0] == '<')
        name = std::string(name.begin() + 1, name.end());
    if (name.size() > 1 && name.back() == '>')
        name = std::string(name.begin(), name.end() - 1);
    auto clangtu = c->LoadCPPHeader(std::move(name), c.where);

    return c->GetClangNamespace(*clangtu, clangtu->GetDeclContext())->BuildValueConstruction({}, c);
}