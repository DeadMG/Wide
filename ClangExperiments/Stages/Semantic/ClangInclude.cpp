#include "ClangInclude.h"
#include "../Codegen/Expression.h"
#include "../Codegen/Generator.h"
#include "Analyzer.h"
#include "ClangTU.h"
#include "ClangNamespace.h"
#include "FunctionType.h"

#pragma warning(push, 0)

#include <clang/AST/Type.h>

#pragma warning(pop)


using namespace Wide;
using namespace Semantic;

Expression ClangIncludeEntity::AccessMember(Expression, std::string name, Analyzer& a) {
    if (name == "mangle") {
        struct ClangNameMangler : public MetaType {
            Expression BuildCall(Expression, std::vector<Expression> args, Analyzer& a) {
                if (args.size() != 1)
                    throw std::runtime_error("Attempt to mangle name but passed more than one object.");
                auto ty = dynamic_cast<FunctionType*>(args[0].t);
                if (!ty) throw std::runtime_error("Attempt to mangle the name of something that was not a function.");
                auto fun = dynamic_cast<Codegen::FunctionValue*>(args[0].Expr);
                if (!fun)
                    throw std::runtime_error("The argument was not a Clang mangled function name.");
                return Expression(a.GetLiteralStringType(), a.gen->CreateStringExpression(fun->GetMangledName()));
            }
        };
        return a.arena.Allocate<ClangNameMangler>()->BuildValueConstruction(a);
    }
    throw std::runtime_error("Attempted to access a member of ClangIncludeEntity that did not exist.");
}

Expression ClangIncludeEntity::BuildCall(Expression e, std::vector<Expression> args, Analyzer& a) {
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
    auto clangtu = a.LoadCPPHeader(std::move(name));

    return a.GetClangNamespace(*clangtu, clangtu->GetDeclContext())->BuildValueConstruction(a);
}