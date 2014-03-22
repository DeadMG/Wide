#include <Wide/Semantic/ClangInclude.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/SemanticError.h>
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
            Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
                if (types.size() != 1) return Util::none;
                if (!dynamic_cast<OverloadSet*>(types[0]->Decay())) return Util::none;
                return types;
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
        if (LiteralOverloadSet) return LiteralOverloadSet->BuildValueConstruction({}, c);
        struct LiteralIncluder : OverloadResolvable, Callable {
            Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
                if (types.size() != 1) return Util::none;
                if (!dynamic_cast<StringType*>(types[0]->Decay())) return Util::none;
                return types;
            }
            Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
            std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
            ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final {
                auto str = dynamic_cast<StringType*>(args[0].BuildValue(c).t);
                llvm::SmallVector<char, 30> fuck_out_parameters;
                auto error = llvm::sys::fs::createTemporaryFile("", "", fuck_out_parameters);
                if (error) throw CannotCreateTemporaryFile(c.where);
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
        if (MacroOverloadSet) return MacroOverloadSet->BuildValueConstruction({}, c);
        struct ClangMacroHandler : public OverloadResolvable, Callable {
            Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
                if (types.size() != 2) return Util::none;
                if (!dynamic_cast<ClangNamespace*>(types[0]->Decay())) return Util::none;
                if (!dynamic_cast<StringType*>(types[1]->Decay())) return Util::none;
                return types;
            }
            Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
            std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
            ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final{
                auto gnamespace = dynamic_cast<ClangNamespace*>(args[0].t->Decay());
                auto str = dynamic_cast<StringType*>(args[1].t->Decay());
                assert(gnamespace && "Overload resolution picked bad candidate.");
                assert(str && "Overload resolution picked bad candidate.");
                auto tu = gnamespace->GetTU();
                return InterpretExpression(tu->ParseMacro(str->GetValue(), c.where), *tu, c);
            }
        };
        return (MacroOverloadSet = c->GetOverloadSet(c->arena.Allocate<ClangMacroHandler>()))->BuildValueConstruction({}, c);
    }
    if (name == "header") {
        if (HeaderOverloadSet) return HeaderOverloadSet->BuildValueConstruction({}, c);
        struct ClangHeaderHandler : OverloadResolvable, Callable {
            Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
                if (types.size() != 1) return Util::none;
                if (!dynamic_cast<StringType*>(types[0]->Decay())) return Util::none;
                return types;
            }
            Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
            std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
            ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final {
                auto str = dynamic_cast<StringType*>(args[0].BuildValue(c).t);
                auto name = str->GetValue();
                if (name.size() > 1 && name[0] == '<')
                    name = std::string(name.begin() + 1, name.end());
                if (name.size() > 1 && name.back() == '>')
                    name = std::string(name.begin(), name.end() - 1);
                auto clangtu = c->LoadCPPHeader(std::move(name), c.where);
                return c->GetClangNamespace(*clangtu, clangtu->GetDeclContext())->BuildValueConstruction({}, c);
            }
        };
        return (HeaderOverloadSet = c->GetOverloadSet(c->arena.Allocate<ClangHeaderHandler>()))->BuildValueConstruction({}, c);
    }
    return Wide::Util::none;
}

ConcreteExpression ClangIncludeEntity::BuildCall(ConcreteExpression e, std::vector<ConcreteExpression> args, Context c) {
    if (!dynamic_cast<StringType*>(args[0].BuildValue(c).t))
        throw std::runtime_error("fuck");
    if (args.size() != 1)
        throw std::runtime_error("fuck");
    auto str = dynamic_cast<StringType*>(args[0].BuildValue(c).t);
    auto name = str->GetValue();
    if (name.size() > 1 && name[0] == '<')
        name = std::string(name.begin() + 1, name.end());
    if (name.size() > 1 && name.back() == '>')
        name = std::string(name.begin(), name.end() - 1);
    auto clangtu = c->AggregateCPPHeader(std::move(name), c.where);
    return c->GetClangNamespace(*clangtu, clangtu->GetDeclContext())->BuildValueConstruction({}, c);
}

std::string ClangIncludeEntity::explain(Analyzer& a) {
    return "global.cpp";
}