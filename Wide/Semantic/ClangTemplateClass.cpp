#include <Wide/Semantic/ClangTemplateClass.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/SemanticError.h>

#pragma warning(push, 0)
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Type.h>
#include <clang/Sema/Sema.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

std::shared_ptr<Expression> ClangTemplateClass::ConstructCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c){
    clang::TemplateArgumentListInfo tl;
    std::vector<Type*> types;
    std::list<clang::IntegerLiteral> literals;
    for (auto&& x : args) {
        if (auto con = dynamic_cast<ConstructorType*>(x->GetType())) {
            auto clangty = con->GetConstructedType()->GetClangType(*from);
            if (!clangty) throw SpecificError<TemplateTypeNoCPPConversion>(analyzer, c.where, "Template argument type has no C++ equivalent type.");
            auto tysrcinfo = from->GetASTContext().getTrivialTypeSourceInfo(*clangty);

            tl.addArgument(clang::TemplateArgumentLoc(clang::TemplateArgument(*clangty), tysrcinfo));
            types.push_back(con->GetConstructedType());
            continue;
        }
        if (auto in = dynamic_cast<Integer*>(x.get())) {
            literals.emplace_back(from->GetASTContext(), in->value, from->GetASTContext().LongLongTy, clang::SourceLocation());
            tl.addArgument(clang::TemplateArgumentLoc(clang::TemplateArgument(&literals.back()), clang::TemplateArgumentLocInfo(&literals.back())));
            types.push_back(x->GetType());
            continue;
        }
        throw SpecificError<TemplateArgumentNoConversion>(analyzer, c.where, "Template argument was not a type or integer.");
    }

    llvm::SmallVector<clang::TemplateArgument, 10> tempargs;
    if (from->GetSema().CheckTemplateArgumentList(tempdecl, tempdecl->getLocation(), tl, false, tempargs))
        throw SpecificError<CouldNotInstantiateTemplate>(analyzer, c.where, "Could not instantiate C++ template.");

    void* pos = 0;
    auto spec = tempdecl->findSpecialization(tempargs, pos);
    if (spec)
        return analyzer.GetConstructorType(analyzer.GetClangType(*from, from->GetASTContext().getRecordType(spec)))->BuildValueConstruction({}, { l, c.where });
    auto loc = from->GetFileEnd();
    if (!spec) {
        spec = clang::ClassTemplateSpecializationDecl::Create(
            from->GetASTContext(),
            clang::TagTypeKind::TTK_Class,
            from->GetDeclContext(),
            loc,
            loc,
            tempdecl,
            tempargs.data(),
            tempargs.size(),
            0
            );
        tempdecl->AddSpecialization(spec, pos);
    }
    spec->setLexicalDeclContext(from->GetDeclContext());
    from->GetDeclContext()->addDecl(spec);
    auto tsk = clang::TemplateSpecializationKind::TSK_ExplicitInstantiationDefinition;

    if (!spec->getDefinition())
        if (from->GetSema().InstantiateClassTemplateSpecialization(loc, spec, tsk))
            throw SpecificError<CouldNotInstantiateTemplate>(analyzer, c.where, "Could not instantiate C++ template.");

    return BuildChain(std::move(val), analyzer.GetConstructorType(analyzer.GetClangType(*from, from->GetASTContext().getRecordType(spec)))->BuildValueConstruction({}, { l, c.where }));
}
std::string ClangTemplateClass::explain() {
    return boost::get<Location::CppLocation>(l.location).namespaces.back()->explain() + "." + tempdecl->getName().str();
} 