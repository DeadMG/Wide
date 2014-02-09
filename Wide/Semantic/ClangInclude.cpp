#include <Wide/Semantic/ClangInclude.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Codegen/Generator.h>

#pragma warning(push, 0)
#include <clang/Parse/Parser.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Sema/Sema.h>
#include <clang/Lex/Preprocessor.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

Wide::Util::optional<ConcreteExpression> ClangIncludeEntity::AccessMember(ConcreteExpression, std::string name, Context c) {
    if (name == "mangle") {
        struct ClangNameMangler : public MetaType {
            ConcreteExpression BuildCall(ConcreteExpression, std::vector<ConcreteExpression> args, Context c) override {
                if (args.size() != 1)
                    throw std::runtime_error("Attempt to mangle name but passed more than one object.");
                auto ty = dynamic_cast<FunctionType*>(args[0].t);
                if (!ty) throw std::runtime_error("Attempt to mangle the name of something that was not a function.");
                auto fun = dynamic_cast<Codegen::FunctionValue*>(args[0].Expr);
                if (!fun)
                    throw std::runtime_error("The argument was not a Clang mangled function name.");
                return ConcreteExpression(c->GetLiteralStringType(), c->gen->CreateStringExpression(fun->GetMangledName()));
            }
        };
        return c->arena.Allocate<ClangNameMangler>()->BuildValueConstruction(c);
    }
    if (name == "macro") {
        struct ClangMacroHandler : public MetaType {
            ConcreteExpression BuildCall(ConcreteExpression, std::vector<ConcreteExpression> args, Context c) override {
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
                    if (expr.get()->isIntegerConstantExpr(tu->GetASTContext())) {
                        llvm::APSInt out;
                        if (expr.get()->EvaluateAsInt(out, tu->GetASTContext())) {
                            if (out.getBitWidth() == 1)
                                return ConcreteExpression(c->GetBooleanType(), c->gen->CreateIntegralExpression(out.getLimitedValue(1), false, c->GetBooleanType()->GetLLVMType(*c)));
                            auto ty = c->GetIntegralType(out.getBitWidth(), out.isSigned());
                            return ConcreteExpression(ty, c->gen->CreateIntegralExpression(out.getLimitedValue(), out.isSigned(), ty->GetLLVMType(*c)));
                        }
                    }
                }
                throw std::runtime_error("Only support constexpr integral macros right now.");
            }
        };
        return c->arena.Allocate<ClangMacroHandler>()->BuildValueConstruction(c);
    }
    return Wide::Util::none;
}

ConcreteExpression ClangIncludeEntity::BuildCall(ConcreteExpression e, std::vector<ConcreteExpression> args, Context c) {
    if (args.size() != 1)
        throw std::runtime_error("Attempted to call the Clang Include Entity with the wrong number of arguments.");
    auto expr = dynamic_cast<Codegen::StringExpression*>(args[0].Expr);
    if (!expr)
        throw std::runtime_error("Attempted to call the Clang Include Entity with something other than a literal string.");
    auto name = expr->GetContents();
    if (name.size() > 1 && name[0] == '<')
        name = std::string(name.begin() + 1, name.end());
    if (name.size() > 1 && name.back() == '>')
        name = std::string(name.begin(), name.end() - 1);
    auto clangtu = c->LoadCPPHeader(std::move(name), c.where);

    return c->GetClangNamespace(*clangtu, clangtu->GetDeclContext())->BuildValueConstruction(c);
}