#include <Wide/Semantic/ClangInclude.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Codegen/Generator.h>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

Wide::Util::optional<Expression> ClangIncludeEntity::AccessMember(ConcreteExpression, std::string name, Context c) {
    if (name == "mangle") {
        struct ClangNameMangler : public MetaType {
            Expression BuildCall(ConcreteExpression, std::vector<ConcreteExpression> args, Context c) override {
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
    return Wide::Util::none;
}

Expression ClangIncludeEntity::BuildCall(ConcreteExpression e, std::vector<ConcreteExpression> args, Context c) {
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