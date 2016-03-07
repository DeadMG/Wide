#include <Wide/Semantic/ClangInclude.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/StringType.h>
#include <Wide/Semantic/Util.h>
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

using namespace Wide;
using namespace Semantic;

std::shared_ptr<Expression> ClangIncludeEntity::AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> t, std::string name, Context c) {
    if (name == "literal") {
        struct LiteralIncluder : OverloadResolvable, Callable {
            Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Location source) override final {
                if (types.size() != 1) return Util::none;
                if (!dynamic_cast<StringType*>(types[0]->Decay())) return Util::none;
                return types;
            }
            Callable* GetCallableForResolution(std::vector<Type*>, Location, Analyzer& a) override final { return this; }
            std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final { return args; }
            std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
                auto str = dynamic_cast<String*>(args[0].get());
                llvm::SmallVector<char, 30> fuck_out_parameters;
                auto error = llvm::sys::fs::createTemporaryFile("", "", fuck_out_parameters);
                if (error) throw SpecificError<CouldNotCreateTemporaryFile>(str->a, c.where, "Could not create temporary file.");
                std::string path(fuck_out_parameters.begin(), fuck_out_parameters.end());
                std::ofstream file(path, std::ios::out);
                file << str->str;
                file.flush();
                file.close();
                auto clangtu = c.from.GetAnalyzer().LoadCPPHeader(std::move(path), c.where);
                return args[0]->GetType(key)->analyzer.GetClangNamespace(*clangtu, c.from, clangtu->GetDeclContext())->BuildValueConstruction(key, {}, c);
            }            
        };
        if (!LiteralHandler) LiteralHandler = Wide::Memory::MakeUnique<LiteralIncluder>();
        return BuildChain(std::move(t), analyzer.GetOverloadSet(LiteralHandler.get())->BuildValueConstruction(key, {}, { c.from, c.where }));
    }
    if (name == "macro") {
        struct ClangMacroHandler : public OverloadResolvable, Callable {
            Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Location source) override final {
                if (types.size() != 1) return Util::none;
                if (!dynamic_cast<StringType*>(types[0]->Decay())) return Util::none;
                return types;
            }
            Callable* GetCallableForResolution(std::vector<Type*>, Location, Analyzer& a) override final { return this; }
            std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final { return args; }
            std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final{
                auto str = dynamic_cast<String*>(args[0].get());
                if (!str) throw SpecificError<MacroNameNotConstant>(str->a, c.where, "Failed to evaluate macro: name was not a constant expression.");
                auto tu = c.from.GetAnalyzer().GetAggregateTU();
                return InterpretExpression(tu->ParseMacro(str->str, c.where), *tu, c, c.from.GetAnalyzer());
            }
        };
        if (!MacroHandler) MacroHandler = Wide::Memory::MakeUnique<ClangMacroHandler>();
        return BuildChain(std::move(t), analyzer.GetOverloadSet(MacroHandler.get())->BuildValueConstruction(key, {}, { c.from, c.where }));
    }
    if (name == "header") {
        struct ClangHeaderHandler : OverloadResolvable, Callable {
            Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Location source) override final {
                if (types.size() != 1) return Util::none;
                if (!dynamic_cast<StringType*>(types[0]->Decay())) return Util::none;
                return types;
            }
            Callable* GetCallableForResolution(std::vector<Type*>, Location, Analyzer& a) override final { return this; }
            std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final { return args; }
            std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
                auto str = dynamic_cast<String*>(args[0].get());
                auto name = str->str;
                if (name.size() > 1 && name[0] == '<')
                    name = std::string(name.begin() + 1, name.end());
                if (name.size() > 1 && name.back() == '>')
                    name = std::string(name.begin(), name.end() - 1);
                auto clangtu = c.from.GetAnalyzer().LoadCPPHeader(std::move(name), c.where);
                return c.from.GetAnalyzer().GetClangNamespace(*clangtu, c.from, clangtu->GetDeclContext())->BuildValueConstruction(key, {}, c);
            }
        };
        if (!HeaderIncluder) HeaderIncluder = Wide::Memory::MakeUnique<ClangHeaderHandler>();
        return BuildChain(std::move(t), analyzer.GetOverloadSet(HeaderIncluder.get())->BuildValueConstruction(key, {}, { c.from, c.where }));
    }
    auto clangtu = analyzer.GetAggregateTU();
    auto _namespace = analyzer.GetClangNamespace(*clangtu, c.from, clangtu->GetDeclContext())->BuildValueConstruction(key, {}, c);
    return Wide::Semantic::Type::AccessMember(key, _namespace, name, c);
}

std::string ClangIncludeEntity::explain() {
    return ".cpp";
}