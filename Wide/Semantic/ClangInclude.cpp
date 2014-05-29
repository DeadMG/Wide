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

std::unique_ptr<Expression> ClangIncludeEntity::AccessMember(std::unique_ptr<Expression> t, std::string name, Context c) {
    if (name == "literal") {
        if (LiteralOverloadSet) return BuildChain(std::move(t), LiteralOverloadSet->BuildValueConstruction({}, { this, c.where }));
        struct LiteralIncluder : OverloadResolvable, Callable {
            Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
                if (types.size() != 1) return Util::none;
                if (!dynamic_cast<StringType*>(types[0]->Decay())) return Util::none;
                return types;
            }
            Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
            std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c) override final { return args; }
            std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
                auto str = dynamic_cast<StringType*>(args[0]->GetType()->Decay());
                llvm::SmallVector<char, 30> fuck_out_parameters;
                auto error = llvm::sys::fs::createTemporaryFile("", "", fuck_out_parameters);
                if (error) throw CannotCreateTemporaryFile(c.where);
                std::string path(fuck_out_parameters.begin(), fuck_out_parameters.end());
                std::ofstream file(path, std::ios::out);
                file << str->GetValue();
                file.flush();
                file.close();
                auto clangtu = args[0]->GetType()->analyzer.LoadCPPHeader(std::move(path), c.where);
                return args[0]->GetType()->analyzer.GetClangNamespace(*clangtu, clangtu->GetDeclContext())->BuildValueConstruction(Expressions(), c);
            }            
        };
        LiteralHandler = Wide::Memory::MakeUnique<LiteralIncluder>();
        LiteralOverloadSet = analyzer.GetOverloadSet(LiteralHandler.get());
        return BuildChain(std::move(t), LiteralOverloadSet->BuildValueConstruction(Expressions(), { this, c.where }));
    }
    if (name == "macro") {
        if (MacroOverloadSet) return BuildChain(std::move(t), MacroOverloadSet->BuildValueConstruction({}, { this, c.where }));
        struct ClangMacroHandler : public OverloadResolvable, Callable {
            Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
                if (types.size() != 2) return Util::none;
                if (!dynamic_cast<ClangNamespace*>(types[0]->Decay())) return Util::none;
                if (!dynamic_cast<StringType*>(types[1]->Decay())) return Util::none;
                return types;
            }
            Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
            std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c) override final { return args; }
            std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) override final{
                auto gnamespace = dynamic_cast<ClangNamespace*>(args[0]->GetType()->Decay());
                auto str = dynamic_cast<StringType*>(args[1]->GetType()->Decay());
                assert(gnamespace && "Overload resolution picked bad candidate.");
                assert(str && "Overload resolution picked bad candidate.");
                auto tu = gnamespace->GetTU();
                return InterpretExpression(tu->ParseMacro(str->GetValue(), c.where), *tu, c, c.from->analyzer);
            }
        };
        MacroHandler = Wide::Memory::MakeUnique<ClangMacroHandler>();
        MacroOverloadSet = analyzer.GetOverloadSet(MacroHandler.get());
        return BuildChain(std::move(t), MacroOverloadSet->BuildValueConstruction(Expressions(), { this, c.where }));
    }
    if (name == "header") {
        if (HeaderOverloadSet) return HeaderOverloadSet->BuildValueConstruction({}, { this, c.where });
        struct ClangHeaderHandler : OverloadResolvable, Callable {
            Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
                if (types.size() != 1) return Util::none;
                if (!dynamic_cast<StringType*>(types[0]->Decay())) return Util::none;
                return types;
            }
            Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
            std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c) override final { return args; }
            std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
                auto str = dynamic_cast<StringType*>(args[0]->GetType()->Decay());
                auto name = str->GetValue();
                if (name.size() > 1 && name[0] == '<')
                    name = std::string(name.begin() + 1, name.end());
                if (name.size() > 1 && name.back() == '>')
                    name = std::string(name.begin(), name.end() - 1);
                auto clangtu = c.from->analyzer.LoadCPPHeader(std::move(name), c.where);
                return c.from->analyzer.GetClangNamespace(*clangtu, clangtu->GetDeclContext())->BuildValueConstruction(Expressions(), c);
            }
        };
        HeaderIncluder = Wide::Memory::MakeUnique<ClangHeaderHandler>();
        HeaderOverloadSet = analyzer.GetOverloadSet(HeaderIncluder.get());
        return BuildChain(std::move(t), HeaderOverloadSet->BuildValueConstruction(Expressions(), { this, c.where }));
    }
    return nullptr;
}

std::unique_ptr<Expression> ClangIncludeEntity::BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) {
    if (!dynamic_cast<StringType*>(args[0]->GetType()->Decay()))
        throw std::runtime_error("fuck");
    if (args.size() != 1)
        throw std::runtime_error("fuck");
    auto str = dynamic_cast<StringType*>(args[0]->GetType()->Decay());
    auto name = str->GetValue();
    if (name.size() > 1 && name[0] == '<')
        name = std::string(name.begin() + 1, name.end());
    if (name.size() > 1 && name.back() == '>')
        name = std::string(name.begin(), name.end() - 1);
    auto clangtu = analyzer.AggregateCPPHeader(std::move(name), c.where);
    return BuildChain(std::move(val), analyzer.GetClangNamespace(*clangtu, clangtu->GetDeclContext())->BuildValueConstruction(Expressions(), { this, c.where }));
}

std::string ClangIncludeEntity::explain() {
    return ".cpp";
}